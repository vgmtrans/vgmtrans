/*
* VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#include "ItikitiSnesScanner.h"
#include "ItikitiSnesSeq.h"
#include "ItikitiSnesInstr.h"
#include "BytePattern.h"
#include "ScannerManager.h"

namespace vgmtrans::scanners {
ScannerRegistration<ItikitiSnesScanner> s_itikiti_snes("ITIKITISNES", {"spc"});
}

void ItikitiSnesScanner::Scan(RawFile *file, void *info) {
  if (file->size() == 0x10000)
    ScanFromApuRam(file);
  else
    ScanFromRom(file);
}

void ItikitiSnesScanner::ScanFromApuRam(RawFile *file) {
  std::string name =
      file->tag.HasTitle() ? file->tag.title : file->stem();

  uint32_t song_header_offset{};
  if (!ScanSongHeader(file, song_header_offset))
    return;

  auto seq = std::make_unique<ItikitiSnesSeq>(file, song_header_offset, name);
  if (!seq->LoadVGMFile())
    return;
  (void)seq.release();

  auto insrument_set = std::make_unique<ItikitiSnesInstrSet>(file, 0x1d40, 0x1e60, 0x1b00);
  if (!insrument_set->LoadVGMFile())
    return;
  (void)insrument_set.release();
}

void ItikitiSnesScanner::ScanFromRom(RawFile *file) {
}

bool ItikitiSnesScanner::ScanSongHeader(RawFile *file, uint32_t &song_header_offset) {
  //; Rudra no Hihou SPC
  // 0eb5: ed        notc
  // 0eb6: 6b de     ror   $de
  // 0eb8: f8 a1     mov   x,$a1
  // 0eba: f5 80 ed  mov   a,$ed80+x
  // 0ebd: c4 02     mov   $02,a
  // 0ebf: f5 81 ed  mov   a,$ed81+x
  // 0ec2: c4 03     mov   $03,a             ; obtain song header address
  // 0ec4: 8d 01     mov   y,#$01
  // 0ec6: e4 ef     mov   a,$ef
  // 0ec8: 77 02     cmp   a,($02)+y
  const BytePattern pattern_for_header(
      "\xed\x6b\xde\xf8\xa1\xf5\x80\xed\xc4\x02\xf5\x81\xed\xc4\x03\x8d\x01\xe4\xef\x77\x02",
      "xx?x?x??x?x??x?xxx?x?", 21);

  uint32_t code_offset{};
  if (!file->SearchBytePattern(pattern_for_header, code_offset))
    return false;

  const uint16_t header_pointer_address = file->GetShort(code_offset + 6);

  // The pointer points to the end of header, not the start. I don't know why.
  const uint16_t header_end = file->GetShort(header_pointer_address);
  if (header_end < 0x200)
    return false; // direct pages should not be used

  // TODO: improve the algorithm for the header location
  uint16_t header_start;
  if (file->GetByte(header_end - 18 + 1) == 8) // 8 tracks
    header_start = header_end - 18;
  else if (file->GetByte(header_end - 16 + 1) == 7) // 7 tracks
    header_start = header_end - 16;
  else if (file->GetByte(header_end - 14 + 1) == 6) // 6 tracks
    header_start = header_end - 14;
  else if (file->GetByte(header_end - 12 + 1) == 5)  // 5 tracks
    header_start = header_end - 12;
  else if (file->GetByte(header_end - 10 + 1) == 4)  // 4 tracks
    header_start = header_end - 10;
  else if (file->GetByte(header_end - 8 + 1) == 3)  // 3 tracks
    header_start = header_end - 8;
  else if (file->GetByte(header_end - 6 + 1) == 2)  // 2 tracks
    header_start = header_end - 6;
  else if (file->GetByte(header_end - 4 + 1) == 1)  // 1 tracks
    header_start = header_end - 4;
  else
    return false;

  song_header_offset = header_start;
  return true;
}
