cmake_minimum_required(VERSION 3.16)
project(flight_telemetry VERSION 0.1.0 LANGUAGES CXX)

# Use C++17 standard — modern enough for everything we need,
# widely supported on Pi and Mac
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Compiler warnings — catch problems early, not at runtime
add_compile_options(-Wall -Wextra -Wpedantic)

# Build policy switches. Defaults suit a developer's Mac and the Pi; CI
# flips both ON. Kept as options rather than hardcoded so one CMakeLists
# serves every build without editing.
#
# WARNINGS_AS_ERRORS: FTS-TP-001 section 3 says a warning is a failed
# build, but only where the build is the gate. Unconditional -Werror would
# let a newer compiler on the Pi break the build on a warning nobody has
# seen yet.
option(WARNINGS_AS_ERRORS "Treat compiler warnings as errors (CI)" OFF)
# ENABLE_COVERAGE: gcov instrumentation for REQ-TEST-003. Never on for a
# release or Pi build; it slows the code and writes .gcda files at exit.
option(ENABLE_COVERAGE "Instrument telemetry_core and unit_tests for gcov/lcov" OFF)

if(WARNINGS_AS_ERRORS)
    add_compile_options(-Werror)
endif()

# Everything except main() lives in a static library so the executable and
# the unit tests link the same object code. Without this, each test target
# would recompile every source, and a test could pass against code that
# differs from what ships.
add_library(telemetry_core STATIC
    src/telemetry_frame.cpp
    src/processing/altitude.cpp
    src/processing/processor.cpp
    src/processing/kalman_filter.cpp
    src/processing/complementary_filter.cpp
    src/processing/fault_detector.cpp
    src/drivers/simulated_data.cpp
    src/drivers/simulated_source.cpp
    src/drivers/fault_injecting_source.cpp
    src/drivers/i2c_bus.cpp
    src/drivers/bmp280.cpp
    src/drivers/live_sensor_source.cpp
    src/drivers/mpu6050.cpp
    src/drivers/nmea.cpp
    src/drivers/serial_port.cpp
    src/drivers/nmea_gps.cpp
    src/logging/log_format.cpp
    src/logging/binary_logger.cpp
    src/logging/log_reader.cpp
    src/replay/log_replay_source.cpp
    src/timing/clock.cpp
    src/timing/fixed_rate_scheduler.cpp
    src/timing/jitter_log.cpp
    src/transport/zmq_publisher.cpp
    src/transport/udp_publisher.cpp
    src/config/telemetry_config.cpp
)

# PUBLIC: anything that links telemetry_core (the executable, the tests)
# also gets this include path, so #include "processing/kalman.h" works there.
target_include_directories(telemetry_core PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

# Main executable
add_executable(telemetry src/main.cpp)
target_link_libraries(telemetry PRIVATE telemetry_core)

# ---------------------------------------------------------------------------
# Unit tests (REQ-TEST-001): Google Test, fetched at configure time so the
# build has no system dependency on a preinstalled gtest.
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2   # pinned: an unpinned tag would make builds non-reproducible
)
# Don't install gtest alongside the project, and don't build its own tests.
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# ---------------------------------------------------------------------------
# ZeroMQ transport (REQ-TRANS-001). Two pieces. libzmq, the C library, comes
# from the system like the compiler does (brew install zeromq; apt-get
# install libzmq3-dev): a shared library with a stable ABI, the same 4.3.5
# on the Mac, the CI runner, and the Pi. cppzmq, the header-only C++
# wrapper, is compiled into our objects, so it is fetched and pinned to a
# tag like Google Test: every build compiles the same wrapper. cppzmq's own
# CMake finds libzmq (CMake package, then pkg-config, then a plain path
# search) and exports the `cppzmq` target that carries both.
# ---------------------------------------------------------------------------
FetchContent_Declare(
    cppzmq
    GIT_REPOSITORY https://github.com/zeromq/cppzmq.git
    GIT_TAG        v4.11.0   # pinned, same reason as googletest
)
# Its tests default to ON and would fetch Catch2 and build dozens of targets.
set(CPPZMQ_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(cppzmq)

# PUBLIC: the unit tests open their own SUB socket to read what the
# publisher sends, so they need the headers and the library too.
target_link_libraries(telemetry_core PUBLIC cppzmq)

# ---------------------------------------------------------------------------
# YAML configuration (REQ-CFG-001). yaml-cpp is the document parser behind
# src/config/telemetry_config.cpp, the one translation unit that includes
# it; no header of ours exposes it, so the link is PRIVATE (a static
# library still carries the dependency to the executable and the tests).
# Fetched and pinned to a tag like Google Test. 0.9.0 is the first release
# whose cmake_minimum_required (3.5...3.30) a CMake 4 host accepts; 0.8.0
# declares 3.4 and does not configure there. Its tools, contrib, tests,
# install rules and formatter are off: this build needs the library only.
# Third-party sources sit outside the project's warning policy (FTS-TP-001
# section 3 governs src/); yaml-cpp applies its own warning set only as
# the main project and compiles clean under ours.
# ---------------------------------------------------------------------------
FetchContent_Declare(
    yaml-cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG        yaml-cpp-0.9.0   # pinned, same reason as googletest
)
set(YAML_CPP_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_INSTALL OFF CACHE BOOL "" FORCE)
set(YAML_CPP_FORMAT_SOURCE OFF CACHE BOOL "" FORCE)
set(YAML_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(yaml-cpp)
target_link_libraries(telemetry_core PRIVATE yaml-cpp::yaml-cpp)

enable_testing()

# CONFIGURE_DEPENDS re-runs the glob at build time, so a new test file is
# picked up without touching this file. The trade-off is a small configure
# cost per build.
file(GLOB UNIT_TEST_SOURCES CONFIGURE_DEPENDS tests/unit/*.cpp)

add_executable(unit_tests ${UNIT_TEST_SOURCES})
# The configuration tests load the shipped file itself (TC-016 step 1, the
# drift test), so its path is compiled in rather than guessed from whatever
# directory ctest runs from.
target_compile_definitions(unit_tests PRIVATE
    TELEMETRY_SHIPPED_CONFIG="${CMAKE_SOURCE_DIR}/config/telemetry_config.yaml")
# gtest_main supplies main(), so test files contain only TEST() bodies.
target_link_libraries(unit_tests PRIVATE telemetry_core GTest::gtest_main)

# Register each TEST() as its own CTest case, so `ctest` reports them
# individually rather than as one opaque binary.
include(GoogleTest)
gtest_discover_tests(unit_tests)

# Coverage flags go on our two targets only, not globally: instrumenting
# gtest itself would put thousands of library branches into the report
# and drown the numbers that matter. -O0 keeps every branch in the code
# as written; the optimizer would otherwise fold some away before gcov
# can count them.
if(ENABLE_COVERAGE)
    foreach(t telemetry_core unit_tests)
        target_compile_options(${t} PRIVATE --coverage -O0 -g)
    endforeach()
    # The link flag is PUBLIC on the library: anything that links
    # instrumented object code (the tests, and the telemetry executable)
    # must also link the profiling runtime, or the build fails with
    # undefined __gcov_* symbols.
    target_link_options(telemetry_core PUBLIC --coverage)
endif()
