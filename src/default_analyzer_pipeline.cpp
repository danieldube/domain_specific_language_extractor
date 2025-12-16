#include <dsl/default_analyzer_pipeline.h>

#include <chrono>
#include <utility>

namespace dsl {

DefaultAnalyzerPipeline::DefaultAnalyzerPipeline(PipelineComponents components)
    : source_acquirer_(std::move(components.source_acquirer)),
      indexer_(std::move(components.indexer)),
      extractor_(std::move(components.extractor)),
      analyzer_(std::move(components.analyzer)),
      reporter_(std::move(components.reporter)),
      logger_(EnsureLogger(std::move(components.logger))),
      ast_cache_(std::move(components.ast_cache)) {}

PipelineResult DefaultAnalyzerPipeline::Run(const AnalysisConfig &config) {
  logger_->Log(LogLevel::kInfo, "pipeline.start",
               {{"root", config.root_path},
                {"formats", std::to_string(config.formats.size())}});

  const auto pipeline_start = std::chrono::steady_clock::now();
  const auto sources = source_acquirer_->Acquire(config);
  logger_->Log(LogLevel::kDebug, "pipeline.stage.complete",
               {{"stage", "source"},
                {"file_count", std::to_string(sources.files.size())}});

  const auto index = indexer_->BuildIndex(sources);
  logger_->Log(
      LogLevel::kDebug, "pipeline.stage.complete",
      {{"stage", "index"}, {"facts", std::to_string(index.facts.size())}});

  const auto extraction = extractor_->Extract(index);
  logger_->Log(
      LogLevel::kDebug, "pipeline.stage.complete",
      {{"stage", "extract"},
       {"terms", std::to_string(extraction.terms.size())},
       {"relationships", std::to_string(extraction.relationships.size())}});

  const auto coherence = analyzer_->Analyze(extraction);
  logger_->Log(LogLevel::kDebug, "pipeline.stage.complete",
               {{"stage", "analyze"},
                {"findings", std::to_string(coherence.findings.size())}});

  const auto report = reporter_->Render(extraction, coherence, config);

  const auto duration_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - pipeline_start)
          .count();
  logger_->Log(LogLevel::kInfo, "pipeline.complete",
               {{"duration_ms", std::to_string(duration_ms)},
                {"findings", std::to_string(coherence.findings.size())}});

  return PipelineResult{report, coherence, extraction};
}

} // namespace dsl
