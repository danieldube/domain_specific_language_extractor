#include <dsl/default_components.h>

#include <algorithm>
#include <chrono>
#include <iomanip>
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
namespace {

std::string EscapeJsonString(const std::string &value) {
  std::ostringstream escaped;
  for (const auto character : value) {
    switch (character) {
    case '"':
      escaped << "\\\"";
      break;
    case '\\':
      escaped << "\\\\";
      break;
    case '\n':
      escaped << "\\n";
      break;
    case '\r':
      escaped << "\\r";
      break;
    case '\t':
      escaped << "\\t";
      break;
    default:
      escaped << character;
      break;
    }
  }
  return escaped.str();
}

std::string JoinWithBreaks(const std::vector<std::string> &items) {
  if (items.empty()) {
    return "";
  }

  std::ostringstream joined;
  for (std::size_t i = 0; i < items.size(); ++i) {
    if (i > 0) {
      joined << "<br>";
    }
    joined << items[i];
  }
  return joined.str();
}

bool ShouldRenderFormat(const std::vector<std::string> &formats,
                        const std::string &format) {
  if (formats.empty()) {
    return format == "markdown";
  }
  return std::find(formats.begin(), formats.end(), format) != formats.end();
}

std::string BuildAnalysisHeaderMarkdown(const AnalysisConfig &config,
                                        const std::string &timestamp) {
  std::ostringstream section;
  section << "## Analysis Header\n\n";
  section << "| Field | Value |\n";
  section << "| --- | --- |\n";
  section << "| Generated On | " << timestamp << " |\n";
  section << "| Source | " << config.root_path << " |\n";
  section << "| Scope Notes | "
          << (config.scope_notes.empty() ? "None" : config.scope_notes) << " |\n\n";
  return section.str();
}

std::string BuildTermsMarkdown(const DslExtractionResult &extraction) {
  std::ostringstream section;
  section << "## Canonical Terms (Glossary)\n\n";
  section << "| Term | Kind | Definition | Evidence | Aliases | Usage Count |\n";
  section << "| --- | --- | --- | --- | --- | --- |\n";
  if (extraction.terms.empty()) {
    section << "| None | - | - | - | - | - |\n\n";
    return section.str();
  }

  for (const auto &term : extraction.terms) {
    section << "| " << term.name << " | " << term.kind << " | "
            << term.definition << " | " << JoinWithBreaks(term.evidence) << " | "
            << JoinWithBreaks(term.aliases) << " | " << term.usage_count
            << " |\n";
  }
  section << "\n";
  return section.str();
}

std::string BuildRelationshipsMarkdown(const DslExtractionResult &extraction) {
  std::ostringstream section;
  section << "## Relationships\n\n";
  section << "| Subject | Verb | Object | Evidence | Notes | Usage Count |\n";
  section << "| --- | --- | --- | --- | --- | --- |\n";
  if (extraction.relationships.empty()) {
    section << "| None | - | - | - | - | - |\n\n";
    return section.str();
  }

  for (const auto &relationship : extraction.relationships) {
    section << "| " << relationship.subject << " | " << relationship.verb
            << " | " << relationship.object << " | "
            << JoinWithBreaks(relationship.evidence) << " | "
            << (relationship.notes.empty() ? "-" : relationship.notes) << " | "
            << relationship.usage_count << " |\n";
  }
  section << "\n";
  return section.str();
}

std::string BuildWorkflowsMarkdown(const DslExtractionResult &extraction) {
  std::ostringstream section;
  section << "## Workflows\n\n";
  if (extraction.workflows.empty()) {
    section << "- None\n\n";
    return section.str();
  }

  for (const auto &workflow : extraction.workflows) {
    section << "- " << workflow.name << "\n";
    for (std::size_t i = 0; i < workflow.steps.size(); ++i) {
      section << "  " << (i + 1) << ". " << workflow.steps[i] << "\n";
    }
    section << "\n";
  }
  return section.str();
}

std::string BuildIncoherenceMarkdown(const CoherenceResult &coherence) {
  std::ostringstream section;
  section << "## Incoherence Report\n\n";
  section << "| Term | Conflict | Examples | Suggested Canonical Form | Details |\n";
  section << "| --- | --- | --- | --- | --- |\n";

  if (coherence.findings.empty()) {
    section << "| None | - | - | - | - |\n\n";
    return section.str();
  }

  for (const auto &finding : coherence.findings) {
    section << "| " << finding.term << " | "
            << (finding.conflict.empty() ? finding.description : finding.conflict)
            << " | " << JoinWithBreaks(finding.examples) << " | "
            << (finding.suggested_canonical_form.empty()
                    ? "-"
                    : finding.suggested_canonical_form)
            << " | "
            << (finding.description.empty() ? finding.conflict : finding.description)
            << " |\n";
  }
  section << "\n";
  return section.str();
}

std::string BuildExtractionNotesMarkdown(const DslExtractionResult &extraction) {
  std::ostringstream section;
  section << "## Extraction Notes\n\n";
  if (extraction.extraction_notes.empty()) {
    section << "- None\n";
    return section.str();
  }

  for (const auto &note : extraction.extraction_notes) {
    section << "- " << note << "\n";
  }
  return section.str();
}

std::string BuildAnalysisHeaderJson(const AnalysisConfig &config,
                                    const std::string &timestamp) {
  std::ostringstream json;
  json << "\"analysis_header\": {";
  json << "\"generated_on\": \"" << EscapeJsonString(timestamp) << "\",";
  json << "\"source\": \"" << EscapeJsonString(config.root_path) << "\",";
  json << "\"scope_notes\": \""
       << EscapeJsonString(config.scope_notes.empty() ? "None"
                                                     : config.scope_notes)
       << "\"}";
  return json.str();
}

std::string BuildTermsJson(const DslExtractionResult &extraction) {
  std::ostringstream json;
  json << "\"terms\": [";
  for (std::size_t i = 0; i < extraction.terms.size(); ++i) {
    const auto &term = extraction.terms[i];
    if (i > 0) {
      json << ",";
    }
    json << "{\"name\": \"" << EscapeJsonString(term.name) << "\",";
    json << "\"kind\": \"" << EscapeJsonString(term.kind) << "\",";
    json << "\"definition\": \"" << EscapeJsonString(term.definition)
         << "\",";
    json << "\"evidence\": [";
    for (std::size_t j = 0; j < term.evidence.size(); ++j) {
      if (j > 0) {
        json << ",";
      }
      json << "\"" << EscapeJsonString(term.evidence[j]) << "\"";
    }
    json << "],";
    json << "\"aliases\": [";
    for (std::size_t j = 0; j < term.aliases.size(); ++j) {
      if (j > 0) {
        json << ",";
      }
      json << "\"" << EscapeJsonString(term.aliases[j]) << "\"";
    }
    json << "],";
    json << "\"usage_count\": " << term.usage_count << "}";
  }
  json << "]";
  return json.str();
}

std::string BuildRelationshipsJson(const DslExtractionResult &extraction) {
  std::ostringstream json;
  json << "\"relationships\": [";
  for (std::size_t i = 0; i < extraction.relationships.size(); ++i) {
    const auto &relationship = extraction.relationships[i];
    if (i > 0) {
      json << ",";
    }
    json << "{\"subject\": \"" << EscapeJsonString(relationship.subject)
         << "\",";
    json << "\"verb\": \"" << EscapeJsonString(relationship.verb) << "\",";
    json << "\"object\": \"" << EscapeJsonString(relationship.object)
         << "\",";
    json << "\"evidence\": [";
    for (std::size_t j = 0; j < relationship.evidence.size(); ++j) {
      if (j > 0) {
        json << ",";
      }
      json << "\"" << EscapeJsonString(relationship.evidence[j]) << "\"";
    }
    json << "],";
    json << "\"notes\": \"" << EscapeJsonString(relationship.notes) << "\",";
    json << "\"usage_count\": " << relationship.usage_count << "}";
  }
  json << "]";
  return json.str();
}

std::string BuildWorkflowsJson(const DslExtractionResult &extraction) {
  std::ostringstream json;
  json << "\"workflows\": [";
  for (std::size_t i = 0; i < extraction.workflows.size(); ++i) {
    const auto &workflow = extraction.workflows[i];
    if (i > 0) {
      json << ",";
    }
    json << "{\"name\": \"" << EscapeJsonString(workflow.name) << "\",";
    json << "\"steps\": [";
    for (std::size_t j = 0; j < workflow.steps.size(); ++j) {
      if (j > 0) {
        json << ",";
      }
      json << "\"" << EscapeJsonString(workflow.steps[j]) << "\"";
    }
    json << "]}";
  }
  json << "]";
  return json.str();
}

std::string BuildIncoherenceJson(const CoherenceResult &coherence) {
  std::ostringstream json;
  json << "\"incoherence_report\": [";
  for (std::size_t i = 0; i < coherence.findings.size(); ++i) {
    const auto &finding = coherence.findings[i];
    if (i > 0) {
      json << ",";
    }
    json << "{\"term\": \"" << EscapeJsonString(finding.term) << "\",";
    json << "\"conflict\": \"" << EscapeJsonString(finding.conflict)
         << "\",";
    json << "\"examples\": [";
    for (std::size_t j = 0; j < finding.examples.size(); ++j) {
      if (j > 0) {
        json << ",";
      }
      json << "\"" << EscapeJsonString(finding.examples[j]) << "\"";
    }
    json << "],";
    json << "\"suggested_canonical_form\": \""
         << EscapeJsonString(finding.suggested_canonical_form) << "\",";
    json << "\"description\": \"" << EscapeJsonString(finding.description)
         << "\"}";
  }
  json << "]";
  return json.str();
}

std::string BuildExtractionNotesJson(const DslExtractionResult &extraction) {
  std::ostringstream json;
  json << "\"extraction_notes\": [";
  for (std::size_t i = 0; i < extraction.extraction_notes.size(); ++i) {
    if (i > 0) {
      json << ",";
    }
    json << "\"" << EscapeJsonString(extraction.extraction_notes[i]) << "\"";
  }
  json << "]";
  return json.str();
}

} // namespace

