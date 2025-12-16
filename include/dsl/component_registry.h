#pragma once

#include <dsl/interfaces.h>

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace dsl {

class ComponentRegistry {
public:
  using ExtractorFactory = std::function<std::unique_ptr<DslExtractor>()>;
  using AnalyzerFactory =
      std::function<std::unique_ptr<CoherenceAnalyzer>()>;
  using ReporterFactory = std::function<std::unique_ptr<Reporter>()>;

  void RegisterExtractor(const std::string &name, ExtractorFactory factory,
                         bool set_as_default = false);
  void RegisterAnalyzer(const std::string &name, AnalyzerFactory factory,
                        bool set_as_default = false);
  void RegisterReporter(const std::string &name, ReporterFactory factory,
                        bool set_as_default = false);

  std::unique_ptr<DslExtractor>
  CreateExtractor(const std::string &name = "") const;
  std::unique_ptr<CoherenceAnalyzer>
  CreateAnalyzer(const std::string &name = "") const;
  std::unique_ptr<Reporter> CreateReporter(const std::string &name = "") const;

  std::vector<std::string> ExtractorNames() const;
  std::vector<std::string> AnalyzerNames() const;
  std::vector<std::string> ReporterNames() const;

  const std::string &DefaultExtractorName() const;
  const std::string &DefaultAnalyzerName() const;
  const std::string &DefaultReporterName() const;

  template <typename Factory>
  struct ComponentSet {
    std::unordered_map<std::string, Factory> factories;
    std::string default_name;
  };

private:
  template <typename Factory>
  static std::vector<std::string>
  RegisteredNames(const ComponentSet<Factory> &set);

  template <typename Factory>
  static std::string JoinNames(const ComponentSet<Factory> &set);

  template <typename Interface, typename Factory>
  std::unique_ptr<Interface>
  CreateComponent(const std::string &name, const ComponentSet<Factory> &set,
                  const std::string &kind) const;

  template <typename Factory>
  void RegisterComponent(const std::string &name, Factory factory,
                         bool set_as_default, ComponentSet<Factory> &set);

  ComponentSet<ExtractorFactory> extractors_;
  ComponentSet<AnalyzerFactory> analyzers_;
  ComponentSet<ReporterFactory> reporters_;
};

ComponentRegistry MakeComponentRegistryWithDefaults();
const ComponentRegistry &GlobalComponentRegistry();

} // namespace dsl
