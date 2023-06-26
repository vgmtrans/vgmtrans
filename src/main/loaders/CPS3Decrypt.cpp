#include "pch.h"
#include "common.h"
#include "CPS3Decrypt.h"

// The following code comes directly from MAME

uint16_t CPS3Decrypt::rotate_left(uint16_t value, int n)
{
  int aux = value>>(16-n);
  return ((value<<n)|aux)%0x10000;
}

uint16_t CPS3Decrypt::rotxor(uint16_t val, uint16_t xorval)
{
  uint16_t res;

  res = val + rotate_left(val,2);
  res = rotate_left(res,4) ^ (res & (val ^ xorval));

  return res;
}

uint32_t CPS3Decrypt::cps3_mask(uint32_t address, uint32_t key1, uint32_t key2)
{
  uint16_t val;

  address ^= key1;

  val = (address & 0xffff) ^ 0xffff;
  val = rotxor(val, key2 & 0xffff);
  val ^= (address >> 16) ^ 0xffff;
  val = rotxor(val, key2 >> 16);
  val ^= (address & 0xffff) ^ (key2 & 0xffff);

  return val | (val << 16);
}


void CPS3Decrypt::cps3_decode(uint32_t *src, uint32_t *dest, uint32_t key1, uint32_t key2, uint32_t length) {
  for (int i = 0; i < length; i += 4) {
    uint32_t data = swap_bytes32(src[i / 4]) ^ cps3_mask(0x6000000 + i, key1, key2);
    dest[i / 4] = swap_bytes32(data);
  }
}
