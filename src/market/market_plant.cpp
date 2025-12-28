#include "market_plant.h"

#include <iostream>
#include <thread>
#include <stdexcept>
#include <string>

OrderBook::OrderBook(const Depth depth_,  const InstrumentId instrument_id_)
    : instrument_id(instrument_id_), depth(depth_) {}



static void print_help() {

}

static void parse_config(const char* path) {

} 

static bool parse_args(int argc, char* argv[]) {
    if (argc <= 1) throw std::runtime_error("insufficient options provided.");
    int config_file_idx = -1;

    for (size_t i = 1; i < argc; ++i) {
        std::string option = argv[i];

        if (option == "-h" || option == "--help") {
            print_help();
            return false;
        } else if (option == "-c" || option == "--config") {
            if (i + 1 < argc) {
                config_file_idx = i + 1;
                ++i;
            } else {
                throw std::runtime_error("insufficient arguments provided.");
            }
        } else {
            throw std::runtime_error("invalid option name provided.");
        }
    }

    if (config_file_idx != -1) parse_config(argv[config_file_idx]);
    return true;
}

int main(int argc, char* argv[]) {

    try {
        if (!parse_args(argc, argv)) return 0;
    } catch (const std::runtime_error& e) {
        std::cerr << "Error: " << e.what() << "\n";
        print_help();
        return 1;
    }

    return 0;
}
