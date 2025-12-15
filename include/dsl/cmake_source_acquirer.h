#pragma once

#include <dsl/interfaces.h>
#include <dsl/logging.h>

#include <filesystem>
#include <memory>

namespace dsl {

class CMakeSourceAcquirer : public SourceAcquirer {
public:
  explicit CMakeSourceAcquirer(
      std::filesystem::path build_directory = std::filesystem::path("build"),
      std::shared_ptr<Logger> logger = nullptr);
  SourceAcquisitionResult Acquire(const AnalysisConfig &config) override;

private:
  std::filesystem::path build_directory_;
  std::shared_ptr<Logger> logger_;
};

} // namespace dsl
