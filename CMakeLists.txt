cmake_minimum_required(VERSION 3.16)
project(flight_telemetry VERSION 0.1.0 LANGUAGES CXX)

# Use C++17 standard — modern enough for everything we need,
# widely supported on Pi and Mac
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Compiler warnings — catch problems early, not at runtime
add_compile_options(-Wall -Wextra -Wpedantic)

# Main executable — add source files as we build each component
add_executable(telemetry
    src/main.cpp
    src/processing/altitude.cpp
)

# Include directories — so #include "processing/kalman.h" works
target_include_directories(telemetry PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
