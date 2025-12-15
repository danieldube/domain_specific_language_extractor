#pragma once

#include <string>
#include <vector>

namespace dsl {

std::string Escape(const std::string &value);
std::string Unescape(const std::string &value);
std::vector<std::string> SplitEscaped(const std::string &line);

} // namespace dsl
