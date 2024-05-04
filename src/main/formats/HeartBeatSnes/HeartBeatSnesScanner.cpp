/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "HeartBeatSnesSeq.h"
#include "HeartBeatSnesInstr.h"
#include "ScannerManager.h"

namespace vgmtrans::scanners {
ScannerRegistration<HeartBeatSnesScanner> s_heartbeat_snes("HEARTBEATSNES", {"spc"});
}

//; Dragon Quest 6 SPC
//1b9c: ee        pop   y
//1b9d: f6 0d 0a  mov   a,$0a0d+y
//1ba0: c4 08     mov   $08,a
//1ba2: f6 19 0a  mov   a,$0a19+y
//1ba5: c4 09     mov   $09,a             ; set $0A0D/0A19+Y to $08/9
//1ba7: f8 36     mov   x,$36
//1ba9: dd        mov   a,y
//1baa: d5 e0 08  mov   $08e0+x,a
//1bad: 8d 00     mov   y,#$00            ; zero offset
BytePattern HeartBeatSnesScanner::ptnReadSongList(
	"\xee\xf6\x0d\x0a\xc4\x08\xf6\x19"
	"\x0a\xc4\x09\xf8\x36\xdd\xd5\xe0"
	"\x08\x8d\x00"
	,
	"xx??x?x?"
	"?x?x?xx?"
	"?xx"
	,
	19);

//; Dragon Quest 6 SPC
//1343: e8 02     mov   a,#$02
//1345: 8d 5d     mov   y,#$5d
//1347: 3f 48 14  call  $1448
//134a: e8 00     mov   a,#$00
BytePattern HeartBeatSnesScanner::ptnSetDIR(
	"\xe8\x02\x8d\x5d\x3f\x48\x14\xe8"
	"\x00"
	,
	"x?xxx??x"
	"x"
	,
	9);

//; Dragon Quest 6 SPC
//1505: 3f c3 12  call  $12c3             ; read arg1: set patch
//1508: 8d 06     mov   y,#$06
//150a: cf        mul   ya                ; offset = patch * 6
//150b: fd        mov   y,a               ; use lower 8 bits
//150c: 6d        push  y
//150d: f7 08     mov   a,($08)+y         ; offset +0: sample index
//150f: d5 d8 03  mov   $03d8+x,a
//1512: eb 36     mov   y,$36
//1514: f6 e0 08  mov   a,$08e0+y
//1517: 28 0f     and   a,#$0f
//1519: 8d 10     mov   y,#$10
//151b: cf        mul   ya
//151c: ee        pop   y
//151d: 60        clrc
//151e: 97 08     adc   a,($08)+y         ; offset +0: sample index + (n * 0x10)
//1520: fc        inc   y
//1521: 6d        push  y
//1522: fd        mov   y,a
//1523: f6 25 0a  mov   a,$0a25+y         ; read actual SRCN
//1526: 8d 04     mov   y,#$04
//1528: 3f 37 14  call  $1437             ; set SRCN
//152b: ee        pop   y
BytePattern HeartBeatSnesScanner::ptnLoadSRCN(
	"\x3f\xc3\x12\x8d\x06\xcf\xfd\x6d"
	"\xf7\x08\xd5\xd8\x03\xeb\x36\xf6"
	"\xe0\x08\x28\x0f\x8d\x10\xcf\xee"
	"\x60\x97\x08\xfc\x6d\xfd\xf6\x25"
	"\x0a\x8d\x04\x3f\x37\x14\xee"
	,
	"x??xxxxx"
	"x?x??x?x"
	"??xxxxxx"
	"xx?xxxx?"
	"?xxx??x"
	,
	39);

//; Dragon Quest 6 SPC
//0bd4: f5 e0 08  mov   a,$08e0+x
//0bd7: 28 0f     and   a,#$0f
//0bd9: fd        mov   y,a
//0bda: f6 0d 0a  mov   a,$0a0d+y
//0bdd: c4 3b     mov   $3b,a
//0bdf: f6 19 0a  mov   a,$0a19+y
//0be2: c4 3c     mov   $3c,a
BytePattern HeartBeatSnesScanner::ptnSaveSeqHeaderAddress(
	"\xf5\xe0\x08\x28\x0f\xfd\xf6\x0d"
	"\x0a\xc4\x3b\xf6\x19\x0a\xc4\x3c"
	,
	"x??xxxx?"
	"?x?x??x?"
	,
	16);

void HeartBeatSnesScanner::Scan(RawFile *file, void *info) {
  uint32_t nFileLength = file->size();
  if (nFileLength == 0x10000) {
    SearchForHeartBeatSnesFromARAM(file);
  } else {
    SearchForHeartBeatSnesFromROM(file);
  }
  return;
}

