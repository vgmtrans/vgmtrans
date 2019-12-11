/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "PandoraBoxSnesSeq.h"
#include "PandoraBoxSnesInstr.h"

// ; Kishin Kourinden Oni SPC
// f91d: 8d 10     mov   y,#$10
// f91f: fc        inc   y
// f920: f7 d3     mov   a,($d3)+y
// f922: dc        dec   y
// f923: 37 d3     and   a,($d3)+y
// f925: 68 ff     cmp   a,#$ff
// f927: f0 30     beq   $f959
BytePattern PandoraBoxSnesScanner::ptnLoadSeqKKO("\x8d\x10\xfc\xf7\xd3\xdc\x37\xd3"
                                                 "\x68\xff\xf0\x30",
                                                 "xxxx?xx?"
                                                 "xxx?",
                                                 12);

// ; Traverse: Starlight and Prairie SPC
// f96f: 8d 10     mov   y,#$10
// f971: 7d        mov   a,x
// f972: f0 45     beq   $f9b9
// f974: 6d        push  y
// f975: f7 08     mov   a,($08)+y
// f977: fc        inc   y
// f978: c4 00     mov   $00,a
// f97a: f7 08     mov   a,($08)+y
// f97c: fc        inc   y
// f97d: c4 01     mov   $01,a
// f97f: bc        inc   a
// f980: f0 31     beq   $f9b3
BytePattern PandoraBoxSnesScanner::ptnLoadSeqTSP("\x8d\x10\x7d\xf0\x45\x6d\xf7\x08"
                                                 "\xfc\xc4\x00\xf7\x08\xfc\xc4\x01"
                                                 "\xbc\xf0\x31",
                                                 "xxxx?xx?"
                                                 "xx?x?xx?"
                                                 "xx?",
                                                 19);

//; Gakkou de atta Kowai Hanashi SPC
// f9d8: e8 fb     mov   a,#$fb
// f9da: 8d 5d     mov   y,#$5d
// f9dc: 61        tcall 6                 ; write dsp DIR
BytePattern PandoraBoxSnesScanner::ptnSetDIR("\xe8\xfb\x8d\x5d\x61", "x?xxx", 5);

//; Gakkou de atta Kowai Hanashi SPC
// f664: 8d 0c     mov   y,#$0c
// f666: 60        clrc
// f667: 97 d9     adc   a,($d9)+y         ; add offset to instrument #
// f669: fd        mov   y,a               ; use it for index
// f66a: f7 d9     mov   a,($d9)+y         ; get global instrument number
// f66c: ec c3 01  mov   y,$01c3
// f66f: f0 07     beq   $f678
// f671: 76 3f 01  cmp   a,$013f+y         ; search for global instrument number
// f674: f0 03     beq   $f679
// f676: fe f9     dbnz  y,$f671
// f678: fc        inc   y
// f679: dc        dec   y                 ; SRCN is index of the global instrument number (0 if not
// found)
BytePattern PandoraBoxSnesScanner::ptnLoadSRCN("\x8d\x0c\x60\x97\xd9\xfd\xf7\xd9"
                                               "\xec\xc3\x01\xf0\x07\x76\x3f\x01"
                                               "\xf0\x03\xfe\xf9\xfc\xdc",
                                               "xxxx?xx?"
                                               "x??xxx??"
                                               "xxxxxx",
                                               22);

void PandoraBoxSnesScanner::Scan(RawFile *file, void *info) {
    uint32_t nFileLength = file->size();
    if (nFileLength == 0x10000) {
        SearchForPandoraBoxSnesFromARAM(file);
    } else {
        SearchForPandoraBoxSnesFromROM(file);
    }
    return;
}

void PandoraBoxSnesScanner::SearchForPandoraBoxSnesFromARAM(RawFile *file) {
    PandoraBoxSnesVersion version = PANDORABOXSNES_NONE;
    std::string name =
        file->tag.HasTitle() ? file->tag.title : removeExtFromPath(file->name());

    uint32_t ofsLoadSeq;
    uint16_t addrSeqHeader;
    if (file->SearchBytePattern(ptnLoadSeqTSP, ofsLoadSeq)) {
        uint8_t addrSeqHeaderPtr = file->GetByte(ofsLoadSeq + 7);
        addrSeqHeader = file->GetShort(addrSeqHeaderPtr);
        version = PANDORABOXSNES_V2;
    } else if (file->SearchBytePattern(ptnLoadSeqKKO, ofsLoadSeq)) {
        uint8_t addrSeqHeaderPtr = file->GetByte(ofsLoadSeq + 4);
        addrSeqHeader = file->GetShort(addrSeqHeaderPtr);
        version = PANDORABOXSNES_V1;
    } else {
        return;
    }

    PandoraBoxSnesSeq *newSeq = new PandoraBoxSnesSeq(file, version, addrSeqHeader, name);
    if (!newSeq->LoadVGMFile()) {
        delete newSeq;
        return;
    }

    // scan for DIR address
    uint16_t spcDirAddr;
    uint32_t ofsSetDIR;
    if (file->SearchBytePattern(ptnSetDIR, ofsSetDIR)) {
        spcDirAddr = file->GetByte(ofsSetDIR + 1) << 8;
    } else {
        return;
    }

    // scan for instrument table
    uint32_t ofsLoadSRCN;
    uint16_t addrLocalInstrTable;
    uint16_t addrGlobalInstrTable;
    uint8_t globalInstrumentCount;
    if (file->SearchBytePattern(ptnLoadSRCN, ofsLoadSRCN)) {
        uint8_t ofsLocalInstrTable = file->GetByte(addrSeqHeader + 12);
        if (addrSeqHeader + ofsLocalInstrTable >= 0x10000) {
            return;
        }
        addrLocalInstrTable = addrSeqHeader + ofsLocalInstrTable;

        addrGlobalInstrTable = file->GetShort(ofsLoadSRCN + 14) + 1;

        uint16_t availInstrCountPtr = file->GetShort(ofsLoadSRCN + 9);
        globalInstrumentCount = file->GetByte(availInstrCountPtr);
    } else {
        return;
    }

    PandoraBoxSnesInstrSet *newInstrSet = new PandoraBoxSnesInstrSet(
        file, version, spcDirAddr, addrLocalInstrTable, addrGlobalInstrTable, globalInstrumentCount,
        newSeq->instrADSRHints);
    if (!newInstrSet->LoadVGMFile()) {
        delete newInstrSet;
        return;
    }
}

void PandoraBoxSnesScanner::SearchForPandoraBoxSnesFromROM(RawFile *file) {}
