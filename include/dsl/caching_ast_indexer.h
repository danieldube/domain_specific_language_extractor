#pragma once

#include <dsl/ast_cache.h>

#include <memory>

namespace dsl {

class CachingAstIndexer : public AstIndexer {
public:
  CachingAstIndexer(std::unique_ptr<AstIndexer> inner, AstCacheOptions options,
                    std::shared_ptr<Logger> logger);

  AstIndex BuildIndex(const SourceAcquisitionResult &sources) override;

private:
  std::unique_ptr<AstIndexer> inner_;
  AstCacheOptions options_;
  AstCache cache_;
  std::shared_ptr<Logger> logger_;
};

} // namespace dsl
