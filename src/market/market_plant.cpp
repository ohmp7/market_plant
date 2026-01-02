#include "endian.h"
#include "event.h"
#include "market_plant.h"
#include "moldudp64.h"
#include "market_cli.h"
#include <iomanip>

#include <arpa/inet.h>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>

#include <memory>
#include <cstring>
#include <fstream>
#include <iostream>
#include <deque>
#include <map>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <condition_variable>
#include <unordered_map>
#include <stdexcept>
#include <string>

#include "market_plant/market_plant.pb.h"

constexpr std::uint16_t market_port = 9001;
const std::string market_ip = "127.0.0.1";

std::string SessionGenerator::Generate() {
    std::string session_key(16, '\0');
    std::lock_guard<std::mutex> lock(generator_mutex_);
    for (Bytes i = 0; i < session_key.size(); i += 8) {
        std::uint64_t n = byte_generator_();
        std::memcpy(session_key.data() + i, &n, 8);
    }
    return session_key;
};

OrderBook::OrderBook(InstrumentId id, const Depth depth) : id_(id), depth_(depth) {}
    
// TODO: Combine AddOrder() and RemoveOrder() with PushEventToSubscribers()
void OrderBook::AddOrder(Side side, Price price, Quantity quantity) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (side == Side::BID) {
        UpdateLevel(bids_, price, quantity);
    } else {
        UpdateLevel(asks_, price, quantity);
    }
}

void OrderBook::RemoveOrder(Side side, Price price, Quantity quantity) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (side == Side::BID) {
        auto it = bids_.find(price);
        if (it == bids_.end()) return;
        ModifyLevel(bids_, it, quantity);
    } else {
        auto it = asks_.find(price);
        if (it == asks_.end()) return;
        ModifyLevel(asks_, it, quantity);
    }
}

void OrderBook::PushEventToSubscribers(std::shared_ptr<const ms::OrderBookUpdate> event) {
    std::vector<std::shared_ptr<Subscriber>> to_enqueue;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        to_enqueue.reserve(subscriptions_.size());

        // Get Subscribers for corresponding InstrumentID
        for (auto it = subscriptions_.begin(); it != subscriptions_.end(); ) {
            if (auto sub = it->second.lock()) {
                to_enqueue.push_back(std::move(sub));
                ++it;
            } else {
                // subscriber already destroyed, so prune subscription entry
                it = subscriptions_.erase(it);
            }
        }
    }

    for (const auto& sub : to_enqueue) sub->Enqueue(event);
}

void OrderBook::InitializeSubscription(std::shared_ptr<Subscriber> subscriber) {
    ms::SnapshotUpdate snapshot;
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Add Subscriber to subscriptions_
    subscriptions_[subscriber->get_subscriber().subscriber_id] = subscriber;
    
    // Add snapshot to subscriber's queue
    Snapshot(snapshot);
    subscriber->Enqueue(MarketPlantServer::ConstructSnapshot(id_, snapshot));

    // This way, any new feed for this instruement is blocked until we get the snapshot in
}

void OrderBook::Snapshot(ms::SnapshotUpdate &snapshot) {
    // INVARIANT: Caller must hold mutex
    snapshot.mutable_bids()->Reserve(static_cast<int>(depth_));
    snapshot.mutable_asks()->Reserve(static_cast<int>(depth_));

    // Top-Depth Bids
    auto b_it = bids_.begin();
    for (Depth i = 0; i < depth_ && b_it != bids_.end(); ++i, ++b_it) {
        auto* bid = snapshot.add_bids();
        bid->set_type(ms::ADD_LEVEL);
        auto* level = bid->mutable_level();
        level->set_side(ms::BID);
        level->set_price(b_it->first);
        level->set_quantity(b_it->second);
    }

    // Top-Depth Asks
    auto a_it = asks_.begin();
    for (Depth i = 0; i < depth_ && a_it != asks_.end(); ++i, ++a_it) {
        auto* ask = snapshot.add_asks();
        ask->set_type(ms::ADD_LEVEL);
        auto* level = ask->mutable_level();
        level->set_side(ms::ASK);
        level->set_price(a_it->first);
        level->set_quantity(a_it->second);
    }
}

BookManager::BookManager(const InstrumentConfig& instruments) {
    books_.reserve(instruments.size());
    
    for (const auto& instrument : instruments) {
        books_.try_emplace(instrument.id, instrument.id, instrument.depth);
    }
}

OrderBook& BookManager::book(InstrumentId id) {
    auto it = books_.find(id);
    if (it == books_.end()) {
        throw std::runtime_error("BookManager::book: unknown instrument id " + std::to_string(id));
    }
    return it->second;
}

const OrderBook& BookManager::book(InstrumentId id) const {
    auto it = books_.find(id);
    if (it == books_.end()) {
        throw std::runtime_error("BookManager::book: unknown instrument id " + std::to_string(id));
    }
    return it->second;
}


