#include <dsl/dsl_analyzer.h>

#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

namespace dsl {
namespace {

TEST(ParseAnalyzeArgumentsTest, ParsesFlagsAndValues) {
  const std::vector<std::string> args = {"--root",
                                         "/project/root",
                                         "--build",
                                         "build-dir",
                                         "--format",
                                         "markdown,json",
                                         "--out",
                                         "out-dir",
                                         "--scope-notes",
                                         "notes",
                                         "--config",
                                         "config.yml",
                                         "--ignored-namespaces",
                                         "custom,other",
                                         "--ignored-source-folders",
                                         "generated,temp",
                                         "--log-level",
                                         "debug",
                                         "--cache-ast",
                                         "--clean-cache",
                                         "--cache-dir",
                                         "cache",
                                         "--extractor",
                                         "custom-extractor",
                                         "--analyzer",
                                         "custom-analyzer",
                                         "--reporter",
                                         "custom-reporter"};

  const auto options = ParseAnalyzeArguments(args);

  ASSERT_TRUE(options.root);
  EXPECT_EQ(options.root->generic_string(), "/project/root");
  ASSERT_TRUE(options.build_directory);
  EXPECT_EQ(options.build_directory->generic_string(), "build-dir");
  ASSERT_TRUE(options.output_directory);
  EXPECT_EQ(options.output_directory->generic_string(), "out-dir");
  ASSERT_TRUE(options.config_file);
  EXPECT_EQ(options.config_file->generic_string(), "config.yml");
  ASSERT_EQ(options.formats, (std::vector<std::string>{"markdown", "json"}));
  ASSERT_EQ(options.ignored_namespaces,
            (std::vector<std::string>{"custom", "other"}));
  ASSERT_EQ(options.ignored_source_directories,
            (std::vector<std::string>{"generated", "temp"}));
  EXPECT_EQ(options.scope_notes, std::optional<std::string>("notes"));
  EXPECT_EQ(options.log_level,
            std::optional<dsl::LogLevel>(dsl::LogLevel::kDebug));
  EXPECT_TRUE(options.enable_ast_cache.value());
  EXPECT_TRUE(options.clean_cache.value());
  ASSERT_TRUE(options.cache_directory);
  EXPECT_EQ(options.cache_directory->generic_string(), "cache");
  EXPECT_EQ(options.extractor, std::optional<std::string>("custom-extractor"));
  EXPECT_EQ(options.analyzer, std::optional<std::string>("custom-analyzer"));
  EXPECT_EQ(options.reporter, std::optional<std::string>("custom-reporter"));
}

TEST(ParseReportArgumentsTest, ParsesFlagsAndFormats) {
  const std::vector<std::string> args = {"--root",   "/project/root",
                                         "--out",    "reports",
                                         "--format", "markdown,json"};

  const auto options = ParseReportArguments(args);

  ASSERT_TRUE(options.root);
  EXPECT_EQ(options.root->generic_string(), "/project/root");
  ASSERT_TRUE(options.output_directory);
  EXPECT_EQ(options.output_directory->generic_string(), "reports");
  ASSERT_EQ(options.formats, (std::vector<std::string>{"markdown", "json"}));
}

TEST(ParseConfigFileTest, ParsesYamlNestedValuesAndFormatsList) {
  const auto temp_config =
      std::filesystem::temp_directory_path() / "dsl_analyzer_config_test.yaml";
  std::ofstream config_stream(temp_config);
  config_stream << "root: /from/yaml\n";
  config_stream << "build:\n  directory: build-yaml\n";
  config_stream << "out:\n  path: reports\n";
  config_stream << "formats:\n  - markdown\n  - json\n";
  config_stream << "cache_ast: true\n";
  config_stream << "clean_cache: false\n";
  config_stream << "cache_dir: .dsl/cache\n";
  config_stream << "log_level: info\n";
  config_stream << "scope_notes: yaml notes\n";
  config_stream << "extractor: yaml-extractor\n";
  config_stream << "analyzer: yaml-analyzer\n";
  config_stream << "reporter: yaml-reporter\n";
  config_stream << "ignored_source_directories:\n";
  config_stream << "  - generated\n";
  config_stream << "  - vendor\n";
  config_stream.close();

  const auto options = ParseConfigFile(temp_config);

  ASSERT_TRUE(options.root);
  EXPECT_EQ(options.root->generic_string(), "/from/yaml");
  ASSERT_TRUE(options.build_directory);
  EXPECT_EQ(options.build_directory->generic_string(), "build-yaml");
  ASSERT_TRUE(options.output_directory);
  EXPECT_EQ(options.output_directory->generic_string(), "reports");
  ASSERT_EQ(options.formats, (std::vector<std::string>{"markdown", "json"}));
  EXPECT_EQ(options.enable_ast_cache, std::optional<bool>(true));
  EXPECT_EQ(options.clean_cache, std::optional<bool>(false));
  ASSERT_TRUE(options.cache_directory);
  EXPECT_EQ(options.cache_directory->generic_string(), ".dsl/cache");
  EXPECT_EQ(options.log_level,
            std::optional<dsl::LogLevel>(dsl::LogLevel::kInfo));
  EXPECT_EQ(options.scope_notes, std::optional<std::string>("yaml notes"));
  EXPECT_EQ(options.extractor, std::optional<std::string>("yaml-extractor"));
  EXPECT_EQ(options.analyzer, std::optional<std::string>("yaml-analyzer"));
  EXPECT_EQ(options.reporter, std::optional<std::string>("yaml-reporter"));
  ASSERT_EQ(options.ignored_source_directories,
            (std::vector<std::string>{"generated", "vendor"}));
  std::filesystem::remove(temp_config);
}

TEST(ParseConfigFileTest, RejectsUnknownKeys) {
  const auto temp_config =
      std::filesystem::temp_directory_path() / "dsl_analyzer_unknown.yaml";
  std::ofstream config_stream(temp_config);
  config_stream << "root: /project\n";
  config_stream << "unexpected: true\n";
  config_stream.close();

  EXPECT_THROW(ParseConfigFile(temp_config), std::invalid_argument);
  std::filesystem::remove(temp_config);
}

TEST(ResolveAnalyzeOptionsTest, CliOverridesConfig) {
  const auto temp_config = std::filesystem::temp_directory_path() /
                           "dsl_analyzer_config_override.yaml";
  std::ofstream config_stream(temp_config);
  config_stream << "root: /config/root\n";
  config_stream << "formats: markdown\n";
  config_stream << "cache-ast: true\n";
  config_stream.close();

  AnalyzeOptions cli_options;
  cli_options.root = "/cli/root";
  cli_options.formats = {"json"};
  cli_options.enable_ast_cache = false;
  cli_options.config_file = temp_config;
  cli_options.extractor = "cli-extractor";
  cli_options.reporter = "cli-reporter";
  cli_options.ignored_source_directories = {"generated"};

  const auto resolved = ResolveAnalyzeOptions(cli_options);

  EXPECT_EQ(resolved.root->generic_string(), "/cli/root");
  ASSERT_EQ(resolved.formats, (std::vector<std::string>{"json"}));
  EXPECT_FALSE(resolved.enable_ast_cache.value());
  EXPECT_EQ(resolved.extractor, std::optional<std::string>("cli-extractor"));
  EXPECT_EQ(resolved.reporter, std::optional<std::string>("cli-reporter"));
  ASSERT_EQ(resolved.ignored_source_directories,
            (std::vector<std::string>{"generated"}));
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

TEST(RunReportTest, CopiesCachedReportsToOutput) {
  const auto input_root =
      std::filesystem::temp_directory_path() / "dsl_report_input";
  std::filesystem::create_directories(input_root);
  const auto markdown_path = input_root / "dsl_report.md";
  const auto json_path = input_root / "dsl_report.json";
  {
    std::ofstream markdown(markdown_path);
    markdown << "cached markdown";
  }
  {
    std::ofstream json(json_path);
    json << "cached json";
  }

  const auto output_root =
      std::filesystem::temp_directory_path() / "dsl_report_output";

  const int exit_code =
      RunReport({"--root", input_root.string(), "--out", output_root.string(),
                 "--format", "markdown,json"});

  EXPECT_EQ(exit_code, 0);
  EXPECT_TRUE(std::filesystem::exists(output_root / "dsl_report.md"));
  EXPECT_TRUE(std::filesystem::exists(output_root / "dsl_report.json"));
  std::filesystem::remove_all(input_root);
  std::filesystem::remove_all(output_root);
}

TEST(RunReportTest, DetectsAvailableFormatsWhenNotSpecified) {
  const auto input_root =
      std::filesystem::temp_directory_path() / "dsl_report_defaults";
  std::filesystem::create_directories(input_root);
  const auto markdown_path = input_root / "dsl_report.md";
  {
    std::ofstream markdown(markdown_path);
    markdown << "cached markdown";
  }

  const auto output_root =
      std::filesystem::temp_directory_path() / "dsl_report_defaults_out";

  const int exit_code =
      RunReport({"--root", input_root.string(), "--out", output_root.string()});

  EXPECT_EQ(exit_code, 0);
  EXPECT_TRUE(std::filesystem::exists(output_root / "dsl_report.md"));
  EXPECT_FALSE(std::filesystem::exists(output_root / "dsl_report.json"));
  std::filesystem::remove_all(input_root);
  std::filesystem::remove_all(output_root);
}

} // namespace
} // namespace dsl
