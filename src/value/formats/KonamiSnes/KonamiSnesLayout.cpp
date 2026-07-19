/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/KonamiSnes/KonamiSnesLayout.h"

#include <algorithm>
#include <optional>
#include <string_view>

namespace vgmtrans::formats::konami_snes {

using namespace core;
using namespace std::string_view_literals;

namespace {

struct BytePatternView {
  std::string_view bytes;
  std::string_view mask;
};

constexpr BytePatternView kSetSongHeaderAddressGG4{
    "\x8f\x00\x0a\x8f\x39\x0b\xcd\x00\xd8\x1c"sv,
    "x??x??xxx?"sv,
};

constexpr BytePatternView kReadSongListPNTB{
    "\xc4\x0c\x8f\x1b\x04\x8f\x05\x05\x8d\x05\xcf\x7a\x04\xda\x04\x8d"
    "\x00\xcd\x00\xf7\x04\xc4\x1a\xfc\xf7\x04\xc4\x06\xe4\x0c\x68\x41"
    "\xb0\x3b"sv,
    "x?x??x??xxxx?x?xxxxx?x?xx?x?x?x?x?"sv,
};

constexpr BytePatternView kReadSongListAXE{
    "\xe4\x0c\x8f\xe6\x04\x8f\x03\x05\x9c\x8d\x05\xcf\x7a\x04\xda\x04"
    "\x8d\x00\xcd\x00\xf7\x04\xc4\x20\xfc\xf7\x04\xc4\x06\xe4\x0c\x68"
    "\x4d\xb0\x5a\xcd\x0c\x68\x41\xb0\x4a"sv,
    "x?x??x??xxxxx?x?xxxxx?x?xx?x?x?x?x?x?x?x?"sv,
};

constexpr BytePatternView kReadSongListCNTR3{
    "\xe4\x0c\x8f\xe6\x04\x8f\x03\x05\x9c\x8d\x05\xcf\x7a\x04\xda\x04"
    "\x8d\x00\xcd\x00\xf7\x04\xc4\x20\xfc\xf7\x04\xc4\x06\xe4\x0c\x68"
    "\x60\xb0\x64\x68\x5c\x90\x0a"sv,
    "x?x??x??xxxxx?x?xxxxx?x?xx?x?x?x?x?x?x?"sv,
};

constexpr BytePatternView kJumpToVcmdGG4{
    "\x1c\xfd\xf6\xbc\x1a\x2d\xf6\xbb\x1a\x2d\xf6\xfb\x1a\xf0\x08"sv,
    "xxx??xx??xx??x?"sv,
};

constexpr BytePatternView kJumpToVcmdCNTR3{
    "\x80\xa4\x04\x1c\xfd\xf6\xde\x0d\x2d\xf6\xdd\x0d\x2d\xdd\x5c\xfd"
    "\xf6\x27\x0e\xf0\x08"sv,
    "xx?xxx??xx??xxxxx??x?"sv,
};

constexpr BytePatternView kBranchForVcmd6xMDR2{
    "\xe4\x08\x8f\xde\x04\x68\xe0\xb0\x0c\x8f\x60\x04\x68\x62\x90\x05"sv,
    "x?x??xxx?x??xxx?"sv,
};

constexpr BytePatternView kBranchForVcmd6xCNTR3{
    "\xe4\x08\x8f\xdb\x04\x68\xe0\xb0\x0c\x68\x65\x90\x05"sv,
    "x?x??xxx?xx??"sv,
};

constexpr BytePatternView kSetDIRGG4{
    "\x8f\x5d\xf2\x8f\x04\xf3"sv,
    "xxxx?x"sv,
};

constexpr BytePatternView kSetDIRCNTR3{
    "\xe8\x50\x8d\x5d\xcc\xf2\x00\xc5\xf3\x00"sv,
    "x?xxxxxxxx"sv,
};

constexpr BytePatternView kLoadInstrJOP{
    "\x09\x11\x10\x68\x24\xb0\x0c\x8f\xa0\x04\x8f\x05\x05\x3f\xf5\x17"
    "\x5f\x12\x15\xa8\x24\x2d\xec\xe0\x01\xf6\x8a\x05\xc4\x04\xf6\x8b"
    "\x05\xc4\x05\xae\x3f\xf5\x17\x5f\x12\x15"sv,
    "x??x?xxx??x??x??x??x?xx??x??x?x??x?xx??x??"sv,
};

constexpr BytePatternView kLoadInstrGP{
    "\x09\x11\x10\xfd\xf4\xd1\xd0\x2d\xdd\x68\x1f\xb0\x0c\x8f\x68\x04"
    "\x8f\x05\x05\x3f\x31\x14\x5f\x45\x11\xa8\x1f\x2d\xeb\x25\xf6\x58"
    "\x05\xc4\x04\xf6\x59\x05\xc4\x05\xae\x3f\x31\x14\x5f\x45\x11"sv,
    "x??xx?x?xx?xxx??x??x??x??x?xx?x??x?x??x?xx??x??"sv,
};

constexpr BytePatternView kLoadInstrGG4{
    "\x09\x11\x10\xfd\xf5\xa1\x01\xd0\x27\xdd\x68\x28\xb0\x0c\x8f\x3c"
    "\x04\x8f\x0a\x05\x3f\xee\x1b\x5f\xe2\x18\xa8\x28\x2d\xeb\x25\xf6"
    "\x20\x0a\xc4\x04\xf6\x21\x0a\xc4\x05\xae\x3f\xee\x1b\x5f\xe2\x18"sv,
    "x??xx??x?xx?xxx??x??x??x??x?xx?x??x?x??x?xx??x??"sv,
};

constexpr BytePatternView kLoadInstrPNTB{
    "\x09\x1a\x11\x68\xf0\xb0\xda\x68\x1d\xb0\x0c\x8f\x00\x04\x8f\x07"
    "\x05\x3f\x3e\x13\x5f\x61\x10\xa8\x1d\x2d\xeb\x24\xf6\xf4\x06\xc4"
    "\x04\xf6\xf5\x06\xc4\x05\xae\x3f\x3e\x13\x5f\x61\x10\x8f\xe8\x04"
    "\x8f\x07\x05\x1c\x1c\x1c\x8d\x00\x7a\x04\xda\x04"sv,
    "x??x?x?x?xxx??x??x??x??x?xx?x??x?x??x?xx??x??x??x??xxxxxx?x?"sv,
};

constexpr BytePatternView kLoadInstrCNTR3{
    "\x09\x20\x17\xd5\x97\x02\x68\x19\xb0\x04\x68\x14\xb0\x11\xe8\x36"
    "\xc4\x04\xe8\x06\xc4\x05\xf5\x97\x02\x3f\x36\x0f\x5f\x8c\x0c\xe5"
    "\x05\x02\x1c\xfd\xf6\x28\x06\xc4\x04\xf6\x29\x06\xc4\x05\xf5\x97"
    "\x02\x80\xa8\x14\x3f\x36\x0f\x5f\x8c\x0c\xe8\xfe\xc4\x04\xe8\x06"
    "\xc4\x05\xf5\x97\x02\x8d\x08\xcf\x7a\x04\xda\x04"sv,
    "x??x??x?xxx?xxx?x?x?x?x??x??x??x??xxx??x?x??x?x??xx?x??x??x?x?x?x?x??xxxx?x?"sv,
};

constexpr BytePatternView kLoadPercInstrGG4{
    "\x8f\xe6\x04\x8f\x0d\x05\x8d\x07\xcf\x7a\x04\xda\x04"sv,
    "x??x??xxxx?x?"sv,
};

static_assert(kSetSongHeaderAddressGG4.bytes.size() == kSetSongHeaderAddressGG4.mask.size());
static_assert(kReadSongListPNTB.bytes.size() == kReadSongListPNTB.mask.size());
static_assert(kReadSongListAXE.bytes.size() == kReadSongListAXE.mask.size());
static_assert(kReadSongListCNTR3.bytes.size() == kReadSongListCNTR3.mask.size());
static_assert(kJumpToVcmdGG4.bytes.size() == kJumpToVcmdGG4.mask.size());
static_assert(kJumpToVcmdCNTR3.bytes.size() == kJumpToVcmdCNTR3.mask.size());
static_assert(kBranchForVcmd6xMDR2.bytes.size() == kBranchForVcmd6xMDR2.mask.size());
static_assert(kBranchForVcmd6xCNTR3.bytes.size() == kBranchForVcmd6xCNTR3.mask.size());
static_assert(kSetDIRGG4.bytes.size() == kSetDIRGG4.mask.size());
static_assert(kSetDIRCNTR3.bytes.size() == kSetDIRCNTR3.mask.size());
static_assert(kLoadInstrJOP.bytes.size() == kLoadInstrJOP.mask.size());
static_assert(kLoadInstrGP.bytes.size() == kLoadInstrGP.mask.size());
static_assert(kLoadInstrGG4.bytes.size() == kLoadInstrGG4.mask.size());
static_assert(kLoadInstrPNTB.bytes.size() == kLoadInstrPNTB.mask.size());
static_assert(kLoadInstrCNTR3.bytes.size() == kLoadInstrCNTR3.mask.size());
static_assert(kLoadPercInstrGG4.bytes.size() == kLoadPercInstrGG4.mask.size());

[[nodiscard]] bool matchPattern(ByteReader reader, u64 offset, BytePatternView pattern) {
  if (pattern.bytes.size() != pattern.mask.size() || !reader.has(offset, pattern.bytes.size())) {
    return false;
  }

  for (size_t i = 0; i < pattern.bytes.size(); ++i) {
    if (pattern.mask[i] == 'x' && reader.u8At(offset + i) != static_cast<u8>(pattern.bytes[i])) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] std::optional<u32> searchPattern(ByteReader reader, BytePatternView pattern) {
  if (pattern.bytes.size() != pattern.mask.size() || pattern.bytes.empty() || pattern.bytes.size() > reader.size()) {
    return std::nullopt;
  }
  for (u64 offset = 0; offset <= reader.size() - pattern.bytes.size(); ++offset) {
    if (matchPattern(reader, offset, pattern)) {
      return static_cast<u32>(offset);
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<u32> percussionTableFromGG4Pattern(ByteReader reader) {
  if (const auto offset = searchPattern(reader, kLoadPercInstrGG4)) {
    return static_cast<u32>(reader.u8At(*offset + 1) | (reader.u8At(*offset + 4) << 8));
  }
  return std::nullopt;
}

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

[[nodiscard]] bool sampleDirEntryLooksValid(ByteReader reader, u32 spcDirAddress, u8 srcn) {
  const u32 dirEntryAddress = spcDirAddress + srcn * 4;
  if (!reader.has(dirEntryAddress, 4)) {
    return false;
  }

  const u16 sampleStart = reader.le16(dirEntryAddress);
  const u16 sampleLoop = reader.le16(dirEntryAddress + 2);
  if (sampleStart < dirEntryAddress + 4 || sampleLoop < sampleStart || !reader.has(sampleStart, 10) ||
      ((sampleLoop - sampleStart) % 9) != 0) {
    return false;
  }

  bool loops = false;
  const u32 length = inferredSampleLength(reader, sampleStart, loops);
  return length != 0 && (!loops || sampleLoop < sampleStart + length);
}

[[nodiscard]] bool instrumentHeaderLooksValid(ByteReader reader, KonamiSnesVersion version, u32 address,
                                              u32 spcDirAddress) {
  if (!reader.has(address, instrumentHeaderSize(version))) {
    return false;
  }
  const u8 srcn = reader.u8At(address);
  return srcn != 0xff && sampleDirEntryLooksValid(reader, spcDirAddress, srcn);
}

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
  for (u32 i = 0; i < 8; ++i) {
    if (instrumentHeaderLooksValid(reader, layout.version, *layout.bankedInstrumentTableAddress + i * headerSize,
                                   candidateDir)) {
      ++score;
    }
    if (instrumentHeaderLooksValid(reader, layout.version, *layout.percussionInstrumentTableAddress + i * headerSize,
                                   candidateDir)) {
      ++score;
    }
  }
  return score;
}

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
  if (reader.size() != kKonamiSnesAramSize) {
    return std::nullopt;
  }

  KonamiSnesLayout layout;

  bool hasSongList = false;
  u32 songListAddress = 0;
  u32 songHeaderAddress = 0;
  u8 primarySongIndex = 0;
  if (const auto directHeaderOffset = searchPattern(reader, kSetSongHeaderAddressGG4)) {
    songHeaderAddress = reader.u8At(*directHeaderOffset + 1) | (reader.u8At(*directHeaderOffset + 4) << 8);
    layout.vcmdLengthItemSize = 2;
  } else if (const auto pntbSongListOffset = searchPattern(reader, kReadSongListPNTB)) {
    songListAddress = reader.u8At(*pntbSongListOffset + 3) | (reader.u8At(*pntbSongListOffset + 6) << 8);
    primarySongIndex = reader.u8At(*pntbSongListOffset + 31);
    layout.vcmdLengthItemSize = 1;
    hasSongList = true;
  } else if (const auto axeSongListOffset = searchPattern(reader, kReadSongListAXE)) {
    songListAddress = reader.u8At(*axeSongListOffset + 3) | (reader.u8At(*axeSongListOffset + 6) << 8);
    primarySongIndex = reader.u8At(*axeSongListOffset + 32);
    layout.vcmdLengthItemSize = 2;
    hasSongList = true;
  } else if (const auto cntr3SongListOffset = searchPattern(reader, kReadSongListCNTR3)) {
    songListAddress = reader.u8At(*cntr3SongListOffset + 3) | (reader.u8At(*cntr3SongListOffset + 6) << 8);
    primarySongIndex = reader.u8At(*cntr3SongListOffset + 32);
    layout.vcmdLengthItemSize = 1;
    hasSongList = true;
  } else {
    return std::nullopt;
  }
  layout.hasSongList = hasSongList;

  u32 vcmdLengthTableAddress = 0;
  if (const auto gg4VcmdOffset = searchPattern(reader, kJumpToVcmdGG4)) {
    vcmdLengthTableAddress = reader.le16(*gg4VcmdOffset + 11);
    layout.vcmd6xCountInList = 0;
  } else if (const auto cntr3VcmdOffset = searchPattern(reader, kJumpToVcmdCNTR3)) {
    vcmdLengthTableAddress = reader.le16(*cntr3VcmdOffset + 17);
    if (searchPattern(reader, kBranchForVcmd6xCNTR3)) {
      layout.vcmd6xCountInList = 5;
    } else if (searchPattern(reader, kBranchForVcmd6xMDR2)) {
      layout.vcmd6xCountInList = 2;
    } else {
      return std::nullopt;
    }
  } else {
    return std::nullopt;
  }

  const u32 vcmdTableSize = (layout.vcmd6xCountInList + 0x20) * layout.vcmdLengthItemSize;
  if (!reader.has(vcmdLengthTableAddress, vcmdTableSize)) {
    return std::nullopt;
  }

  if (hasSongList) {
    if (layout.vcmd6xCountInList == 5) {
      layout.version = KONAMISNES_V1;
    } else if (layout.vcmd6xCountInList == 2) {
      layout.version = KONAMISNES_V2;
    } else {
      layout.version = KONAMISNES_V3;
    }
  } else {
    if (reader.u8At(vcmdLengthTableAddress + (0xed - 0xe0) * layout.vcmdLengthItemSize) == 3) {
      layout.version = KONAMISNES_V4;
    } else if (reader.u8At(vcmdLengthTableAddress + (0xfc - 0xe0) * layout.vcmdLengthItemSize) == 2) {
      layout.version = KONAMISNES_V5;
    } else {
      layout.version = KONAMISNES_V6;
    }
  }

  if (hasSongList) {
    u8 songIndex = primarySongIndex;
    while (true) {
      const u32 pointerOffset = songListAddress + songIndex * 5 + 3;
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
  layout.sequenceHeaderAddress = songHeaderAddress;

  if (const auto gg4DirOffset = searchPattern(reader, kSetDIRGG4)) {
    layout.spcDirAddress = static_cast<u32>(reader.u8At(*gg4DirOffset + 4)) << 8;
  } else if (const auto cntr3DirOffset = searchPattern(reader, kSetDIRCNTR3)) {
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
  if (const auto jopInstrumentOffset = searchPattern(reader, kLoadInstrJOP)) {
    loadInstrumentOffset = *jopInstrumentOffset;
    pattern = InstrumentPattern::Jop;
  } else if (const auto gpInstrumentOffset = searchPattern(reader, kLoadInstrGP)) {
    loadInstrumentOffset = *gpInstrumentOffset;
    pattern = InstrumentPattern::Gp;
  } else if (const auto gg4InstrumentOffset = searchPattern(reader, kLoadInstrGG4)) {
    loadInstrumentOffset = *gg4InstrumentOffset;
    pattern = InstrumentPattern::Gg4;
  } else if (const auto pntbInstrumentOffset = searchPattern(reader, kLoadInstrPNTB)) {
    loadInstrumentOffset = *pntbInstrumentOffset;
    pattern = InstrumentPattern::Pntb;
  } else if (const auto cntr3InstrumentOffset = searchPattern(reader, kLoadInstrCNTR3)) {
    loadInstrumentOffset = *cntr3InstrumentOffset;
    pattern = InstrumentPattern::Cntr3;
  }

  switch (pattern) {
    case InstrumentPattern::Jop: {
      layout.commonInstrumentTableAddress =
          reader.u8At(loadInstrumentOffset + 8) | (reader.u8At(loadInstrumentOffset + 11) << 8);
      layout.firstBankedInstrument = reader.u8At(loadInstrumentOffset + 4);
      layout.bankedInstrumentTableAddress =
          bankedTableByCurrentBank(reader, reader.le16(loadInstrumentOffset + 23),
                                   reader.le16(loadInstrumentOffset + 26), false);
      layout.percussionInstrumentTableAddress = percussionTableFromGG4Pattern(reader);
      break;
    }
    case InstrumentPattern::Gp: {
      layout.commonInstrumentTableAddress =
          reader.u8At(loadInstrumentOffset + 14) | (reader.u8At(loadInstrumentOffset + 17) << 8);
      layout.firstBankedInstrument = reader.u8At(loadInstrumentOffset + 10);
      layout.bankedInstrumentTableAddress =
          bankedTableByCurrentBank(reader, reader.u8At(loadInstrumentOffset + 29),
                                   reader.le16(loadInstrumentOffset + 31), false);
      layout.percussionInstrumentTableAddress = percussionTableFromGG4Pattern(reader);
      break;
    }
    case InstrumentPattern::Gg4: {
      layout.commonInstrumentTableAddress =
          reader.u8At(loadInstrumentOffset + 15) | (reader.u8At(loadInstrumentOffset + 18) << 8);
      layout.firstBankedInstrument = reader.u8At(loadInstrumentOffset + 11);
      layout.bankedInstrumentTableAddress =
          bankedTableByCurrentBank(reader, reader.u8At(loadInstrumentOffset + 30),
                                   reader.le16(loadInstrumentOffset + 32), false);
      layout.percussionInstrumentTableAddress = percussionTableFromGG4Pattern(reader);
      break;
    }
    case InstrumentPattern::Pntb: {
      layout.commonInstrumentTableAddress =
          reader.u8At(loadInstrumentOffset + 12) | (reader.u8At(loadInstrumentOffset + 15) << 8);
      layout.firstBankedInstrument = reader.u8At(loadInstrumentOffset + 8);
      layout.bankedInstrumentTableAddress =
          bankedTableByCurrentBank(reader, reader.u8At(loadInstrumentOffset + 27),
                                   reader.le16(loadInstrumentOffset + 29), false);
      layout.percussionInstrumentTableAddress =
          reader.u8At(loadInstrumentOffset + 46) | (reader.u8At(loadInstrumentOffset + 49) << 8);
      break;
    }
    case InstrumentPattern::Cntr3: {
      layout.commonInstrumentTableAddress =
          reader.u8At(loadInstrumentOffset + 15) | (reader.u8At(loadInstrumentOffset + 19) << 8);
      layout.firstBankedInstrument = reader.u8At(loadInstrumentOffset + 11);
      layout.bankedInstrumentTableAddress =
          bankedTableByCurrentBank(reader, reader.le16(loadInstrumentOffset + 32),
                                   reader.le16(loadInstrumentOffset + 37), true);
      layout.percussionInstrumentTableAddress =
          reader.u8At(loadInstrumentOffset + 59) | (reader.u8At(loadInstrumentOffset + 63) << 8);
      break;
    }
    case InstrumentPattern::None:
      break;
  }

  if (!layout.spcDirAddress) {
    layout.spcDirAddress = inferSpcDirAddress(reader, layout);
  }

  return layout;
}

}  // namespace vgmtrans::formats::konami_snes
