/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/ItikitiSnes/ItikitiSnes.h"

#include "value/scan/BytePattern.h"

#include <bit>
#include <optional>

namespace vgmtrans::formats::itikiti_snes {

using namespace core;

namespace {

// This is the header parser at Rudra $0eb5. The first indexed load names the
// per-group live header-end pointer; entries are two bytes apart.
constexpr auto kSelectSequence = makeMaskedBytePattern(
    "\xed\x6b\xde\xf8\xa1\xf5\x00\x00\xc4\x02\xf5\x00\x00\xc4\x03\x8d\x01\xe4\xef\x77\x02", "xxxxxx??xxx??xxxxxx??");

// Instrument selection reads one big-endian tuning word and one ADSR word for
// every SRCN. The upper-SRCN branch proves these are two continuous tables.
constexpr auto kLoadInstrument = makeMaskedBytePattern(
    "\xf5\x00\x00\xd6\x80\xef\xf5\x00\x00\xd6\x81\xef\xf5\x00\x00\xd6\x00\xee\xf5\x00\x00\x2f\x00",
    "x??xxxx??xxxx??xxxx??x?");

struct SequenceHeader {
  u16 address;
  u16 base;
  u8 tracks;
  u8 echoDelay;
};

[[nodiscard]] unsigned activeVoices(ByteReader reader, u16 state, u8 group) {
  const u32 address = state + 0x10 + group * 2u;
  return reader.has(address, 2) ? std::popcount(static_cast<unsigned>(reader.u8At(address) | reader.u8At(address + 1)))
                                : 0;
}

[[nodiscard]] std::optional<SequenceHeader> groupHeader(ByteReader reader, u16 state, u8 group) {
  const u32 slot = state + group * 2u;
  if (!reader.has(slot, 2)) {
    return std::nullopt;
  }
  const u16 end = reader.le16(slot);
  for (u8 tracks = kTrackCount; tracks != 0; --tracks) {
    const u32 size = 2 + tracks * 2u;
    if (end < size) {
      continue;
    }
    const u16 address = static_cast<u16>(end - size);
    if (!reader.has(address, size) || reader.u8At(address + 1) != tracks) {
      continue;
    }
    const u16 first = reader.le16(address + 2);
    const u16 base = static_cast<u16>(end - first);
    bool valid = true;
    for (u32 track = 0; track < tracks; ++track) {
      const u16 start = static_cast<u16>(base + reader.le16(address + 2 + track * 2));
      valid &= start >= 0x0200 && reader.has(start, 1);
    }
    if (valid) {
      return SequenceHeader{.address = address, .base = base, .tracks = tracks, .echoDelay = reader.u8At(address)};
    }
  }
  return std::nullopt;
}

}  // namespace

std::optional<Layout> findLayout(ByteReader reader) {
  if (reader.size() != kAramSize) {
    return std::nullopt;
  }
  const auto sequenceCode = findBytePattern(reader, kSelectSequence);
  const auto instrumentCode = findBytePattern(reader, kLoadInstrument);
  if (!sequenceCode || !instrumentCode) {
    return std::nullopt;
  }

  const u16 state = reader.le16(*sequenceCode + 6);
  const u16 tuning = reader.le16(*instrumentCode + 1);
  const u16 adsr = reader.le16(*instrumentCode + 13);
  if (reader.le16(*sequenceCode + 11) != static_cast<u16>(state + 1) ||
      reader.le16(*instrumentCode + 7) != static_cast<u16>(tuning + 1) ||
      reader.le16(*instrumentCode + 19) != static_cast<u16>(adsr + 1) || tuning < 0x0240 || !reader.has(tuning, 2) ||
      !reader.has(adsr, 2)) {
    return std::nullopt;
  }

  // Group zero is the music group. A battle snapshot uses group one instead,
  // so fall back to the active valid group with the most voices.
  std::optional<SequenceHeader> selected = groupHeader(reader, state, 0);
  u8 selectedGroup = 0;
  unsigned selectedVoices = selected ? activeVoices(reader, state, 0) : 0;
  if (!selected || selectedVoices == 0) {
    for (u8 group = 1; group < 7; ++group) {
      const auto candidate = groupHeader(reader, state, group);
      const unsigned voices = candidate ? activeVoices(reader, state, group) : 0;
      if (candidate && (!selected || voices > selectedVoices)) {
        selected = candidate;
        selectedGroup = group;
        selectedVoices = voices;
      }
    }
  }
  if (!selected) {
    return std::nullopt;
  }

  const u16 directory = static_cast<u16>(tuning - 0x0240);
  if (!reader.has(directory, 4)) {
    return std::nullopt;
  }
  return Layout{
      .sequenceHeaderAddress = selected->address,
      .sequenceBaseAddress = selected->base,
      .tuningTableAddress = tuning,
      .adsrTableAddress = adsr,
      .spcDirAddress = directory,
      .groupIndex = selectedGroup,
      .trackCount = selected->tracks,
      .echoDelay = selectedGroup == 0 ? selected->echoDelay : u8{0},
  };
}

}  // namespace vgmtrans::formats::itikiti_snes
