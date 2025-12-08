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
      ".c",  ".cc",  ".cxx", ".cpp", ".h",   ".hh",
      ".hpp", ".hxx", ".ixx"};
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
         std::equal(parent.begin(), parent.end(),
                    normalized_candidate.begin());
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

std::filesystem::path ResolveBuildDirectory(const AnalysisConfig &config,
                                            const std::filesystem::path &root) {
  std::filesystem::path build_directory =
      config.build_directory.empty() ? root / "build"
                                     : std::filesystem::path(config.build_directory);

  if (!build_directory.is_absolute()) {
    build_directory = root / build_directory;
  }

  return std::filesystem::weakly_canonical(build_directory);
}

std::filesystem::path ResolveCompilationDatabase(
    const AnalysisConfig &config, const std::filesystem::path &root,
    const std::filesystem::path &build_dir) {
  const auto normalize = [&root](const std::filesystem::path &candidate) {
    if (candidate.is_absolute()) {
      return std::filesystem::weakly_canonical(candidate);
    }
    return std::filesystem::weakly_canonical(root / candidate);
  };

  if (!config.compilation_database_path.empty()) {
    const auto configured_path = normalize(config.compilation_database_path);
    if (!std::filesystem::exists(configured_path)) {
      throw std::runtime_error(
          "Configured compilation database does not exist: " +
          configured_path.string());
    }
    return configured_path;
  }

  const auto root_candidate = normalize("compile_commands.json");
  if (std::filesystem::exists(root_candidate)) {
    return root_candidate;
  }

  const auto build_candidate = normalize(build_dir / "compile_commands.json");
  if (std::filesystem::exists(build_candidate)) {
    return build_candidate;
  }

  throw std::runtime_error(
      "Compilation database not found in root or build directory.");
}

std::vector<std::string> CollectSourceFiles(
    const std::filesystem::path &root, const std::filesystem::path &build_dir) {
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

SourceAcquisitionResult
CMakeSourceAcquirer::Acquire(const AnalysisConfig &config) {
  const auto root = ResolveRootPath(config);
  RequireCMakeProject(root);
  const auto build_dir = ResolveBuildDirectory(config, root);
  const auto compilation_database =
      ResolveCompilationDatabase(config, root, build_dir);

  auto files = CollectSourceFiles(root, build_dir);
  if (files.empty()) {
    throw std::runtime_error("No source files found under root: " +
                             root.string());
  }

  SourceAcquisitionResult result;
  result.files = std::move(files);
  result.project_root = root.string();
  result.artifacts.emplace("compilation_database",
                           compilation_database.string());
  return result;
}

} // namespace dsl
