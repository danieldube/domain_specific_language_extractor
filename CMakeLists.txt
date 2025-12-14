cmake_minimum_required(VERSION 3.20)
project(domain_specific_language_extractor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_library(
  LIBCLANG_LIBRARY
  NAMES clang
  HINTS /usr/lib/llvm-18/lib /usr/lib/llvm-18/lib64 /usr/lib)

find_path(
  LIBCLANG_INCLUDE_DIR
  clang-c/Index.h
  HINTS /usr/lib/llvm-18/include /usr/include)

if(NOT LIBCLANG_LIBRARY OR NOT LIBCLANG_INCLUDE_DIR)
  message(FATAL_ERROR "libclang not found. Install libclang-dev.")
endif()

add_library(
  dsl_core
  src/default_components.cpp src/heuristic_dsl_extractor.cpp
  src/cmake_source_acquirer.cpp src/markdown_reporter.cpp
  src/compile_commands_ast_indexer.cpp)

target_sources(
  dsl_core
  PRIVATE src/default_components.cpp src/heuristic_dsl_extractor.cpp
          src/cmake_source_acquirer.cpp src/markdown_reporter.cpp
  PUBLIC FILE_SET
         HEADERS
         TYPE
         HEADERS
         BASE_DIRS
         ${CMAKE_CURRENT_SOURCE_DIR}/include
         FILES
         include/dsl/default_components.h
         include/dsl/heuristic_dsl_extractor.h
         include/dsl/interfaces.h
         include/dsl/models.h)

target_include_directories(
  dsl_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include PRIVATE
                   ${LIBCLANG_INCLUDE_DIR})

target_link_libraries(dsl_core PRIVATE ${LIBCLANG_LIBRARY})

include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip)
FetchContent_MakeAvailable(googletest)

enable_testing()
include(GoogleTest)

set(TEST_SOURCES
    tests/components_test.cpp tests/cmake_source_acquirer_test.cpp
    tests/markdown_reporter_test.cpp
    tests/compile_commands_ast_indexer_test.cpp)
add_executable(dsl_tests ${TEST_SOURCES})

target_link_libraries(dsl_tests PRIVATE dsl_core GTest::gtest_main GTest::gmock)

gtest_discover_tests(dsl_tests)
