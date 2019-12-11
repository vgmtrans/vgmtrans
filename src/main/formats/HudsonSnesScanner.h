/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "Scanner.h"
#include "BytePattern.h"

enum HudsonSnesVersion : uint8_t;  // see HudsonSnesFormat.h

class HudsonSnesScanner : public VGMScanner {
   public:
    HudsonSnesScanner(void) { USE_EXTENSION("spc"); }
    virtual ~HudsonSnesScanner(void) {}

    virtual void Scan(RawFile *file, void *info = 0);
    void SearchForHudsonSnesFromARAM(RawFile *file);
    void SearchForHudsonSnesFromROM(RawFile *file);

   private:
    static BytePattern ptnNoteLenTable;
    static BytePattern ptnGetSeqTableAddrV0;
    static BytePattern ptnGetSeqTableAddrV1V2;
    static BytePattern ptnLoadTrackAddress;
    static BytePattern ptnLoadDIRV0;
};
