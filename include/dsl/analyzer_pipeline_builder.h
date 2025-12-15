#pragma once

#include <dsl/ast_cache.h>
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

  DefaultAnalyzerPipeline Build();

  static AnalyzerPipelineBuilder WithDefaults();

private:
  PipelineComponents components_;
};

} // namespace dsl
