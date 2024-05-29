/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "SoftCreatSnesSeq.h"
#include "ScannerManager.h"

namespace vgmtrans::scanners {
ScannerRegistration<SoftCreatSnesScanner> s_softcreat_snes("SOFTWARECREATIONSSNES", {"spc"});
}

//; Plok!
//0589: 7d        mov   a,x
//058a: 68 05     cmp   a,#$05
//058c: b0 fa     bcs   $0588
//058e: fd        mov   y,a               ; y = song select (0-4)
//058f: cd 00     mov   x,#$00
//0591: f6 85 13  mov   a,$1385+y
//0594: f0 0a     beq   $05a0             ; $00xx = unused channel
//0596: c4 31     mov   $31,a
//0598: f6 80 13  mov   a,$1380+y
//059b: c4 30     mov   $30,a             ; channel 1 voice ptr
//059d: 3f 38 06  call  $0638
//05a0: 3d        inc   x
//05a1: 3d        inc   x
BytePattern SoftCreatSnesScanner::ptnLoadSeq(
	"\x7d\x68\x05\xb0\xfa\xfd\xcd\x00"
	"\xf6\x85\x13\xf0\x0a\xc4\x31\xf6"
	"\x80\x13\xc4\x30\x3f\x38\x06\x3d"
	"\x3d"
	,
	"xx?x?xxx"
	"x??x?x?x"
	"??x?x??x"
	"x"
	,
	25);

//; Plok!
//076e: 3f 85 07  call  $0785
//0771: 10 1d     bpl   $0790             ; 00-7f - note/rest
//0773: 68 ba     cmp   a,#$ba
//0775: b0 0b     bcs   $0782
//0777: 1c        asl   a
//0778: fd        mov   y,a
//0779: f6 70 0b  mov   a,$0b70+y
//077c: 2d        push  a
//077d: f6 6f 0b  mov   a,$0b6f+y
//0780: 2d        push  a
//0781: 6f        ret
BytePattern SoftCreatSnesScanner::ptnVCmdExec(
	"\x3f\x85\x07\x10\x1d\x68\xba\xb0"
	"\x0b\x1c\xfd\xf6\x70\x0b\x2d\xf6"
	"\x6f\x0b\x2d\x6f"
	,
	"x??x?x?x"
	"xxxx??xx"
	"??xx"
	,
	20);

void SoftCreatSnesScanner::Scan(RawFile *file, void *info) {
  size_t nFileLength = file->size();
  if (nFileLength == 0x10000) {
    SearchForSoftCreatSnesFromARAM(file);
  } else {
    // Search from ROM unimplemented
  }
}

void SoftCreatSnesScanner::SearchForSoftCreatSnesFromARAM(RawFile *file) {
  SoftCreatSnesVersion version = SOFTCREATSNES_NONE;
  std::string name = file->tag.HasTitle() ? file->tag.title : file->stem();

  // search song list
  uint32_t ofsLoadSeq;
  uint16_t addrSeqList;
  uint8_t songIndexMax;
  uint8_t headerAlignSize;
  if (file->SearchBytePattern(ptnLoadSeq, ofsLoadSeq)) {
    addrSeqList = file->GetShort(ofsLoadSeq + 16);

    songIndexMax = file->GetByte(ofsLoadSeq + 2);
    if (songIndexMax == 0) {
      return;
    }

    uint16_t addrStartLow = file->GetByte(ofsLoadSeq + 16);
    uint16_t addrStartHigh = file->GetByte(ofsLoadSeq + 9);
    if (addrStartLow > addrStartHigh || addrStartHigh - addrStartLow > songIndexMax) {
      return;
    }
    headerAlignSize = addrStartHigh - addrStartLow;
  } else {
    return;
  }

  // search vcmd address table for version check
  uint32_t ofsVCmdExec;
  uint8_t VCMD_CUTOFF;
  uint16_t addrVCmdAddressTable;
  if (file->SearchBytePattern(ptnVCmdExec, ofsVCmdExec)) {
    VCMD_CUTOFF = file->GetByte(ofsVCmdExec + 6);
    addrVCmdAddressTable = file->GetByte(ofsVCmdExec + 16);
  } else {
    return;
  }

  switch (VCMD_CUTOFF) {
    case 0xb8:
      version = SOFTCREATSNES_V1;
      break;

    case 0xba:
      version = SOFTCREATSNES_V2;
      break;

    case 0xbd:
      version = SOFTCREATSNES_V3;
      break;

    case 0xc7:
      version = SOFTCREATSNES_V4;
      break;

    case 0xc3:
      version = SOFTCREATSNES_V5;
      break;

    default:
      version = SOFTCREATSNES_NONE;
      break;
  }

  // TODO: guess song index
  int8_t songIndex = 1;

  uint32_t addrSeqHeader = addrSeqList + songIndex;
  SoftCreatSnesSeq *newSeq = new SoftCreatSnesSeq(file, version, addrSeqHeader, headerAlignSize, name);
  if (!newSeq->LoadVGMFile()) {
    delete newSeq;
    return;
  }
}
