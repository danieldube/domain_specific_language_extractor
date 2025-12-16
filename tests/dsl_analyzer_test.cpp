#include <dsl/dsl_analyzer.h>

#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

namespace dsl {
namespace {

TEST(ParseAnalyzeArgumentsTest, ParsesFlagsAndValues) {
  const std::vector<std::string> args = {
      "--root",        "/project/root",   "--build",   "build-dir",
      "--format",      "markdown,json",   "--out",     "out-dir",
      "--scope-notes", "notes",          "--config",  "config.yml",
      "--log-level",   "debug",          "--cache-ast",
      "--clean-cache", "--cache-dir",     "cache"};

  const auto options = ParseAnalyzeArguments(args);

  ASSERT_TRUE(options.root);
  EXPECT_EQ(options.root->generic_string(), "/project/root");
  ASSERT_TRUE(options.build_directory);
  EXPECT_EQ(options.build_directory->generic_string(), "build-dir");
  ASSERT_TRUE(options.output_directory);
  EXPECT_EQ(options.output_directory->generic_string(), "out-dir");
  ASSERT_TRUE(options.config_file);
  EXPECT_EQ(options.config_file->generic_string(), "config.yml");
  ASSERT_EQ(options.formats,
            (std::vector<std::string>{"markdown", "json"}));
  EXPECT_EQ(options.scope_notes, std::optional<std::string>("notes"));
  EXPECT_EQ(options.log_level, std::optional<dsl::LogLevel>(dsl::LogLevel::kDebug));
  EXPECT_TRUE(options.enable_ast_cache.value());
  EXPECT_TRUE(options.clean_cache.value());
  ASSERT_TRUE(options.cache_directory);
  EXPECT_EQ(options.cache_directory->generic_string(), "cache");
}

TEST(ParseConfigLineTest, ParsesTrimmedKeyValuePairs) {
  const auto entry = ParseConfigLine(" scope_notes : ' example ' ");

  ASSERT_TRUE(entry.has_value());
  EXPECT_EQ(entry->key, "scope_notes");
  EXPECT_EQ(entry->value, " example ");
}

TEST(ParseConfigLineTest, RejectsMissingDelimiters) {
  EXPECT_THROW(ParseConfigLine("invalid line"), std::invalid_argument);
}

TEST(ParseConfigFileTest, CombinesConfigEntries) {
  const auto temp_config = std::filesystem::temp_directory_path() /
                           "dsl_analyzer_config_test.toml";
  std::ofstream config_stream(temp_config);
  config_stream << "root = /from/config\n";
  config_stream << "formats = [markdown,json]\n";
  config_stream << "cache-ast = true\n";
  config_stream << "scope-notes = \"with notes\"\n";
  config_stream.close();

  const auto options = ParseConfigFile(temp_config);

  ASSERT_TRUE(options.root);
  EXPECT_EQ(options.root->generic_string(), "/from/config");
  ASSERT_EQ(options.formats,
            (std::vector<std::string>{"markdown", "json"}));
  EXPECT_EQ(options.enable_ast_cache, std::optional<bool>(true));
  EXPECT_EQ(options.scope_notes, std::optional<std::string>("with notes"));
  std::filesystem::remove(temp_config);
}

TEST(ResolveAnalyzeOptionsTest, CliOverridesConfig) {
  const auto temp_config = std::filesystem::temp_directory_path() /
                           "dsl_analyzer_config_override.toml";
  std::ofstream config_stream(temp_config);
  config_stream << "root = /config/root\n";
  config_stream << "formats = markdown\n";
  config_stream << "cache-ast = true\n";
  config_stream.close();

  AnalyzeOptions cli_options;
  cli_options.root = "/cli/root";
  cli_options.formats = {"json"};
  cli_options.enable_ast_cache = false;
  cli_options.config_file = temp_config;

  const auto resolved = ResolveAnalyzeOptions(cli_options);

  EXPECT_EQ(resolved.root->generic_string(), "/cli/root");
  ASSERT_EQ(resolved.formats, (std::vector<std::string>{"json"}));
  EXPECT_FALSE(resolved.enable_ast_cache.value());
  std::filesystem::remove(temp_config);
}

TEST(CacheCleanHelpersTest, ParsesAndResolvesCacheDirectory) {
  const auto options =
      ParseCacheCleanArguments({"--root", "/project", "--cache-dir", "cache"});

  ASSERT_TRUE(options.root);
  ASSERT_TRUE(options.cache_directory);
  const auto resolved = ResolveCacheDirectory(options, "/project");
  EXPECT_EQ(resolved.generic_string(), "cache");
}

TEST(CacheCleanHelpersTest, RemovesCacheDirectoryIfPresent) {
  const auto temp_dir =
      std::filesystem::temp_directory_path() / "dsl_cache_clean_test";
  std::filesystem::create_directories(temp_dir);

  EXPECT_TRUE(std::filesystem::exists(temp_dir));
  EXPECT_TRUE(RemoveCacheDirectory(temp_dir));
  EXPECT_FALSE(std::filesystem::exists(temp_dir));
  EXPECT_FALSE(RemoveCacheDirectory(temp_dir));
}

} // namespace
} // namespace dsl