ExchangeFeed::ExchangeFeed(const Exchange& exchange, BookManager& books)
    : sockfd_(socket(AF_INET, SOCK_DGRAM, 0)),
        protocol_(0, sockfd_, exchange.ip, exchange.port),
        books_(books) {
    
    if (sockfd_ < 0) throw std::runtime_error("Error: socket creation to exchange failed.");
    
    // MARKET
    sockaddr_in plantaddr = construct_ipv4(market_ip, market_port);
    if (bind(sockfd_, reinterpret_cast<sockaddr*>(&plantaddr), sizeof(plantaddr)) < 0) {
        throw std::runtime_error("Error: bind failed.");
    }

    // EXCHANGE
    sockaddr_in exaddr = construct_ipv4(exchange.ip, exchange.port);
    if (connect(sockfd_, reinterpret_cast<sockaddr*>(&exaddr), sizeof(exaddr)) < 0) {
        throw std::runtime_error("udp connect failed");
    }
}

ExchangeFeed::~ExchangeFeed() {
        if (sockfd_ >= 0) close(sockfd_);
}

void ExchangeFeed::ConnectToExchange() {
    std::uint8_t buf[512];

    while (true) {
        ssize_t n = recvfrom(sockfd_, buf, sizeof(buf), 0, nullptr, nullptr);
        if (n <= 0) continue;
        
        try {
            if (protocol_.handle_packet(buf, static_cast<Bytes>(n))) {
                auto message = protocol_.message_view();
                handle_event(message);
            }
        } catch (const PacketTruncatedError& e) {
            std::cerr << e.what() << "\n";
        }
    }
}

const OrderBook& ExchangeFeed::GetOrderBook(InstrumentId id) const {
    return books_.book(id);
}   

void ExchangeFeed::handle_event(const MessageView &message) {
    MarketEvent e = parse_event(message);

    if (e.event == LevelEvent::AddLevel) {
        books_.book(e.instrument_id).AddOrder(e.side, e.price, e.quantity);
    } else {
        books_.book(e.instrument_id).RemoveOrder(e.side, e.price, e.quantity);
    }

    books_.book(e.instrument_id).PushEventToSubscribers(MarketPlantServer::ConstructEventUpdate(e));

}

MarketEvent ExchangeFeed::parse_event(const MessageView &message) {
    const std::uint8_t* p = message.data;
    Bytes off = 0;

    MarketEvent m_event{};

    m_event.instrument_id = read_big_endian<InstrumentId>(p, off); off += sizeof(InstrumentId);
    m_event.side = static_cast<Side>(p[off++]);
    m_event.event = static_cast<LevelEvent>(p[off++]);
    m_event.price = read_big_endian<Price>(p, off); off += sizeof(Price);
    m_event.quantity = read_big_endian<Quantity>(p, off); off += sizeof(Quantity);
    m_event.exchange_ts = read_big_endian<Timestamp>(p, off); off += sizeof(Timestamp);

    return m_event;
}

sockaddr_in ExchangeFeed::construct_ipv4(const std::string& ip, std::uint16_t port) {
    sockaddr_in res{};
    res.sin_family = AF_INET;
    res.sin_port = htons(port);
    if (inet_pton(AF_INET, ip.c_str(), &res.sin_addr) != 1) {
        close(sockfd_);
        sockfd_ = -1;
        throw std::runtime_error("Error: failed to convert IPv4 address from text to binary form.");
    }
    return res;
}

Subscriber::Subscriber(const Identifier& subscriber, const ms::InstrumentIds& instruments)
    : subscriber_(subscriber) {
    subscribed_to.reserve(static_cast<size_t>(instruments.ids_size()));

    for (auto x : instruments.ids()) {
        subscribed_to.insert(static_cast<InstrumentId>(x));
    }
}

bool Subscriber::Subscribe(const InstrumentId id) {
    // If not in unordered_set, Add to unordered_set
    std::lock_guard<std::mutex> lock(mutex_);
    if (subscribed_to.find(id) == subscribed_to.end()) {
        subscribed_to.insert(id);
        return true;
    }
    return false;
}

void Subscriber::Unsubscribe(const InstrumentId id) {
    // If in unordered_set, remove from unordered_set.
    std::lock_guard<std::mutex> lock(mutex_);
    if (subscribed_to.find(id) != subscribed_to.end()) {
        subscribed_to.erase(id);
    }
    if (subscribed_to.empty()) cv_.notify_one();
}

void Subscriber::Enqueue(std::shared_ptr<const ms::OrderBookUpdate> next) {
    std::lock_guard<std::mutex> lock(mutex_);
    updates.push_back(std::move(next));
    // If the queue was previously empty, signal the CV
    if (updates.size() == 1) cv_.notify_one();
}

