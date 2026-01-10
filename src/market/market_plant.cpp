#include "cpu_affinity.h"
#include "endian.h"
#include "market_plant.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

std::string SessionGenerator::Generate() {
    std::string session_key(16, '\0');
    std::lock_guard<std::mutex> lock(generator_mutex_);
    for (Bytes i = 0; i < session_key.size(); i += 8) {
        std::uint64_t n = byte_generator_();
        std::memcpy(session_key.data() + i, &n, 8);
    }
    return session_key;
};

OrderBook::OrderBook(InstrumentId id, Depth depth) : id_(id), depth_(depth) {}
    
void OrderBook::PushEventToSubscribers(const MarketEvent& data) {
    std::vector<std::shared_ptr<Subscriber>> to_enqueue;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (data.event == LevelEvent::kAddLevel) { 
            AddOrder(data.side, data.price, data.quantity);
        } else { 
            RemoveOrder(data.side, data.price, data.quantity);
        }

        to_enqueue.reserve(subscriptions_.size());
        // Get Subscribers for corresponding InstrumentID
        for (auto it = subscriptions_.begin(); it != subscriptions_.end(); ) {
            if (auto sub = it->second.lock()) [[likely]] {
                to_enqueue.push_back(std::move(sub));
                ++it;
            } else {
                it = subscriptions_.erase(it);
            }
        }
    }

    if (to_enqueue.empty()) return;

    StreamResponsePtr event = MarketPlantServer::ConstructEventUpdate(data);
    for (const auto& sub : to_enqueue) sub->Enqueue(event);
}

void OrderBook::AddOrder(Side side, Price price, Quantity quantity) {
    if (side == Side::kBid) {
        UpdateLevel(bids_, price, quantity);
    } else {
        UpdateLevel(asks_, price, quantity);
    }
}

void OrderBook::RemoveOrder(Side side, Price price, Quantity quantity) {
    if (side == Side::kBid) {
        auto it = bids_.find(price);
        if (it == bids_.end()) [[unlikely]] return;
        ModifyLevel(bids_, it, quantity);
    } else {
        auto it = asks_.find(price);
        if (it == asks_.end()) [[unlikely]] return;
        ModifyLevel(asks_, it, quantity);
    }
}

void OrderBook::InitializeSubscription(std::shared_ptr<Subscriber> subscriber) {
    auto snapshot_response = std::make_shared<ms::StreamResponse>();
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Add Subscriber to subscriptions_
    subscriptions_[subscriber->subscriber().subscriber_id] = subscriber;
    
    // Add snapshot to subscriber's queue
    auto* update = snapshot_response->mutable_update();
    update->set_instrument_id(id_);
    Snapshot(update->mutable_snapshot());

    subscriber->Enqueue(snapshot_response);
    // This way, any new feed for this instrument is blocked until we get the snapshot in
}

void OrderBook::CancelSubscription(SubscriberId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    subscriptions_.erase(id);
}

