/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "components/FileLoader.h"

#include <cstddef>
#include <string>
#include <vector>

class At3Loader final : public FileLoader {
public:
  void apply(const RawFile* file) override;

private:
  static bool decodeMember(const RawFile* body, size_t offset, std::vector<u8>& output);
  static bool decompressSegment(const u8* input, size_t inputSize, const u8* controlFlags,
                                std::vector<u8>& output);
};
