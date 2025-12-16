#include <dsl/component_registry.h>

#include <dsl/heuristic_dsl_extractor.h>
#include <dsl/markdown_reporter.h>
#include <dsl/rule_based_coherence_analyzer.h>

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace {

constexpr const char kDefaultExtractor[] = "heuristic";
constexpr const char kDefaultAnalyzer[] = "rule-based";
constexpr const char kDefaultReporter[] = "markdown";

} // namespace

namespace dsl {

template <typename Factory>
std::vector<std::string>
ComponentRegistry::RegisteredNames(const ComponentSet<Factory> &set) {
  std::vector<std::string> names;
  names.reserve(set.factories.size());
  for (const auto &entry : set.factories) {
    names.push_back(entry.first);
  }
  std::sort(names.begin(), names.end());
  return names;
}

template <typename Factory>
std::string ComponentRegistry::JoinNames(const ComponentSet<Factory> &set) {
  const auto names = RegisteredNames(set);
  std::string message;
  for (std::size_t i = 0; i < names.size(); ++i) {
    message += names[i];
    if (i + 1 < names.size()) {
      message += ", ";
    }
  }
  return message;
}

template <typename Interface, typename Factory>
std::unique_ptr<Interface>
ComponentRegistry::CreateComponent(const std::string &name,
                                   const ComponentSet<Factory> &set,
                                    const std::string &kind) const {
  const auto target_name = name.empty() ? set.default_name : name;
  if (target_name.empty()) {
    throw std::invalid_argument("No default " + kind + " registered");
  }
  const auto found = set.factories.find(target_name);
  if (found == set.factories.end()) {
    throw std::invalid_argument("Unknown " + kind + " '" + target_name +
                                "'. Registered: " + JoinNames(set));
  }
  auto instance = found->second();
  if (!instance) {
    throw std::runtime_error("Factory for " + kind + " '" + target_name +
                             "' returned null");
  }
  return instance;
}

template <typename Factory>
void ComponentRegistry::RegisterComponent(const std::string &name,
                                          Factory factory,
                                          bool set_as_default,
                                          ComponentSet<Factory> &set) {
  if (name.empty()) {
    throw std::invalid_argument("Component name cannot be empty");
  }
  if (!factory) {
    throw std::invalid_argument("Factory for '" + name + "' cannot be null");
  }
  if (set.factories.count(name) != 0) {
    throw std::invalid_argument("Component with name '" + name +
                                "' already registered");
  }
  set.factories.emplace(name, std::move(factory));
  if (set_as_default || set.default_name.empty()) {
    set.default_name = name;
  }
}

void ComponentRegistry::RegisterExtractor(const std::string &name,
                                          ExtractorFactory factory,
                                          bool set_as_default) {
  RegisterComponent(name, std::move(factory), set_as_default, extractors_);
}

void ComponentRegistry::RegisterAnalyzer(const std::string &name,
                                         AnalyzerFactory factory,
                                         bool set_as_default) {
  RegisterComponent(name, std::move(factory), set_as_default, analyzers_);
}

void ComponentRegistry::RegisterReporter(const std::string &name,
                                         ReporterFactory factory,
                                         bool set_as_default) {
  RegisterComponent(name, std::move(factory), set_as_default, reporters_);
}

std::unique_ptr<DslExtractor>
ComponentRegistry::CreateExtractor(const std::string &name) const {
  return CreateComponent<DslExtractor>(name, extractors_, "extractor");
}

std::unique_ptr<CoherenceAnalyzer>
ComponentRegistry::CreateAnalyzer(const std::string &name) const {
  return CreateComponent<CoherenceAnalyzer>(name, analyzers_, "analyzer");
}

std::unique_ptr<Reporter>
ComponentRegistry::CreateReporter(const std::string &name) const {
  return CreateComponent<Reporter>(name, reporters_, "reporter");
}

std::vector<std::string> ComponentRegistry::ExtractorNames() const {
  return RegisteredNames(extractors_);
}

std::vector<std::string> ComponentRegistry::AnalyzerNames() const {
  return RegisteredNames(analyzers_);
}

std::vector<std::string> ComponentRegistry::ReporterNames() const {
  return RegisteredNames(reporters_);
}

const std::string &ComponentRegistry::DefaultExtractorName() const {
  return extractors_.default_name;
}

const std::string &ComponentRegistry::DefaultAnalyzerName() const {
  return analyzers_.default_name;
}

const std::string &ComponentRegistry::DefaultReporterName() const {
  return reporters_.default_name;
}

ComponentRegistry MakeComponentRegistryWithDefaults() {
  ComponentRegistry registry;
  registry.RegisterExtractor(
      kDefaultExtractor, []() { return std::make_unique<HeuristicDslExtractor>(); },
      true);
  registry.RegisterAnalyzer(kDefaultAnalyzer, []() {
    return std::make_unique<RuleBasedCoherenceAnalyzer>();
  }, true);
  registry.RegisterReporter(
      kDefaultReporter, []() { return std::make_unique<MarkdownReporter>(); },
      true);
  return registry;
}

const ComponentRegistry &GlobalComponentRegistry() {
  static const ComponentRegistry registry = MakeComponentRegistryWithDefaults();
  return registry;
}

template std::unique_ptr<DslExtractor>
ComponentRegistry::CreateComponent<DslExtractor, ExtractorFactory>(
    const std::string &, const ComponentSet<ExtractorFactory> &,
    const std::string &) const;

template std::unique_ptr<CoherenceAnalyzer>
ComponentRegistry::CreateComponent<CoherenceAnalyzer, AnalyzerFactory>(
    const std::string &, const ComponentSet<AnalyzerFactory> &,
    const std::string &) const;

template std::unique_ptr<Reporter>
ComponentRegistry::CreateComponent<Reporter, ReporterFactory>(
    const std::string &, const ComponentSet<ReporterFactory> &,
    const std::string &) const;

template void ComponentRegistry::RegisterComponent<ComponentRegistry::ExtractorFactory>(
    const std::string &, ComponentRegistry::ExtractorFactory, bool,
    ComponentSet<ComponentRegistry::ExtractorFactory> &);

template void ComponentRegistry::RegisterComponent<ComponentRegistry::AnalyzerFactory>(
    const std::string &, ComponentRegistry::AnalyzerFactory, bool,
    ComponentSet<ComponentRegistry::AnalyzerFactory> &);

template void ComponentRegistry::RegisterComponent<ComponentRegistry::ReporterFactory>(
    const std::string &, ComponentRegistry::ReporterFactory, bool,
    ComponentSet<ComponentRegistry::ReporterFactory> &);

} // namespace dsl
