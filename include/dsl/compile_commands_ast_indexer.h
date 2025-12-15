#pragma once

#include <dsl/interfaces.h>
#include <dsl/logging.h>

#include <filesystem>
#include <memory>

namespace dsl {

class CompileCommandsAstIndexer : public AstIndexer {
public:
  explicit CompileCommandsAstIndexer(
      std::filesystem::path compile_commands_path = {},
      std::shared_ptr<Logger> logger = nullptr);
  AstIndex BuildIndex(const SourceAcquisitionResult &sources) override;

private:
  std::filesystem::path compile_commands_path_;
  std::shared_ptr<Logger> logger_;
};

} // namespace dsl
