#ifndef DSL_TEST_SUPPORT_TEMPORARY_PROJECT_H
#define DSL_TEST_SUPPORT_TEMPORARY_PROJECT_H

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace dsl {
namespace test {

class TemporaryProject {
public:
  TemporaryProject() {
    const auto timestamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    root_ = std::filesystem::temp_directory_path() /
            ("dsl-acquirer-" + std::to_string(timestamp));
    std::filesystem::create_directories(root_);
  }

  ~TemporaryProject() { std::filesystem::remove_all(root_); }

  std::filesystem::path AddFile(const std::filesystem::path &relative,
                                const std::string &content = "") const {
    const auto full_path = root_ / relative;
    std::filesystem::create_directories(full_path.parent_path());
    std::ofstream stream(full_path);
    stream << content;
    return full_path;
  }

  const std::filesystem::path &root() const { return root_; }

private:
  std::filesystem::path root_;
};

} // namespace test
} // namespace dsl

#endif // DSL_TEST_SUPPORT_TEMPORARY_PROJECT_H
