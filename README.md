# **Market Data Feed Handler**

This repository showcases a low-latency C++ market data feed handler simulator _(referred to as the **Market Plant**)_, which:

- ingests UDP unicast datagrams from a simulated exchange using Nasdaq’s **[MoldUDP64](https://www.nasdaqtrader.com/content/technicalsupport/specifications/dataproducts/moldudp64.pdf) Protocol**, including gap detection and retransmission.
- Maintains an in-memory **L2 price-level order book**
- Streams real-time updates to subscribers via **server-side [gRPC](https://grpc.io/) streaming**.

The motivation behind this project was to build a high-performance middleware service that ingests exchange-style UDP feeds and efficiently scales to one-to-many subscribers.

## **Architecture**

<img width="829" height="495" alt="MarketPlantDiagram" src="https://github.com/user-attachments/assets/73399350-64db-483a-91b3-3da1115c8652" />
example

### **Market Feed Data Handler**
high-level overview

### **Networking**
high-level overview

#### **gRPC**
example

#### **UDP Unicast**

Below is an example of the message payload utilized (big-endian / Network Byte Order). _Offsets are byte offsets from the start of the UDP datagram buffer._ As mentioned before, Eech MoldUDP64 message is encoded as: `msg_len (u16)` then `msg_len` bytes of payload. The offsets below are relative to the start of the payload (immediately after `msg_len`).

| Payload Offset (bytes) | Field          | Size | Type |
|---:|---|---:|---|
| 0–3   | `instrument_id` | 4 | `u32` |
| 4     | `side`          | 1 | `u8`  |
| 5     | `event`         | 1 | `u8`  |
| 6–9   | `price`         | 4 | `u32` |
| 10–13 | `quantity`      | 4 | `u32` |
| 14–21 | `exchange_ts`   | 8 | `u64` |



### **Exchange**
high-level overview

### **Subscriber**
high-level overview

## **Project Structure**
high-level overview

## **Usage**
high-level overview

## **Styling**

- **[Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html)**:
- **[Protobuf Styling](https://protobuf.dev/programming-guides/style/)**:
