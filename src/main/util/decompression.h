/*
 * VGMTrans (c) 2002-2020
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#define ZLIB_CONST
#include <zlib.h>
#include <stdexcept>
#include <array>

template <typename T>
std::vector<unsigned char> zdecompress(T src) {
  std::vector<unsigned char> result;

  /* allocate inflate state */
  z_stream strm;
  strm.zalloc = Z_NULL;
  strm.zfree = Z_NULL;
  strm.opaque = Z_NULL;
  strm.avail_in = 0;
  strm.next_in = Z_NULL;
  int ret = inflateInit(&strm);
  if (ret != Z_OK) {
    throw std::runtime_error("Failed to init decompression");
  }

  strm.avail_in = static_cast<uint32_t>(src.size());
  strm.next_in = reinterpret_cast<z_const Bytef *>(src.data());

  unsigned actual_size = 0;
  constexpr int CHUNK = 4096;
  std::array<unsigned char, CHUNK> out;
  do {
    strm.avail_out = CHUNK;
    strm.next_out = reinterpret_cast<Bytef *>(out.data());
    ret = inflate(&strm, Z_NO_FLUSH);

    assert(ret != Z_STREAM_ERROR);

    switch (ret) {
      case Z_NEED_DICT:
        [[fallthrough]];
      case Z_DATA_ERROR:
      case Z_MEM_ERROR:
        (void)inflateEnd(&strm);
        throw std::runtime_error("Decompression failed");
    }

    actual_size = CHUNK - strm.avail_out;
    result.insert(result.end(), out.begin(), out.begin() + actual_size);
  } while (strm.avail_out == 0);

  assert(ret == Z_STREAM_END);

  (void)inflateEnd(&strm);
  return result;
}
