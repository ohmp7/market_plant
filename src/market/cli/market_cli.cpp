#include "market_cli.h"

#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

void PrintHelp() {
    std::cout
        << "Usage:\n" 
        << "  market_plant -c <config_file_path>\n"
        << "  market_plant --help\n"
        << "\n"
        << "Options:\n"
        << "  -c, --config   Path to config file\n"
        << "  -h, --help     Provide Market Plant CLI information\n";
}

static void ParseConfig(const char* path, MarketPlantCliConfig& out) {
    std::ifstream config(path);

    if (!config.is_open()) {
        throw std::runtime_error(std::string("unable to open file ") + path);
    }

    rapidjson::IStreamWrapper wrap(config);
    rapidjson::Document doc;
    doc.ParseStream(wrap);  // assume valid JSON

    auto& in = out.instruments;
    const auto& injson = doc["instruments"].GetArray();

    // instruments
    in.reserve(injson.Size());
    for (const auto& i : injson) {
        in.push_back(
            Instrument{static_cast<InstrumentId>(i["instrument_id"].GetUint64()), static_cast<Depth>(i["specifications"]["depth"].GetUint64())}
        );
    }
}

bool ParseArgs(int argc, char* argv[], MarketPlantCliConfig& out) {
    if (argc <= 1) throw std::runtime_error("insufficient options provided.");
    int config_file_idx = -1;
    
    for (int i = 1; i < argc; ++i) {
        std::string option = argv[i];

        if (option == "-h" || option == "--help") {
            PrintHelp();
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
    
    if (config_file_idx != -1) ParseConfig(argv[config_file_idx], out);
    return true;
}