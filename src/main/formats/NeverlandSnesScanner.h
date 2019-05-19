/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "Scanner.h"
#include "BytePattern.h"

class NeverlandSnesScanner : public VGMScanner {
   public:
    NeverlandSnesScanner(void) { USE_EXTENSION(L"spc"); }
    virtual ~NeverlandSnesScanner(void) {}

    virtual void Scan(RawFile *file, void *info = 0);
    void SearchForNeverlandSnesFromARAM(RawFile *file);
    void SearchForNeverlandSnesFromROM(RawFile *file);

   private:
    static BytePattern ptnLoadSongSFC;
    static BytePattern ptnLoadSongS2C;
};
