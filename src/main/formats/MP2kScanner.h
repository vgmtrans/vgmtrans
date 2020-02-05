/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "Scanner.h"
#include <optional>

/* Scanner for the MP2K (aka Sappy) GBA format */
class MP2kScanner final : public VGMScanner {
   public:
    MP2kScanner() = default;
    ~MP2kScanner() = default;

    void Scan(RawFile *file, void *info = 0) override;

   private:
    std::optional<size_t> DetectMP2K(RawFile *file);
    static bool IsValidOffset(uint32_t offset, uint32_t romsize);
    static bool IsGBAROMAddress(uint32_t address);
    static uint32_t GBAAddressToOffset(uint32_t address);
};
