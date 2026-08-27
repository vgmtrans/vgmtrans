/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/NeverlandSnes/NeverlandSnes.h"

#include "value/scan/BytePattern.h"

#include <algorithm>
#include <optional>
#include <string_view>

namespace vgmtrans::formats::neverland_snes {

using namespace core;

namespace {

// Both loaders point $18/19 (modern) or $08/09 (original) at byte $10 of
// the resident song header. The page byte is deliberately left relocatable.
constexpr auto kModernLoader = makeMaskedBytePattern(
    "\x8f\x10\x18\x8f\x00\x19\xcd\x00\x8d\x00\xf4\x3d\x28\x02\xd4\x3d\xf7\x18\x68\xff\xf0\x08",
    "xxxx?xxxxxxxxxxxxxxxxx");
constexpr auto kOriginalLoader = makeMaskedBytePattern(
    "\x8f\x10\x08\x8f\x00\x09\xcd\x04\xe8\x00\xd5\x00\x00\x3d\xc8\x08\xd0\xf8\x8d\x00\xf7\x08",
    "xxxx?xxxxxx??xxxxxxxxx");

// SRCN * 4 immediately precedes four absolute indexed reads. Later drivers
// write either the DSP registers or mirrors, so only the invariant prefix and
// first table read are part of the signature.
constexpr auto kModernInstrument = makeMaskedBytePattern("\x1c\x1c\xfd\xf6\x00\x00", "xxxx??");
constexpr auto kOriginalInstrument =
    makeMaskedBytePattern("\x1c\x1c\x60\x88\x00\xc4\x18\xe8\x00\x88\x00\xc4\x19", "xxxx?xxxxx?xx");

// The DSP reset table is the most reliable source for DIR: the following
// registers and values make the short signature specific to the reset list.
constexpr auto kDirectory =
    makeMaskedBytePattern("\x5d\x00\x4d\x00\x0d\x00\x3c\x00\x2c\x00", "x?xxxxxxxx");

// Energy Breaker assigns FF 05-07 to a signed fixed-clock pitch accumulator.
constexpr auto kPitchDrift = makeMaskedBytePattern(
    "\xce\xf7\x18\x2f\x12\xce\xf7\x18\x08\x80\x2f\x0b\xce\xe8\x00", "xxxxxxxxxxxxxxx");

// Modern percussion lookup: key, pitch low/high, pan, and SRCN are five
// independently relocated arrays named by this instruction sequence.
constexpr auto kPercussion = makeMaskedBytePattern(
    "\x8d\x00\xf6\x00\x00\xf0\x00\x75\x00\x00\xf0\x03\xfc\x2f\xf3"
    "\xf6\x00\x00\xd4\x4d\xf6\x00\x00\xd4\x5d\xf6\x00\x00\xd5\x00\x00\xf6\x00\x00\x3f",
    "xxx??x?x??xxxxxx??xxx??xxx??x??x??x");

[[nodiscard]] bool signature(ByteReader reader, u16 base, std::string_view text) {
  return reader.has(base, text.size()) &&
         std::equal(text.begin(), text.end(), reader.slice(base, text.size()).begin());
}

[[nodiscard]] std::optional<u16> instrumentTable(ByteReader reader, Version version) {
  if (version == Version::Modern) {
    const auto code = findBytePattern(reader, kModernInstrument);
    if (!code) {
      return std::nullopt;
    }
    const u16 base = reader.le16(*code + 4);
    u32 found = 1;
    for (u32 offset = *code + 6; offset + 2 < std::min<u32>(*code + 48, reader.size()); ++offset) {
      if (reader.u8At(offset) == 0xf6 && reader.le16(offset + 1) == static_cast<u16>(base + found)) {
        ++found;
      }
    }
    return found == 4 && reader.has(base, 4) ? std::optional<u16>{base} : std::nullopt;
  }

  const auto code = findBytePattern(reader, kOriginalInstrument);
  if (!code) {
    return std::nullopt;
  }
  const u16 base = static_cast<u16>((reader.u8At(*code + 10) << 8) | reader.u8At(*code + 4));
  return reader.has(base, 4) ? std::optional<u16>{base} : std::nullopt;
}

[[nodiscard]] std::vector<PercussionPatch> percussionTable(ByteReader reader, Version version) {
  std::vector<PercussionPatch> result;
  if (version == Version::Modern) {
    const auto code = findBytePattern(reader, kPercussion);
    if (!code) {
      return result;
    }
    const u16 keys = reader.le16(*code + 3);
    const u16 pitchLow = reader.le16(*code + 16);
    const u16 pitchHigh = reader.le16(*code + 21);
    const u16 pan = reader.le16(*code + 26);
    const u16 program = reader.le16(*code + 32);
    for (u32 index = 0; index < 128 && reader.has(keys + index, 1); ++index) {
      const u8 key = reader.u8At(keys + index);
      if (key == 0 || !reader.has(pitchLow + index, 1) || !reader.has(pitchHigh + index, 1) ||
          !reader.has(pan + index, 1) || !reader.has(program + index, 1)) {
        break;
      }
      result.push_back(PercussionPatch{
          .key = key,
          .pitch = static_cast<u16>(reader.u8At(pitchLow + index) | (reader.u8At(pitchHigh + index) << 8)),
          .pan = reader.u8At(pan + index),
          .program = reader.u8At(program + index),
      });
    }
    return result;
  }

  // The original driver searches eleven entries backwards. These arrays are
  // adjacent in every original driver and are named by the search itself.
  constexpr auto original = makeMaskedBytePattern(
      "\x8d\x00\xf5\x00\x00\x76\x00\x00\xf0\x04\xdc\x10\xf8\x6f\xf6\x00\x00\x3f",
      "x?x??x??xxxxxxx??x");
  const auto code = findBytePattern(reader, original);
  if (!code) {
    return result;
  }
  const u8 last = reader.u8At(*code + 1);
  const u16 keys = reader.le16(*code + 6);
  const u16 program = reader.le16(*code + 15);
  // The remaining absolute reads occur in program, pan, pitch-low, pitch-high order.
  std::vector<u16> arrays{program};
  for (u32 offset = *code + 18; offset + 2 < std::min<u32>(*code + 48, reader.size()); ++offset) {
    if (reader.u8At(offset) == 0xf6) {
      arrays.push_back(reader.le16(offset + 1));
      offset += 2;
    }
  }
  if (arrays.size() < 4) {
    return {};
  }
  for (u32 index = 0; index <= last; ++index) {
    if (!reader.has(keys + index, 1) || !reader.has(arrays[0] + index, 1) || !reader.has(arrays[1] + index, 1) ||
        !reader.has(arrays[2] + index, 1) || !reader.has(arrays[3] + index, 1)) {
      break;
    }
    const u8 key = reader.u8At(keys + index);
    if (key == 0) {
      continue;
    }
    result.push_back(PercussionPatch{
        .key = key,
        .pitch = static_cast<u16>(reader.u8At(arrays[2] + index) | (reader.u8At(arrays[3] + index) << 8)),
        .pan = reader.u8At(arrays[1] + index),
        .program = reader.u8At(arrays[0] + index),
    });
  }
  return result;
}

struct ModernDefaults {
  u8 master = 0x70;
  u8 echoVolume = 0x54;
  u8 feedback = 0x50;
  u8 filter = 0;
};

[[nodiscard]] ModernDefaults modernDefaults(ByteReader reader) {
  // The three games use different mixer defaults, but the DSP reset routine
  // applies them through the same instruction skeleton.
  for (u32 offset = 0; offset + 32 <= reader.size(); ++offset) {
    if (reader.u8At(offset) == 0x8f && reader.u8At(offset + 1) == 0x6c && reader.u8At(offset + 2) == 0xf2 &&
        reader.u8At(offset + 3) == 0xe8 && reader.u8At(offset + 4) == 0x20 &&
        reader.u8At(offset + 5) == 0xc4 && reader.u8At(offset + 6) == 0xf3 &&
        reader.u8At(offset + 7) == 0xc5 && reader.u8At(offset + 10) == 0xe8 &&
        reader.u8At(offset + 12) == 0xc5 && reader.u8At(offset + 15) == 0x3f &&
        reader.u8At(offset + 18) == 0xe8 && reader.u8At(offset + 20) == 0x3f &&
        reader.u8At(offset + 23) == 0xe8 && reader.u8At(offset + 25) == 0xc5 &&
        reader.u8At(offset + 28) == 0xe8 && reader.u8At(offset + 30) == 0xc5) {
      return ModernDefaults{
          .master = reader.u8At(offset + 11),
          .echoVolume = reader.u8At(offset + 19),
          .feedback = reader.u8At(offset + 24),
          .filter = reader.u8At(offset + 29),
      };
    }
  }
  return {};
}

}  // namespace

std::optional<Layout> findLayout(ByteReader reader) {
  if (reader.size() != kAramSize) {
    return std::nullopt;
  }

  Version version;
  u16 base;
  if (const auto loader = findBytePattern(reader, kModernLoader)) {
    version = Version::Modern;
    base = static_cast<u16>(reader.u8At(*loader + 4) << 8);
    if (!signature(reader, base, "S2C")) {
      return std::nullopt;
    }
  } else if (const auto originalLoader = findBytePattern(reader, kOriginalLoader)) {
    version = Version::Original;
    base = static_cast<u16>(reader.u8At(*originalLoader + 4) << 8);
    if (!signature(reader, base, "SFC")) {
      return std::nullopt;
    }
  } else {
    return std::nullopt;
  }

  const auto instruments = instrumentTable(reader, version);
  const auto directoryCode = findBytePattern(reader, kDirectory);
  if (!instruments || !directoryCode || !reader.has(base, version == Version::Modern ? 0x50 : 0x40)) {
    return std::nullopt;
  }

  const ModernDefaults defaults = version == Version::Modern ? modernDefaults(reader) : ModernDefaults{};
  Layout layout{
      .version = version,
      .sequenceBaseAddress = base,
      .instrumentTableAddress = *instruments,
      .spcDirAddress = static_cast<u16>(reader.u8At(*directoryCode + 1) << 8),
      .initialTempo = reader.u8At(base + 3),
      .initialMasterVolume = version == Version::Modern ? defaults.master : u8{0x7f},
      .initialEchoDelay = version == Version::Modern ? reader.u8At(base + 0x19) : u8{4},
      .initialEchoVolume = version == Version::Modern ? defaults.echoVolume : u8{0x40},
      .initialEchoFeedback = version == Version::Modern ? defaults.feedback : u8{0x30},
      .initialEchoFilter = version == Version::Modern ? defaults.filter : u8{3},
      .hasPitchDrift = version == Version::Modern && findBytePattern(reader, kPitchDrift).has_value(),
  };

  bool anyTrack = false;
  for (u32 track = 0; track < kTrackCount; ++track) {
    const u8 flags = reader.u8At(base + 0x10 + track);
    const u32 pointer = base + 0x20 + track * 2;
    const u16 encoded = reader.le16(pointer);
    const u16 playlist = version == Version::Modern ? static_cast<u16>(base + encoded) : encoded;
    const bool active = flags != 0xff && reader.has(playlist, 1);
    layout.tracks[track] = TrackLayout{
        .active = active,
        .percussion = active && (flags & 0x80) != 0,
        .playlistAddress = playlist,
        .pointerRange = reader.range(pointer, 2),
    };
    anyTrack |= active;
  }
  if (!anyTrack || !reader.has(layout.spcDirAddress, 4)) {
    return std::nullopt;
  }
  layout.percussion = percussionTable(reader, version);
  return layout;
}

}  // namespace vgmtrans::formats::neverland_snes
