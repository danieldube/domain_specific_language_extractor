#pragma once

#include <dsl/interfaces.h>
#include <dsl/logging.h>

#include <filesystem>
#include <memory>
#include <string>

namespace dsl {

class CMakeSourceAcquirer : public SourceAcquirer {
public:
  explicit CMakeSourceAcquirer(
      std::filesystem::path build_directory = std::filesystem::path("build"),
      std::shared_ptr<Logger> logger = nullptr);
  SourceAcquisitionResult Acquire(const AnalysisConfig &config) override;

private:
  std::filesystem::path build_directory_;
  std::shared_ptr<Logger> logger_;
};

class CompileCommandsAstIndexer : public AstIndexer {
public:
  explicit CompileCommandsAstIndexer(
      std::filesystem::path compile_commands_path = {},
      std::shared_ptr<Logger> logger = nullptr);
  AstIndex BuildIndex(const SourceAcquisitionResult &sources) override;

private:
  std::filesystem::path compile_commands_path_;
  std::shared_ptr<Logger> logger_;
};

class RuleBasedCoherenceAnalyzer : public CoherenceAnalyzer {
public:
  CoherenceResult Analyze(const DslExtractionResult &extraction) override;
};

class MarkdownReporter : public Reporter {
public:
  Report Render(const DslExtractionResult &extraction,
                const CoherenceResult &coherence,
                const AnalysisConfig &config) override;
};

class DefaultAnalyzerPipeline;

struct AstCacheOptions {
  bool enabled = false;
  bool clean = false;
  std::filesystem::path directory;
};

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
