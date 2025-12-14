#include <dsl/default_components.h>

#include <clang-c/CXCompilationDatabase.h>
#include <clang-c/Index.h>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace dsl {
namespace {
struct CompileCommandEntry {
  std::filesystem::path file;
  std::filesystem::path directory;
  std::vector<std::string> args;
};

std::string Join(const std::vector<std::string> &values,
                 const std::string &separator) {
  std::ostringstream stream;
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (i > 0) {
      stream << separator;
    }
    stream << values[i];
  }
  return stream.str();
}

void DebugLog(const std::string &message) {
  if (std::getenv("DSL_DEBUG_INDEXER") == nullptr) {
    return;
  }
  std::cerr << message << std::endl;
}

std::string ToString(CXString value) {
  std::string text;
  if (const auto *cstr = clang_getCString(value); cstr != nullptr) {
    text = cstr;
  }
  clang_disposeString(value);
  return text;
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
         std::equal(parent.begin(), parent.end(), normalized_candidate.begin());
}

std::filesystem::path CanonicalPathOrEmpty(const std::string &path) {
  if (path.empty()) {
    return {};
  }
  return std::filesystem::weakly_canonical(path);
}

std::filesystem::path ChooseCompileCommandsPath(
    const std::filesystem::path &explicit_path,
    const std::filesystem::path &project_root,
    const std::filesystem::path &build_directory) {
  if (!explicit_path.empty()) {
    return std::filesystem::weakly_canonical(explicit_path);
  }

  if (!build_directory.empty()) {
    const auto candidate = build_directory / "compile_commands.json";
    if (std::filesystem::exists(candidate)) {
      return std::filesystem::weakly_canonical(candidate);
    }
  }

  return std::filesystem::weakly_canonical(project_root /
                                           "compile_commands.json");
}

std::optional<std::filesystem::path>
PathFromLocation(CXSourceLocation location) {
  CXFile file{};
  unsigned line = 0;
  unsigned column = 0;
  unsigned offset = 0;
  clang_getFileLocation(location, &file, &line, &column, &offset);
  if (file == nullptr) {
    return std::nullopt;
  }
  const auto path = ToString(clang_getFileName(file));
  if (path.empty()) {
    return std::nullopt;
  }
  return std::filesystem::weakly_canonical(path);
}

std::string FormatRange(CXSourceRange range) {
  const auto start = clang_getRangeStart(range);
  const auto end = clang_getRangeEnd(range);

  CXFile file{};
  unsigned start_line = 0;
  unsigned start_column = 0;
  clang_getSpellingLocation(start, &file, &start_line, &start_column, nullptr);

  unsigned end_line = 0;
  unsigned end_column = 0;
  clang_getSpellingLocation(end, nullptr, &end_line, &end_column, nullptr);

  const auto file_path = ToString(clang_getFileName(file));
  if (file_path.empty()) {
    return {};
  }

  return file_path + ":" + std::to_string(start_line) + ":" +
         std::to_string(start_column) + "-" + std::to_string(end_line) + ":" +
         std::to_string(end_column);
}

std::string GetTypeName(CXType type) {
  return ToString(clang_getTypeSpelling(type));
}

std::string QualifiedName(CXCursor cursor) {
  if (clang_Cursor_isNull(cursor)) {
    return {};
  }

  const auto parent = clang_getCursorSemanticParent(cursor);
  if (clang_Cursor_isNull(parent) ||
      clang_getCursorKind(parent) == CXCursor_TranslationUnit) {
    return ToString(clang_getCursorSpelling(cursor));
  }

  const auto parent_name = QualifiedName(parent);
  if (parent_name.empty()) {
    return ToString(clang_getCursorSpelling(cursor));
  }

  const auto name = ToString(clang_getCursorSpelling(cursor));
  if (name.empty()) {
    return parent_name;
  }
  return parent_name + "::" + name;
}

std::string SignatureForCursor(CXCursor cursor) {
  const auto kind = clang_getCursorKind(cursor);
  if (kind == CXCursor_FunctionDecl || kind == CXCursor_CXXMethod ||
      kind == CXCursor_Constructor || kind == CXCursor_FunctionTemplate) {
    const auto return_type = GetTypeName(clang_getCursorResultType(cursor));
    const auto display = ToString(clang_getCursorDisplayName(cursor));
    if (return_type.empty()) {
      return display;
    }
    if (display.empty()) {
      return return_type;
    }
    return return_type + " " + display;
  }

  if (kind == CXCursor_FieldDecl || kind == CXCursor_VarDecl) {
    const auto name = ToString(clang_getCursorSpelling(cursor));
    const auto type = GetTypeName(clang_getCursorType(cursor));
    if (name.empty()) {
      return type;
    }
    if (type.empty()) {
      return name;
    }
    return name + ": " + type;
  }

  return ToString(clang_getCursorDisplayName(cursor));
}

