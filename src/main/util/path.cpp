#include "path.h"

#include <string>

std::string pathToUtf8String(const std::filesystem::path& path) {
#ifdef _WIN32
  // Avoid locale-dependent narrow conversions on Windows.
  const auto utf8 = path.u8string();
  return std::string(utf8.begin(), utf8.end());
#else
  return path.string();
#endif
}

std::filesystem::path makeSafeFileName(std::string_view s) {
#ifdef _WIN32
  std::u8string out;
  out.reserve(s.size());

  for (unsigned char c : s) {
    const bool bad = (c < 32) ||
      (c=='<'||c=='>'||c==':'||c=='"'||c=='/'||c=='\\'||c=='|'||c=='?'||c=='*');
    out.push_back(static_cast<char8_t>(bad ? '_' : c));
  }

  while (!out.empty() && (out.back() == u8' ' || out.back() == u8'.')) out.pop_back();
  if (out.empty()) out = u8"unnamed";

  // Interprets out as UTF-8 and converts to the native wide path internally.
  return std::filesystem::path(out);
#else

  std::string out;
  out.reserve(s.size());

  for (unsigned char c : s) {
    const bool bad = (c=='/') || (c==0);
    out.push_back(bad ? '_' : char(c));
  }

  if (out.empty()) out = "unnamed";
  return std::filesystem::path(out);
#endif
}
