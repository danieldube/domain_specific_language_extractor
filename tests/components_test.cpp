#include <dsl/default_components.h>
#include <dsl/heuristic_dsl_extractor.h>
#include <dsl/interfaces.h>
#include <dsl/models.h>

#include <algorithm>
#include <filesystem>
#include <fstream>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "test_support/temporary_project.h"

namespace dsl {
namespace {

TEST(HeuristicDslExtractorTest, BuildsTermsAndRelationships) {
  AstIndex index;
  index.facts = {
      {"ProcessData", "function|int ProcessData()", "file.cpp:3"},
      {"ProcessData", "call:RenderFrame|Transforms frame", "file.cpp:10"},
      {"RenderFrame", "function|void RenderFrame()", "file.cpp:20"},
      {"RenderFrame", "type_usage:FrameConfig|uses configuration",
       "file.cpp:25"},
      {"FrameConfig", "type|struct FrameConfig", "types.h:5"},
      {"FRAMECONFIG", "type|documented FrameConfig", "types.h:6"},
  };
  HeuristicDslExtractor extractor;

  const auto extraction = extractor.Extract(index);

  ASSERT_EQ(extraction.terms.size(), 3u);

  const auto &process_term = *std::find_if(
      extraction.terms.begin(), extraction.terms.end(),
      [](const auto &term) { return term.name == "processdata"; });
  EXPECT_THAT(process_term.definition,
              ::testing::HasSubstr("int ProcessData()"));
  EXPECT_EQ(process_term.kind, "Action");
  EXPECT_GE(process_term.usage_count, 2);
  EXPECT_FALSE(process_term.evidence.empty());

  const auto &config_term = *std::find_if(
      extraction.terms.begin(), extraction.terms.end(),
      [](const auto &term) { return term.name == "frameconfig"; });
  EXPECT_THAT(config_term.aliases,
              ::testing::UnorderedElementsAre("FrameConfig", "FRAMECONFIG"));
  EXPECT_EQ(config_term.kind, "Entity");

  ASSERT_EQ(extraction.relationships.size(), 2u);
  EXPECT_THAT(
      extraction.relationships,
      ::testing::UnorderedElementsAre(
          ::testing::AllOf(
              ::testing::Field(&DslRelationship::subject, "processdata"),
              ::testing::Field(&DslRelationship::verb, "calls"),
              ::testing::Field(&DslRelationship::object, "renderframe")),
          ::testing::AllOf(
              ::testing::Field(&DslRelationship::subject, "renderframe"),
              ::testing::Field(&DslRelationship::verb, "uses-type"),
              ::testing::Field(&DslRelationship::object, "frameconfig"))));
  EXPECT_FALSE(extraction.workflows.empty());
  EXPECT_FALSE(extraction.extraction_notes.empty());
}

TEST(RuleBasedCoherenceAnalyzerTest, FlagsMissingRelationships) {
  DslExtractionResult extraction;
  DslTerm lonely;
  lonely.name = "lonely";
  lonely.kind = "Action";
  lonely.definition = "Derived from lonely";
  extraction.terms = {lonely};
  RuleBasedCoherenceAnalyzer analyzer;

  const auto result = analyzer.Analyze(extraction);

  ASSERT_EQ(result.findings.size(), 1u);
  EXPECT_EQ(result.findings.front().term, "lonely");
  EXPECT_THAT(result.findings.front().description,
              ::testing::HasSubstr("No relationships"));
}

TEST(RuleBasedCoherenceAnalyzerTest, DetectsDuplicateTerms) {
  DslExtractionResult extraction;
  DslTerm shared_one;
  shared_one.name = "shared";
  shared_one.kind = "Action";
  shared_one.definition = "One";
  DslTerm shared_two = shared_one;
  shared_two.definition = "Two";
  extraction.terms = {shared_one, shared_two};

  DslRelationship relationship;
  relationship.subject = "shared";
  relationship.verb = "relates";
  relationship.object = "shared";
  extraction.relationships = {relationship};
  RuleBasedCoherenceAnalyzer analyzer;

  const auto result = analyzer.Analyze(extraction);

  ASSERT_EQ(result.findings.size(), 1u);
  EXPECT_EQ(result.findings.front().term, "shared");
}

TEST(DefaultAnalyzerPipelineTest, RunsComponentsInOrder) {
  test::TemporaryProject project;
  project.AddFile("CMakeLists.txt", "cmake_minimum_required(VERSION 3.20)\n");
  const auto source_path =
      project.AddFile("src/pipeline.cpp", "int f() {return 1;}");
  const auto build_dir = project.root() / "build";
  std::filesystem::create_directories(build_dir);
  const auto compile_commands_path = build_dir / "compile_commands.json";
  {
    std::ofstream compile_commands(compile_commands_path);
    compile_commands << "[\\n";
    compile_commands << "  {\\n";
    compile_commands << "    \"directory\": \"" << build_dir.string()
                     << "\",\\n";
    compile_commands << "    \"file\": \""
                     << std::filesystem::weakly_canonical(source_path).string()
                     << "\",\\n";
    compile_commands << "    \"command\": \"clang -std=c++17 -c "
                     << std::filesystem::weakly_canonical(source_path).string()
                     << "\"\\n";
    compile_commands << "  }\\n";
    compile_commands << "]\n";
  }

  AnalysisConfig config{.root_path = project.root().string(),
                        .formats = {"markdown", "json"}};
  auto pipeline = AnalyzerPipelineBuilder::WithDefaults().Build();

  const auto result = pipeline.Run(config);

  ASSERT_FALSE(result.extraction.terms.empty());
  EXPECT_FALSE(result.report.markdown.empty());
  EXPECT_FALSE(result.report.json.empty());
  EXPECT_EQ(result.extraction.terms.front().name, "f");
  EXPECT_FALSE(result.coherence.findings.empty());
}

} // namespace
} // namespace dsl
