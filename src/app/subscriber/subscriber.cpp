#include "subscriber.h"
#include <iostream>
#include <iomanip>

MarketDataSubscriber::MarketDataSubscriber(SubscriberConfig config)
    : config_(std::move(config)) {
    auto channel = grpc::CreateChannel(config_.GetAddress(), grpc::InsecureChannelCredentials());
    stub_ = ms::MarketPlantService::NewStub(channel);
}

void MarketDataSubscriber::HandleEvent(const ms::OrderBookEventUpdate& event) {
    const auto& level = event.level();
    Price price = level.price();
    Quantity quantity = level.quantity();

    std::map<Price, Quantity, std::greater<Price>>* levels = nullptr;
    std::map<Price, Quantity, std::less<Price>>* asks_levels = nullptr;
    
    if (level.side() == ms::BID) {
        levels = &book_.bids;
    } else if (level.side() == ms::ASK) {
        asks_levels = &book_.asks;
    } else {
        return;
    }

    switch (event.type()) {
        case ms::ADD_LEVEL:
            if (levels) (*levels)[price] += quantity;
            else if (asks_levels) (*asks_levels)[price] += quantity;
            break;
            
        case ms::REDUCE_LEVEL:
            if (levels) {
                if (quantity == (*levels)[price]) levels->erase(price);
                else (*levels)[price] -= quantity;
            } else if (asks_levels) {
                if (quantity == (*asks_levels)[price]) asks_levels->erase(price);
                else (*asks_levels)[price] -= quantity;
            }
            break;
            
        default: 
            break;
    }
}

void MarketDataSubscriber::HandleSnapshot(const ms::SnapshotUpdate& snapshot) {
    book_.bids.clear();
    book_.asks.clear();
    for (const auto& event : snapshot.bids()) HandleEvent(event);
    for (const auto& event : snapshot.asks()) HandleEvent(event);
}

void MarketDataSubscriber::PrintBookState() {
    std::cout << "\033[2J\033[H";

    std::cout << "   BIDS (Price | Qty)       |   ASKS (Price | Qty)\n";
    std::cout << "----------------------------+-----------------------------\n";

    auto bid_it = book_.bids.begin();
    auto ask_it = book_.asks.begin();

    for (Depth i = 0; i < config_.display_depth; ++i) {
        if (bid_it != book_.bids.end()) {
            std::cout << std::setw(8) << bid_it->first << " | " << std::setw(8) << bid_it->second;
            ++bid_it;
        } else {
            std::cout << std::setw(8) << "-" << " | " << std::setw(8) << "-";
        }
        std::cout << "        |   ";
        if (ask_it != book_.asks.end()) {
            std::cout << std::setw(8) << ask_it->first << " | " << std::setw(8) << ask_it->second;
            ++ask_it;
        } else {
            std::cout << std::setw(8) << "-" << " | " << std::setw(8) << "-";
        }

        std::cout << "\n";
    }
    std::cout << "----------------------------+-----------------------------\n";
    std::cout.flush();
}

void MarketDataSubscriber::Run() {
    ms::Subscription req;
    
    // Subscribe to all configured instrument IDs
    for (InstrumentId id : config_.instrument_ids) {
        req.mutable_subscribe()->add_ids(id);
    }
    
    grpc::ClientContext ctx;
    std::unique_ptr<grpc::ClientReader<ms::StreamResponse>> reader(
        stub_->StreamUpdates(&ctx, req)
    );

    ms::StreamResponse resp;
    bool got_init = false;

    while (reader->Read(&resp)) {
        if (!got_init && resp.has_init()) {
            got_init = true;
            for (size_t i = 0; i < config_.instrument_ids.size(); ++i) {
                if (i > 0) std::cout << ", ";
                std::cout << config_.instrument_ids[i];
            }
            std::cout << "\n";
            continue;
        }

        if (!resp.has_update()) continue;
        const auto& upd = resp.update();

        if (upd.has_snapshot()) {
            HandleSnapshot(upd.snapshot());
            PrintBookState();
        } else if (upd.has_incremental()) {
            HandleEvent(upd.incremental().update());
            PrintBookState();
        }
    }
}

int main() {
    SubscriberConfig config = SubscriberConfig::New();
    MarketDataSubscriber subscriber(config);
    subscriber.Run();
    return 0;
}