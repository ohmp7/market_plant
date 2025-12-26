#include <cstdint>
#include <exception>
#include <iostream>
#include <string>

using Bytes = std::size_t;

class PacketTruncatedError : public std::exception {
public:
    PacketTruncatedError(Bytes received, Bytes expected)
        : message_("Packet Truncated Error: received " + std::to_string(received) +
                   " bytes, but was expecting >= " + std::to_string(expected)) {}
    
    const char* what() const noexcept override {
        return message_.c_str();
    }       
private:
    std::string message_;
};

struct SequenceGap {
    bool active = false;
    std::uint64_t request_until_sequence_num = 0;
};

class MoldUDP64 {
public:
    MoldUDP64(std::uint64_t request_sequence_num_)
        : expected_request_sequence_num(request_sequence_num_) {}

    void handle_packet() {}
    void end_session() {}

private:
    static constexpr Bytes SESSION_LENGTH = 10;
    static constexpr std::uint16_t TIMEOUT = 1000;  // ms

    char session[SESSION_LENGTH]{};
    std::uint64_t expected_request_sequence_num;
    SequenceGap gap;

};