#include <dsl/dsl_analyzer.h>

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void PrintGlobalUsage() {
  std::cout
      << "Usage: dsl-extract <command> [options]\n\n"
      << "Commands:\n"
      << "  analyze   Run DSL analysis (default if no command is given).\n"
      << "  report    (placeholder) Render reports from cached analysis.\n"
      << "  cache     Manage caches (subcommands: clean).\n\n"
      << "Run 'dsl-extract analyze --help' for analysis options.\n";
}
}

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
