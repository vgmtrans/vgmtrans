#include "pch.h"
#include "common.h"

std::string StringToUpper(std::string myString) {
  const size_t length = myString.length();
  for (size_t i = 0; i != length; ++i) {
    myString[i] = toupper(myString[i]);
  }
  return myString;
}

std::string StringToLower(std::string myString) {
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

std::string ConvertToSafeFileName(const std::string &str) {
  std::string filename;
  filename.reserve(str.length());

  const char* forbiddenChars = "\\/:,;*?\"<>|";
  size_t pos_begin = 0;
  size_t pos_end;
  while ((pos_end = str.find_first_of(forbiddenChars, pos_begin)) != std::string::npos) {
    filename += str.substr(pos_begin, pos_end - pos_begin);
    if (filename[filename.length() - 1] != L' ') {
      filename += " ";
    }
    pos_begin = pos_end + 1;
  }
  filename += str.substr(pos_begin);

  // right trim
  filename.erase(filename.find_last_not_of(" \n\r\t") + 1);

  return filename;
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