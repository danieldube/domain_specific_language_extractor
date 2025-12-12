#include <dsl/default_components.h>
#include <dsl/heuristic_dsl_extractor.h>

#include <unordered_map>

#include <memory>
#include <utility>

namespace {

void AddDuplicateFindings(const std::unordered_map<std::string, int> &counts,
                          dsl::CoherenceResult &result) {
  for (const auto &[name, count] : counts) {
    if (count < 2) {
      continue;
    }
    dsl::Finding finding{};
    finding.term = name;
    finding.conflict = "Duplicate term name indicates incoherent DSL usage.";
    finding.examples.push_back(name + ": duplicate usage");
    finding.suggested_canonical_form = name;
    finding.description = finding.conflict;
    result.findings.push_back(finding);
  }
}

void AddRelationshipMissingFinding(const dsl::DslExtractionResult &extraction,
                                   dsl::CoherenceResult &result) {
  if (!extraction.relationships.empty() || extraction.terms.empty()) {
    return;
  }

  const auto &term = extraction.terms.front();
  dsl::Finding finding{};
  finding.term = term.name;
  finding.conflict = "No relationships detected; DSL may be incomplete.";
  finding.examples.push_back("Relationships missing for term");
  finding.suggested_canonical_form = term.name;
  finding.description = finding.conflict;
  result.findings.push_back(finding);
}

template <typename Interface, typename Implementation>
std::unique_ptr<Interface>
EnsureComponent(std::unique_ptr<Interface> component) {
  if (component) {
    return component;
  }
  return std::make_unique<Implementation>();
}

std::unordered_map<std::string, int>
CountTermOccurrences(const std::vector<dsl::DslTerm> &terms) {
  std::unordered_map<std::string, int> occurrence_counts;
  for (const auto &term : terms) {
    ++occurrence_counts[term.name];
  }
  return occurrence_counts;
}

} // namespace

namespace dsl {

CoherenceResult
RuleBasedCoherenceAnalyzer::Analyze(const DslExtractionResult &extraction) {
  CoherenceResult result{};
  const auto counts = CountTermOccurrences(extraction.terms);
  AddDuplicateFindings(counts, result);
  AddRelationshipMissingFinding(extraction, result);
  return result;
}
AnalyzerPipelineBuilder AnalyzerPipelineBuilder::WithDefaults() {
  AnalyzerPipelineBuilder builder;
  builder.WithSourceAcquirer(std::make_unique<CMakeSourceAcquirer>());
  builder.WithIndexer(std::make_unique<CompileCommandsAstIndexer>());
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

DefaultAnalyzerPipeline AnalyzerPipelineBuilder::Build() {
  components_.source_acquirer =
      EnsureComponent<SourceAcquirer, CMakeSourceAcquirer>(
          std::move(components_.source_acquirer));
  components_.indexer = EnsureComponent<AstIndexer, CompileCommandsAstIndexer>(
      std::move(components_.indexer));
  components_.extractor = EnsureComponent<DslExtractor, HeuristicDslExtractor>(
      std::move(components_.extractor));
  components_.analyzer =
      EnsureComponent<CoherenceAnalyzer, RuleBasedCoherenceAnalyzer>(
          std::move(components_.analyzer));
  components_.reporter = EnsureComponent<Reporter, MarkdownReporter>(
      std::move(components_.reporter));
  return DefaultAnalyzerPipeline(std::move(components_));
}

DefaultAnalyzerPipeline::DefaultAnalyzerPipeline(PipelineComponents components)
    : source_acquirer_(std::move(components.source_acquirer)),
      indexer_(std::move(components.indexer)),
      extractor_(std::move(components.extractor)),
      analyzer_(std::move(components.analyzer)),
      reporter_(std::move(components.reporter)) {}

PipelineResult DefaultAnalyzerPipeline::Run(const AnalysisConfig &config) {
  const auto sources = source_acquirer_->Acquire(config);
  const auto index = indexer_->BuildIndex(sources);
  const auto extraction = extractor_->Extract(index);
  const auto coherence = analyzer_->Analyze(extraction);
  const auto report = reporter_->Render(extraction, coherence, config);

  return PipelineResult{report, coherence, extraction};
}

} // namespace dsl
