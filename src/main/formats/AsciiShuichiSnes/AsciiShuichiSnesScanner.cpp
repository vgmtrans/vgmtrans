/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "AsciiShuichiSnesSeq.h"
#include "AsciiShuichiSnesInstr.h"
#include "ScannerManager.h"

namespace vgmtrans::scanners {
ScannerRegistration<AsciiShuichiSnesScanner> s_graph_snes("AsciiShuichiSnes", {"spc"});
}

// Only tested with:
//     - Wizardry VI - Bane of the Cosmic Forge
//     - Down the World - Mervil's Ambition

// ; Wizardry VI SPC
// 0f95: e8 05     mov   a,#$05
// 0f97: 3f 23 0f  call  $0f23
// 0f9a: f5 00 12  mov   a,$1200+x         ; read voice pointer (low) from address table
// 0f9d: d5 00 02  mov   $0200+x,a
// 0fa0: f5 08 12  mov   a,$1208+x         ; read voice pointer (high) from address table
// 0fa3: d4 0c     mov   $0c+x,a
// 0fa5: 1d        dec   x
// 0fa6: 10 ed     bpl   $0f95
BytePattern AsciiShuichiSnesScanner::ptnLoadSeq(
  "\xe8\x05\x3f\x23\x0f\xf5\x00\x12"
  "\xd5\x00\x02\xf5\x08\x12\xd4\x0c"
  "\x1d\x10\xed",
  "xxx??x??"
  "x??x??x?"
  "xxx",
  19);

// ; Wizardry VI SPC
// ; load instrument
// 094f: f8 ac     mov   x,$ac
// 0951: 1c        asl   a
// 0952: 1c        asl   a
// 0953: c4 b3     mov   $b3,a
// 0955: fd        mov   y,a
// 0956: f4 c8     mov   a,$c8+x           ; get ADSR(1) dsp reg address
// 0958: c4 b7     mov   $b7,a
// 095a: f7 bc     mov   a,($bc)+y         ; offset 0: SRCN
// 095c: eb b7     mov   y,$b7
// 095e: dc        dec   y
// 095f: 3f 20 08  call  $0820             ; set SRCN
// 0962: fd        mov   y,a
// 0963: f7 be     mov   a,($be)+y         ; ($be) fine tuning per instrument (signed)
// 0965: d5 d0 02  mov   $02d0+x,a
BytePattern AsciiShuichiSnesScanner::ptnLoadInstr(
  "\xf8\xac\x1c\x1c\xc4\xb3\xfd\xf4"
  "\xc8\xc4\xb7\xf7\xbc\xeb\xb7\xdc"
  "\x3f\x20\x08\xfd\xf7\xbe\xd5\xd0"
  "\x02",
  "x?xxx?xx"
  "?x?x?x?x"
  "x??xx?x?"
  "?",
  25);

// ; Wizardry VI SPC
// 056c: 8d 5d     mov   y,#$5d
// 056e: e8 30     mov   a,#$30
// 0570: 3f 20 08  call  $0820             ; set DIR ($3000)
BytePattern AsciiShuichiSnesScanner::ptnLoadDIR(
  "\x8d\x5d\xe8\x30\x3f\x20\x08",
  "xxx?x??",
  7);

void AsciiShuichiSnesScanner::scan(RawFile *file, void *info) {
  const size_t nFileLength = file->size();
  if (nFileLength == 0x10000) {
    scanFromARAM(file);
  } else {
    // Search from ROM unimplemented
  }
}

void AsciiShuichiSnesScanner::scanFromARAM(RawFile *file) {
  const std::string name = file->tag.hasTitle() ? file->tag.title : file->stem();

  // search song header
  uint32_t ofsLoadSeq;
  if (!file->searchBytePattern(ptnLoadSeq, ofsLoadSeq))
    return;
  const uint16_t seqHeaderAddress = file->readShort(ofsLoadSeq + 6);

  const auto newSeq = new AsciiShuichiSnesSeq(file, seqHeaderAddress, name);
  if (!newSeq->loadVGMFile()) {
    delete newSeq;
    return;
  }

  // search instrument loader
  uint32_t ofsLoadInstr;
  if (!file->searchBytePattern(ptnLoadInstr, ofsLoadInstr))
    return;
  const uint8_t instrTablePtrAddress = file->readByte(ofsLoadInstr + 12);
  const uint16_t instrTableAddress = file->readShort(instrTablePtrAddress);
  L_DEBUG("{}: found instrument table {:#04x} via {:#02x}", "AsciiShuichiSnesScanner",
          instrTableAddress, instrTablePtrAddress);

  const uint8_t instrTuningTablePtrAddress = file->readByte(ofsLoadInstr + 21);
  const uint16_t instrTuningTableAddress = file->readShort(instrTuningTablePtrAddress);
  L_DEBUG("{}: found instrument tuning table {:#04x} via {:#02x}", "AsciiShuichiSnesScanner",
          instrTuningTableAddress, instrTuningTablePtrAddress);

  // search DIR address
  uint32_t ofsLoadDIR;
  if (!file->searchBytePattern(ptnLoadDIR, ofsLoadDIR))
    return;
  const auto sampleDirAddress = static_cast<uint16_t>(file->readByte(ofsLoadDIR + 3) << 8);
  L_DEBUG("{}: found sample DIR address {:#04x}", "AsciiShuichiSnesScanner", sampleDirAddress);

  const auto newInstrSet = new AsciiShuichiSnesInstrSet(file, instrTableAddress,
                                                        instrTuningTableAddress, sampleDirAddress);
  if (!newInstrSet->loadVGMFile()) {
    delete newInstrSet;
    return;
  }
}
