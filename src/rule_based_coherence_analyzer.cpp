#include <dsl/rule_based_coherence_analyzer.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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
  finding.examples.emplace_back("Relationships missing for term");
  finding.suggested_canonical_form = term.name;
  finding.description = finding.conflict;
  result.findings.push_back(finding);
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
  std::unordered_map<std::string, std::unordered_set<std::string>>
      alias_to_terms;
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
    const auto example =
        relationship.evidence.empty()
            ? relationship.verb + ": " + relationship.subject + " " +
                  relationship.object
            : relationship.verb + ": " + relationship.evidence.front();
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
    finding.term = subject;
    finding.term.append("->");
    finding.term.append(object);
    finding.conflict =
        "Conflicting verbs found between the same subject and object.";
    for (const auto &[verb, examples] : evidence.verbs_to_examples) {
      finding.examples.push_back(examples.front());
    }
    finding.suggested_canonical_form = subject;
    finding.suggested_canonical_form.append(" ");
    finding.suggested_canonical_form.append(object);
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
    if (relationship_participants.find(term.name) !=
        relationship_participants.end()) {
      continue;
    }

    dsl::Finding finding{};
    finding.term = term.name;
    finding.conflict =
        "High-usage term lacks relationships; DSL graph may be incomplete.";
    if (!term.evidence.empty()) {
      finding.examples.push_back(term.evidence.front());
    } else {
      finding.examples.push_back("usage count: " +
                                 std::to_string(term.usage_count));
    }
    finding.suggested_canonical_form = term.name;
    finding.description = finding.conflict;
    result.findings.push_back(finding);
  }
}

void AddCanonicalizationInconsistencyFindings(
    const dsl::DslExtractionResult &extraction, dsl::CoherenceResult &result) {
  std::unordered_map<std::string, std::unordered_set<std::string>>
      canonical_to_names;
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
    finding.examples.insert(finding.examples.end(), names.begin(), names.end());
    finding.suggested_canonical_form = *names.begin();
    finding.description = finding.conflict;
    result.findings.push_back(finding);
  }
}

std::string Trim(std::string value) {
  const auto is_space = [](unsigned char character) {
    return std::isspace(character) != 0;
  };
  value.erase(value.begin(), std::find_if(value.begin(), value.end(),
                                          [&](unsigned char character) {
                                            return !is_space(character);
                                          }));
  value.erase(std::find_if(
                  value.rbegin(), value.rend(),
                  [&](unsigned char character) { return !is_space(character); })
                  .base(),
              value.end());
  return value;
}

bool StartsWithInsensitive(const std::string &value,
                           const std::string &prefix) {
  if (value.size() < prefix.size()) {
    return false;
  }
  for (std::size_t index = 0; index < prefix.size(); ++index) {
    if (std::tolower(static_cast<unsigned char>(value[index])) !=
        std::tolower(static_cast<unsigned char>(prefix[index]))) {
      return false;
    }
  }
  return true;
}

std::string ExtractReturnType(const std::string &signature) {
  const auto paren_position = signature.find('(');
  if (paren_position == std::string::npos) {
    return {};
  }
  const auto prefix = signature.substr(0, paren_position);
  const auto last_space = prefix.find_last_of(' ');
  if (last_space == std::string::npos) {
    return Trim(prefix);
  }
  return Trim(prefix.substr(0, last_space));
}

bool IsBoolType(const std::string &type) {
  const auto normalized = CanonicalizeName(type);
  return normalized == "bool" || normalized == "const bool";
}

bool IsVoidType(const std::string &type) {
  const auto normalized = CanonicalizeName(type);
  return normalized == "void";
}

bool IsMutationKind(const std::string &kind) {
  const auto normalized = CanonicalizeName(kind);
  return normalized == "mutation" || normalized == "assignment" ||
         normalized == "state_change";
}

std::string FactEvidence(const dsl::AstFact &fact) {
  if (!fact.source_location.empty()) {
    return fact.source_location;
  }
  if (!fact.descriptor.empty()) {
    return fact.descriptor;
  }
  return fact.signature;
}

struct FunctionBehavior {
  bool has_mutation = false;
  std::vector<std::string> mutation_evidence;
  std::string return_type;
  std::string signature_evidence;
};

struct IntentAnalysisContext {
  std::unordered_map<std::string, FunctionBehavior> functions;
  std::unordered_map<std::string, std::vector<std::string>> call_targets;
  std::unordered_map<std::string, std::string> call_evidence;
};

std::string CallKey(const std::string &caller, const std::string &target) {
  return caller + "->" + target;
}

void CollectIntentFacts(const std::vector<dsl::AstFact> &facts,
                        IntentAnalysisContext &context) {
  for (const auto &fact : facts) {
    const auto canonical_name = CanonicalizeName(fact.name);
    auto &behavior = context.functions[canonical_name];

    if (fact.kind == "function") {
      behavior.return_type = ExtractReturnType(fact.signature);
      behavior.signature_evidence = FactEvidence(fact);
    }
    if (IsMutationKind(fact.kind)) {
      behavior.has_mutation = true;
      behavior.mutation_evidence.push_back(FactEvidence(fact));
    }
    if (fact.kind == "call") {
      if (fact.target.empty()) {
        continue;
      }
      const auto target = CanonicalizeName(fact.target);
      context.call_targets[canonical_name].push_back(target);
      const auto key = CallKey(canonical_name, target);
      if (context.call_evidence.find(key) == context.call_evidence.end()) {
        context.call_evidence.emplace(key, FactEvidence(fact));
      }
    }
  }
}

