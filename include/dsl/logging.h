#pragma once

#include <iosfwd>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dsl {

enum class LogLevel { kError = 0, kWarn = 1, kInfo = 2, kDebug = 3 };

struct LoggingConfig {
  LogLevel level = LogLevel::kError;
};

class Logger {
public:
  virtual ~Logger() = default;
  virtual void Log(LogLevel level, std::string_view message,
                   std::vector<std::pair<std::string, std::string>> fields =
                       {}) = 0;
  virtual LogLevel Level() const = 0;
  bool IsEnabled(LogLevel level) const { return static_cast<int>(level) <=
                                                static_cast<int>(Level()); }
};

class NullLogger : public Logger {
public:
  void Log(LogLevel, std::string_view,
           std::vector<std::pair<std::string, std::string>>) override {}
  LogLevel Level() const override { return LogLevel::kError; }
};

class StructuredLogger : public Logger {
public:
  StructuredLogger(std::ostream &stream, LoggingConfig config);
  void Log(LogLevel level, std::string_view message,
           std::vector<std::pair<std::string, std::string>> fields) override;
  LogLevel Level() const override { return config_.level; }

private:
  std::ostream *stream_;
  LoggingConfig config_;
};

std::shared_ptr<Logger> MakeLogger(const LoggingConfig &config,
                                   std::ostream &stream);

} // namespace dsl
