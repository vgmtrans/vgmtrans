/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "Scanner.h"

class NDSScanner : public VGMScanner {
   public:
    NDSScanner(void) {
        USE_EXTENSION("nds")
        USE_EXTENSION("sdat")
        USE_EXTENSION("2sf")
        USE_EXTENSION("mini2sf")
    }
    virtual ~NDSScanner(void) {}

    virtual void Scan(RawFile *file, void *info = 0);
    void SearchForSDAT(RawFile *file);
    uint32_t LoadFromSDAT(RawFile *file, uint32_t offset);
};
