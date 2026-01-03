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
- **[`config/config.json`](./config/config.json)** _Runtime configuration (ex. ports, instruments, depth)._

- **[`protos/market_plant/market_plant.proto`](./protos/market_plant/market_plant.proto)** _Protobuf definitions for the Market Plant gRPC API._

- **[`src/app/`](./src/app)** _Top-level applications._
  - **[`exchange.cpp`](./src/app/exchange.cpp)** _Exchange simulator._ 
  - **[`subscriber.cpp`](./src/app/subscriber.cpp)** _gRPC subscriber client._

- **[`src/market/`](./src/market)** _Market Plant core._
  - **[`market_plant.h`](./src/market/market_plant.h)** _Market Plant server._  
  - **[`event.h`](./src/market/event.h)** _Market event types and shared structures._ 
  - **[`cli/market_cli.h`](./src/market/cli/market_cli.h)** _CLI parsing / config wiring._ 

- **[`src/network/`](./src/network)** _Networking + wire-format utilities._
  - **[`moldudp64.h`](./src/network/moldudp64.h)** _MoldUDP64 framing + gap detection/retransmission client logic._  
  - **[`udp_messenger.h`](./src/network/udp_messenger.h)** _UDP socket send wrapper._
  - **[`endian.h`](./src/network/endian.h)** _Big-endian (Network Byte Order) read/write helpers._


## **Usage**
high-level overview

## **Styling**

- **[Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html)**:
- **[Protobuf Styling](https://protobuf.dev/programming-guides/style/)**:
