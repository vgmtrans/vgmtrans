#include "PSDSEPS2Header.h"

#include <algorithm>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <iconv.h>
#endif

namespace PSDSEPS2 {

std::string decodeShiftJis(std::string_view input) {
  if (input.empty() || std::ranges::all_of(input, [](unsigned char value) { return value < 0x80; })) {
    return std::string(input);
  }

#ifdef _WIN32
  const int wideLength =
      MultiByteToWideChar(932, MB_ERR_INVALID_CHARS, input.data(), static_cast<int>(input.size()), nullptr, 0);
  if (wideLength <= 0) {
    return {};
  }
  std::wstring wide(static_cast<size_t>(wideLength), L'\0');
  if (MultiByteToWideChar(932, MB_ERR_INVALID_CHARS, input.data(), static_cast<int>(input.size()), wide.data(),
                          wideLength) != wideLength) {
    return {};
  }
  const int utf8Length = WideCharToMultiByte(CP_UTF8, 0, wide.data(), wideLength, nullptr, 0, nullptr, nullptr);
  if (utf8Length <= 0) {
    return {};
  }
  std::string output(static_cast<size_t>(utf8Length), '\0');
  if (WideCharToMultiByte(CP_UTF8, 0, wide.data(), wideLength, output.data(), utf8Length, nullptr, nullptr) !=
      utf8Length) {
    return {};
  }
  return output;
#else
  iconv_t converter = iconv_open("UTF-8", "CP932");
  if (converter == reinterpret_cast<iconv_t>(-1)) {
    converter = iconv_open("UTF-8", "SHIFT-JIS");
  }
  if (converter == reinterpret_cast<iconv_t>(-1)) {
    return {};
  }

  std::vector<char> buffer(input.size() * 4 + 1);
  char* inputCursor = const_cast<char*>(input.data());
  size_t inputRemaining = input.size();
  char* outputCursor = buffer.data();
  size_t outputRemaining = buffer.size();
  const bool failed =
      iconv(converter, &inputCursor, &inputRemaining, &outputCursor, &outputRemaining) == static_cast<size_t>(-1);
  iconv_close(converter);
  if (failed || inputRemaining != 0) {
    return {};
  }
  return {buffer.data(), static_cast<size_t>(outputCursor - buffer.data())};
#endif
}

}  // namespace PSDSEPS2
