#pragma once

#include <dsl/interfaces.h>

namespace dsl {

class RuleBasedCoherenceAnalyzer : public CoherenceAnalyzer {
public:
  CoherenceResult Analyze(const DslExtractionResult &extraction) override;
};

} // namespace dsl
