#pragma once

#include <dsl/interfaces.h>
#include <dsl/logging.h>

#include <filesystem>
#include <memory>
#include <string>

namespace dsl {

struct AstCacheOptions {
  bool enabled = false;
  bool clean = false;
  std::filesystem::path directory;
};

std::filesystem::path ResolveCacheDirectory(const AstCacheOptions &options);

class AstCache {
public:
  AstCache(AstCacheOptions options, std::shared_ptr<Logger> logger);

  bool Load(const std::string &key, AstIndex &index) const;
  void Store(const std::string &key, const AstIndex &index) const;
  void Clean() const;
  const std::filesystem::path &Directory() const { return directory_; }

private:
  std::filesystem::path CachePath(const std::string &key) const;

  AstCacheOptions options_;
  std::filesystem::path directory_;
  std::shared_ptr<Logger> logger_;
};

std::string ToolchainVersion();
std::string BuildCacheKey(const SourceAcquisitionResult &sources,
                          const std::string &toolchain_version);

class CachingAstIndexer : public AstIndexer {
public:
  CachingAstIndexer(std::unique_ptr<AstIndexer> inner,
                    AstCacheOptions options, std::shared_ptr<Logger> logger);

  AstIndex BuildIndex(const SourceAcquisitionResult &sources) override;

private:
  std::unique_ptr<AstIndexer> inner_;
  AstCacheOptions options_;
  AstCache cache_;
  std::shared_ptr<Logger> logger_;
};

} // namespace dsl
