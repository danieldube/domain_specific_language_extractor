#include <dsl/default_components.h>
#include <dsl/interfaces.h>
#include <dsl/models.h>

#include <filesystem>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "test_support/temporary_project.h"

namespace dsl {
namespace {

TEST(SimpleAstIndexerTest, CreatesFactsForEachFile) {
  SourceAcquisitionResult sources;
  sources.files = {"/project/root/a.cpp", "/project/root/b.cpp"};
  sources.project_root = "/project/root";
  SimpleAstIndexer indexer;

  const auto index = indexer.BuildIndex(sources);

  ASSERT_EQ(index.facts.size(), 2u);
  EXPECT_EQ(index.facts.at(0).name, "symbol_from_/project/root/a.cpp");
  EXPECT_EQ(index.facts.at(1).name, "symbol_from_/project/root/b.cpp");
}

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

  AnalysisConfig config{.root_path = project.root().string(),
                        .formats = {"markdown", "json"}};
  auto pipeline = AnalyzerPipelineBuilder::WithDefaults().Build();

  const auto result = pipeline.Run(config);

  ASSERT_FALSE(result.extraction.terms.empty());
  EXPECT_FALSE(result.report.markdown.empty());
  EXPECT_FALSE(result.report.json.empty());
  EXPECT_EQ(result.extraction.terms.front().name,
            "symbol_from_" +
                std::filesystem::weakly_canonical(source_path).string());
  EXPECT_FALSE(result.coherence.findings.empty());
}

} // namespace
} // namespace dsl
