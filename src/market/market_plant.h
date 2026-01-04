#pragma once

// C++ standard (needed by member fields + function signatures)
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include <netinet/in.h>

#include <grpcpp/grpcpp.h>
#include "market_plant/market_plant.grpc.pb.h"

#include "event.h"
#include "market_cli.h"
#include "market_plant_config.h"
#include "moldudp64.h"

namespace ms = market_plant::v1;

using grpc::Status;
using grpc::ServerContext;
using grpc::ServerWriter;
using StreamResponsePtr = std::shared_ptr<const ms::StreamResponse>;

class SessionGenerator {
public:
    static std::string Generate();
private:
    inline static std::mutex generator_mutex_;
    inline static std::mt19937_64 byte_generator_{std::random_device{}()};
};

struct Identifier {
    SubscriberId subscriber_id;
    std::string session_key;
};


// on construction, queue should be initialization to n snapshots of the n instruements subscribed to
class Subscriber {
public:
    Subscriber(const Identifier& subscriber, const ms::InstrumentIds& instruments);

    bool Subscribe(const InstrumentId id);

    void Unsubscribe(const InstrumentId id);

    void Enqueue(const StreamResponsePtr& next);

    StreamResponsePtr WaitDequeue(grpc::ServerContext* ctx);

    const Identifier& get_subscriber() const { return subscriber_; }

private:
    Identifier subscriber_;

    std::condition_variable cv_;
    std::mutex mutex_;
    
    // queue of Update(s) to send
    std::deque<StreamResponsePtr> updates_;

    // unordered_set of instruments subscribed to
    std::unordered_set<InstrumentId> subscribed_to_;
};


// All orderbook updates happen from ExchangeFeed
// All subscription updates happen from the MarketPlantServer
class OrderBook {
public:
    OrderBook(InstrumentId id, const Depth depth);
    
    void PushEventToSubscribers(const MarketEvent& data);

    void InitializeSubscription(std::shared_ptr<Subscriber> subscriber);
    
    void CancelSubscription(const SubscriberId id);
private:
    void AddOrder(Side side, Price price, Quantity quantity);
        
    void RemoveOrder(Side side, Price price, Quantity quantity);

    void Snapshot(ms::SnapshotUpdate* snapshot);

    template <class Levels>
    static void UpdateLevel(Levels &levels, Price price, Quantity quantity) {
        auto [it, added] = levels.try_emplace(price, quantity);
        if (!added) it->second += quantity;
    }
    
    template <class Levels>
    static void ModifyLevel(Levels &levels, typename Levels::iterator it, Quantity quantity) {
        if (quantity >= it->second) {
            levels.erase(it);
        } else {
            it->second -= quantity;
        }
    }

    std::mutex mutex_;
    std::map<Price, Quantity, std::greater<Price>> bids_;
    std::map<Price, Quantity, std::less<Price>> asks_;

    std::unordered_map<SubscriberId, std::weak_ptr<Subscriber>> subscriptions_;
    InstrumentId id_;
    Depth depth_;
};


class BookManager {
public:
    explicit BookManager(const InstrumentConfig& instruments);

    OrderBook& book(InstrumentId id);

    const OrderBook& book(InstrumentId id) const;

private:
    std::unordered_map<InstrumentId, OrderBook> books_;
};


class ExchangeFeed {
public:
    ExchangeFeed(const Exchange& exchange, BookManager& books, const MarketPlantConfig& mp_config);
    
    ~ExchangeFeed();

    void ConnectToExchange();
    
    const OrderBook& GetOrderBook(InstrumentId id) const;

private:
    void handle_event(const MessageView &message);

    MarketEvent parse_event(const MessageView &message);

    sockaddr_in construct_ipv4(const std::string& ip, std::uint16_t port);

    int sockfd_{-1};
    MoldUDP64 protocol_;
    BookManager& books_;
};


// handle all subscription and order
class MarketPlantServer final : public ms::MarketPlantService::Service {
public:
    MarketPlantServer(BookManager& books);

    // Server-side streaming
    grpc::Status StreamUpdates(grpc::ServerContext* context, const ms::Subscription* request, ::grpc::ServerWriter< ms::StreamResponse>* writer);

    // Control-plane for modifying subscriptions
    grpc::Status UpdateSubscriptions(grpc::ServerContext* context, const ms::UpdateSubscriptionRequest* request, ::google::protobuf::Empty* response);

    std::shared_ptr<Subscriber> AddSubscriber(const ms::InstrumentIds& subscriptions) {
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
            OrderBook& book = books_.book(id);
            book.InitializeSubscription(sub);
        }

        return sub;
    }

    void RemoveSubscriber(const SubscriberId id);

    static StreamResponsePtr ConstructEventUpdate(const MarketEvent& e);

private:
    static Identifier InitSubscriber();

    inline static std::atomic<SubscriberId> next_subscriber_id_{1};
    BookManager& books_;

    inline static std::shared_mutex sub_lock_;
    std::unordered_map<SubscriberId, std::weak_ptr<Subscriber>> subscribers_;
};

