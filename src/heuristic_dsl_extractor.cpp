#include <dsl/heuristic_dsl_extractor.h>

#include <algorithm>
#include <cctype>
#include <map>
#include <optional>
#include <set>
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

std::string EvidenceLocation(const dsl::AstFact &fact) {
  if (fact.source_location.empty()) {
    return fact.range;
  }
  if (fact.range.empty()) {
    return fact.source_location;
  }
  return fact.source_location + "@" + fact.range;
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

ParsedKind ParseKind(const dsl::AstFact &fact) {
  ParsedKind parsed{fact.kind, std::nullopt, std::nullopt};
  ExtractDescriptor(parsed);
  ExtractRelationshipTarget(parsed);
  if (!fact.descriptor.empty()) {
    parsed.descriptor = fact.descriptor;
  }
  if (!fact.target.empty()) {
    parsed.relationship_target = fact.target;
  }
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

bool IsSymbolReference(const ParsedKind &parsed) {
  return parsed.base_kind == "reference" || parsed.base_kind == "alias" ||
         parsed.base_kind == "symbol_reference";
}

void AddEvidence(const std::string &evidence_location,
                 std::vector<std::string> &evidence) {
  if (evidence_location.empty()) {
    return;
  }
  if (std::find(evidence.begin(), evidence.end(), evidence_location) ==
      evidence.end()) {
    evidence.push_back(evidence_location);
  }
}

void AppendDefinitionPart(const std::string &definition_part,
                          dsl::DslTerm &term) {
  if (definition_part.empty()) {
    return;
  }
  if (term.definition.empty()) {
    term.definition = definition_part;
    return;
  }
  if (term.definition.find(definition_part) == std::string::npos) {
    term.definition.append(" | ");
    term.definition.append(definition_part);
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

void AppendAlias(const std::string &canonical_name, const std::string &alias,
                 AliasMap &aliases, dsl::DslTerm &term) {
  if (canonical_name == alias) {
    return;
  }
  if (aliases[canonical_name].insert(alias).second) {
    term.aliases.push_back(alias);
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
  AddEvidence(EvidenceLocation(fact), relationship.evidence);
  UpdateRelationshipNotes(parsed, relationship);
  ++relationship.usage_count;
}

void TrackTargetReference(const dsl::AstFact &fact, const ParsedKind &parsed,
                          TermMap &terms, AliasMap &aliases) {
  if (!parsed.relationship_target.has_value()) {
    return;
  }
  const auto target_name = CanonicalizeName(*parsed.relationship_target);
  auto &target = terms[target_name];
  if (target.name.empty()) {
    target.name = target_name;
  }
  AddEvidence(EvidenceLocation(fact), target.evidence);
  ++target.usage_count;
  if (IsSymbolReference(parsed)) {
    AppendAlias(target_name, fact.name, aliases, target);
  }
}

void UpdateTermFromFact(const dsl::AstFact &fact, TermMap &terms,
                        AliasMap &aliases, RelationshipMap &relationships) {
  const auto parsed = ParseKind(fact);
  if (IsSymbolReference(parsed) && parsed.relationship_target.has_value()) {
    TrackTargetReference(fact, parsed, terms, aliases);
    return;
  }
  const auto canonical_name = CanonicalizeName(fact.name);
  auto &term = terms[canonical_name];
  term.name = canonical_name;
  EnsureKindInitialized(parsed, term);
  AppendDefinitionPart(parsed.descriptor.value_or(""), term);
  AppendDefinitionPart(fact.signature, term);
  AddEvidence(EvidenceLocation(fact), term.evidence);
  ++term.usage_count;
  AppendAlias(canonical_name, fact.name, aliases, term);
  TrackRelationship(fact, parsed, relationships);
  TrackTargetReference(fact, parsed, terms, aliases);
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
  std::sort(relationship_list.begin(), relationship_list.end(),
            [](const auto &lhs, const auto &rhs) {
              if (lhs.subject != rhs.subject) {
                return lhs.subject < rhs.subject;
              }
              if (lhs.verb != rhs.verb) {
                return lhs.verb < rhs.verb;
              }
              return lhs.object < rhs.object;
            });
  return relationship_list;
}

std::vector<dsl::DslExtractionResult::Workflow>
BuildWorkflows(const std::vector<dsl::DslRelationship> &relationships) {
  if (relationships.empty()) {
    return {};
  }
  std::map<std::string, std::vector<const dsl::DslRelationship *>> adjacency;
  std::set<std::string> objects;
  for (const auto &relationship : relationships) {
    adjacency[relationship.subject].push_back(&relationship);
    objects.insert(relationship.object);
  }
  std::vector<dsl::DslExtractionResult::Workflow> workflows;
  std::set<const dsl::DslRelationship *> visited;

  const auto build_steps = [&](const auto &self, const std::string &subject,
                               std::vector<std::string> &steps) -> void {
    const auto iterator = adjacency.find(subject);
    if (iterator == adjacency.end()) {
      return;
    }
    for (const auto *relationship : iterator->second) {
      if (!visited.insert(relationship).second) {
        continue;
      }
      steps.push_back(relationship->subject + " " + relationship->verb + " " +
                      relationship->object);
      self(self, relationship->object, steps);
    }
  };

  for (const auto &entry : adjacency) {
    const auto &subject = entry.first;
    if (objects.find(subject) != objects.end()) {
      continue;
    }
    dsl::DslExtractionResult::Workflow workflow{};
    workflow.name = subject + " workflow";
    build_steps(build_steps, subject, workflow.steps);
    if (!workflow.steps.empty()) {
      workflows.push_back(std::move(workflow));
    }
  }

  if (!visited.empty() && visited.size() == relationships.size()) {
    return workflows;
  }

  if (workflows.empty()) {
    dsl::DslExtractionResult::Workflow workflow{};
    workflow.name = "Heuristic relationships";
    for (const auto &relationship : relationships) {
      workflow.steps.push_back(relationship.subject + " " + relationship.verb +
                               " " + relationship.object);
    }
    workflows.push_back(std::move(workflow));
    return workflows;
  }

  for (const auto &relationship : relationships) {
    if (visited.find(&relationship) != visited.end()) {
      continue;
    }
    workflows.front().steps.push_back(relationship.subject + " " +
                                      relationship.verb + " " +
                                      relationship.object);
  }

  return workflows;
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
  result.facts = index.facts;
  AppendExtractionNotes(result);
  return result;
}

} // namespace dsl
