#include <dsl/markdown_reporter.h>
#include <dsl/models.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace dsl {
namespace {

TEST(MarkdownReporterTest, RendersSections) {
  DslExtractionResult extraction;
  DslTerm verb_term;
  verb_term.name = "verb";
  verb_term.kind = "Action";
  verb_term.definition = "Derived from verb";
  verb_term.evidence = {"verb.cpp:10-12"};
  verb_term.aliases = {"verbAlias"};
  verb_term.usage_count = 2;
  extraction.terms = {verb_term};

  DslTerm external_term;
  external_term.name = "std::vector";
  external_term.kind = "External";
  external_term.definition = "calls std::vector";
  external_term.evidence = {"vector.h:1"};
  external_term.usage_count = 1;
  extraction.external_dependencies = {external_term};

  DslRelationship relationship;
  relationship.subject = "verb";
  relationship.verb = "acts";
  relationship.object = "object";
  relationship.evidence = {"caller:1-2"};
  relationship.notes = "note";
  relationship.usage_count = 3;
  extraction.relationships = {relationship};

  DslExtractionResult::Workflow workflow;
  workflow.name = "Example";
  workflow.steps = {"verb -> object"};
  extraction.workflows = {workflow};
  extraction.extraction_notes = {"example note"};

  CoherenceResult coherence;
  Finding finding;
  finding.term = "verb";
  finding.conflict = "Conflict";
  finding.examples = {"file:1"};
  finding.suggested_canonical_form = "verb";
  finding.description = finding.conflict;
  coherence.findings = {finding};
  MarkdownReporter reporter;
  AnalysisConfig config{.root_path = "repo", .formats = {"markdown", "json"}};

  const auto report = reporter.Render(extraction, coherence, config);

  EXPECT_THAT(report.markdown, ::testing::HasSubstr("Analysis Header"));
  EXPECT_THAT(report.markdown,
              ::testing::HasSubstr("Canonical Terms (Glossary)"));
  EXPECT_THAT(report.markdown, ::testing::HasSubstr("External Dependencies"));
  EXPECT_THAT(report.markdown, ::testing::HasSubstr("Relationships"));
  EXPECT_THAT(report.markdown, ::testing::HasSubstr("Workflows"));
  EXPECT_THAT(report.markdown, ::testing::HasSubstr("Incoherence Report"));
  EXPECT_THAT(report.markdown, ::testing::HasSubstr("Extraction Notes"));
  EXPECT_THAT(report.markdown, ::testing::HasSubstr("verb"));
  EXPECT_THAT(report.json, ::testing::HasSubstr("\"analysis_header\""));
  EXPECT_THAT(report.json, ::testing::HasSubstr("\"terms\""));
  EXPECT_THAT(report.json, ::testing::HasSubstr("\"external_dependencies\""));
  EXPECT_THAT(report.json, ::testing::HasSubstr("\"relationships\""));
  EXPECT_THAT(report.json, ::testing::HasSubstr("\"workflows\""));
  EXPECT_THAT(report.json, ::testing::HasSubstr("\"incoherence_report\""));
  EXPECT_THAT(report.json, ::testing::HasSubstr("\"extraction_notes\""));
}

TEST(MarkdownReporterTest, JoinsListsWithDelimiters) {
  DslExtractionResult extraction;
  DslTerm term;
  term.name = "term";
  term.kind = "Kind";
  term.definition = "Definition";
  term.evidence = {"file:1", "file:2"};
  term.aliases = {"alias1", "alias2"};
  term.usage_count = 1;
  extraction.terms = {term};

  CoherenceResult coherence;
  MarkdownReporter reporter;
  AnalysisConfig config{.root_path = "repo", .formats = {"markdown", "json"}};

  const auto report = reporter.Render(extraction, coherence, config);

  EXPECT_THAT(report.markdown, ::testing::HasSubstr("file:1<br>\nfile:2"));
  EXPECT_THAT(report.markdown, ::testing::HasSubstr("alias1<br>alias2"));
  EXPECT_THAT(report.json,
              ::testing::HasSubstr("\"evidence\": [\"file:1\",\"file:2\"]"));
  EXPECT_THAT(report.json,
              ::testing::HasSubstr("\"aliases\": [\"alias1\",\"alias2\"]"));
}

} // namespace
} // namespace dsl
