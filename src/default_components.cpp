#include <dsl/default_components.h>

#include <algorithm>
#include <filesystem>
#include <iterator>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace dsl {

namespace {
bool IsSourceExtension(const std::filesystem::path &path) {
  static const std::set<std::string> kExtensions = {
      ".c",  ".cc",  ".cxx", ".cpp", ".h",   ".hh",
      ".hpp", ".hxx", ".ixx"};
  return kExtensions.count(path.extension().string()) > 0;
}

bool IsWithin(const std::filesystem::path &candidate,
              const std::filesystem::path &potential_parent) {
  if (potential_parent.empty()) {
    return false;
  }

  const auto parent = std::filesystem::weakly_canonical(potential_parent);
  const auto normalized_candidate =
      std::filesystem::weakly_canonical(candidate);

  return std::distance(parent.begin(), parent.end()) <=
             std::distance(normalized_candidate.begin(),
                           normalized_candidate.end()) &&
         std::equal(parent.begin(), parent.end(),
                    normalized_candidate.begin());
}

std::filesystem::path ResolveRootPath(const AnalysisConfig &config) {
  if (config.root_path.empty()) {
    throw std::invalid_argument("AnalysisConfig.root_path must not be empty.");
  }

  const auto normalized_root =
      std::filesystem::weakly_canonical(config.root_path);

  if (!std::filesystem::exists(normalized_root) ||
      !std::filesystem::is_directory(normalized_root)) {
    throw std::runtime_error("Analysis root path is not a directory: " +
                             normalized_root.string());
  }

  return normalized_root;
}

void RequireCMakeProject(const std::filesystem::path &root) {
  const auto cmake_lists = root / "CMakeLists.txt";
  if (!std::filesystem::exists(cmake_lists)) {
    throw std::runtime_error("CMakeLists.txt not found in root: " +
                             root.string());
  }
}

std::filesystem::path ResolveBuildDirectory(const AnalysisConfig &config,
                                            const std::filesystem::path &root) {
  std::filesystem::path build_directory =
      config.build_directory.empty() ? root / "build"
                                     : std::filesystem::path(config.build_directory);

  if (!build_directory.is_absolute()) {
    build_directory = root / build_directory;
  }

  return std::filesystem::weakly_canonical(build_directory);
}

std::filesystem::path ResolveCompileCommands(
    const AnalysisConfig &config, const std::filesystem::path &root,
    const std::filesystem::path &build_dir) {
  const auto normalize = [&root](const std::filesystem::path &candidate) {
    if (candidate.is_absolute()) {
      return std::filesystem::weakly_canonical(candidate);
    }
    return std::filesystem::weakly_canonical(root / candidate);
  };

  if (!config.compile_commands_path.empty()) {
    const auto configured_path = normalize(config.compile_commands_path);
    if (!std::filesystem::exists(configured_path)) {
      throw std::runtime_error(
          "Configured compile_commands.json does not exist: " +
          configured_path.string());
    }
    return configured_path;
  }

  const auto root_candidate = normalize("compile_commands.json");
  if (std::filesystem::exists(root_candidate)) {
    return root_candidate;
  }

  const auto build_candidate = normalize(build_dir / "compile_commands.json");
  if (std::filesystem::exists(build_candidate)) {
    return build_candidate;
  }

  throw std::runtime_error(
      "compile_commands.json not found in root or build directory.");
}

std::vector<std::string> CollectSourceFiles(
    const std::filesystem::path &root, const std::filesystem::path &build_dir) {
  std::vector<std::string> files;

  for (std::filesystem::recursive_directory_iterator it(root), end; it != end;
       ++it) {
    const auto &entry = *it;
    const auto path = entry.path();

    if (entry.is_directory() && IsWithin(path, build_dir)) {
      it.disable_recursion_pending();
      continue;
    }

    if (!entry.is_regular_file()) {
      continue;
    }

    if (!IsSourceExtension(path)) {
      continue;
    }

    if (IsWithin(path, build_dir)) {
      continue;
    }

    files.push_back(std::filesystem::weakly_canonical(path).string());
  }

  std::sort(files.begin(), files.end());
  files.erase(std::unique(files.begin(), files.end()), files.end());
  return files;
}
} // namespace

SourceAcquisitionResult
BasicSourceAcquirer::Acquire(const AnalysisConfig &config) {
  const auto root = ResolveRootPath(config);
  RequireCMakeProject(root);
  const auto build_dir = ResolveBuildDirectory(config, root);
  const auto compile_commands = ResolveCompileCommands(config, root, build_dir);

  auto files = CollectSourceFiles(root, build_dir);
  if (files.empty()) {
    throw std::runtime_error("No source files found under root: " +
                             root.string());
  }

  SourceAcquisitionResult result;
  result.files = std::move(files);
  result.compile_commands_path = compile_commands.string();
  result.normalized_root = root.string();
  return result;
}

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
    result.terms.push_back(term);
  }

  for (std::size_t i = 1; i < result.terms.size(); ++i) {
    DslRelationship relationship;
    relationship.subject = result.terms[i - 1].name;
    relationship.verb = "precedes";
    relationship.object = result.terms[i].name;
    result.relationships.push_back(relationship);
  }

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
      finding.description =
          "Duplicate term name indicates incoherent DSL usage.";
      result.findings.push_back(finding);
    }
  }

  if (extraction.relationships.empty() && !extraction.terms.empty()) {
    Finding finding;
    finding.term = extraction.terms.front().name;
    finding.description = "No relationships detected; DSL may be incomplete.";
    result.findings.push_back(finding);
  }

  return result;
}

Report MarkdownReporter::Render(const DslExtractionResult &extraction,
                                const CoherenceResult &coherence,
                                const AnalysisConfig &config) {
  std::ostringstream output;
  output << "# DSL Extraction Report\n\n";
  output << "Source root: " << config.root_path << "\n\n";

  output << "## Terms\n";
  for (const auto &term : extraction.terms) {
    output << "- " << term.name << " (" << term.kind << "): " << term.definition
           << "\n";
  }
  output << "\n## Relationships\n";
  for (const auto &relationship : extraction.relationships) {
    output << "- " << relationship.subject << " " << relationship.verb << " "
           << relationship.object << "\n";
  }

  output << "\n## Findings\n";
  if (coherence.findings.empty()) {
    output << "- None\n";
  } else {
    for (const auto &finding : coherence.findings) {
      output << "- " << finding.term << ": " << finding.description << "\n";
    }
  }

  Report report;
  report.markdown = output.str();
  return report;
}

AnalyzerPipelineBuilder AnalyzerPipelineBuilder::WithDefaults() {
  AnalyzerPipelineBuilder builder;
  builder.WithSourceAcquirer(std::make_unique<BasicSourceAcquirer>());
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
    components_.source_acquirer = std::make_unique<BasicSourceAcquirer>();
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
