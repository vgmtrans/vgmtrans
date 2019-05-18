/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
 





#include "PrismSnesInstr.h"
#include "PrismSnesSeq.h"

//; Dual Orb 2 SPC
//0a28: f6 00 23  mov   a,$2300+y
//0a2b: c4 06     mov   $06,a
//0a2d: fc        inc   y
//0a2e: f6 00 23  mov   a,$2300+y
//0a31: c4 07     mov   $07,a
//; zero track header item index
//0a33: 8d 00     mov   y,#$00
//; read header for each tracks
//0a35: f7 06     mov   a,($06)+y
//0a37: 30 d4     bmi   $0a0d             ; break if >= $80 (usually $ff)
BytePattern PrismSnesScanner::ptnLoadSeq(
	"\xf6\x00\x23\xc4\x06\xfc\xf6\x00"
	"\x23\xc4\x07\x8d\x00\xf7\x06\x30"
	"\xd4"
	,
	"x??x?xx?"
	"?x?xxx?x"
	"?"
	,
	17);

//; Dual Orb 2 SPC
//0b5d: 8d 00     mov   y,#$00
//0b5f: f7 26     mov   a,($26)+y         ; read vcmd
//0b61: 10 1f     bpl   $0b82
//0b63: 3a 26     incw  $26
//0b65: 68 a0     cmp   a,#$a0
//0b67: 90 08     bcc   $0b71
//0b69: 80        setc
//0b6a: a8 c0     sbc   a,#$c0            ; a0-b0 => crash
//0b6c: 1c        asl   a
//0b6d: 5d        mov   x,a
//0b6e: 1f ff 0e  jmp   ($0eff+x)
BytePattern PrismSnesScanner::ptnExecVCmd(
	"\x8d\x00\xf7\x26\x10\x1f\x3a\x26"
	"\x68\xa0\x90\x08\x80\xa8\xc0\x1c"
	"\x5d\x1f\xff\x0e"
	,
	"xxx?x?x?"
	"xxxxxxxx"
	"xx??"
	,
	20);

//; Dual Orb 2 SPC
//15ea: 8d 16     mov   y,#$16
//15ec: e8 18     mov   a,#$18
//15ee: da 00     movw  $00,ya            ; $1618
//15f0: 8d 07     mov   y,#$07
//15f2: cd 7d     mov   x,#$7d
//15f4: 3f 05 16  call  $1605
BytePattern PrismSnesScanner::ptnSetDSPd(
	"\x8d\x16\xe8\x18\xda\x00\x8d\x07"
	"\xcd\x7d\x3f\x05\x16"
	,
	"x?x?x?xx"
	"xxx??"
	,
	13);

//; Dual Orb 2 SPC
//; vcmd fe - instrument
//13a3: f8 24     mov   x,$24
//13a5: f7 26     mov   a,($26)+y
//13a7: 3a 26     incw  $26
//13a9: d5 50 03  mov   $0350+x,a         ; set SRCN shadow
//13ac: fd        mov   y,a
//13ad: f6 00 1f  mov   a,$1f00+y
//13b0: d5 b0 03  mov   $03b0+x,a         ; load ADSR(1)
//13b3: f6 00 20  mov   a,$2000+y
//13b6: d5 c8 03  mov   $03c8+x,a         ; load ADSR(2)
//13b9: 38 7f 28  and   $28,#$7f          ; ADSR mode
//13bc: 8f ff 39  mov   $39,#$ff
BytePattern PrismSnesScanner::ptnLoadInstr(
	"\xf8\x24\xf7\x26\x3a\x26\xd5\x50"
	"\x03\xfd\xf6\x00\x1f\xd5\xb0\x03"
	"\xf6\x00\x20\xd5\xc8\x03\x38\x7f"
	"\x28\x8f\xff\x39"
	,
	"x?x?x?x?"
	"?xx??x??"
	"x??x??xx"
	"?xx?"
	,
	28);

//; Dual Orb 2 SPC
//; calculate note pitch ($2a: note number)
//14e0: f8 42     mov   x,$42             ; SRCN
//14e2: f5 00 21  mov   a,$2100+x         ; per-instrument tuning (coarse)
//14e5: 60        clrc
//14e6: 84 2a     adc   a,$2a
//14e8: c4 2a     mov   $2a,a
//14ea: f5 00 22  mov   a,$2200+x
//14ed: 60        clrc
//14ee: 84 29     adc   a,$29
//14f0: c4 29     mov   $29,a
//14f2: 90 02     bcc   $14f6
//14f4: ab 2a     inc   $2a
BytePattern PrismSnesScanner::ptnLoadInstrTuning(
	"\xf8\x42\xf5\x00\x21\x60\x84\x2a"
	"\xc4\x2a\xf5\x00\x22\x60\x84\x29"
	"\xc4\x29\x90\x02\xab\x2a"
	,
	"x?x??xx?"
	"x?x??xx?"
	"x?xxx?"
	,
	22);

