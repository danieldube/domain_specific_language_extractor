#pragma once

#include <dsl/analyzer_pipeline_builder.h>

#include <memory>

namespace dsl {

class DefaultAnalyzerPipeline : public AnalyzerPipeline {
public:
  explicit DefaultAnalyzerPipeline(PipelineComponents components);

  PipelineResult Run(const AnalysisConfig &config) override;

private:
  std::unique_ptr<SourceAcquirer> source_acquirer_;
  std::unique_ptr<AstIndexer> indexer_;
  std::unique_ptr<DslExtractor> extractor_;
  std::unique_ptr<CoherenceAnalyzer> analyzer_;
  std::unique_ptr<Reporter> reporter_;
  std::shared_ptr<Logger> logger_;
  AstCacheOptions ast_cache_;
};

} // namespace dsl
