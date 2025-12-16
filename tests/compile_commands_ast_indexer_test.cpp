#include <dsl/compile_commands_ast_indexer.h>
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

using ::testing::AllOf;
using ::testing::Contains;
using ::testing::Field;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::Not;

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
      "src/example.cpp",
      "struct Widget { int value; };\nint Add(int a, int b) "
      "{ return a + b; }\ndouble threshold = 3.14;\nint Use(const "
      "Widget &widget) { return Add(widget.value, "
      "static_cast<int>(threshold)); "
      "}\n");
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

  EXPECT_THAT(index.facts, Not(IsEmpty()));
  EXPECT_THAT(
      index.facts,
      Contains(
          AllOf(Field(&AstFact::name, "Widget"), Field(&AstFact::kind, "type"),
                Field(&AstFact::signature, HasSubstr("Widget")),
                Field(&AstFact::source_location, HasSubstr("example.cpp:1")))));
  EXPECT_THAT(
      index.facts,
      Contains(
          AllOf(Field(&AstFact::name, "Add"), Field(&AstFact::kind, "function"),
                Field(&AstFact::signature, HasSubstr("int Add(int")),
                Field(&AstFact::source_location, HasSubstr("example.cpp:2")))));
  EXPECT_THAT(
      index.facts,
      Contains(AllOf(
          Field(&AstFact::name, "threshold"), Field(&AstFact::kind, "variable"),
          Field(&AstFact::descriptor, HasSubstr("double")),
          Field(&AstFact::source_location, HasSubstr("example.cpp:3")))));
}

TEST(CompileCommandsAstIndexerTest, EmitsRelationshipFactsAndMetadata) {
  test::TemporaryProject project;
  const auto source_path = project.AddFile(
      "src/example.cpp",
      "struct Widget { int value; };\nint Add(int a, int b) { return a + b; }\n"
      "int Use(Widget widget) { return Add(widget.value, 1); }\n");
  const auto build_dir = project.root() / "build";
  std::filesystem::create_directories(build_dir);

  const auto compile_commands_path = build_dir / "compile_commands.json";
  {
    std::ofstream stream(compile_commands_path);
    stream << "[\\n";
    stream << "  {\\n";
    stream << "    \"directory\": \"" << build_dir.string() << "\",\\n";
    stream << "    \"file\": \"" << source_path.string() << "\",\\n";
    stream << "    \"command\": \"clang -std=c++17 -c " << source_path.string()
           << "\"\\n";
    stream << "  }\\n";
    stream << "]\n";
  }

  SourceAcquisitionResult sources;
  sources.project_root = project.root().string();
  sources.build_directory = build_dir.string();
  sources.files = {source_path.string()};

  CompileCommandsAstIndexer indexer;
  const auto index = indexer.BuildIndex(sources);

  EXPECT_THAT(
      index.facts,
      Contains(AllOf(Field(&AstFact::kind, "call"),
                     Field(&AstFact::name, HasSubstr("Use")),
                     Field(&AstFact::target, HasSubstr("Add")),
                     Field(&AstFact::signature, HasSubstr("int Add")),
                     Field(&AstFact::subject_in_project, true),
                     Field(&AstFact::target_scope,
                           Eq(AstFact::TargetScope::kInProject)))));
  EXPECT_THAT(
      index.facts,
      Contains(AllOf(Field(&AstFact::kind, "type_usage"),
                     Field(&AstFact::name, HasSubstr("Use")),
                     Field(&AstFact::target, HasSubstr("Widget")),
                     Field(&AstFact::descriptor, HasSubstr("uses Widget")),
                     Field(&AstFact::subject_in_project, true),
                     Field(&AstFact::target_scope,
                           Eq(AstFact::TargetScope::kInProject)))));
  EXPECT_THAT(index.facts,
              Contains(AllOf(Field(&AstFact::kind, "owns"),
                             Field(&AstFact::name, "Widget"),
                             Field(&AstFact::target, "int"),
                             Field(&AstFact::descriptor, HasSubstr("value")),
                             Field(&AstFact::subject_in_project, true),
                             Field(&AstFact::target_scope,
                                   Eq(AstFact::TargetScope::kUnknown)))));
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
    stream << "    \"file\": \"" << build_file.string() << "\",\\n";
    stream << "    \"command\": \"clang -std=c++17 -c " << build_file.string()
           << "\"\\n";
    stream << "  }\\n";
    stream << "]\n";
  }

  SourceAcquisitionResult sources;
  sources.project_root = project.root().string();
  sources.build_directory = build_dir.string();
  sources.files = {build_file.string()};

  CompileCommandsAstIndexer indexer;
  const auto index = indexer.BuildIndex(sources);

  EXPECT_THAT(index.facts, IsEmpty());
}

} // namespace
} // namespace dsl
