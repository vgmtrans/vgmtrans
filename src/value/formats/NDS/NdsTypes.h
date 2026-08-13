/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "base/Types.h"

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace vgmtrans::formats::nds {

inline constexpr std::string_view kNdsFormatName = "NDS";

struct NdsFileRange {
  u32 offset = 0;
  u32 size = 0;
};

struct NdsSequenceRange {
  u32 offset = 0;
  u32 decodeOffset = 0;
  u32 size = 0;
  u32 sequenceEnd = 0;
  bool recoverMalformedSdatRange = false;
};

struct NdsSequenceInfo {
  bool valid = false;
  u16 fileId = 0xffff;
  u16 bank = 0xffff;
};

struct NdsBankInfo {
  bool valid = false;
  u16 fileId = 0xffff;
  std::array<u16, 4> waveArchives{0xffff, 0xffff, 0xffff, 0xffff};
};

struct NdsWaveArchiveInfo {
  bool valid = false;
  u16 fileId = 0xffff;
};

struct NdsLayout {
  // Parsed SDAT table-of-contents. File IDs refer into FAT; sequence/bank/wave indexes
  // refer into INFO/SYMB tables.
  u32 baseOffset = 0;
  u32 length = 0;
  u32 symbOffset = 0;
  u32 infoOffset = 0;
  u32 fatOffset = 0;
  bool hasSymb = false;
  std::vector<std::string> sequenceNames;
  std::vector<std::string> bankNames;
  std::vector<std::string> waveArchiveNames;
  std::vector<NdsSequenceInfo> sequences;
  std::vector<NdsBankInfo> banks;
  std::vector<NdsWaveArchiveInfo> waveArchives;
};

}  // namespace vgmtrans::formats::nds
