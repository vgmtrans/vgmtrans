/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "Scanner.h"
#include "BytePattern.h"

class PrismSnesScanner : public VGMScanner {
   public:
    virtual void Scan(RawFile *file, void *info = 0);
    void SearchForPrismSnesFromARAM(RawFile *file);
    void SearchForPrismSnesFromROM(RawFile *file);

   private:
    static BytePattern ptnLoadSeq;
    static BytePattern ptnExecVCmd;
    static BytePattern ptnSetDSPd;
    static BytePattern ptnLoadInstr;
    static BytePattern ptnLoadInstrTuning;
};
