#include <dsl/cli_exit_codes.h>
#include <dsl/default_components.h>
#include <dsl/models.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct AnalyzeOptions {
  std::filesystem::path root;
  std::filesystem::path build_directory{"build"};
  std::filesystem::path output_directory;
  std::vector<std::string> formats;
  std::string scope_notes;
  bool show_help = false;
};

void PrintGlobalUsage() {
  std::cout
      << "Usage: dsl-extract <command> [options]\n\n"
      << "Commands:\n"
      << "  analyze   Run DSL analysis (default if no command is given).\n"
      << "  report    (placeholder) Render reports from cached analysis.\n"
      << "  cache     (placeholder) Manage CLI caches (subcommands: clean).\n\n"
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
      << "  --out <path>          Directory for report outputs\n"
      << "                        (default: analysis root)\n"
      << "  --scope-notes <text>  Scope notes to embed in the report header\n"
      << "  --help                Show this message\n";
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
    throw std::invalid_argument("Unknown argument: " + argument);
  }

  if (options.root.empty()) {
    throw std::invalid_argument("--root is required");
  }

  return options;
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
      const auto options = ParseAnalyzeArguments(analyze_arguments);
      if (options.show_help) {
        PrintAnalyzeUsage();
        return 0;
      }

      dsl::AnalyzerPipelineBuilder builder =
          dsl::AnalyzerPipelineBuilder::WithDefaults();
      builder.WithSourceAcquirer(
          std::make_unique<dsl::CMakeSourceAcquirer>(options.build_directory));

      auto pipeline = builder.Build();
      dsl::AnalysisConfig config;
      config.root_path = options.root.string();
      config.formats = options.formats;
      config.scope_notes = options.scope_notes;

      const auto result = pipeline.Run(config);
      const auto output_root = options.output_directory.empty()
                                   ? options.root
                                   : options.output_directory;
      WriteReports(output_root, result.report);
      return dsl::CoherenceExitCode(result.coherence);
    }

    if (command == "report") {
      std::cout << "Report command is not implemented yet. "
                   "Run 'dsl-extract analyze' to generate reports.\n";
      return 1;
    }

    if (command == "cache") {
      std::cout << "Cache management is not implemented yet. Use "
                   "'dsl-extract analyze' to regenerate outputs.\n";
      return 1;
    }

    throw std::invalid_argument("Unknown command: " + command);
  } catch (const std::exception &ex) {
    std::cerr << "Error: " << ex.what() << "\n";
    PrintGlobalUsage();
    return 1;
  }
}
