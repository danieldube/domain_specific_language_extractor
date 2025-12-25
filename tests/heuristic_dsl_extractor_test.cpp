#include <dsl/heuristic_dsl_extractor.h>
#include <dsl/models.h>

#include <algorithm>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace dsl {
namespace {

using ::testing::AllOf;
using ::testing::Contains;
using ::testing::Each;
using ::testing::Field;
using ::testing::HasSubstr;
using ::testing::Not;
using ::testing::UnorderedElementsAre;

AnalysisConfig MakeConfig() {
  AnalysisConfig config;
  config.root_path = "repo";
  config.formats = {"markdown"};
  return config;
}

AstFact MakeDefinition(const std::string &name, const std::string &kind,
                       const std::string &signature,
                       const std::string &doc_comment = "",
                       const std::string &scope_path = "") {
  AstFact fact{};
  fact.name = name;
  fact.kind = kind;
  fact.signature = signature;
  fact.descriptor = signature;
  fact.source_location = name + "::location";
  fact.range = fact.source_location;
  fact.doc_comment = doc_comment;
  fact.scope_path = scope_path;
  fact.subject_in_project = true;
  return fact;
}

AstFact MakeRelationshipFact(const std::string &name, const std::string &kind,
                             const std::string &target,
                             AstFact::TargetScope scope,
                             const std::string &signature,
                             const std::string &descriptor,
                             const std::string &scope_path = "") {
  AstFact fact{};
  fact.name = name;
  fact.kind = kind;
  fact.target = target;
  fact.signature = signature;
  fact.descriptor = descriptor;
  fact.source_location = name + "::" + target;
  fact.range = fact.source_location;
  fact.scope_path = scope_path;
  fact.subject_in_project = true;
  fact.target_scope = scope;
  fact.target_location = target + "::location";
  return fact;
}

TEST(HeuristicDslExtractorTest, SkipsExternalTargetsAndCollectsDependencies) {
  AstIndex index;
  index.facts.push_back(MakeDefinition("Foo", "function", "int Foo()"));
  index.facts.push_back(MakeDefinition("Bar", "function", "int Bar()"));
  index.facts.push_back(MakeRelationshipFact("Foo", "call", "Bar",
                                             AstFact::TargetScope::kInProject,
                                             "int Bar()", "calls Bar"));
  index.facts.push_back(MakeRelationshipFact("Foo", "call", "std::sort",
                                             AstFact::TargetScope::kExternal,
                                             "std::sort", "calls std::sort"));
  index.facts.push_back(MakeRelationshipFact(
      "Foo", "type_usage", "ExternalType", AstFact::TargetScope::kExternal,
      "uses ExternalType", "uses ExternalType"));

  HeuristicDslExtractor extractor;
  const auto result = extractor.Extract(index, MakeConfig());

  std::vector<std::string> term_names;
  term_names.reserve(result.terms.size());
  for (const auto &term : result.terms) {
    term_names.push_back(term.name);
  }

  EXPECT_THAT(term_names, UnorderedElementsAre("foo", "bar"));
  EXPECT_THAT(result.relationships,
              Each(Field(&DslRelationship::object, Not("externaltype"))));
  EXPECT_THAT(result.relationships,
              Contains(AllOf(Field(&DslRelationship::subject, "foo"),
                             Field(&DslRelationship::verb, "calls"),
                             Field(&DslRelationship::object, "bar"))));

  ASSERT_THAT(result.external_dependencies,
              Contains(Field(&DslTerm::name, "std..sort")));
  ASSERT_THAT(result.external_dependencies,
              Contains(Field(&DslTerm::name, "externaltype")));
}

TEST(HeuristicDslExtractorTest, SkipsDefaultIgnoredNamespaces) {
  AstIndex index;
  index.facts.push_back(
      MakeDefinition("std::Vector", "type", "class std::Vector"));
  index.facts.push_back(MakeRelationshipFact(
      "Foo", "call", "testing::Do", AstFact::TargetScope::kInProject,
      "void testing::Do()", "calls testing::Do"));

  HeuristicDslExtractor extractor;
  const auto result = extractor.Extract(index, MakeConfig());

  EXPECT_THAT(result.terms,
              Not(Contains(Field(&DslTerm::name, "std..vector"))));
  EXPECT_THAT(result.relationships,
              Not(Contains(Field(&DslRelationship::object, "testing..do"))));
}

TEST(HeuristicDslExtractorTest, AllowsCustomIgnoredNamespaces) {
  AstIndex index;
  index.facts.push_back(MakeDefinition("Bar", "function", "void Bar()"));
  index.facts.push_back(
      MakeDefinition("gtest::Suite", "type", "class gtest::Suite"));
  index.facts.push_back(MakeRelationshipFact(
      "Bar", "call", "gtest::Suite", AstFact::TargetScope::kInProject,
      "void gtest::Suite()", "calls gtest::Suite"));

  auto config = MakeConfig();
  config.ignored_namespaces = {"custom"};

  HeuristicDslExtractor extractor;
  const auto result = extractor.Extract(index, config);

  EXPECT_THAT(result.terms, Contains(Field(&DslTerm::name, "gtest..suite")));
  EXPECT_THAT(result.relationships,
              Contains(Field(&DslRelationship::object, "gtest..suite")));
}

TEST(HeuristicDslExtractorTest, MergesCommentsAndScopeIntoDefinitions) {
  AstIndex index;
  index.facts.push_back(MakeDefinition("sample::Widget", "type",
                                       "struct Widget",
                                       "Widget entity for samples", "sample"));
  index.facts.push_back(MakeDefinition(
      "sample::Calculator::Add", "function", "int Add(int a, int b)",
      "Adds two numbers", "sample::Calculator"));

  HeuristicDslExtractor extractor;
  const auto result = extractor.Extract(index, MakeConfig());

  ASSERT_THAT(result.terms,
              Contains(AllOf(Field(&DslTerm::name, "sample..widget"),
                             Field(&DslTerm::definition,
                                   AllOf(HasSubstr("Widget entity for samples"),
                                         HasSubstr("sample"))))));
  ASSERT_THAT(
      result.terms,
      Contains(AllOf(
          Field(&DslTerm::name, "sample..calculator..add"),
          Field(&DslTerm::definition, AllOf(HasSubstr("Adds two numbers"),
                                            HasSubstr("sample::Calculator"))),
          Field(
              &DslTerm::evidence,
              Contains(HasSubstr(
                  "sample::Calculator@sample::Calculator::Add::location"))))));
}

TEST(HeuristicDslExtractorTest, MarksHelpersAsLowRelevance) {
  AstIndex index;
  index.facts.push_back(MakeDefinition("app::RunPipeline", "function",
                                       "void RunPipeline()",
                                       "Runs the pipeline", "app"));
  index.facts.push_back(MakeDefinition(
      "helpers::LoggingHelper", "function", "void LoggingHelper()",
      "Utility helper for logging", "helpers"));
  index.facts.push_back(MakeRelationshipFact(
      "app::RunPipeline", "call", "helpers::LoggingHelper",
      AstFact::TargetScope::kInProject, "void helpers::LoggingHelper()",
      "calls helper", "app"));

  HeuristicDslExtractor extractor;
  const auto result = extractor.Extract(index, MakeConfig());

  const auto pipeline_term = std::find_if(
      result.terms.begin(), result.terms.end(),
      [](const auto &term) { return term.name == "app..runpipeline"; });
  ASSERT_NE(pipeline_term, result.terms.end());
  EXPECT_THAT(pipeline_term->definition, HasSubstr("Runs the pipeline"));

  const auto helper_term = std::find_if(
      result.terms.begin(), result.terms.end(),
      [](const auto &term) { return term.name == "helpers..logginghelper"; });
  ASSERT_NE(helper_term, result.terms.end());
  EXPECT_THAT(helper_term->definition,
              HasSubstr("Low relevance: helper/utility"));
  EXPECT_GE(helper_term->usage_count, 2);
}

TEST(HeuristicDslExtractorTest, DropsPlaceholderOnlyEntries) {
  AstIndex index;
  auto ignored = MakeDefinition("std::IgnoredType", "type", "");
  ignored.descriptor.clear();
  index.facts.push_back(ignored);

  auto placeholder =
      MakeDefinition("helpers::TempUtility", "function", "", "", "helpers");
  placeholder.descriptor.clear();
  index.facts.push_back(placeholder);

  index.facts.push_back(MakeDefinition("core::RealWork", "function",
                                       "void RealWork()", "Performs work"));

  HeuristicDslExtractor extractor;
  const auto result = extractor.Extract(index, MakeConfig());

  EXPECT_THAT(result.terms,
              Not(Contains(Field(&DslTerm::name, "helpers..temputility"))));
  EXPECT_THAT(result.terms,
              Not(Contains(Field(&DslTerm::name, "std..ignoredtype"))));
  ASSERT_THAT(result.terms, Contains(Field(&DslTerm::name, "core..realwork")));
  for (const auto &term : result.terms) {
    EXPECT_THAT(term.definition,
                AllOf(Not(HasSubstr("Inferred from symbol context")),
                      Not(HasSubstr("Declared as"))));
  }
}

} // namespace
} // namespace dsl
