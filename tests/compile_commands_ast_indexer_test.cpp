#include <dsl/default_components.h>
#include <dsl/models.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "test_support/temporary_project.h"

namespace dsl {
namespace {

std::string LoadFixture(const std::filesystem::path &fixture_path) {
  std::ifstream stream(fixture_path);
  return std::string((std::istreambuf_iterator<char>(stream)),
                     std::istreambuf_iterator<char>());
}

void ReplaceAll(std::string &text, const std::string &placeholder,
                const std::string &value) {
  std::size_t position = 0;
  while ((position = text.find(placeholder, position)) != std::string::npos) {
    text.replace(position, placeholder.size(), value);
    position += value.size();
  }
}

TEST(CompileCommandsAstIndexerTest, ExtractsFactsFromTranslationUnits) {
  test::TemporaryProject project;
  const auto source_path = project.AddFile(
      "src/example.cpp", "struct Widget { int value; };\nint Add(int a, int b) "
                         "{ return a + b; }\ndouble threshold = 3.14;\n");
  const auto build_dir = project.root() / "build";
  std::filesystem::create_directories(build_dir);
  const auto generated_path =
      project.AddFile("build/generated.cpp", "int Generated() { return 2; }\n");

  const auto fixture_path = std::filesystem::path(__FILE__).parent_path() /
                            "test_support/fixtures/compile_commands.json";
  auto fixture_contents = LoadFixture(fixture_path);
  for (const auto &[placeholder, value] :
       std::vector<std::pair<std::string, std::string>>{
           {"__PROJECT_ROOT__", project.root().string()},
           {"__SOURCE_FILE__",
            std::filesystem::weakly_canonical(source_path).string()},
           {"__GENERATED_FILE__",
            std::filesystem::weakly_canonical(generated_path).string()}}) {
    ReplaceAll(fixture_contents, placeholder, value);
  }

  const auto compile_commands_path = build_dir / "compile_commands.json";
  {
    std::ofstream stream(compile_commands_path);
    stream << fixture_contents;
  }

  SourceAcquisitionResult sources;
  sources.project_root = project.root().string();
  sources.build_directory = build_dir.string();
  sources.files = {source_path.string()};

  CompileCommandsAstIndexer indexer;
  const auto index = indexer.BuildIndex(sources);

  ASSERT_THAT(index.facts, ::testing::SizeIs(3));
  EXPECT_THAT(index.facts,
              ::testing::Contains(::testing::AllOf(
                  ::testing::Field(&AstFact::name, "Widget"),
                  ::testing::Field(&AstFact::kind, "type"),
                  ::testing::Field(&AstFact::source_location,
                                   ::testing::HasSubstr("example.cpp:1")))));
  EXPECT_THAT(index.facts,
              ::testing::Contains(::testing::AllOf(
                  ::testing::Field(&AstFact::name, "Add"),
                  ::testing::Field(&AstFact::kind, "function"),
                  ::testing::Field(&AstFact::source_location,
                                   ::testing::HasSubstr("example.cpp:2")))));
  EXPECT_THAT(index.facts,
              ::testing::Contains(::testing::AllOf(
                  ::testing::Field(&AstFact::name, "threshold"),
                  ::testing::Field(&AstFact::kind, "variable"),
                  ::testing::Field(&AstFact::source_location,
                                   ::testing::HasSubstr("example.cpp:3")))));
}

TEST(CompileCommandsAstIndexerTest, SkipsBuildDirectoryEntries) {
  test::TemporaryProject project;
  const auto build_dir = project.root() / "build";
  std::filesystem::create_directories(build_dir);
  const auto build_file =
      project.AddFile("build/generated.cpp", "int Generated() { return 2; }\n");

  const auto compile_commands_path = build_dir / "compile_commands.json";
  {
    std::ofstream stream(compile_commands_path);
    stream << "[\\n";
    stream << "  {\\n";
    stream << "    \"directory\": \"" << build_dir.string() << "\",\\n";
    stream << "    \"file\": \"" << build_file.string() << "\"\\n";
    stream << "  }\\n";
    stream << "]\n";
  }

  SourceAcquisitionResult sources;
  sources.project_root = project.root().string();
  sources.build_directory = build_dir.string();
  sources.files = {build_file.string()};

  CompileCommandsAstIndexer indexer;
  const auto index = indexer.BuildIndex(sources);

  EXPECT_THAT(index.facts, ::testing::IsEmpty());
}

} // namespace
} // namespace dsl
