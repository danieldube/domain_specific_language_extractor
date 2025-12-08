#include <dsl/default_components.h>
#include <dsl/interfaces.h>
#include <dsl/models.h>

#include <filesystem>
#include <stdexcept>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "test_support/temporary_project.h"

namespace dsl {
namespace {

TEST(SimpleAstIndexerTest, ExtractsFactsFromSourceFiles) {
  test::TemporaryProject project;
  const auto source_path = project.AddFile(
      "src/example.cpp",
      "class Widget {\npublic:\n  void Paint() { }\n};\nint Run() {return 0;}\n");

  SourceAcquisitionResult sources;
  sources.files = {std::filesystem::weakly_canonical(source_path).string()};
  sources.project_root = project.root().string();
  SimpleAstIndexer indexer;

  const auto index = indexer.BuildIndex(sources);

  ASSERT_EQ(index.facts.size(), 3u);
  EXPECT_EQ(index.facts.at(0).name, "Widget");
  EXPECT_EQ(index.facts.at(0).kind, "class");
  EXPECT_EQ(index.facts.at(0).location.file_path,
            std::filesystem::weakly_canonical(source_path).string());
  EXPECT_EQ(index.facts.at(0).location.line, 1u);

  EXPECT_EQ(index.facts.at(1).name, "Paint");
  EXPECT_EQ(index.facts.at(1).kind, "function");
  EXPECT_EQ(index.facts.at(2).name, "Run");
  EXPECT_EQ(index.facts.at(2).kind, "function");
}

TEST(SimpleAstIndexerTest, ThrowsWhenSourceListIsEmpty) {
  SourceAcquisitionResult sources;
  SimpleAstIndexer indexer;

  EXPECT_THROW(indexer.BuildIndex(sources), std::invalid_argument);
}

TEST(HeuristicDslExtractorTest, BuildsTermsAndRelationships) {
  AstIndex index;
  index.facts = {{"alpha", "function", {"/tmp/a.cpp", 3}},
                 {"beta", "class", {"/tmp/b.cpp", 10}}};
  HeuristicDslExtractor extractor;

  const auto extraction = extractor.Extract(index);

  ASSERT_EQ(extraction.terms.size(), 2u);
  EXPECT_THAT(extraction.terms.front().definition,
              ::testing::HasSubstr("/tmp/a.cpp:3"));
  ASSERT_EQ(extraction.relationships.size(), 1u);
  EXPECT_EQ(extraction.relationships.front().subject, "alpha");
  EXPECT_EQ(extraction.relationships.front().object, "beta");
}

TEST(RuleBasedCoherenceAnalyzerTest, FlagsMissingRelationships) {
  DslExtractionResult extraction;
  extraction.terms = {{"lonely", "Action", "Derived from lonely"}};
  RuleBasedCoherenceAnalyzer analyzer;

  const auto result = analyzer.Analyze(extraction);

  ASSERT_EQ(result.findings.size(), 1u);
  EXPECT_EQ(result.findings.front().term, "lonely");
  EXPECT_THAT(result.findings.front().description,
              ::testing::HasSubstr("No relationships"));
}

TEST(RuleBasedCoherenceAnalyzerTest, DetectsDuplicateTerms) {
  DslExtractionResult extraction;
  extraction.terms = {{"shared", "Action", "One"}, {"shared", "Action", "Two"}};
  extraction.relationships = {{"shared", "relates", "shared"}};
  RuleBasedCoherenceAnalyzer analyzer;

  const auto result = analyzer.Analyze(extraction);

  ASSERT_EQ(result.findings.size(), 1u);
  EXPECT_EQ(result.findings.front().term, "shared");
}

TEST(MarkdownReporterTest, RendersSections) {
  DslExtractionResult extraction;
  extraction.terms = {{"verb", "Action", "Derived from verb"}};
  extraction.relationships = {};
  CoherenceResult coherence;
  coherence.findings = {
      {"verb", "No relationships detected; DSL may be incomplete."}};
  MarkdownReporter reporter;
  AnalysisConfig config{.root_path = "repo"};

  const auto report = reporter.Render(extraction, coherence, config);

  EXPECT_THAT(report.markdown, ::testing::HasSubstr("# DSL Extraction Report"));
  EXPECT_THAT(report.markdown, ::testing::HasSubstr("## Terms"));
  EXPECT_THAT(report.markdown, ::testing::HasSubstr("## Findings"));
  EXPECT_THAT(report.markdown, ::testing::HasSubstr("verb"));
}

TEST(DefaultAnalyzerPipelineTest, RunsComponentsInOrder) {
  test::TemporaryProject project;
  project.AddFile("CMakeLists.txt", "cmake_minimum_required(VERSION 3.20)\n");
  const auto source_path =
      project.AddFile("src/pipeline.cpp", "int f() {return 1;}");
  const auto build_dir = project.root() / "build";
  std::filesystem::create_directories(build_dir);

  AnalysisConfig config{.root_path = project.root().string(),
                        .formats = {"markdown"}};
  auto pipeline = AnalyzerPipelineBuilder::WithDefaults().Build();

  const auto result = pipeline.Run(config);

  ASSERT_FALSE(result.extraction.terms.empty());
  EXPECT_FALSE(result.report.markdown.empty());
  EXPECT_EQ(result.extraction.terms.front().name, "f");
  EXPECT_FALSE(result.coherence.findings.empty());
}

} // namespace
} // namespace dsl
