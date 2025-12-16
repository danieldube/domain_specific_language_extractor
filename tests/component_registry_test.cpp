#include <dsl/analyzer_pipeline_builder.h>
#include <dsl/component_registry.h>
#include <dsl/default_analyzer_pipeline.h>
#include <dsl/heuristic_dsl_extractor.h>
#include <dsl/logging.h>
#include <dsl/markdown_reporter.h>
#include <dsl/models.h>
#include <dsl/rule_based_coherence_analyzer.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace dsl {
namespace {

class StubSourceAcquirer : public SourceAcquirer {
public:
  SourceAcquisitionResult Acquire(const AnalysisConfig &) override {
    return SourceAcquisitionResult{};
  }
};

class StubIndexer : public AstIndexer {
public:
  AstIndex BuildIndex(const SourceAcquisitionResult &) override {
    return AstIndex{};
  }
};

class CustomExtractor : public DslExtractor {
public:
  DslExtractionResult Extract(const AstIndex &,
                              const AnalysisConfig &) override {
    DslExtractionResult result;
    result.extraction_notes.push_back("custom extractor used");
    DslTerm term;
    term.name = "custom-term";
    result.terms.push_back(term);
    return result;
  }
};

class CustomAnalyzer : public CoherenceAnalyzer {
public:
  CoherenceResult Analyze(const DslExtractionResult &) override {
    CoherenceResult result;
    result.severity = CoherenceSeverity::kIncoherent;
    result.findings.push_back(Finding{.term = "custom-analyzer"});
    return result;
  }
};

class CustomReporter : public Reporter {
public:
  Report Render(const DslExtractionResult &, const CoherenceResult &,
                const AnalysisConfig &) override {
    return Report{.markdown = "custom-report", .json = "custom-json"};
  }
};

AnalysisConfig MinimalConfig() {
  AnalysisConfig config;
  config.root_path = ".";
  config.formats = {"markdown"};
  config.logging.level = LogLevel::kWarn;
  config.cache.directory = ".cache";
  config.cache.enable_ast_cache = false;
  config.cache.clean = false;
  config.logger = std::make_shared<NullLogger>();
  return config;
}

TEST(ComponentRegistryTest,
     ProvidesDefaultsAndKeepsThemAfterCustomRegistration) {
  auto registry = MakeComponentRegistryWithDefaults();

  auto default_extractor = registry.CreateExtractor();
  EXPECT_NE(dynamic_cast<HeuristicDslExtractor *>(default_extractor.get()),
            nullptr);
  auto default_analyzer = registry.CreateAnalyzer();
  EXPECT_NE(dynamic_cast<RuleBasedCoherenceAnalyzer *>(default_analyzer.get()),
            nullptr);
  auto default_reporter = registry.CreateReporter();
  EXPECT_NE(dynamic_cast<MarkdownReporter *>(default_reporter.get()), nullptr);

  registry.RegisterExtractor(
      "custom-extractor", []() { return std::make_unique<CustomExtractor>(); });
  registry.RegisterAnalyzer(
      "custom-analyzer", []() { return std::make_unique<CustomAnalyzer>(); });
  registry.RegisterReporter(
      "custom-reporter", []() { return std::make_unique<CustomReporter>(); });

  auto still_default = registry.CreateExtractor();
  EXPECT_NE(dynamic_cast<HeuristicDslExtractor *>(still_default.get()),
            nullptr);

  auto custom_instance = registry.CreateReporter("custom-reporter");
  EXPECT_NE(dynamic_cast<CustomReporter *>(custom_instance.get()), nullptr);
}

TEST(ComponentRegistryTest, PipelineBuilderUsesCustomPluginsWhenSelected) {
  auto registry = MakeComponentRegistryWithDefaults();
  registry.RegisterExtractor(
      "custom-extractor", []() { return std::make_unique<CustomExtractor>(); });
  registry.RegisterAnalyzer(
      "custom-analyzer", []() { return std::make_unique<CustomAnalyzer>(); });
  registry.RegisterReporter(
      "custom-reporter", []() { return std::make_unique<CustomReporter>(); });

  AnalyzerPipelineBuilder builder(registry);
  builder.WithLogger(std::make_shared<NullLogger>());
  builder.WithSourceAcquirer(std::make_unique<StubSourceAcquirer>());
  builder.WithIndexer(std::make_unique<StubIndexer>());
  builder.WithExtractorName("custom-extractor")
      .WithAnalyzerName("custom-analyzer")
      .WithReporterName("custom-reporter");

  auto pipeline = builder.Build();
  const auto result = pipeline.Run(MinimalConfig());

  ASSERT_THAT(result.extraction.extraction_notes,
              ::testing::Contains("custom extractor used"));
  ASSERT_THAT(
      result.coherence.findings,
      ::testing::Contains(::testing::Field(&Finding::term, "custom-analyzer")));
  EXPECT_EQ(result.report.markdown, "custom-report");
  EXPECT_EQ(result.report.json, "custom-json");
}

} // namespace
} // namespace dsl
