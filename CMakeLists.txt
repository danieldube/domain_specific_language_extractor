cmake_minimum_required(VERSION 3.20)
project(domain_specific_language_extractor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_library(
  LIBCLANG_LIBRARY
  NAMES clang
  HINTS /usr/lib/llvm-18/lib /usr/lib/llvm-18/lib64 /usr/lib)

find_path(LIBCLANG_INCLUDE_DIR clang-c/Index.h HINTS /usr/lib/llvm-18/include
                                                     /usr/include)

if(NOT LIBCLANG_LIBRARY OR NOT LIBCLANG_INCLUDE_DIR)
  message(FATAL_ERROR "libclang not found. Install libclang-dev.")
endif()

add_library(
  dsl_core
  src/analyzer_pipeline_builder.cpp
  src/ast_cache.cpp
  src/caching_ast_indexer.cpp
  src/cli_exit_codes.cpp
  src/cmake_source_acquirer.cpp
  src/compile_commands_ast_indexer.cpp
  src/default_analyzer_pipeline.cpp
  src/escaping.cpp
  src/heuristic_dsl_extractor.cpp
  src/logging.cpp
  src/markdown_reporter.cpp
  src/rule_based_coherence_analyzer.cpp)

add_executable(dsl_analyzer src/dsl_analyzer.cpp)
set_target_properties(dsl_analyzer PROPERTIES OUTPUT_NAME "dsl-extract")
add_custom_command(
  TARGET dsl_analyzer
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:dsl_analyzer>
          $<TARGET_FILE_DIR:dsl_analyzer>/dsl_analyzer
  COMMENT "Copy legacy dsl_analyzer compatibility binary")
target_link_libraries(dsl_analyzer PRIVATE dsl_core)
install(TARGETS dsl_analyzer RUNTIME DESTINATION bin)

target_sources(
  dsl_core
  PRIVATE src/analyzer_pipeline_builder.cpp
          src/ast_cache.cpp
          src/caching_ast_indexer.cpp
          src/cli_exit_codes.cpp
          src/cmake_source_acquirer.cpp
          src/compile_commands_ast_indexer.cpp
          src/default_analyzer_pipeline.cpp
          src/escaping.cpp
          src/heuristic_dsl_extractor.cpp
          src/logging.cpp
          src/markdown_reporter.cpp
          src/rule_based_coherence_analyzer.cpp
  PUBLIC FILE_SET
         HEADERS
         TYPE
         HEADERS
         BASE_DIRS
         ${CMAKE_CURRENT_SOURCE_DIR}/include
         FILES
         include/dsl/analyzer_pipeline_builder.h
         include/dsl/ast_cache.h
         include/dsl/caching_ast_indexer.h
         include/dsl/cmake_source_acquirer.h
         include/dsl/cli_exit_codes.h
         include/dsl/compile_commands_ast_indexer.h
         include/dsl/default_analyzer_pipeline.h
         include/dsl/default_components.h
         include/dsl/escaping.h
         include/dsl/heuristic_dsl_extractor.h
         include/dsl/interfaces.h
         include/dsl/logging.h
         include/dsl/markdown_reporter.h
         include/dsl/models.h
         include/dsl/rule_based_coherence_analyzer.h)

target_include_directories(
  dsl_core
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${LIBCLANG_INCLUDE_DIR})

target_link_libraries(dsl_core PRIVATE ${LIBCLANG_LIBRARY})

include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip)
FetchContent_MakeAvailable(googletest)

enable_testing()
include(GoogleTest)

set(TEST_SOURCES
    tests/components_test.cpp
    tests/cmake_source_acquirer_test.cpp
    tests/markdown_reporter_test.cpp
    tests/exit_code_test.cpp
    tests/compile_commands_ast_indexer_test.cpp
    tests/cli_integration_test.cpp
    tests/escaping_test.cpp
    tests/logging_test.cpp)
add_executable(dsl_tests ${TEST_SOURCES})

add_dependencies(dsl_tests dsl_analyzer)

target_link_libraries(dsl_tests PRIVATE dsl_core GTest::gtest_main GTest::gmock)

gtest_discover_tests(dsl_tests)