class FactCollector {
public:
  explicit FactCollector(const std::filesystem::path &project_root)
      : project_root_(std::filesystem::weakly_canonical(project_root)) {}

  std::vector<AstFact> Collect(CXCursor root) {
    Traverse(root);
    return facts_;
  }

private:
  struct EntityScope {
    explicit EntityScope(std::vector<std::string> &stack)
        : stack_(&stack), active_(true) {}

    EntityScope(const EntityScope &) = delete;
    EntityScope &operator=(const EntityScope &) = delete;

    EntityScope(EntityScope &&other) noexcept
        : stack_(other.stack_), active_(other.active_) {
      other.stack_ = nullptr;
      other.active_ = false;
    }

    EntityScope &operator=(EntityScope &&other) noexcept {
      if (this == &other) {
        return *this;
      }
      Release();
      stack_ = other.stack_;
      active_ = other.active_;
      other.stack_ = nullptr;
      other.active_ = false;
      return *this;
    }

    ~EntityScope() { Release(); }

  private:
    void Release() {
      if (active_ && stack_ != nullptr) {
        stack_->pop_back();
        active_ = false;
      }
    }

    std::vector<std::string> *stack_;
    bool active_;
  };

  bool IsProjectCursor(CXCursor cursor) const {
    const auto location = clang_getCursorLocation(cursor);
    const auto path = PathFromLocation(location);
    return path.has_value() && IsWithin(*path, project_root_);
  }

  std::optional<std::string> CurrentEntity() const {
    if (entity_stack_.empty()) {
      return std::nullopt;
    }
    return entity_stack_.back();
  }

  bool IsEntityDefinition(CXCursorKind kind) const {
    return kind == CXCursor_FunctionDecl || kind == CXCursor_CXXMethod ||
           kind == CXCursor_Constructor || kind == CXCursor_FunctionTemplate ||
           kind == CXCursor_StructDecl || kind == CXCursor_ClassDecl ||
           kind == CXCursor_EnumDecl;
  }

  std::optional<EntityScope> EnterEntity(CXCursor cursor, CXCursorKind kind) {
    if (!IsEntityDefinition(kind)) {
      return std::nullopt;
    }
    if (!clang_isCursorDefinition(cursor)) {
      return std::nullopt;
    }

    const auto name = QualifiedName(cursor);
    if (name.empty()) {
      return std::nullopt;
    }

    entity_stack_.push_back(name);
    return EntityScope(entity_stack_);
  }

  void AddFact(AstFact fact) { facts_.push_back(std::move(fact)); }

  void AddSymbolFact(CXCursor cursor, const std::string &kind) {
    if (!clang_isCursorDefinition(cursor)) {
      return;
    }

    AstFact fact;
    fact.name = QualifiedName(cursor);
    fact.kind = kind;
    fact.signature = SignatureForCursor(cursor);
    fact.descriptor = fact.signature;
    fact.source_location = FormatRange(clang_getCursorExtent(cursor));
    fact.range = fact.source_location;
    AddFact(std::move(fact));
  }

  void AddOwnershipFact(CXCursor cursor) {
    const auto owner = CurrentEntity();
    if (!owner.has_value()) {
      return;
    }

    AstFact fact;
    fact.name = *owner;
    fact.kind = "owns";
    fact.target = GetTypeName(clang_getCursorType(cursor));
    fact.descriptor = ToString(clang_getCursorSpelling(cursor));
    fact.signature = fact.descriptor + ": " + fact.target;
    fact.source_location = FormatRange(clang_getCursorExtent(cursor));
    fact.range = fact.source_location;
    AddFact(std::move(fact));
  }

