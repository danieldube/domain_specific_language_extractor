#include <dsl/default_components.h>

#include <sstream>
#include <unordered_map>

namespace dsl {

AstIndex SimpleAstIndexer::BuildIndex(const SourceAcquisitionResult &sources) {
  AstIndex index;
  for (const auto &file : sources.files) {
    AstFact fact;
    fact.name = "symbol_from_" + file;
    fact.kind = "function";
    index.facts.push_back(fact);
  }
  return index;
}

DslExtractionResult HeuristicDslExtractor::Extract(const AstIndex &index) {
  DslExtractionResult result;
  for (const auto &fact : index.facts) {
    DslTerm term;
    term.name = fact.name;
    term.kind = fact.kind == "type" ? "Entity" : "Action";
    term.definition = "Derived from " + fact.name;
    result.terms.push_back(term);
  }

  for (std::size_t i = 1; i < result.terms.size(); ++i) {
    DslRelationship relationship;
    relationship.subject = result.terms[i - 1].name;
    relationship.verb = "precedes";
    relationship.object = result.terms[i].name;
    result.relationships.push_back(relationship);
  }

  return result;
}

CoherenceResult
RuleBasedCoherenceAnalyzer::Analyze(const DslExtractionResult &extraction) {
  CoherenceResult result;
  std::unordered_map<std::string, int> occurrence_counts;
  for (const auto &term : extraction.terms) {
    occurrence_counts[term.name] += 1;
  }

  for (const auto &[name, count] : occurrence_counts) {
    if (count > 1) {
      Finding finding;
      finding.term = name;
      finding.description =
          "Duplicate term name indicates incoherent DSL usage.";
      result.findings.push_back(finding);
    }
  }

  if (extraction.relationships.empty() && !extraction.terms.empty()) {
    Finding finding;
    finding.term = extraction.terms.front().name;
    finding.description = "No relationships detected; DSL may be incomplete.";
    result.findings.push_back(finding);
  }

  return result;
}

Report MarkdownReporter::Render(const DslExtractionResult &extraction,
                                const CoherenceResult &coherence,
                                const AnalysisConfig &config) {
  std::ostringstream output;
  output << "# DSL Extraction Report\n\n";
  output << "Source root: " << config.root_path << "\n\n";

  output << "## Terms\n";
  for (const auto &term : extraction.terms) {
    output << "- " << term.name << " (" << term.kind << "): " << term.definition
           << "\n";
  }
  output << "\n## Relationships\n";
  for (const auto &relationship : extraction.relationships) {
    output << "- " << relationship.subject << " " << relationship.verb << " "
           << relationship.object << "\n";
  }

  output << "\n## Findings\n";
  if (coherence.findings.empty()) {
    output << "- None\n";
  } else {
    for (const auto &finding : coherence.findings) {
      output << "- " << finding.term << ": " << finding.description << "\n";
    }
  }

  Report report;
  report.markdown = output.str();
  return report;
}

AnalyzerPipelineBuilder AnalyzerPipelineBuilder::WithDefaults() {
  AnalyzerPipelineBuilder builder;
  builder.WithSourceAcquirer(std::make_unique<CMakeSourceAcquirer>());
  builder.WithIndexer(std::make_unique<SimpleAstIndexer>());
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
  if (!components_.source_acquirer) {
    components_.source_acquirer = std::make_unique<CMakeSourceAcquirer>();
  }
  if (!components_.indexer) {
    components_.indexer = std::make_unique<SimpleAstIndexer>();
  }
  if (!components_.extractor) {
    components_.extractor = std::make_unique<HeuristicDslExtractor>();
  }
  if (!components_.analyzer) {
    components_.analyzer = std::make_unique<RuleBasedCoherenceAnalyzer>();
  }
  if (!components_.reporter) {
    components_.reporter = std::make_unique<MarkdownReporter>();
  }
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
