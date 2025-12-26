#include <iostream>
#include <cstdint>
#include <unordered_map>
#include <cstddef>

using Price = std::int32_t;
using Quantity = std::uint32_t;
using InstrumentId = std::uint32_t;
using Depth = std::size_t;
using Levels = std::unordered_map<Price, Quantity>;

enum class Side : std::uint8_t {
    BID = 0,
    ASK = 1,
};

// Exchange -> Plant API
struct Event {
    std::uint64_t sequence_number;  // for UDP
    InstrumentId instrument_id;
    Side side;
    Price price;  // if price dne, add a new level
    Quantity quantity;  // if quantity == 0, erase level
};

// Plant -> Subscriber API (TBD)
struct OrderBookDelta {
    
};

// use std::move later
class OrderBook {
public:
    OrderBook(const Depth depth_,  const InstrumentId instrument_id_)
        : instrument_id(instrument_id_), depth(depth_) {}
    
private:
    InstrumentId instrument_id;
    Depth depth;

    Levels bids;
    Levels asks;
};

int main(int argc, char* argv[]) {

}
