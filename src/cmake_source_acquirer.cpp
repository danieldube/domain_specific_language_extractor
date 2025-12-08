#include <dsl/default_components.h>

#include <algorithm>
#include <filesystem>
#include <iterator>
#include <set>
#include <stdexcept>

namespace dsl {

namespace {
bool IsSourceExtension(const std::filesystem::path &path) {
  static const std::set<std::string> kExtensions = {
      ".c", ".cc", ".cxx", ".cpp", ".h", ".hh", ".hpp", ".hxx", ".ixx"};
  return kExtensions.count(path.extension().string()) > 0;
}

bool IsWithin(const std::filesystem::path &candidate,
              const std::filesystem::path &potential_parent) {
  if (potential_parent.empty()) {
    return false;
  }

  const auto parent = std::filesystem::weakly_canonical(potential_parent);
  const auto normalized_candidate =
      std::filesystem::weakly_canonical(candidate);

  return std::distance(parent.begin(), parent.end()) <=
             std::distance(normalized_candidate.begin(),
                           normalized_candidate.end()) &&
         std::equal(parent.begin(), parent.end(), normalized_candidate.begin());
}

std::filesystem::path ResolveRootPath(const AnalysisConfig &config) {
  if (config.root_path.empty()) {
    throw std::invalid_argument("AnalysisConfig.root_path must not be empty.");
  }

  const auto normalized_root =
      std::filesystem::weakly_canonical(config.root_path);

  if (!std::filesystem::exists(normalized_root) ||
      !std::filesystem::is_directory(normalized_root)) {
    throw std::runtime_error("Analysis root path is not a directory: " +
                             normalized_root.string());
  }

  return normalized_root;
}

void RequireCMakeProject(const std::filesystem::path &root) {
  const auto cmake_lists = root / "CMakeLists.txt";
  if (!std::filesystem::exists(cmake_lists)) {
    throw std::runtime_error("CMakeLists.txt not found in root: " +
                             root.string());
  }
}

std::vector<std::string>
CollectSourceFiles(const std::filesystem::path &root,
                   const std::filesystem::path &build_dir) {
  std::vector<std::string> files;

  for (std::filesystem::recursive_directory_iterator it(root), end; it != end;
       ++it) {
    const auto &entry = *it;
    const auto path = entry.path();

    if (entry.is_directory() && IsWithin(path, build_dir)) {
      it.disable_recursion_pending();
      continue;
    }

    if (!entry.is_regular_file()) {
      continue;
    }

    if (!IsSourceExtension(path)) {
      continue;
    }

    if (IsWithin(path, build_dir)) {
      continue;
    }

    files.push_back(std::filesystem::weakly_canonical(path).string());
  }

  std::sort(files.begin(), files.end());
  files.erase(std::unique(files.begin(), files.end()), files.end());
  return files;
}
} // namespace

CMakeSourceAcquirer::CMakeSourceAcquirer(std::filesystem::path build_directory)
    : build_directory_(std::move(build_directory)) {}

SourceAcquisitionResult
CMakeSourceAcquirer::Acquire(const AnalysisConfig &config) {
  const auto root = ResolveRootPath(config);
  RequireCMakeProject(root);

  auto build_dir = build_directory_;
  if (!build_dir.is_absolute()) {
    build_dir = root / build_dir;
  }
  build_dir = std::filesystem::weakly_canonical(build_dir);

  auto files = CollectSourceFiles(root, build_dir);
  if (files.empty()) {
    throw std::runtime_error("No source files found under root: " +
                             root.string());
  }

  SourceAcquisitionResult result;
  result.files = std::move(files);
  result.project_root = root.string();
  return result;
}

} // namespace dsl
