/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "RareSnesSeq.h"
#include "RareSnesInstr.h"
#include "SNESDSP.h"

using namespace std;

// ; Load DIR address
// 10df: 8f 5d f2  mov   $f2,#$5d
// 10e2: 8f 32 f3  mov   $f3,#$32
BytePattern RareSnesScanner::ptnLoadDIR("\x8f\x5d\xf2\x8f\x32\xf3", "xxxx?x", 6);

//; Donkey Kong Country SPC
// 0ac0: 4d        push  x
// 0ac1: f7 01     mov   a,($01)+y
// 0ac3: 5d        mov   x,a
// 0ac4: f5 e0 04  mov   a,$04e0+x         ; read SRCN table
// 0ac7: ce        pop   x
BytePattern RareSnesScanner::ptnReadSRCNTable("\x4d\xf7\x01\x5d\xf5\xe0\x04\xce", "xx?xx??x", 8);

//; Donkey Kong Country SPC
// 1123: e8 01     mov   a,#$01
// 1125: d4 3c     mov   $3c+x,a
// 1127: d5 10 01  mov   $0110+x,a
// 112a: f6 a0 12  mov   a,$12a0+y
// 112d: d4 4c     mov   $4c+x,a
// 112f: f6 a1 12  mov   a,$12a1+y
// 1132: d4 5c     mov   $5c+x,a           ; set pointer for each track
BytePattern RareSnesScanner::ptnSongLoadDKC("\xe8\x01\xd4\x3c\xd5\x10\x01\xf6"
                                            "\xa0\x12\xd4\x4c\xf6\xa1\x12\xd4"
                                            "\x5c",
                                            "xxx?x??x"
                                            "??x?x??x"
                                            "?",
                                            17);

//; Donkey Kong Country 2 SPC
// 10a9: e8 01     mov   a,#$01
// 10ab: d4 34     mov   $34+x,a
// 10ad: d5 10 01  mov   $0110+x,a
// 10b0: f7 e5     mov   a,($e5)+y
// 10b2: d4 44     mov   $44+x,a
// 10b4: fc        inc   y
// 10b5: f7 e5     mov   a,($e5)+y
// 10b7: d4 54     mov   $54+x,a           ; set pointer for each track
BytePattern RareSnesScanner::ptnSongLoadDKC2("\xe8\x01\xd4\x34\xd5\x10\x01\xf7"
                                             "\xe5\xd4\x44\xfc\xf7\xe5\xd4\x54",
                                             "xxx?x??x"
                                             "?x?xx?x?",
                                             16);

//; Donkey Kong Country SPC
// 078e: 8d 00     mov   y,#$00
// 0790: f7 01     mov   a,($01)+y
// 0792: 68 00     cmp   a,#$00
// 0794: 30 06     bmi   $079c
// 0796: 4d        push  x
// 0797: 1c        asl   a
// 0798: 5d        mov   x,a
// 0799: 1f 0f 10  jmp   ($100f+x)
BytePattern RareSnesScanner::ptnVCmdExecDKC("\x8d\x00\xf7\x01\x68\x00\x30\x06"
                                            "\x4d\x1c\x5d\x1f\x0f\x10",
                                            "xxx?xxxx"
                                            "xxxx??",
                                            14);

//;Donkey Kong Country 2 SPC
// 0856: 8d 00     mov   y,#$00
// 0858: f7 00     mov   a,($00)+y
// 085a: 30 06     bmi   $0862
// 085c: 4d        push  x
// 085d: 1c        asl   a
// 085e: 5d        mov   x,a
// 085f: 1f a5 0f  jmp   ($0fa5+x)
BytePattern RareSnesScanner::ptnVCmdExecDKC2("\x8d\x00\xf7\x00\x30\x06\x4d\x1c"
                                             "\x5d\x1f\xa5\x0f",
                                             "xxx?xxxx"
                                             "xx??",
                                             12);

void RareSnesScanner::Scan(RawFile *file, void *info) {
    uint32_t nFileLength = file->size();
    if (nFileLength == 0x10000) {
        SearchForRareSnesFromARAM(file);
    } else {
        SearchForRareSnesFromROM(file);
    }
    return;
}

