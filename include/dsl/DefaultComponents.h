#pragma once

#include <memory>
#include <string>

#include "dsl/Interfaces.h"

namespace dsl {

class BasicSourceAcquirer : public SourceAcquirer {
 public:
  SourceAcquisitionResult Acquire(const AnalysisConfig& config) override;
};

class SimpleAstIndexer : public AstIndexer {
 public:
  AstIndex BuildIndex(const SourceAcquisitionResult& sources) override;
};

class HeuristicDslExtractor : public DslExtractor {
 public:
  DslExtractionResult Extract(const AstIndex& index) override;
};

class RuleBasedCoherenceAnalyzer : public CoherenceAnalyzer {
 public:
  CoherenceResult Analyze(const DslExtractionResult& extraction) override;
};

class MarkdownReporter : public Reporter {
 public:
  Report Render(const DslExtractionResult& extraction,
                const CoherenceResult& coherence,
                const AnalysisConfig& config) override;
};

class DefaultAnalyzerPipeline : public AnalyzerPipeline {
 public:
  DefaultAnalyzerPipeline(std::unique_ptr<SourceAcquirer> source_acquirer,
                          std::unique_ptr<AstIndexer> indexer,
                          std::unique_ptr<DslExtractor> extractor,
                          std::unique_ptr<CoherenceAnalyzer> analyzer,
                          std::unique_ptr<Reporter> reporter);

  PipelineResult Run(const AnalysisConfig& config) override;

 private:
  std::unique_ptr<SourceAcquirer> source_acquirer_;
  std::unique_ptr<AstIndexer> indexer_;
  std::unique_ptr<DslExtractor> extractor_;
  std::unique_ptr<CoherenceAnalyzer> analyzer_;
  std::unique_ptr<Reporter> reporter_;
};

}  // namespace dsl

