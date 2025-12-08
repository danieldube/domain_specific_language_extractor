#pragma once

#include <dsl/models.h>

namespace dsl {

class SourceAcquirer {
public:
  virtual ~SourceAcquirer() = default;
  virtual SourceAcquisitionResult Acquire(const AnalysisConfig &config) = 0;
};

class AstIndexer {
public:
  virtual ~AstIndexer() = default;
  virtual AstIndex BuildIndex(const SourceAcquisitionResult &sources) = 0;
};

class DslExtractor {
public:
  virtual ~DslExtractor() = default;
  virtual DslExtractionResult Extract(const AstIndex &index) = 0;
};

class CoherenceAnalyzer {
public:
  virtual ~CoherenceAnalyzer() = default;
  virtual CoherenceResult Analyze(const DslExtractionResult &extraction) = 0;
};

class Reporter {
public:
  virtual ~Reporter() = default;
  virtual Report Render(const DslExtractionResult &extraction,
                        const CoherenceResult &coherence,
                        const AnalysisConfig &config) = 0;
};

class AnalyzerPipeline {
public:
  virtual ~AnalyzerPipeline() = default;
  virtual PipelineResult Run(const AnalysisConfig &config) = 0;
};

} // namespace dsl