void RareSnesScanner::SearchForRareSnesFromARAM(RawFile *file) {
    RareSnesVersion version = RARESNES_NONE;
    uint32_t ofsSongLoadASM;
    uint32_t ofsVCmdExecASM;
    uint32_t addrSeqHeader;
    uint32_t addrVCmdTable;
    wstring name =
        file->tag.HasTitle() ? file->tag.title : RawFile::removeExtFromPath(file->GetFileName());

    // find a sequence
    if (file->SearchBytePattern(ptnSongLoadDKC2, ofsSongLoadASM)) {
        addrSeqHeader = file->GetShort(file->GetByte(ofsSongLoadASM + 8));
    } else if (file->SearchBytePattern(ptnSongLoadDKC, ofsSongLoadASM) &&
               file->GetShort(ofsSongLoadASM + 13) == file->GetShort(ofsSongLoadASM + 8) + 1) {
        addrSeqHeader = file->GetShort(ofsSongLoadASM + 8);
    } else {
        return;
    }

    // guess engine version
    if (file->SearchBytePattern(ptnVCmdExecDKC2, ofsVCmdExecASM)) {
        addrVCmdTable = file->GetShort(ofsVCmdExecASM + 10);
        if (file->GetShort(addrVCmdTable + (0x0c * 2)) != 0) {
            if (file->GetShort(addrVCmdTable + (0x11 * 2)) != 0) {
                version = RARESNES_WNRN;
            } else {
                version = RARESNES_DKC2;
            }
        } else {
            version = RARESNES_KI;
        }
    } else if (file->SearchBytePattern(ptnVCmdExecDKC, ofsVCmdExecASM)) {
        addrVCmdTable = file->GetShort(ofsVCmdExecASM + 12);
        version = RARESNES_DKC;
    } else {
        return;
    }

    // load sequence
    RareSnesSeq *newSeq = new RareSnesSeq(file, version, addrSeqHeader, name);
    if (!newSeq->LoadVGMFile()) {
        delete newSeq;
        return;
    }

    // Rare engine has a instrument # <--> SRCN # table, find it
    uint32_t ofsReadSRCNASM;
    if (!file->SearchBytePattern(ptnReadSRCNTable, ofsReadSRCNASM)) {
        return;
    }
    uint32_t addrSRCNTable = file->GetShort(ofsReadSRCNASM + 5);
    if (addrSRCNTable + 0x100 > 0x10000) {
        return;
    }

    // find DIR address
    uint32_t ofsSetDIRASM;
    if (!file->SearchBytePattern(ptnLoadDIR, ofsSetDIRASM)) {
        return;
    }
    uint32_t spcDirAddr = file->GetByte(ofsSetDIRASM + 4) << 8;

    // scan SRCN table
    RareSnesInstrSet *newInstrSet =
        new RareSnesInstrSet(file, addrSRCNTable, spcDirAddr, newSeq->instrUnityKeyHints,
                             newSeq->instrPitchHints, newSeq->instrADSRHints);
    if (!newInstrSet->LoadVGMFile()) {
        delete newInstrSet;
        return;
    }

    // get SRCN # range
    uint8_t maxSRCN = 0;
    std::vector<uint8_t> usedSRCNs;
    const std::vector<uint8_t> &availInstruments = newInstrSet->GetAvailableInstruments();
    for (std::vector<uint8_t>::const_iterator itr = availInstruments.begin();
         itr != availInstruments.end(); ++itr) {
        uint8_t inst = (*itr);
        uint8_t srcn = file->GetByte(addrSRCNTable + inst);

        if (maxSRCN < srcn) {
            maxSRCN = srcn;
        }

        std::vector<uint8_t>::iterator itrSRCN = find(usedSRCNs.begin(), usedSRCNs.end(), srcn);
        if (itrSRCN == usedSRCNs.end()) {
            usedSRCNs.push_back(srcn);
        }
    }
    std::sort(usedSRCNs.begin(), usedSRCNs.end());

    // load BRR samples
    SNESSampColl *newSampColl = new SNESSampColl(RareSnesFormat::name, file, spcDirAddr, usedSRCNs);
    if (!newSampColl->LoadVGMFile()) {
        delete newSampColl;
        return;
    }
}

void RareSnesScanner::SearchForRareSnesFromROM(RawFile *file) {}
