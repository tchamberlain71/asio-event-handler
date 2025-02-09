cmake_minimum_required(VERSION 3.10)
project(asio-event-handler)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Boost 1.83 REQUIRED)
find_package(fmt)

include_directories(${Boost_INCLUDE_DIRS})
add_executable(asio-event-handler main.cpp)
target_link_libraries(asio-event-handler ${Boost_LIBRARIES} fmt::fmt)