  void AddCallFact(CXCursor cursor) {
    const auto caller = CurrentEntity();
    if (!caller.has_value()) {
      return;
    }

    const auto referenced = clang_getCursorReferenced(cursor);
    auto target_name = QualifiedName(referenced);
    if (target_name.empty()) {
      target_name = ToString(clang_getCursorDisplayName(cursor));
    }
    if (target_name.empty()) {
      return;
    }

    AstFact fact;
    fact.name = *caller;
    fact.kind = "call";
    fact.target = target_name;
    fact.signature = SignatureForCursor(referenced);
    fact.descriptor = "calls " + target_name;
    fact.source_location = FormatRange(clang_getCursorExtent(cursor));
    fact.range = fact.source_location;
    AddFact(std::move(fact));
  }

  void AddTypeUsageFact(CXCursor cursor) {
    const auto subject = CurrentEntity();
    if (!subject.has_value()) {
      return;
    }

    const auto type_name = GetTypeName(clang_getCursorType(cursor));
    if (type_name.empty()) {
      return;
    }

    AstFact fact;
    fact.name = *subject;
    fact.kind = "type_usage";
    fact.target = type_name;
    fact.descriptor = "uses " + type_name;
    fact.signature = fact.descriptor;
    fact.source_location = FormatRange(clang_getCursorExtent(cursor));
    fact.range = fact.source_location;
    AddFact(std::move(fact));
  }

  void Traverse(CXCursor cursor) {
    const auto kind = clang_getCursorKind(cursor);
    const bool in_project = IsProjectCursor(cursor);
    std::optional<EntityScope> scope;
    if (in_project) {
      scope = EnterEntity(cursor, kind);
      switch (kind) {
      case CXCursor_FunctionDecl:
      case CXCursor_CXXMethod:
      case CXCursor_Constructor:
      case CXCursor_FunctionTemplate:
        AddSymbolFact(cursor, "function");
        break;
      case CXCursor_StructDecl:
      case CXCursor_ClassDecl:
      case CXCursor_EnumDecl:
        AddSymbolFact(cursor, "type");
        break;
      case CXCursor_VarDecl:
        AddSymbolFact(cursor, "variable");
        break;
      case CXCursor_FieldDecl:
        AddOwnershipFact(cursor);
        break;
      case CXCursor_CallExpr:
        AddCallFact(cursor);
        break;
      case CXCursor_TypeRef:
        AddTypeUsageFact(cursor);
        break;
      default:
        break;
      }
    }

    clang_visitChildren(
        cursor,
        [](CXCursor child, CXCursor, CXClientData data) {
          auto *collector = static_cast<FactCollector *>(data);
          collector->Traverse(child);
          return CXChildVisit_Continue;
        },
        this);
  }

  std::filesystem::path project_root_;
  std::vector<AstFact> facts_;
  std::vector<std::string> entity_stack_;
};

std::vector<std::string> ExtractArgs(CXCompileCommand command) {
  std::vector<std::string> args;
  const unsigned count = clang_CompileCommand_getNumArgs(command);
  args.reserve(count);
  for (unsigned index = 0; index < count; ++index) {
    args.push_back(ToString(clang_CompileCommand_getArg(command, index)));
  }
  return args;
}

bool ContainsArg(const std::vector<std::string> &args,
                 const std::string &needle) {
  return std::find(args.begin(), args.end(), needle) != args.end();
}

bool ContainsStandardFlag(const std::vector<std::string> &args) {
  return std::any_of(args.begin(), args.end(), [](const std::string &arg) {
    return arg.rfind("-std=", 0) == 0;
  });
}

bool LooksLikeCompiler(const std::string &arg) {
  return arg.find("clang++") != std::string::npos ||
         arg.find("clang") != std::string::npos;
}

std::vector<std::string> TokenizeCommand(const std::string &command) {
  std::istringstream stream(command);
  std::vector<std::string> tokens;
  std::string token;
  while (stream >> token) {
    tokens.push_back(token);
  }
  return tokens;
}

std::vector<std::string> NormalizeArgs(const CompileCommandEntry &entry) {
  std::vector<std::string> args;
  args.reserve(entry.args.size() + 2);
  for (std::size_t i = 0; i < entry.args.size(); ++i) {
    const auto &arg = entry.args[i];
    if (i == 0 && LooksLikeCompiler(arg)) {
      continue;
    }
    if (arg == entry.file.string()) {
      continue;
    }
    if (arg == "-c") {
      continue;
    }
    if (arg == "-o" && i + 1 < entry.args.size()) {
      ++i;
      continue;
    }
    args.push_back(arg);
  }

  if (!ContainsStandardFlag(args)) {
    args.push_back("-std=c++17");
  }

  return args;
}

