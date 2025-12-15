#include <dsl/analyzer_pipeline_builder.h>

#include <dsl/caching_ast_indexer.h>
#include <dsl/cmake_source_acquirer.h>
#include <dsl/compile_commands_ast_indexer.h>
#include <dsl/default_analyzer_pipeline.h>
#include <dsl/heuristic_dsl_extractor.h>
#include <dsl/markdown_reporter.h>
#include <dsl/rule_based_coherence_analyzer.h>

#include <filesystem>
#include <utility>

namespace {

template <typename Interface, typename Implementation>
std::unique_ptr<Interface> EnsureComponent(
    std::unique_ptr<Interface> component) {
  if (component) {
    return component;
  }
  return std::make_unique<Implementation>();
}

} // namespace

namespace dsl {

AnalyzerPipelineBuilder AnalyzerPipelineBuilder::WithDefaults() {
  AnalyzerPipelineBuilder builder;
  builder.WithLogger(std::make_shared<NullLogger>());
  builder.WithSourceAcquirer(std::make_unique<CMakeSourceAcquirer>(
      std::filesystem::path("build"), builder.components_.logger));
  builder.WithIndexer(std::make_unique<CompileCommandsAstIndexer>(
      std::filesystem::path{}, builder.components_.logger));
  builder.WithExtractor(std::make_unique<HeuristicDslExtractor>());
  builder.WithAnalyzer(std::make_unique<RuleBasedCoherenceAnalyzer>());
  builder.WithReporter(std::make_unique<MarkdownReporter>());
  return builder;
}

AnalyzerPipelineBuilder &AnalyzerPipelineBuilder::WithSourceAcquirer(
    std::unique_ptr<SourceAcquirer> source_acquirer) {
  components_.source_acquirer = std::move(source_acquirer);
  return *this;
}

AnalyzerPipelineBuilder &
AnalyzerPipelineBuilder::WithIndexer(std::unique_ptr<AstIndexer> indexer) {
  components_.indexer = std::move(indexer);
  return *this;
}

AnalyzerPipelineBuilder &AnalyzerPipelineBuilder::WithExtractor(
    std::unique_ptr<DslExtractor> extractor) {
  components_.extractor = std::move(extractor);
  return *this;
}

AnalyzerPipelineBuilder &AnalyzerPipelineBuilder::WithAnalyzer(
    std::unique_ptr<CoherenceAnalyzer> analyzer) {
  components_.analyzer = std::move(analyzer);
  return *this;
}

AnalyzerPipelineBuilder &
AnalyzerPipelineBuilder::WithReporter(std::unique_ptr<Reporter> reporter) {
  components_.reporter = std::move(reporter);
  return *this;
}

AnalyzerPipelineBuilder &
AnalyzerPipelineBuilder::WithLogger(std::shared_ptr<Logger> logger) {
  components_.logger = std::move(logger);
  return *this;
}

AnalyzerPipelineBuilder &
AnalyzerPipelineBuilder::WithAstCacheOptions(AstCacheOptions options) {
  components_.ast_cache = std::move(options);
  return *this;
}

DefaultAnalyzerPipeline AnalyzerPipelineBuilder::Build() {
  components_.logger = EnsureLogger(std::move(components_.logger));
  components_.source_acquirer =
      components_.source_acquirer
          ? std::move(components_.source_acquirer)
          : std::make_unique<CMakeSourceAcquirer>(
                std::filesystem::path("build"), components_.logger);
  components_.indexer = components_.indexer
                            ? std::move(components_.indexer)
                            : std::make_unique<CompileCommandsAstIndexer>(
                                  std::filesystem::path{}, components_.logger);
  components_.extractor = EnsureComponent<DslExtractor, HeuristicDslExtractor>(
      std::move(components_.extractor));
  components_.analyzer =
      EnsureComponent<CoherenceAnalyzer, RuleBasedCoherenceAnalyzer>(
          std::move(components_.analyzer));
  components_.reporter = EnsureComponent<Reporter, MarkdownReporter>(
      std::move(components_.reporter));

  if (components_.ast_cache.enabled || components_.ast_cache.clean) {
    components_.indexer = std::make_unique<CachingAstIndexer>(
        std::move(components_.indexer), components_.ast_cache,
        components_.logger);
  }
  return DefaultAnalyzerPipeline(std::move(components_));
}

} // namespace dsl
