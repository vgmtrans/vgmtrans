/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/GraphResSnes/GraphResSnes.h"

#include "value/scan/BytePattern.h"

#include <array>
#include <optional>

namespace vgmtrans::formats::graph_res_snes {

using namespace core;

namespace {

constexpr auto kLoadSequence = makeMaskedBytePattern(
    "\x3f\x00\x00\xe8\x00\xc4\x0d\xe8\x00\xc4\x0e\xe5\x00\x00\x80\xa8"
    "\x18\xc4\x0f\xe5\x00\x00\xa8\x00\xc4\x10",
    "x??x?xxx?xxx??xxxxxx??xxxx");
constexpr auto kDspInitialization = makeMaskedBytePattern(
    "\x8d\x00\xf6\x00\x00\x5d\xf6\x00\x00\x68\xff\xf0\x07\x3f\x00\x00\xfc\xfc",
    "xxx??xx??xxxxx??xx");
constexpr auto kTableInitialization = makeMaskedBytePattern(
    "\xe5\x00\x00\xc4\x05\xe5\x00\x00\xc4\x06\xe5\x00\x00\xc4\x07\xe5\x00\x00\xc4\x08",
    "x??xxx??xxx??xxx??xx");
constexpr auto kTimerInitialization =
    makeMaskedBytePattern("\xe8\x00\xc4\xf1\xe8\x00\xc4\xfa\xe8\x01\xc4\xf1", "xxxxx?xxxxxx");

// The game stores its starting sound settings as number/value pairs. Read them
// into a table until the $FF end marker is reached.
[[nodiscard]] std::optional<std::array<u8, 128>> readDspRegisters(ByteReader reader, u16 address) {
  std::array<u8, 128> registers{};
  for (u32 item = 0; item < 128 && reader.has(address, 1); ++item) {
    const u8 reg = reader.u8At(address++);
    if (reg == 0xff) {
      return registers;
    }
    if (reg >= registers.size() || !reader.has(address, 1)) {
      return std::nullopt;
    }
    registers[reg] = reader.u8At(address++);
  }
  return std::nullopt;
}

// The game does not store the number of pitch patterns. The first pattern
// begins immediately after the pointer list, so its address reveals the count.
[[nodiscard]] u8 readPitchEnvelopeCount(ByteReader reader, u16 list) {
  if (!reader.has(list, 2)) {
    return 0;
  }
  const u16 firstTable = reader.le16(list);
  if (firstTable <= list || ((firstTable - list) & 1) != 0) {
    return 0;
  }
  const u32 count = (firstTable - list) / 2u;
  if (count == 0 || count > 0xff || !reader.has(list, count * 2u)) {
    return 0;
  }
  for (u32 index = 0; index < count; ++index) {
    if (!reader.has(reader.le16(list + index * 2u), 4)) {
      return 0;
    }
  }
  return static_cast<u8>(count);
}

}  // namespace

// Find several recognizable pieces of the sound driver, then read the table
// addresses embedded in them. This works even when a game moves the driver.
std::optional<Layout> findLayout(ByteReader reader) {
  if (reader.size() != kAramSize) {
    return std::nullopt;
  }
  const auto loadSequence = findBytePattern(reader, kLoadSequence);
  const auto initializeDsp = findBytePattern(reader, kDspInitialization);
  const auto initializeTables = findBytePattern(reader, kTableInitialization);
  const auto initializeTimer = findBytePattern(reader, kTimerInitialization);
  if (!loadSequence || !initializeDsp || !initializeTables || !initializeTimer) {
    return std::nullopt;
  }

  const u16 sequenceHeader = static_cast<u16>(reader.u8At(*loadSequence + 4) | (reader.u8At(*loadSequence + 8) << 8));
  const u16 tablePointers = reader.le16(*initializeTables + 1);
  const u16 dspList = reader.le16(*initializeDsp + 7);
  if (!reader.has(sequenceHeader, kTrackCount * 3u) || !reader.has(tablePointers, 8)) {
    return std::nullopt;
  }
  const auto dspRegisters = readDspRegisters(reader, dspList);
  if (!dspRegisters) {
    return std::nullopt;
  }

  Layout layout{
      .sequenceHeaderAddress = sequenceHeader,
      .volumeTableAddress = reader.le16(tablePointers),
      .panTableAddress = reader.le16(tablePointers + 2),
      .pitchTableAddress = reader.le16(tablePointers + 4),
      .pitchEnvelopeListAddress = reader.le16(tablePointers + 6),
      .spcDirAddress = static_cast<u16>((*dspRegisters)[0x5d] << 8),
      .timerTarget = reader.u8At(*initializeTimer + 5),
      // The chip reset sets master volume to zero, but the sequence player
      // keeps its own starting master volume of $7F.
      .dsp = DspState{
          .echoFeedback = static_cast<s8>((*dspRegisters)[0x0d]),
          .echoVoices = (*dspRegisters)[0x4d],
          .echoDelay = static_cast<u8>((*dspRegisters)[0x7d] & 0x0f),
          .flags = (*dspRegisters)[0x6c],
      },
  };
  for (u8 coefficient = 0; coefficient < layout.dsp.fir.size(); ++coefficient) {
    layout.dsp.fir[coefficient] = static_cast<s8>((*dspRegisters)[coefficient * 0x10u + 0x0f]);
  }
  layout.pitchEnvelopeCount = readPitchEnvelopeCount(reader, layout.pitchEnvelopeListAddress);
  if (layout.timerTarget == 0 || layout.pitchEnvelopeCount == 0 || !reader.has(layout.volumeTableAddress, 16) ||
      !reader.has(layout.panTableAddress, 32) || !reader.has(layout.pitchTableAddress, 48 * 2u) ||
      layout.spcDirAddress == 0 || !reader.has(layout.spcDirAddress, 4)) {
    return std::nullopt;
  }

  const u16 virtualBase = reader.le16(sequenceHeader + 1);
  for (u8 track = 0; track < kTrackCount; ++track) {
    const u32 item = sequenceHeader + track * 3u;
    if (reader.u8At(item) == 0) {
      continue;
    }
    const u16 virtualStart = reader.le16(item + 1);
    const u16 start = static_cast<u16>(sequenceHeader + kTrackCount * 3u + virtualStart - virtualBase);
    if (!reader.has(start, 1)) {
      return std::nullopt;
    }
    layout.tracks.push_back(TrackHeader{.index = track, .startAddress = start, .range = reader.range(item, 3)});
  }
  return layout.tracks.empty() ? std::nullopt : std::optional{std::move(layout)};
}

}  // namespace vgmtrans::formats::graph_res_snes
