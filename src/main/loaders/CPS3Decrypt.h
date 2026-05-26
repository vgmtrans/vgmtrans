#pragma once

#include "util/types.h"

class CPS3Decrypt {

public:
  static void cps3_decode(const u32 *src, u32 *dest, u32 key, u32 key2, u32 length);

private:
  static u16 rotate_left(u16 value, int n);
  static u16 rotxor(u16 val, u16 xorval);
  static u32 cps3_mask(u32 address, u32 key1, u32 key2);

};
