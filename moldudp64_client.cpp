#include <cstdint>
#include <cstddef>
#include <exception>
#include <iostream>
#include <string>

using Bytes = std::size_t;

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
    std::uint64_t request_until_sequence_num = 0;
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
    MoldUDP64(std::uint64_t request_sequence_num_)
        : expected_sequence_num(request_sequence_num_) {}

    void handle_packet(const std::uint8_t* buf, Bytes len) {
        // MoldUDP64 header truncate check
        if (len < HEADER_LENGTH) throw PacketTruncatedError(len, HEADER_LENGTH);

        Bytes curr_offset = 0;

        // get first 10 bytes for session (endian conversion is not needed)

        // get the next 8 bytes for sequence number and convert to endian 

        // get the next 2 bytes for message count (should always be 1 for now)

        // current_sequence_number = sequence_number + message_count
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

    char session[SESSION_LENGTH]{};
    std::uint64_t expected_sequence_num;
    SequenceGap gap;
};