void HeartBeatSnesScanner::SearchForHeartBeatSnesFromARAM(RawFile *file) {
  HeartBeatSnesVersion version = HEARTBEATSNES_NONE;
  std::string name = file->tag.HasTitle() ? file->tag.title : removeExtFromPath(file->name());

  // search song list
  uint32_t ofsReadSongList;
  uint16_t addrSongListLo;
  uint16_t addrSongListHi;
  int8_t maxSongIndex;
  if (file->SearchBytePattern(ptnReadSongList, ofsReadSongList)) {
    addrSongListLo = file->GetShort(ofsReadSongList + 2);
    addrSongListHi = file->GetShort(ofsReadSongList + 7);

    if (addrSongListLo >= addrSongListHi || addrSongListHi - addrSongListLo >= 0x10) {
      return;
    }

    maxSongIndex = addrSongListHi - addrSongListLo;
    if (addrSongListHi + maxSongIndex > 0x10000) {
      return;
    }

    version = HEARTBEATSNES_STANDARD;
  }
  else {
    return;
  }

  uint32_t ofsSaveSeqHeaderAddress;
  uint8_t addrSeqHeaderPtr;
  if (file->SearchBytePattern(ptnSaveSeqHeaderAddress, ofsSaveSeqHeaderAddress)) {
    addrSeqHeaderPtr = file->GetByte(ofsSaveSeqHeaderAddress + 10);
  }
  else {
    return;
  }

  // guess song index
  int8_t songIndex = -1;
  for (int8_t songIndexCandidate = 0; songIndexCandidate < maxSongIndex; songIndexCandidate++) {
    uint16_t addrSeqHeader =
        file->GetByte(addrSongListLo + songIndexCandidate) | (file->GetByte(addrSongListHi + songIndexCandidate) << 8);
    if (addrSeqHeader == 0) {
      break;
    }

    uint16_t addrTargetSeqHeader = file->GetShort(addrSeqHeaderPtr);
    if (addrSeqHeader == addrTargetSeqHeader) {
      songIndex = songIndexCandidate;
      break;
    }
  }

  if (songIndex == -1) {
    songIndex = 0;
  }

  // search DIR address
  uint32_t ofsSetDIR;
  uint16_t spcDirAddr = 0;
  if (file->SearchBytePattern(ptnSetDIR, ofsSetDIR)) {
    spcDirAddr = file->GetByte(ofsSetDIR + 1) << 8;
  }

  // search SRCN lookup table
  uint32_t ofsLoadSRCN;
  uint16_t addrSRCNTable = 0;
  if (file->SearchBytePattern(ptnLoadSRCN, ofsLoadSRCN)) {
    addrSRCNTable = file->GetShort(ofsLoadSRCN + 31);
  }

  uint16_t addrSeqHeader = file->GetByte(addrSongListLo + songIndex) | (file->GetByte(addrSongListHi + songIndex) << 8);
  HeartBeatSnesSeq *newSeq = new HeartBeatSnesSeq(file, version, addrSeqHeader, name);
  if (!newSeq->LoadVGMFile()) {
    delete newSeq;
    return;
  }

  if (spcDirAddr == 0 || addrSRCNTable == 0) {
    return;
  }

  uint16_t ofsInstrTable = file->GetShort(addrSeqHeader);
  uint16_t ofsFirstTrack = file->GetShort(addrSeqHeader + 2);
  if (ofsInstrTable >= ofsFirstTrack || addrSeqHeader + ofsInstrTable >= 0x10000) {
    return;
  }

  uint16_t addrInstrTable = addrSeqHeader + ofsInstrTable;
  uint16_t instrTableSize = ofsFirstTrack - ofsInstrTable;

  // we sometimes need to shorten the expected table size
  // example: Dragon Quest 6 - Through the Fields
  for (uint16_t newTableSize = 0; newTableSize < instrTableSize; newTableSize++) {
    if (newSeq->IsItemAtOffset(addrInstrTable + newTableSize, false)) {
      instrTableSize = newTableSize;
      break;
    }
  }

  HeartBeatSnesInstrSet *newInstrSet =
      new HeartBeatSnesInstrSet(file, version, addrInstrTable, instrTableSize, addrSRCNTable, songIndex, spcDirAddr);
  if (!newInstrSet->LoadVGMFile()) {
    delete newInstrSet;
    return;
  }
}

void HeartBeatSnesScanner::SearchForHeartBeatSnesFromROM(RawFile *file) {}
