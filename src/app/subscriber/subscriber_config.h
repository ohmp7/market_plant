#pragma once

#include "event.h"
#include <string>
#include <vector>
#include <sstream>
#include <cstdint>
#include <cstdlib>

inline std::string get_env(const std::string &key, const std::string &default_value) {
    const char* value = std::getenv(key.c_str());
    return value ? std::string(value) : default_value;
}

inline int get_env_int(const std::string &key, int default_value) {
    const char* value = std::getenv(key.c_str());
    return value ? std::atoi(value) : default_value;
}

struct SubscriberConfig {
    std::string grpc_host;
    std::uint16_t grpc_port;
    std::vector<InstrumentId> instrument_ids;
    Depth display_depth;

    static SubscriberConfig New() {
        SubscriberConfig config;
        
        config.grpc_host = get_env("GRPC_HOST", "127.0.0.1");
        config.grpc_port = static_cast<std::uint16_t>(get_env_int("GRPC_PORT", 50051));
        config.display_depth = static_cast<Depth>(get_env_int("DISPLAY_DEPTH", 10));
        
        std::string ids_str = get_env("INSTRUMENT_IDS", "1");
        std::stringstream ss(ids_str);
        std::string token;
        
        while (std::getline(ss, token, ',')) {
            config.instrument_ids.push_back(static_cast<InstrumentId>(std::stoi(token)));
        }
        
        if (config.instrument_ids.empty()) {
            config.instrument_ids.push_back(1);
        }
        
        return config;
    }
    
    std::string GetAddress() const {
        return grpc_host + ":" + std::to_string(grpc_port);
    }
};