std::vector<AstFact> CollectFacts(CXTranslationUnit translation_unit,
                                  const std::filesystem::path &project_root) {
  FactCollector collector(project_root);
  const auto root = clang_getTranslationUnitCursor(translation_unit);
  return collector.Collect(root);
}

std::vector<AstFact> ExtractFactsFromCommand(
    CXIndex index, const CompileCommandEntry &entry,
    const std::filesystem::path &project_root) {
  const auto args = NormalizeArgs(entry);
  std::vector<const char *> arg_pointers;
  arg_pointers.reserve(args.size());
  for (const auto &arg : args) {
    arg_pointers.push_back(arg.c_str());
  }

  CXTranslationUnit translation_unit = nullptr;
  const auto error = clang_parseTranslationUnit2(
      index, entry.file.string().c_str(), arg_pointers.data(),
      static_cast<int>(arg_pointers.size()), nullptr, 0, CXTranslationUnit_None,
      &translation_unit);
  DebugLog("parsing " + entry.file.string() + " with args [" +
           Join(args, ", ") + "] result code " + std::to_string(error));

  if (error != CXError_Success || translation_unit == nullptr) {
    const char *fallback_args[] = {"-std=c++17", entry.file.string().c_str()};
    const auto fallback_error = clang_parseTranslationUnit2(
        index, entry.file.string().c_str(), fallback_args, 2, nullptr, 0,
        CXTranslationUnit_None, &translation_unit);
    DebugLog("fallback parsing " + entry.file.string() + " result code " +
             std::to_string(fallback_error));
    if (fallback_error != CXError_Success || translation_unit == nullptr) {
      return {};
    }
  }

  auto facts = CollectFacts(translation_unit, project_root);
  DebugLog("collected " + std::to_string(facts.size()) + " facts from " +
           entry.file.string());
  for (const auto &fact : facts) {
    DebugLog("fact => name:" + fact.name + " kind:" + fact.kind +
             " target:" + fact.target + " signature:" + fact.signature +
             " location:" + fact.source_location);
  }
  clang_disposeTranslationUnit(translation_unit);
  return facts;
}

std::filesystem::path CanonicalTranslationUnitPath(
    const std::string &file, const std::string &directory,
    const std::filesystem::path &project_root) {
  auto path = std::filesystem::path(file);
  if (path.is_relative()) {
    if (!directory.empty()) {
      path = std::filesystem::path(directory) / path;
    } else {
      path = project_root / path;
    }
  }
  return std::filesystem::weakly_canonical(path);
}

std::vector<CompileCommandEntry> LoadCompileCommandsFromJson(
    const std::filesystem::path &compile_commands_path,
    const std::filesystem::path &project_root) {
  std::ifstream stream(compile_commands_path);
  if (!stream.is_open()) {
    return {};
  }
  const std::string content((std::istreambuf_iterator<char>(stream)),
                            std::istreambuf_iterator<char>());

  const std::regex object_regex("\\{[^\\}]*\\}");
  const std::regex file_regex("\\\"file\\\"\\s*:\\s*\\\"([^\\\"]+)\\\"");
  const std::regex directory_regex("\\\"directory\\\"\\s*:\\s*\\\"([^\\\"]+)\\\"");
  const std::regex command_regex("\\\"command\\\"\\s*:\\s*\\\"([^\\\"]+)\\\"");

  std::unordered_set<std::string> seen_paths;
  std::vector<CompileCommandEntry> entries;

  for (std::sregex_iterator object(content.begin(), content.end(), object_regex),
       end;
       object != end; ++object) {
    const auto object_text = object->str();
    std::smatch file_match;
    if (!std::regex_search(object_text, file_match, file_regex)) {
      continue;
    }

    std::smatch directory_match;
    const auto directory =
        std::regex_search(object_text, directory_match, directory_regex)
            ? directory_match[1].str()
            : std::string{};
    const auto path = CanonicalTranslationUnitPath(file_match[1], directory,
                                                   project_root);

    if (path.empty() || seen_paths.count(path.string()) > 0 ||
        !std::filesystem::exists(path) ||
        !std::filesystem::is_regular_file(path) ||
        !IsWithin(path, project_root)) {
      continue;
    }

    std::smatch command_match;
    const auto args =
        std::regex_search(object_text, command_match, command_regex)
            ? TokenizeCommand(command_match[1])
            : std::vector<std::string>{path.string()};

    CompileCommandEntry entry;
    entry.file = path;
    if (!directory.empty()) {
      entry.directory = std::filesystem::weakly_canonical(directory);
    }
    entry.args = args;

    seen_paths.insert(path.string());
    entries.push_back(std::move(entry));
  }

  return entries;
}

