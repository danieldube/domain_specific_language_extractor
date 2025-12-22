#include <dsl/analyzer_pipeline_builder.h>
#include <dsl/cli_exit_codes.h>
#include <dsl/cmake_source_acquirer.h>
#include <dsl/compile_commands_ast_indexer.h>
#include <dsl/default_analyzer_pipeline.h>
#include <dsl/dsl_analyzer.h>
#include <dsl/heuristic_dsl_extractor.h>
#include <dsl/markdown_reporter.h>
#include <dsl/models.h>
#include <dsl/rule_based_coherence_analyzer.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include <yaml-cpp/yaml.h>

namespace {

using dsl::AnalyzeOptions;

void PrintAnalyzeUsage() {
  std::cout
      << "Usage: dsl-extract analyze --root <path> [options]\n"
      << "Options:\n"
      << "  --root <path>         Root directory of the CMake project\n"
      << "  --build <path>        Build directory containing "
         "compile_commands.json\n"
      << "                        (default: build)\n"
      << "  --format <list>       Comma-separated list of output formats\n"
      << "                        (supported: markdown,json)\n"
      << "  --out <path>          Directory for report outputs (default: "
         "analysis root)\n"
      << "  --scope-notes <text>  Scope notes to embed in the report header\n"
      << "  --config <file>       Optional YAML config file\n"
      << "  --ignored-namespaces <list>  Comma-separated namespaces to ignore\n"
      << "                        when analyzing symbols (default: "
         "std,testing,\n"
      << "                        gtest)\n"
      << "  --ignored-paths <list> Comma-separated paths relative to --root\n"
      << "                        to ignore during analysis\n"
      << "  --log-level <level>   Logging verbosity (error,warn,info,debug)\n"
      << "  --verbose             Shortcut for --log-level info\n"
      << "  --debug               Shortcut for --log-level debug\n"
      << "  --extractor <name>    DSL extractor plug-in to use\n"
      << "  --analyzer <name>     Coherence analyzer plug-in to use\n"
      << "  --reporter <name>     Reporter plug-in to render outputs\n"
      << "  --cache-ast           Enable AST caching\n"
      << "  --cache-dir <path>    Override AST cache directory\n"
      << "  --clean-cache         Remove AST cache before running\n"
      << "  --help                Show this message\n";
}

void PrintReportUsage() {
  std::cout << "Usage: dsl-extract report --root <path> [options]\n"
            << "Options:\n"
            << "  --root <path>   Directory containing cached reports\n"
            << "  --out <path>    Directory for regenerated reports\n"
            << "                  (default: reuse --root)\n"
            << "  --format <list> Comma-separated list of output formats\n"
            << "                  (supported: markdown,json)\n"
            << "  --help          Show this message\n";
}

std::string Trim(std::string value) {
  const auto is_space = [](unsigned char ch) { return std::isspace(ch) != 0; };
  value.erase(value.begin(),
              std::find_if(value.begin(), value.end(),
                           [&](unsigned char ch) { return !is_space(ch); }));
  value.erase(std::find_if(value.rbegin(), value.rend(),
                           [&](unsigned char ch) { return !is_space(ch); })
                  .base(),
              value.end());
  return value;
}

std::string ToLower(std::string value) {
  std::transform(
      value.begin(), value.end(), value.begin(),
      [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

bool ParseBool(const std::string &value) {
  const auto normalized = ToLower(Trim(value));
  return normalized == "true" || normalized == "1" || normalized == "yes" ||
         normalized == "on";
}

dsl::LogLevel ParseLogLevel(const std::string &value) {
  const auto normalized = ToLower(Trim(value));
  if (normalized == "error") {
    return dsl::LogLevel::kError;
  }
  if (normalized == "warn" || normalized == "warning") {
    return dsl::LogLevel::kWarn;
  }
  if (normalized == "info") {
    return dsl::LogLevel::kInfo;
  }
  if (normalized == "debug") {
    return dsl::LogLevel::kDebug;
  }
  throw std::invalid_argument("Unknown log level: " + value);
}

std::vector<std::string> SplitFormats(const std::string &raw_formats) {
  std::vector<std::string> values;
  std::string current;
  for (const auto character : raw_formats) {
    if (character == ',') {
      if (!current.empty()) {
        values.push_back(current);
        current.clear();
      }
    } else {
      current.push_back(static_cast<char>(std::tolower(character)));
    }
  }
  if (!current.empty()) {
    values.push_back(current);
  }
  return values;
}

void AppendValues(const std::string &raw_values,
                  std::vector<std::string> &target) {
  for (auto value : SplitFormats(raw_values)) {
    value = Trim(value);
    if (value.empty()) {
      continue;
    }
    if (std::find(target.begin(), target.end(), value) == target.end()) {
      target.push_back(std::move(value));
    }
  }
}

void AppendFormats(const std::string &raw_formats,
                   std::vector<std::string> &target) {
  for (auto format : SplitFormats(raw_formats)) {
    format = Trim(format);
    if (format != "markdown" && format != "json") {
      throw std::invalid_argument("Unsupported format: " + format);
    }
    if (std::find(target.begin(), target.end(), format) == target.end()) {
      target.push_back(std::move(format));
    }
  }
}

void AppendIgnoredNamespaces(const std::string &raw_namespaces,
                             std::vector<std::string> &target) {
  AppendValues(raw_namespaces, target);
}

std::vector<std::string> SplitPathList(const std::string &raw_paths) {
  std::vector<std::string> values;
  std::string current;
  for (const auto character : raw_paths) {
    if (character == ',') {
      if (!current.empty()) {
        values.push_back(current);
        current.clear();
      }
    } else {
      current.push_back(character);
    }
  }
  if (!current.empty()) {
    values.push_back(current);
  }
  return values;
}

void AppendIgnoredPaths(const std::string &raw_paths,
                        std::vector<std::filesystem::path> &target) {
  for (auto path_value : SplitPathList(raw_paths)) {
    path_value = Trim(path_value);
    if (path_value.empty()) {
      continue;
    }

    const std::filesystem::path path(
        std::filesystem::path(path_value).generic_string());
    const auto matches =
        std::find_if(target.begin(), target.end(), [&](const auto &existing) {
          return existing.generic_string() == path.generic_string();
        });
    if (matches == target.end()) {
      target.push_back(path);
    }
  }
}

void AppendRawPathStrings(const std::string &raw_paths,
                          std::vector<std::string> &target) {
  for (auto path_value : SplitPathList(raw_paths)) {
    path_value = Trim(path_value);
    if (path_value.empty()) {
      continue;
    }

    const auto normalized = std::filesystem::path(path_value).generic_string();
    if (std::find(target.begin(), target.end(), normalized) == target.end()) {
      target.push_back(normalized);
    }
  }
}

std::string RequireValue(const std::vector<std::string> &arguments,
                         std::size_t &index, const std::string &flag) {
  if (++index >= arguments.size()) {
    throw std::invalid_argument(flag + " requires a value");
  }
  return arguments[index];
}

void HandleCacheOption(const std::vector<std::string> &arguments,
                       std::size_t &index, AnalyzeOptions &options) {
  const auto &argument = arguments[index];
  if (argument == "--cache-ast") {
    options.enable_ast_cache = true;
    return;
  }
  if (argument == "--clean-cache") {
    options.clean_cache = true;
    return;
  }
  if (argument == "--cache-dir") {
    options.cache_directory = RequireValue(arguments, index, "--cache-dir");
    return;
  }
}

void HandleLoggingOption(const std::vector<std::string> &arguments,
                         std::size_t &index, AnalyzeOptions &options) {
  const auto &argument = arguments[index];
  if (argument == "--log-level") {
    options.log_level =
        ParseLogLevel(RequireValue(arguments, index, std::string(argument)));
    return;
  }
  if (argument == "--verbose") {
    options.log_level = dsl::LogLevel::kInfo;
    return;
  }
  if (argument == "--debug") {
    options.log_level = dsl::LogLevel::kDebug;
    return;
  }
}

void HandleFormatOption(const std::vector<std::string> &arguments,
                        std::size_t &index, AnalyzeOptions &options) {
  const auto &argument = arguments[index];
  if (argument == "--format") {
    AppendFormats(RequireValue(arguments, index, "--format"), options.formats);
  }
}

void HandleIgnoredNamespacesOption(const std::vector<std::string> &arguments,
                                   std::size_t &index,
                                   AnalyzeOptions &options) {
  const auto &argument = arguments[index];
  if (argument == "--ignored-namespaces") {
    AppendIgnoredNamespaces(
        RequireValue(arguments, index, "--ignored-namespaces"),
        options.ignored_namespaces);
  }
}

void HandleIgnoredPathsOption(const std::vector<std::string> &arguments,
                              std::size_t &index, AnalyzeOptions &options) {
  const auto &argument = arguments[index];
  if (argument == "--ignored-paths") {
    AppendIgnoredPaths(RequireValue(arguments, index, "--ignored-paths"),
                       options.ignored_paths);
  }
}

void HandlePluginSelection(const std::vector<std::string> &arguments,
                           std::size_t &index, AnalyzeOptions &options) {
  const auto &argument = arguments[index];
  if (argument == "--extractor") {
    options.extractor = RequireValue(arguments, index, argument);
    return;
  }
  if (argument == "--analyzer") {
    options.analyzer = RequireValue(arguments, index, argument);
    return;
  }
  if (argument == "--reporter") {
    options.reporter = RequireValue(arguments, index, argument);
    return;
  }
}

bool DispatchAnalyzeOption(const std::vector<std::string> &arguments,
                           std::size_t &index, AnalyzeOptions &options) {
  const auto &argument = arguments[index];
  if (argument == "--help" || argument == "-h") {
    options.show_help = true;
    return true;
  }
  if (argument == "--root") {
    options.root = RequireValue(arguments, index, "--root");
    return true;
  }
  if (argument == "--build") {
    options.build_directory = RequireValue(arguments, index, "--build");
    return true;
  }
  if (argument == "--out") {
    options.output_directory = RequireValue(arguments, index, "--out");
    return true;
  }
  if (argument == "--scope-notes") {
    options.scope_notes = RequireValue(arguments, index, "--scope-notes");
    return true;
  }
  if (argument == "--config") {
    options.config_file = RequireValue(arguments, index, "--config");
    return true;
  }

  HandleFormatOption(arguments, index, options);
  if (argument == "--format") {
    return true;
  }

  HandleIgnoredNamespacesOption(arguments, index, options);
  if (argument == "--ignored-namespaces") {
    return true;
  }

  HandleIgnoredPathsOption(arguments, index, options);
  if (argument == "--ignored-paths") {
    return true;
  }

  HandleLoggingOption(arguments, index, options);
  if (argument == "--log-level" || argument == "--verbose" ||
      argument == "--debug") {
    return true;
  }

  HandleCacheOption(arguments, index, options);
  if (argument == "--cache-ast" || argument == "--clean-cache" ||
      argument == "--cache-dir") {
    return true;
  }

  HandlePluginSelection(arguments, index, options);
  if (argument == "--extractor" || argument == "--analyzer" ||
      argument == "--reporter") {
    return true;
  }

  return false;
}

void ValidateAnalyzeOptions(const AnalyzeOptions &options) {
  if (!options.root) {
    throw std::invalid_argument("--root is required (or set in config file)");
  }
}

void WriteFileIfContent(const std::filesystem::path &path,
                        const std::string &content) {
  if (content.empty()) {
    return;
  }
  std::ofstream stream(path);
  if (!stream) {
    throw std::runtime_error("Failed to open output file: " + path.string());
  }
  stream << content;
}

void WriteReports(const std::filesystem::path &root,
                  const dsl::Report &report) {
  std::filesystem::create_directories(root);
  WriteFileIfContent(root / "dsl_report.md", report.markdown);
  WriteFileIfContent(root / "dsl_report.json", report.json);
}

std::string ReadFileContent(const std::filesystem::path &path) {
  std::ifstream stream(path);
  if (!stream) {
    throw std::runtime_error("Failed to open cached report: " + path.string());
  }
  return std::string(std::istreambuf_iterator<char>(stream),
                     std::istreambuf_iterator<char>());
}

std::vector<std::string>
DetectAvailableReportFormats(const std::filesystem::path &root) {
  std::vector<std::string> formats;
  if (std::filesystem::exists(root / "dsl_report.md")) {
    formats.emplace_back("markdown");
  }
  if (std::filesystem::exists(root / "dsl_report.json")) {
    formats.emplace_back("json");
  }
  return formats;
}

} // namespace

namespace dsl {

AnalyzeOptions
ParseAnalyzeArguments(const std::vector<std::string> &arguments) {
  AnalyzeOptions options;

  for (std::size_t i = 0; i < arguments.size(); ++i) {
    if (!DispatchAnalyzeOption(arguments, i, options)) {
      throw std::invalid_argument("Unknown argument: " + arguments[i]);
    }
    if (options.show_help) {
      break;
    }
  }

  return options;
}

ReportOptions ParseReportArguments(const std::vector<std::string> &arguments) {
  ReportOptions options;
  for (std::size_t i = 0; i < arguments.size(); ++i) {
    const auto &argument = arguments[i];
    if (argument == "--help" || argument == "-h") {
      options.show_help = true;
      return options;
    }
    if (argument == "--root") {
      options.root = RequireValue(arguments, i, "--root");
      continue;
    }
    if (argument == "--out") {
      options.output_directory = RequireValue(arguments, i, "--out");
      continue;
    }
    if (argument == "--format") {
      AppendFormats(RequireValue(arguments, i, "--format"), options.formats);
      continue;
    }
    throw std::invalid_argument("Unknown report argument: " + argument);
  }
  return options;
}

using ConfigValue = std::variant<std::string, bool, std::vector<std::string>>;
using RawConfig = std::unordered_map<std::string, ConfigValue>;

const std::vector<std::string> &SupportedConfigKeys() {
  static const std::vector<std::string> keys = {"root",
                                                "build",
                                                "out",
                                                "formats",
                                                "cache_ast",
                                                "cache_dir",
                                                "clean_cache",
                                                "log_level",
                                                "scope_notes",
                                                "extractor",
                                                "analyzer",
                                                "reporter",
                                                "ignored_namespaces",
                                                "ignored_paths"};
  return keys;
}

std::string NormalizeConfigKey(std::string key) {
  key = ToLower(Trim(key));
  std::replace(key.begin(), key.end(), '-', '_');
  static const std::unordered_map<std::string, std::string> aliases = {
      {"build_directory", "build"},
      {"output", "out"},
      {"output_directory", "out"},
      {"format", "formats"},
      {"cache_directory", "cache_dir"}};

  if (const auto alias = aliases.find(key); alias != aliases.end()) {
    return alias->second;
  }
  return key;
}

[[noreturn]] void ThrowUnknownKey(const std::string &key) {
  std::string message = "Unknown config key: " + key + ". Supported keys: ";
  const auto &supported = SupportedConfigKeys();
  for (std::size_t i = 0; i < supported.size(); ++i) {
    message += supported[i];
    if (i + 1 < supported.size()) {
      message += ", ";
    }
  }
  throw std::invalid_argument(message);
}

std::string NormalizeAndValidateKey(const std::string &key) {
  const auto normalized = NormalizeConfigKey(key);
  const auto &supported = SupportedConfigKeys();
  const auto found = std::find(supported.begin(), supported.end(), normalized);
  if (found == supported.end()) {
    ThrowUnknownKey(key);
  }
  return normalized;
}

std::string ExtractStringScalar(const YAML::Node &node,
                                const std::string &key_name) {
  if (!node.IsScalar()) {
    throw std::invalid_argument("Config key '" + key_name +
                                "' must be a string or path value");
  }
  return node.as<std::string>();
}

std::string ExtractPathLike(const YAML::Node &node,
                            const std::string &key_name) {
  if (node.IsScalar()) {
    return node.as<std::string>();
  }
  if (node.IsMap()) {
    for (const auto &candidate : {"path", "dir", "directory"}) {
      if (node[candidate]) {
        return ExtractStringScalar(node[candidate], key_name);
      }
    }
    throw std::invalid_argument("Config key '" + key_name +
                                "' map must contain 'path', 'dir', or "
                                "'directory'");
  }
  throw std::invalid_argument("Config key '" + key_name +
                              "' must be a string or mapping");
}

using ListAppender = void (*)(const std::string &, std::vector<std::string> &);

std::vector<std::string> ExtractList(const YAML::Node &node,
                                     const std::string &key_name,
                                     ListAppender appender) {
  std::vector<std::string> values;
  if (node.IsSequence()) {
    for (const auto &child : node) {
      if (!child.IsScalar()) {
        throw std::invalid_argument("Config key '" + key_name +
                                    "' must be a list of strings");
      }
      appender(child.as<std::string>(), values);
    }
    return values;
  }
  if (node.IsScalar()) {
    appender(node.as<std::string>(), values);
    return values;
  }
  throw std::invalid_argument("Config key '" + key_name +
                              "' must be a string or list of strings");
}

std::vector<std::string> ExtractFormats(const YAML::Node &node,
                                        const std::string &key_name) {
  return ExtractList(node, key_name, AppendFormats);
}

std::vector<std::string> ExtractIgnoredNamespaces(const YAML::Node &node,
                                                  const std::string &key_name) {
  return ExtractList(node, key_name, AppendIgnoredNamespaces);
}

std::vector<std::string> ExtractIgnoredPaths(const YAML::Node &node,
                                             const std::string &key_name) {
  return ExtractList(node, key_name, AppendRawPathStrings);
}

bool ExtractBool(const YAML::Node &node, const std::string &key_name) {
  if (!node.IsScalar()) {
    throw std::invalid_argument("Config key '" + key_name +
                                "' must be a boolean or boolean-like string");
  }
  return ParseBool(node.as<std::string>());
}

ConfigValue ToConfigValue(const std::string &key, const YAML::Node &node) {
  if (key == "formats") {
    return ExtractFormats(node, key);
  }
  if (key == "ignored_namespaces") {
    return ExtractIgnoredNamespaces(node, key);
  }
  if (key == "ignored_paths") {
    return ExtractIgnoredPaths(node, key);
  }
  if (key == "cache_ast" || key == "clean_cache") {
    return ConfigValue{ExtractBool(node, key)};
  }
  if (key == "build" || key == "out" || key == "root" || key == "cache_dir" ||
      key == "scope_notes" || key == "log_level" || key == "extractor" ||
      key == "analyzer" || key == "reporter") {
    if (key == "build" || key == "out" || key == "root" || key == "cache_dir") {
      return ConfigValue{ExtractPathLike(node, key)};
    }
    return ConfigValue{ExtractStringScalar(node, key)};
  }
  ThrowUnknownKey(key);
}

RawConfig ParseYamlConfig(const std::filesystem::path &path) {
  const auto root = YAML::LoadFile(path.string());
  if (!root.IsMap()) {
    throw std::invalid_argument(
        "Config file must contain a mapping at the root");
  }

  RawConfig config;
  for (const auto &entry : root) {
    const auto key = NormalizeAndValidateKey(entry.first.as<std::string>());
    config[key] = ToConfigValue(key, entry.second);
  }
  return config;
}

void ApplyConfig(const RawConfig &config, AnalyzeOptions &options) {
  for (const auto &[key, value] : config) {
    if (key == "root") {
      options.root = std::get<std::string>(value);
      continue;
    }
    if (key == "build") {
      options.build_directory = std::get<std::string>(value);
      continue;
    }
    if (key == "out") {
      options.output_directory = std::get<std::string>(value);
      continue;
    }
    if (key == "scope_notes") {
      options.scope_notes = std::get<std::string>(value);
      continue;
    }
    if (key == "formats") {
      options.formats = std::get<std::vector<std::string>>(value);
      continue;
    }
    if (key == "ignored_namespaces") {
      options.ignored_namespaces = std::get<std::vector<std::string>>(value);
      continue;
    }
    if (key == "ignored_paths") {
      const auto paths = std::get<std::vector<std::string>>(value);
      options.ignored_paths.assign(paths.begin(), paths.end());
      continue;
    }
    if (key == "log_level") {
      options.log_level = ParseLogLevel(std::get<std::string>(value));
      continue;
    }
    if (key == "extractor") {
      options.extractor = std::get<std::string>(value);
      continue;
    }
    if (key == "analyzer") {
      options.analyzer = std::get<std::string>(value);
      continue;
    }
    if (key == "reporter") {
      options.reporter = std::get<std::string>(value);
      continue;
    }
    if (key == "cache_ast") {
      options.enable_ast_cache = std::get<bool>(value);
      continue;
    }
    if (key == "clean_cache") {
      options.clean_cache = std::get<bool>(value);
      continue;
    }
    if (key == "cache_dir") {
      options.cache_directory = std::get<std::string>(value);
      continue;
    }
    ThrowUnknownKey(key);
  }
}

AnalyzeOptions ParseConfigFile(const std::filesystem::path &path) {
  if (!std::filesystem::exists(path)) {
    throw std::runtime_error("Config file not found: " + path.string());
  }
  const auto extension = ToLower(path.extension().string());
  if (extension != ".yml" && extension != ".yaml") {
    throw std::invalid_argument("Unsupported config format: " + extension);
  }

  AnalyzeOptions options;
  options.config_file = path;
  RawConfig config = ParseYamlConfig(path);
  ApplyConfig(config, options);

  return options;
}

AnalyzeOptions MergeOptions(const AnalyzeOptions &config_options,
                            const AnalyzeOptions &cli_options) {
  AnalyzeOptions merged = config_options;
  const auto override_path = [](auto &target, const auto &source) {
    if (source) {
      target = source;
    }
  };

  override_path(merged.root, cli_options.root);
  override_path(merged.build_directory, cli_options.build_directory);
  override_path(merged.output_directory, cli_options.output_directory);
  override_path(merged.scope_notes, cli_options.scope_notes);
  override_path(merged.config_file, cli_options.config_file);
  override_path(merged.cache_directory, cli_options.cache_directory);
  override_path(merged.extractor, cli_options.extractor);
  override_path(merged.analyzer, cli_options.analyzer);
  override_path(merged.reporter, cli_options.reporter);

  if (!cli_options.formats.empty()) {
    merged.formats = cli_options.formats;
  }
  if (!cli_options.ignored_namespaces.empty()) {
    merged.ignored_namespaces = cli_options.ignored_namespaces;
  }
  if (!cli_options.ignored_paths.empty()) {
    merged.ignored_paths = cli_options.ignored_paths;
  }
  if (cli_options.log_level) {
    merged.log_level = cli_options.log_level;
  }
  if (cli_options.enable_ast_cache) {
    merged.enable_ast_cache = cli_options.enable_ast_cache;
  }
  if (cli_options.clean_cache) {
    merged.clean_cache = cli_options.clean_cache;
  }
  return merged;
}

AnalyzeOptions ResolveAnalyzeOptions(const AnalyzeOptions &cli_options) {
  if (cli_options.show_help) {
    return cli_options;
  }

  AnalyzeOptions config_options;
  if (cli_options.config_file) {
    config_options = ParseConfigFile(*cli_options.config_file);
  }

  const auto merged = MergeOptions(config_options, cli_options);
  ValidateAnalyzeOptions(merged);
  return merged;
}

void ValidateReportOptions(const ReportOptions &options) {
  if (!options.root) {
    throw std::invalid_argument("--root is required for report command");
  }
}

std::vector<std::string>
ResolveReportFormats(const ReportOptions &options,
                     const std::filesystem::path &input_root) {
  const auto available = DetectAvailableReportFormats(input_root);
  if (options.formats.empty()) {
    if (available.empty()) {
      throw std::invalid_argument("No cached reports found under " +
                                  input_root.string());
    }
    return available;
  }

  for (const auto &format : options.formats) {
    if (std::find(available.begin(), available.end(), format) ==
        available.end()) {
      throw std::invalid_argument("Cached " + format +
                                  " report not found under " +
                                  input_root.string());
    }
  }
  return options.formats;
}

dsl::Report LoadCachedReport(const std::filesystem::path &root,
                             const std::vector<std::string> &formats) {
  dsl::Report report;
  for (const auto &format : formats) {
    if (format == "markdown") {
      report.markdown = ReadFileContent(root / "dsl_report.md");
    } else if (format == "json") {
      report.json = ReadFileContent(root / "dsl_report.json");
    }
  }
  return report;
}

AstCacheOptions BuildCacheOptions(const AnalyzeOptions &options,
                                  const std::filesystem::path &root) {
  AstCacheOptions cache_options;
  cache_options.enabled = options.enable_ast_cache.value_or(false);
  cache_options.clean = options.clean_cache.value_or(false);
  const auto cache_dir = options.cache_directory.value_or(root / ".dsl_cache");
  cache_options.directory = cache_dir;
  return cache_options;
}

std::filesystem::path ResolveBuildDirectory(const AnalyzeOptions &options,
                                            const std::filesystem::path &root) {
  if (options.build_directory) {
    return *options.build_directory;
  }
  return root / "build";
}

dsl::LoggingConfig BuildLoggingConfig(const AnalyzeOptions &options) {
  dsl::LoggingConfig logging;
  logging.level = options.log_level.value_or(dsl::LogLevel::kWarn);
  return logging;
}

dsl::AnalysisConfig BuildAnalysisConfig(const AnalyzeOptions &options,
                                        const std::filesystem::path &root,
                                        const std::filesystem::path &cache_dir,
                                        std::shared_ptr<dsl::Logger> logger) {
  dsl::AnalysisConfig config;
  config.root_path = root.string();
  config.formats = options.formats.empty()
                       ? std::vector<std::string>{"markdown"}
                       : options.formats;
  config.scope_notes = options.scope_notes.value_or("");
  config.logging = BuildLoggingConfig(options);
  if (!options.ignored_namespaces.empty()) {
    config.ignored_namespaces = options.ignored_namespaces;
  }
  if (!options.ignored_paths.empty()) {
    std::vector<std::string> normalized_paths;
    normalized_paths.reserve(options.ignored_paths.size());
    for (auto path : options.ignored_paths) {
      if (!path.is_absolute()) {
        path = root / path;
      }
      normalized_paths.push_back(
          std::filesystem::weakly_canonical(path).generic_string());
    }
    config.ignored_paths = std::move(normalized_paths);
  }
  config.cache.enable_ast_cache = options.enable_ast_cache.value_or(false);
  config.cache.clean = options.clean_cache.value_or(false);
  config.cache.directory = cache_dir.string();
  config.logger = std::move(logger);
  config.config_file = options.config_file ? options.config_file->string() : "";
  return config;
}

dsl::DefaultAnalyzerPipeline
BuildAnalyzePipeline(const AnalyzeOptions &options,
                     const std::filesystem::path &root,
                     const std::shared_ptr<dsl::Logger> &logger) {
  dsl::AnalyzerPipelineBuilder builder;
  builder.WithLogger(logger);
  builder.WithSourceAcquirer(std::make_unique<dsl::CMakeSourceAcquirer>(
      ResolveBuildDirectory(options, root), logger));
  builder.WithIndexer(std::make_unique<dsl::CompileCommandsAstIndexer>(
      std::filesystem::path{}, logger));
  if (options.extractor) {
    builder.WithExtractorName(*options.extractor);
  }
  if (options.analyzer) {
    builder.WithAnalyzerName(*options.analyzer);
  }
  if (options.reporter) {
    builder.WithReporterName(*options.reporter);
  }
  builder.WithAstCacheOptions(BuildCacheOptions(options, root));
  return builder.Build();
}

void WriteAnalyzeReports(const AnalyzeOptions &options,
                         const std::filesystem::path &root,
                         const dsl::Report &report) {
  const auto output_root = options.output_directory.value_or(root);
  WriteReports(output_root, report);
}

int RunAnalyze(const std::vector<std::string> &arguments) {
  const auto cli_options = ParseAnalyzeArguments(arguments);
  if (cli_options.show_help) {
    PrintAnalyzeUsage();
    return 0;
  }

  const auto merged = ResolveAnalyzeOptions(cli_options);
  const auto root = std::filesystem::weakly_canonical(*merged.root);
  const auto cache_directory =
      merged.cache_directory.value_or(root / ".dsl_cache");
  auto logger = dsl::MakeLogger(BuildLoggingConfig(merged), std::clog);

  auto pipeline = BuildAnalyzePipeline(merged, root, logger);
  auto config = BuildAnalysisConfig(merged, root, cache_directory, logger);

  const auto result = pipeline.Run(config);
  WriteAnalyzeReports(merged, root, result.report);
  return dsl::CoherenceExitCode(result.coherence);
}

int RunReport(const std::vector<std::string> &arguments) {
  const auto options = ParseReportArguments(arguments);
  if (options.show_help) {
    PrintReportUsage();
    return 0;
  }

  ValidateReportOptions(options);
  const auto input_root = std::filesystem::weakly_canonical(*options.root);
  const auto output_root =
      options.output_directory.value_or(std::filesystem::path{input_root});

  const auto formats = ResolveReportFormats(options, input_root);
  const auto cached_report = LoadCachedReport(input_root, formats);
  WriteReports(output_root, cached_report);
  return 0;
}

CacheCleanOptions
ParseCacheCleanArguments(const std::vector<std::string> &arguments) {
  CacheCleanOptions options;
  for (std::size_t i = 0; i < arguments.size(); ++i) {
    const auto &arg = arguments[i];
    if (arg == "--root") {
      options.root = RequireValue(arguments, i, "--root");
      continue;
    }
    if (arg == "--cache-dir") {
      options.cache_directory = RequireValue(arguments, i, "--cache-dir");
      continue;
    }
    if (arg == "--help" || arg == "-h") {
      options.show_help = true;
      return options;
    }
    throw std::invalid_argument("Unknown cache argument: " + arg);
  }
  return options;
}

std::filesystem::path ResolveCacheDirectory(const CacheCleanOptions &options,
                                            const std::filesystem::path &root) {
  if (options.cache_directory) {
    return *options.cache_directory;
  }
  return root / ".dsl_cache";
}

bool RemoveCacheDirectory(const std::filesystem::path &path) {
  if (!std::filesystem::exists(path)) {
    return false;
  }
  std::filesystem::remove_all(path);
  return true;
}

int RunCacheClean(const std::vector<std::string> &arguments) {
  const auto options = ParseCacheCleanArguments(arguments);
  if (options.show_help) {
    std::cout << "Usage: dsl-extract cache clean --root <path> [--cache-dir "
                 "<path>]\n";
    return 0;
  }
  if (!options.root) {
    throw std::invalid_argument("--root is required for cache clean");
  }

  const auto resolved_root = std::filesystem::weakly_canonical(*options.root);
  const auto cache_dir = ResolveCacheDirectory(options, resolved_root);
  if (RemoveCacheDirectory(cache_dir)) {
    std::cout << "Removed cache at " << cache_dir << "\n";
  } else {
    std::cout << "No cache directory found at " << cache_dir << "\n";
  }
  return 0;
}

int RunCacheCommand(const std::vector<std::string> &arguments) {
  if (arguments.empty()) {
    std::cout << "Cache subcommand requires an action (e.g., clean).\n";
    return 1;
  }
  const std::string &action = arguments.front();
  if (action == "clean") {
    const std::vector<std::string> clean_args(arguments.begin() + 1,
                                              arguments.end());
    return RunCacheClean(clean_args);
  }
  std::cout << "Unknown cache subcommand: " << action << "\n";
  return 1;
}

} // namespace dsl
