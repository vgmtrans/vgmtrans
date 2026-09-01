/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/KonamiPS1/KonamiPS1.h"

#include "value/formats/SonyPS1/SonyPS1.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace vgmtrans::formats::konami_ps1 {

using namespace core;

namespace {

constexpr u32 kHeaderSize = 0x10;
constexpr u32 kMaximumTracks = 64;
constexpr u32 kMaximumEvents = 1048576;
constexpr u32 kMaximumSequenceSize = 0x200000;
constexpr u32 kProgramSize = 0x10;
constexpr u32 kToneSize = 0x20;
constexpr u32 kTonesPerProgram = 16;

[[nodiscard]] bool signature(ByteReader reader, u32 offset, char fourth) {
  return reader.has(offset, 4) && reader.u8At(offset) == 'K' && reader.u8At(offset + 1) == 'D' &&
         reader.u8At(offset + 2) == 'T' && reader.u8At(offset + 3) == static_cast<u8>(fourth);
}

[[nodiscard]] std::optional<u32> vlq(ByteReader reader, u32& offset, u32 end) {
  u32 result = 0;
  for (u32 byte = 0; byte < 4 && offset < end; ++byte) {
    const u8 value = reader.u8At(offset++);
    result = (result << 7) | (value & 0x7f);
    if ((value & 0x80) == 0) {
      return result;
    }
  }
  return std::nullopt;
}

[[nodiscard]] EventKind eventKind(u8 command) {
  switch (command) {
    case 70:
      return EventKind::SetChannel;
    case 71:
      return EventKind::Tempo;
    case 72:
      return EventKind::PitchBend;
    case 73:
      return EventKind::Program;
    case 74:
    case 75:
      return EventKind::NoteOff;
    case 127:
      return EventKind::End;
    default:
      return EventKind::Controller;
  }
}

[[nodiscard]] std::optional<TrackLayout> readTrack(ByteReader reader, u32 begin, u32 end) {
  TrackLayout track{.offset = begin, .end = end};
  std::optional<u32> loopDestination;
  u8 loopCount = 0;
  bool loopCountSet = false;
  bool chained = false;
  u32 offset = begin;

  while (offset < end && track.events.size() < kMaximumEvents) {
    EventLayout event{.offset = offset};
    if (!chained) {
      const auto delta = vlq(reader, offset, end);
      if (!delta) {
        return std::nullopt;
      }
      event.delta = *delta;
    }
    if (offset >= end) {
      return std::nullopt;
    }

    const u8 encoded = reader.u8At(offset++);
    if ((encoded & 0x80) == 0) {
      if (offset >= end) {
        return std::nullopt;
      }
      const u8 velocity = reader.u8At(offset++);
      event.kind = EventKind::Note;
      event.command = encoded;
      event.value = velocity & 0x7f;
      event.chained = (velocity & 0x80) != 0;
    } else {
      event.command = encoded & 0x7f;
      event.kind = eventKind(event.command);
      if (event.kind == EventKind::NoteOff) {
        event.chained = event.command == 75;
      } else {
        // Even FF consumes one byte in both audited driver generations.
        if (offset >= end) {
          return std::nullopt;
        }
        const u8 value = reader.u8At(offset++);
        event.value = value & 0x7f;
        event.chained = (value & 0x80) != 0;
      }

      if (event.kind == EventKind::Controller && event.command == 99) {
        if (event.value == 20) {
          loopDestination = offset;
          loopCount = 127;
          loopCountSet = false;
        } else if (event.value == 30 && loopDestination) {
          event.loopDestination = loopDestination;
          event.loopCount = loopCount;
          loopDestination.reset();
        }
      } else if (event.kind == EventKind::Controller && event.command == 6 && loopDestination && !loopCountSet) {
        loopCount = event.value;
        loopCountSet = true;
      }
    }

    event.end = offset;
    chained = event.chained;
    // Infinite loop ends often retain a structurally valid FF terminator after
    // their unreachable fallthrough path, so keep decoding to the track bound.
    const bool terminal = event.kind == EventKind::End;
    track.events.push_back(event);
    if (terminal) {
      // Declared track sizes include no reachable bytes after a terminal event.
      return offset == end ? std::optional{std::move(track)} : std::nullopt;
    }
  }
  return offset == end && !track.events.empty() ? std::optional{std::move(track)} : std::nullopt;
}

}  // namespace

