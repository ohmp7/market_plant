#include <cstdint>
#include <cstddef>
#include <cstring>
#include <exception>
#include <iostream>
#include <string>

using Bytes = std::size_t;
using MessageCount = std::uint16_t;
using SequenceNumber = std::uint64_t;

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

struct SequenceGap {
    bool active = false;
    SequenceNumber request_until_sequence_num = 0;  // 0 means we're on track, -1 means uninit
};

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

class MoldUDP64 {
public:
    MoldUDP64(SequenceNumber request_sequence_num_)
        : expected_sequence_num(request_sequence_num_) {}

    void handle_packet(const std::uint8_t* buf, Bytes len) {
        // parse packet header
        if (len < HEADER_LENGTH) throw PacketTruncatedError(len, HEADER_LENGTH);

        Bytes curr_offset = 0;

        std::memcpy(session, buf + curr_offset, SESSION_LENGTH);
        curr_offset += SESSION_LENGTH;

        SequenceNumber sequence_number = read_big_endian<SequenceNumber>(buf, curr_offset);
        curr_offset += sizeof(SequenceNumber);

        MessageCount message_count = read_big_endian<MessageCount>(buf, curr_offset);  // INVARIANT: 1 message per packet
        if (message_count == END_SESSION) message_count = 0;
        curr_offset += sizeof(MessageCount);

        SequenceNumber next_sequence_number = sequence_number + message_count;

        // if the sequence_number > expected_sequence_num -> A packet has been dropped/delayed
            // check if back fill (i.e., request_until_sequence_num = UNKOWN)
                // This basically means you connected to the exchange late and need to catch up (cold start)

                // update request_until_sequence_num to next_sequence_number
                // request the expected_sequence_num to the exchange sim

            // check if gap fill (i.e., you were connected but packets were dropped/not in order)

                // update request_until_sequence_num to next_sequence_number
                // request the expected_sequence_num to the exchange sim

            // else: (we are already in recover mode)
                
                // set the request_until_sequence_num bound to the max between request_until_sequence_num and new_sequence_number

                // retry if enough time has surpased (now - last request > TIMEOUT)
                    // request the expected_sequence_num to the exchange sim
        // else (packet is not ahead) -> in-order, duplicate, or old packet
            // if we were in recovery mode (request_until_sequence_num != 0)

                // if back fill (i.e., request_until_sequence_num = UNKOWN)
                    // reset request_until_sequence_num
                    // update status to synced
                
                // else if the request_until_sequence_num == next_sequence_number (gap_fill)

                    // reset request_until_sequence_num
                    // update status to synced
                
                // else
                    
                    // request the nextSequenceNumber
            
                    // design tradeoff (detecting a gap immediately and requesting the missing range w/o storing all sequences)
            
            // if session is ending
                // end session
            // else if sequence_number < expected_sequence_num
                // return (i.e., old/duplicate packet)
            // else if sequence_number == expected_sequence_num
                // deliver message
                // increment expected_sequence_num
        
    }           


    void end_session() {
        gap.active = false;
        gap.request_until_sequence_num = 0;
    }

private:
    static constexpr Bytes SESSION_LENGTH = 10;
    static constexpr Bytes HEADER_LENGTH = 20;
    static constexpr std::uint16_t TIMEOUT = 1000;  // ms
    static constexpr std::uint16_t END_SESSION = 0xFFFF;
 
    SequenceNumber expected_sequence_num;
    SequenceGap gap;
    char session[SESSION_LENGTH]{};
};