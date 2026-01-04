#pragma once

#include <string>
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

struct MarketPlantConfig {
    std::string grpc_host;
    std::uint16_t grpc_port;
    std::string market_ip;
    std::uint16_t market_port;
    std::string exchange_ip;
    std::uint16_t exchange_port;

    static MarketPlantConfig New() {
        MarketPlantConfig config;
        
        config.grpc_host = get_env("GRPC_HOST", "0.0.0.0");
        config.grpc_port = static_cast<std::uint16_t>(get_env_int("GRPC_PORT", 50051));
        
        config.market_ip = get_env("MARKET_IP", "127.0.0.1");
        config.market_port = static_cast<std::uint16_t>(get_env_int("MARKET_PORT", 9001));
        
        config.exchange_ip = get_env("EXCHANGE_IP", "127.0.0.1");
        config.exchange_port = static_cast<std::uint16_t>(get_env_int("EXCHANGE_PORT", 9000));

        return config;
    }
    
    std::string GetGrpcAddress() const {
        return grpc_host + ":" + std::to_string(grpc_port);
    }
};