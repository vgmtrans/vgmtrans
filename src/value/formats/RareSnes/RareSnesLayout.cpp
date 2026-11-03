/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/RareSnes/RareSnes.h"

#include "value/scan/BytePattern.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string_view>

namespace vgmtrans::formats::rare_snes {

using namespace core;
using namespace std::string_view_literals;

namespace {

constexpr MaskedBytePattern kSetDir{
    "\x8f\x5d\xf2\x8f\x00\xf3"sv,
    "xxxx?x"sv,
};

constexpr MaskedBytePattern kReadSrcnTable{
    "\x4d\xf7\x00\x5d\xf5\x00\x00\xce"sv,
    "xx?xx??x"sv,
};

constexpr MaskedBytePattern kLaterDispatch{
    "\x8d\x00\xf7\x00\x30\x06\x4d\x1c\x5d\x1f\x00\x00"sv,
    "xxx?xxxxxx??"sv,
};

constexpr MaskedBytePattern kEarlierDispatch{
    "\x8d\x00\xf7\x00\x68\x00\x30\x06\x4d\x1c\x5d\x1f\x00\x00"sv,
    "xxx?xxxxxxxx??"sv,
};

constexpr MaskedBytePattern kIndirectSongHeader{
    "\xe8\x01\xd4\x00\xd5\x10\x01\xf7\x00\xd4\x00\xfc\xf7\x00\xd4\x00"sv,
    "xxx?xxxx?x?xx?x?"sv,
};

constexpr MaskedBytePattern kDirectSongHeader{
    "\xe8\x01\xd4\x00\xd5\x10\x01\xf6\x00\x00\xd4\x00\xf6\x00\x00\xd4\x00"sv,
    "xxx?xxxx??x?x??x?"sv,
};

// The six-channel Battlemaniacs branch has a different command dispatch and
// keeps its song list at $0f00/$0fa0.
constexpr MaskedBytePattern kBattlemaniacsTempoLoad{
    "\xf6\x00\x0f\xc4\x01"sv,
    "xxxxx"sv,
};

constexpr MaskedBytePattern kBattlemaniacsTrackLoad{
    "\xf6\xa0\x0f\xd4\x8b\xf6\xa1\x0f\xd4\x91\xfc"sv,
    "xxxxxxxxxxx"sv,
};

[[nodiscard]] bool validAddress(ByteReader reader, u32 address, u32 size = 1) {
  return address < kAramSize && reader.has(address, size);
}

[[nodiscard]] std::optional<u16> findDir(ByteReader reader) {
  const auto offset = findBytePattern(reader, kSetDir);
  return offset ? std::optional<u16>{static_cast<u16>(reader.u8At(*offset + 4) << 8)} : std::nullopt;
}

[[nodiscard]] std::optional<u32> findInstrumentTable(ByteReader reader) {
  const auto offset = findBytePattern(reader, kReadSrcnTable);
  if (!offset) {
    return std::nullopt;
  }
  const u32 address = reader.le16(*offset + 5);
  return reader.has(address, 1) ? std::optional<u32>{address} : std::nullopt;
}

[[nodiscard]] bool validHeader(ByteReader reader, u32 address, u32 tracks) {
  if (!reader.has(address, tracks * 2 + 1)) {
    return false;
  }
  u32 nonzero = 0;
  for (u32 track = 0; track < tracks; ++track) {
    const u16 pointer = reader.le16(address + track * 2);
    if (pointer != 0 && validAddress(reader, pointer)) {
      ++nonzero;
    } else if (pointer != 0) {
      return false;
    }
  }
  return nonzero != 0;
}

[[nodiscard]] Profile classifyLater(ByteReader reader, u32 dispatchTable) {
  const auto handler = [&](u8 opcode) -> u16 {
    const u32 address = dispatchTable + static_cast<u32>(opcode) * 2;
    return reader.has(address, 2) ? reader.le16(address) : 0;
  };

  if (handler(0x0c) == 0) {
    return Profile::KillerInstinct;
  }
  // Winning Run removes the three noise commands at 19-1b. DKC2 also assigns
  // 20 and 22, so those tail entries alone do not distinguish the builds.
  if (handler(0x19) == 0 && handler(0x1a) == 0 && handler(0x1b) == 0 && handler(0x20) != 0 && handler(0x22) != 0) {
    return Profile::WinningRun;
  }
  // The beta has DKC's conditional tail and the later six-operand LFO, but no
  // DKC/DKC2 volume presets.
  if (handler(0x24) != 0 && handler(0x25) != 0 && handler(0x2d) != 0) {
    return Profile::KillerInstinctBeta;
  }
  return Profile::DonkeyKongCountry2;
}

[[nodiscard]] std::optional<Layout> battlemaniacsLayout(ByteReader reader) {
  if (!findBytePattern(reader, kBattlemaniacsTempoLoad) || !findBytePattern(reader, kBattlemaniacsTrackLoad)) {
    return std::nullopt;
  }

  constexpr u32 kTempoTable = 0x0f00;
  constexpr u32 kPointerTable = 0x0fa0;
  constexpr u32 kTracks = 6;
  constexpr u32 kRuntimeLow = 0x8b;
  constexpr u32 kRuntimeHigh = 0x91;
  if (!reader.has(kPointerTable, 12) || !reader.has(kRuntimeLow, kTracks) || !reader.has(kRuntimeHigh, kTracks)) {
    return std::nullopt;
  }

  std::array<u16, kTracks> current{};
  for (u32 track = 0; track < kTracks; ++track) {
    current[track] = static_cast<u16>(reader.u8At(kRuntimeLow + track) | (reader.u8At(kRuntimeHigh + track) << 8));
  }

  std::optional<u8> bestSong;
  u64 bestScore = std::numeric_limits<u64>::max();
  std::array<u16, kTracks> bestStarts{};
  // Six pointers consume twelve bytes, so no more than 128 rows fit before
  // the fixed high-memory upload area. Invalid rows are simply ignored.
  for (u32 song = 0; song < 128; ++song) {
    const u32 row = kPointerTable + song * 12;
    if (!reader.has(row, 12) || !reader.has(kTempoTable + song, 1)) {
      break;
    }
    std::array<u16, kTracks> starts{};
    u64 score = 0;
    bool valid = true;
    for (u32 track = 0; track < kTracks; ++track) {
      starts[track] = reader.le16(row + track * 2);
      if (starts[track] < 0x0300 || starts[track] >= 0xff98 || !reader.has(starts[track], 1)) {
        valid = false;
        break;
      }
      score += static_cast<u64>(std::abs(static_cast<s32>(current[track]) - static_cast<s32>(starts[track])));
    }
    // Tempo is an independent and strong snapshot fingerprint.
    if (valid && reader.has(0x01, 1) && reader.u8At(kTempoTable + song) != reader.u8At(0x01)) {
      score += 0x10000;
    }
    if (valid && score < bestScore) {
      bestScore = score;
      bestSong = static_cast<u8>(song);
      bestStarts = starts;
    }
  }
  if (!bestSong) {
    return std::nullopt;
  }

  const u32 row = kPointerTable + static_cast<u32>(*bestSong) * 12;
  return Layout{
      .profile = Profile::Battlemaniacs,
      .sequenceHeaderAddress = row,
      .sequenceHeaderRange = reader.range(row, 12),
      .initialTempoRange = reader.range(kTempoTable + *bestSong, 1),
      .trackStarts = {bestStarts.begin(), bestStarts.end()},
      .initialTempo = reader.u8At(kTempoTable + *bestSong),
      .initialTimer = 0x85,
      .monoOutput = reader.u8At(0x03) != 0,
      .spcDirAddress = findDir(reader),
      .battlemaniacsSong = bestSong,
  };
}

}  // namespace

const char* profileName(Profile profile) {
  switch (profile) {
    case Profile::Battlemaniacs:
      return "Battletoads in Battlemaniacs";
    case Profile::BattletoadsDoubleDragon:
      return "Battletoads & Double Dragon";
    case Profile::DonkeyKongCountry:
      return "Donkey Kong Country";
    case Profile::KillerInstinctBeta:
      return "Killer Instinct Beta";
    case Profile::WinningRun:
      return "Ken Griffey Jr.'s Winning Run";
    case Profile::KillerInstinct:
      return "Killer Instinct";
    case Profile::DonkeyKongCountry2:
      return "Donkey Kong Country 2/3";
    case Profile::Unknown:
      return "Unknown";
  }
  return "Unknown";
}

std::optional<Layout> findLayout(ByteReader reader) {
  if (reader.size() != kAramSize) {
    return std::nullopt;
  }
  if (const auto battlemaniacs = battlemaniacsLayout(reader)) {
    return battlemaniacs;
  }

  Profile selected = Profile::Unknown;
  if (const auto dispatch = findBytePattern(reader, kLaterDispatch)) {
    selected = classifyLater(reader, reader.le16(*dispatch + 10));
  } else if (findBytePattern(reader, kEarlierDispatch)) {
    selected = findInstrumentTable(reader) ? Profile::DonkeyKongCountry : Profile::BattletoadsDoubleDragon;
  } else {
    return std::nullopt;
  }

  u32 header = 0;
  if (selected == Profile::BattletoadsDoubleDragon) {
    header = 0x0f00;
  } else if (const auto direct = findBytePattern(reader, kDirectSongHeader)) {
    header = reader.le16(*direct + 8);
  } else if (const auto indirect = findBytePattern(reader, kIndirectSongHeader)) {
    const u8 pointer = reader.u8At(*indirect + 8);
    header = reader.le16(pointer);
  }
  if (!validHeader(reader, header, 8)) {
    return std::nullopt;
  }

  std::vector<u16> starts;
  starts.reserve(8);
  for (u32 track = 0; track < 8; ++track) {
    starts.push_back(reader.le16(header + track * 2));
  }
  const u8 timer = selected == Profile::DonkeyKongCountry || selected == Profile::BattletoadsDoubleDragon ||
                           selected == Profile::KillerInstinctBeta
                       ? 0x3c
                       : 0x64;
  const bool mono = [&] {
    switch (selected) {
      case Profile::BattletoadsDoubleDragon:
        return true;
      case Profile::DonkeyKongCountry:
        return reader.u8At(0x25) != 0;
      case Profile::KillerInstinctBeta:
        return reader.u8At(0x20) != 0;
      case Profile::WinningRun:
        return reader.u8At(0x1f) != 0;
      case Profile::DonkeyKongCountry2:
        return reader.u8At(0x1d) != 0;
      case Profile::Battlemaniacs:
      case Profile::KillerInstinct:
      case Profile::Unknown:
        return false;
    }
    return false;
  }();
  return Layout{
      .profile = selected,
      .sequenceHeaderAddress = header,
      .sequenceHeaderRange = reader.range(header, 18),
      .initialTempoRange = reader.range(header + 16, 1),
      .trackStarts = std::move(starts),
      .initialTempo = reader.u8At(header + 16),
      .initialTimer = timer,
      .monoOutput = mono,
      .instrumentTableAddress = findInstrumentTable(reader),
      .spcDirAddress = findDir(reader),
  };
}

}  // namespace vgmtrans::formats::rare_snes
