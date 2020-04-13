/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "Scanner.h"
#include "BytePattern.h"

class SoftCreatSnesScanner : public VGMScanner {
   public:
    virtual void Scan(RawFile *file, void *info = 0);
    void SearchForSoftCreatSnesFromARAM(RawFile *file);
    void SearchForSoftCreatSnesFromROM(RawFile *file);

   private:
    static BytePattern ptnLoadSeq;
    static BytePattern ptnVCmdExec;
};
