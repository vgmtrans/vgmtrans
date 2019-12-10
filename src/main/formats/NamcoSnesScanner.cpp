/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "NamcoSnesInstr.h"
#include "NamcoSnesSeq.h"

// Wagan Paradise SPC
// 05cc: 68 60     cmp   a,#$60
// 05ce: b0 0a     bcs   $05da
// 05d0: cd 18     mov   x,#$18
// 05d2: d8 3c     mov   $3c,x
// 05d4: cd dc     mov   x,#$dc
// 05d6: d8 3d     mov   $3d,x             ; $dc18 - song list
// 05d8: 2f 0a     bra   $05e4
// 05da: cd 00     mov   x,#$00
// 05dc: d8 3c     mov   $3c,x
// 05de: cd f4     mov   x,#$f4
// 05e0: d8 3d     mov   $3d,x             ; $f400 - song list (sfx?)
// 05e2: 28 1f     and   a,#$1f
// 05e4: 8d 03     mov   y,#$03
// 05e6: cf        mul   ya
// 05e7: fd        mov   y,a
// 05e8: f7 3c     mov   a,($3c)+y         ; read slot index (priority?)
// 05ea: 1c        asl   a
BytePattern NamcoSnesScanner::ptnReadSongList("\x68\x60\xb0\x0a\xcd\x18\xd8\x3c"
                                              "\xcd\xdc\xd8\x3d\x2f\x0a\xcd\x00"
                                              "\xd8\x3c\xcd\xf4\xd8\x3d\x28\x1f"
                                              "\x8d\x03\xcf\xfd\xf7\x3c\x1c",
                                              "x?xxx?x?"
                                              "x?x?xxx?"
                                              "x?x?x?xx"
                                              "xxxxx?x",
                                              31);

//; Wagan Paradise SPC
// 0703: cd 00     mov   x,#$00
// 0705: d8 df     mov   $df,x
// 0707: d8 da     mov   $da,x
// 0709: d8 db     mov   $db,x
// 070b: d8 dc     mov   $dc,x
// 070d: f4 49     mov   a,$49+x
// 070f: c4 d6     mov   $d6,a             ; song number
// 0711: d8 de     mov   $de,x             ; slot index (0~3?)
// 0713: f0 05     beq   $071a
// 0715: 10 4a     bpl   $0761
// 0717: 5f 0a 09  jmp   $090a
BytePattern NamcoSnesScanner::ptnStartSong("\xcd\x00\xd8\xdf\xd8\xda\xd8\xdb"
                                           "\xd8\xdc\xf4\x49\xc4\xd6\xd8\xde"
                                           "\xf0\x05\x10\x4a\x5f\x0a\x09",
                                           "x?x?x?x?"
                                           "x?x?x?x?"
                                           "xxx?x??",
                                           23);

//; Wagan Paradise SPC
// 0d6d: f8 df     mov   x,$df
// 0d6f: f5 00 02  mov   a,$0200+x         ; SRCN index
// 0d72: 1c        asl   a
// 0d73: 60        clrc
// 0d74: 88 e0     adc   a,#$e0
// 0d76: c4 3c     mov   $3c,a
// 0d78: e8 11     mov   a,#$11
// 0d7a: 88 00     adc   a,#$00
// 0d7c: c4 3d     mov   $3d,a             ; $3c/d = $11e0 + (srcn * 2)
// 0d7e: 8d 00     mov   y,#$00
// 0d80: f7 3c     mov   a,($3c)+y
// 0d82: c4 3e     mov   $3e,a
// 0d84: fc        inc   y
// 0d85: f7 3c     mov   a,($3c)+y
// 0d87: c4 3d     mov   $3d,a             ; read per-instrument tuning
BytePattern NamcoSnesScanner::ptnLoadInstrTuning("\xf8\xdf\xf5\x00\x02\x1c\x60\x88"
                                                 "\xe0\xc4\x3c\xe8\x11\x88\x00\xc4"
                                                 "\x3d\x8d\x00\xf7\x3c\xc4\x3e\xfc"
                                                 "\xf7\x3c\xc4\x3d",
                                                 "x?x??xxx"
                                                 "?x?x?x?x"
                                                 "?xxx?x?x"
                                                 "x?x?",
                                                 28);

//; Wagan Paradise SPC
// 056c: cd 00     mov   x,#$00
// 056e: f5 d3 09  mov   a,$09d3+x
// 0571: fd        mov   y,a
// 0572: 3d        inc   x
// 0573: f5 d3 09  mov   a,$09d3+x
// 0576: 3f f7 09  call  $09f7             ; reset DSP
// 0579: 3d        inc   x
// 057a: c8 18     cmp   x,#$18
// 057c: d0 f0     bne   $056e
BytePattern NamcoSnesScanner::ptnDspRegInit("\xcd\x00\xf5\xd3\x09\xfd\x3d\xf5"
                                            "\xd3\x09\x3f\xf7\x09\x3d\xc8\x18"
                                            "\xd0\xf0",
                                            "xxx??xxx"
                                            "??x??xx?"
                                            "xx",
                                            18);