void OrderBook::Snapshot(ms::SnapshotUpdate* snapshot) {
    // INVARIANT: Caller must hold mutex
    snapshot->mutable_bids()->Reserve(static_cast<int>(depth_));
    snapshot->mutable_asks()->Reserve(static_cast<int>(depth_));

    // Top-Depth Bids
    auto b_it = bids_.begin();
    for (Depth i = 0; i < depth_ && b_it != bids_.end(); ++i, ++b_it) {
        auto* bid = snapshot->add_bids();
        bid->set_type(ms::ADD_LEVEL);
        auto* level = bid->mutable_level();
        level->set_side(ms::BID);
        level->set_price(b_it->first);
        level->set_quantity(b_it->second);
    }

    // Top-Depth Asks
    auto a_it = asks_.begin();
    for (Depth i = 0; i < depth_ && a_it != asks_.end(); ++i, ++a_it) {
        auto* ask = snapshot->add_asks();
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

OrderBook& BookManager::Book(InstrumentId id) {
    auto it = books_.find(id);
    if (it == books_.end()) {
        throw std::runtime_error("Error: unknown instrument id " + std::to_string(id));
    }
    return it->second;
}

const OrderBook& BookManager::Book(InstrumentId id) const {
    auto it = books_.find(id);
    if (it == books_.end()) {
        throw std::runtime_error("Error: unknown instrument id " + std::to_string(id));
    }
    return it->second;
}


ExchangeFeed::ExchangeFeed(BookManager& books, const MarketPlantConfig& mp_config, int cpu_core)
    : sockfd_(socket(AF_INET, SOCK_DGRAM, 0)),
        protocol_(0, sockfd_, mp_config.exchange_ip, mp_config.exchange_port),
        books_(books),
        cpu_core_(cpu_core) {
    
    if (sockfd_ < 0) throw std::runtime_error("Error: socket creation to exchange failed.");
    
    // MARKET
    sockaddr_in plantaddr = ConstructIpv4(mp_config.market_ip, mp_config.market_port);
    if (bind(sockfd_, reinterpret_cast<sockaddr*>(&plantaddr), sizeof(plantaddr)) < 0) {
        throw std::runtime_error("Error: bind failed.");
    }

    // EXCHANGE
    sockaddr_in exaddr = ConstructIpv4(mp_config.exchange_ip, mp_config.exchange_port);
    if (connect(sockfd_, reinterpret_cast<sockaddr*>(&exaddr), sizeof(exaddr)) < 0) {
        throw std::runtime_error("udp connect failed");
    }
}

ExchangeFeed::~ExchangeFeed() {
        if (sockfd_ >= 0) close(sockfd_);
}

void ExchangeFeed::ConnectToExchange() {
    if (cpu_core_ >= 0) {
        if (CPUAffinity::PinToCore(cpu_core_)) [[likely]] 
            std::cout << "Successfully pinned Exchange Feed to core " << cpu_core_ << ".\n";
        else {
            std::cout << "Failed to pin Exchange Feed to core " << cpu_core_ << ".\n";
        }
    }

    std::uint8_t buf[512];

    while (true) {
        ssize_t n = recvfrom(sockfd_, buf, sizeof(buf), 0, nullptr, nullptr);
        if (n <= 0) [[unlikely]] continue;
        
        try {
            if (protocol_.HandlePacket(buf, static_cast<Bytes>(n))) {
                auto message = protocol_.message_view();
                HandleEvent(message);
            }
        } catch (const PacketTruncatedError& e) {
            std::cerr << e.what() << "\n";
        }
    }
}

const OrderBook& ExchangeFeed::GetOrderBook(InstrumentId id) const {
    return books_.Book(id);
}   

void ExchangeFeed::HandleEvent(const MessageView& message) {
    MarketEvent e = ParseEvent(message);
    books_.Book(e.instrument_id).PushEventToSubscribers(e);
}

MarketEvent ExchangeFeed::ParseEvent(const MessageView& message) {
    const std::uint8_t* p = message.data;
    Bytes off = 0;

    MarketEvent m_event{};

    m_event.instrument_id = ReadBigEndian<InstrumentId>(p, off); off += sizeof(InstrumentId);
    m_event.side = static_cast<Side>(p[off++]);
    m_event.event = static_cast<LevelEvent>(p[off++]);
    m_event.price = ReadBigEndian<Price>(p, off); off += sizeof(Price);
    m_event.quantity = ReadBigEndian<Quantity>(p, off); off += sizeof(Quantity);
    m_event.exchange_ts = ReadBigEndian<Timestamp>(p, off); off += sizeof(Timestamp);

    return m_event;
}

sockaddr_in ExchangeFeed::ConstructIpv4(const std::string& ip, std::uint16_t port) {
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
    subscribed_to_.reserve(static_cast<size_t>(instruments.ids_size()));

    for (auto x : instruments.ids()) {
        subscribed_to_.insert(static_cast<InstrumentId>(x));
    }
}

bool Subscriber::Subscribe(InstrumentId id) {
    // If not in unordered_set, Add to unordered_set
    std::lock_guard<std::mutex> lock(mutex_);
    if (subscribed_to_.find(id) == subscribed_to_.end()) {
        subscribed_to_.insert(id);
        return true;
    }
    return false;
}

void Subscriber::Unsubscribe(InstrumentId id) {
    // If in unordered_set, remove from unordered_set.
    std::lock_guard<std::mutex> lock(mutex_);
    if (subscribed_to_.find(id) != subscribed_to_.end()) {
        subscribed_to_.erase(id);
    }
    if (subscribed_to_.empty()) cv_.notify_one();
}

void Subscriber::Enqueue(const StreamResponsePtr& next) {
    std::lock_guard<std::mutex> lock(mutex_);
    updates_.push_back(next);
    // If the queue was previously empty, signal the CV
    if (updates_.size() == 1) cv_.notify_one();
}

StreamResponsePtr Subscriber::WaitDequeue(ServerContext* ctx) {
    std::unique_lock<std::mutex> lock(mutex_);

    // If:
        // No Market Updates
        // Subscriber is still connected
        // Subscriber is subscribed to  >= 1 instrument
        // 
    // -> Go to sleep

    while (updates_.empty() && !(ctx && ctx->IsCancelled()) && !subscribed_to_.empty()) {
        cv_.wait_for(lock, std::chrono::milliseconds(kCancellationPollInterval)); 
    }

    if ((ctx && ctx->IsCancelled()) || subscribed_to_.empty()) return nullptr;

    // queue is non-empty: pop + return one item.
    auto next = std::move(updates_.front());
    updates_.pop_front();
    return next;
}



MarketPlantServer::MarketPlantServer(BookManager& books) : books_(books) {}

Status MarketPlantServer::StreamUpdates(ServerContext* context, const ms::Subscription* request, ServerWriter< ms::StreamResponse>* writer) {
    // NOTE: on first call, it is a new subscribe
    if (!request || !request->has_subscribe()) {
        return Status(grpc::StatusCode::INVALID_ARGUMENT, "Error: invalid request.");
    }

    std::shared_ptr<Subscriber> subscriber = this->AddSubscriber(request->subscribe());
    const auto& [id, session_key] = subscriber->subscriber();

    // Send session_key and subscriber_id first
    ms::StreamResponse init_response;
    auto* init = init_response.mutable_init();
    init->set_subscriber_id(id);
    init->set_session_id(session_key.data(), session_key.size());
    writer->Write(init_response);

    while (true) {
        auto update = subscriber->WaitDequeue(context);
        if (!update) break;
        if (!writer->Write(*update)) break;
    }
    
    RemoveSubscriber(id);
    return Status::OK;
}

Status MarketPlantServer::UpdateSubscriptions(ServerContext* context, const ms::UpdateSubscriptionRequest* request, ::google::protobuf::Empty* response) {
    (void)context;
    (void)response;

    const SubscriberId subscriber_id = static_cast<SubscriberId>(request->subscriber_id());
    const std::string& session_id = request->session_id();

    // reject if subscription id and token doesn't match
    std::shared_ptr<Subscriber> subscriber;
    {
        std::shared_lock<std::shared_mutex> lock(sub_lock_);
        auto it = subscribers_.find(subscriber_id);
        if (it == subscribers_.end()) {
            return Status(grpc::StatusCode::NOT_FOUND, "Error: unknown subscriber_id.");
        }
        subscriber = it->second.lock();
    }

    if (!subscriber) {
        std::unique_lock<std::shared_mutex> lock(sub_lock_);
        auto it = subscribers_.find(subscriber_id);
        if (it != subscribers_.end() && it->second.expired()) {
            subscribers_.erase(it);
        }
        return Status(grpc::StatusCode::NOT_FOUND, "Error: subscriber expired.");
    } else if (subscriber->subscriber().session_key != session_id) {
        return Status(grpc::StatusCode::PERMISSION_DENIED, "Error: invalid session_id.");
    }

    const ms::Subscription& change = request->change();

    if (change.has_subscribe()) {
        const ms::InstrumentIds& ids = change.subscribe();

        for (auto instrument_id : ids.ids()) {
            OrderBook* book = nullptr;
            // filter for valid instruments
            try {
                book = &books_.Book(instrument_id);
            } catch (const std::exception& e) {
                return Status(grpc::StatusCode::INVALID_ARGUMENT, e.what());
            }

            // Only initialize subscription and queue snapshot if new subscription
            if (subscriber->Subscribe(instrument_id)) book->InitializeSubscription(subscriber);
        }
    } else if (change.has_unsubscribe()) {
        const ms::InstrumentIds& ids = change.unsubscribe();
        for (auto instrument_id : ids.ids()) {
            // Filter for valid instruments
            try {
                books_.Book(instrument_id).CancelSubscription(subscriber_id);
            } catch (const std::exception& e) {
                return Status(grpc::StatusCode::INVALID_ARGUMENT, e.what());
            }

            subscriber->Unsubscribe(instrument_id);
        }
    }
    return Status::OK;
}

std::shared_ptr<Subscriber> MarketPlantServer::AddSubscriber(const ms::InstrumentIds& subscriptions) {
    std::shared_ptr<Subscriber> sub;

    // add new id to subscribers_
    Identifier subscriber = InitSubscriber();
    sub = std::make_shared<Subscriber>(subscriber, subscriptions);
    {
        std::unique_lock<std::shared_mutex> lock(sub_lock_);
        subscribers_[subscriber.subscriber_id] = sub;
    }

    // Initialize Subscriptions
    for (auto& id : subscriptions.ids()) {
        OrderBook& book = books_.Book(id);
        book.InitializeSubscription(sub);
    }

    return sub;
}

void MarketPlantServer::RemoveSubscriber(const SubscriberId id) {
    std::unique_lock<std::shared_mutex> lock(sub_lock_);
    subscribers_.erase(id);
}

StreamResponsePtr MarketPlantServer::ConstructEventUpdate(const MarketEvent& e) {
    auto final_event = std::make_shared<ms::StreamResponse>();
    auto* update = final_event->mutable_update();

    update->set_instrument_id(e.instrument_id);

    auto* curr = update->mutable_incremental()->mutable_update();

    // Map event -> proto type
    switch (e.event) {
        case LevelEvent::kAddLevel: curr->set_type(ms::ADD_LEVEL); break;
        case LevelEvent::kModifyLevel: curr->set_type(ms::REDUCE_LEVEL); break;
        default: curr->set_type(ms::EVENT_UNSPECIFIED); break;
    }

    auto* level = curr->mutable_level();

    switch (e.side) {
        case Side::kBid: level->set_side(ms::BID); break;
        case Side::kAsk: level->set_side(ms::ASK); break;
        default: level->set_side(ms::SIDE_UNSPECIFIED); break;
    }

    level->set_price(e.price);
    level->set_quantity(e.quantity);

    return final_event;
}

Identifier MarketPlantServer::InitSubscriber() {
    // generate new subscriber
    Identifier new_sub{};
    new_sub.subscriber_id = next_subscriber_id_++;
    new_sub.session_key = SessionGenerator::Generate();
    return new_sub;
}

int main(int argc, char* argv[]) {
    MarketPlantCliConfig conf{};

    try {
        if (!ParseArgs(argc, argv, conf)) return 0;
    } catch (const std::runtime_error& e) {
        std::cerr << "Error: " << e.what() << "\n\n";
        PrintHelp();
        return 1;
    }

    MarketPlantConfig mp_config = MarketPlantConfig::New();
    BookManager manager(conf.instruments);

    // connect to exchange
    auto feed = std::make_shared<ExchangeFeed>(manager, mp_config, conf.cpu_core);
    std::thread exchange_feed([feed]{ feed->ConnectToExchange(); });
    exchange_feed.detach();

    // gRPC server runs on main thread
    MarketPlantServer service(manager);

    grpc::ServerBuilder builder;
    builder.AddListeningPort(mp_config.GetGrpcAddress(), grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    std::cout << "gRPC listening on " << mp_config.GetGrpcAddress() << "\n";

    server->Wait();

    return 0;
}