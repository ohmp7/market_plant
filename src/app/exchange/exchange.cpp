#include "endian.h"
#include "exchange.h"
#include "moldudp64.h"
#include "udp_messenger.h"

#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <iostream>
#include <mutex>
#include <random>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

BookState::BookState() {
    avail_prices.reserve(MAX_PRICE);

    for (int i = 1; i <= 100; ++i) {
        avail_prices.push_back(static_cast<Price>(i));
    }
}

ExchangeSimulator::ExchangeSimulator()
    : sockfd_(socket(AF_INET, SOCK_DGRAM, 0)),
      config_(ExchangeConfig::New()),
      generate_id(config_.min_instrument_id, config_.max_instrument_id),
      generate_side(0, 1),
      generate_event(1, 100),
      generate_price(config_.min_price, config_.max_price),
      generate_quantity(config_.min_quantity, config_.max_quantity),
      generate_interval(config_.min_interval_ms, config_.max_interval_ms) {

    if (sockfd_ < 0) throw std::runtime_error("Error: socket creation to exchange failed.");

    memset(&plantaddr_, 0, sizeof(plantaddr_));
    plantaddr_.sin_family = AF_INET;
    plantaddr_.sin_port = htons(config_.exchange_port);
    plantaddr_.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sockfd_, reinterpret_cast<sockaddr*>(&plantaddr_), sizeof(plantaddr_)) < 0) {
        close(sockfd_);
        sockfd_ = -1;
        throw std::runtime_error("Error: bind failed.");
    }

    {
        std::lock_guard<std::mutex> lock(history_mutex_);
        events_history_.resize(MAX_EXCHANGE_EVENTS);
    }
}

void ExchangeSimulator::SendDatagrams() {
    UdpMessenger messenger(sockfd_, config_.plant_ip.c_str(), config_.plant_port);

    while (true) {

        // wait for event
        std::unique_lock<std::mutex> lock(queue_mutex_);
        while (events_queue.empty()) {
            cv.wait(lock);
        }

        EventToSend next = events_queue.front(); 
        events_queue.pop_front();

        lock.unlock();

        std::uint8_t buf[PACKET_SIZE];
        serialize_event(buf, next);

        // TODO: add retry functionality later
        messenger.SendDatagram(buf, PACKET_SIZE);
    }
}

void ExchangeSimulator::GenerateMarketEvents() {
    while (true) {
        const InstrumentId id = static_cast<InstrumentId>(generate_id(number_generator_));
        const Side side = static_cast<Side>(generate_side(number_generator_));
        BookState& book = get_book(id, side);

        const bool add_level = book.levels.empty() || generate_event(number_generator_) <= config_.chance_of_add;
        
        MarketEvent e{};

        if (add_level) {
            // update live state
            const Quantity quantity = static_cast<Quantity>(generate_quantity(number_generator_));

            // decide new price or existing price
            const bool new_price = generate_event(number_generator_) <= config_.chance_of_new_price;

            Price price;

            if (book.levels.empty() || new_price) {
                price = pick_new_price(book.avail_prices);
                book.levels[price] = quantity;
            } else {
                auto it = pick_existing_price(book);
                price = it->first;
                book.levels[price] += quantity;
            }
            
            e.instrument_id = id;
            e.side = side;
            e.event = LevelEvent::AddLevel;
            e.price = price;
            e.quantity = quantity;
            e.exchange_ts = current_time();
            
        } else {
            auto it = pick_existing_price(book);
            const Price price = it->first;
            const Quantity curr_quantity = it->second;

            const bool delete_level = generate_event(number_generator_) <= config_.chance_of_delete;
            Quantity quantity_to_remove;

            if (delete_level) {
                quantity_to_remove = curr_quantity;
                release_price(book, price);
            } else {
                std::uniform_int_distribution<Quantity> generate_quantity_to_remove(1, curr_quantity - 1);
                quantity_to_remove = generate_quantity_to_remove(number_generator_);
                it->second -= quantity_to_remove;
            }

            e.instrument_id = id;
            e.side = side;
            e.event = LevelEvent::ModifyLevel;
            e.price = price;
            e.quantity = quantity_to_remove;
            e.exchange_ts = current_time();
        }
        
        SequenceNumber seq;
        {
            std::lock_guard<std::mutex> lock(history_mutex_);
            seq = sequence_number_++;
            events_history_[seq] = e;
        }

        enqueue_event(e, seq);
        
        Timestamp sleep = static_cast<Timestamp>(generate_interval(number_generator_));
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep));
    }
}

