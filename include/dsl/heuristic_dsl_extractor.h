#pragma once

#include <dsl/interfaces.h>

namespace dsl {

class HeuristicDslExtractor : public DslExtractor {
public:
  DslExtractionResult Extract(const AstIndex &index) override;
};

} // namespace dsl

