#pragma once

#include "event.h"
#include <string>
#include <cstdint>
#include <cstdlib>
#include <iostream>


inline std::string get_env(const std::string &key, const std::string &default_value) {
    const char* value = std::getenv(key.c_str());
    return (value != nullptr) ? std::string(value) : default_value;
}

inline int get_env_int(const std::string &key, int default_value) {
    const char* value = std::getenv(key.c_str());
    return (value != nullptr) ? std::atoi(value) : default_value;
}

struct ExchangeConfig {
    // Network
    std::string plant_ip;
    std::uint16_t plant_port;
    std::uint16_t exchange_port;
    
    // Market generation probabilities
    int chance_of_add;
    int chance_of_delete;
    int chance_of_new_price;
    
    // Timing range
    int min_interval_ms;
    int max_interval_ms;
    
    // Instrument range
    int min_instrument_id;
    int max_instrument_id;
    
    Price min_price;
    Price max_price;
    Quantity min_quantity;
    Quantity max_quantity;

    static ExchangeConfig New() {

        ExchangeConfig config;
        
        config.plant_ip = get_env("PLANT_IP", "127.0.0.1");

        config.plant_port = static_cast<std::uint16_t>(get_env_int("PLANT_PORT", 9001));
        config.exchange_port = static_cast<std::uint16_t>(get_env_int("EXCHANGE_PORT", 9000));
        
        config.chance_of_add = get_env_int("CHANCE_OF_ADD", 55);
        config.chance_of_delete = get_env_int("CHANCE_OF_DELETE", 50);
        config.chance_of_new_price = get_env_int("CHANCE_OF_NEW_PRICE", 50);
        
        config.min_interval_ms = get_env_int("MIN_INTERVAL_MS", 50);
        config.max_interval_ms = get_env_int("MAX_INTERVAL_MS", 100);
        
        config.min_instrument_id = get_env_int("MIN_INSTRUMENT_ID", 1);
        config.max_instrument_id = get_env_int("MAX_INSTRUMENT_ID", 1);
        
        config.min_price = static_cast<Price>(get_env_int("MIN_PRICE", 1));
        config.max_price = static_cast<Price>(get_env_int("MAX_PRICE", 100));
        config.min_quantity = static_cast<Quantity>(get_env_int("MIN_QUANTITY", 1));
        config.max_quantity = static_cast<Quantity>(get_env_int("MAX_QUANTITY", 100));
        
        return config;
    }
};