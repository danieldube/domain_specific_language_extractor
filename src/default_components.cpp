#include <dsl/default_components.h>

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
    term.evidence.push_back(fact.name + ":1-5");
    term.aliases.push_back(fact.name + "Alias");
    term.usage_count = 1;
    result.terms.push_back(term);
  }

  for (std::size_t i = 1; i < result.terms.size(); ++i) {
    DslRelationship relationship;
    relationship.subject = result.terms[i - 1].name;
    relationship.verb = "precedes";
    relationship.object = result.terms[i].name;
    relationship.evidence.push_back("call_site:10-12");
    relationship.notes = "Sequential operation";
    relationship.usage_count = 1;
    result.relationships.push_back(relationship);
  }

  if (result.terms.size() > 1) {
    DslExtractionResult::Workflow workflow;
    workflow.name = "Heuristic workflow";
    for (const auto &relationship : result.relationships) {
      workflow.steps.push_back(relationship.subject + " -> " +
                               relationship.object);
    }
    result.workflows.push_back(workflow);
  }

  result.extraction_notes.push_back(
      "Heuristic extraction generated placeholder evidence and aliases.");

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
      finding.conflict = "Duplicate term name indicates incoherent DSL usage.";
      finding.examples.push_back(name + ": duplicate usage");
      finding.suggested_canonical_form = name;
      finding.description = finding.conflict;
      result.findings.push_back(finding);
    }
  }

  if (extraction.relationships.empty() && !extraction.terms.empty()) {
    Finding finding;
    finding.term = extraction.terms.front().name;
    finding.conflict = "No relationships detected; DSL may be incomplete.";
    finding.examples.push_back("Relationships missing for term");
    finding.suggested_canonical_form = extraction.terms.front().name;
    finding.description = finding.conflict;
    result.findings.push_back(finding);
  }

  return result;
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
