/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/CapcomSnes/CapcomSnesLayout.h"
#include "value/scan/BytePattern.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <limits>
#include <map>
#include <span>
#include <string_view>

namespace vgmtrans::formats::capcom_snes {

using namespace core;

namespace {

// These signatures identify driver routines whose operands reveal runtime table addresses.
constexpr std::array<u8, 16> kReadSongListPattern{0x1c, 0x5d, 0xf5, 0x03, 0x0e, 0xc4, 0xc0, 0xf5,
                                                  0x02, 0x0e, 0xc4, 0xc1, 0x04, 0xc0, 0xf0, 0xdd};
constexpr std::string_view kReadSongListMask = "xxx??x?x??x?x?x?";

constexpr std::array<u8, 16> kReadBgmAddressPattern{0x6f, 0x3f, 0xef, 0x06, 0x8f, 0x0d, 0xa1, 0x8f,
                                                    0xaf, 0xa0, 0x3f, 0x82, 0x05, 0x8d, 0x00, 0xdd};
constexpr std::string_view kReadBgmAddressMask = "xx??x??x??x??xxx";

constexpr std::array<u8, 16> kDspRegInitPattern{0x8d, 0x03, 0xf6, 0x63, 0x04, 0xc5, 0xf2, 0x00,
                                                0xf6, 0x66, 0x04, 0xc5, 0xf3, 0x00, 0xfe, 0xf2};
constexpr std::string_view kDspRegInitMask = "x?x??xxxx??xxxx?";

constexpr std::array<u8, 15> kDspRegInitOldPattern{0xf5, 0xf9, 0x0b, 0xfd, 0xf5, 0x05, 0x0c, 0x3f,
                                                   0xf2, 0x0b, 0x3d, 0xc8, 0x0c, 0xd0, 0xf1};
constexpr std::string_view kDspRegInitOldMask = "x??xx??x??xx?x?";

constexpr std::array<u8, 12> kLoadInstrTablePattern{0x8d, 0x06, 0xcf, 0xda, 0xa0, 0x60,
                                                    0x98, 0xac, 0xa0, 0x98, 0x47, 0xa1};
constexpr std::string_view kLoadInstrTableMask = "xxxx?xx??x??";

[[nodiscard]] bool isValidBgmHeader(ByteReader reader, u32 address) {
  if (!reader.has(address, 17)) {
    return false;
  }

  for (u32 track = 0; track < kCapcomSnesMaxTracks; ++track) {
    const u16 trackAddress = reader.be16(address + 1 + track * 2);
    if ((trackAddress & 0xff00) == 0) {
      return false;
    }
  }

  return true;
}

[[nodiscard]] int songListLength(ByteReader reader, u16 songListAddress) {
  int length = 0;
  for (int songIndex = 0; songIndex <= 0x7f; ++songIndex) {
    const u32 pointerAddress = songListAddress + songIndex * 2;
    if (!reader.has(pointerAddress, 2)) {
      break;
    }

    const u16 songHeaderAddress = reader.be16(pointerAddress);
    if (songHeaderAddress == 0) {
      ++length;
      continue;
    }
    if (!isValidBgmHeader(reader, songHeaderAddress)) {
      break;
    }

    ++length;
  }
  return length;
}

[[nodiscard]] u16 currentPlayAddress(ByteReader reader, CapcomSnesEngineVersion version, u8 channel) {
  if (version == CapcomSnesEngineVersion::v1BgmInList) {
    return static_cast<u16>(reader.u8At(0x00 + channel * 2 + 1) | (reader.u8At(0x10 + channel * 2 + 1) << 8));
  }
  return static_cast<u16>(reader.u8At(0x00 + channel) | (reader.u8At(0x08 + channel) << 8));
}

[[nodiscard]] std::optional<u8> guessCurrentSong(ByteReader reader, CapcomSnesEngineVersion version,
                                                 u16 songListAddress) {
  // Song-list SPC dumps only expose the current playback cursor, so choose the nearest valid header.
  std::optional<u8> guessedSongIndex;
  int bestScore = std::numeric_limits<int>::max();

  const int length = songListLength(reader, songListAddress);
  for (int songIndex = 0; songIndex < length; ++songIndex) {
    const u16 songHeaderAddress = reader.be16(songListAddress + songIndex * 2);
    if (songHeaderAddress == 0) {
      continue;
    }

    int score = 0;
    int validTrackCount = 0;
    for (u32 track = 0; track < kCapcomSnesMaxTracks; ++track) {
      const u16 trackStart = reader.be16(songHeaderAddress + 1 + track * 2);
      const u16 currentAddress = currentPlayAddress(reader, version, static_cast<u8>(7 - track));
      if (currentAddress == 0) {
        continue;
      }
      if (trackStart > currentAddress) {
        validTrackCount = 0;
        break;
      }

      score += currentAddress - trackStart;
      ++validTrackCount;
    }

    if (validTrackCount > 0) {
      score = (score * 16) / validTrackCount;
      if (score < bestScore) {
        bestScore = score;
        guessedSongIndex = static_cast<u8>(songIndex);
      }
    }
  }

  return guessedSongIndex;
}

[[nodiscard]] std::map<u8, u8> initialDspRegisterMap(ByteReader reader) {
  std::map<u8, u8> registers;

  // The DIR base is usually written through the driver's DSP register initialization table.
  u32 registerCount = 0;
  u32 registerListAddress = 0;
  u32 valueListAddress = 0;

  if (const auto modernOffset = findBytePattern(reader, MaskedBytePattern{kDspRegInitPattern, kDspRegInitMask})) {
    registerCount = reader.u8At(*modernOffset + 1);
    registerListAddress = reader.le16(*modernOffset + 3) + 1;
    valueListAddress = reader.le16(*modernOffset + 9) + 1;
  } else if (const auto oldOffset =
                 findBytePattern(reader, MaskedBytePattern{kDspRegInitOldPattern, kDspRegInitOldMask})) {
    registerCount = reader.u8At(*oldOffset + 12);
    registerListAddress = reader.le16(*oldOffset + 1);
    valueListAddress = reader.le16(*oldOffset + 5);
  } else {
    return registers;
  }

  if (!reader.has(registerListAddress, registerCount) || !reader.has(valueListAddress, registerCount)) {
    return registers;
  }

  for (u32 i = 0; i < registerCount; ++i) {
    registers[reader.u8At(registerListAddress + i)] = reader.u8At(valueListAddress + i);
  }

  return registers;
}

}  // namespace

std::string capcomSnesSourceDisplayName(const SourceFile& source) {
  if (source.title && !source.title->empty()) {
    return *source.title;
  }
  if (!source.name.empty()) {
    return std::filesystem::path(source.name).stem().string();
  }
  if (!source.path.empty()) {
    return source.path.stem().string();
  }
  return "CapcomSnes";
}

std::optional<CapcomSnesLayout> findCapcomSnesLayout(ByteReader reader) {
  // CapcomSnes SPC dumps have no declarative header. Layout discovery reconstructs the
  // current driver's table addresses by recognizing small snippets of SPC700 code.
  if (reader.size() != kCapcomSnesAramSize) {
    return std::nullopt;
  }

  CapcomSnesLayout layout;

  if (const auto offset = findBytePattern(reader, MaskedBytePattern{kReadSongListPattern, kReadSongListMask})) {
    layout.hasSongList = true;
    layout.songListAddress = std::min(reader.le16(*offset + 3), reader.le16(*offset + 8));
  }

  if (const auto offset = findBytePattern(reader, MaskedBytePattern{kReadBgmAddressPattern, kReadBgmAddressMask})) {
    layout.bgmAtFixedAddress = true;
    layout.bgmHeaderAddress = static_cast<u32>((reader.u8At(*offset + 5) << 8) | reader.u8At(*offset + 8));
  }

  if (layout.hasSongList) {
    if (layout.bgmAtFixedAddress) {
      layout.version = CapcomSnesEngineVersion::v2BgmUsuallyAtFixedLocation;
      const bool bgmHeaderCoversSongList =
          layout.bgmHeaderAddress <= layout.songListAddress && layout.bgmHeaderAddress + 17 > layout.songListAddress;
      // Some v2 drivers contain both patterns, but the fixed-header operand can point into the song list.
      if (bgmHeaderCoversSongList || !isValidBgmHeader(reader, layout.bgmHeaderAddress)) {
        layout.bgmAtFixedAddress = false;
      }
    } else {
      layout.version = CapcomSnesEngineVersion::v1BgmInList;
    }
  } else if (layout.bgmAtFixedAddress) {
    layout.version = CapcomSnesEngineVersion::v3BgmFixedLocation;
  } else {
    return std::nullopt;
  }

  if (layout.bgmAtFixedAddress) {
    layout.sequenceHeaderAddress = layout.bgmHeaderAddress + 1;
    layout.priorityInHeader = false;
  } else if (layout.hasSongList) {
    // Song-list entries point at headers that include a one-byte priority before track pointers.
    const auto currentSong = guessCurrentSong(reader, layout.version, static_cast<u16>(layout.songListAddress));
    if (!currentSong) {
      return std::nullopt;
    }
    layout.sequenceHeaderAddress = reader.be16(layout.songListAddress + (*currentSong * 2));
    layout.priorityInHeader = true;
  }

  if (!isValidBgmHeader(reader,
                        layout.priorityInHeader ? layout.sequenceHeaderAddress : layout.sequenceHeaderAddress - 1)) {
    return std::nullopt;
  }

  if (const auto offset = findBytePattern(reader, MaskedBytePattern{kLoadInstrTablePattern, kLoadInstrTableMask})) {
    // The instrument table address is embedded as split operands in the loader routine.
    layout.instrumentTableAddress = static_cast<u32>(reader.u8At(*offset + 7) | (reader.u8At(*offset + 10) << 8));
  }

  const auto dspRegisters = initialDspRegisterMap(reader);
  if (const auto found = dspRegisters.find(0x5d); found != dspRegisters.end()) {
    layout.spcDirAddress = static_cast<u32>(found->second) << 8;
  }

  return layout;
}

}  // namespace vgmtrans::formats::capcom_snes
