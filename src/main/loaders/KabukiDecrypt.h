/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

class KabukiDecrypter {
   public:
    static void kabuki_decode(uint8_t *src, uint8_t *dest_op, uint8_t *dest_data, int base_addr,
                              int length, int swap_key1, int swap_key2, int addr_key, int xor_key);

   private:
    static int bitswap1(int src, int key, int select);
    static int bitswap2(int src, int key, int select);
    static int bytedecode(int src, int swap_key1, int swap_key2, int xor_key, int select);
};
