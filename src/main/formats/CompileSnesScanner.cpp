/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "CompileSnesInstr.h"
#include "CompileSnesSeq.h"

//; Super Puyo Puyo 2 SPC
// 08e6: e5 00 18  mov   a,$1800
// 08e9: c4 50     mov   $50,a
// 08eb: e5 01 18  mov   a,$1801
// 08ee: c4 51     mov   $51,a             ; set song table address to $50/1
// 08f0: e5 02 18  mov   a,$1802
// 08f3: c4 52     mov   $52,a
// 08f5: 60        clrc
// 08f6: 88 11     adc   a,#$11
// 08f8: c4 54     mov   $54,a
// 08fa: e5 03 18  mov   a,$1803
// 08fd: c4 53     mov   $53,a             ; set duration table address to $52/3
// 08ff: 88 00     adc   a,#$00
// 0901: c4 55     mov   $55,a             ; set percussion table address to $54/5
BytePattern CompileSnesScanner::ptnSetSongListAddress("\xe5\x00\x18\xc4\x50\xe5\x01\x18"
                                                      "\xc4\x51\xe5\x02\x18\xc4\x52\x60"
                                                      "\x88\x11\xc4\x54\xe5\x03\x18\xc4"
                                                      "\x53\x88\x00\xc4\x55",
                                                      "x??x?x??"
                                                      "x?x??x?x"
                                                      "xxx?x??x"
                                                      "?xxx?",
                                                      29);

void CompileSnesScanner::Scan(RawFile *file, void *info) {
    uint32_t nFileLength = file->size();
    if (nFileLength == 0x10000) {
        SearchForCompileSnesFromARAM(file);
    } else {
        SearchForCompileSnesFromROM(file);
    }
    return;
}

void CompileSnesScanner::SearchForCompileSnesFromARAM(RawFile *file) {
    CompileSnesVersion version = COMPILESNES_NONE;

    std::wstring basefilename = removeExtFromPath(file->name());
    std::wstring name = file->tag.HasTitle() ? file->tag.title : basefilename;

    // scan for table pointer initialize code
    uint32_t ofsSetSongListAddress;
    if (!file->SearchBytePattern(ptnSetSongListAddress, ofsSetSongListAddress)) {
        return;
    }

    // determine the header address of Compile SPC engine
    uint16_t addrEngineHeader = file->GetShort(ofsSetSongListAddress + 1);
    if (addrEngineHeader < 0x0100 && addrEngineHeader + 18 > 0x10000) {
        return;
    }

    // classify engine version
    uint8_t addrSongListReg = file->GetByte(ofsSetSongListAddress + 4);
    if (addrSongListReg == 0x4e) {
        if (file->GetShortBE(ofsSetSongListAddress - 4) == 0x280c) {  // and a,#$0c
            version = COMPILESNES_JAKICRUSH;
        } else {
            version = COMPILESNES_ALESTE;
        }
    } else {
        if (file->GetShortBE(ofsSetSongListAddress - 4) == 0x280c) {  // and a,#$0c
            version = COMPILESNES_SUPERPUYO;
        } else {
            version = COMPILESNES_STANDARD;
        }
    }

    // determine the song list address
    uint16_t addrSongList = file->GetShort(addrEngineHeader);

    // TODO: guess song index
    int8_t guessedSongIndex = -1;
    if (addrSongList + 4 <= 0x10000) {
        guessedSongIndex = 1;
    }

    uint32_t addrSongHeaderPtr = addrSongList + guessedSongIndex * 2;
    if (addrSongHeaderPtr + 2 <= 0x10000) {
        uint16_t addrSongHeader = file->GetShort(addrSongHeaderPtr);
        uint8_t numTracks = file->GetByte(addrSongHeader);
        if (numTracks > 0 && numTracks <= 8) {
            CompileSnesSeq *newSeq = new CompileSnesSeq(file, version, addrSongHeader, name);
            if (!newSeq->LoadVGMFile()) {
                delete newSeq;
                return;
            }
        }
    }

    uint16_t spcDirAddr = file->GetByte(addrEngineHeader + 0x0e) << 8;

    uint16_t addrTuningTable = file->GetShort(addrEngineHeader + 0x12);
    uint16_t addrPitchTablePtrs = 0;
    if (version != COMPILESNES_ALESTE && version != COMPILESNES_JAKICRUSH) {
        addrPitchTablePtrs = file->GetShort(addrEngineHeader + 0x16);
    }

    CompileSnesInstrSet *newInstrSet =
        new CompileSnesInstrSet(file, version, addrTuningTable, addrPitchTablePtrs, spcDirAddr);
    if (!newInstrSet->LoadVGMFile()) {
        delete newInstrSet;
        return;
    }
}

void CompileSnesScanner::SearchForCompileSnesFromROM(RawFile *file) {}
