/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "components/FileLoader.h"

#include <cstddef>
#include <vector>

class At7Loader final : public FileLoader {
public:
  void apply(const RawFile* file) override;

private:
  static bool decompressSegment(const u8* input, size_t inputSize, std::vector<u8>& output);
};
