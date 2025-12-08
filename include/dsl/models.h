#pragma once

#include <string>
#include <vector>

namespace dsl {

struct AnalysisConfig {
  std::string root_path;
  std::vector<std::string> formats;
};

struct SourceAcquisitionResult {
  std::vector<std::string> files;
  std::string project_root;
};

struct AstFact {
  std::string name;
  std::string kind;
};

struct AstIndex {
  std::vector<AstFact> facts;
};

struct DslTerm {
  std::string name;
  std::string kind;
  std::string definition;
};

struct DslRelationship {
  std::string subject;
  std::string verb;
  std::string object;
};

struct DslExtractionResult {
  std::vector<DslTerm> terms;
  std::vector<DslRelationship> relationships;
};

struct Finding {
  std::string term;
  std::string description;
};

struct CoherenceResult {
  std::vector<Finding> findings;
};

struct Report {
  std::string markdown;
};

struct PipelineResult {
  Report report;
  CoherenceResult coherence;
  DslExtractionResult extraction;
};

} // namespace dsl
