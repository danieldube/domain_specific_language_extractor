#include <dsl/escaping.h>

namespace dsl {

std::string Escape(const std::string &value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (const auto character : value) {
    if (character == '\\' || character == '\t' || character == '\n') {
      escaped.push_back('\\');
      if (character == '\t') {
        escaped.push_back('t');
        continue;
      }
      if (character == '\n') {
        escaped.push_back('n');
        continue;
      }
    }
    escaped.push_back(character);
  }
  return escaped;
}

std::string Unescape(const std::string &value) {
  std::string unescaped;
  unescaped.reserve(value.size());
  for (std::size_t i = 0; i < value.size(); ++i) {
    if (value[i] == '\\' && i + 1 < value.size()) {
      const auto next = value[i + 1];
      if (next == 't') {
        unescaped.push_back('\t');
        ++i;
        continue;
      }
      if (next == 'n') {
        unescaped.push_back('\n');
        ++i;
        continue;
      }
      unescaped.push_back(next);
      ++i;
      continue;
    }
    unescaped.push_back(value[i]);
  }
  return unescaped;
}

std::vector<std::string> SplitEscaped(const std::string &line) {
  std::vector<std::string> fields;
  std::string current;
  for (std::size_t i = 0; i < line.size(); ++i) {
    if (line[i] == '\t') {
      fields.push_back(Unescape(current));
      current.clear();
      continue;
    }
    current.push_back(line[i]);
  }
  fields.push_back(Unescape(current));
  return fields;
}

} // namespace dsl
