#include <dsl/models.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "test_support/temporary_project.h"

namespace dsl {
namespace {

using ::testing::Gt;

std::filesystem::path
WriteCompileCommands(const std::filesystem::path &build_directory,
                     const std::filesystem::path &source_path) {
  std::filesystem::create_directories(build_directory);
  const auto compile_commands_path = build_directory / "compile_commands.json";
  std::ofstream stream(compile_commands_path);
  stream << "[\n";
  stream << "  {\n";
  stream << "    \"directory\": \"" << build_directory.string() << "\",\n";
  stream << "    \"file\": \"" << source_path.string() << "\",\n";
  stream << "    \"command\": \"clang -std=c++17 -c " << source_path.string()
         << "\"\n";
  stream << "  }\n";
  stream << "]\n";
  return compile_commands_path;
}

std::string LoadFile(const std::filesystem::path &path) {
  std::ifstream stream(path);
  return std::string((std::istreambuf_iterator<char>(stream)),
                     std::istreambuf_iterator<char>());
}

std::filesystem::path ExecutableUnderTest() {
  const auto current = std::filesystem::current_path();
  return current / "dsl_analyzer";
}

TEST(CliIntegrationTest, GeneratesReportsForSampleProject) {
  test::TemporaryProject project;
  const auto cmake_lists = project.AddFile(
      "CMakeLists.txt", "cmake_minimum_required(VERSION 3.20)\nproject(sample "
                        "CXX)\nadd_library(sample src/example.cpp)\n");
  (void)cmake_lists;
  const auto source_path =
      project.AddFile("src/example.cpp", "int Example() { return 42; }\n");
  const auto build_directory = project.root() / "build";
  WriteCompileCommands(build_directory, source_path);

  const auto cli = ExecutableUnderTest();
  ASSERT_TRUE(std::filesystem::exists(cli))
      << "Expected CLI executable at " << cli;

  const std::string command =
      cli.string() + " --root " + project.root().string() + " --build " +
      build_directory.string() +
      " --format markdown,json --scope-notes integration";

  ASSERT_EQ(std::system(command.c_str()), 0);

  const auto markdown_report = project.root() / "dsl_report.md";
  const auto json_report = project.root() / "dsl_report.json";

  ASSERT_TRUE(std::filesystem::exists(markdown_report));
  ASSERT_TRUE(std::filesystem::exists(json_report));

  EXPECT_THAT(LoadFile(markdown_report).size(), Gt<std::size_t>(0));
  EXPECT_THAT(LoadFile(json_report).size(), Gt<std::size_t>(0));
}

} // namespace
} // namespace dsl
