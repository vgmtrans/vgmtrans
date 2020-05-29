/*
 * VGMCis (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once
#include "Scanner.h"

class AkaoScanner final : public VGMScanner {
   public:
    AkaoScanner() = default;
    ~AkaoScanner() = default;

    void Scan(RawFile *file, void *info = 0) override;
};
