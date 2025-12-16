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

struct ConfigEntry {
  std::string key;
  std::string value;
};

struct CacheCleanOptions {
  std::optional<std::filesystem::path> root;
  std::optional<std::filesystem::path> cache_directory;
  bool show_help = false;
};

AstCacheOptions BuildCacheOptions(const AnalyzeOptions &options,
                                  const std::filesystem::path &root);

AnalyzeOptions ParseAnalyzeArguments(const std::vector<std::string> &arguments);
AnalyzeOptions ParseConfigFile(const std::filesystem::path &path);
AnalyzeOptions MergeOptions(const AnalyzeOptions &config_options,
                            const AnalyzeOptions &cli_options);
AnalyzeOptions ResolveAnalyzeOptions(const AnalyzeOptions &cli_options);

std::optional<ConfigEntry> ParseConfigLine(std::string line);
void ApplyConfigEntry(const ConfigEntry &entry, AnalyzeOptions &options);

CacheCleanOptions
ParseCacheCleanArguments(const std::vector<std::string> &arguments);
std::filesystem::path
ResolveCacheDirectory(const CacheCleanOptions &options,
                      const std::filesystem::path &root);
bool RemoveCacheDirectory(const std::filesystem::path &path);

int RunAnalyze(const std::vector<std::string> &arguments);
int RunCacheClean(const std::vector<std::string> &arguments);
int RunCacheCommand(const std::vector<std::string> &arguments);

} // namespace dsl
