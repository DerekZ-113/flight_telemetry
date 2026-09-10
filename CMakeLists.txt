cmake_minimum_required(VERSION 3.16)
project(flight_telemetry VERSION 0.1.0 LANGUAGES CXX)

# Use C++17 standard — modern enough for everything we need,
# widely supported on Pi and Mac
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Compiler warnings — catch problems early, not at runtime
add_compile_options(-Wall -Wextra -Wpedantic)

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

enable_testing()

# CONFIGURE_DEPENDS re-runs the glob at build time, so a new test file is
# picked up without touching this file. The trade-off is a small configure
# cost per build.
file(GLOB UNIT_TEST_SOURCES CONFIGURE_DEPENDS tests/unit/*.cpp)

add_executable(unit_tests ${UNIT_TEST_SOURCES})
# gtest_main supplies main(), so test files contain only TEST() bodies.
target_link_libraries(unit_tests PRIVATE telemetry_core GTest::gtest_main)

# Register each TEST() as its own CTest case, so `ctest` reports them
# individually rather than as one opaque binary.
include(GoogleTest)
gtest_discover_tests(unit_tests)
