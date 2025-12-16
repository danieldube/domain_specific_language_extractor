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
#include <vector>

namespace {

using dsl::AnalyzeOptions;

void PrintGlobalUsage() {
  std::cout
      << "Usage: dsl-extract <command> [options]\n\n"
      << "Commands:\n"
      << "  analyze   Run DSL analysis (default if no command is given).\n"
      << "  report    (placeholder) Render reports from cached analysis.\n"
      << "  cache     Manage caches (subcommands: clean).\n\n"
      << "Run 'dsl-extract analyze --help' for analysis options.\n";
}

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
      << "  --config <file>       Optional YAML/TOML config file\n"
      << "  --log-level <level>   Logging verbosity (error,warn,info,debug)\n"
      << "  --verbose             Shortcut for --log-level info\n"
      << "  --debug               Shortcut for --log-level debug\n"
      << "  --cache-ast           Enable AST caching\n"
      << "  --cache-dir <path>    Override AST cache directory\n"
      << "  --clean-cache         Remove AST cache before running\n"
      << "  --help                Show this message\n";
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
    options.log_level = ParseLogLevel(
        RequireValue(arguments, index, std::string(argument)));
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
    AppendFormats(RequireValue(arguments, index, "--format"),
                 options.formats);
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

} // namespace

namespace dsl {

AnalyzeOptions ParseAnalyzeArguments(const std::vector<std::string> &arguments) {
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

std::optional<ConfigEntry> ParseConfigLine(std::string line) {
  const auto comment = line.find('#');
  if (comment != std::string::npos) {
    line = line.substr(0, comment);
  }
  line = Trim(line);
  if (line.empty()) {
    return std::nullopt;
  }

  const auto delimiter = line.find_first_of(":=");
  if (delimiter == std::string::npos) {
    throw std::invalid_argument("Invalid config line (missing delimiter): " +
                                line);
  }

  auto key = Trim(line.substr(0, delimiter));
  auto value = Trim(line.substr(delimiter + 1));
  if (key.empty()) {
    throw std::invalid_argument("Invalid config line (missing key): " + line);
  }

  if (!value.empty() && value.front() == '[' && value.back() == ']') {
    value = value.substr(1, value.size() - 2);
  }
  if (!value.empty() && ((value.front() == '"' && value.back() == '"') ||
                         (value.front() == '\'' && value.back() == '\''))) {
    value = value.substr(1, value.size() - 2);
  }

  return ConfigEntry{ToLower(key), value};
}

void ApplyConfigEntry(const ConfigEntry &entry, AnalyzeOptions &options) {
  const auto &key = entry.key;
  const auto &value = entry.value;

  if (key == "root") {
    options.root = value;
    return;
  }
  if (key == "build" || key == "build_directory") {
    options.build_directory = value;
    return;
  }
  if (key == "out" || key == "output" || key == "output_directory") {
    options.output_directory = value;
    return;
  }
  if (key == "scope_notes" || key == "scope-notes") {
    options.scope_notes = value;
    return;
  }
  if (key == "formats" || key == "format") {
    AppendFormats(value, options.formats);
    return;
  }
  if (key == "log_level" || key == "log-level") {
    options.log_level = ParseLogLevel(value);
    return;
  }
  if (key == "cache_ast" || key == "cache-ast") {
    options.enable_ast_cache = ParseBool(value);
    return;
  }
  if (key == "clean_cache" || key == "clean-cache") {
    options.clean_cache = ParseBool(value);
    return;
  }
  if (key == "cache_dir" || key == "cache-dir") {
    options.cache_directory = value;
  }
}

AnalyzeOptions ParseConfigFile(const std::filesystem::path &path) {
  if (!std::filesystem::exists(path)) {
    throw std::runtime_error("Config file not found: " + path.string());
  }
  const auto extension = ToLower(path.extension().string());
  if (extension != ".yml" && extension != ".yaml" && extension != ".toml") {
    throw std::invalid_argument("Unsupported config format: " + extension);
  }

  AnalyzeOptions options;
  options.config_file = path;
  std::ifstream stream(path);
  std::string line;
  while (std::getline(stream, line)) {
    const auto parsed = ParseConfigLine(line);
    if (!parsed) {
      continue;
    }
    ApplyConfigEntry(*parsed, options);
  }

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

  if (!cli_options.formats.empty()) {
    merged.formats = cli_options.formats;
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
  config.cache.enable_ast_cache = options.enable_ast_cache.value_or(false);
  config.cache.clean = options.clean_cache.value_or(false);
  config.cache.directory = cache_dir.string();
  config.logger = std::move(logger);
  config.config_file = options.config_file ? options.config_file->string() : "";
  return config;
}

dsl::DefaultAnalyzerPipeline BuildAnalyzePipeline(
    const AnalyzeOptions &options, const std::filesystem::path &root,
    const std::shared_ptr<dsl::Logger> &logger) {
  dsl::AnalyzerPipelineBuilder builder;
  builder.WithLogger(logger);
  builder.WithSourceAcquirer(std::make_unique<dsl::CMakeSourceAcquirer>(
      ResolveBuildDirectory(options, root), logger));
  builder.WithIndexer(std::make_unique<dsl::CompileCommandsAstIndexer>(
      std::filesystem::path{}, logger));
  builder.WithExtractor(std::make_unique<dsl::HeuristicDslExtractor>());
  builder.WithAnalyzer(std::make_unique<dsl::RuleBasedCoherenceAnalyzer>());
  builder.WithReporter(std::make_unique<dsl::MarkdownReporter>());
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

std::filesystem::path
ResolveCacheDirectory(const CacheCleanOptions &options,
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

int main(int argc, char **argv) {
  try {
    const std::vector<std::string> arguments(argv + 1, argv + argc);

    if (!arguments.empty() &&
        (arguments.front() == "--help" || arguments.front() == "-h")) {
      PrintGlobalUsage();
      return 0;
    }

    std::string command = "analyze";
    std::size_t first_argument_index = 0;
    if (!arguments.empty() && arguments.front().rfind('-', 0) != 0) {
      command = arguments.front();
      first_argument_index = 1;
    }

    if (command == "analyze") {
      const std::vector<std::string> analyze_arguments(
          arguments.begin() + static_cast<std::ptrdiff_t>(first_argument_index),
          arguments.end());
      return dsl::RunAnalyze(analyze_arguments);
    }

    if (command == "report") {
      std::cout
          << "Report command is not implemented yet. Run 'dsl-extract analyze' "
             "to generate reports.\n";
      return 1;
    }

    if (command == "cache") {
      const std::vector<std::string> cache_arguments(
          arguments.begin() + static_cast<std::ptrdiff_t>(first_argument_index),
          arguments.end());
      return dsl::RunCacheCommand(cache_arguments);
    }

    throw std::invalid_argument("Unknown command: " + command);
  } catch (const std::exception &ex) {
    std::cerr << "Error: " << ex.what() << "\n";
    PrintGlobalUsage();
    return 1;
  }
}