std::shared_ptr<const ms::OrderBookUpdate> Subscriber::WaitDequeue(grpc::ServerContext* ctx) {
    std::unique_lock<std::mutex> lock(mutex_);

    // If:
        // No Market Updates
        // Subscriber is still connected
        // Subscriber is subscribed to  >= 1 instrument
        // 
    // -> Go to sleep

    while (updates.empty() && !(ctx && ctx->IsCancelled()) && !subscribed_to.empty()) {
        cv_.wait_for(lock, std::chrono::milliseconds(CANCELLATION_POLL_INTERVAL)); 
    }

    if ((ctx && ctx->IsCancelled()) || subscribed_to.empty()) return nullptr;

    // queue is non-empty: pop + return one item.
    auto next = std::move(updates.front());
    updates.pop_front();
    return next;
}



MarketPlantServer::MarketPlantServer(BookManager& books) : books_(books) {}

grpc::Status MarketPlantServer::StreamUpdates(grpc::ServerContext* context, const ms::Subscription* request, ::grpc::ServerWriter< ms::StreamResponse>* writer) {
    // NOTE: on first call, it is a new subscribe

    std::shared_ptr<Subscriber> subscriber = this->AddSubscriber(request->subscribe());
    auto [id, session_key] = subscriber->get_subscriber();

    // Send session_key and subscriber_id first
    ms::StreamResponse init_response;
    auto* init = init_response.mutable_init();
    init->set_subscriber_id(id);
    init->set_session_id(session_key.data(), session_key.size());
    writer->Write(init_response);

    while (true) {
        auto update = subscriber->WaitDequeue(context);

        if (!update) break;

        ms::StreamResponse next;
        *next.mutable_update() = *update;
        if (!writer->Write(next)) break;
    }
    
    RemoveSubscriber(id);
    return grpc::Status::OK;
}

grpc::Status MarketPlantServer::UpdateSubscriptions(grpc::ServerContext* context, const ms::UpdateSubscriptionRequest* request, ::google::protobuf::Empty* response) {
    // reject if subscription id and token doesn't match
    
    // if subscribe:
        // for all instrumentIds
            // Subscribe(instrumentId)
    // if unsubscribe:
        // for all instrumentIds
            // Unsubscribe(instrumentId)
    
    // return
}

void MarketPlantServer::RemoveSubscriber(const SubscriberId id) {
    std::unique_lock<std::shared_mutex> lock(sub_lock_);
    subscribers_.erase(id);
}

std::shared_ptr<const ms::OrderBookUpdate>  MarketPlantServer::ConstructEventUpdate(const MarketEvent& e) {
    auto update = std::make_shared<ms::OrderBookUpdate>();

    update->set_instrument_id(e.instrument_id);

    auto* curr = update->mutable_incremental()->mutable_update();

    // Map event -> proto type
    switch (e.event) {
        case LevelEvent::AddLevel: curr->set_type(ms::ADD_LEVEL); break;
        case LevelEvent::ModifyLevel: curr->set_type(ms::REPLACE_LEVEL); break; // or REMOVE_LEVEL if that's your meaning
        default: curr->set_type(ms::EVENT_UNSPECIFIED); break;
    }

    auto* level = curr->mutable_level();

    // Map side -> proto side (can't directly assign: different enum + different values)
    switch (e.side) {
        case Side::BID: level->set_side(ms::BID); break;
        case Side::ASK: level->set_side(ms::ASK); break;
        default: level->set_side(ms::SIDE_UNSPECIFIED); break;
    }

    level->set_price(e.price);
    level->set_quantity(e.quantity);

    return update;
}

std::shared_ptr<const ms::OrderBookUpdate> MarketPlantServer::ConstructSnapshot(const InstrumentId id, const ms::SnapshotUpdate& s) {
    auto snapshot = std::make_shared<ms::OrderBookUpdate>();
    snapshot->set_instrument_id(id);           
    *snapshot->mutable_snapshot() = s;  // TODO: Make Snapshot() function more efficient by directly building within
    return snapshot;
}

Identifier MarketPlantServer::InitSubscriber() {
    // generate new subscriber
    Identifier new_sub{};
    new_sub.subscriber_id = next_subscriber_id_++;
    new_sub.session_key = SessionGenerator::Generate();
    return new_sub;
}

int main(int argc, char* argv[]) {
    Config conf{};

    try {
        if (!parse_args(argc, argv, conf)) return 0;
    } catch (const std::runtime_error& e) {
        std::cerr << "Error: " << e.what() << "\n\n";
        print_help();
        return 1;
    }

    // TODO: initialize OrderBookManager once
    BookManager manager(conf.instruments);

    // connect to exchange
    ExchangeFeed feed(conf.exchange, manager);
    std::thread exchange_feed([&]{ feed.ConnectToExchange(); });

    // gRPC server runs on main thread
    MarketPlantServer service(manager);

    grpc::ServerBuilder builder;
    builder.AddListeningPort("0.0.0.0:50051", grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    std::cout << "gRPC listening on 0.0.0.0:50051\n";

    server->Wait();

    exchange_feed.join();

    return 0;
}