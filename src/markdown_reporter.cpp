#include <dsl/default_components.h>

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <unordered_map>

namespace dsl {
namespace {

std::string EscapeJsonString(const std::string &value) {
  static const std::unordered_map<char, std::string> replacements{
      {'"', "\\\""},
      {'\\', "\\\\"},
      {'\n', "\\n"},
      {'\r', "\\r"},
      {'\t', "\\t"}};

  std::string escaped;
  escaped.reserve(value.size());
  for (const auto character : value) {
    const auto replacement = replacements.find(character);
    if (replacement != replacements.end()) {
      escaped.append(replacement->second);
    } else {
      escaped.push_back(character);
    }
  }
  return escaped;
}

template <typename Collection, typename Formatter>
std::string Join(const Collection &items, const std::string &delimiter,
                 Formatter formatter) {
  std::ostringstream output;
  bool first = true;
  std::for_each(items.begin(), items.end(), [&](const auto &item) {
    if (!first) {
      output << delimiter;
    }
    output << formatter(item);
    first = false;
  });
  return output.str();
}

std::string JoinWithBreaks(const std::vector<std::string> &items) {
  return Join(items, "<br>", [](const std::string &value) { return value; });
}

std::string JoinJsonArray(const std::vector<std::string> &values) {
  return Join(values, ",", [](const std::string &value) {
    return "\"" + EscapeJsonString(value) + "\"";
  });
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
  std::string scope_notes = "None";
  if (!config.scope_notes.empty()) {
    scope_notes = config.scope_notes;
  }
  section << "| Scope Notes | " << scope_notes << " |\n\n";
  return section.str();
}

std::string BuildTermsMarkdown(const DslExtractionResult &extraction) {
  std::ostringstream section;
  section << "## Canonical Terms (Glossary)\n\n";
  section
      << "| Term | Kind | Definition | Evidence | Aliases | Usage Count |\n";
  section << "| --- | --- | --- | --- | --- | --- |\n";
  if (extraction.terms.empty()) {
    section << "| None | - | - | - | - | - |\n\n";
    return section.str();
  }

  for (const auto &term : extraction.terms) {
    section << "| " << term.name << " | " << term.kind << " | "
            << term.definition << " | " << JoinWithBreaks(term.evidence)
            << " | " << JoinWithBreaks(term.aliases) << " | "
            << term.usage_count << " |\n";
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
    std::string relationship_notes = "-";
    if (!relationship.notes.empty()) {
      relationship_notes = relationship.notes;
    }

    section << "| " << relationship.subject << " | " << relationship.verb
            << " | " << relationship.object << " | "
            << JoinWithBreaks(relationship.evidence) << " | "
            << relationship_notes << " | " << relationship.usage_count
            << " |\n";
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
  section << "| Term | Conflict | Examples | Suggested Canonical Form | "
             "Details |\n";
  section << "| --- | --- | --- | --- | --- |\n";

  if (coherence.findings.empty()) {
    section << "| None | - | - | - | - |\n\n";
    return section.str();
  }

  for (const auto &finding : coherence.findings) {
    std::string conflict = finding.description;
    if (!finding.conflict.empty()) {
      conflict = finding.conflict;
    }

    std::string suggested_canonical = "-";
    if (!finding.suggested_canonical_form.empty()) {
      suggested_canonical = finding.suggested_canonical_form;
    }

    std::string details = finding.conflict;
    if (!finding.description.empty()) {
      details = finding.description;
    }

    section << "| " << finding.term << " | " << conflict << " | "
            << JoinWithBreaks(finding.examples) << " | " << suggested_canonical
            << " | " << details << " |\n";
  }
  section << "\n";
  return section.str();
}

std::string
BuildExtractionNotesMarkdown(const DslExtractionResult &extraction) {
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
  std::string scope_notes = "None";
  if (!config.scope_notes.empty()) {
    scope_notes = config.scope_notes;
  }
  json << "\"scope_notes\": \"" << EscapeJsonString(scope_notes) << "\"}";
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
    json << "\"definition\": \"" << EscapeJsonString(term.definition) << "\",";
    json << "\"evidence\": [" << JoinJsonArray(term.evidence) << "],";
    json << "\"aliases\": [" << JoinJsonArray(term.aliases) << "],";
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
    json << "\"object\": \"" << EscapeJsonString(relationship.object) << "\",";
    json << "\"evidence\": [" << JoinJsonArray(relationship.evidence) << "],";
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
    json << "\"steps\": [" << JoinJsonArray(workflow.steps) << "]}";
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
    json << "\"conflict\": \"" << EscapeJsonString(finding.conflict) << "\",";
    json << "\"examples\": [" << JoinJsonArray(finding.examples) << "],";
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
  json << JoinJsonArray(extraction.extraction_notes);
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

} // namespace dsl
