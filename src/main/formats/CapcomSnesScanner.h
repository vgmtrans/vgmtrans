/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "Scanner.h"
#include "BytePattern.h"

enum CapcomSnesVersion : uint8_t;  // see CapcomSnesFormat.h

class CapcomSnesScanner : public VGMScanner {
   public:
    CapcomSnesScanner(void) { USE_EXTENSION(L"spc"); }
    virtual ~CapcomSnesScanner(void) {}

    virtual void Scan(RawFile *file, void *info = 0);
    void SearchForCapcomSnesFromARAM(RawFile *file);
    void SearchForCapcomSnesFromROM(RawFile *file);

   private:
    int GetLengthOfSongList(RawFile *file, uint16_t addrSongList);
    uint16_t GetCurrentPlayAddressFromARAM(RawFile *file, CapcomSnesVersion version,
                                           uint8_t channel);
    int8_t GuessCurrentSongFromARAM(RawFile *file, CapcomSnesVersion version,
                                    uint16_t addrSongList);
    bool IsValidBGMHeader(RawFile *file, uint32_t addrSongHeader);
    std::map<uint8_t, uint8_t> GetInitDspRegMap(RawFile *file);

    static BytePattern ptnReadSongList;
    static BytePattern ptnReadBGMAddress;
    static BytePattern ptnDspRegInit;
    static BytePattern ptnDspRegInitOldVer;
    static BytePattern ptnLoadInstrTableAddress;
};
