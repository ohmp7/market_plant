# **Market Data Feed Handler**

This repository showcases a low-latency C++ Market Data Feed Handler simulator _(the **Market Plant**)_. The motivation behind this project was to build a high-performance middleware service that ingests exchange-style UDP feeds and efficiently scales to one-to-many subscribers via [gRPC](https://grpc.io/).

### _What is a Market Data Feed Handler?_

A Market data feed handler connects exchange feeds with internal trading systems, turning raw data into digestible market data and streaming to downstream consumers at scale. They’re a crucial part of market infrastructure and are designed to move high-volume, latency-sensitive data.

## **Architecture**

_The diagram below represents the Market Plant system architecture at a high level, showing the data flow between the Exchange simulator, Market Plant server, and subscriber(s)._

<img width="829" height="495" alt="MarketPlantDiagram" src="https://github.com/user-attachments/assets/73399350-64db-483a-91b3-3da1115c8652" />

### **Market Data Feed Handler**

The Market Plant primarily:

- Ingests UDP unicast datagrams from a simulated exchange using **Nasdaq’s [MoldUDP64](https://www.nasdaqtrader.com/content/technicalsupport/specifications/dataproducts/moldudp64.pdf) Protocol**, including gap detection and retransmission.
- Builds and maintains in-memory L2 price-level order books for configurable instruments from exchange feed data.
- Streams real-time snapshots and deltas to subscribers via **server-side [gRPC](https://grpc.io/) streaming** efficiently.
  
### **UDP Unicast**
For Exchange → Plant communication, the simulator sends the feed over **UDP unicast** to replicate how market data is delivered. In production, exchange feeds often use **multicast** for efficient one-to-many distribution, with **unicast** used for recovery/retransmission. However, for the scope of this project _(single Exchange and Market Data Feed Handler)_, unicast suffices and provides the same advantages as multicast.

**[`moldudp64_client.h`](./src/network/moldudp64_client.h)** implements a **MoldUDP64** client state machine that:
- Parses the MoldUDP64 header (session, sequence number, message count) and tracks the active session.
- Enforces **in-order processing** using _sequencing_, dropping late/duplicate datagrams.
- Detects **sequence gaps** and enters recovery state (either cold-start backfill or mid-stream gapfill).
- **Retransmits requests** starting at the missing sequence number, throttled by a timeout and bounded by `MAX_MESSAGE_COUNT`.

Below is an example of the message payload utilized (Big-Endian/NBO). As mentioned before, Each **[MoldUDP64](https://www.nasdaqtrader.com/content/technicalsupport/specifications/dataproducts/moldudp64.pdf)** message is encoded as: `msg_len (u16)` then `msg_len` bytes of payload. _The offsets below are byte offsets from the start of the UDP datagram buffer._

| Payload Offset (bytes) | Field          | Size | Type |
|---:|---|---:|---|
| 0–3   | `instrument_id` | 4 | `u32` |
| 4     | `side`          | 1 | `u8`  |
| 5     | `event`         | 1 | `u8`  |
| 6–9   | `price`         | 4 | `u32` |
| 10–13 | `quantity`      | 4 | `u32` |
| 14–21 | `exchange_ts`   | 8 | `u64` |

> Note: for this simulator, each UDP datagram carries a single MoldUDP64 message, while preserving the MoldUDP64 Protocol and sequencing semantics.

### **gRPC**

I decided to use gRPC with server-side streaming because it provides bidirectional communication over a single persistent connection, thereby eliminating the connection overhead associated with repeated HTTP requests. Server-side streaming enables the Market Plant to push updates to subscribers in real-time as market events occur, eliminating the need for clients to poll for data. 

The protocol also uses **[HTTP/2 multiplexing](https://blog.codavel.com/http2-multiplexing)**, which allows us to handle multiple concurrent streams over a single TCP connection. Lastly, gRPC's native support for structured data via Protocol Buffers provides type-safe message serialization that's more efficient than JSON.

### **Exchange Simulator**

The Exchange Simulator produces market movement for testing the Market Plant. It continuously generates randomized **L2 price-level deltas** _(add level, reduce level, remove level)_ across instruments and sides, serializes each event into **MoldUDP64-framed UDP datagrams**, and sends them to the Market Plant over UDP unicast.

To support gap recovery, the simulator also keeps a fixed-size **in-memory history buffer** keyed by sequence number. When it receives retransmission requests _(MoldUDP64 header containing a starting sequence number and message count)_, it re-enqueues the requested events and replays them back to the Market Plant.

## Project Structure
- **[`config/config.json`](./config/config.json)** _Runtime instrument configuration._

- **[`protos/market_plant/market_plant.proto`](./protos/market_plant/market_plant.proto)** _Protobuf definitions for the Market Plant gRPC API._

- **[`src/app/`](./src/app)** _Top-level applications._
  - **[`exhange/exchange.h`](./src/app/exhange/exchange.h)** _Exchange simulator._ 
  - **[`subscriber/subscriber.cpp`](./src/app/subscriber/subscriber.h)** _gRPC subscriber client example._

- **[`src/market/`](./src/market)** _Market Plant core._
  - **[`market_plant.h`](./src/market/market_plant.h)** _Market Plant server._  
  - **[`event.h`](./src/market/event.h)** _Market event types and shared structures._ 
  - **[`cli/market_cli.h`](./src/market/cli/market_cli.h)** _CLI parsing / config wiring._ 

- **[`src/network/`](./src/network)** _Networking + wire-format utilities._
  - **[`moldudp64.h`](./src/network/moldudp64.h)** _MoldUDP64 framing + gap detection/retransmission client logic._  
  - **[`udp_messenger.h`](./src/network/udp_messenger.h)** _UDP socket send wrapper._
  - **[`endian.h`](./src/network/endian.h)** _Big-endian (Network Byte Order) read/write helpers._


## **Usage**

### Instrument Configuration

The Market Plant Server is configurable via a JSON configuration file that defines the instruments to track:

```json
{
    "instruments": [
        {
            "instrument_id": 1,
            "symbol": "AAPL",
            "specifications": {
                "depth": 10
            }
        },
        {
            "instrument_id": 2,
            "symbol": "META",
            "specifications": {
                "depth": 5
            }
        }
    ]
}
```

**Configuration fields:**
- `instrument_id`: Unique identifier for the instrument
- `symbol`: Trading symbol (informational)
- `depth`: Number of price levels to maintain in the order book

### Environment Variables

The Market Plant also supports runtime configuration via environment variables:

| Variable | Description | Default |
|----------|-------------|---------|
| `GRPC_HOST` | gRPC server bind address | `0.0.0.0` |
| `GRPC_PORT` | gRPC server port | `50051` |
| `MARKET_IP` | UDP socket bind address | `127.0.0.1` |
| `MARKET_PORT` | UDP socket bind port | `9001` |
| `EXCHANGE_IP` | Exchange simulator address | `127.0.0.1` |
| `EXCHANGE_PORT` | Exchange simulator port | `9000` |

### Running Market Plant

```bash
# Use default configuration
./market_plant --config config.json

# Pin exchange feed thread to specified CPU core 
./market_plant --config config.json --cpu 7

# Custom configuration
GRPC_HOST=0.0.0.0 \
GRPC_PORT=8080 \
MARKET_PORT=9002 \
EXCHANGE_IP=10.0.0.1 \
EXCHANGE_PORT=9000 \
./market_plant --config config.json
```

## gRPC API

The Market Plant exposes two main RPC methods for market data streaming and subscription management.

### 1. StreamUpdates() _(Server-side Streaming)_

Establishes a streaming connection to receive real-time order book updates.

**Request:**
```protobuf
message Subscription {
    oneof action {
        InstrumentIds subscribe = 1;    // Initial subscription(s)
    }
}

message InstrumentIds {
    repeated uint32 ids = 1;
}
```

**Response Stream:**
```protobuf
message StreamResponse {
    oneof payload {
        SubscriberInitialization init = 1;    // First message
        OrderBookUpdate update = 2;           // Subsequent messages
    }
}
```
At a high level, the flow is:

1. The client starts a streaming session by subscribing to one or more instrument IDs.
2. The server acknowledges the session with `subscriber_id` and `session_id`.
3. The server publishes an initial snapshot per instrument (top-N depth on bid and ask side).
4. The stream continues with incremental updates reflecting real-time book changes.

### 2. UpdateSubscriptions() _(Subscription Management)_

Modifies an existing subscription to add or remove instruments.

**Request:**
```protobuf
message UpdateSubscriptionRequest {
    uint32 subscriber_id = 1;     // From initialization
    bytes session_id = 2;         // From initialization
    Subscription change = 3;      // Add or remove instruments from client subscription list
}
```

**Response:**
```protobuf
google.protobuf.Empty
```

**Basic Authentication:**
- Requires `subscriber_id` and `session_id` from the initial StreamUpdates connection.
- Invalid credentials return `PERMISSION_DENIED` error.

### Order Book Updates

#### Snapshot Updates

Sent when a client first subscribes to an instrument. Contains the current state of the order book up to the configured depth.
```protobuf
message SnapshotUpdate {
    repeated OrderBookEventUpdate bids = 1;  // Top-N bid levels
    repeated OrderBookEventUpdate asks = 2;  // Top-N ask levels
}
```

#### Incremental Updates

Sent as market data changes occur. Each update modifies a single price level.
```protobuf
message IncrementalUpdate {
    OrderBookEventUpdate update = 1;
}

message OrderBookEventUpdate {
    OrderBookEventType type = 1;  // Delta Type
    Level level = 2;
}

message Level {
    Side side = 1;        // BID or ASK
    uint32 price = 2;     // Price level
    uint32 quantity = 3;  // Quantity to add/remove
}
```

**Event Types:**
- `ADD_LEVEL`: Adds quantity to a price level _(creates level if it doesn't exist)_.
- `REDUCE_LEVEL`: Removes quantity from a price level _(deletes level if quantity reaches zero)_.

The repository also includes a **sample subscriber** that displays a live order book:

```bash
# Subscribe to instrument 1 (default)
./subscriber

# Custom configuration
GRPC_HOST=192.168.1.100 \
GRPC_PORT=50051 \
INSTRUMENT_IDS=1,2,3,4 \
./subscriber
```

## **Styling**

The Market Plant primarily utilizes the naming conventions provided below:

- **[Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html)**
- **[Protobuf Styling](https://protobuf.dev/programming-guides/style/)**
