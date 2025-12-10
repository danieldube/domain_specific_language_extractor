#include <dsl/default_components.h>

#include <algorithm>
#include <iterator>
#include <unordered_map>

namespace {

dsl::DslTerm BuildTerm(const dsl::AstFact &fact) {
  dsl::DslTerm term{};
  term.name = fact.name;
  term.kind = fact.kind == "type" ? "Entity" : "Action";
  term.definition = "Derived from " + fact.name;
  term.evidence.push_back(fact.source_location.empty()
                              ? fact.name + ":1-5"
                              : fact.source_location);
  term.aliases.push_back(fact.name + "Alias");
  term.usage_count = 1;
  return term;
}

std::vector<dsl::DslTerm> BuildTerms(const dsl::AstIndex &index) {
  std::vector<dsl::DslTerm> terms;
  terms.reserve(index.facts.size());
  std::transform(index.facts.begin(), index.facts.end(),
                 std::back_inserter(terms), BuildTerm);
  return terms;
}

dsl::DslRelationship BuildRelationship(const dsl::DslTerm &previous,
                                       const dsl::DslTerm &current) {
  dsl::DslRelationship relationship{};
  relationship.subject = previous.name;
  relationship.verb = "precedes";
  relationship.object = current.name;
  relationship.evidence.push_back("call_site:10-12");
  relationship.notes = "Sequential operation";
  relationship.usage_count = 1;
  return relationship;
}

std::vector<dsl::DslRelationship>
BuildRelationships(const std::vector<dsl::DslTerm> &terms) {
  std::vector<dsl::DslRelationship> relationships;
  for (std::size_t i = 1; i < terms.size(); ++i) {
    relationships.push_back(BuildRelationship(terms[i - 1], terms[i]));
  }
  return relationships;
}

dsl::DslExtractionResult::Workflow
BuildWorkflow(const std::vector<dsl::DslRelationship> &relationships) {
  dsl::DslExtractionResult::Workflow workflow{};
  workflow.name = "Heuristic workflow";
  for (const auto &relationship : relationships) {
    workflow.steps.push_back(relationship.subject + " -> " +
                             relationship.object);
  }
  return workflow;
}

std::vector<dsl::DslExtractionResult::Workflow>
BuildWorkflows(const std::vector<dsl::DslRelationship> &relationships) {
  if (relationships.empty()) {
    return {};
  }
  return {BuildWorkflow(relationships)};
}

void AppendExtractionNotes(dsl::DslExtractionResult &result) {
  result.extraction_notes.push_back(
      "Heuristic extraction generated placeholder evidence and aliases.");
}

std::unordered_map<std::string, int>
CountTermOccurrences(const std::vector<dsl::DslTerm> &terms) {
  std::unordered_map<std::string, int> occurrence_counts;
  for (const auto &term : terms) {
    ++occurrence_counts[term.name];
  }
  return occurrence_counts;
}

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
std::unique_ptr<Interface> EnsureComponent(
    std::unique_ptr<Interface> component) {
  if (component) {
    return component;
  }
  return std::make_unique<Implementation>();
}

} // namespace

namespace dsl {

DslExtractionResult HeuristicDslExtractor::Extract(const AstIndex &index) {
  DslExtractionResult result{};
  result.terms = BuildTerms(index);
  result.relationships = BuildRelationships(result.terms);
  result.workflows = BuildWorkflows(result.relationships);
  AppendExtractionNotes(result);
  return result;
}

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
  components_.indexer =
      EnsureComponent<AstIndexer, CompileCommandsAstIndexer>(
          std::move(components_.indexer));
  components_.extractor = EnsureComponent<DslExtractor, HeuristicDslExtractor>(
      std::move(components_.extractor));
  components_.analyzer = EnsureComponent<CoherenceAnalyzer,
                                         RuleBasedCoherenceAnalyzer>(
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
