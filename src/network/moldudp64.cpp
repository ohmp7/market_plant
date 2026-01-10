#include "moldudp64.h"
#include "endian.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <exception>
#include <iostream>
#include <optional>
#include <string>

PacketTruncatedError::PacketTruncatedError(Bytes received, Bytes expected)
        : message_("Packet Truncated Error: received " + std::to_string(received) +
                   " bytes, but was expecting >= " + std::to_string(expected)) {}
    
const char* PacketTruncatedError::what() const noexcept {
    return message_.c_str();
}

PacketHeader ParsePacketHeader(const std::uint8_t* buf, const Bytes len) {
    // Parse the given packet's header
    if (len < kHeaderLength) [[unlikely]] throw PacketTruncatedError(len, kHeaderLength);

    Bytes curr_offset = 0;
    PacketHeader header{};

    std::memcpy(header.session, buf + curr_offset, kSessionLength);
    curr_offset += kSessionLength;

    header.sequence_number = ReadBigEndian<SequenceNumber>(buf, curr_offset);
    curr_offset += sizeof(SequenceNumber);

    // INVARIANT: 1 message per packet
    header.message_count = ReadBigEndian<MessageCount>(buf, curr_offset); 

    header.end_of_session = (header.message_count == kEndSession);
    if (header.end_of_session ) [[unlikely]] header.message_count = 0;

    return header;
}

MoldUDP64::MoldUDP64(SequenceNumber request_sequence_num, int sockfd, const std::string& ip, std::uint16_t port)
    : next_expected_sequence_num_(request_sequence_num), messenger_(sockfd, ip, port) {}

bool MoldUDP64::HandlePacket(const std::uint8_t* buf, Bytes len) {
    message_view_ = {};
    const auto& [curr_session, sequence_number, message_count, session_has_ended] = ParsePacketHeader(buf, len);

    if (!session_.set) SetSession(curr_session);

    const SequenceNumber next_sequence_number = sequence_number + message_count;
    
    // If 'next_expected_sequence_num' was constructed with 0, initialize handler to start from the first received packet
    if (next_expected_sequence_num_ == 0) {
        next_expected_sequence_num_ = sequence_number;
    }

    // Check if a packet has been dropped or delayed
    if (sequence_number > next_expected_sequence_num_) { 

        if (!request_until_sequence_num_) {                            // Backfill: (cold start)
            // begin requesting packets until up-to-date
            request_until_sequence_num_ = next_sequence_number;
            Request(next_expected_sequence_num_);

        } else if (*request_until_sequence_num_ == kSynchronized) {     // Gapfill: (previously synchronized, but detected missing packets)
            // begin requesting packets until up-to-date
            request_until_sequence_num_ = next_sequence_number;
            Request(next_expected_sequence_num_);

        } else {                                                     // Already recovering: update recovery window if needed
            request_until_sequence_num_ = std::max(*request_until_sequence_num_, next_sequence_number);
            
            // Throttle retries
            if (Clock::now() - last_request_sent_ > kTimeout) {
                Request(next_expected_sequence_num_);
            }
        }
    } else {

        // Check if the current packet is behind the current up-to-date stream of packets (if so, drop the packet)
        if (sequence_number < next_expected_sequence_num_) return false;

        // Check if handler was previously in recovery state
        if (!request_until_sequence_num_ || *request_until_sequence_num_ != kSynchronized) {
            if (!request_until_sequence_num_) {
                // Cold start: backfilling will now begin
                request_until_sequence_num_ = kSynchronized;

            } else if (*request_until_sequence_num_ == next_sequence_number) {
                // Reached the recovery window's end bound; gap has been filled and recovered
                request_until_sequence_num_ = kSynchronized;
            
            } else {
                // still in recovery state: request next packet
                Request(next_sequence_number);
            }
        }

        if (session_has_ended) [[unlikely]] return false;
        
        // In-order packet parsing (one message per event)
        if (sequence_number == next_expected_sequence_num_) {
            Read(buf, len);
            return true;
        }
    }
    return false;
}

void MoldUDP64::SetSession(const char (&src_session)[kSessionLength]) {
    std::memcpy(session_.session, src_session, kSessionLength);
    session_.set = true;
}

MessageView MoldUDP64::message_view() const { return message_view_; }


void MoldUDP64::Request(SequenceNumber sequence_number) {
    // Send a request packet for retransmission starting from 'sequence_number'
    const SequenceNumber packets_remaining = *request_until_sequence_num_ - sequence_number;
    const SequenceNumber messages_to_send = std::min<SequenceNumber>(packets_remaining, static_cast<SequenceNumber>(kMaxMessageCount));
    const MessageCount message_count = static_cast<MessageCount>(messages_to_send);

    std::uint8_t header[kHeaderLength]{};

    std::memcpy(header, session_.session, kSessionLength);
    WriteBigEndian<SequenceNumber>(header, kSessionLength, sequence_number);
    WriteBigEndian<MessageCount>(header, kSessionLength + sizeof(SequenceNumber), message_count);

    messenger_.SendDatagram(header, kHeaderLength);
    last_request_sent_ = Clock::now();
}

void MoldUDP64::Read(const std::uint8_t* buf, Bytes len) {
    // Read through the packet's message block
    if (kHeaderLength + kMessageHeaderLength > len) [[unlikely]] throw PacketTruncatedError(len, kHeaderLength + kMessageHeaderLength);

    Bytes curr_offset = kHeaderLength;
    std::uint16_t curr_message_len = ReadBigEndian<std::uint16_t>(buf, curr_offset);
    curr_offset += kMessageHeaderLength;

    if (curr_offset + curr_message_len > len) [[unlikely]] throw PacketTruncatedError(len, curr_offset + curr_message_len);
    
    message_view_.data = buf + curr_offset;
    message_view_.len = static_cast<Bytes>(curr_message_len);

    ++next_expected_sequence_num_;
}
