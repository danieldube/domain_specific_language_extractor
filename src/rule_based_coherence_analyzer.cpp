#include <dsl/rule_based_coherence_analyzer.h>

#include <algorithm>
#include <cctype>
#include <memory>
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
        "Canonicalization inconsistency detected across multiple names.";
    finding.examples.insert(finding.examples.end(), names.begin(), names.end());
    finding.suggested_canonical_form = *names.begin();
    finding.description = finding.conflict;
    result.findings.push_back(finding);
  }
}

struct IntentAnalysisContext {
  std::unordered_map<std::string, std::vector<std::string>> call_targets;
  std::unordered_map<std::string, std::vector<std::string>> call_evidence;
  std::unordered_map<std::string, std::vector<std::string>> predicate_targets;
};

std::string CallKey(const std::string &caller, const std::string &target) {
  return caller + "->" + target;
}

void CollectIntentFacts(const std::vector<dsl::DslFact> &facts,
                        IntentAnalysisContext &context) {
  for (const auto &fact : facts) {
    if (fact.kind != "call") {
      continue;
    }
    if (fact.target.empty()) {
      continue;
    }

    const auto key = CallKey(fact.name, fact.target);
    context.call_targets[fact.name].push_back(fact.target);
    context.call_evidence[key].push_back(fact.evidence);

    if (fact.target.find("Is") == 0 || fact.target.find("Has") == 0) {
      context.predicate_targets[fact.name].push_back(fact.target);
    }
  }
}

bool HasTargetWithSuffix(
    const std::unordered_map<std::string, std::vector<std::string>> &targets,
    const std::string &function, const std::string &suffix) {
  const auto it = targets.find(function);
  if (it == targets.end()) {
    return false;
  }

  return std::any_of(it->second.begin(), it->second.end(),
                     [&](const std::string &target) {
                       return target.size() >= suffix.size() &&
                              target.rfind(suffix) == target.size() -
                                                      suffix.size();
                     });
}

void AddGetterFindings(const IntentAnalysisContext &context,
                       dsl::CoherenceResult &result) {
  for (const auto &[caller, targets] : context.call_targets) {
    if (!HasTargetWithSuffix(context.call_targets, caller, "Get") ||
        HasTargetWithSuffix(context.call_targets, caller, "Set")) {
      continue;
    }

    if (HasTargetWithSuffix(context.predicate_targets, caller, "Is") ||
        HasTargetWithSuffix(context.predicate_targets, caller, "Has")) {
      continue;
    }

    dsl::Finding finding{};
    finding.term = caller;
    finding.conflict =
        "Getter without predicate suggests incomplete state validation.";
    const auto &evidence = context.call_evidence.find(CallKey(caller, "Get"));
    if (evidence != context.call_evidence.end()) {
      finding.examples.push_back(evidence->second.front());
    }
    finding.suggested_canonical_form = caller;
    finding.description = finding.conflict;
    result.findings.push_back(finding);
  }
}

void AddSetterFindings(const IntentAnalysisContext &context,
                       dsl::CoherenceResult &result) {
  for (const auto &[caller, targets] : context.call_targets) {
    if (!HasTargetWithSuffix(context.call_targets, caller, "Set") ||
        HasTargetWithSuffix(context.call_targets, caller, "Get")) {
      continue;
    }

    dsl::Finding finding{};
    finding.term = caller;
    finding.conflict =
        "Setter without getter suggests incomplete state encapsulation.";
    const auto &evidence = context.call_evidence.find(CallKey(caller, "Set"));
    if (evidence != context.call_evidence.end()) {
      finding.examples.push_back(evidence->second.front());
    }
    finding.suggested_canonical_form = caller;
    finding.description = finding.conflict;
    result.findings.push_back(finding);
  }
}

bool HasLifecycleClosure(
    const std::unordered_map<std::string, std::vector<std::string>> &targets,
    const std::string &suffix) {
  const auto closes = suffix + "Close";
  const auto finishes = suffix + "Finish";
  const auto destroys = suffix + "Destroy";
  const auto commits = suffix + "Commit";

  return std::any_of(targets.begin(), targets.end(),
                     [&](const auto &entry) {
                       return HasTargetWithSuffix(targets, entry.first, closes) ||
                              HasTargetWithSuffix(targets, entry.first,
                                                  finishes) ||
                              HasTargetWithSuffix(targets, entry.first,
                                                  destroys) ||
                              HasTargetWithSuffix(targets, entry.first,
                                                  commits);
                     });
}

void AddPredicateFindings(const IntentAnalysisContext &context,
                          dsl::CoherenceResult &result) {
  for (const auto &[caller, predicates] : context.predicate_targets) {
    if (HasTargetWithSuffix(context.call_targets, caller, "Get") ||
        HasTargetWithSuffix(context.call_targets, caller, "Set")) {
      continue;
    }

    dsl::Finding finding{};
    finding.term = caller;
    finding.conflict =
        "Predicate without getter/setter suggests unclear state ownership.";
    const auto &evidence =
        context.call_evidence.find(CallKey(caller, predicates.front()));
    if (evidence != context.call_evidence.end()) {
      finding.examples.push_back(evidence->second.front());
    }
    finding.suggested_canonical_form = caller;
    finding.description = finding.conflict;
    result.findings.push_back(finding);
  }
}

void AddLifecycleFindings(const IntentAnalysisContext &context,
                          dsl::CoherenceResult &result) {
  for (const auto &[caller, targets] : context.call_targets) {
    for (const auto &target : targets) {
      std::string suffix;
      if (target.find("Init") == 0) {
        suffix = target.substr(4);
      } else if (target.find("Open") == 0) {
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
