#include <dsl/heuristic_dsl_extractor.h>

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

struct ParsedKind {
  std::string base_kind;
  std::optional<std::string> relationship_target;
  std::optional<std::string> descriptor;
};

struct RelationshipKey {
  std::string subject;
  std::string verb;
  std::string object;

  bool operator==(const RelationshipKey &other) const {
    return subject == other.subject && verb == other.verb &&
           object == other.object;
  }
};

struct RelationshipKeyHash {
  std::size_t operator()(const RelationshipKey &key) const {
    return std::hash<std::string>{}(key.subject) ^
           (std::hash<std::string>{}(key.verb) << 1) ^
           (std::hash<std::string>{}(key.object) << 2);
  }
};

using RelationshipMap =
    std::unordered_map<RelationshipKey, dsl::DslRelationship,
                       RelationshipKeyHash>;
using TermMap = std::unordered_map<std::string, dsl::DslTerm>;
using AliasMap =
    std::unordered_map<std::string, std::unordered_set<std::string>>;

std::string CanonicalizeName(std::string name) {
  std::replace(name.begin(), name.end(), ':', '.');
  std::transform(name.begin(), name.end(), name.begin(),
                 [](unsigned char character) {
                   return static_cast<char>(std::tolower(character));
                 });
  return name;
}

std::string DeriveTermKind(const std::string &base_kind) {
  if (base_kind == "type" || base_kind == "variable") {
    return "Entity";
  }
  return "Action";
}

void ExtractDescriptor(ParsedKind &parsed) {
  const auto descriptor_separator = parsed.base_kind.find('|');
  if (descriptor_separator == std::string::npos) {
    return;
  }
  parsed.descriptor = parsed.base_kind.substr(descriptor_separator + 1);
  parsed.base_kind = parsed.base_kind.substr(0, descriptor_separator);
}

void ExtractRelationshipTarget(ParsedKind &parsed) {
  const auto relationship_separator = parsed.base_kind.find(':');
  if (relationship_separator == std::string::npos) {
    return;
  }
  parsed.relationship_target =
      parsed.base_kind.substr(relationship_separator + 1);
  parsed.base_kind = parsed.base_kind.substr(0, relationship_separator);
}

ParsedKind ParseKind(std::string kind) {
  ParsedKind parsed{std::move(kind), std::nullopt, std::nullopt};
  ExtractDescriptor(parsed);
  ExtractRelationshipTarget(parsed);
  return parsed;
}

std::string RelationshipVerbForKind(const std::string &base_kind) {
  if (base_kind == "call") {
    return "calls";
  }
  if (base_kind == "type_usage") {
    return "uses-type";
  }
  if (base_kind == "owns") {
    return "owns";
  }
  return base_kind;
}

void AddEvidence(const std::string &source_location,
                 std::vector<std::string> &evidence) {
  if (source_location.empty()) {
    return;
  }
  if (std::find(evidence.begin(), evidence.end(), source_location) ==
      evidence.end()) {
    evidence.push_back(source_location);
  }
}

void MergeDefinition(const std::optional<std::string> &descriptor,
                     dsl::DslTerm &term) {
  if (!descriptor.has_value()) {
    return;
  }
  if (term.definition.empty()) {
    term.definition = *descriptor;
    return;
  }
  if (term.definition.find(*descriptor) == std::string::npos) {
    term.definition.append(" | ");
    term.definition.append(*descriptor);
  }
}

void EnsureKindInitialized(const ParsedKind &parsed, dsl::DslTerm &term) {
  if (!term.kind.empty()) {
    return;
  }
  term.kind = DeriveTermKind(parsed.base_kind);
}

void EnsureDefaultDefinition(const ParsedKind &parsed, dsl::DslTerm &term) {
  if (!term.definition.empty()) {
    return;
  }
  term.definition = "Declared as " + parsed.base_kind;
}

void EnsureFallbackDefinitions(TermMap &terms) {
  for (auto &[name, term] : terms) {
    if (term.definition.empty()) {
      term.definition = "Inferred from symbol context";
    }
  }
}

void AddAlias(const dsl::AstFact &fact, const std::string &canonical_name,
              AliasMap &aliases, dsl::DslTerm &term) {
  if (canonical_name == fact.name) {
    return;
  }
  if (aliases[canonical_name].insert(fact.name).second) {
    term.aliases.push_back(fact.name);
  }
}

