cmake_minimum_required(VERSION 3.20)
project(domain_specific_language_extractor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(dsl_core
    src/default_components.cpp
)

target_include_directories(dsl_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()
include(GoogleTest)

set(TEST_SOURCES
    tests/components_test.cpp
)
add_executable(dsl_tests ${TEST_SOURCES})

target_link_libraries(dsl_tests
    PRIVATE
        dsl_core
        GTest::gtest_main
        GTest::gmock
)

gtest_discover_tests(dsl_tests)
