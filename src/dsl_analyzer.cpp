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

AnalyzeOptions
ParseAnalyzeArguments(const std::vector<std::string> &arguments) {
  AnalyzeOptions options;

  for (std::size_t i = 0; i < arguments.size(); ++i) {
    const std::string &argument = arguments[i];
    if (argument == "--help" || argument == "-h") {
      options.show_help = true;
      return options;
    }
    if (argument == "--root") {
      if (++i >= arguments.size()) {
        throw std::invalid_argument("--root requires a value");
      }
      options.root = arguments[i];
      continue;
    }
    if (argument == "--build") {
      if (++i >= arguments.size()) {
        throw std::invalid_argument("--build requires a value");
      }
      options.build_directory = arguments[i];
      continue;
    }
    if (argument == "--format") {
      if (++i >= arguments.size()) {
        throw std::invalid_argument("--format requires a value");
      }
      AppendFormats(arguments[i], options.formats);
      continue;
    }
    if (argument == "--out") {
      if (++i >= arguments.size()) {
        throw std::invalid_argument("--out requires a value");
      }
      options.output_directory = arguments[i];
      continue;
    }
    if (argument == "--scope-notes") {
      if (++i >= arguments.size()) {
        throw std::invalid_argument("--scope-notes requires a value");
      }
      options.scope_notes = arguments[i];
      continue;
    }
    if (argument == "--config") {
      if (++i >= arguments.size()) {
        throw std::invalid_argument("--config requires a file path");
      }
      options.config_file = arguments[i];
      continue;
    }
    if (argument == "--log-level") {
      if (++i >= arguments.size()) {
        throw std::invalid_argument("--log-level requires a value");
      }
      options.log_level = ParseLogLevel(arguments[i]);
      continue;
    }
    if (argument == "--verbose") {
      options.log_level = dsl::LogLevel::kInfo;
      continue;
    }
    if (argument == "--debug") {
      options.log_level = dsl::LogLevel::kDebug;
      continue;
    }
    if (argument == "--cache-ast") {
      options.enable_ast_cache = true;
      continue;
    }
    if (argument == "--clean-cache") {
      options.clean_cache = true;
      continue;
    }
    if (argument == "--cache-dir") {
      if (++i >= arguments.size()) {
        throw std::invalid_argument("--cache-dir requires a value");
      }
      options.cache_directory = arguments[i];
      continue;
    }
    throw std::invalid_argument("Unknown argument: " + argument);
  }

  return options;
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
    const auto comment = line.find('#');
    if (comment != std::string::npos) {
      line = line.substr(0, comment);
    }
    line = Trim(line);
    if (line.empty()) {
      continue;
    }
    const auto delimiter = line.find_first_of(":=");
    if (delimiter == std::string::npos) {
      continue;
    }
    auto key = Trim(line.substr(0, delimiter));
    auto value = Trim(line.substr(delimiter + 1));
    if (!value.empty() && value.front() == '[' && value.back() == ']') {
      value = value.substr(1, value.size() - 2);
    }
    if (!value.empty() && ((value.front() == '"' && value.back() == '"') ||
                           (value.front() == '\'' && value.back() == '\''))) {
      value = value.substr(1, value.size() - 2);
    }
    key = ToLower(key);

    if (key == "root") {
      options.root = value;
      continue;
    }
    if (key == "build" || key == "build_directory") {
      options.build_directory = value;
      continue;
    }
    if (key == "out" || key == "output" || key == "output_directory") {
      options.output_directory = value;
      continue;
    }
    if (key == "scope_notes" || key == "scope-notes") {
      options.scope_notes = value;
      continue;
    }
    if (key == "formats" || key == "format") {
      AppendFormats(value, options.formats);
      continue;
    }
    if (key == "log_level" || key == "log-level") {
      options.log_level = ParseLogLevel(value);
      continue;
    }
    if (key == "cache_ast" || key == "cache-ast") {
      options.enable_ast_cache = ParseBool(value);
      continue;
    }
    if (key == "clean_cache" || key == "clean-cache") {
      options.clean_cache = ParseBool(value);
      continue;
    }
    if (key == "cache_dir" || key == "cache-dir") {
      options.cache_directory = value;
      continue;
    }
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

int RunAnalyze(const std::vector<std::string> &arguments) {
  const auto cli_options = ParseAnalyzeArguments(arguments);
  if (cli_options.show_help) {
    PrintAnalyzeUsage();
    return 0;
  }

  AnalyzeOptions config_options;
  if (cli_options.config_file) {
    config_options = ParseConfigFile(*cli_options.config_file);
  }

  const auto merged = MergeOptions(config_options, cli_options);
  ValidateAnalyzeOptions(merged);

  const auto root = std::filesystem::weakly_canonical(*merged.root);
  const auto cache_directory =
      merged.cache_directory.value_or(root / ".dsl_cache");
  auto logger = dsl::MakeLogger(BuildLoggingConfig(merged), std::clog);

  dsl::AnalyzerPipelineBuilder builder;
  builder.WithLogger(logger);
  builder.WithSourceAcquirer(std::make_unique<dsl::CMakeSourceAcquirer>(
      ResolveBuildDirectory(merged, root), logger));
  builder.WithIndexer(std::make_unique<dsl::CompileCommandsAstIndexer>(
      std::filesystem::path{}, logger));
  builder.WithExtractor(std::make_unique<dsl::HeuristicDslExtractor>());
  builder.WithAnalyzer(std::make_unique<dsl::RuleBasedCoherenceAnalyzer>());
  builder.WithReporter(std::make_unique<dsl::MarkdownReporter>());
  builder.WithAstCacheOptions(BuildCacheOptions(merged, root));

  auto pipeline = builder.Build();
  auto config = BuildAnalysisConfig(merged, root, cache_directory, logger);

  const auto result = pipeline.Run(config);
  const auto output_root = merged.output_directory.value_or(root);
  WriteReports(output_root, result.report);
  return dsl::CoherenceExitCode(result.coherence);
}

int RunCacheClean(const std::vector<std::string> &arguments) {
  std::optional<std::filesystem::path> root;
  std::optional<std::filesystem::path> cache_directory;
  for (std::size_t i = 0; i < arguments.size(); ++i) {
    const auto &arg = arguments[i];
    if (arg == "--root") {
      if (++i >= arguments.size()) {
        throw std::invalid_argument("--root requires a value");
      }
      root = arguments[i];
      continue;
    }
    if (arg == "--cache-dir") {
      if (++i >= arguments.size()) {
        throw std::invalid_argument("--cache-dir requires a value");
      }
      cache_directory = arguments[i];
      continue;
    }
    if (arg == "--help" || arg == "-h") {
      std::cout << "Usage: dsl-extract cache clean --root <path> [--cache-dir "
                   "<path>]\n";
      return 0;
    }
    throw std::invalid_argument("Unknown cache argument: " + arg);
  }

  if (!root) {
    throw std::invalid_argument("--root is required for cache clean");
  }

  const auto resolved_root = std::filesystem::weakly_canonical(*root);
  const auto cache_dir = cache_directory.value_or(resolved_root / ".dsl_cache");
  if (std::filesystem::exists(cache_dir)) {
    std::filesystem::remove_all(cache_dir);
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
