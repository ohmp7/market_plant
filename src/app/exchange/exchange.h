#pragma once

#include "event.h"
#include "exchange_config.h"

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

    ExchangeConfig config_;
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
    std::uniform_int_distribution<int> generate_id;
    std::uniform_int_distribution<int> generate_side;
    std::uniform_int_distribution<int> generate_event;
    std::uniform_int_distribution<Price> generate_price;
    std::uniform_int_distribution<Quantity> generate_quantity;
    std::uniform_int_distribution<int> generate_interval;
};