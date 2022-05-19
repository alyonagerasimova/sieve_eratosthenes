cmake_minimum_required(VERSION 3.22)
project(resheto_eratosthenes)

set(CMAKE_CXX_STANDARD 14)

include_directories(.)

add_executable(resheto_eratosthenes resheto.cpp)
target_link_libraries(resheto_eratosthenes pthread m)
SET(TARGET_SRC basesim.hpp templet.hpp)