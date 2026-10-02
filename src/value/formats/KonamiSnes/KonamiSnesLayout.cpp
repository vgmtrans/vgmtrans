/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/KonamiSnes/KonamiSnes.h"
#include "value/scan/BytePattern.h"

#include <algorithm>
#include <array>
#include <optional>
#include <string_view>

namespace vgmtrans::formats::konami_snes {

using namespace core;
using namespace std::string_view_literals;

namespace {

// Konami reused this driver across several games, but moved its RAM variables
// and tables between builds. These signatures match instructions run by the
// SNES sound CPU; '?' leaves changing addresses and constants unchecked.
// The short names identify representative driver builds, not game-specific
// parsing paths.
constexpr MaskedBytePattern kSetSongHeaderAddressGG4{
    "\x8f\x00\x0a\x8f\x39\x0b\xcd\x00\xd8\x1c"sv,
    "x??x??xxx?"sv,
};

constexpr MaskedBytePattern kReadSongListPNTB{
    "\xc4\x0c\x8f\x1b\x04\x8f\x05\x05\x8d\x05\xcf\x7a\x04\xda\x04\x8d"
    "\x00\xcd\x00\xf7\x04\xc4\x1a\xfc\xf7\x04\xc4\x06\xe4\x0c\x68\x41"
    "\xb0\x3b"sv,
    "x?x??x??xxxx?x?xxxxx?x?xx?x?x?x?x?"sv,
};

constexpr MaskedBytePattern kReadSongListAXE{
    "\xe4\x0c\x8f\xe6\x04\x8f\x03\x05\x9c\x8d\x05\xcf\x7a\x04\xda\x04"
    "\x8d\x00\xcd\x00\xf7\x04\xc4\x20\xfc\xf7\x04\xc4\x06\xe4\x0c\x68"
    "\x4d\xb0\x5a\xcd\x0c\x68\x41\xb0\x4a"sv,
    "x?x??x??xxxxx?x?xxxxx?x?xx?x?x?x?x?x?x?x?"sv,
};

constexpr MaskedBytePattern kReadSongListCNTR3{
    "\xe4\x0c\x8f\xe6\x04\x8f\x03\x05\x9c\x8d\x05\xcf\x7a\x04\xda\x04"
    "\x8d\x00\xcd\x00\xf7\x04\xc4\x20\xfc\xf7\x04\xc4\x06\xe4\x0c\x68"
    "\x60\xb0\x64\x68\x5c\x90\x0a"sv,
    "x?x??x??xxxxx?x?xxxxx?x?xx?x?x?x?x?x?x?"sv,
};

// Batman Returns uses the same early five-byte song rows as the other
// pointer-based builds, but branches around its sound-effect arbitration and
// jumps directly to the shared music setup path for IDs 0x7c and above.
constexpr MaskedBytePattern kReadSongListBR{
    "\xe4\x0c\x8f\xe5\x04\x8f\x03\x05\x9c\x8d\x05\xcf\x7a\x04\xda\x04"
    "\x8d\x00\xcd\x00\xf7\x04\xc4\x1c\xfc\xf7\x04\xc4\x06\xe4\x0c\x68"
    "\x7c\x90\x03\x5f\x6d\x1b"sv,
    "x?x??x??xxxxx?x?xxxxx?x?xx?x?x?x?x?x??"sv,
};

constexpr MaskedBytePattern kJumpToVcmdGG4{
    "\x1c\xfd\xf6\xbc\x1a\x2d\xf6\xbb\x1a\x2d\xf6\xfb\x1a\xf0\x08"sv,
    "xxx??xx??xx??x?"sv,
};

constexpr MaskedBytePattern kJumpToVcmdCNTR3{
    "\x80\xa4\x04\x1c\xfd\xf6\xde\x0d\x2d\xf6\xdd\x0d\x2d\xdd\x5c\xfd"
    "\xf6\x27\x0e\xf0\x08"sv,
    "xx?xxx??xx??xxxxx??x?"sv,
};

constexpr MaskedBytePattern kBranchForVcmd6xMDR2{
    "\xe4\x08\x8f\xde\x04\x68\xe0\xb0\x0c\x8f\x60\x04\x68\x62\x90\x05"sv,
    "x?x??xxx?x??xxx?"sv,
};

constexpr MaskedBytePattern kBranchForVcmd6xCNTR3{
    "\xe4\x08\x8f\xdb\x04\x68\xe0\xb0\x0c\x68\x65\x90\x05"sv,
    "x?x??xxx?xx??"sv,
};

constexpr std::array kIndexedEchoFilters{
    MaskedBytePattern{"\xbc\x8d\x08\xcf\x5d\x8d\x08"sv, "xxxxxxx"sv},
    MaskedBytePattern{"\xbc\x1c\x1c\x1c\x5d\x8d\x08"sv, "xxxxxxx"sv},
    MaskedBytePattern{"\x3f\x00\x00\x1c\x1c\x1c\xc4\x04\x8f\x00\x05\xe8\xe0\x8d\x03\x7a\x04"sv,
                      "x??xxxxxxxxxxxxxx"sv},
};

constexpr MaskedBytePattern kSetDIRGG4{
    "\x8f\x5d\xf2\x8f\x04\xf3"sv,
    "xxxx?x"sv,
};

constexpr MaskedBytePattern kSetDIRCNTR3{
    "\xe8\x50\x8d\x5d\xcc\xf2\x00\xc5\xf3\x00"sv,
    "x?xxxxxxxx"sv,
};

constexpr MaskedBytePattern kLoadInstrJOP{
    "\x09\x11\x10\x68\x24\xb0\x0c\x8f\xa0\x04\x8f\x05\x05\x3f\xf5\x17"
    "\x5f\x12\x15\xa8\x24\x2d\xec\xe0\x01\xf6\x8a\x05\xc4\x04\xf6\x8b"
    "\x05\xc4\x05\xae\x3f\xf5\x17\x5f\x12\x15"sv,
    "x??x?xxx??x??x??x??x?xx??x??x?x??x?xx??x??"sv,
};

constexpr MaskedBytePattern kLoadInstrGP{
    "\x09\x11\x10\xfd\xf4\xd1\xd0\x2d\xdd\x68\x1f\xb0\x0c\x8f\x68\x04"
    "\x8f\x05\x05\x3f\x31\x14\x5f\x45\x11\xa8\x1f\x2d\xeb\x25\xf6\x58"
    "\x05\xc4\x04\xf6\x59\x05\xc4\x05\xae\x3f\x31\x14\x5f\x45\x11"sv,
    "x??xx?x?xx?xxx??x??x??x??x?xx?x??x?x??x?xx??x??"sv,
};

constexpr MaskedBytePattern kLoadInstrGG4{
    "\x09\x11\x10\xfd\xf5\xa1\x01\xd0\x27\xdd\x68\x28\xb0\x0c\x8f\x3c"
    "\x04\x8f\x0a\x05\x3f\xee\x1b\x5f\xe2\x18\xa8\x28\x2d\xeb\x25\xf6"
    "\x20\x0a\xc4\x04\xf6\x21\x0a\xc4\x05\xae\x3f\xee\x1b\x5f\xe2\x18"sv,
    "x??xx??x?xx?xxx??x??x??x??x?xx?x??x?x??x?xx??x??"sv,
};

constexpr MaskedBytePattern kLoadInstrPNTB{
    "\x09\x1a\x11\x68\xf0\xb0\xda\x68\x1d\xb0\x0c\x8f\x00\x04\x8f\x07"
    "\x05\x3f\x3e\x13\x5f\x61\x10\xa8\x1d\x2d\xeb\x24\xf6\xf4\x06\xc4"
    "\x04\xf6\xf5\x06\xc4\x05\xae\x3f\x3e\x13\x5f\x61\x10\x8f\xe8\x04"
    "\x8f\x07\x05\x1c\x1c\x1c\x8d\x00\x7a\x04\xda\x04"sv,
    "x??x?x?x?xxx??x??x??x??x?xx?x??x?x??x?xx??x??x??x??xxxxxx?x?"sv,
};

constexpr MaskedBytePattern kLoadInstrCNTR3{
    "\x09\x20\x17\xd5\x97\x02\x68\x19\xb0\x04\x68\x14\xb0\x11\xe8\x36"
    "\xc4\x04\xe8\x06\xc4\x05\xf5\x97\x02\x3f\x36\x0f\x5f\x8c\x0c\xe5"
    "\x05\x02\x1c\xfd\xf6\x28\x06\xc4\x04\xf6\x29\x06\xc4\x05\xf5\x97"
    "\x02\x80\xa8\x14\x3f\x36\x0f\x5f\x8c\x0c\xe8\xfe\xc4\x04\xe8\x06"
    "\xc4\x05\xf5\x97\x02\x8d\x08\xcf\x7a\x04\xda\x04"sv,
    "x??x??x?xxx?xxx?x?x?x?x??x??x??x??xxx??x?x??x?x??xx?x??x??x?x?x?x?x??xxxx?x?"sv,
};

constexpr MaskedBytePattern kLoadPercInstrGG4{
    "\x8f\xe6\x04\x8f\x0d\x05\x8d\x07\xcf\x7a\x04\xda\x04"sv,
    "x??x??xxxx?x?"sv,
};

// Older drivers load the percussion table address as two separate immediates.
[[nodiscard]] std::optional<u32> percussionTableFromGG4Pattern(ByteReader reader) {
  if (const auto offset = findBytePattern(reader, kLoadPercInstrGG4)) {
    return static_cast<u32>(reader.u8At(*offset + 1) | (reader.u8At(*offset + 4) << 8));
  }
  return std::nullopt;
}

// The instrument loader selects one pointer using the bank byte that was live
// when the SPC snapshot was captured.
[[nodiscard]] std::optional<u32> bankedTableByCurrentBank(ByteReader reader, u32 currentBankAddress,
                                                          u32 bankTableAddress, bool indexIsWords) {
  if (!reader.has(currentBankAddress, 1)) {
    return std::nullopt;
  }
  const u32 bankIndex = reader.u8At(currentBankAddress);
  const u32 pointerOffset = bankTableAddress + (indexIsWords ? bankIndex * 2 : bankIndex);
  if (!reader.has(pointerOffset, 2)) {
    return std::nullopt;
  }
  return reader.le16(pointerOffset);
}

// Walk the sample's nine-byte compressed blocks until an end flag appears.
// This rejects proposed directories whose pointers do not lead to a complete
// sample.
[[nodiscard]] u32 inferredSampleLength(ByteReader reader, u32 startAddress, bool& loops) {
  u32 offset = startAddress;
  while (true) {
    if (!reader.has(offset, 9)) {
      return 0;
    }
    const u8 flags = reader.u8At(offset);
    offset += 9;
    if ((flags & 1) != 0) {
      loops = (flags & 2) != 0;
      return offset - startAddress;
    }
  }
}

// A proposed sample-directory row must point forward to the start of a
// nine-byte block. A looping sample must keep its loop point inside the sample.
[[nodiscard]] bool sampleDirEntryLooksValid(ByteReader reader, u32 spcDirAddress, u8 srcn) {
  const u32 dirEntryAddress = spcDirAddress + srcn * 4;
  if (!reader.has(dirEntryAddress, 4)) {
    return false;
  }

  const u16 sampleStart = reader.le16(dirEntryAddress);
  const u16 sampleLoop = reader.le16(dirEntryAddress + 2);
  if (sampleStart < dirEntryAddress + 4 || !reader.has(sampleStart, 10)) {
    return false;
  }

  bool loops = false;
  const u32 length = inferredSampleLength(reader, sampleStart, loops);
  return length != 0 && (!loops || (sampleLoop >= sampleStart && sampleLoop < sampleStart + length &&
                                    ((sampleLoop - sampleStart) % 9) == 0));
}

[[nodiscard]] bool instrumentHeaderLooksValid(ByteReader reader, KonamiSnesVersion version, u32 address,
                                              u32 spcDirAddress) {
  if (!reader.has(address, instrumentHeaderSize(version))) {
    return false;
  }
  const u8 srcn = reader.u8At(address);
  const u32 panOffset = usesLegacyInstrumentLayout(version) ? 6 : 5;
  return srcn != 0xff && reader.u8At(address + panOffset) <= instrumentPanLimit(version) &&
         sampleDirEntryLooksValid(reader, spcDirAddress, srcn);
}

// Test each possible sample-directory page against a few rows from every
// instrument table. Requiring two matching rows avoids treating random RAM as
// a directory.
[[nodiscard]] u32 scoreInferredSpcDir(ByteReader reader, const KonamiSnesLayout& layout, u32 candidateDir) {
  if (!layout.commonInstrumentTableAddress || !layout.bankedInstrumentTableAddress ||
      !layout.percussionInstrumentTableAddress) {
    return 0;
  }

  const u32 headerSize = instrumentHeaderSize(layout.version);
  u32 score = 0;
  for (u32 i = 0; i < std::min<u32>(layout.firstBankedInstrument, 8); ++i) {
    if (instrumentHeaderLooksValid(reader, layout.version, *layout.commonInstrumentTableAddress + i * headerSize,
                                   candidateDir)) {
      ++score;
    }
  }
  const u32 bankedProbeCount = std::min<u32>(8, layout.bankedInstrumentEnd - layout.firstBankedInstrument);
  for (u32 i = 0; i < bankedProbeCount; ++i) {
    if (instrumentHeaderLooksValid(reader, layout.version, *layout.bankedInstrumentTableAddress + i * headerSize,
                                   candidateDir)) {
      ++score;
    }
  }
  for (u32 i = 0; i < 8; ++i) {
    if (instrumentHeaderLooksValid(reader, layout.version, *layout.percussionInstrumentTableAddress + i * headerSize,
                                   candidateDir)) {
      ++score;
    }
  }
  return score;
}

// Some drivers do not set the sample-directory register in recognizable code.
// In that case, sample numbers from instrument rows and valid compressed sample
// data reveal which page is active.
[[nodiscard]] std::optional<u32> inferSpcDirAddress(ByteReader reader, const KonamiSnesLayout& layout) {
  u32 bestDir = 0;
  u32 bestScore = 0;
  for (u32 candidateDir = 0; candidateDir <= 0xff00; candidateDir += 0x100) {
    const u32 score = scoreInferredSpcDir(reader, layout, candidateDir);
    if (score > bestScore) {
      bestScore = score;
      bestDir = candidateDir;
    }
  }
  if (bestScore >= 2) {
    return bestDir;
  }
  return std::nullopt;
}

}  // namespace

const char* konamiSnesVersionName(KonamiSnesVersion version) {
  switch (version) {
    case KONAMISNES_V1:
      return "v1";
    case KONAMISNES_V2:
      return "v2";
    case KONAMISNES_V3:
      return "v3";
    case KONAMISNES_V4:
      return "v4";
    case KONAMISNES_V5:
      return "v5";
    case KONAMISNES_V6:
      return "v6";
    case KONAMISNES_NONE:
      return "unknown";
  }
  return "unknown";
}

std::optional<KonamiSnesLayout> findKonamiSnesLayout(ByteReader reader) {
  // All addresses belong to the SNES sound CPU's 64 KiB memory. Refuse partial
  // dumps so a matching byte sequence cannot point outside the supplied data.
  if (reader.size() != kKonamiSnesAramSize) {
    return std::nullopt;
  }

  KonamiSnesLayout layout;

  u32 songHeaderAddress = 0;
  u8 vcmdLengthItemSize = 0;
  std::optional<u32> songListAddress;
  u8 firstCandidateSongIndex = 0;
  // Early engines select a five-byte song-list row; later engines keep the
  // active header address directly in driver RAM.
  if (const auto directHeaderOffset = findBytePattern(reader, kSetSongHeaderAddressGG4)) {
    // The two immediate bytes are the low and high halves of the RAM variable
    // that currently holds the song header address.
    songHeaderAddress = reader.u8At(*directHeaderOffset + 1) | (reader.u8At(*directHeaderOffset + 4) << 8);
    vcmdLengthItemSize = 2;
  } else if (const auto pntbSongListOffset = findBytePattern(reader, kReadSongListPNTB)) {
    songListAddress = reader.u8At(*pntbSongListOffset + 3) | (reader.u8At(*pntbSongListOffset + 6) << 8);
    firstCandidateSongIndex = reader.u8At(*pntbSongListOffset + 31);
    vcmdLengthItemSize = 1;
  } else if (const auto axeSongListOffset = findBytePattern(reader, kReadSongListAXE)) {
    songListAddress = reader.u8At(*axeSongListOffset + 3) | (reader.u8At(*axeSongListOffset + 6) << 8);
    firstCandidateSongIndex = reader.u8At(*axeSongListOffset + 32);
    vcmdLengthItemSize = 2;
  } else if (const auto cntr3SongListOffset = findBytePattern(reader, kReadSongListCNTR3)) {
    songListAddress = reader.u8At(*cntr3SongListOffset + 3) | (reader.u8At(*cntr3SongListOffset + 6) << 8);
    firstCandidateSongIndex = reader.u8At(*cntr3SongListOffset + 32);
    vcmdLengthItemSize = 1;
  } else if (const auto brSongListOffset = findBytePattern(reader, kReadSongListBR)) {
    songListAddress = reader.u8At(*brSongListOffset + 3) | (reader.u8At(*brSongListOffset + 6) << 8);
    firstCandidateSongIndex = reader.u8At(*brSongListOffset + 32);
    vcmdLengthItemSize = 1;
  } else {
    return std::nullopt;
  }
  u32 vcmdLengthTableAddress = 0;
  u8 vcmd6xCountInList = 0;
  // The command-length lookup identifies both the table bounds and which early
  // 0x60 command family the driver supports.
  if (const auto gg4VcmdOffset = findBytePattern(reader, kJumpToVcmdGG4)) {
    vcmdLengthTableAddress = reader.le16(*gg4VcmdOffset + 11);
    vcmd6xCountInList = 0;
  } else if (const auto cntr3VcmdOffset = findBytePattern(reader, kJumpToVcmdCNTR3)) {
    vcmdLengthTableAddress = reader.le16(*cntr3VcmdOffset + 17);
    if (findBytePattern(reader, kBranchForVcmd6xCNTR3)) {
      vcmd6xCountInList = 5;
    } else if (findBytePattern(reader, kBranchForVcmd6xMDR2)) {
      vcmd6xCountInList = 2;
    } else {
      return std::nullopt;
    }
  } else {
    return std::nullopt;
  }

  const u32 vcmdTableSize = (vcmd6xCountInList + 0x20) * vcmdLengthItemSize;
  // Every driver has 32 commands from 0xe0 through 0xff. Early builds prepend
  // two or five extra handlers for commands in the 0x60 range.
  if (!reader.has(vcmdLengthTableAddress, vcmdTableSize)) {
    return std::nullopt;
  }

  // Early versions are distinguished by their extra 0x60 handlers. Later
  // versions are distinguished by two opcode lengths in the same table.
  if (songListAddress) {
    if (vcmd6xCountInList == 5) {
      layout.version = KONAMISNES_V1;
    } else if (vcmd6xCountInList == 2) {
      layout.version = KONAMISNES_V2;
    } else {
      layout.version = KONAMISNES_V3;
    }
  } else {
    if (reader.u8At(vcmdLengthTableAddress + (0xed - 0xe0) * vcmdLengthItemSize) == 3) {
      layout.version = KONAMISNES_V4;
    } else if (reader.u8At(vcmdLengthTableAddress + (0xfc - 0xe0) * vcmdLengthItemSize) == 2) {
      layout.version = KONAMISNES_V5;
    } else {
      layout.version = KONAMISNES_V6;
    }
  }
  layout.indexedEchoFilter =
      std::ranges::any_of(kIndexedEchoFilters,
                          [&](const auto& pattern) { return findBytePattern(reader, pattern).has_value(); });

  if (songListAddress) {
    // The comparison immediate gives the first song-table index to inspect.
    // Each entry is five bytes; skip entries with a null sequence-header pointer.
    u8 songIndex = firstCandidateSongIndex;
    while (true) {
      const u32 pointerOffset = *songListAddress + songIndex * 5 + 3;
      if (!reader.has(pointerOffset, 2)) {
        return std::nullopt;
      }
      songHeaderAddress = reader.le16(pointerOffset);
      if (songHeaderAddress != 0) {
        break;
      }
      if (songIndex == 0xff) {
        return std::nullopt;
      }
      ++songIndex;
    }
  }

  if (!reader.has(songHeaderAddress, 2)) {
    return std::nullopt;
  }
  // The header itself is a compact list of track pointers. Its first pointer
  // later tells the sequence parser how many entries are present.
  layout.sequenceHeaderAddress = songHeaderAddress;

  // The SNES DIR register selects the page containing the sample directory.
  // Prefer an explicit write to that register when the driver code has one.
  if (const auto gg4DirOffset = findBytePattern(reader, kSetDIRGG4)) {
    layout.spcDirAddress = static_cast<u32>(reader.u8At(*gg4DirOffset + 4)) << 8;
  } else if (const auto cntr3DirOffset = findBytePattern(reader, kSetDIRCNTR3)) {
    layout.spcDirAddress = static_cast<u32>(reader.u8At(*cntr3DirOffset + 1)) << 8;
  }

  u32 loadInstrumentOffset = 0;
  enum class InstrumentPattern {
    None,
    Jop,
    Gp,
    Gg4,
    Pntb,
    Cntr3,
  } pattern = InstrumentPattern::None;
  // Each known loader exposes the same final tables through a slightly
  // different instruction sequence; keep those layouts explicit here.
  if (const auto jopInstrumentOffset = findBytePattern(reader, kLoadInstrJOP)) {
    loadInstrumentOffset = *jopInstrumentOffset;
    pattern = InstrumentPattern::Jop;
  } else if (const auto gpInstrumentOffset = findBytePattern(reader, kLoadInstrGP)) {
    loadInstrumentOffset = *gpInstrumentOffset;
    pattern = InstrumentPattern::Gp;
  } else if (const auto gg4InstrumentOffset = findBytePattern(reader, kLoadInstrGG4)) {
    loadInstrumentOffset = *gg4InstrumentOffset;
    pattern = InstrumentPattern::Gg4;
  } else if (const auto pntbInstrumentOffset = findBytePattern(reader, kLoadInstrPNTB)) {
    loadInstrumentOffset = *pntbInstrumentOffset;
    pattern = InstrumentPattern::Pntb;
  } else if (const auto cntr3InstrumentOffset = findBytePattern(reader, kLoadInstrCNTR3)) {
    loadInstrumentOffset = *cntr3InstrumentOffset;
    pattern = InstrumentPattern::Cntr3;
  }

  switch (pattern) {
    // The offsets below point at immediate operands in each matched loader.
    // Although the instruction sequences differ, all five reveal the common
    // table, the first banked program, the selected bank table, and drums.
    case InstrumentPattern::Jop: {
      layout.commonInstrumentTableAddress =
          reader.u8At(loadInstrumentOffset + 8) | (reader.u8At(loadInstrumentOffset + 11) << 8);
      layout.firstBankedInstrument = reader.u8At(loadInstrumentOffset + 4);
      layout.bankedInstrumentTableAddress = bankedTableByCurrentBank(reader, reader.le16(loadInstrumentOffset + 23),
                                                                     reader.le16(loadInstrumentOffset + 26), false);
      layout.percussionInstrumentTableAddress = percussionTableFromGG4Pattern(reader);
      break;
    }
    case InstrumentPattern::Gp: {
      layout.commonInstrumentTableAddress =
          reader.u8At(loadInstrumentOffset + 14) | (reader.u8At(loadInstrumentOffset + 17) << 8);
      layout.firstBankedInstrument = reader.u8At(loadInstrumentOffset + 10);
      layout.bankedInstrumentTableAddress = bankedTableByCurrentBank(reader, reader.u8At(loadInstrumentOffset + 29),
                                                                     reader.le16(loadInstrumentOffset + 31), false);
      layout.percussionInstrumentTableAddress = percussionTableFromGG4Pattern(reader);
      break;
    }
    case InstrumentPattern::Gg4: {
      layout.commonInstrumentTableAddress =
          reader.u8At(loadInstrumentOffset + 15) | (reader.u8At(loadInstrumentOffset + 18) << 8);
      layout.firstBankedInstrument = reader.u8At(loadInstrumentOffset + 11);
      layout.bankedInstrumentTableAddress = bankedTableByCurrentBank(reader, reader.u8At(loadInstrumentOffset + 30),
                                                                     reader.le16(loadInstrumentOffset + 32), false);
      layout.percussionInstrumentTableAddress = percussionTableFromGG4Pattern(reader);
      break;
    }
    case InstrumentPattern::Pntb: {
      layout.commonInstrumentTableAddress =
          reader.u8At(loadInstrumentOffset + 12) | (reader.u8At(loadInstrumentOffset + 15) << 8);
      layout.firstBankedInstrument = reader.u8At(loadInstrumentOffset + 8);
      layout.bankedInstrumentTableAddress = bankedTableByCurrentBank(reader, reader.u8At(loadInstrumentOffset + 27),
                                                                     reader.le16(loadInstrumentOffset + 29), false);
      layout.percussionInstrumentTableAddress =
          reader.u8At(loadInstrumentOffset + 46) | (reader.u8At(loadInstrumentOffset + 49) << 8);
      break;
    }
    case InstrumentPattern::Cntr3: {
      layout.commonInstrumentTableAddress =
          reader.u8At(loadInstrumentOffset + 15) | (reader.u8At(loadInstrumentOffset + 19) << 8);
      layout.firstBankedInstrument = reader.u8At(loadInstrumentOffset + 11);
      const u8 bankedInstrumentEnd = reader.u8At(loadInstrumentOffset + 7);
      if (bankedInstrumentEnd > layout.firstBankedInstrument) {
        layout.bankedInstrumentEnd = bankedInstrumentEnd;
      }
      layout.bankedInstrumentTableAddress = bankedTableByCurrentBank(reader, reader.le16(loadInstrumentOffset + 32),
                                                                     reader.le16(loadInstrumentOffset + 37), true);
      layout.percussionInstrumentTableAddress =
          reader.u8At(loadInstrumentOffset + 59) | (reader.u8At(loadInstrumentOffset + 63) << 8);
      break;
    }
    case InstrumentPattern::None:
      break;
  }

  if (!layout.spcDirAddress) {
    // Some snapshots were captured after sample-directory setup had run or use
    // an unrecognized setup sequence. Validate every possible page against the
    // instrument rows rather than discarding otherwise valid data.
    layout.spcDirAddress = inferSpcDirAddress(reader, layout);
  }

  return layout;
}

}  // namespace vgmtrans::formats::konami_snes
