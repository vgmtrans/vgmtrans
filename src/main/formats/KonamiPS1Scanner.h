/*
 * VGMCis (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "Scanner.h"

class KonamiPS1Scanner : public VGMScanner {
   public:
    KonamiPS1Scanner() {}

    virtual ~KonamiPS1Scanner() {}

    virtual void Scan(RawFile *file, void *info = 0);
};