bool HasTargetWithSuffix(const std::vector<std::string> &targets,
                         const std::string &suffix) {
  return std::any_of(
      targets.begin(), targets.end(), [&](const std::string &target) {
        return target.size() >= suffix.size() &&
               target.rfind(suffix) == target.size() - suffix.size();
      });
}

bool HasTargetWithSuffix(
    const std::unordered_map<std::string, std::vector<std::string>> &targets,
    const std::string &function, const std::string &suffix) {
  const auto it = targets.find(function);
  if (it == targets.end()) {
    return false;
  }
  return HasTargetWithSuffix(it->second, suffix);
}

void AddGetterFindings(const IntentAnalysisContext &context,
                       dsl::CoherenceResult &result) {
  for (const auto &[name, behavior] : context.functions) {
    if (!StartsWithInsensitive(name, "get")) {
      continue;
    }
    if (behavior.has_mutation) {
      dsl::Finding finding{};
      finding.term = name;
      finding.conflict = "Getter mutates state; expected no mutations.";
      if (!behavior.mutation_evidence.empty()) {
        finding.examples.push_back(behavior.mutation_evidence.front());
      }
      finding.suggested_canonical_form = name;
      finding.description = finding.conflict;
      result.findings.push_back(finding);
    }
    if (!behavior.return_type.empty() && IsVoidType(behavior.return_type)) {
      dsl::Finding finding{};
      finding.term = name;
      finding.conflict = "Getter returns void; expected a value result.";
      if (!behavior.signature_evidence.empty()) {
        finding.examples.push_back(behavior.signature_evidence);
      }
      finding.suggested_canonical_form = name;
      finding.description = finding.conflict;
      result.findings.push_back(finding);
    }
  }
}

void AddSetterFindings(const IntentAnalysisContext &context,
                       dsl::CoherenceResult &result) {
  for (const auto &[name, behavior] : context.functions) {
    if (!StartsWithInsensitive(name, "set")) {
      continue;
    }
    if (behavior.has_mutation) {
      continue;
    }
    dsl::Finding finding{};
    finding.term = name;
    finding.conflict = "Setter lacks mutations; expected state change.";
    if (!behavior.signature_evidence.empty()) {
      finding.examples.push_back(behavior.signature_evidence);
    }
    finding.suggested_canonical_form = name;
    finding.description = finding.conflict;
    result.findings.push_back(finding);
  }
}

void AddPredicateFindings(const IntentAnalysisContext &context,
                          dsl::CoherenceResult &result) {
  for (const auto &[name, behavior] : context.functions) {
    const bool is_predicate =
        StartsWithInsensitive(name, "is") || StartsWithInsensitive(name, "has");
    if (!is_predicate) {
      continue;
    }

    if (!behavior.return_type.empty() && !IsBoolType(behavior.return_type)) {
      dsl::Finding finding{};
      finding.term = name;
      finding.conflict = "Predicate does not return bool; intent unclear.";
      if (!behavior.signature_evidence.empty()) {
        finding.examples.push_back(behavior.signature_evidence);
      }
      finding.suggested_canonical_form = name;
      finding.description = finding.conflict;
      result.findings.push_back(finding);
    }

    if (behavior.has_mutation) {
      dsl::Finding finding{};
      finding.term = name;
      finding.conflict = "Predicate mutates state; expected to be pure.";
      if (!behavior.mutation_evidence.empty()) {
        finding.examples.push_back(behavior.mutation_evidence.front());
      }
      finding.suggested_canonical_form = name;
      finding.description = finding.conflict;
      result.findings.push_back(finding);
    }
  }
}

bool HasLifecycleClosure(const std::vector<std::string> &targets,
                         const std::string &suffix) {
  return std::any_of(
      targets.begin(), targets.end(), [&](const std::string &target) {
        return target == "close" + suffix || target == "teardown" + suffix;
      });
}

void AddLifecycleFindings(const IntentAnalysisContext &context,
                          dsl::CoherenceResult &result) {
  for (const auto &[caller, targets] : context.call_targets) {
    for (const auto &target : targets) {
      std::string suffix;
      if (StartsWithInsensitive(target, "open") ||
          StartsWithInsensitive(target, "init")) {
        suffix = target.substr(4);
      } else {
        continue;
      }

      if (HasLifecycleClosure(targets, suffix)) {
        continue;
      }

      dsl::Finding finding{};
      finding.term = caller;
      finding.conflict =
          "Lifecycle mismatch: opens or inits resource without closing it.";
      const auto key = CallKey(caller, target);
      const auto evidence = context.call_evidence.find(key);
      if (evidence != context.call_evidence.end()) {
        finding.examples.push_back(evidence->second);
      }
      finding.suggested_canonical_form = caller;
      finding.description = finding.conflict;
      result.findings.push_back(finding);
    }
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
  IntentAnalysisContext intent_context{};
  CollectIntentFacts(extraction.facts, intent_context);
  AddGetterFindings(intent_context, result);
  AddSetterFindings(intent_context, result);
  AddPredicateFindings(intent_context, result);
  AddLifecycleFindings(intent_context, result);
  if (!result.findings.empty()) {
    result.severity = CoherenceSeverity::kIncoherent;
  }
  return result;
}

} // namespace dsl
