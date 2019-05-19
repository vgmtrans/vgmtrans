/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
 





#include "common.h"
#include <cstring>

std::wstring StringToUpper(std::wstring myString) {
  const size_t length = myString.length();
  for (size_t i = 0; i != length; ++i) {
    myString[i] = toupper(myString[i]);
  }
  return myString;
}

std::wstring StringToLower(std::wstring myString) {
  const size_t length = myString.length();
  for (size_t i = 0; i != length; ++i) {
    myString[i] = tolower(myString[i]);
  }
  return myString;
}

uint32_t StringToHex(const std::string &str) {
  uint32_t value;
  std::stringstream convert(str);
  convert >> std::hex >> value;        //read seq_table as hexadecimal value
  return value;
}

std::wstring ConvertToSafeFileName(const std::wstring &str) {
  std::wstring filename;
  filename.reserve(str.length());

  const wchar_t *forbiddenChars = L"\\/:,;*?\"<>|";
  size_t pos_begin = 0;
  size_t pos_end;
  while ((pos_end = str.find_first_of(forbiddenChars, pos_begin)) != std::wstring::npos) {
    filename += str.substr(pos_begin, pos_end - pos_begin);
    if (filename[filename.length() - 1] != L' ') {
      filename += L" ";
    }
    pos_begin = pos_end + 1;
  }
  filename += str.substr(pos_begin);

  // right trim
  filename.erase(filename.find_last_not_of(L" \n\r\t") + 1);

  return filename;
}

wchar_t *GetFileWithBase(const wchar_t *f, const wchar_t *newfile) {
  static wchar_t *ret;
  wchar_t *tp1;

#if PSS_STYLE == 1
  tp1=((wchar_t *)wcsrchr (f,'/'));
#else
  tp1 = ((wchar_t *) std::wcsrchr(f, '\\'));
#if PSS_STYLE != 3
  {
    wchar_t *tp3;

    tp3 = ((wchar_t *) std::wcsrchr(f, '/'));
    if (tp1 < tp3) tp1 = tp3;
  }
#endif
#endif
  if (!tp1) {
    ret = (wchar_t *) malloc(wcslen(newfile) + 1);
    wcscpy(ret, newfile);
  }
  else {
    ret = (wchar_t *) malloc((tp1 - f + 2 + wcslen(newfile)) * sizeof(wchar_t));    // 1(NULL), 1(/).
    memcpy(ret, f, (tp1 - f) * sizeof(wchar_t));
    ret[tp1 - f] = L'/';
    ret[tp1 - f + 1] = 0;
    wcscat(ret, newfile);
  }
  return (ret);
}
