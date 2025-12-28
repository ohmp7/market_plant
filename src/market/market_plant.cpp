#include "market_plant.h"

#include <iostream>
#include <thread>


OrderBook::OrderBook(const Depth depth_,  const InstrumentId instrument_id_)
    : instrument_id(instrument_id_), depth(depth_) {}


int main(int argc, char* argv[]) {

    std::cout << "hello word!" << "\n";
    return 0;
}
