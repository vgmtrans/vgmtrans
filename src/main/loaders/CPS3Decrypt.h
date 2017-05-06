#pragma once

class CPS3Decrypt {

 public:
  static void cps3_decode(uint32_t *src, uint32_t *dest, uint32_t key, uint32_t key2, uint32_t length);

 private:
  static uint16_t rotate_left(uint16_t value, int n);
  static uint16_t rotxor(uint16_t val, uint16_t xorval);
  static uint32_t cps3_mask(uint32_t address, uint32_t key1, uint32_t key2);

};
