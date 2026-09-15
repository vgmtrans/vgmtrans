/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "components/FileLoader.h"

#include <cstddef>
#include <vector>

class DarcLoader final : public FileLoader {
public:
  void apply(const RawFile* file) override;

private:
  void unpackDarc(const RawFile* file);
  void unpackDenc(const RawFile* file);

  static bool decodeDenc(const RawFile* file, size_t offset, size_t size, std::vector<u8>& decoded);
  static bool decompressLzss(const u8* input, size_t inputSize, size_t expectedSize, std::vector<u8>& output);
};
