# **Market Data Feed Handler**

This repository showcases a low-latency C++ Market Data Feed Handler simulator _(the **Market Plant**)_ that:

- Ingests UDP unicast datagrams from a simulated exchange using **Nasdaq’s [MoldUDP64](https://www.nasdaqtrader.com/content/technicalsupport/specifications/dataproducts/moldudp64.pdf) Protocol**, including gap detection and retransmission.
- Builds and maintains an in-memory L2 price-level order book from exchange feed data.
- Streams real-time deltas to subscribers via **server-side [gRPC](https://grpc.io/) streaming**.

The motivation behind this project was to build a high-performance middleware service that ingests exchange-style UDP feeds and efficiently scales to one-to-many subscribers.

### _What is a Market Data Feed Handler?_

A Market data feed handler connects exchange feeds with internal trading systems, turning raw data into digestible market data and streaming to downstream consumers at scale. They’re a crucial part of market infrastructure and are designed to move high-volume, latency-sensitive data.

## **Architecture**

<img width="829" height="495" alt="MarketPlantDiagram" src="https://github.com/user-attachments/assets/73399350-64db-483a-91b3-3da1115c8652" />
example

### **Market Feed Data Handler**
high-level overview

### **Networking**
high-level overview

#### **gRPC**
I chose to use gRPC because ...


#### **UDP Unicast**
For Exchange → Plant communication, the simulator sends the feed over **UDP unicast** to replicate how market data is delivered at the wire level. In production, exchange feeds are often **multicast** for efficient one-to-many distribution, with **unicast** used for recovery/retransmission. However, for the scope of this project _(single Exchange, single Market Plant)_, unicast suffices and provides the same low-latency advantages as multicast.

**[`moldudp64_client.h`](./src/network/moldudp64_client.h)** implements a **MoldUDP64** client state machine that:
- Parses the MoldUDP64 header (session, sequence number, message count) and tracks the active session.
- Enforces **in-order processing** using _sequencing_, dropping late/duplicate datagrams.
- Detects **sequence gaps** and enters recovery state (either cold-start backfill or mid-stream gapfill).
- **Retransmits requests** starting at the missing sequence number, throttled by a timeout and bounded by `MAX_MESSAGE_COUNT`.

Below is an example of the message payload utilized (Big-Endian/NBO). As mentioned before, Each **[MoldUDP64](https://www.nasdaqtrader.com/content/technicalsupport/specifications/dataproducts/moldudp64.pdf)** message is encoded as: `msg_len (u16)` then `msg_len` bytes of payload. The offsets below are byte offsets from the start of the UDP datagram buffer.
<br></br>
| Payload Offset (bytes) | Field          | Size | Type |
|---:|---|---:|---|
| 0–3   | `instrument_id` | 4 | `u32` |
| 4     | `side`          | 1 | `u8`  |
| 5     | `event`         | 1 | `u8`  |
| 6–9   | `price`         | 4 | `u32` |
| 10–13 | `quantity`      | 4 | `u32` |
| 14–21 | `exchange_ts`   | 8 | `u64` |
<br></br>

> Note: for this simulator, each UDP datagram carries a single MoldUDP64 message, while preserving the MoldUDP64 Protocol and sequencing semantics.

### **Exchange Simulator**

The Exchange Simulator produces market movement for testing the Market Plant. It continuously generates randomized **L2 price-level events** (add level, remove level, update level) across instruments and sides, serializes each event into **MoldUDP64-framed UDP datagrams**, and sends them to the Market Plant over UDP unicast.

To support gap recovery, the simulator also keeps a fixed-size **in-memory history buffer** keyed by sequence number. When it receives retransmission requests _(MoldUDP64 header containing a starting sequence number and message count)_, it re-enqueues the requested events and replays them back to the Market Plant.

### **Subscriber**
high-level overview

## Project Structure
- **[`config/`](./config)**  
  Runtime configuration (ex. ports, instruments, depth).  
  - **[`config.json`](./config/config.json)**

- **[`protos/market_plant/`](./protos/market_plant)**  
  Protobuf definitions for the Market Plant gRPC API.  
  - **[`market_plant.proto`](./protos/market_plant/market_plant.proto)**

- **[`genproto/market_plant/`](./genproto/market_plant)**  
  Generated protobuf + gRPC C++ sources (checked in for convenience).  
  - **[`market_plant.pb.h`](./genproto/market_plant/market_plant.pb.h) / [`market_plant.pb.cc`](./genproto/market_plant/market_plant.pb.cc)**  
  - **[`market_plant.grpc.pb.h`](./genproto/market_plant/market_plant.grpc.pb.h) / [`market_plant.grpc.pb.cc`](./genproto/market_plant/market_plant.grpc.pb.cc)**

- **[`src/app/`](./src/app)**  
  Top-level applications / entrypoints.  
  - **[`exchange.cpp`](./src/app/exchange.cpp)** / **[`exchange.h`](./src/app/exchange.h)** — Exchange simulator (event generation + UDP publisher + retransmission).  
  - **[`subscriber.cpp`](./src/app/subscriber.cpp)** / **[`subscriber.h`](./src/app/subscriber.h)** — gRPC subscriber client.

- **[`src/market/`](./src/market)**  
  Market Plant core (order books + subscriber fanout + exchange feed ingestion).  
  - **[`market_plant.h`](./src/market/market_plant.h)** / **[`market_plant.cpp`](./src/market/market_plant.cpp)** — Market Plant server implementation.  
  - **[`event.h`](./src/market/event.h)** — Market event types and shared structures.  
  - **[`cli/`](./src/market/cli)** — CLI parsing / config wiring.  
    - **[`market_cli.h`](./src/market/cli/market_cli.h)** / **[`market_cli.cpp`](./src/market/cli/market_cli.cpp)**

- **[`src/network/`](./src/network)**  
  Networking + wire-format utilities.  
  - **[`moldudp64.h`](./src/network/moldudp64.h)** / **[`moldudp64.cpp`](./src/network/moldudp64.cpp)** — MoldUDP64 framing + gap detection/retransmission client logic.  
  - **[`udp_messenger.h`](./src/network/udp_messenger.h)** / **[`udp_messenger.cpp`](./src/network/udp_messenger.cpp)** — UDP socket send wrapper.  
  - **[`endian.h`](./src/network/endian.h)** — Big-endian (network byte order) read/write helpers.

- **[`scripts/`](./scripts)**  
  Helper scripts for development (build/run tooling, etc.).

- **[`CMakeLists.txt`](./CMakeLists.txt)**  
  Build configuration.

- **[`README.md`](./README.md)**  
  Project documentation.


## **Usage**
high-level overview

## **Styling**

- **[Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html)**:
- **[Protobuf Styling](https://protobuf.dev/programming-guides/style/)**:
