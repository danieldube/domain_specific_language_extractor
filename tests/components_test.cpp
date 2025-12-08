#include <dsl/default_components.h>
#include <dsl/interfaces.h>
#include <dsl/models.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace dsl {
namespace {

TEST(BasicSourceAcquirerTest, ProducesNormalizedFileList) {
  AnalysisConfig config{.root_path = "/project/root", .formats = {"markdown"}};
  BasicSourceAcquirer acquirer;

  const auto result = acquirer.Acquire(config);

  ASSERT_EQ(result.files.size(), 1u);
  EXPECT_EQ(result.files.front(), "/project/root/sample.cpp");
}

TEST(SimpleAstIndexerTest, CreatesFactsForEachFile) {
  SourceAcquisitionResult sources;
  sources.files = {"/project/root/a.cpp", "/project/root/b.cpp"};
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
  extraction.terms = {{"shared", "Action", "One"},
                      {"shared", "Action", "Two"}};
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
  coherence.findings = {{"verb", "No relationships detected; DSL may be incomplete."}};
  MarkdownReporter reporter;
  AnalysisConfig config{.root_path = "repo"};

  const auto report = reporter.Render(extraction, coherence, config);

  EXPECT_THAT(report.markdown, ::testing::HasSubstr("# DSL Extraction Report"));
  EXPECT_THAT(report.markdown, ::testing::HasSubstr("## Terms"));
  EXPECT_THAT(report.markdown, ::testing::HasSubstr("## Findings"));
  EXPECT_THAT(report.markdown, ::testing::HasSubstr("verb"));
}

TEST(DefaultAnalyzerPipelineTest, RunsComponentsInOrder) {
  AnalysisConfig config{.root_path = "repo", .formats = {"markdown"}};
  auto pipeline = AnalyzerPipelineBuilder::WithDefaults().Build();

  const auto result = pipeline.Run(config);

  ASSERT_FALSE(result.extraction.terms.empty());
  EXPECT_FALSE(result.report.markdown.empty());
  EXPECT_EQ(result.extraction.terms.front().name, "symbol_from_repo/sample.cpp");
  EXPECT_FALSE(result.coherence.findings.empty());
}

}  // namespace
}  // namespace dsl

