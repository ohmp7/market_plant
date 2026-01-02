#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>

using Bytes = std::size_t;
using MessageCount = std::uint16_t;
using MessageDataSize = std::uint16_t;
using SequenceNumber = std::uint64_t;

using Depth = std::size_t;
using Price = std::uint32_t;
using Quantity = std::uint32_t;
using InstrumentId = std::uint32_t;
using SubscriberId = std::uint32_t;
using Timestamp = std::uint64_t;

enum class Side : std::uint8_t {
    BID = 0,
    ASK = 1
};

enum class LevelEvent : std::uint8_t {
    AddLevel = 0,
    ModifyLevel = 1,
};

/*           
Message Payload (Network-Byte-Order / Big Endian)

offset 20 - 21:     msg_len         (2 bytes, u16)        ex. 'msg_len' = 22 (bytes after 'msg_len')
offset 22 - 25:     instrument_id   (4 bytes, u32)        ex. 'AAPL' = 1
offset 26:          side            (1 byte, u8)          ex. 'BID' = 0, 'ASK' = 1
offset 27:          event           (1 byte, u8)          ex. 'ADD' = 0, 'REDUCE' = 1
offset 28 - 31:     price           (4 bytes, u32)        ex. 'price' = 32 (USD)
offset 32 - 35:     quantity        (4 bytes, u32)        ex. 'quantity' = 5917
offset 36 - 43:     exchange_ts     (8 bytes, u64)        ex. 1234567891234567890 (ns)

*/

inline constexpr Bytes SESSION_LENGTH = 10;
inline constexpr Bytes HEADER_LENGTH = 20;
inline constexpr Bytes MESSAGE_COUNT = 1;
inline constexpr Bytes PACKET_SIZE = 44;
inline constexpr Bytes MESSAGE_HEADER_LENGTH = 2;
inline constexpr Timestamp CANCELLATION_POLL_INTERVAL = 500;

inline constexpr MessageCount END_SESSION = 0xFFFF;
inline constexpr MessageCount MAX_MESSAGE_COUNT = END_SESSION - 1;

inline constexpr uint32_t MAX_EXCHANGE_EVENTS = 1000000;

struct MarketEvent {
    InstrumentId instrument_id;
    Side side;
    LevelEvent event;
    Price price;
    Quantity quantity;
    Timestamp exchange_ts;
};

