#include <dsl/models.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <sys/wait.h>

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
  const auto preferred = current / "dsl-extract";
  if (std::filesystem::exists(preferred)) {
    return preferred;
  }
  return current / "dsl_analyzer";
}

int ExitCode(const std::string &command) {
  return WEXITSTATUS(std::system(command.c_str()));
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

  const auto output_directory = project.root() / "artifacts";
  const std::string command =
      cli.string() + " analyze --root " + project.root().string() +
      " --build " + build_directory.string() +
      " --format markdown,json --scope-notes integration --out " +
      output_directory.string();

  ASSERT_EQ(ExitCode(command), 2);

  const auto markdown_report = output_directory / "dsl_report.md";
  const auto json_report = output_directory / "dsl_report.json";

  ASSERT_TRUE(std::filesystem::exists(markdown_report));
  ASSERT_TRUE(std::filesystem::exists(json_report));

  EXPECT_THAT(LoadFile(markdown_report).size(), Gt<std::size_t>(0));
  EXPECT_THAT(LoadFile(json_report).size(), Gt<std::size_t>(0));
}

TEST(CliIntegrationTest, DefaultsMirrorLegacyBehavior) {
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

  const std::string command = cli.string() + " --root " +
                              project.root().string() + " --build " +
                              build_directory.string();

  ASSERT_EQ(ExitCode(command), 2);

  const auto markdown_report = project.root() / "dsl_report.md";
  const auto json_report = project.root() / "dsl_report.json";

  ASSERT_TRUE(std::filesystem::exists(markdown_report));
  EXPECT_FALSE(std::filesystem::exists(json_report));
  EXPECT_THAT(LoadFile(markdown_report).size(), Gt<std::size_t>(0));
}

TEST(CliIntegrationTest, UsesConfigFileAndCacheOptions) {
  test::TemporaryProject project;
  const auto cmake_lists = project.AddFile(
      "CMakeLists.txt", "cmake_minimum_required(VERSION 3.20)\nproject(sample "
                        "CXX)\nadd_library(sample src/example.cpp)\n");
  (void)cmake_lists;
  const auto source_path =
      project.AddFile("src/example.cpp", "int Example() { return 42; }\n");
  const auto build_directory = project.root() / "build";
  WriteCompileCommands(build_directory, source_path);

  const auto cache_dir = project.root() / "dsl-cache";
  const auto config_path = project.AddFile(
      "dsl_config.yaml", "root: " + project.root().string() + "\n" +
                             "build: " + build_directory.string() + "\n" +
                             "formats: markdown,json\n" + "cache_ast: true\n" +
                             "cache_dir: " + cache_dir.string() + "\n");

  const auto cli = ExecutableUnderTest();
  ASSERT_TRUE(std::filesystem::exists(cli))
      << "Expected CLI executable at " << cli;

  const std::string command =
      cli.string() + " analyze --config " + config_path.string();

  ASSERT_EQ(ExitCode(command), 2);

  const auto markdown_report = project.root() / "dsl_report.md";
  const auto json_report = project.root() / "dsl_report.json";

  ASSERT_TRUE(std::filesystem::exists(markdown_report));
  ASSERT_TRUE(std::filesystem::exists(json_report));
  EXPECT_THAT(LoadFile(markdown_report).size(), Gt<std::size_t>(0));
  EXPECT_TRUE(std::filesystem::exists(cache_dir));
  EXPECT_FALSE(std::filesystem::is_empty(cache_dir));

  const std::string clean_command = cli.string() + " cache clean --root " +
                                    project.root().string() + " --cache-dir " +
                                    cache_dir.string();

  ASSERT_EQ(ExitCode(clean_command), 0);
  EXPECT_FALSE(std::filesystem::exists(cache_dir));
}

} // namespace
} // namespace dsl
