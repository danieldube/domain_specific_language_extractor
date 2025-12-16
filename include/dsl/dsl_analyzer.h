#pragma once

#include <dsl/ast_cache.h>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace dsl {

struct AnalyzeOptions {
  std::optional<std::filesystem::path> root;
  std::optional<std::filesystem::path> build_directory;
  std::optional<std::filesystem::path> output_directory;
  std::optional<std::filesystem::path> config_file;
  std::optional<std::filesystem::path> cache_directory;
  std::optional<std::string> scope_notes;
  std::vector<std::string> formats;
  std::optional<dsl::LogLevel> log_level;
  std::optional<bool> enable_ast_cache;
  std::optional<bool> clean_cache;
  bool show_help = false;
};

AstCacheOptions BuildCacheOptions(const AnalyzeOptions &options,
                                  const std::filesystem::path &root);

int RunAnalyze(const std::vector<std::string> &arguments);
int RunCacheClean(const std::vector<std::string> &arguments);
int RunCacheCommand(const std::vector<std::string> &arguments);

} // namespace dsl
