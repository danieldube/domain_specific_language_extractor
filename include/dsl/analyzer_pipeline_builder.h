#pragma once

#include <dsl/ast_cache.h>
#include <dsl/component_registry.h>
#include <dsl/interfaces.h>
#include <dsl/logging.h>

#include <memory>

namespace dsl {

class DefaultAnalyzerPipeline;

struct PipelineComponents {
  std::unique_ptr<SourceAcquirer> source_acquirer;
  std::unique_ptr<AstIndexer> indexer;
  std::unique_ptr<DslExtractor> extractor;
  std::unique_ptr<CoherenceAnalyzer> analyzer;
  std::unique_ptr<Reporter> reporter;
  std::shared_ptr<Logger> logger;
  AstCacheOptions ast_cache;
};

class AnalyzerPipelineBuilder {
public:
  explicit AnalyzerPipelineBuilder(
      const ComponentRegistry &registry = GlobalComponentRegistry());

  AnalyzerPipelineBuilder &
  WithSourceAcquirer(std::unique_ptr<SourceAcquirer> source_acquirer);
  AnalyzerPipelineBuilder &WithIndexer(std::unique_ptr<AstIndexer> indexer);
  AnalyzerPipelineBuilder &
  WithExtractor(std::unique_ptr<DslExtractor> extractor);
  AnalyzerPipelineBuilder &
  WithAnalyzer(std::unique_ptr<CoherenceAnalyzer> analyzer);
  AnalyzerPipelineBuilder &WithReporter(std::unique_ptr<Reporter> reporter);
  AnalyzerPipelineBuilder &WithLogger(std::shared_ptr<Logger> logger);
  AnalyzerPipelineBuilder &WithAstCacheOptions(AstCacheOptions options);
  AnalyzerPipelineBuilder &WithExtractorName(std::string name);
  AnalyzerPipelineBuilder &WithAnalyzerName(std::string name);
  AnalyzerPipelineBuilder &WithReporterName(std::string name);

  DefaultAnalyzerPipeline Build();

  static AnalyzerPipelineBuilder WithDefaults();

private:
  const ComponentRegistry *registry_;
  struct ComponentSelections {
    std::string extractor;
    std::string analyzer;
    std::string reporter;
  } selections_;
  PipelineComponents components_;
};

} // namespace dsl
