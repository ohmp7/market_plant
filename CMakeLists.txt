cmake_minimum_required(VERSION 4.2.1)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

project(MarketPlant)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

function(enable_warnings target)
  target_compile_options(${target} PRIVATE
    -Wall -Wextra -Wpedantic
    -Wshadow -Wconversion -Wsign-conversion
    -Wnull-dereference -Wdouble-promotion
  )
endfunction()

# find necessary packages
set(CMAKE_FIND_PACKAGE_PREFER_CONFIG ON)

find_package(Protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)
find_package(utf8_range REQUIRED)

# Set necessary variables for genproto lib target
set(GENPROTO_LIB "genproto_lib")
set(GENPROTO_DIR "${PROJECT_SOURCE_DIR}/genproto")

# loop through genproto dir
file(GLOB_RECURSE PROTO_SRC "${GENPROTO_DIR}/*.pb.cc")
file(GLOB_RECURSE PROTO_HDR "${GENPROTO_DIR}/*.pb.h")

add_library(${GENPROTO_LIB} ${PROTO_SRC} ${PROTO_HDR})

target_link_libraries(${GENPROTO_LIB}
  PUBLIC
    protobuf::libprotobuf
    gRPC::grpc++
    utf8_range::utf8_range
    utf8_range::utf8_validity
)

target_include_directories(${GENPROTO_LIB}
  PUBLIC
    ${GENPROTO_DIR}
)

#  Networking Library
add_library(network_lib 
  src/network/moldudp64.cpp 
  src/network/udp_messenger.cpp
)
enable_warnings(network_lib)
target_include_directories(network_lib PUBLIC 
  ${CMAKE_CURRENT_SOURCE_DIR}/src/network
)
target_include_directories(network_lib PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src/market
)

# Market Plant Executable
add_executable(market_plant
  src/market/market_plant.cpp 
  src/market/cli/market_cli.cpp
)
enable_warnings(market_plant)
target_include_directories(market_plant PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src/market       
  ${CMAKE_CURRENT_SOURCE_DIR}/src/market/cli
  /opt/homebrew/include
)
target_link_libraries(market_plant PRIVATE 
  network_lib
  ${GENPROTO_LIB}
)

# Exchange Executable
add_executable(exchange 
  src/app/exchange/exchange.cpp
)
target_link_libraries(exchange PRIVATE network_lib)
target_include_directories(exchange PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src/market       
)
enable_warnings(exchange)


# Subscriber Executable
add_executable(subscriber 
  src/app/subscriber/subscriber.cpp
)
enable_warnings(subscriber)
target_link_libraries(subscriber PRIVATE 
  ${GENPROTO_LIB}
)
target_include_directories(subscriber PRIVATE
  ${GENPROTO_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/src/market
  ${CMAKE_CURRENT_SOURCE_DIR}/src/app/subscriber
)

enable_warnings(subscriber)