std::optional<SequenceLayout> readKonamiPs1SequenceLayout(ByteReader reader, u32 offset) {
  if (!reader.has(offset, kHeaderSize) || (!signature(reader, offset, '1') && !signature(reader, offset, '2'))) {
    return std::nullopt;
  }
  const u8 version = reader.u8At(offset + 3) - '0';
  const u32 length = reader.le32(offset + 4);
  const u32 ppqn = reader.le32(offset + 8);
  const u32 trackCount = reader.le32(offset + 12);
  if (length < kHeaderSize || length > kMaximumSequenceSize || !reader.has(offset, length) || ppqn == 0 ||
      ppqn > 9600 || trackCount == 0 || trackCount > kMaximumTracks || (version == 2 && trackCount > 32) ||
      (version == 1 ? static_cast<u64>(kHeaderSize) + trackCount * 2 : 0x50ull) > length) {
    return std::nullopt;
  }

  SequenceLayout layout{
      .containerOffset = offset,
      .containerLength = length,
      .offset = offset,
      .length = length,
      .version = version,
      .ppqn = ppqn,
  };
  // KDT2 is the older 32-slot layout used by the Azure Dreams generation;
  // KDT1 packs the table to its declared track count. Suikoden II's driver
  // explicitly selects between these offsets (0x50 versus 0x10+2*N).
  u32 trackOffset = offset + (version == 1 ? kHeaderSize + trackCount * 2 : 0x50);
  const u32 sequenceEnd = offset + length;
  layout.tracks.reserve(trackCount);
  for (u32 index = 0; index < trackCount; ++index) {
    const u32 size = reader.le16(offset + kHeaderSize + index * 2);
    if (size == 0 || size > sequenceEnd - trackOffset) {
      return std::nullopt;
    }
    auto track = readTrack(reader, trackOffset, trackOffset + size);
    if (!track) {
      return std::nullopt;
    }
    layout.tracks.push_back(std::move(*track));
    trackOffset += size;
  }
  // The KDT1 size includes its header and track-size table. This exact check is
  // intentional: adding another 0x10 here was the legacy loading regression.
  if (trackOffset != sequenceEnd) {
    return std::nullopt;
  }

  const u32 wrapperLength = version == 1 && offset >= kHeaderSize && signature(reader, offset - kHeaderSize, '2')
                                ? reader.le32(offset - kHeaderSize + 4)
                                : 0;
  if (wrapperLength >= length && wrapperLength - length <= 3 && (wrapperLength & 3) == 0 &&
      reader.has(offset, wrapperLength)) {
    layout.containerOffset = offset - kHeaderSize;
    layout.containerLength = wrapperLength + kHeaderSize;
    layout.sequenceId = reader.le32(offset - kHeaderSize + 8);
    layout.hasKdt2Header = true;
  }
  return layout;
}

std::vector<SequenceLayout> findKonamiPs1Sequences(ByteReader reader) {
  std::vector<SequenceLayout> layouts;
  if (reader.size() < kHeaderSize) {
    return layouts;
  }
  const u64 last = reader.size() - kHeaderSize;
  for (u64 candidate = 0; candidate <= last && candidate <= std::numeric_limits<u32>::max(); ++candidate) {
    const u32 offset = static_cast<u32>(candidate);
    if (!signature(reader, offset, '1') && !signature(reader, offset, '2')) {
      continue;
    }
    if (auto layout = readKonamiPs1SequenceLayout(reader, offset)) {
      const u32 length = layout->length;
      layouts.push_back(std::move(*layout));
      candidate += length - 1;
    }
  }
  return layouts;
}

std::vector<Tone> readKonamiPs1Tones(ByteReader reader) {
  std::vector<Tone> tones;
  const auto banks = sony_ps1::findSonyPs1Banks(reader);
  for (u32 bank = 0; bank < banks.size() && bank <= std::numeric_limits<u16>::max(); ++bank) {
    const auto& layout = banks[bank];
    const u32 programs = layout.offset + 0x20;
    const u32 toneTable = programs + layout.programSlots * kProgramSize;
    u32 effectiveProgram = 0;
    for (u32 program = 0; program < layout.programSlots && effectiveProgram < layout.programCount; ++program) {
      const u32 programOffset = programs + program * kProgramSize;
      const u8 count = reader.u8At(programOffset);
      if (count == 0) {
        continue;
      }
      const u32 programTones = toneTable + effectiveProgram++ * kTonesPerProgram * kToneSize;
      for (u32 index = 0; index < std::min<u32>(count, kTonesPerProgram); ++index) {
        const u32 toneOffset = programTones + index * kToneSize;
        const u8 low = reader.u8At(toneOffset + 6);
        const u8 high = reader.u8At(toneOffset + 7);
        if (low > high || reader.le16(toneOffset + 0x16) == 0) {
          continue;
        }
        const u16 adsr1 = reader.le16(toneOffset + 0x10);
        const u16 adsr2 = reader.le16(toneOffset + 0x12);
        tones.push_back(Tone{
            .bank = static_cast<u16>(bank),
            .program = static_cast<u8>(program),
            .index = static_cast<u8>(index),
            .keyLow = low,
            .keyHigh = high,
            .flags = reader.u8At(toneOffset + 1),
            .bendDown = reader.u8At(toneOffset + 0x0c),
            .bendUp = reader.u8At(toneOffset + 0x0d),
            .adsr1 = adsr1,
            .adsr2 = adsr2,
            .originalAdsr1 = adsr1,
            .originalAdsr2 = adsr2,
        });
      }
    }
  }
  return tones;
}

}  // namespace vgmtrans::formats::konami_ps1
