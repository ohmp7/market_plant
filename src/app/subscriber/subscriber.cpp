#include <grpcpp/grpcpp.h>
#include <iostream>
#include <iomanip>
#include <map>
#include <string>

#include "event.h"
#include "market_plant/market_plant.grpc.pb.h"
#include "market_plant/market_plant.pb.h"

namespace ms = market_plant::v1;

struct LocalOrderBookCopy {
    std::map<Price, Quantity, std::greater<Price>> bids;
    std::map<Price, Quantity, std::less<Price>> asks;
};

static void HandleEvent(LocalOrderBookCopy& book, const ms::OrderBookEventUpdate& event) {
    const auto& level = event.level();
    Price price = level.price();
    Quantity quantity = level.quantity();

    if (level.side() == ms::BID) {
        auto& levels = book.bids;

        switch (event.type()) {
            case ms::ADD_LEVEL: levels[price] += quantity; break;
            case ms::REDUCE_LEVEL:
                if (quantity == levels[price]) levels.erase(price);
                else levels[price] -= quantity;
                break;
            default: break;
        }
    } else if (level.side() == ms::ASK) {
        auto& levels = book.asks;

        switch (event.type()) {
            case ms::ADD_LEVEL: levels[price] += quantity; break;
            case ms::REDUCE_LEVEL:
                if (quantity == levels[price]) levels.erase(price);
                else levels[price] -= quantity;
                break;
            default: break;
        }
    }
}

static void HandleSnapshot(LocalOrderBookCopy& book, const ms::SnapshotUpdate& snapshot) {
    book.bids.clear();
    book.asks.clear();

    for (const auto& event : snapshot.bids()) HandleEvent(book, event);
    for (const auto& event : snapshot.asks()) HandleEvent(book, event);
}

static void PrintBookState(const LocalOrderBookCopy& book, std::size_t depth) {
    std::cout << "\033[2J\033[H";

    std::cout << "   BIDS (Price | Qty)       |   ASKS (Price | Qty)\n";
    std::cout << "----------------------------+-----------------------------\n";


    auto bid_it = book.bids.begin();
    auto ask_it = book.asks.begin();


    for (Depth i = 0; i < depth; ++i) {
        // left side: bids
        if (bid_it != book.bids.end()) {
            std::cout << std::setw(8) << bid_it->first << " | "
                        << std::setw(8) << bid_it->second;
            ++bid_it;
        } else {
            std::cout << std::setw(8) << "-" << " | " << std::setw(8) << "-";
        }


        std::cout << "        |   ";


        // right side: asks
        if (ask_it != book.asks.end()) {
            std::cout << std::setw(8) << ask_it->first << " | "
                        << std::setw(8) << ask_it->second;
            ++ask_it;
        } else {
            std::cout << std::setw(8) << "-" << " | " << std::setw(8) << "-";
        }


        std::cout << "\n";
    }


    std::cout << "----------------------------+-----------------------------\n";
    std::cout.flush();
}

int main() {
    const std::string addr = "127.0.0.1:50051";
    auto channel = grpc::CreateChannel(addr, grpc::InsecureChannelCredentials());
    auto stub = ms::MarketPlantService::NewStub(channel);

    ms::Subscription req;
    req.mutable_subscribe()->add_ids(1);
    grpc::ClientContext ctx;
    std::unique_ptr<grpc::ClientReader<ms::StreamResponse>> reader(stub->StreamUpdates(&ctx, req));

    LocalOrderBookCopy book;
    constexpr Depth kDepth = 10;

    ms::StreamResponse resp;
    bool got_init = false;

    while (reader->Read(&resp)) {
        if (!got_init && resp.has_init()) {
            got_init = true;
            std::cout << "Connected. subscriber_id=" << resp.init().subscriber_id()
                        << " session_id_bytes=" << resp.init().session_id().size() << "\n";
            continue;
        }

        if (!resp.has_update()) continue;
        const auto& upd = resp.update();

        if (upd.has_snapshot()) {
            HandleSnapshot(book, upd.snapshot());
            PrintBookState(book, kDepth);
        } else if (upd.has_incremental()) {
            HandleEvent(book, upd.incremental().update());
            PrintBookState(book, kDepth);
        }
    }
}