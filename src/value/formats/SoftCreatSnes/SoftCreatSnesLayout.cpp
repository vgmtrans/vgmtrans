/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/SoftCreatSnes/SoftCreatSnes.h"

#include "value/scan/BytePattern.h"

#include <algorithm>
#include <array>
#include <numeric>
#include <optional>

namespace vgmtrans::formats::softcreat_snes {

using namespace core;

namespace {

// V2 and later select a song byte from each of sixteen low/high pointer
// columns. Some games overlap the columns, so the compare immediate is the
// song count; the distance between the first pair is only the column offset.
constexpr auto kLoadSequence = makeMaskedBytePattern(
    "\x7d\x68\x05\xb0\xfa\xfd\xcd\x00\xf6\x85\x13\xf0\x0a\xc4\x31\xf6\x80\x13\xc4\x30\x3f\x38\x06\x3d\x3d",
    "xx?x?xxxx??x?x?x??x?x??xx");

constexpr auto kDispatch = makeMaskedBytePattern(
    "\x3f\x85\x07\x10\x1d\x68\xba\xb0\x0b\x1c\xfd\xf6\x70\x0b\x2d\xf6\x6f\x0b\x2d\x6f",
    "x??x?x?xxxxx??xx??xx");

constexpr auto kCoarseTable =
    makeMaskedBytePattern("\xfb\x20\x60\x96\x90\x42\x5b\xc0\xb0\x04\x60\x95\x40\x02", "xxxx??xxxxxxxx");

constexpr auto kPitchAndFineTables = makeMaskedBytePattern(
    "\xf6\x87\x12\xc4\xd9\xf6\xdc\x12\xc4\xda\xfb\x20\xf6\xb4\x42\xfd\x6d\xe4\xd9\xcf\xcb\xdd\xee\xe4\xda\xcf\x8f\x00\xde\x7a\xdd\x7a\xd9",
    "x??xxx??xxxxx??xxxxxxxxxxxxxxxxxx");

constexpr auto kEnvelopeTable = makeMaskedBytePattern("\xe8\x3e\xc4\xd9\xe8\x1e\xc4\xda", "x?xxx?xx");
constexpr auto kTimerSetup = makeMaskedBytePattern("\x8f\x84\xfc\x8f\x04\xf1", "x?xxxx");
constexpr auto kTimerSetupWithTimer1 =
    makeMaskedBytePattern("\x8f\x85\xfc\x8f\x14\xfb\x8f\x06\xf1", "x?xx?xxxx");

// V1 uses 32-entry pointer columns and indexes its dispatch table directly by
// an even opcode ($80, $82, ... $C2).
constexpr auto kV1LoadSequence = makeMaskedBytePattern(
    "\x3f\x8a\x06\xf5\x20\x0e\xc4\x30\xf5\x40\x0e\xc4\x31\xf5\x60\x0e\xc4\x32",
    "x??x??xxx??xxx??xx");
constexpr auto kV1Dispatch = makeMaskedBytePattern(
    "\x3f\x05\x0a\x10\x0a\xfd\xf6\x91\x09\x2d\xf6\x90\x09\x2d\x6f", "x??xxxx??xx??xx");
constexpr auto kV1PitchAndFineTables = makeMaskedBytePattern(
    "\xf6\xed\x0c\xc4\xd6\xf6\x42\x0d\xc4\xd7\xfb\x20\xf6\xa4\x50\xfd\x6d\xe4\xd6\xcf\xcb\xd8\xee\xe4\xd7\xcf\x8f\x00\xd9\x7a\xd8\x7a\xd6",
    "x??xxx??xxxxx??xxxxxxxxxxxxxxxxxx");
constexpr auto kV1EnvelopeTable = makeMaskedBytePattern("\xfb\x61\xf6\x90\x10\xd4\xa0", "xxx??xx");
constexpr auto kV1TimerSetup = makeMaskedBytePattern("\x8f\x42\xfa\x8f\x81\xf1", "x?xxxx");

constexpr std::array<u8, 27> kDspRegisters{
    0x2c, 0x3c, 0x5c, 0x2d, 0x3d, 0x4d, 0x7d, 0x6d, 0x0d, 0x5d, 0x0f, 0x1f, 0x2f, 0x3f,
    0x4f, 0x5f, 0x6f, 0x7f, 0x05, 0x15, 0x25, 0x35, 0x45, 0x55, 0x65, 0x75, 0xff,
};

struct PointerColumn {
  u16 low = 0;
  u16 high = 0;
};

struct PointerTables {
  u8 songCount = 0;
  std::array<PointerColumn, kTrackCount> columns{};
  SourceRange range;
};

[[nodiscard]] bool validTrackPointer(ByteReader reader, u16 address) {
  return address >= 0x0200 && reader.has(address, 1);
}

[[nodiscard]] std::optional<PointerTables> pointerTables(
    ByteReader reader, u8 songs, std::array<PointerColumn, kTrackCount> columns) {
  if (songs == 0) {
    return std::nullopt;
  }
  u32 begin = columns.front().low;
  u32 end = begin;
  for (const auto [low, high] : columns) {
    if (!reader.has(low, songs) || !reader.has(high, songs)) {
      return std::nullopt;
    }
    begin = std::min<u32>({begin, low, high});
    end = std::max<u32>({end, static_cast<u32>(low) + songs, static_cast<u32>(high) + songs});
  }
  return PointerTables{.songCount = songs, .columns = columns, .range = reader.range(begin, end - begin)};
}

[[nodiscard]] std::optional<PointerTables> modernPointerTables(ByteReader reader, u32 load) {
  const u8 songs = reader.u8At(load + 2);
  std::array<PointerColumn, kTrackCount> columns{};
  for (u32 track = 0; track < kTrackCount; ++track) {
    const u32 highInstruction = load + 8 + track * 17u;
    const u32 lowInstruction = load + 15 + track * 17u;
    if (!reader.has(highInstruction, 3) || !reader.has(lowInstruction, 3) ||
        reader.u8At(highInstruction) != 0xf6 || reader.u8At(lowInstruction) != 0xf6) {
      return std::nullopt;
    }
    columns[track] = {.low = reader.le16(lowInstruction + 1), .high = reader.le16(highInstruction + 1)};
    if (columns[track].high < columns[track].low || columns[track].high - columns[track].low > songs) {
      return std::nullopt;
    }
  }
  return pointerTables(reader, songs, columns);
}

[[nodiscard]] std::optional<PointerTables> regularPointerTables(ByteReader reader, u8 songs, u16 firstLow,
                                                               u16 firstHigh, u16 stride) {
  std::array<PointerColumn, kTrackCount> columns{};
  for (u32 track = 0; track < kTrackCount; ++track) {
    columns[track] = {.low = static_cast<u16>(firstLow + track * stride),
                      .high = static_cast<u16>(firstHigh + track * stride)};
  }
  return pointerTables(reader, songs, columns);
}

[[nodiscard]] u16 trackAddress(ByteReader reader, const PointerTables& tables, u8 song, u32 track) {
  const auto [low, high] = tables.columns[track];
  return static_cast<u16>(reader.u8At(low + song) | (reader.u8At(high + song) << 8));
}

struct SongScore {
  u8 song = 0;
  unsigned meaningfulTracks = 0;
  unsigned activeTracks = 0;
  u32 cursorDistance = 0;
};

[[nodiscard]] SongScore scoreSong(ByteReader reader, const PointerTables& tables, u8 song) {
  SongScore score{.song = song};
  std::array<u16, kTrackCount> distances{};
  for (u32 track = 0; track < kTrackCount; ++track) {
    const u16 start = trackAddress(reader, tables, song, track);
    if (!validTrackPointer(reader, start)) {
      continue;
    }
    ++score.activeTracks;
    score.meaningfulTracks += reader.u8At(start) != 0x80;
    const u16 current = reader.le16(0x30 + track * 2u);
    distances[score.activeTracks - 1] = current > start ? current - start : start - current;
  }
  if (score.activeTracks != 0) {
    std::sort(distances.begin(), distances.begin() + score.activeTracks);
    // Two hardware voices may hold unrelated SFX cursors.
    const unsigned kept = score.activeTracks > 2 ? score.activeTracks - 2 : score.activeTracks;
    score.cursorDistance = std::accumulate(distances.begin(), distances.begin() + kept, 0u);
  }
  return score;
}

[[nodiscard]] bool betterSong(const SongScore& candidate, const SongScore& current) {
  const bool meaningful = candidate.meaningfulTracks != 0;
  if (meaningful != (current.meaningfulTracks != 0)) {
    return meaningful;
  }
  if (candidate.cursorDistance != current.cursorDistance) {
    return candidate.cursorDistance < current.cursorDistance;
  }
  return candidate.meaningfulTracks != current.meaningfulTracks
             ? candidate.meaningfulTracks > current.meaningfulTracks
             : candidate.activeTracks > current.activeTracks;
}

[[nodiscard]] std::optional<u8> bestSong(ByteReader reader, const PointerTables& tables,
                                         std::optional<u8> live = std::nullopt) {
  if (live && *live < tables.songCount) {
    const SongScore score = scoreSong(reader, tables, *live);
    if (score.meaningfulTracks != 0) {
      return *live;
    }
  }

  std::optional<SongScore> best;
  for (u8 song = 0; song < tables.songCount; ++song) {
    const SongScore candidate = scoreSong(reader, tables, song);
    if (candidate.activeTracks != 0 && (!best || betterSong(candidate, *best))) {
      best = candidate;
    }
  }
  return best ? std::optional<u8>{best->song} : std::nullopt;
}

[[nodiscard]] std::optional<std::array<TrackPointer, kTrackCount>> readTracks(ByteReader reader,
                                                                              const PointerTables& tables,
                                                                              u8 song) {
  std::array<TrackPointer, kTrackCount> tracks{};
  unsigned active = 0;
  for (u32 track = 0; track < kTrackCount; ++track) {
    const auto [low, high] = tables.columns[track];
    const u16 address = trackAddress(reader, tables, song, track);
    tracks[track] = TrackPointer{
        .address = address,
        .lowSource = reader.range(low + song, 1),
        .highSource = reader.range(high + song, 1),
    };
    if (validTrackPointer(reader, address)) {
      ++active;
    } else if (address != 0 && address != 0xffff) {
      return std::nullopt;
    }
  }
  return active == 0 ? std::nullopt : std::optional{tracks};
}

[[nodiscard]] std::optional<u16> aliasTable(ByteReader reader, u16 dispatch, u8 opcode) {
  const u32 entry = dispatch + (opcode - 0x80u) * 2u;
  if (!reader.has(entry, 2)) {
    return std::nullopt;
  }
  const u16 handler = reader.le16(entry);
  // call read-byte; mov y,a; mov a,table+y
  if (!reader.has(handler, 8) || reader.u8At(handler) != 0x3f || reader.u8At(handler + 3) != 0xfd ||
      reader.u8At(handler + 4) != 0xf6) {
    return std::nullopt;
  }
  const u16 table = reader.le16(handler + 5);
  return reader.has(table, 16) ? std::optional<u16>{table} : std::nullopt;
}

[[nodiscard]] std::optional<Version> versionForDriver(ByteReader reader, u8 cutoff, u16 dispatch) {
  switch (cutoff) {
    case 0xb8:
      return Version::V2;
    case 0xb9:
      return Version::V5;
    case 0xba:
      if (aliasTable(reader, dispatch, 0xb8)) {
        return Version::V2b;
      }
      return aliasTable(reader, dispatch, 0xb9) ? std::optional{Version::V5} : std::nullopt;
    case 0xbd:
      return Version::V6c;
    case 0xc7:
      return Version::V6;
    case 0xc3: {
      const u16 volumeDecay = reader.le16(dispatch + (0xb0u - 0x80u) * 2u);
      for (u32 opcode = 0xaa; opcode < 0xb0; ++opcode) {
        if (reader.le16(dispatch + (opcode - 0x80u) * 2u) != volumeDecay) {
          return Version::V7;
        }
      }
      return Version::V6d;
    }
    default:
      return std::nullopt;
  }
}

[[nodiscard]] u16 musicVolume(ByteReader reader, u32 load) {
  if (reader.has(load + 23, 1) && reader.u8At(load + 20) == 0x3f) {
    const u16 initializeTrack = reader.le16(load + 21);
    for (u32 offset = initializeTrack; offset < initializeTrack + 32 && reader.has(offset, 5); ++offset) {
      if (reader.u8At(offset) == 0xe4 && reader.u8At(offset + 2) == 0xd5 &&
          reader.u8At(offset + 3) == 0x66 && reader.u8At(offset + 4) == 0x03) {
        return reader.u8At(reader.u8At(offset + 1));
      }
    }
  }
  return reader.u8At(0xe8);
}

[[nodiscard]] bool validInstrumentTables(ByteReader reader, u16 pitchLow, u16 pitchHigh, u16 coarse, u16 fine,
                                         u16 envelopes) {
  return pitchHigh > pitchLow && reader.has(pitchLow, pitchHigh - pitchLow) &&
         reader.has(pitchHigh, pitchHigh - pitchLow) && reader.has(coarse, 1) && reader.has(fine, 1) &&
         reader.has(envelopes, 7);
}

[[nodiscard]] std::optional<Layout> findModernLayout(ByteReader reader) {
  const auto load = findBytePattern(reader, kLoadSequence);
  const auto dispatchCode = findBytePattern(reader, kDispatch);
  const auto coarseCode = findBytePattern(reader, kCoarseTable);
  const auto pitchCode = findBytePattern(reader, kPitchAndFineTables);
  const auto envelopeCode = findBytePattern(reader, kEnvelopeTable);
  const auto dspRegisters = findBytes(reader, kDspRegisters);
  if (!load || !dispatchCode || !coarseCode || !pitchCode || !envelopeCode || !dspRegisters) {
    return std::nullopt;
  }

  const u8 cutoff = reader.u8At(*dispatchCode + 6);
  const u16 dispatch = reader.le16(*dispatchCode + 16);
  if (cutoff <= 0x80 || !reader.has(dispatch, (cutoff - 0x80u) * 2u)) {
    return std::nullopt;
  }
  const auto version = versionForDriver(reader, cutoff, dispatch);
  if (!version) {
    return std::nullopt;
  }

  const auto pointers = modernPointerTables(reader, *load);
  const auto song = pointers ? bestSong(reader, *pointers, reader.u8At(0xe4)) : std::nullopt;
  const auto tracks = song ? readTracks(reader, *pointers, *song) : std::nullopt;
  if (!pointers || !song || !tracks) {
    return std::nullopt;
  }

  const u16 pitchLow = reader.le16(*pitchCode + 1);
  const u16 pitchHigh = reader.le16(*pitchCode + 6);
  const u16 coarse = reader.le16(*coarseCode + 4);
  const u16 fine = reader.le16(*pitchCode + 13);
  const u16 envelopes = static_cast<u16>(reader.u8At(*envelopeCode + 1) | (reader.u8At(*envelopeCode + 5) << 8));
  // V7 prepends FLG and master-volume registers to this otherwise shared
  // register list. Its parallel values therefore begin three entries later.
  const bool prefixedDspTable = *dspRegisters >= 3 && matchesBytes(reader, *dspRegisters - 3, "\x6c\x0c\x1c");
  const u32 dspValues = *dspRegisters + kDspRegisters.size() + (prefixedDspTable ? 3u : 0u);
  if (!validInstrumentTables(reader, pitchLow, pitchHigh, coarse, fine, envelopes) ||
      !reader.has(dspValues, 26)) {
    return std::nullopt;
  }

  EchoState echo{
      .left = reader.s8At(dspValues),
      .right = reader.s8At(dspValues + 1),
      .feedback = reader.s8At(dspValues + 8),
      .voiceMask = reader.u8At(dspValues + 5),
      .delay = static_cast<u8>(reader.u8At(dspValues + 6) & 0x0f),
  };
  for (u32 coefficient = 0; coefficient < echo.fir.size(); ++coefficient) {
    echo.fir[coefficient] = reader.s8At(dspValues + 10 + coefficient);
  }
  const u16 directory = static_cast<u16>(reader.u8At(dspValues + 9) << 8);
  if (!reader.has(directory, 4)) {
    return std::nullopt;
  }

  u8 timer = 0x85;
  if (const auto setup = findBytePattern(reader, kTimerSetup)) {
    timer = reader.u8At(*setup + 1);
  } else if (const auto extendedSetup = findBytePattern(reader, kTimerSetupWithTimer1)) {
    timer = reader.u8At(*extendedSetup + 1);
  }
  const auto aliasOpcode = dialect(*version).noteAliasOpcode;
  return Layout{
      .version = *version,
      .songIndex = *song,
      .initialTimer = timer,
      .musicVolume = musicVolume(reader, *load),
      .pitchLowTableAddress = pitchLow,
      .pitchHighTableAddress = pitchHigh,
      .coarseTableAddress = coarse,
      .fineTableAddress = fine,
      .envelopeTableAddress = envelopes,
      .spcDirAddress = directory,
      .noteAliasTableAddress = aliasOpcode && *aliasOpcode < cutoff
                                   ? aliasTable(reader, dispatch, *aliasOpcode)
                                   : std::nullopt,
      .sequenceHeaderRange = pointers->range,
      .tracks = *tracks,
      .echo = echo,
  };
}

[[nodiscard]] std::optional<Layout> findV1Layout(ByteReader reader) {
  const auto load = findBytePattern(reader, kV1LoadSequence);
  const auto dispatchCode = findBytePattern(reader, kV1Dispatch);
  const auto coarseCode = findBytePattern(reader, kCoarseTable);
  const auto pitchCode = findBytePattern(reader, kV1PitchAndFineTables);
  const auto envelopeCode = findBytePattern(reader, kV1EnvelopeTable);
  const auto timerCode = findBytePattern(reader, kV1TimerSetup);
  if (!load || !dispatchCode || !coarseCode || !pitchCode || !envelopeCode || !timerCode) {
    return std::nullopt;
  }

  constexpr u8 songs = 0x20;
  const u16 firstLow = reader.le16(*load + 4);
  const u16 firstHigh = reader.le16(*load + 9);
  if (firstHigh < firstLow || firstHigh - firstLow != songs ||
      reader.le16(*load + 14) - firstLow != songs * 2u) {
    return std::nullopt;
  }
  const auto pointers = regularPointerTables(reader, songs, firstLow, firstHigh, songs * 2u);
  const auto song = pointers ? bestSong(reader, *pointers) : std::nullopt;
  const auto tracks = song ? readTracks(reader, *pointers, *song) : std::nullopt;
  if (!pointers || !song || !tracks) {
    return std::nullopt;
  }

  const u16 dispatch = reader.le16(*dispatchCode + 11);
  const u16 pitchLow = reader.le16(*pitchCode + 1);
  const u16 pitchHigh = reader.le16(*pitchCode + 6);
  const u16 coarse = reader.le16(*coarseCode + 4);
  const u16 fine = reader.le16(*pitchCode + 13);
  const u16 envelopes = reader.le16(*envelopeCode + 3);
  const u16 directory = static_cast<u16>(coarse & 0xff00u);
  if (!reader.has(dispatch + 0xc2, 2) ||
      !validInstrumentTables(reader, pitchLow, pitchHigh, coarse, fine, envelopes) || !reader.has(directory, 4)) {
    return std::nullopt;
  }

  return Layout{
      .version = Version::V1,
      .songIndex = *song,
      .initialTimer = static_cast<u8>(reader.u8At(*timerCode + 1) * 2u),
      .musicVolume = 0x100,
      .pitchLowTableAddress = pitchLow,
      .pitchHighTableAddress = pitchHigh,
      .coarseTableAddress = coarse,
      .fineTableAddress = fine,
      .envelopeTableAddress = envelopes,
      .spcDirAddress = directory,
      .sequenceHeaderRange = pointers->range,
      .tracks = *tracks,
  };
}

}  // namespace

std::optional<Layout> findLayout(ByteReader reader) {
  if (reader.size() != kAramSize) {
    return std::nullopt;
  }
  if (auto layout = findModernLayout(reader)) {
    return layout;
  }
  return findV1Layout(reader);
}

}  // namespace vgmtrans::formats::softcreat_snes
