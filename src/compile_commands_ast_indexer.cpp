#include <dsl/default_components.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <regex>
#include <set>
#include <stdexcept>
#include <unordered_set>

namespace dsl {
namespace {
struct CompileCommandEntry {
  std::string file;
  std::string directory;
};

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
         std::equal(parent.begin(), parent.end(), normalized_candidate.begin());
}

std::optional<CompileCommandEntry> ParseEntry(const std::string &object_text,
                                              const std::regex &file_regex,
                                              const std::regex &dir_regex) {
  std::smatch file_match;
  if (!std::regex_search(object_text, file_match, file_regex)) {
    return std::nullopt;
  }

  CompileCommandEntry entry;
  entry.file = file_match[1];

  std::smatch directory_match;
  if (std::regex_search(object_text, directory_match, dir_regex)) {
    entry.directory = directory_match[1];
  }

  return entry;
}

std::vector<CompileCommandEntry>
ParseCompileCommands(const std::string &content) {
  std::vector<CompileCommandEntry> entries;
  const std::regex object_regex(R"(\{[^\}]*\})");
  const std::regex file_regex(R"_("file"\s*:\s*"([^"]+)")_");
  const std::regex directory_regex(R"_("directory"\s*:\s*"([^"]+)")_");

  for (std::sregex_iterator it(content.begin(), content.end(), object_regex),
       end;
       it != end; ++it) {
    const auto parsed = ParseEntry(it->str(), file_regex, directory_regex);
    if (parsed) {
      entries.push_back(*parsed);
    }
  }

  return entries;
}

std::string ReadFileContents(const std::filesystem::path &path) {
  std::ifstream stream(path);
  if (!stream.is_open()) {
    throw std::runtime_error("Failed to open compile_commands.json at " +
                             path.string());
  }
  return {std::istreambuf_iterator<char>(stream),
          std::istreambuf_iterator<char>()};
}

class TranslationUnitCollector {
public:
  explicit TranslationUnitCollector(const std::filesystem::path &project_root)
      : project_root_(std::filesystem::weakly_canonical(project_root)) {}

  void Add(const CompileCommandEntry &entry) {
    const auto path = CanonicalPath(entry);
    if (path.empty() || seen_.count(path.string()) > 0) {
      return;
    }
    if (!std::filesystem::exists(path) ||
        !std::filesystem::is_regular_file(path) ||
        !IsWithin(path, project_root_)) {
      return;
    }
    translation_units_.push_back(path);
    seen_.insert(path.string());
  }

  std::vector<std::filesystem::path> Release() { return translation_units_; }

private:
  std::filesystem::path CanonicalPath(const CompileCommandEntry &entry) const {
    auto path = std::filesystem::path(entry.file);
    if (path.is_relative() && !entry.directory.empty()) {
      path = std::filesystem::path(entry.directory) / path;
    } else if (path.is_relative()) {
      path = project_root_ / path;
    }
    return std::filesystem::weakly_canonical(path);
  }

  std::filesystem::path project_root_;
  std::unordered_set<std::string> seen_;
  std::vector<std::filesystem::path> translation_units_;
};

std::filesystem::path CanonicalPathOrEmpty(const std::string &path) {
  if (path.empty()) {
    return {};
  }
  return std::filesystem::weakly_canonical(path);
}

std::filesystem::path
ChooseCompileCommandsPath(const std::filesystem::path &override_path,
                          const std::filesystem::path &project_root,
                          const std::filesystem::path &build_directory) {
  if (!override_path.empty()) {
    return std::filesystem::weakly_canonical(
        override_path.is_absolute() ? override_path
                                    : project_root / override_path);
  }

  const auto base = build_directory.empty() ? project_root : build_directory;
  return std::filesystem::weakly_canonical(base / "compile_commands.json");
}

std::vector<AstFact>
ExtractFactsFromFile(const std::filesystem::path &translation_unit_path);

void AppendFactsFromUnit(const std::filesystem::path &unit,
                         std::unordered_set<std::string> &seen_facts,
                         std::vector<AstFact> &facts) {
  for (auto &fact : ExtractFactsFromFile(unit)) {
    const auto fingerprint =
        fact.name + "|" + fact.kind + "|" + fact.source_location;
    if (seen_facts.insert(fingerprint).second) {
      facts.push_back(std::move(fact));
    }
  }
}

std::vector<std::filesystem::path>
LoadTranslationUnits(const std::filesystem::path &compile_commands_path,
                     const std::filesystem::path &project_root) {
  const auto content = ReadFileContents(compile_commands_path);
  TranslationUnitCollector collector(project_root);
  for (const auto &entry : ParseCompileCommands(content)) {
    collector.Add(entry);
  }
  return collector.Release();
}

void AddFunctionFacts(const std::string &line, std::size_t line_number,
                      const std::filesystem::path &file_path,
                      std::vector<AstFact> &facts) {
  const std::regex function_regex(
      R"_(([A-Za-z_][A-Za-z0-9_:<>]*)\s+([A-Za-z_][A-Za-z0-9_]*)\s*\([^;]*\)\s*\{)_");
  std::smatch match;
  if (std::regex_search(line, match, function_regex)) {
    AstFact fact;
    fact.name = match[2];
    fact.kind = "function";
    fact.source_location = file_path.string() + ":" +
                           std::to_string(static_cast<int>(line_number));
    facts.push_back(fact);
  }
}

void AddTypeFacts(const std::string &line, std::size_t line_number,
                  const std::filesystem::path &file_path,
                  std::vector<AstFact> &facts) {
  const std::regex type_regex(
      R"_((class|struct|enum)\s+([A-Za-z_][A-Za-z0-9_]*))_");
  for (std::sregex_iterator it(line.begin(), line.end(), type_regex), end;
       it != end; ++it) {
    const auto &match = *it;
    AstFact fact;
    fact.name = match[2];
    fact.kind = "type";
    fact.source_location = file_path.string() + ":" +
                           std::to_string(static_cast<int>(line_number));
    facts.push_back(fact);
  }
}

void AddVariableFacts(const std::string &line, std::size_t line_number,
                      const std::filesystem::path &file_path,
                      std::vector<AstFact> &facts) {
  if (line.find('(') != std::string::npos) {
    return;
  }

  if (line.find('{') != std::string::npos ||
      line.find('}') != std::string::npos) {
    return;
  }

  const std::regex variable_regex(
      R"_(([A-Za-z_][A-Za-z0-9_:<>]*)\s+([A-Za-z_][A-Za-z0-9_]*)\s*(=|;))_");
  std::smatch match;
  if (std::regex_search(line, match, variable_regex)) {
    AstFact fact;
    fact.name = match[2];
    fact.kind = "variable";
    fact.source_location = file_path.string() + ":" +
                           std::to_string(static_cast<int>(line_number));
    facts.push_back(fact);
  }
}

std::vector<AstFact>
ExtractFactsFromFile(const std::filesystem::path &translation_unit_path) {
  std::ifstream source(translation_unit_path);
  if (!source.is_open()) {
    return {};
  }

  std::vector<AstFact> facts;
  std::string line;
  std::size_t line_number = 0;

  while (std::getline(source, line)) {
    ++line_number;
    AddFunctionFacts(line, line_number, translation_unit_path, facts);
    AddTypeFacts(line, line_number, translation_unit_path, facts);
    AddVariableFacts(line, line_number, translation_unit_path, facts);
  }

  return facts;
}
} // namespace

CompileCommandsAstIndexer::CompileCommandsAstIndexer(
    std::filesystem::path compile_commands_path)
    : compile_commands_path_(std::move(compile_commands_path)) {}

AstIndex
CompileCommandsAstIndexer::BuildIndex(const SourceAcquisitionResult &sources) {
  if (sources.project_root.empty()) {
    throw std::invalid_argument(
        "SourceAcquisitionResult.project_root is empty");
  }

  const auto project_root =
      std::filesystem::weakly_canonical(sources.project_root);
  const auto build_directory = CanonicalPathOrEmpty(sources.build_directory);
  const auto compile_commands_path = ChooseCompileCommandsPath(
      compile_commands_path_, project_root, build_directory);
  if (!std::filesystem::exists(compile_commands_path)) {
    throw std::runtime_error("compile_commands.json not found at " +
                             compile_commands_path.string());
  }

  const auto translation_units =
      LoadTranslationUnits(compile_commands_path, project_root);

  AstIndex index;
  std::unordered_set<std::string> seen_facts;
  for (const auto &unit : translation_units) {
    if (!build_directory.empty() && IsWithin(unit, build_directory)) {
      continue;
    }
    AppendFactsFromUnit(unit, seen_facts, index.facts);
  }

  return index;
}

} // namespace dsl