RelationshipKey MakeRelationshipKey(const dsl::AstFact &fact,
                                    const ParsedKind &parsed) {
  return {CanonicalizeName(fact.name),
          RelationshipVerbForKind(parsed.base_kind),
          CanonicalizeName(*parsed.relationship_target)};
}

void InitializeRelationshipParticipants(const RelationshipKey &key,
                                        dsl::DslRelationship &relationship) {
  relationship.subject = key.subject;
  relationship.verb = key.verb;
  relationship.object = key.object;
}

void UpdateRelationshipNotes(const ParsedKind &parsed,
                             dsl::DslRelationship &relationship) {
  if (!parsed.descriptor.has_value() || parsed.descriptor->empty()) {
    return;
  }
  if (relationship.notes.empty()) {
    relationship.notes = *parsed.descriptor;
    return;
  }
  if (relationship.notes.find(*parsed.descriptor) == std::string::npos) {
    relationship.notes.append(" | ");
    relationship.notes.append(*parsed.descriptor);
  }
}

void TrackRelationship(const dsl::AstFact &fact, const ParsedKind &parsed,
                       RelationshipMap &relationships) {
  if (!parsed.relationship_target.has_value()) {
    return;
  }
  const auto key = MakeRelationshipKey(fact, parsed);
  auto &relationship = relationships[key];
  InitializeRelationshipParticipants(key, relationship);
  AddEvidence(fact.source_location, relationship.evidence);
  UpdateRelationshipNotes(parsed, relationship);
  ++relationship.usage_count;
}

void UpdateTermFromFact(const dsl::AstFact &fact, TermMap &terms,
                        AliasMap &aliases, RelationshipMap &relationships) {
  const auto parsed = ParseKind(fact.kind);
  const auto canonical_name = CanonicalizeName(fact.name);
  auto &term = terms[canonical_name];
  term.name = canonical_name;
  EnsureKindInitialized(parsed, term);
  MergeDefinition(parsed.descriptor, term);
  AddEvidence(fact.source_location, term.evidence);
  ++term.usage_count;
  AddAlias(fact, canonical_name, aliases, term);
  TrackRelationship(fact, parsed, relationships);
  EnsureDefaultDefinition(parsed, term);
}

std::vector<dsl::DslTerm> MoveTerms(TermMap &terms) {
  std::vector<dsl::DslTerm> term_list;
  term_list.reserve(terms.size());
  for (auto &entry : terms) {
    term_list.push_back(std::move(entry.second));
  }
  return term_list;
}

std::vector<dsl::DslTerm> BuildTerms(const dsl::AstIndex &index,
                                     RelationshipMap &relationships) {
  TermMap terms;
  AliasMap aliases;
  for (const auto &fact : index.facts) {
    UpdateTermFromFact(fact, terms, aliases, relationships);
  }
  EnsureFallbackDefinitions(terms);
  return MoveTerms(terms);
}

std::vector<dsl::DslRelationship>
BuildRelationships(RelationshipMap relationships) {
  std::vector<dsl::DslRelationship> relationship_list;
  relationship_list.reserve(relationships.size());
  for (auto &entry : relationships) {
    relationship_list.push_back(std::move(entry.second));
  }
  return relationship_list;
}

std::vector<dsl::DslExtractionResult::Workflow>
BuildWorkflows(const std::vector<dsl::DslRelationship> &relationships) {
  if (relationships.empty()) {
    return {};
  }
  dsl::DslExtractionResult::Workflow workflow{};
  workflow.name = "Heuristic relationships";
  for (const auto &relationship : relationships) {
    workflow.steps.push_back(relationship.subject + " " + relationship.verb +
                             " " + relationship.object);
  }
  return {workflow};
}

void AppendExtractionNotes(dsl::DslExtractionResult &result) {
  result.extraction_notes.push_back(
      "Heuristic extraction canonicalized identifiers, synthesized definitions "
      "from signatures, and inferred relationships "
      "from AST facts.");
}

} // namespace

namespace dsl {

DslExtractionResult HeuristicDslExtractor::Extract(const AstIndex &index) {
  DslExtractionResult result{};
  RelationshipMap relationships;
  result.terms = BuildTerms(index, relationships);
  result.relationships = BuildRelationships(std::move(relationships));
  result.workflows = BuildWorkflows(result.relationships);
  AppendExtractionNotes(result);
  return result;
}

} // namespace dsl
