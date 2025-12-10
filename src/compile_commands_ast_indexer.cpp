#include <dsl/default_components.h>

#include <filesystem>
#include <fstream>
#include <iterator>
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

std::vector<CompileCommandEntry>
ParseCompileCommands(const std::string &content) {
  std::vector<CompileCommandEntry> entries;
  const std::regex object_regex("\\{[^\\}]*\\}");
  const std::regex file_regex(R"_("file"\s*:\s*"([^"]+)")_");
  const std::regex directory_regex(R"_("directory"\s*:\s*"([^"]+)")_");

  for (std::sregex_iterator it(content.begin(), content.end(), object_regex),
       end;
       it != end; ++it) {
    const auto object_text = it->str();
    std::smatch file_match;
    if (!std::regex_search(object_text, file_match, file_regex)) {
      continue;
    }

    CompileCommandEntry entry;
    entry.file = file_match[1];

    std::smatch directory_match;
    if (std::regex_search(object_text, directory_match, directory_regex)) {
      entry.directory = directory_match[1];
    }

    entries.push_back(entry);
  }

  return entries;
}

std::vector<std::filesystem::path>
LoadTranslationUnits(const std::filesystem::path &compile_commands_path,
                     const std::filesystem::path &project_root) {
  std::ifstream stream(compile_commands_path);
  if (!stream.is_open()) {
    throw std::runtime_error("Failed to open compile_commands.json at " +
                             compile_commands_path.string());
  }

  const std::string content((std::istreambuf_iterator<char>(stream)),
                            std::istreambuf_iterator<char>());

  std::vector<std::filesystem::path> translation_units;
  std::unordered_set<std::string> seen;
  for (const auto &entry : ParseCompileCommands(content)) {
    auto file_path = std::filesystem::path(entry.file);
    if (file_path.is_relative()) {
      if (!entry.directory.empty()) {
        file_path = std::filesystem::path(entry.directory) / file_path;
      } else {
        file_path = project_root / file_path;
      }
    }

    file_path = std::filesystem::weakly_canonical(file_path);
    if (seen.count(file_path.string()) > 0) {
      continue;
    }

    if (!std::filesystem::exists(file_path) ||
        !std::filesystem::is_regular_file(file_path)) {
      continue;
    }

    if (!IsWithin(file_path, project_root)) {
      continue;
    }

    translation_units.push_back(file_path);
    seen.insert(file_path.string());
  }

  return translation_units;
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
  std::filesystem::path build_directory;
  if (!sources.build_directory.empty()) {
    build_directory =
        std::filesystem::weakly_canonical(sources.build_directory);
  }

  std::filesystem::path compile_commands_path = compile_commands_path_;
  if (!compile_commands_path.empty() && !compile_commands_path.is_absolute()) {
    compile_commands_path = project_root / compile_commands_path;
  }

  if (compile_commands_path.empty()) {
    compile_commands_path =
        (build_directory.empty() ? project_root : build_directory) /
        "compile_commands.json";
  }

  compile_commands_path =
      std::filesystem::weakly_canonical(compile_commands_path);
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

    for (auto &fact : ExtractFactsFromFile(unit)) {
      const auto fingerprint =
          fact.name + "|" + fact.kind + "|" + fact.source_location;
      if (seen_facts.insert(fingerprint).second) {
        index.facts.push_back(std::move(fact));
      }
    }
  }

  return index;
}

} // namespace dsl
