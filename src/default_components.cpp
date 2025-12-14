#include <dsl/default_components.h>
#include <dsl/heuristic_dsl_extractor.h>

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

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

std::string CanonicalizeName(std::string name) {
  std::replace(name.begin(), name.end(), ':', '.');
  std::transform(name.begin(), name.end(), name.begin(),
                 [](unsigned char character) {
                   return static_cast<char>(std::tolower(character));
                 });
  return name;
}

void AddAmbiguousAliasFindings(const std::vector<dsl::DslTerm> &terms,
                               dsl::CoherenceResult &result) {
  std::unordered_map<std::string, std::unordered_set<std::string>> alias_to_terms;
  for (const auto &term : terms) {
    for (const auto &alias : term.aliases) {
      alias_to_terms[CanonicalizeName(alias)].insert(term.name);
    }
  }

  for (const auto &[alias, owners] : alias_to_terms) {
    if (owners.size() < 2) {
      continue;
    }

    dsl::Finding finding{};
    finding.term = alias;
    finding.conflict =
        "Alias reused across multiple terms; canonical naming may be unclear.";
    std::string example = alias + " used for";
    for (const auto &owner : owners) {
      example.append(" " + owner);
    }
    finding.examples.push_back(example);
    finding.suggested_canonical_form = *owners.begin();
    finding.description = finding.conflict;
    result.findings.push_back(finding);
  }
}

void AddConflictingVerbFindings(
    const std::vector<dsl::DslRelationship> &relationships,
    dsl::CoherenceResult &result) {
  struct RelationshipEvidence {
    std::unordered_map<std::string, std::vector<std::string>> verbs_to_examples;
  };

  std::unordered_map<std::string, RelationshipEvidence> pair_to_relationships;
  for (const auto &relationship : relationships) {
    const auto key = relationship.subject + "->" + relationship.object;
    auto &entry = pair_to_relationships[key];
    const auto example = relationship.evidence.empty()
                             ? relationship.verb + ": " + relationship.subject +
                                   " " + relationship.object
                             : relationship.verb + ": " +
                                   relationship.evidence.front();
    entry.verbs_to_examples[relationship.verb].push_back(example);
  }

  for (const auto &[key, evidence] : pair_to_relationships) {
    if (evidence.verbs_to_examples.size() < 2) {
      continue;
    }

    const auto separator = key.find("->");
    const auto subject = key.substr(0, separator);
    const auto object = key.substr(separator + 2);

    dsl::Finding finding{};
    finding.term = subject + "->" + object;
    finding.conflict =
        "Conflicting verbs found between the same subject and object.";
    for (const auto &[verb, examples] : evidence.verbs_to_examples) {
      finding.examples.push_back(examples.front());
    }
    finding.suggested_canonical_form = subject + " " + object;
    finding.description = finding.conflict;
    result.findings.push_back(finding);
  }
}

void AddHighUsageMissingRelationshipFindings(
    const dsl::DslExtractionResult &extraction, dsl::CoherenceResult &result) {
  std::unordered_set<std::string> relationship_participants;
  for (const auto &relationship : extraction.relationships) {
    relationship_participants.insert(relationship.subject);
    relationship_participants.insert(relationship.object);
  }

  for (const auto &term : extraction.terms) {
    if (term.usage_count < 3) {
      continue;
    }
    if (relationship_participants.contains(term.name)) {
      continue;
    }

    dsl::Finding finding{};
    finding.term = term.name;
    finding.conflict =
        "High-usage term lacks relationships; DSL graph may be incomplete.";
    if (!term.evidence.empty()) {
      finding.examples.push_back(term.evidence.front());
    } else {
      finding.examples.push_back("usage count: " + std::to_string(term.usage_count));
    }
    finding.suggested_canonical_form = term.name;
    finding.description = finding.conflict;
    result.findings.push_back(finding);
  }
}

void AddCanonicalizationInconsistencyFindings(
    const dsl::DslExtractionResult &extraction, dsl::CoherenceResult &result) {
  std::unordered_map<std::string, std::unordered_set<std::string>> canonical_to_names;
  for (const auto &term : extraction.terms) {
    canonical_to_names[CanonicalizeName(term.name)].insert(term.name);
  }
  for (const auto &relationship : extraction.relationships) {
    canonical_to_names[CanonicalizeName(relationship.subject)].insert(
        relationship.subject);
    canonical_to_names[CanonicalizeName(relationship.object)].insert(
        relationship.object);
  }

  for (const auto &[canonical, names] : canonical_to_names) {
    if (names.size() < 2) {
      continue;
    }

    dsl::Finding finding{};
    finding.term = canonical;
    finding.conflict =
        "Inconsistent canonicalization detected for equivalent terms.";
    std::string example = "Variants:";
    for (const auto &name : names) {
      example.append(" " + name);
    }
    finding.examples.push_back(example);
    finding.suggested_canonical_form = canonical;
    finding.description = finding.conflict;
    result.findings.push_back(finding);
  }
}

} // namespace

namespace dsl {

CoherenceResult
RuleBasedCoherenceAnalyzer::Analyze(const DslExtractionResult &extraction) {
  CoherenceResult result{};
  const auto counts = CountTermOccurrences(extraction.terms);
  AddDuplicateFindings(counts, result);
  AddRelationshipMissingFinding(extraction, result);
  AddAmbiguousAliasFindings(extraction.terms, result);
  AddConflictingVerbFindings(extraction.relationships, result);
  AddHighUsageMissingRelationshipFindings(extraction, result);
  AddCanonicalizationInconsistencyFindings(extraction, result);
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