void PrismSnesScanner::Scan(RawFile *file, void *info) {
  uint32_t nFileLength = file->size();
  if (nFileLength == 0x10000) {
    SearchForPrismSnesFromARAM(file);
  }
  else {
    SearchForPrismSnesFromROM(file);
  }
  return;
}

void PrismSnesScanner::SearchForPrismSnesFromARAM(RawFile *file) {
  PrismSnesVersion version = PRISMSNES_NONE;
  std::wstring name = file->tag.HasTitle() ? file->tag.title : RawFile::removeExtFromPath(file->GetFileName());

  // search song list
  uint32_t ofsLoadSeq;
  uint16_t addrSeqList;
  if (file->SearchBytePattern(ptnLoadSeq, ofsLoadSeq)) {
    addrSeqList = file->GetShort(ofsLoadSeq + 1);
  }
  else {
    return;
  }

  uint32_t ofsExecVCmd;
  uint16_t addrVoiceCmdAddressTable;
  if (file->SearchBytePattern(ptnExecVCmd, ofsExecVCmd)) {
    addrVoiceCmdAddressTable = file->GetShort(ofsExecVCmd + 18);
    if (addrVoiceCmdAddressTable + (2 * 0x40) > 0x10000) {
      return;
    }
  }
  else {
    return;
  }

  // detect engine version
  if (file->GetShort(addrVoiceCmdAddressTable) == file->GetShort(addrVoiceCmdAddressTable + 32)) {
    version = PRISMSNES_CGV;
  }
  else if (file->GetShort(addrVoiceCmdAddressTable) == file->GetShort(addrVoiceCmdAddressTable + 10)) {
    version = PRISMSNES_DO;
  }
  else {
    version = PRISMSNES_DO2;
  }

  // TODO: guess song index
  int8_t songIndex = 0;

  uint32_t addrSeqHeaderPtr = addrSeqList + (songIndex * 2);
  uint32_t addrSeqHeader = file->GetShort(addrSeqHeaderPtr);
  PrismSnesSeq *newSeq = new PrismSnesSeq(file, version, addrSeqHeader, name);
  if (!newSeq->LoadVGMFile()) {
    delete newSeq;
    return;
  }

  uint32_t ofsSetDSPd;
  uint16_t spcDirAddr;
  if (file->SearchBytePattern(ptnSetDSPd, ofsSetDSPd)) {
    uint16_t addrDspRegTabled = file->GetByte(ofsSetDSPd + 3) | (file->GetByte(ofsSetDSPd + 1) << 8);
    if (addrDspRegTabled + 8 > 0x10000) {
      return;
    }
    spcDirAddr = file->GetByte(addrDspRegTabled + 5) << 8;
  }
  else {
    return;
  }

  uint32_t ofsLoadInstr;
  uint16_t addrADSR1Table;
  uint16_t addrADSR2Table;
  if (file->SearchBytePattern(ptnLoadInstr, ofsLoadInstr)) {
    addrADSR1Table = file->GetShort(ofsLoadInstr + 11);
    addrADSR2Table = file->GetShort(ofsLoadInstr + 17);
  }
  else {
    return;
  }

  uint32_t ofsLoadInstrTuning;
  uint16_t adsrTuningTableHigh;
  uint16_t adsrTuningTableLow;
  if (file->SearchBytePattern(ptnLoadInstrTuning, ofsLoadInstrTuning)) {
    adsrTuningTableHigh = file->GetShort(ofsLoadInstrTuning + 3);
    adsrTuningTableLow = file->GetShort(ofsLoadInstrTuning + 11);
  }
  else {
    return;
  }

  PrismSnesInstrSet *newInstrSet = new PrismSnesInstrSet(file,
                                                         version,
                                                         spcDirAddr,
                                                         addrADSR1Table,
                                                         addrADSR2Table,
                                                         adsrTuningTableHigh,
                                                         adsrTuningTableLow);
  if (!newInstrSet->LoadVGMFile()) {
    delete newInstrSet;
    return;
  }
}

void PrismSnesScanner::SearchForPrismSnesFromROM(RawFile *file) {
}
