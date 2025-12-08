#include <dsl/default_components.h>
#include <dsl/interfaces.h>
#include <dsl/models.h>

#include <chrono>
#include <filesystem>
#include <fstream>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace dsl {
namespace {

class TemporaryProject {
public:
  TemporaryProject() {
    const auto timestamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    root_ = std::filesystem::temp_directory_path() /
            ("dsl-acquirer-" + std::to_string(timestamp));
    std::filesystem::create_directories(root_);
  }

  ~TemporaryProject() { std::filesystem::remove_all(root_); }

  std::filesystem::path AddFile(const std::filesystem::path &relative,
                                const std::string &content = "") const {
    const auto full_path = root_ / relative;
    std::filesystem::create_directories(full_path.parent_path());
    std::ofstream stream(full_path);
    stream << content;
    return full_path;
  }

  const std::filesystem::path &root() const { return root_; }

private:
  std::filesystem::path root_;
};

TEST(CMakeSourceAcquirerTest, ProducesNormalizedFileList) {
  TemporaryProject project;
  project.AddFile("CMakeLists.txt", "cmake_minimum_required(VERSION 3.20)\n");
  const auto source_path = project.AddFile("src/main.cpp", "int main() {return 0;}");
  const auto header_path = project.AddFile("include/example.h", "void example();");
  const auto build_dir = project.root() / "build";
  std::filesystem::create_directories(build_dir);
  project.AddFile("build/generated.cpp", "int generated();");

  AnalysisConfig config{.root_path = project.root().string(),
                        .formats = {"markdown"}};
  CMakeSourceAcquirer acquirer;

  const auto result = acquirer.Acquire(config);

  EXPECT_EQ(result.project_root,
            std::filesystem::weakly_canonical(project.root()).string());
  EXPECT_THAT(result.files,
              ::testing::UnorderedElementsAre(
                  std::filesystem::weakly_canonical(source_path).string(),
                  std::filesystem::weakly_canonical(header_path).string()));
}

TEST(CMakeSourceAcquirerTest, ThrowsWhenProjectIsNotCMakeBased) {
  TemporaryProject project;
  project.AddFile("src/example.cpp", "void example() {}");

  AnalysisConfig config{.root_path = project.root().string()};
  CMakeSourceAcquirer acquirer;

  EXPECT_THROW(acquirer.Acquire(config), std::runtime_error);
}

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
  TemporaryProject project;
  project.AddFile("CMakeLists.txt", "cmake_minimum_required(VERSION 3.20)\n");
  const auto source_path = project.AddFile("src/pipeline.cpp", "int f() {return 1;}");
  const auto build_dir = project.root() / "build";
  std::filesystem::create_directories(build_dir);

  AnalysisConfig config{.root_path = project.root().string(),
                        .formats = {"markdown"}};
  auto pipeline = AnalyzerPipelineBuilder::WithDefaults().Build();

  const auto result = pipeline.Run(config);

  ASSERT_FALSE(result.extraction.terms.empty());
  EXPECT_FALSE(result.report.markdown.empty());
  EXPECT_EQ(result.extraction.terms.front().name,
            "symbol_from_" +
                std::filesystem::weakly_canonical(source_path).string());
  EXPECT_FALSE(result.coherence.findings.empty());
}

} // namespace
} // namespace dsl
