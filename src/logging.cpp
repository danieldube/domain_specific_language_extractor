#include <dsl/logging.h>

#include <chrono>
#include <ctime>
#include <iomanip>
#include <ostream>
#include <sstream>

namespace dsl {
namespace {
std::string LevelName(LogLevel level) {
  switch (level) {
  case LogLevel::kError:
    return "error";
  case LogLevel::kWarn:
    return "warn";
  case LogLevel::kInfo:
    return "info";
  case LogLevel::kDebug:
    return "debug";
  }
  return "unknown";
}

std::string Timestamp() {
  const auto now = std::chrono::system_clock::now();
  const auto time = std::chrono::system_clock::to_time_t(now);
  std::tm tm;
#ifdef _WIN32
  localtime_s(&tm, &time);
#else
  localtime_r(&time, &tm);
#endif
  std::ostringstream stream;
  stream << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S%z");
  return stream.str();
}

std::string FormatFields(
    const std::vector<std::pair<std::string, std::string>> &fields) {
  if (fields.empty()) {
    return "{}";
  }
  std::ostringstream stream;
  stream << "{";
  for (std::size_t i = 0; i < fields.size(); ++i) {
    if (i > 0) {
      stream << ", ";
    }
    stream << '"' << fields[i].first << '"' << ": " << '"'
           << fields[i].second << '"';
  }
  stream << "}";
  return stream.str();
}
} // namespace

StructuredLogger::StructuredLogger(std::ostream &stream, LoggingConfig config)
    : stream_(&stream), config_(config) {}

void StructuredLogger::Log(
    LogLevel level, std::string_view message,
    std::vector<std::pair<std::string, std::string>> fields) {
  if (!IsEnabled(level) || stream_ == nullptr) {
    return;
  }

  (*stream_) << "[" << Timestamp() << "] level=" << LevelName(level)
             << " message=\"" << message << "\" fields="
             << FormatFields(fields) << "\n";
}

std::shared_ptr<Logger> EnsureLogger(std::shared_ptr<Logger> logger) {
  if (!logger) {
    return std::make_shared<NullLogger>();
  }
  return logger;
}

std::shared_ptr<Logger> MakeLogger(const LoggingConfig &config,
                                   std::ostream &stream) {
  return std::make_shared<StructuredLogger>(stream, config);
}

} // namespace dsl