void ExchangeSimulator::Retransmitter() {
    PacketHeader header;
    while (true) {
        uint8_t buf[HEADER_LENGTH];
        
        ssize_t bytes_received = recvfrom(sockfd_, buf, HEADER_LENGTH, 0, nullptr, nullptr);
        if (bytes_received <= 0) continue;

        try {
            header = ParsePacketHeader(buf, static_cast<Bytes>(bytes_received));
        } catch (const PacketTruncatedError& e) {
            std::cerr << e.what() << "\n";
            continue;
        }

        if (std::memcmp(header.session, session, SESSION_LENGTH) == 0) {
            for (MessageCount i = 0; i < header.message_count; ++i) {
                std::lock_guard<std::mutex> lock(history_mutex_);

                const SequenceNumber seq = header.sequence_number + i;
                if (seq >= sequence_number_) break;
                enqueue_event(events_history_[seq], seq);
            }
        }
    }
}

void ExchangeSimulator::enqueue_event(const MarketEvent& e, const SequenceNumber sequence_number) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    events_queue.push_back(EventToSend{e, sequence_number});
    cv.notify_one();
}

Price ExchangeSimulator::pick_new_price(std::vector<Price>& avail_prices) {
    std::uniform_int_distribution<std::size_t> generate_idx(0, avail_prices.size() - 1);
    std::size_t i = generate_idx(number_generator_);

    Price p = avail_prices[i];
    avail_prices[i] = avail_prices.back();
    avail_prices.pop_back();
    return p;
}

std::unordered_map<Price, Quantity>::iterator ExchangeSimulator::pick_existing_price(BookState& book) {
    std::uniform_int_distribution<std::size_t> generate_it(0, book.levels.size() - 1);
    std::size_t skip = generate_it(number_generator_);

    auto it = book.levels.begin();
    std::advance(it, skip);
    return it;
}
    
void ExchangeSimulator::release_price(BookState& book, const Price price_to_release) {
    book.levels.erase(price_to_release);
    book.avail_prices.push_back(price_to_release);
}

void ExchangeSimulator::serialize_event(std::uint8_t* buf, const EventToSend& next) {
    const MarketEvent& event = next.event;
    Bytes offset = write_moldudp64_header(buf, next.sequence_number);

    write_big_endian<InstrumentId>(buf, offset, event.instrument_id);
    offset += sizeof(InstrumentId);

    write_big_endian<uint8_t>(buf, offset, static_cast<uint8_t>(event.side));
    offset += sizeof(Side);

    write_big_endian<uint8_t>(buf, offset, static_cast<uint8_t>(event.event));
    offset += sizeof(event.event);

    write_big_endian<Price>(buf, offset, event.price);
    offset += sizeof(Price);

    write_big_endian<Quantity>(buf, offset, event.quantity);
    offset += sizeof(Quantity);

    write_big_endian<Timestamp>(buf, offset, event.exchange_ts);
    
}

Bytes ExchangeSimulator::write_moldudp64_header(std::uint8_t* buf, SequenceNumber sequence_number) {
    Bytes offset = 0;

    std::memcpy(buf, session, SESSION_LENGTH);
    offset += SESSION_LENGTH;

    write_big_endian<SequenceNumber>(buf, offset, sequence_number);
    offset += sizeof(SequenceNumber);

    write_big_endian<MessageCount>(buf, offset, MESSAGE_COUNT);
    offset += sizeof(MessageCount);

    const Bytes remaining = PACKET_SIZE - (offset + sizeof(MessageDataSize));
    write_big_endian<MessageDataSize>(buf, offset, static_cast<MessageDataSize>(remaining));

    offset += sizeof(MessageDataSize);

    return offset;
}

Timestamp ExchangeSimulator::current_time() {
    return static_cast<Timestamp>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()
    );
}

BookState& ExchangeSimulator::get_book(InstrumentId id, Side side) {
    if (side == Side::BID) return books_[id].bids;
    else return books_[id].asks;
}


int main() {
    ExchangeSimulator exchange;
    
    std::thread sender([&] {exchange.SendDatagrams(); } );
    std::thread generator([&] {exchange.GenerateMarketEvents(); } );
    std::thread retransmitter([&]{ exchange.Retransmitter(); });

    std::cout << "Exchange simulator has started.\n";

    generator.join();
}