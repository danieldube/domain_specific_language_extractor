#include <dsl/analyzer_pipeline_builder.h>
#include <dsl/default_analyzer_pipeline.h>
#include <dsl/heuristic_dsl_extractor.h>
#include <dsl/interfaces.h>
#include <dsl/models.h>
#include <dsl/rule_based_coherence_analyzer.h>

#include <algorithm>
#include <filesystem>
#include <fstream>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "test_support/temporary_project.h"

namespace dsl {
namespace {

TEST(HeuristicDslExtractorTest, BuildsTermsAndRelationships) {
  AstIndex index;
  index.facts = {
      {"ProcessData", "function", "file.cpp:3", "int ProcessData()",
       "Processes input", "", "3:1-3:10"},
      {"ProcessData", "call", "file.cpp:10", "", "Transforms frame",
       "RenderFrame", "10:3-10:20"},
      {"RenderFrame", "function", "file.cpp:20",
       "void RenderFrame(FrameConfig cfg)", "Renders frame", "", "20:1-20:35"},
      {"RenderFrame", "type_usage", "file.cpp:25", "", "uses configuration",
       "FrameConfig", "25:5-25:16"},
      {"FrameConfig", "type", "types.h:5", "struct FrameConfig",
       "frame settings", "", "5:1-8:1"},
      {"CfgAlias", "reference", "types.h:12", "", "", "FrameConfig",
       "12:3-12:12"},
      {"RenderAlias", "alias", "file.cpp:40", "", "", "RenderFrame",
       "40:2-40:12"},
      {"RenderFrame", "owns", "file.cpp:45", "", "owns buffer", "FrameBuffer",
       "45:1-45:12"},
      {"FrameBuffer", "type", "types.h:30", "class FrameBuffer", "holds pixels",
       "", "30:1-30:20"},
  };
  for (auto &fact : index.facts) {
    fact.subject_in_project = true;
    if (fact.kind == "call" || fact.kind == "type_usage" || fact.kind == "owns" ||
        fact.kind == "reference" || fact.kind == "alias") {
      fact.target_scope = AstFact::TargetScope::kInProject;
    }
  }
  HeuristicDslExtractor extractor;

  const auto extraction = extractor.Extract(index);

  ASSERT_EQ(extraction.terms.size(), 4u);

  const auto &process_term = *std::find_if(
      extraction.terms.begin(), extraction.terms.end(),
      [](const auto &term) { return term.name == "processdata"; });
  EXPECT_THAT(process_term.definition,
              ::testing::HasSubstr("int ProcessData()"));
  EXPECT_THAT(process_term.definition, ::testing::HasSubstr("Processes input"));
  EXPECT_EQ(process_term.kind, "Action");
  EXPECT_GE(process_term.usage_count, 2);
  EXPECT_THAT(process_term.evidence,
              ::testing::Contains("file.cpp:3@3:1-3:10"));

  const auto &config_term = *std::find_if(
      extraction.terms.begin(), extraction.terms.end(),
      [](const auto &term) { return term.name == "frameconfig"; });
  EXPECT_THAT(config_term.aliases,
              ::testing::UnorderedElementsAre("FrameConfig", "CfgAlias"));
  EXPECT_EQ(config_term.kind, "Entity");
  EXPECT_EQ(config_term.usage_count, 3);

  const auto &render_term = *std::find_if(
      extraction.terms.begin(), extraction.terms.end(),
      [](const auto &term) { return term.name == "renderframe"; });
  EXPECT_THAT(render_term.aliases,
              ::testing::UnorderedElementsAre("RenderFrame", "RenderAlias"));
  EXPECT_THAT(render_term.definition, ::testing::HasSubstr("Renders frame"));
  EXPECT_EQ(render_term.usage_count, 5);

  const auto &buffer_term = *std::find_if(
      extraction.terms.begin(), extraction.terms.end(),
      [](const auto &term) { return term.name == "framebuffer"; });
  EXPECT_EQ(buffer_term.kind, "Entity");
  EXPECT_EQ(buffer_term.usage_count, 2);

  ASSERT_EQ(extraction.relationships.size(), 3u);
  EXPECT_THAT(
      extraction.relationships,
      ::testing::UnorderedElementsAre(
          ::testing::AllOf(
              ::testing::Field(&DslRelationship::subject, "processdata"),
              ::testing::Field(&DslRelationship::verb, "calls"),
              ::testing::Field(&DslRelationship::object, "renderframe")),
          ::testing::AllOf(
              ::testing::Field(&DslRelationship::subject, "renderframe"),
              ::testing::Field(&DslRelationship::verb, "uses-type"),
              ::testing::Field(&DslRelationship::object, "frameconfig")),
          ::testing::AllOf(
              ::testing::Field(&DslRelationship::subject, "renderframe"),
              ::testing::Field(&DslRelationship::verb, "owns"),
              ::testing::Field(&DslRelationship::object, "framebuffer"))));
  ASSERT_FALSE(extraction.workflows.empty());
  ASSERT_FALSE(extraction.workflows.front().steps.empty());
  EXPECT_THAT(extraction.workflows.front().steps,
              ::testing::ElementsAre("processdata calls renderframe",
                                     "renderframe owns framebuffer",
                                     "renderframe uses-type frameconfig"));
  EXPECT_FALSE(extraction.extraction_notes.empty());
}

TEST(RuleBasedCoherenceAnalyzerTest, FlagsMissingRelationships) {
  DslExtractionResult extraction;
  DslTerm lonely;
  lonely.name = "lonely";
  lonely.kind = "Action";
  lonely.definition = "Derived from lonely";
  extraction.terms = {lonely};
  RuleBasedCoherenceAnalyzer analyzer;

  const auto result = analyzer.Analyze(extraction);

  ASSERT_EQ(result.findings.size(), 1u);
  EXPECT_EQ(result.findings.front().term, "lonely");
  EXPECT_THAT(result.findings.front().description,
              ::testing::HasSubstr("No relationships"));
}

TEST(RuleBasedCoherenceAnalyzerTest, DetectsDuplicateTerms) {
  DslExtractionResult extraction;
  DslTerm shared_one;
  shared_one.name = "shared";
  shared_one.kind = "Action";
  shared_one.definition = "One";
  DslTerm shared_two = shared_one;
  shared_two.definition = "Two";
  extraction.terms = {shared_one, shared_two};

  DslRelationship relationship;
  relationship.subject = "shared";
  relationship.verb = "relates";
  relationship.object = "shared";
  extraction.relationships = {relationship};
  RuleBasedCoherenceAnalyzer analyzer;

  const auto result = analyzer.Analyze(extraction);

  ASSERT_EQ(result.findings.size(), 1u);
  EXPECT_EQ(result.findings.front().term, "shared");
}

TEST(RuleBasedCoherenceAnalyzerTest, DetectsAmbiguousAliases) {
  DslExtractionResult extraction;
  DslTerm first;
  first.name = "alpha";
  first.aliases = {"SharedAlias"};
  first.evidence = {"alpha:1"};
  DslTerm second;
  second.name = "beta";
  second.aliases = {"SharedAlias"};
  second.evidence = {"beta:2"};
  extraction.terms = {first, second};

  DslRelationship relationship;
  relationship.subject = "alpha";
  relationship.verb = "relates";
  relationship.object = "beta";
  extraction.relationships = {relationship};

  RuleBasedCoherenceAnalyzer analyzer;

  const auto result = analyzer.Analyze(extraction);

  EXPECT_THAT(result.findings,
              ::testing::Contains(::testing::AllOf(
                  ::testing::Field(&Finding::term, "sharedalias"),
                  ::testing::Field(&Finding::conflict,
                                   ::testing::HasSubstr("Alias reused")),
                  ::testing::Field(
                      &Finding::examples,
                      ::testing::Contains(::testing::HasSubstr("alpha"))))));
}

TEST(RuleBasedCoherenceAnalyzerTest, DetectsConflictingVerbsBetweenSamePair) {
  DslExtractionResult extraction;
  DslRelationship calls;
  calls.subject = "alpha";
  calls.verb = "calls";
  calls.object = "beta";
  calls.evidence = {"alpha calls beta"};

  DslRelationship owns;
  owns.subject = "alpha";
  owns.verb = "owns";
  owns.object = "beta";
  owns.evidence = {"alpha owns beta"};
  extraction.relationships = {calls, owns};

  RuleBasedCoherenceAnalyzer analyzer;

  const auto result = analyzer.Analyze(extraction);

  EXPECT_THAT(result.findings,
              ::testing::Contains(::testing::AllOf(
                  ::testing::Field(&Finding::term, "alpha->beta"),
                  ::testing::Field(&Finding::conflict,
                                   ::testing::HasSubstr("Conflicting verbs")),
                  ::testing::Field(&Finding::examples,
                                   ::testing::Contains(::testing::HasSubstr(
                                       "alpha calls beta"))))));
}

TEST(RuleBasedCoherenceAnalyzerTest, FlagsHighUsageTermsWithoutRelationships) {
  DslExtractionResult extraction;
  DslTerm busy;
  busy.name = "busy";
  busy.usage_count = 5;
  busy.evidence = {"busy evidence"};
  DslTerm connected;
  connected.name = "connected";
  extraction.terms = {busy, connected};

  DslRelationship unrelated;
  unrelated.subject = "connected";
  unrelated.verb = "links";
  unrelated.object = "else";
  extraction.relationships = {unrelated};

  RuleBasedCoherenceAnalyzer analyzer;

  const auto result = analyzer.Analyze(extraction);

  EXPECT_THAT(result.findings,
              ::testing::Contains(::testing::AllOf(
                  ::testing::Field(&Finding::term, "busy"),
                  ::testing::Field(&Finding::conflict,
                                   ::testing::HasSubstr("High-usage term")),
                  ::testing::Field(&Finding::examples,
                                   ::testing::Contains("busy evidence")))));
}

TEST(RuleBasedCoherenceAnalyzerTest,
     DetectsCanonicalizationInconsistenciesAcrossArtifacts) {
  DslExtractionResult extraction;
  DslTerm service;
  service.name = "PaymentService";
  DslTerm duplicate;
  duplicate.name = "paymentservice";
  extraction.terms = {service, duplicate};

  DslRelationship helper;
  helper.subject = "Helper";
  helper.verb = "uses";
  helper.object = "Other";
  extraction.relationships = {helper};

  RuleBasedCoherenceAnalyzer analyzer;

  const auto result = analyzer.Analyze(extraction);

  EXPECT_THAT(result.findings,
              ::testing::Contains(::testing::AllOf(
                  ::testing::Field(&Finding::term, "paymentservice"),
                  ::testing::Field(&Finding::conflict,
                                   ::testing::HasSubstr("canonicalization")),
                  ::testing::Field(&Finding::examples,
                                   ::testing::Contains(::testing::HasSubstr(
                                       "PaymentService"))))));
}

TEST(RuleBasedCoherenceAnalyzerTest, FlagsMutatingOrVoidGetter) {
  DslExtractionResult extraction;
  extraction.facts = {
      {"GetValue", "function", "file.cpp:3", "void GetValue()", "", "", ""},
      {"GetValue", "mutation", "file.cpp:4", "", "writes cache", "", ""},
  };

  RuleBasedCoherenceAnalyzer analyzer;

  const auto result = analyzer.Analyze(extraction);

  EXPECT_THAT(
      result.findings,
      ::testing::Contains(::testing::Field(
          &Finding::conflict, ::testing::HasSubstr("Getter mutates state"))));
  EXPECT_THAT(result.findings,
              ::testing::Contains(::testing::Field(
                  &Finding::conflict, ::testing::HasSubstr("returns void"))));
}

TEST(RuleBasedCoherenceAnalyzerTest, FlagsSetterWithoutMutations) {
  DslExtractionResult extraction;
  extraction.facts = {{"SetValue", "function", "file.cpp:8",
                       "void SetValue(int value)", "", "", ""}};

  RuleBasedCoherenceAnalyzer analyzer;

  const auto result = analyzer.Analyze(extraction);

  EXPECT_THAT(
      result.findings,
      ::testing::Contains(::testing::Field(
          &Finding::conflict, ::testing::HasSubstr("Setter lacks mutations"))));
}

TEST(RuleBasedCoherenceAnalyzerTest, FlagsImpureOrNonBoolPredicates) {
  DslExtractionResult extraction;
  extraction.facts = {
      {"IsReady", "function", "file.cpp:12", "int IsReady()", "", "", ""},
      {"IsReady", "mutation", "file.cpp:13", "", "updates cache", "", ""},
  };

  RuleBasedCoherenceAnalyzer analyzer;

  const auto result = analyzer.Analyze(extraction);

  EXPECT_THAT(
      result.findings,
      ::testing::Contains(::testing::Field(
          &Finding::conflict, ::testing::HasSubstr("does not return bool"))));
  EXPECT_THAT(result.findings,
              ::testing::Contains(::testing::Field(
                  &Finding::conflict,
                  ::testing::HasSubstr("Predicate mutates state"))));
}

TEST(RuleBasedCoherenceAnalyzerTest, DetectsOpenWithoutCloseInCaller) {
  DslExtractionResult extraction;
  extraction.facts = {
      {"Controller::Run", "call", "runner.cpp:20", "", "", "OpenSession", ""},
      {"Controller::Run", "call", "runner.cpp:21", "", "", "DoWork", ""},
  };

  RuleBasedCoherenceAnalyzer analyzer;

  const auto result = analyzer.Analyze(extraction);

  EXPECT_THAT(
      result.findings,
      ::testing::Contains(::testing::Field(
          &Finding::conflict, ::testing::HasSubstr("Lifecycle mismatch"))));
}

TEST(RuleBasedCoherenceAnalyzerTest, AcceptsBalancedOpenCloseInCaller) {
  DslExtractionResult extraction;
  extraction.facts = {
      {"Controller::Run", "call", "runner.cpp:20", "", "", "OpenSession", ""},
      {"Controller::Run", "call", "runner.cpp:22", "", "", "CloseSession", ""},
  };

  RuleBasedCoherenceAnalyzer analyzer;

  const auto result = analyzer.Analyze(extraction);

  EXPECT_TRUE(result.findings.empty());
}

TEST(DefaultAnalyzerPipelineTest, RunsComponentsInOrder) {
  test::TemporaryProject project;
  project.AddFile("CMakeLists.txt", "cmake_minimum_required(VERSION 3.20)\n");
  const auto source_path =
      project.AddFile("src/pipeline.cpp", "int f() {return 1;}");
  const auto build_dir = project.root() / "build";
  std::filesystem::create_directories(build_dir);
  const auto compile_commands_path = build_dir / "compile_commands.json";
  {
    std::ofstream compile_commands(compile_commands_path);
    compile_commands << "[\\n";
    compile_commands << "  {\\n";
    compile_commands << "    \"directory\": \"" << build_dir.string()
                     << "\",\\n";
    compile_commands << "    \"file\": \""
                     << std::filesystem::weakly_canonical(source_path).string()
                     << "\",\\n";
    compile_commands << "    \"command\": \"clang -std=c++17 -c "
                     << std::filesystem::weakly_canonical(source_path).string()
                     << "\"\\n";
    compile_commands << "  }\\n";
    compile_commands << "]\n";
  }

  AnalysisConfig config{.root_path = project.root().string(),
                        .formats = {"markdown", "json"}};
  auto pipeline = AnalyzerPipelineBuilder::WithDefaults().Build();

  const auto result = pipeline.Run(config);

  ASSERT_FALSE(result.extraction.terms.empty());
  EXPECT_FALSE(result.report.markdown.empty());
  EXPECT_FALSE(result.report.json.empty());
  EXPECT_EQ(result.extraction.terms.front().name, "f");
  EXPECT_FALSE(result.coherence.findings.empty());
}

} // namespace
} // namespace dsl
