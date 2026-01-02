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
    if (len < HEADER_LENGTH) throw PacketTruncatedError(len, HEADER_LENGTH);

    Bytes curr_offset = 0;
    PacketHeader header{};

    std::memcpy(header.session, buf + curr_offset, SESSION_LENGTH);
    curr_offset += SESSION_LENGTH;

    header.sequence_number = read_big_endian<SequenceNumber>(buf, curr_offset);
    curr_offset += sizeof(SequenceNumber);

    // INVARIANT: 1 message per packet
    header.message_count = read_big_endian<MessageCount>(buf, curr_offset); 

    header.end_of_session = (header.message_count == END_SESSION);
    if (header.end_of_session ) header.message_count = 0;

    return header;
}

MoldUDP64::MoldUDP64(SequenceNumber request_sequence_num_, int sockfd, const std::string& ip, std::uint16_t port)
    : next_expected_sequence_num(request_sequence_num_), messenger(sockfd, ip, port) {}

bool MoldUDP64::handle_packet(const std::uint8_t* buf, Bytes len) {
    msg = {};
    const auto& [curr_session, sequence_number, message_count, session_has_ended] = ParsePacketHeader(buf, len);

    if (!session.set) set_session(curr_session);

    SequenceNumber next_sequence_number = sequence_number + message_count;
    
    // If 'next_expected_sequence_num' was constructed with 0, initialize handler to start from the first received packet
    if (next_expected_sequence_num == 0) {
        next_expected_sequence_num = sequence_number;
    }

    // Check if a packet has been dropped or delayed
    if (sequence_number > next_expected_sequence_num) { 

        if (!request_until_sequence_num) {                            // Backfill: (cold start)
            // begin requesting packets until up-to-date
            request_until_sequence_num = next_sequence_number;
            request(next_expected_sequence_num);

        } else if (*request_until_sequence_num == SYNCHRONIZED) {     // Gapfill: (previously synchronized, but detected missing packets)
            // begin requesting packets until up-to-date
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
        if (sequence_number < next_expected_sequence_num) return false;

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

        if (session_has_ended) return false;
        
        // In-order packet parsing (one message per event)
        if (sequence_number == next_expected_sequence_num) {
            read(buf, len);
            return true;
        }
    }
    return false;
}

void MoldUDP64::set_session(const char (&src_session)[SESSION_LENGTH]) {
    std::memcpy(session.session, src_session, SESSION_LENGTH);
    session.set = true;
}

MessageView MoldUDP64::message_view() const { return msg; }


void MoldUDP64::request(SequenceNumber sequence_number) {
    // Send a request packet for retransmission starting from 'sequence_number'
    SequenceNumber packets_remaining = *request_until_sequence_num - sequence_number;
    SequenceNumber messages_to_send = std::min<SequenceNumber>(packets_remaining, static_cast<SequenceNumber>(MAX_MESSAGE_COUNT));
    MessageCount message_count = static_cast<MessageCount>(messages_to_send);

    std::uint8_t header[HEADER_LENGTH]{};

    std::memcpy(header, session.session, SESSION_LENGTH);
    write_big_endian<SequenceNumber>(header, SESSION_LENGTH, sequence_number);
    write_big_endian<MessageCount>(header, SESSION_LENGTH + sizeof(SequenceNumber), message_count);

    messenger.SendDatagram(header, HEADER_LENGTH);
    last_request_sent = Clock::now();
}

void MoldUDP64::read(const std::uint8_t* buf, Bytes len) {
    // Read through the packet's message block
    if (HEADER_LENGTH + MESSAGE_HEADER_LENGTH > len) throw PacketTruncatedError(len, HEADER_LENGTH + MESSAGE_HEADER_LENGTH);

    Bytes curr_offset = HEADER_LENGTH;
    std::uint16_t curr_message_len = read_big_endian<std::uint16_t>(buf, curr_offset);
    curr_offset += MESSAGE_HEADER_LENGTH;

    if (curr_offset + curr_message_len > len) throw PacketTruncatedError(len, curr_offset + curr_message_len);
    
    msg.data = buf + curr_offset;
    msg.len = static_cast<Bytes>(curr_message_len);

    ++next_expected_sequence_num;
}
