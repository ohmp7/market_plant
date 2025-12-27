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

/* 

Exchange -> Plant API Payload (Network-Byte-Order / Big Endian)

offset 20 - 21:     msg_len         (2 bytes, u16)        ex. 'msg_len' = 18 (bytes)
offset 22 - 25:     instrument_id   (4 bytes, u32)        ex. 'AAPL' = 1
offset 26:          side            (1 byte, u8)          ex. 'BID' = 0, 'ASK' = 1
offset 27:          event           (1 byte, u8)          ex. 'ADD' = 0, 'REDUCE' = 1
offset 28 - 31:     price           (4 bytes, u32)        ex. 'price' = 32 (USD)
offset 32 - 39:     exchange_ts     (8 bytes, u64)        ex. 1234567891234567890 (ns)

*/

struct Event {
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
