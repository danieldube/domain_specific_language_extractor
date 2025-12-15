#include <dsl/escaping.h>

#include <gtest/gtest.h>

namespace {

TEST(EscapingTest, EscapesControlCharacters) {
  const std::string input = "value\twith\ncontrols\\";
  const std::string escaped = dsl::Escape(input);

  EXPECT_EQ("value\\twith\\ncontrols\\\\", escaped);
}

TEST(EscapingTest, UnescapeRestoresEscapedSequences) {
  const std::string escaped = "value\\twith\\ncontrols\\\\";
  EXPECT_EQ("value\twith\ncontrols\\", dsl::Unescape(escaped));
}

TEST(EscapingTest, SplitEscapedHandlesLiteralTabs) {
  const std::string line =
      "first\tsecond\\twith\\nescaped\tthird\\\\segment";

  const auto fields = dsl::SplitEscaped(line);

  ASSERT_EQ(3u, fields.size());
  EXPECT_EQ("first", fields[0]);
  EXPECT_EQ("second\twith\nescaped", fields[1]);
  EXPECT_EQ("third\\segment", fields[2]);
}

TEST(EscapingTest, RoundTripPreservesContent) {
  const std::string original = "alpha\\bravo\tcharlie\ndelta";

  EXPECT_EQ(original, dsl::Unescape(dsl::Escape(original)));
}

} // namespace
