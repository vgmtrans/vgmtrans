#include "Text.h"

#include <algorithm>
#include <cctype>

std::string toUpper(const std::string& input) {
  std::string output = input;
  std::transform(output.begin(), output.end(), output.begin(),
                 [](unsigned char c) { return std::toupper(c); });
  return output;
}

std::string toLower(const std::string& input) {
  std::string output = input;
  std::transform(output.begin(), output.end(), output.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return output;
}
