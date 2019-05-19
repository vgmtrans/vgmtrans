/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "Scanner.h"
#include "BytePattern.h"

enum CompileSnesVersion : uint8_t;  // see CompileSnesFormat.h

class CompileSnesScanner : public VGMScanner {
   public:
    CompileSnesScanner(void) { USE_EXTENSION(L"spc"); }
    virtual ~CompileSnesScanner(void) {}

    virtual void Scan(RawFile *file, void *info = 0);
    void SearchForCompileSnesFromARAM(RawFile *file);
    void SearchForCompileSnesFromROM(RawFile *file);

   private:
    static BytePattern ptnSetSongListAddress;
};
