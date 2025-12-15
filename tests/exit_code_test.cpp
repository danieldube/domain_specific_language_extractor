#include <dsl/cli_exit_codes.h>
#include <dsl/models.h>
#include <dsl/rule_based_coherence_analyzer.h>

#include <gtest/gtest.h>

namespace dsl {
namespace {

TEST(CoherenceExitCodesTest, ReturnsZeroWhenResultIsClean) {
  CoherenceResult coherence;
  EXPECT_EQ(CoherenceExitCode(coherence), 0);
}

TEST(CoherenceExitCodesTest, ReturnsNonZeroWhenFindingsExist) {
  CoherenceResult coherence;
  coherence.severity = CoherenceSeverity::kIncoherent;
  coherence.findings.push_back(Finding{});

  EXPECT_EQ(CoherenceExitCode(coherence), 2);
}

TEST(CoherenceExitCodesTest, AnalyzerMarksIncoherentResults) {
  DslExtractionResult extraction;
  DslTerm lonely;
  lonely.name = "lonely";
  lonely.kind = "Action";
  lonely.definition = "Declared";
  extraction.terms = {lonely};

  RuleBasedCoherenceAnalyzer analyzer;
  const auto coherence = analyzer.Analyze(extraction);

  EXPECT_EQ(coherence.severity, CoherenceSeverity::kIncoherent);
  EXPECT_EQ(CoherenceExitCode(coherence), 2);
}

TEST(CoherenceExitCodesTest, AnalyzerLeavesSeverityCleanWhenNoFindings) {
  DslExtractionResult extraction;

  RuleBasedCoherenceAnalyzer analyzer;
  const auto coherence = analyzer.Analyze(extraction);

  EXPECT_EQ(coherence.severity, CoherenceSeverity::kClean);
  EXPECT_EQ(CoherenceExitCode(coherence), 0);
}

} // namespace
} // namespace dsl
