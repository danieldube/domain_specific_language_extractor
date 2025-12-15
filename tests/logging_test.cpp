#include <dsl/logging.h>

#include <gtest/gtest.h>

#include <iostream>
#include <memory>
#include <sstream>

namespace {

TEST(LoggingTest, RespectsLogLevelThreshold) {
  std::stringstream stream;
  dsl::StructuredLogger logger(stream, {dsl::LogLevel::kInfo});

  logger.Log(dsl::LogLevel::kDebug, "debug message", {});
  logger.Log(dsl::LogLevel::kInfo, "info message", {});

  const auto output = stream.str();
  EXPECT_EQ(std::string::npos, output.find("debug message"));
  EXPECT_NE(std::string::npos, output.find("level=info"));
  EXPECT_NE(std::string::npos, output.find("info message"));
}

TEST(LoggingTest, FormatsFieldsAsStructuredPairs) {
  std::stringstream stream;
  dsl::StructuredLogger logger(stream, {dsl::LogLevel::kDebug});

  logger.Log(dsl::LogLevel::kDebug, "operation.complete",
             {{"stage", "parse"}, {"duration_ms", "42"}});

  const auto output = stream.str();
  EXPECT_NE(std::string::npos, output.find("fields={\"stage\": \"parse\""));
  EXPECT_NE(std::string::npos, output.find("\"duration_ms\": \"42\"}"));
  EXPECT_NE(std::string::npos, output.find("message=\"operation.complete\""));
}

TEST(LoggingTest, EnsureLoggerProvidesDefault) {
  auto provided = dsl::EnsureLogger(nullptr);
  EXPECT_NE(nullptr, provided);
  EXPECT_NE(nullptr, std::dynamic_pointer_cast<dsl::NullLogger>(provided));

  auto custom =
      std::make_shared<dsl::StructuredLogger>(std::cout, dsl::LoggingConfig{});
  EXPECT_EQ(custom, dsl::EnsureLogger(custom));
}

} // namespace
