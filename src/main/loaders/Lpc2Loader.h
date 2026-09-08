/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "components/FileLoader.h"

#include <cstddef>
#include <vector>

class Lpc2Loader final : public FileLoader {
public:
  void apply(const RawFile* file) override;

private:
  static bool decompressLz10(const RawFile* file, std::vector<u8>& output);
  void unpackLpc2(const RawFile* source, const u8* data, size_t size);
};
