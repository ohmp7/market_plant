cmake_minimum_required(VERSION 4.2.1)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(MarketPlant)

function(enable_warnings target)
  target_compile_options(${target} PRIVATE
    -Wall -Wextra -Wpedantic
    -Wshadow -Wconversion -Wsign-conversion
    -Wnull-dereference -Wdouble-promotion
  )
endfunction()

#  Networking Library
add_library(network_lib src/network/moldudp64_client.cpp src/network/udp_messenger.cpp)
enable_warnings(network_lib)
target_include_directories(network_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/network)


# Market Plant Executable
add_executable(market_plant src/market/market_plant.cpp)
enable_warnings(market_plant)
target_link_libraries(market_plant PRIVATE network_lib)


# Exchange Executable
add_executable(exchange src/app/exchange.cpp)
enable_warnings(exchange)

# Subscriber Executable
add_executable(subscriber src/app/subscriber.cpp)
enable_warnings(subscriber)