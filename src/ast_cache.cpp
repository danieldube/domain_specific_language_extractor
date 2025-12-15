#include <dsl/ast_cache.h>

#include <dsl/escaping.h>

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

} // namespace dsl
