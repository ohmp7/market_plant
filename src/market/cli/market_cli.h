#pragma once

#include "event.h"

#include <cstdint>
#include <string>
#include <vector>

struct Instrument {
    InstrumentId id;
    Depth depth;
};

using InstrumentConfig = std::vector<Instrument>;

struct Config {
    InstrumentConfig instruments;
};

void print_help();

// Returns false if user asked for help; otherwise parses config in `out` and returns true.
bool parse_args(int argc, char* argv[], Config& out);
