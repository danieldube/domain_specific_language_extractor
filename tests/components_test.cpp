#include <dsl/default_components.h>
#include <dsl/interfaces.h>
#include <dsl/models.h>

#include <filesystem>
#include <fstream>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "test_support/temporary_project.h"

namespace dsl {
namespace {

TEST(HeuristicDslExtractorTest, BuildsTermsAndRelationships) {
  AstIndex index;
  index.facts = {{"alpha", "function"}, {"beta", "function"}};
  HeuristicDslExtractor extractor;

  const auto extraction = extractor.Extract(index);

  ASSERT_EQ(extraction.terms.size(), 2u);
  EXPECT_EQ(extraction.terms.front().definition, "Derived from alpha");
  EXPECT_FALSE(extraction.terms.front().evidence.empty());
  ASSERT_EQ(extraction.relationships.size(), 1u);
  EXPECT_EQ(extraction.relationships.front().subject, "alpha");
  EXPECT_EQ(extraction.relationships.front().object, "beta");
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