Report MarkdownReporter::Render(const DslExtractionResult &extraction,
                                const CoherenceResult &coherence,
                                const AnalysisConfig &config) {
  const auto now = std::chrono::system_clock::now();
  const std::time_t now_time = std::chrono::system_clock::to_time_t(now);
  std::ostringstream timestamp_stream;
  timestamp_stream << std::put_time(std::localtime(&now_time), "%FT%TZ");
  const auto timestamp = timestamp_stream.str();

  Report report;
  const bool render_markdown =
      ShouldRenderFormat(config.formats, "markdown") || config.formats.empty();
  const bool render_json = ShouldRenderFormat(config.formats, "json");

  if (render_markdown) {
    std::ostringstream output;
    output << "# DSL Extraction Report\n\n";
    output << BuildAnalysisHeaderMarkdown(config, timestamp);
    output << BuildTermsMarkdown(extraction);
    output << BuildRelationshipsMarkdown(extraction);
    output << BuildWorkflowsMarkdown(extraction);
    output << BuildIncoherenceMarkdown(coherence);
    output << BuildExtractionNotesMarkdown(extraction);
    report.markdown = output.str();
  }

  if (render_json) {
    std::ostringstream output;
    output << "{";
    output << BuildAnalysisHeaderJson(config, timestamp) << ",";
    output << BuildTermsJson(extraction) << ",";
    output << BuildRelationshipsJson(extraction) << ",";
    output << BuildWorkflowsJson(extraction) << ",";
    output << BuildIncoherenceJson(coherence) << ",";
    output << BuildExtractionNotesJson(extraction);
    output << "}";
    report.json = output.str();
  }

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
