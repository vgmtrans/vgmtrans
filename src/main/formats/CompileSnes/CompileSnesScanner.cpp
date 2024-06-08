/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "CompileSnesInstr.h"
#include "CompileSnesSeq.h"
#include "ScannerManager.h"

namespace vgmtrans::scanners {
ScannerRegistration<CompileSnesScanner> s_compile_snes("COMPILESNES", {"spc"});
}

//; Super Puyo Puyo 2 SPC
//08e6: e5 00 18  mov   a,$1800
//08e9: c4 50     mov   $50,a
//08eb: e5 01 18  mov   a,$1801
//08ee: c4 51     mov   $51,a             ; set song table address to $50/1
//08f0: e5 02 18  mov   a,$1802
//08f3: c4 52     mov   $52,a
//08f5: 60        clrc
//08f6: 88 11     adc   a,#$11
//08f8: c4 54     mov   $54,a
//08fa: e5 03 18  mov   a,$1803
//08fd: c4 53     mov   $53,a             ; set duration table address to $52/3
//08ff: 88 00     adc   a,#$00
//0901: c4 55     mov   $55,a             ; set percussion table address to $54/5
BytePattern CompileSnesScanner::ptnSetSongListAddress(
	"\xe5\x00\x18\xc4\x50\xe5\x01\x18"
	"\xc4\x51\xe5\x02\x18\xc4\x52\x60"
	"\x88\x11\xc4\x54\xe5\x03\x18\xc4"
	"\x53\x88\x00\xc4\x55"
	,
	"x??x?x??"
	"x?x??x?x"
	"xxx?x??x"
	"?xxx?"
	,
	29);

void CompileSnesScanner::scan(RawFile* file, void* /*info*/) {
  size_t nFileLength = file->size();
  if (nFileLength == 0x10000) {
    searchForCompileSnesFromARAM(file);
  }
  else {
    // Search from ROM unimplemented
  }
}

void CompileSnesScanner::searchForCompileSnesFromARAM(RawFile *file) {
  CompileSnesVersion version;

  std::string basefilename = file->stem();
  std::string name = file->tag.hasTitle() ? file->tag.title : basefilename;

  // scan for table pointer initialize code
  uint32_t ofsSetSongListAddress;
  if (!file->searchBytePattern(ptnSetSongListAddress, ofsSetSongListAddress)) {
    return;
  }

  // determine the header address of Compile SPC engine
  uint16_t addrEngineHeader = file->readShort(ofsSetSongListAddress + 1);
  if (addrEngineHeader < 0x0100 && addrEngineHeader + 18 > 0x10000) {
    return;
  }

  // classify engine version
  uint8_t addrSongListReg = file->readByte(ofsSetSongListAddress + 4);
  if (addrSongListReg == 0x4e) {
    if (file->readShortBE(ofsSetSongListAddress - 4) == 0x280c) { // and a,#$0c
      version = COMPILESNES_JAKICRUSH;
    }
    else {
      version = COMPILESNES_ALESTE;
    }
  }
  else {
    if (file->readShortBE(ofsSetSongListAddress - 4) == 0x280c) { // and a,#$0c
      version = COMPILESNES_SUPERPUYO;
    }
    else {
      version = COMPILESNES_STANDARD;
    }
  }

  // determine the song list address
  uint16_t addrSongList = file->readShort(addrEngineHeader);

  // TODO: guess song index
  int8_t guessedSongIndex = -1;
  if (addrSongList + 4 <= 0x10000) {
    guessedSongIndex = 1;
  }

  uint32_t addrSongHeaderPtr = addrSongList + guessedSongIndex * 2;
  if (addrSongHeaderPtr + 2 <= 0x10000) {
    uint16_t addrSongHeader = file->readShort(addrSongHeaderPtr);
    uint8_t numTracks = file->readByte(addrSongHeader);
    if (numTracks > 0 && numTracks <= 8) {
      CompileSnesSeq *newSeq = new CompileSnesSeq(file, version, addrSongHeader, name);
      if (!newSeq->loadVGMFile()) {
        delete newSeq;
        return;
      }
    }
  }

  uint16_t spcDirAddr = file->readByte(addrEngineHeader + 0x0e) << 8;

  uint16_t addrTuningTable = file->readShort(addrEngineHeader + 0x12);
  uint16_t addrPitchTablePtrs = 0;
  if (version != COMPILESNES_ALESTE && version != COMPILESNES_JAKICRUSH) {
    addrPitchTablePtrs = file->readShort(addrEngineHeader + 0x16);
  }

  CompileSnesInstrSet *newInstrSet = new CompileSnesInstrSet(file, version, addrTuningTable, addrPitchTablePtrs, spcDirAddr);
  if (!newInstrSet->loadVGMFile()) {
    delete newInstrSet;
    return;
  }
}