void NamcoSnesScanner::Scan(RawFile *file, void *info) {
    uint32_t nFileLength = file->size();
    if (nFileLength == 0x10000) {
        SearchForNamcoSnesFromARAM(file);
    } else {
        SearchForNamcoSnesFromROM(file);
    }
    return;
}

void NamcoSnesScanner::SearchForNamcoSnesFromARAM(RawFile *file) {
    NamcoSnesVersion version = NAMCOSNES_NONE;
    std::wstring name =
        file->tag.HasTitle() ? file->tag.title : RawFile::removeExtFromPath(file->name());

    // search song list
    uint32_t ofsReadSongList;
    uint16_t addrSongList;
    if (file->SearchBytePattern(ptnReadSongList, ofsReadSongList)) {
        addrSongList =
            file->GetByte(ofsReadSongList + 5) | (file->GetByte(ofsReadSongList + 9) << 8);
        version = NAMCOSNES_STANDARD;
    } else {
        return;
    }

    // search song start sequence
    uint32_t ofsStartSong;
    uint8_t addrSongIndexArray;
    uint8_t addrSongSlotIndex;
    if (file->SearchBytePattern(ptnStartSong, ofsStartSong)) {
        addrSongIndexArray = file->GetByte(ofsStartSong + 11);
        addrSongSlotIndex = file->GetByte(ofsStartSong + 15);
    } else {
        return;
    }

    // determine song index
    uint8_t songSlot = file->GetByte(addrSongSlotIndex);  // 0..3
    uint8_t songIndex = file->GetByte(addrSongIndexArray + songSlot);
    uint16_t addrSeqHeader = addrSongList + (songIndex * 3);
    if (addrSeqHeader + 3 < 0x10000) {
        if (file->GetByte(addrSeqHeader) > 3 || (file->GetShort(addrSeqHeader + 1) & 0xff00) == 0) {
            songIndex = 1;
        }
        addrSeqHeader = addrSongList + (songIndex * 3);
    }
    if (addrSeqHeader + 3 > 0x10000) {
        return;
    }

    uint16_t addrEventStart = file->GetShort(addrSeqHeader + 1);
    if (addrEventStart + 1 > 0x10000) {
        return;
    }

    NamcoSnesSeq *newSeq = new NamcoSnesSeq(file, version, addrEventStart, name);
    if (!newSeq->LoadVGMFile()) {
        delete newSeq;
        return;
    }

    uint32_t ofsLoadInstrTuning;
    uint16_t addrTuningTable;
    if (file->SearchBytePattern(ptnLoadInstrTuning, ofsLoadInstrTuning)) {
        addrTuningTable =
            file->GetByte(ofsLoadInstrTuning + 8) | (file->GetByte(ofsLoadInstrTuning + 12) << 8);
    } else {
        return;
    }

    std::map<uint8_t, uint8_t> dspRegMap = GetInitDspRegMap(file);
    if (dspRegMap.count(0x5d) == 0) {
        return;
    }
    uint16_t spcDirAddr = dspRegMap[0x5d] << 8;

    NamcoSnesInstrSet *newInstrSet =
        new NamcoSnesInstrSet(file, version, spcDirAddr, addrTuningTable);
    if (!newInstrSet->LoadVGMFile()) {
        delete newInstrSet;
        return;
    }
}

void NamcoSnesScanner::SearchForNamcoSnesFromROM(RawFile *file) {}

std::map<uint8_t, uint8_t> NamcoSnesScanner::GetInitDspRegMap(RawFile *file) {
    std::map<uint8_t, uint8_t> dspRegMap;

    // find a code block which initializes dsp registers
    uint32_t ofsDspRegInit;
    uint8_t dspRegCount;
    uint16_t addrDspRegValueList;
    if (file->SearchBytePattern(ptnDspRegInit, ofsDspRegInit)) {
        dspRegCount = file->GetByte(ofsDspRegInit + 15) / 2;
        addrDspRegValueList = file->GetShort(ofsDspRegInit + 3);
    } else {
        return dspRegMap;
    }

    // check address range
    if (addrDspRegValueList + (dspRegCount * 2) > 0x10000) {
        return dspRegMap;
    }

    // store dsp reg/value pairs to map
    for (uint8_t regIndex = 0; regIndex < dspRegCount; regIndex++) {
        uint8_t dspReg = file->GetByte(addrDspRegValueList + (regIndex * 2));
        uint8_t dspValue = file->GetByte(addrDspRegValueList + (regIndex * 2) + 1);
        dspRegMap[dspReg] = dspValue;
    }

    return dspRegMap;
}
