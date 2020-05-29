/*
 * VGMCis (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "Scanner.h"

class TamSoftPS1Scanner : public VGMScanner {
   public:
    virtual void Scan(RawFile *file, void *info = 0);
};
