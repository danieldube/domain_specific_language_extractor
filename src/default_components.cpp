#include <dsl/default_components.h>
#include <dsl/heuristic_dsl_extractor.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <clang-c/Index.h>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <unordered_set>

#include <memory>
#include <sstream>
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
  finding.examples.emplace_back("Relationships missing for term");
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
      const auto target = CanonicalizeName(fact.target);
      context.call_targets[canonical_name].push_back(target);
      const auto key = CallKey(canonical_name, target);
      if (context.call_evidence.find(key) == context.call_evidence.end()) {
        context.call_evidence.emplace(key, FactEvidence(fact));
      }
    }
  }
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
  for (const auto &target : targets) {
    if (target == "close" + suffix || target == "teardown" + suffix) {
      return true;
    }
  }
  return false;
}

void AddLifecycleFindings(const IntentAnalysisContext &context,
                          dsl::CoherenceResult &result) {
  for (const auto &[caller, targets] : context.call_targets) {
    for (const auto &target : targets) {
      std::string suffix;
      if (StartsWithInsensitive(target, "open")) {
        suffix = target.substr(4);
      } else if (StartsWithInsensitive(target, "init")) {
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

std::shared_ptr<Logger> EnsureLogger(std::shared_ptr<Logger> logger) {
  if (!logger) {
    return std::make_shared<NullLogger>();
  }
  return logger;
}

std::filesystem::path ResolveCacheDirectory(const AstCacheOptions &options) {
  if (!options.directory.empty()) {
    return std::filesystem::weakly_canonical(options.directory);
  }
  return std::filesystem::weakly_canonical(std::filesystem::current_path() /
                                           ".dsl_cache");
}

std::string Escape(const std::string &value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (const auto character : value) {
    if (character == '\\' || character == '\t' || character == '\n') {
      escaped.push_back('\\');
      if (character == '\t') {
        escaped.push_back('t');
        continue;
      }
      if (character == '\n') {
        escaped.push_back('n');
        continue;
      }
    }
    escaped.push_back(character);
  }
  return escaped;
}

std::string Unescape(const std::string &value) {
  std::string unescaped;
  unescaped.reserve(value.size());
  for (std::size_t i = 0; i < value.size(); ++i) {
    if (value[i] == '\\' && i + 1 < value.size()) {
      const auto next = value[i + 1];
      if (next == 't') {
        unescaped.push_back('\t');
        ++i;
        continue;
      }
      if (next == 'n') {
        unescaped.push_back('\n');
        ++i;
        continue;
      }
      unescaped.push_back(next);
      ++i;
      continue;
    }
    unescaped.push_back(value[i]);
  }
  return unescaped;
}

std::vector<std::string> SplitEscaped(const std::string &line) {
  std::vector<std::string> fields;
  std::string current;
  for (std::size_t i = 0; i < line.size(); ++i) {
    if (line[i] == '\t') {
      fields.push_back(Unescape(current));
      current.clear();
      continue;
    }
    current.push_back(line[i]);
  }
  fields.push_back(Unescape(current));
  return fields;
}

class AstCache {
public:
  AstCache(AstCacheOptions options, std::shared_ptr<Logger> logger)
      : options_(std::move(options)), directory_(ResolveCacheDirectory(options_)),
        logger_(EnsureLogger(std::move(logger))) {}

  bool Load(const std::string &key, AstIndex &index) const {
    if (!options_.enabled) {
      return false;
    }
    const auto path = CachePath(key);
    if (!std::filesystem::exists(path)) {
      return false;
    }

    std::ifstream stream(path);
    if (!stream) {
      logger_->Log(LogLevel::kWarn, "Failed to open AST cache",
                   {{"path", path.string()}});
      return false;
    }

    std::string line;
    AstIndex cached;
    while (std::getline(stream, line)) {
      if (line.empty() || line[0] == '#') {
        continue;
      }
      auto fields = SplitEscaped(line);
      if (fields.size() != 7) {
        logger_->Log(LogLevel::kWarn, "Ignoring malformed cache line",
                     {{"path", path.string()}});
        continue;
      }
      cached.facts.push_back({fields[0], fields[1], fields[2], fields[3],
                              fields[4], fields[5], fields[6]});
    }

    index = std::move(cached);
    logger_->Log(LogLevel::kInfo, "Loaded AST facts from cache",
                 {{"path", path.string()},
                  {"fact_count", std::to_string(index.facts.size())}});
    return true;
  }

  void Store(const std::string &key, const AstIndex &index) const {
    if (!options_.enabled) {
      return;
    }
    const auto path = CachePath(key);
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::trunc);
    if (!stream) {
      logger_->Log(LogLevel::kWarn, "Failed to write AST cache",
                   {{"path", path.string()}});
      return;
    }

    stream << "# toolchain cache entry\n";
    for (const auto &fact : index.facts) {
      stream << Escape(fact.name) << '\t' << Escape(fact.kind) << '\t'
             << Escape(fact.source_location) << '\t' << Escape(fact.signature)
             << '\t' << Escape(fact.descriptor) << '\t'
             << Escape(fact.target) << '\t' << Escape(fact.range) << '\n';
    }
    logger_->Log(LogLevel::kInfo, "Persisted AST cache",
                 {{"path", path.string()},
                  {"fact_count", std::to_string(index.facts.size())}});
  }

  void Clean() const {
    if (std::filesystem::exists(directory_)) {
      std::filesystem::remove_all(directory_);
      logger_->Log(LogLevel::kInfo, "Cleared AST cache",
                   {{"directory", directory_.string()}});
    }
  }

private:
  std::filesystem::path CachePath(const std::string &key) const {
    return directory_ / ("ast_cache_" + key + ".dat");
  }

  AstCacheOptions options_;
  std::filesystem::path directory_;
  std::shared_ptr<Logger> logger_;
};

std::string ToolchainVersion() {
  const auto version = clang_getClangVersion();
  std::string text;
  if (const auto *cstr = clang_getCString(version); cstr != nullptr) {
    text = cstr;
  }
  clang_disposeString(version);
  return text;
}

std::string BuildCacheKey(const SourceAcquisitionResult &sources,
                          const std::string &toolchain_version) {
  std::string accumulator = toolchain_version;
  accumulator.append(sources.project_root);
  accumulator.append(sources.build_directory);
  for (const auto &file : sources.files) {
    accumulator.append(file);
  }
  return std::to_string(std::hash<std::string>{}(accumulator));
}

class CachingAstIndexer : public AstIndexer {
public:
  CachingAstIndexer(std::unique_ptr<AstIndexer> inner,
                    AstCacheOptions options, std::shared_ptr<Logger> logger)
      : inner_(std::move(inner)), options_(std::move(options)),
        cache_(options_, logger), logger_(EnsureLogger(std::move(logger))) {}

  AstIndex BuildIndex(const SourceAcquisitionResult &sources) override {
    if (options_.clean) {
      cache_.Clean();
    }
    if (!options_.enabled) {
      return inner_->BuildIndex(sources);
    }

    const auto version = ToolchainVersion();
    const auto key = BuildCacheKey(sources, version);
    AstIndex index;
    if (cache_.Load(key, index)) {
      logger_->Log(LogLevel::kInfo, "AST cache hit",
                   {{"key", key}, {"toolchain", version}});
      return index;
    }

    logger_->Log(LogLevel::kInfo, "AST cache miss",
                 {{"key", key}, {"toolchain", version}});
    index = inner_->BuildIndex(sources);
    cache_.Store(key, index);
    return index;
  }

private:
  std::unique_ptr<AstIndexer> inner_;
  AstCacheOptions options_;
  AstCache cache_;
  std::shared_ptr<Logger> logger_;
};

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
AnalyzerPipelineBuilder AnalyzerPipelineBuilder::WithDefaults() {
  AnalyzerPipelineBuilder builder;
  builder.WithLogger(std::make_shared<NullLogger>());
  builder.WithSourceAcquirer(std::make_unique<CMakeSourceAcquirer>(
      std::filesystem::path("build"), builder.components_.logger));
  builder.WithIndexer(std::make_unique<CompileCommandsAstIndexer>(
      std::filesystem::path{}, builder.components_.logger));
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

AnalyzerPipelineBuilder &
AnalyzerPipelineBuilder::WithLogger(std::shared_ptr<Logger> logger) {
  components_.logger = std::move(logger);
  return *this;
}

AnalyzerPipelineBuilder &
AnalyzerPipelineBuilder::WithAstCacheOptions(AstCacheOptions options) {
  components_.ast_cache = std::move(options);
  return *this;
}

DefaultAnalyzerPipeline AnalyzerPipelineBuilder::Build() {
  components_.logger = EnsureLogger(std::move(components_.logger));
  components_.source_acquirer =
      components_.source_acquirer
          ? std::move(components_.source_acquirer)
          : std::make_unique<CMakeSourceAcquirer>(std::filesystem::path("build"),
                                                  components_.logger);
  components_.indexer = components_.indexer
                            ? std::move(components_.indexer)
                            : std::make_unique<CompileCommandsAstIndexer>(
                                  std::filesystem::path{}, components_.logger);
  components_.extractor = EnsureComponent<DslExtractor, HeuristicDslExtractor>(
      std::move(components_.extractor));
  components_.analyzer =
      EnsureComponent<CoherenceAnalyzer, RuleBasedCoherenceAnalyzer>(
          std::move(components_.analyzer));
  components_.reporter = EnsureComponent<Reporter, MarkdownReporter>(
      std::move(components_.reporter));

  if (components_.ast_cache.enabled || components_.ast_cache.clean) {
    components_.indexer = std::make_unique<CachingAstIndexer>(
        std::move(components_.indexer), components_.ast_cache,
        components_.logger);
  }
  return DefaultAnalyzerPipeline(std::move(components_));
}

DefaultAnalyzerPipeline::DefaultAnalyzerPipeline(PipelineComponents components)
    : source_acquirer_(std::move(components.source_acquirer)),
      indexer_(std::move(components.indexer)),
      extractor_(std::move(components.extractor)),
      analyzer_(std::move(components.analyzer)),
      reporter_(std::move(components.reporter)),
      logger_(EnsureLogger(std::move(components.logger))),
      ast_cache_(std::move(components.ast_cache)) {}

PipelineResult DefaultAnalyzerPipeline::Run(const AnalysisConfig &config) {
  logger_->Log(LogLevel::kInfo, "pipeline.start",
               {{"root", config.root_path},
                {"formats", std::to_string(config.formats.size())}});

  const auto pipeline_start = std::chrono::steady_clock::now();
  const auto sources = source_acquirer_->Acquire(config);
  logger_->Log(LogLevel::kDebug, "pipeline.stage.complete",
               {{"stage", "source"},
                {"file_count", std::to_string(sources.files.size())}});

  const auto index = indexer_->BuildIndex(sources);
  logger_->Log(LogLevel::kDebug, "pipeline.stage.complete",
               {{"stage", "index"},
                {"facts", std::to_string(index.facts.size())}});

  const auto extraction = extractor_->Extract(index);
  logger_->Log(LogLevel::kDebug, "pipeline.stage.complete",
               {{"stage", "extract"},
                {"terms", std::to_string(extraction.terms.size())},
                {"relationships",
                 std::to_string(extraction.relationships.size())}});

  const auto coherence = analyzer_->Analyze(extraction);
  logger_->Log(LogLevel::kDebug, "pipeline.stage.complete",
               {{"stage", "analyze"},
                {"findings", std::to_string(coherence.findings.size())}});

  const auto report = reporter_->Render(extraction, coherence, config);

  const auto duration_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - pipeline_start)
          .count();
  logger_->Log(LogLevel::kInfo, "pipeline.complete",
               {{"duration_ms", std::to_string(duration_ms)},
                {"findings", std::to_string(coherence.findings.size())}});

  return PipelineResult{report, coherence, extraction};
}

} // namespace dsl
