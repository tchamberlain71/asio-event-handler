cmake_minimum_required(VERSION 3.10)
project(asio-event-handler)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Boost 1.83 REQUIRED)
find_package(azmq REQUIRED)
find_package(fmt REQUIRED)
find_package(spdlog REQUIRED)

include_directories(${Boost_INCLUDE_DIRS})

add_executable(template template.cpp)
target_link_libraries(template ${Boost_LIBRARIES} fmt::fmt)

add_executable(asio-event-handler main.cpp)
target_link_libraries(asio-event-handler Azmq::azmq ${Boost_LIBRARIES} spdlog::spdlog)
