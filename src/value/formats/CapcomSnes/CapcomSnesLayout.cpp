/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/CapcomSnes/CapcomSnes.h"
#include "value/scan/BytePattern.h"

#include <algorithm>
#include <array>
#include <limits>
#include <optional>
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

[[nodiscard]] u16 currentTrackCursor(ByteReader reader, CapcomSnesEngineVersion version, u8 channel) {
  // V1 and later drivers keep the cursor bytes in different zero-page layouts.
  if (version == CapcomSnesEngineVersion::v1BgmInList) {
    return static_cast<u16>(reader.u8At(0x00 + channel * 2 + 1) | (reader.u8At(0x10 + channel * 2 + 1) << 8));
  }
  return static_cast<u16>(reader.u8At(0x00 + channel) | (reader.u8At(0x08 + channel) << 8));
}

[[nodiscard]] std::optional<u8> guessCurrentSong(ByteReader reader, CapcomSnesEngineVersion version,
                                                 u32 songListAddress) {
  // Song-list SPC dumps only expose the current playback cursor, so choose the nearest valid header.
  std::optional<u8> guessedSongIndex;
  int bestScore = std::numeric_limits<int>::max();

  for (u32 songIndex = 0; songIndex <= 0x7f; ++songIndex) {
    const u32 pointerAddress = songListAddress + songIndex * 2;
    if (!reader.has(pointerAddress, 2)) {
      break;
    }

    const u16 songHeaderAddress = reader.be16(pointerAddress);
    if (songHeaderAddress == 0) {
      continue;
    }
    // The first nonzero invalid pointer marks the end of the usable list.
    if (!isValidBgmHeader(reader, songHeaderAddress)) {
      break;
    }

    int score = 0;
    int validTrackCount = 0;
    for (u32 track = 0; track < kCapcomSnesMaxTracks; ++track) {
      const u16 trackStart = reader.be16(songHeaderAddress + 1 + track * 2);
      const u16 currentAddress = currentTrackCursor(reader, version, static_cast<u8>(7 - track));
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

[[nodiscard]] std::optional<u8> initialDspRegisterValue(ByteReader reader, u8 targetRegister) {
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
    return std::nullopt;
  }

  if (!reader.has(registerListAddress, registerCount) || !reader.has(valueListAddress, registerCount)) {
    return std::nullopt;
  }

  std::optional<u8> value;
  for (u32 i = 0; i < registerCount; ++i) {
    if (reader.u8At(registerListAddress + i) == targetRegister) {
      // Preserve the map-based implementation's behavior for duplicate registers.
      value = reader.u8At(valueListAddress + i);
    }
  }
  return value;
}

}  // namespace

std::optional<CapcomSnesLayout> findCapcomSnesLayout(ByteReader reader) {
  // CapcomSnes SPC dumps have no declarative header. Layout discovery reconstructs the
  // current driver's table addresses by recognizing small snippets of SPC700 code.
  if (reader.size() != kCapcomSnesAramSize) {
    return std::nullopt;
  }

  std::optional<u32> songListAddress;
  if (const auto offset = findBytePattern(reader, MaskedBytePattern{kReadSongListPattern, kReadSongListMask})) {
    songListAddress = std::min(reader.le16(*offset + 3), reader.le16(*offset + 8));
  }

  std::optional<u32> fixedBgmHeaderAddress;
  if (const auto offset = findBytePattern(reader, MaskedBytePattern{kReadBgmAddressPattern, kReadBgmAddressMask})) {
    fixedBgmHeaderAddress = static_cast<u32>((reader.u8At(*offset + 5) << 8) | reader.u8At(*offset + 8));
  }

  CapcomSnesEngineVersion version = CapcomSnesEngineVersion::none;
  if (songListAddress) {
    if (fixedBgmHeaderAddress) {
      version = CapcomSnesEngineVersion::v2BgmUsuallyAtFixedLocation;
      const bool bgmHeaderCoversSongList =
          *fixedBgmHeaderAddress <= *songListAddress && *fixedBgmHeaderAddress + 17 > *songListAddress;
      // Some v2 drivers contain both patterns, but the fixed-header operand can point into the song list.
      if (bgmHeaderCoversSongList || !isValidBgmHeader(reader, *fixedBgmHeaderAddress)) {
        fixedBgmHeaderAddress.reset();
      }
    } else {
      version = CapcomSnesEngineVersion::v1BgmInList;
    }
  } else if (fixedBgmHeaderAddress) {
    version = CapcomSnesEngineVersion::v3BgmFixedLocation;
  } else {
    return std::nullopt;
  }

  u32 bgmHeaderAddress = 0;
  bool priorityInHeader = false;
  if (fixedBgmHeaderAddress) {
    bgmHeaderAddress = *fixedBgmHeaderAddress;
  } else {
    // Song-list entries point at headers that include a one-byte priority before track pointers.
    const auto currentSong = guessCurrentSong(reader, version, *songListAddress);
    if (!currentSong) {
      return std::nullopt;
    }
    bgmHeaderAddress = reader.be16(*songListAddress + (*currentSong * 2));
    priorityInHeader = true;
  }

  if (!isValidBgmHeader(reader, bgmHeaderAddress)) {
    return std::nullopt;
  }

  const u32 trackPointerTableAddress = bgmHeaderAddress + 1;
  const u32 sequenceHeaderAddress = priorityInHeader ? bgmHeaderAddress : trackPointerTableAddress;
  const u32 sequenceHeaderSize = (priorityInHeader ? 1 : 0) + kCapcomSnesMaxTracks * 2;
  CapcomSnesLayout layout{
      .version = version,
      .sequenceHeaderRange = reader.range(sequenceHeaderAddress, sequenceHeaderSize),
      .trackPointerTableAddress = trackPointerTableAddress,
  };

  if (const auto offset = findBytePattern(reader, MaskedBytePattern{kLoadInstrTablePattern, kLoadInstrTableMask})) {
    // The instrument table address is embedded as split operands in the loader routine.
    layout.instrumentTableAddress = static_cast<u32>(reader.u8At(*offset + 7) | (reader.u8At(*offset + 10) << 8));
  }

  // DIR is normally supplied by the driver's DSP initialization table.
  if (const auto dirPage = initialDspRegisterValue(reader, 0x5d)) {
    layout.spcDirAddress = static_cast<u32>(*dirPage) << 8;
  }

  return layout;
}

}  // namespace vgmtrans::formats::capcom_snes
