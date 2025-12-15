#pragma once

#include <dsl/interfaces.h>

namespace dsl {

class MarkdownReporter : public Reporter {
public:
  Report Render(const DslExtractionResult &extraction,
                const CoherenceResult &coherence,
                const AnalysisConfig &config) override;
};

} // namespace dsl
