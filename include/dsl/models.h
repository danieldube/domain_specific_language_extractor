#pragma once

#include <string>
#include <vector>

namespace dsl {

struct AnalysisConfig {
  std::string root_path;
  std::vector<std::string> formats;
  std::string scope_notes;
};

struct SourceAcquisitionResult {
  std::vector<std::string> files;
  std::string project_root;
  std::string build_directory;
};

struct AstFact {
  std::string name;
  std::string kind;
  std::string source_location;
  std::string signature;
  std::string descriptor;
  std::string target;
  std::string range;
};

struct AstIndex {
  std::vector<AstFact> facts;
};

struct DslTerm {
  std::string name;
  std::string kind;
  std::string definition;
  std::vector<std::string> evidence;
  std::vector<std::string> aliases;
  int usage_count = 0;
};

struct DslRelationship {
  std::string subject;
  std::string verb;
  std::string object;
  std::vector<std::string> evidence;
  std::string notes;
  int usage_count = 0;
};

struct DslExtractionResult {
  std::vector<DslTerm> terms;
  std::vector<DslRelationship> relationships;
  std::vector<std::string> extraction_notes;
  struct Workflow {
    std::string name;
    std::vector<std::string> steps;
  };
  std::vector<Workflow> workflows;
};

struct Finding {
  std::string term;
  std::string conflict;
  std::vector<std::string> examples;
  std::string suggested_canonical_form;
  std::string description;
};

struct CoherenceResult {
  std::vector<Finding> findings;
};

struct Report {
  std::string markdown;
  std::string json;
};

struct PipelineResult {
  Report report;
  CoherenceResult coherence;
  DslExtractionResult extraction;
};

} // namespace dsl
