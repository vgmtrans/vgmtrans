#if !defined(COMMON_H)
#define COMMON_H

#include "pch.h"
#include "helper.h"

#define KILOBYTE 1024
#define MEGABYTE (KILOBYTE*1024)
#define GIGABYTE (MEGABYTE*1024)

#define F_EPSILON 0.00001

#define FORWARD_DECLARE_TYPEDEF_STRUCT(type) \
    struct _##type;    \
    typedef _##type type

std::string StringToUpper(std::string myString);
std::string StringToLower(std::string myString);

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

std::string ConvertToSafeFileName(const std::string &str);

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

inline uint16_t swap_bytes16(uint16_t val) {
  return (val << 8) | (val >> 8);
}

inline uint32_t swap_bytes32(uint32_t val) {
  return ((val << 24) | ((val & 0x0000FF00) << 8) | ((val & 0x00FF0000) >> 8) | (val >> 24));
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

char* GetFileWithBase(const char* f, const char* newfile);


#endif // !defined(COMMON_H)
