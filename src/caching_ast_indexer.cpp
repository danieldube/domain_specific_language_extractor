#include <dsl/caching_ast_indexer.h>

#include <dsl/logging.h>

#include <clang-c/Index.h>

#include <functional>
#include <utility>

namespace dsl {

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
