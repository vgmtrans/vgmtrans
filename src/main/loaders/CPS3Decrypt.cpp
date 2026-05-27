#include "Types.h"
#include "util/Binary.h"
#include "CPS3Decrypt.h"

// The following code comes directly from MAME

u16 CPS3Decrypt::rotate_left(u16 value, int n)
{
  int aux = value>>(16-n);
  return ((value<<n)|aux)%0x10000;
}

u16 CPS3Decrypt::rotxor(u16 val, u16 xorval)
{
  u16 res = val + rotate_left(val,2);
  res = rotate_left(res,4) ^ (res & (val ^ xorval));

  return res;
}

u32 CPS3Decrypt::cps3_mask(u32 address, u32 key1, u32 key2)
{
  address ^= key1;

  u16 val = (address & 0xffff) ^ 0xffff;
  val = rotxor(val, key2 & 0xffff);
  val ^= (address >> 16) ^ 0xffff;
  val = rotxor(val, key2 >> 16);
  val ^= (address & 0xffff) ^ (key2 & 0xffff);

  return val | (val << 16);
}


void CPS3Decrypt::cps3_decode(const u32 *src, u32 *dest, u32 key1, u32 key2, u32 length) {
  for (int i = 0; i < length; i += 4) {
    u32 data = swap_bytes32(src[i / 4]) ^ cps3_mask(0x6000000 + i, key1, key2);
    dest[i / 4] = swap_bytes32(data);
  }
}
