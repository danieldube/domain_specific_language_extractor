#include <dsl/cmake_source_acquirer.h>
#include <dsl/models.h>

#include <filesystem>
#include <stdexcept>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "test_support/temporary_project.h"

namespace dsl {
namespace {

class CMakeSourceAcquirerTest : public ::testing::Test {
protected:
  AnalysisConfig MakeConfig() const {
    return AnalysisConfig{.root_path = project_.root().string(),
                          .formats = {"markdown"}};
  }

  test::TemporaryProject project_;
  CMakeSourceAcquirer acquirer_;
};

TEST_F(CMakeSourceAcquirerTest, ProducesNormalizedFileList) {
  project_.AddFile("CMakeLists.txt", "cmake_minimum_required(VERSION 3.20)\n");
  const auto source_path =
      project_.AddFile("src/main.cpp", "int main() {return 0;}");
  const auto header_path =
      project_.AddFile("include/example.h", "void example();");
  const auto build_dir = project_.root() / "build";
  std::filesystem::create_directories(build_dir);
  project_.AddFile("build/generated.cpp", "int generated();");

  const auto result = acquirer_.Acquire(MakeConfig());

  EXPECT_EQ(result.project_root,
            std::filesystem::weakly_canonical(project_.root()).string());
  EXPECT_THAT(result.files,
              ::testing::UnorderedElementsAre(
                  std::filesystem::weakly_canonical(source_path).string(),
                  std::filesystem::weakly_canonical(header_path).string()));
}

TEST_F(CMakeSourceAcquirerTest, ThrowsWhenProjectIsNotCMakeBased) {
  project_.AddFile("src/example.cpp", "void example() {}");

  EXPECT_THROW(acquirer_.Acquire(MakeConfig()), std::runtime_error);
}

} // namespace
} // namespace dsl
