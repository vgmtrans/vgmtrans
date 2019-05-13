/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
 #if !defined(COMMON_H)
#define COMMON_H

#include "pch.h"
#include "helper.h"

#define VERSION "1.0.3"

#define KILOBYTE 1024
#define MEGABYTE (KILOBYTE*1024)
#define GIGABYTE (MEGABYTE*1024)

#define F_EPSILON 0.00001

#define FORWARD_DECLARE_TYPEDEF_STRUCT(type) \
    struct _##type;    \
    typedef _##type type

std::wstring StringToUpper(std::wstring myString);
std::wstring StringToLower(std::wstring myString);

//#define for_each(_ITER_, _COLL_) for (auto _ITER_ = _COLL_.begin(); \
//    _ITER_ != _COLL_.end(); _ITER_++)

/**
Converts a std::string to any class with a proper overload of the >> opertor
@param temp			The string to be converted
@param out	[OUT]	The container for the returned value
*/
template<class T>
void FromString(const std::string &temp, T *out) {
  std::istringstream val(temp);
  val >> *out;

  assert(!val.fail());
}

uint32_t StringToHex(const std::string &str);

std::wstring ConvertToSafeFileName(const std::wstring &str);

inline std::string wstring2string(std::wstring &wstr) {
  char *mbs = new char[wstr.length() * MB_CUR_MAX + 1];
  wcstombs(mbs, wstr.c_str(), wstr.length() * MB_CUR_MAX + 1);
  std::string str(mbs);
  delete[] mbs;
  return str;
}

inline std::wstring string2wstring(std::string &str) {
  wchar_t *wcs = new wchar_t[str.length() + 1];
  mbstowcs(wcs, str.c_str(), str.length() + 1);
  std::wstring wstr(wcs);
  delete[] wcs;
  return wstr;
}

//std::string WstringToString(std::wstring& wstr)
//{
//	std::stringstream stream;
//	stream << wstr;
//	return stream.str();
//}
//
//std::wstring StringToWstring(std::string& str)
//{
//	std::wostringstream stream;
//	stream << str;
//	return stream.str();
//}

inline int CountBytesOfVal(uint8_t *buf, uint32_t numBytes, uint8_t val) {
  int count = 0;
  for (uint32_t i = 0; i < numBytes; i++)
    if (buf[i] == val)
      count++;
  return count;
}

inline bool isEqual(float x, float y) {
  //const double epsilon = 0.00001/* some small number such as 1e-5 */;
  return std::abs(x - y) <= F_EPSILON * std::abs(x);
  // see Knuth section 4.2.2 pages 217-218
}

inline int roundi(double x) {
  return (x > 0) ? (int) (x + 0.5) : (int) (x - 0.5);
}

inline uint8_t pow7bit(uint8_t x, double y) {
  if (x > 127) {
    x = 127;
  }
  return roundi(pow(x / 127.0, y) * 127.0);
}

inline uint8_t sqrt7bit(uint8_t x) {
  if (x > 127) {
    x = 127;
  }
  return roundi(sqrt(x / 127.0) * 127.0);
}

struct SizeOffsetPair {
  uint32_t size;
  uint32_t offset;

  SizeOffsetPair() :
      size(0),
      offset(0) {
  }

  SizeOffsetPair(uint32_t offset, uint32_t size) :
      size(size),
      offset(offset) {
  }
};

wchar_t *GetFileWithBase(const wchar_t *f, const wchar_t *newfile);

#endif // !defined(COMMON_H)