std::vector<CompileCommandEntry> LoadCompileCommands(
    const std::filesystem::path &compile_commands_path,
    const std::filesystem::path &project_root) {
  CXCompilationDatabase_Error error = CXCompilationDatabase_NoError;
  const auto parent = compile_commands_path.parent_path();
  CXCompilationDatabase database = clang_CompilationDatabase_fromDirectory(
      parent.string().c_str(), &error);

  if (error != CXCompilationDatabase_NoError || database == nullptr) {
    return LoadCompileCommandsFromJson(compile_commands_path, project_root);
  }

  CXCompileCommands commands =
      clang_CompilationDatabase_getAllCompileCommands(database);
  const unsigned size = clang_CompileCommands_getSize(commands);

  std::unordered_set<std::string> seen_paths;
  std::vector<CompileCommandEntry> entries;
  entries.reserve(size);

  for (unsigned index = 0; index < size; ++index) {
    CXCompileCommand command = clang_CompileCommands_getCommand(commands, index);
    const auto file = ToString(clang_CompileCommand_getFilename(command));
    const auto directory = ToString(clang_CompileCommand_getDirectory(command));
    auto translation_unit_path =
        CanonicalTranslationUnitPath(file, directory, project_root);

    if (translation_unit_path.empty() ||
        seen_paths.count(translation_unit_path.string()) > 0) {
      continue;
    }
    if (!std::filesystem::exists(translation_unit_path) ||
        !std::filesystem::is_regular_file(translation_unit_path)) {
      continue;
    }
    if (!IsWithin(translation_unit_path, project_root)) {
      continue;
    }

    seen_paths.insert(translation_unit_path.string());
    entries.push_back({translation_unit_path,
                       std::filesystem::weakly_canonical(directory),
                       ExtractArgs(command)});
  }

  clang_CompileCommands_dispose(commands);
  clang_CompilationDatabase_dispose(database);

  if (entries.empty()) {
    return LoadCompileCommandsFromJson(compile_commands_path, project_root);
  }
  return entries;
}

std::vector<CompileCommandEntry> BuildFallbackCommands(
    const SourceAcquisitionResult &sources,
    const std::filesystem::path &project_root,
    const std::filesystem::path &build_directory) {
  std::vector<CompileCommandEntry> entries;
  for (const auto &file : sources.files) {
    std::filesystem::path path(file);
    if (path.is_relative()) {
      path = project_root / path;
    }
    path = std::filesystem::weakly_canonical(path);
    if (!std::filesystem::exists(path) ||
        !std::filesystem::is_regular_file(path) ||
        !IsWithin(path, project_root) ||
        (!build_directory.empty() && IsWithin(path, build_directory))) {
      continue;
    }

    CompileCommandEntry entry;
    entry.file = path;
    entry.directory = path.parent_path();
    entry.args = {path.string()};
    entries.push_back(std::move(entry));
  }
  return entries;
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

  auto compile_commands =
      LoadCompileCommands(compile_commands_path, project_root);
  if (compile_commands.empty()) {
    compile_commands =
        BuildFallbackCommands(sources, project_root, build_directory);
  }
  DebugLog("compile commands entries: " +
           std::to_string(compile_commands.size()));

  AstIndex index;
  std::unordered_set<std::string> seen_facts;
  CXIndex clang_index = clang_createIndex(0, 1);

  for (const auto &entry : compile_commands) {
    if (!build_directory.empty() && IsWithin(entry.file, build_directory)) {
      continue;
    }

    DebugLog("command args for " + entry.file.string() + " -> [" +
             Join(entry.args, ", ") + "]");

    for (auto &fact : ExtractFactsFromCommand(clang_index, entry, project_root)) {
      const auto fingerprint = fact.name + "|" + fact.kind + "|" +
                               fact.target + "|" + fact.source_location;
      if (seen_facts.insert(fingerprint).second) {
        index.facts.push_back(std::move(fact));
      }
    }
  }

  clang_disposeIndex(clang_index);
  return index;
}

} // namespace dsl
