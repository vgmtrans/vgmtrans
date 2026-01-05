#include <string.h>
#include <algorithm>
#include <filesystem>
#include "common.h"

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

char* GetFileWithBase(const char* f, const char* newfile) {
  static char* ret;
  char *tp1;

#if PSS_STYLE == 1
  tp1 = ((char*)strrchr(f, '/'));
#else
  tp1 = ((char*)strrchr(f, '\\'));
#if PSS_STYLE != 3
  {
    char *tp3;

    tp3 = ((char*)strrchr(f, '/'));
    if (tp1 < tp3) {
      tp1 = tp3;
    }
  }
#endif
#endif
  if (!tp1) {
    ret = (char*)malloc(strlen(newfile) + 1);  // +1 for the null terminator
    strcpy(ret, newfile);
  } else {
    ret = (char*)malloc((tp1 - f + 2 + strlen(newfile)) * sizeof(char));  // 1 for the null terminator, 1 for the '/'.
    memcpy(ret, f, (tp1 - f) * sizeof(char));
    ret[tp1 - f] = '/';
    ret[tp1 - f + 1] = '\0';
    strcat(ret, newfile);
  }
  return ret;
}
