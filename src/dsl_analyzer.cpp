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

struct CliOptions {
  std::filesystem::path root;
  std::filesystem::path build_directory{"build"};
  std::vector<std::string> formats;
  std::string scope_notes;
  bool show_help = false;
};

void PrintUsage() {
  std::cout
      << "Usage: dsl_analyzer --root <path> [options]\n"
      << "Options:\n"
      << "  --root <path>         Root directory of the CMake project\n"
      << "  --build <path>        Build directory containing "
         "compile_commands.json\n"
      << "                        (default: build)\n"
      << "  --format <list>       Comma-separated list of output formats\n"
      << "                        (supported: markdown,json)\n"
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

CliOptions ParseArguments(int argc, char **argv) {
  CliOptions options;

  for (int i = 1; i < argc; ++i) {
    const std::string argument(argv[i]);
    if (argument == "--help") {
      options.show_help = true;
      return options;
    }
    if (argument == "--root") {
      if (++i >= argc) {
        throw std::invalid_argument("--root requires a value");
      }
      options.root = argv[i];
      continue;
    }
    if (argument == "--build") {
      if (++i >= argc) {
        throw std::invalid_argument("--build requires a value");
      }
      options.build_directory = argv[i];
      continue;
    }
    if (argument == "--format") {
      if (++i >= argc) {
        throw std::invalid_argument("--format requires a value");
      }
      AppendFormats(argv[i], options.formats);
      continue;
    }
    if (argument == "--scope-notes") {
      if (++i >= argc) {
        throw std::invalid_argument("--scope-notes requires a value");
      }
      options.scope_notes = argv[i];
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
  WriteFileIfContent(root / "dsl_report.md", report.markdown);
  WriteFileIfContent(root / "dsl_report.json", report.json);
}

} // namespace

int main(int argc, char **argv) {
  try {
    const auto options = ParseArguments(argc, argv);
    if (options.show_help) {
      PrintUsage();
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
    WriteReports(options.root, result.report);
    return 0;
  } catch (const std::exception &ex) {
    std::cerr << "Error: " << ex.what() << "\n";
    PrintUsage();
    return 1;
  }
}
