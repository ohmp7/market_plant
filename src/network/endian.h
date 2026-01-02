#pragma once

#include "event.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

/*
Handling Network-Byte-Order Integers.
*/
template <typename T>
inline T read_big_endian(const std::uint8_t* buf,  Bytes offset) {
    T converted = 0;
    for (Bytes i = 0; i < sizeof(T); ++i) {
        converted <<= 8;
        std::uint8_t next_byte = buf[offset + i];
        converted = converted | static_cast<T>(next_byte);
    }
    return converted;
}

template <typename T>
inline void write_big_endian(std::uint8_t* buf, Bytes offset, T value) {
    for (Bytes i = 0; i < sizeof(T); ++i) {
        buf[offset + sizeof(T) - i - 1] = static_cast<uint8_t>(value & 0xFF);
        if constexpr (sizeof(T) > 1) value >>= 8;
    }
}