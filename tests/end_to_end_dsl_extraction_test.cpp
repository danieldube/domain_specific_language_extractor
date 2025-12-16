#include <dsl/dsl_analyzer.h>

#include <filesystem>
#include <fstream>
#include <regex>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "test_support/temporary_project.h"

namespace dsl {
namespace {

std::filesystem::path WriteCompileCommands(
    const std::filesystem::path &build_directory,
    const std::filesystem::path &source_path) {
  std::filesystem::create_directories(build_directory);
  const auto compile_commands_path = build_directory / "compile_commands.json";
  std::ofstream stream(compile_commands_path);
  stream << "[\n";
  stream << "  {\n";
  stream << "    \"directory\": \"" << build_directory.string() << "\",\n";
  stream << "    \"file\": \"" << source_path.string() << "\",\n";
  stream << "    \"command\": \"clang++ -std=c++17 -c "
         << source_path.string() << "\"\n";
  stream << "  }\n";
  stream << "]\n";
  return compile_commands_path;
}

std::string LoadFile(const std::filesystem::path &path) {
  std::ifstream stream(path);
  return std::string((std::istreambuf_iterator<char>(stream)),
                     std::istreambuf_iterator<char>());
}

void ReplaceAll(std::string &text, const std::string &from,
                const std::string &to) {
  if (from.empty()) {
    return;
  }
  std::size_t position = 0;
  while ((position = text.find(from, position)) != std::string::npos) {
    text.replace(position, from.size(), to);
    position += to.size();
  }
}

std::string NormalizeReport(const std::string &raw_report,
                            const std::filesystem::path &root) {
  std::string normalized = raw_report;
  const std::regex timestamp_row(R"(\| Generated On \|[^|]*\|)");
  normalized =
      std::regex_replace(normalized, timestamp_row, "| Generated On | <timestamp> |");
  ReplaceAll(normalized, std::filesystem::weakly_canonical(root).string(),
             "<root>");
  return normalized;
}

TEST(EndToEndDslExtractionTest, ExtractsExpectedDslMarkdown) {
  test::TemporaryProject project;
  const auto cmake_lists = project.AddFile(
      "CMakeLists.txt",
      "cmake_minimum_required(VERSION 3.20)\n"
      "project(dsl_e2e_sample LANGUAGES CXX)\n"
      "add_executable(sample src/main.cpp)\n");
  (void)cmake_lists;

  const auto source_path = project.AddFile(
      "src/main.cpp",
      "struct Widget {\n"
      "  int value;\n"
      "};\n\n"
      "int Add(int a, int b) { return a + b; }\n\n"
      "int Use(const Widget &widget) { return Add(widget.value, widget.value); }\n");

  const auto build_directory = project.root() / "build";
  WriteCompileCommands(build_directory, source_path);
  const auto output_directory = project.root() / "out";

  const int exit_code = RunAnalyze({"--root", project.root().string(),
                                    "--build", build_directory.string(),
                                    "--format", "markdown",
                                    "--out", output_directory.string(),
                                    "--log-level", "error"});

  ASSERT_EQ(exit_code, 0);
  const auto report_path = output_directory / "dsl_report.md";
  ASSERT_TRUE(std::filesystem::exists(report_path))
      << "Expected DSL report at " << report_path;

  const auto normalized_report =
      NormalizeReport(LoadFile(report_path), project.root());
  const auto expected_path = std::filesystem::path(__FILE__).parent_path() /
                             "test_support/fixtures/end_to_end_expected_dsl.md";
  const auto expected_report = LoadFile(expected_path);

  EXPECT_EQ(normalized_report, expected_report);
}

} // namespace
} // namespace dsl
