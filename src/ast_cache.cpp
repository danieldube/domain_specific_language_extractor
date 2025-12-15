#include <dsl/ast_cache.h>

#include <dsl/escaping.h>

#include <clang-c/Index.h>

#include <filesystem>
#include <fstream>
#include <utility>

namespace {

std::shared_ptr<dsl::Logger> EnsureLogger(std::shared_ptr<dsl::Logger> logger) {
  if (!logger) {
    return std::make_shared<dsl::NullLogger>();
  }
  return logger;
}

} // namespace

namespace dsl {

std::filesystem::path ResolveCacheDirectory(const AstCacheOptions &options) {
  if (!options.directory.empty()) {
    return std::filesystem::weakly_canonical(options.directory);
  }
  return std::filesystem::weakly_canonical(std::filesystem::current_path() /
                                           ".dsl_cache");
}

AstCache::AstCache(AstCacheOptions options, std::shared_ptr<Logger> logger)
    : options_(std::move(options)), directory_(ResolveCacheDirectory(options_)),
      logger_(EnsureLogger(std::move(logger))) {}

bool AstCache::Load(const std::string &key, AstIndex &index) const {
  if (!options_.enabled) {
    return false;
  }
  const auto path = CachePath(key);
  if (!std::filesystem::exists(path)) {
    return false;
  }

  std::ifstream stream(path);
  if (!stream) {
    logger_->Log(LogLevel::kWarn, "Failed to open AST cache",
                 {{"path", path.string()}});
    return false;
  }

  std::string line;
  AstIndex cached;
  while (std::getline(stream, line)) {
    if (line.empty() || line[0] == '#') {
      continue;
    }
    auto fields = SplitEscaped(line);
    if (fields.size() != 7) {
      logger_->Log(LogLevel::kWarn, "Ignoring malformed cache line",
                   {{"path", path.string()}});
      continue;
    }
    cached.facts.push_back({fields[0], fields[1], fields[2], fields[3],
                            fields[4], fields[5], fields[6]});
  }

  index = std::move(cached);
  logger_->Log(LogLevel::kInfo, "Loaded AST facts from cache",
               {{"path", path.string()},
                {"fact_count", std::to_string(index.facts.size())}});
  return true;
}

void AstCache::Store(const std::string &key, const AstIndex &index) const {
  if (!options_.enabled) {
    return;
  }
  const auto path = CachePath(key);
  std::filesystem::create_directories(path.parent_path());
  std::ofstream stream(path, std::ios::trunc);
  if (!stream) {
    logger_->Log(LogLevel::kWarn, "Failed to write AST cache",
                 {{"path", path.string()}});
    return;
  }

  stream << "# toolchain cache entry\n";
  for (const auto &fact : index.facts) {
    stream << Escape(fact.name) << '\t' << Escape(fact.kind) << '\t'
           << Escape(fact.source_location) << '\t' << Escape(fact.signature)
           << '\t' << Escape(fact.descriptor) << '\t' << Escape(fact.target)
           << '\t' << Escape(fact.range) << '\n';
  }
  logger_->Log(LogLevel::kInfo, "Persisted AST cache",
               {{"path", path.string()},
                {"fact_count", std::to_string(index.facts.size())}});
}

void AstCache::Clean() const {
  if (std::filesystem::exists(directory_)) {
    std::filesystem::remove_all(directory_);
    logger_->Log(LogLevel::kInfo, "Cleared AST cache",
                 {{"directory", directory_.string()}});
  }
}

std::filesystem::path AstCache::CachePath(const std::string &key) const {
  return directory_ / ("ast_cache_" + key + ".dat");
}

std::string ToolchainVersion() {
  const auto version = clang_getClangVersion();
  std::string text;
  if (const auto *cstr = clang_getCString(version); cstr != nullptr) {
    text = cstr;
  }
  clang_disposeString(version);
  return text;
}

std::string BuildCacheKey(const SourceAcquisitionResult &sources,
                          const std::string &toolchain_version) {
  std::string accumulator = toolchain_version;
  accumulator.append(sources.project_root);
  accumulator.append(sources.build_directory);
  for (const auto &file : sources.files) {
    accumulator.append(file);
  }
  return std::to_string(std::hash<std::string>{}(accumulator));
}

CachingAstIndexer::CachingAstIndexer(std::unique_ptr<AstIndexer> inner,
                                     AstCacheOptions options,
                                     std::shared_ptr<Logger> logger)
    : inner_(std::move(inner)), options_(std::move(options)),
      cache_(options_, logger), logger_(EnsureLogger(std::move(logger))) {}

AstIndex CachingAstIndexer::BuildIndex(const SourceAcquisitionResult &sources) {
  if (options_.clean) {
    cache_.Clean();
  }
  if (!options_.enabled) {
    return inner_->BuildIndex(sources);
  }

  const auto version = ToolchainVersion();
  const auto key = BuildCacheKey(sources, version);
  AstIndex index;
  if (cache_.Load(key, index)) {
    logger_->Log(LogLevel::kInfo, "AST cache hit",
                 {{"key", key}, {"toolchain", version}});
    return index;
  }

  logger_->Log(LogLevel::kInfo, "AST cache miss",
               {{"key", key}, {"toolchain", version}});
  index = inner_->BuildIndex(sources);
  cache_.Store(key, index);
  return index;
}

} // namespace dsl
