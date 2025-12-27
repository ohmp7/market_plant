#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <exception>
#include <iostream>
#include <optional>
#include <string>

using Bytes = std::size_t;
using MessageCount = std::uint16_t;
using SequenceNumber = std::uint64_t;
using Clock = std::chrono::steady_clock;

class PacketTruncatedError : public std::exception {
public:
    PacketTruncatedError(Bytes received, Bytes expected)
        : message_("Packet Truncated Error: received " + std::to_string(received) +
                   " bytes, but was expecting >= " + std::to_string(expected)) {}
    
    virtual const char* what() const noexcept override {
        return message_.c_str();
    }
private:
    std::string message_;
};

/*
Handling Network-Byte-Order Integers.
*/
template <typename T>
static T read_big_endian(const std::uint8_t* buf,  Bytes offset) {
    T converted = 0;
    for (Bytes i = 0; i < sizeof(T); ++i) {
        // shift left 8 bits
        converted = converted << 8;
        // get next byte from buf
        std::uint8_t next_byte = buf[offset + i];
        // OR operation with lower 8 bits
        converted = converted | static_cast<T>(next_byte);
    }
    return converted;
}

/*
Client Handler for MoldUDP64 Network Protocol, a lightweight protocol layer built on top of UDP.
*/
class MoldUDP64 {
public:
    MoldUDP64(SequenceNumber request_sequence_num_)
        : next_expected_sequence_num(request_sequence_num_) {}

    void handle_packet(const std::uint8_t* buf, Bytes len) {
        // Parse the given packet's header
        if (len < HEADER_LENGTH) throw PacketTruncatedError(len, HEADER_LENGTH);

        Bytes curr_offset = 0;

        std::memcpy(session, buf + curr_offset, SESSION_LENGTH);
        curr_offset += SESSION_LENGTH;

        SequenceNumber sequence_number = read_big_endian<SequenceNumber>(buf, curr_offset);
        curr_offset += sizeof(SequenceNumber);

        // INVARIANT: 1 message per packet
        MessageCount message_count = read_big_endian<MessageCount>(buf, curr_offset); 
        bool session_has_ended = (message_count == END_SESSION);
        if (session_has_ended) message_count = 0;
        curr_offset += sizeof(MessageCount);

        SequenceNumber next_sequence_number = sequence_number + message_count;
        
        // If 'next_expected_sequence_num' was constructed with 0, initialize handler to start from the first received packet
        if (next_expected_sequence_num == 0) {
            next_expected_sequence_num = sequence_number;
        }

        // Check if a packet has been dropped or delayed
        if (sequence_number > next_expected_sequence_num) { 
   
            if (!request_until_sequence_num) {                            // Backfill: (cold start)
                // begin requesting packets up until up-to-date
                request_until_sequence_num = next_sequence_number;
                request(next_expected_sequence_num);

            } else if (*request_until_sequence_num == SYNCHRONIZED) {     // Gapfill: (previously synchronized, but detected missing packets)
                // begin requesting packets up until up-to-date
                request_until_sequence_num = next_sequence_number;
                request(next_expected_sequence_num);

            } else {                                                     // Already recovering: update recovery window if needed
                request_until_sequence_num = std::max(*request_until_sequence_num, next_sequence_number);
                
                // Throttle retries
                if (Clock::now() - last_request_sent > TIMEOUT) {
                    request(next_expected_sequence_num);
                }
            }
        } else {

            // Check if the current packet is behind the current up-to-date stream of packets (if so, drop the packet)
            if (sequence_number < next_expected_sequence_num) return;

            // Check if handler was previously in recovery state
            if (!request_until_sequence_num || *request_until_sequence_num != SYNCHRONIZED) {
                if (!request_until_sequence_num) {
                    // Cold start: backfilling will now begin
                    request_until_sequence_num = SYNCHRONIZED;

                } else if (*request_until_sequence_num == next_sequence_number) {
                    // Reached the recovery window's end bound; gap has been filled and recovered
                    request_until_sequence_num = SYNCHRONIZED;
                
                } else {
                    // still in recovery state: request next packet
                    request(next_sequence_number);

                }
            }

            if (session_has_ended) return;
           
            // In-order packet parsing (one message per event)
            if (sequence_number == next_expected_sequence_num) read();
        }
    }

    void request(SequenceNumber sequence_number) {
        // Send a request packet for retransmission starting from 'sequence_number'

        last_request_sent = Clock::now();
    }

    void read() {
        ++next_expected_sequence_num;
    }

private:
    static constexpr Bytes SESSION_LENGTH = 10;
    static constexpr Bytes HEADER_LENGTH = 20;
    static constexpr std::uint16_t END_SESSION = 0xFFFF;
    static constexpr auto TIMEOUT = std::chrono::milliseconds(1000);

    // State Helpers 
    static constexpr std::nullopt_t SEQUENCE_LIMIT_UNKNOWN = std::nullopt;
    static constexpr SequenceNumber SYNCHRONIZED = 0;

    Clock::time_point last_request_sent{};

    // next sequence number in order
    SequenceNumber next_expected_sequence_num;

    // Recovery window upper bound (exclusive)
    std::optional<SequenceNumber> request_until_sequence_num = SEQUENCE_LIMIT_UNKNOWN;
    
    char session[SESSION_LENGTH]{};
};