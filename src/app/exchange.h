#pragma once

#include "event.h"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <random>
#include <unordered_map>
#include <vector>

#include <netinet/in.h>
#include <sys/socket.h>

static constexpr int MAX_PRICE = 100;
static constexpr int MAX_QUANTITY = 100;

struct BookState {
    std::unordered_map<Price, Quantity> levels;
    std::vector<Price> avail_prices;

    BookState();
};

struct InstrumentState {
    BookState bids;
    BookState asks;
};

struct EventToSend {
    MarketEvent event;
    SequenceNumber sequence_number;
};

class ExchangeSimulator {
public:

    ExchangeSimulator();

    void SendDatagrams();

    void GenerateMarketEvents();

    void GenerateHeartbeats();

    void Retransmitter();

private:

    void enqueue_event(const MarketEvent& e, const SequenceNumber sequence_number);

    Price pick_new_price(std::vector<Price>& avail_prices);

    std::unordered_map<Price, Quantity>::iterator pick_existing_price(BookState& book);
    
    void release_price(BookState& book, const Price price_to_release);


    void serialize_event(std::uint8_t* buf, const EventToSend& next);

    Bytes write_moldudp64_header(std::uint8_t* buf, SequenceNumber sequence_number);

    static Timestamp current_time();

    BookState& get_book(InstrumentId id, Side side);

    // network
    int sockfd_;
    sockaddr_in plantaddr_{};
    socklen_t addrlen_ = sizeof(plantaddr_);

    // To be fixed
    static constexpr const char* plant_ip = "127.0.0.1";
    static constexpr std::uint16_t plant_port = 9001;
    static constexpr std::uint16_t exchange_port = 9000;
    inline static constexpr char session[SESSION_LENGTH] = {'E','X','C','H','A','N','G','E','I','D'};

    // Live Exchange State
    std::unordered_map<InstrumentId, InstrumentState> books_;
    std::deque<EventToSend> events_queue;
    std::vector<MarketEvent> events_history_;  // events[sequence_number]
    std::uint64_t sequence_number_{0};
    std::mutex queue_mutex_;
    std::mutex history_mutex_;
    std::condition_variable cv;

    // Generators
    std::mt19937_64 number_generator_{std::random_device{}()};
    std::uniform_int_distribution<int> generate_id{1, 1};
    std::uniform_int_distribution<int> generate_side{0, 1};
    std::uniform_int_distribution<int> generate_event{1, 100};
    std::uniform_int_distribution<int> generate_price{1, MAX_PRICE};
    std::uniform_int_distribution<int> generate_quantity{1, MAX_QUANTITY};
    std::uniform_int_distribution<int> generate_interval{50, 100};
};