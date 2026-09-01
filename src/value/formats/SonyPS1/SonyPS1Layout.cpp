/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/SonyPS1/SonyPS1.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace vgmtrans::formats::sony_ps1 {

using namespace core;

namespace {

constexpr u32 kSeqSignature = 0x70514553;          // "SEQp"
constexpr u32 kReversedSeqSignature = 0x53455170;  // "pQES"
constexpr u32 kVabSignature = 0x56414270;          // "pBAV"
constexpr u32 kLogicalVabSignature = 0x70424156;   // "VABp"
constexpr u32 kSeqHeaderSize = 15;
constexpr u32 kSepFirstHeaderSize = 19;
constexpr u32 kSepSequenceHeaderSize = 13;
constexpr u32 kVabHeaderSize = 0x20;
constexpr u32 kProgramSize = 0x10;
constexpr u32 kToneSize = 0x20;
constexpr u32 kTonesPerProgram = 16;
constexpr u32 kSampleSizeTableBytes = 0x200;
constexpr u32 kMaxSequenceEvents = 262144;
constexpr u32 kPsxMainRamMask = 0x1fffff;

[[nodiscard]] bool rangeValid(ByteReader reader, u64 offset, u64 size) {
  return offset <= reader.size() && size <= reader.size() - offset;
}

[[nodiscard]] std::optional<u32> psxMainRamOffset(u32 address, ByteReader reader) {
  const u32 segment = address & 0xe0000000;
  if (segment != 0x80000000 && segment != 0xa0000000) {
    return std::nullopt;
  }
  const u32 offset = address & kPsxMainRamMask;
  return offset < reader.size() ? std::optional{offset} : std::nullopt;
}

// Konami's two audited libsnd derivatives retain a compact runtime VAB record:
// the header pointer is at +0 and the separately loaded sample body at +0x0c.
// Following the pair is safe across relocated PSF RAM images because both
// pointers and the complete VAB size table are validated before use.
[[nodiscard]] std::optional<u32> findRuntimeLinkedSampleBody(ByteReader reader,
                                                             const SonyPs1BankLayout& layout) {
  std::optional<u32> found;
  for (u64 candidate = 0; candidate + 0x10 <= reader.size(); candidate += 4) {
    const auto header = psxMainRamOffset(reader.le32(candidate), reader);
    if (!header || *header != layout.offset) {
      continue;
    }
    const auto body = psxMainRamOffset(reader.le32(candidate + 0x0c), reader);
    if (!body || !matchesSonyPs1SampleBodyAt(reader, *body, layout.sampleSizes)) {
      continue;
    }
    if (found && *found != *body) {
      return std::nullopt;
    }
    found = *body;
  }
  return found;
}

[[nodiscard]] bool seqSignature(ByteReader reader, u32 offset) {
  if (!reader.has(offset, 4)) {
    return false;
  }
  const u32 signature = reader.le32(offset);
  return signature == kSeqSignature || signature == kReversedSeqSignature;
}

[[nodiscard]] u32 be24(ByteReader reader, u32 offset) {
  return (static_cast<u32>(reader.u8At(offset)) << 16) | (static_cast<u32>(reader.u8At(offset + 1)) << 8) |
         reader.u8At(offset + 2);
}

[[nodiscard]] bool validSequenceHeader(u16 ppqn, u32 tempo, u8 numerator, u8 denominatorPower) {
  if (ppqn == 0 || tempo == 0) {
    return false;
  }
  if (numerator == 0) {
    return denominatorPower == 0;
  }
  return numerator <= 32 && denominatorPower <= 7;
}

[[nodiscard]] bool zeroFilled(ByteReader reader, u32 offset, u32 end, u32 count) {
  if (static_cast<u64>(offset) + count > end || !reader.has(offset, count)) {
    return false;
  }
  for (u32 byte = 0; byte < count; ++byte) {
    if (reader.u8At(offset + byte) != 0) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] std::optional<std::vector<SonyPs1EventLayout>> readEvents(ByteReader reader, u32 begin, u32 end) {
  std::vector<SonyPs1EventLayout> events;
  events.reserve(1024);
  u32 offset = begin;
  u8 runningStatus = 0;
  std::optional<u32> loopDestination;
  u8 loopCount = 0;
  bool loopCountSet = false;

  while (offset < end && events.size() < kMaxSequenceEvents) {
    // Preserve the legacy PS1 parser's PSF-rip safeguard. Some rips omit the
    // End-of-Track event and leave the sequence followed by zero-filled RAM.
    if (zeroFilled(reader, offset, end, 10)) {
      if (events.empty()) {
        return std::nullopt;
      }
      events.back().implicitEnd = true;
      return events;
    }
    SonyPs1EventLayout event{.offset = offset};
    u32 delta = 0;
    for (u8 bytes = 0; bytes < 4; ++bytes) {
      if (offset >= end || !reader.has(offset, 1)) {
        return std::nullopt;
      }
      const u8 value = reader.u8At(offset++);
      delta = (delta << 7) | (value & 0x7f);
      ++event.deltaSize;
      if ((value & 0x80) == 0) {
        break;
      }
      if (bytes == 3) {
        return std::nullopt;
      }
    }
    event.delta = delta;
    if (offset >= end || !reader.has(offset, 1)) {
      return std::nullopt;
    }

    u8 status = reader.u8At(offset);
    if ((status & 0x80) != 0) {
      event.explicitStatus = true;
      ++offset;
      runningStatus = status;
    } else {
      status = runningStatus;
      if (status == 0) {
        return std::nullopt;
      }
    }
    event.status = status;

    const auto data = [&](u8 count) -> bool {
      if (!rangeValid(reader, offset, count) || offset + count > end) {
        return false;
      }
      event.dataBytes = count;
      if (count != 0) {
        event.data1 = reader.u8At(offset);
      }
      if (count > 1) {
        event.data2 = reader.u8At(offset + 1);
      }
      offset += count;
      return true;
    };

    switch (status & 0xf0) {
      case 0x90:
      case 0xb0:
      case 0xe0:
        if (!data(2)) {
          return std::nullopt;
        }
        break;
      case 0xc0:
        if (!data(1)) {
          return std::nullopt;
        }
        break;
      case 0xf0:
        if (status != 0xff || !data(1)) {
          return std::nullopt;
        }
        if (event.data1 == 0x51) {
          if (!rangeValid(reader, offset, 3) || offset + 3 > end) {
            return std::nullopt;
          }
          event.data2 = reader.u8At(offset);
          event.dataBytes = 4;
          offset += 3;
        } else if (event.data1 == 0x2f) {
          // Sony's documented three-byte end marker is FF 2F 00. The driver
          // stops at 2F, but consuming its zero keeps SEQ and SEP ranges exact.
          if (offset < end && reader.u8At(offset) == 0) {
            ++event.dataBytes;
            ++offset;
          }
        } else {
          return std::nullopt;
        }
        break;
      default:
        return std::nullopt;
    }

    event.end = offset;
    if ((status & 0xf0) == 0xb0) {
      if (event.data1 == 99 && event.data2 == 20) {
        loopDestination = event.end;
        loopCount = 0;
        loopCountSet = false;
      } else if (loopDestination && !loopCountSet && (event.data1 == 98 || event.data1 == 6)) {
        // Runtime libraries up through the early PS1 era used NRPN LSB (CC98)
        // for this value; later libraries used Data Entry (CC6). Both forms
        // are self-describing and can coexist in one parser.
        loopCount = event.data2;
        loopCountSet = true;
      } else if (event.data1 == 99 && event.data2 == 30 && loopDestination) {
        event.loopDestination = loopDestination;
        event.loopCount = loopCount;
        loopDestination.reset();
      }
    }
    // Count 127 never falls through in libsnd. PSF rips commonly omit the
    // unreachable FF 2F marker, so the loop end is also a structural boundary.
    const bool terminalLoop = event.loopDestination.has_value() && event.loopCount == 127;
    events.push_back(event);
    if (terminalLoop || (status == 0xff && event.data1 == 0x2f)) {
      return events;
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<SonyPs1SequenceLayout> readOrdinarySequence(ByteReader reader, u32 offset) {
  if (!rangeValid(reader, offset, kSeqHeaderSize) || !seqSignature(reader, offset) || reader.be32(offset + 4) != 1) {
    return std::nullopt;
  }
  const u16 ppqn = reader.be16(offset + 8);
  const u32 tempo = be24(reader, offset + 10);
  const u8 numerator = reader.u8At(offset + 13);
  const u8 denominator = reader.u8At(offset + 14);
  if (!validSequenceHeader(ppqn, tempo, numerator, denominator)) {
    return std::nullopt;
  }
  auto events = readEvents(reader, offset + kSeqHeaderSize,
                           static_cast<u32>(std::min<u64>(reader.size(), std::numeric_limits<u32>::max())));
  if (!events) {
    return std::nullopt;
  }
  const u32 end = events->back().end;
  return SonyPs1SequenceLayout{
      .offset = offset,
      .length = end - offset,
      .dataOffset = offset + kSeqHeaderSize,
      .dataEnd = end,
      .ppqn = ppqn,
      .initialTempo = tempo,
      .rhythmNumerator = numerator,
      .rhythmDenominatorPower = denominator,
      .sequenceId = 0,
      .sep = false,
      .sepFirst = false,
      .events = std::move(*events),
  };
}

[[nodiscard]] std::vector<SonyPs1SequenceLayout> readSep(ByteReader reader, u32 offset, u32& containerEnd) {
  std::vector<SonyPs1SequenceLayout> layouts;
  containerEnd = offset;
  if (!rangeValid(reader, offset, kSepFirstHeaderSize) || !seqSignature(reader, offset) ||
      reader.be16(offset + 4) > 1) {
    return layouts;
  }

  u32 header = offset;
  u16 expectedId = reader.be16(offset + 6);
  bool first = true;
  while (rangeValid(reader, header, first ? kSepFirstHeaderSize : kSepSequenceHeaderSize)) {
    const u32 fields = first ? header + 6 : header;
    const u16 sequenceId = reader.be16(fields);
    const u16 ppqn = reader.be16(fields + 2);
    const u32 tempo = be24(reader, fields + 4);
    const u8 numerator = reader.u8At(fields + 7);
    const u8 denominator = reader.u8At(fields + 8);
    const u32 dataSize = reader.be32(fields + 9);
    const u32 headerSize = first ? kSepFirstHeaderSize : kSepSequenceHeaderSize;
    if (sequenceId != expectedId || !validSequenceHeader(ppqn, tempo, numerator, denominator) || dataSize < 3 ||
        dataSize > std::numeric_limits<u32>::max() - header - headerSize ||
        !rangeValid(reader, header + headerSize, dataSize)) {
      break;
    }
    const u32 dataOffset = header + headerSize;
    const u32 dataEnd = dataOffset + dataSize;
    auto events = readEvents(reader, dataOffset, dataEnd);
    if (!events) {
      break;
    }
    layouts.push_back(SonyPs1SequenceLayout{
        .offset = header,
        .length = headerSize + dataSize,
        .dataOffset = dataOffset,
        .dataEnd = events->back().end,
        .ppqn = ppqn,
        .initialTempo = tempo,
        .rhythmNumerator = numerator,
        .rhythmDenominatorPower = denominator,
        .sequenceId = sequenceId,
        .sep = true,
        .sepFirst = first,
        .events = std::move(*events),
    });
    containerEnd = dataEnd;
    header = dataEnd;
    ++expectedId;
    first = false;
  }
  return layouts;
}

}  // namespace

std::optional<SonyPs1SequenceLayout> readSonyPs1SequenceLayout(ByteReader reader, u32 offset) {
  if (auto layout = readOrdinarySequence(reader, offset)) {
    return layout;
  }
  u32 end = offset;
  auto sep = readSep(reader, offset, end);
  return sep.empty() ? std::nullopt : std::optional<SonyPs1SequenceLayout>{std::move(sep.front())};
}

std::vector<SonyPs1SequenceLayout> findSonyPs1Sequences(ByteReader reader) {
  std::vector<SonyPs1SequenceLayout> layouts;
  const u64 last = reader.size() < kSeqHeaderSize ? 0 : reader.size() - kSeqHeaderSize;
  for (u64 candidate = 0; candidate <= last && candidate <= std::numeric_limits<u32>::max(); ++candidate) {
    const u32 offset = static_cast<u32>(candidate);
    if (!seqSignature(reader, offset)) {
      continue;
    }
    if (auto sequence = readOrdinarySequence(reader, offset)) {
      const u32 length = sequence->length;
      layouts.push_back(std::move(*sequence));
      candidate += length - 1;
      continue;
    }
    u32 containerEnd = offset;
    auto sep = readSep(reader, offset, containerEnd);
    if (!sep.empty()) {
      layouts.insert(layouts.end(), std::make_move_iterator(sep.begin()), std::make_move_iterator(sep.end()));
      candidate = containerEnd - 1;
    }
  }
  return layouts;
}

std::optional<SonyPs1BankLayout> readSonyPs1BankLayout(ByteReader reader, u32 offset) {
  if (!rangeValid(reader, offset, kVabHeaderSize)) {
    return std::nullopt;
  }
  const u32 signature = reader.le32(offset);
  if (signature != kVabSignature && signature != kLogicalVabSignature) {
    return std::nullopt;
  }

  SonyPs1BankLayout layout{
      .offset = offset,
      .declaredFileSize = reader.le32(offset + 0x0c),
      .version = reader.le32(offset + 0x04),
      .id = reader.le32(offset + 0x08),
      .programCount = reader.le16(offset + 0x12),
      .toneCount = reader.le16(offset + 0x14),
      .sampleCount = reader.le16(offset + 0x16),
      .masterVolume = reader.u8At(offset + 0x18),
      .masterPan = reader.u8At(offset + 0x19),
  };
  if (layout.version == 0 || layout.version > 0xffff || layout.sampleCount > 255) {
    return std::nullopt;
  }

  // libsnd retains both layouts. Version 5 introduced the 128-entry program
  // table and changed VAG sizes from 4-byte to 8-byte units.
  layout.programSlots = layout.version > 4 ? 128 : 64;
  layout.sampleSizeShift = layout.version > 4 ? 3 : 2;
  if (layout.programCount == 0 || layout.programCount > layout.programSlots ||
      layout.toneCount > layout.programCount * kTonesPerProgram) {
    return std::nullopt;
  }

  const u64 toneTable = static_cast<u64>(offset) + kVabHeaderSize + layout.programSlots * kProgramSize;
  const u64 sizeTable = toneTable + static_cast<u64>(layout.programCount) * kTonesPerProgram * kToneSize;
  const u64 sampleData = sizeTable + kSampleSizeTableBytes;
  if (sampleData > std::numeric_limits<u32>::max() || !rangeValid(reader, offset, sampleData - offset)) {
    return std::nullopt;
  }
  layout.headerSize = static_cast<u32>(sampleData - offset);
  layout.sampleDataOffset = static_cast<u32>(sampleData);
  layout.sampleSizes.reserve(layout.sampleCount);
  u64 expected = 0;
  for (u32 sample = 0; sample < layout.sampleCount; ++sample) {
    const u32 size = static_cast<u32>(reader.le16(sizeTable + (sample + 1) * 2)) << layout.sampleSizeShift;
    expected += size;
    if (expected > std::numeric_limits<u32>::max()) {
      return std::nullopt;
    }
    layout.sampleSizes.push_back(size);
  }
  layout.expectedSampleBytes = static_cast<u32>(expected);
  auto locatedBody = matchSonyPs1SampleBody(reader, layout.sampleDataOffset, layout.sampleSizes);
  if (!locatedBody && matchesSonyPs1SampleBodyAt(reader, layout.sampleDataOffset, layout.sampleSizes)) {
    locatedBody = layout.sampleDataOffset;
  }
  layout.hasSampleBody = locatedBody.has_value();
  if (locatedBody) {
    layout.sampleDataOffset = *locatedBody;
  }
  layout.length = layout.headerSize;
  if (layout.hasSampleBody && layout.sampleDataOffset == offset + layout.headerSize) {
    layout.length += layout.expectedSampleBytes;
  }
  return layout;
}

std::vector<SonyPs1BankLayout> findSonyPs1Banks(ByteReader reader) {
  std::vector<SonyPs1BankLayout> layouts;
  if (reader.size() < kVabHeaderSize) {
    return layouts;
  }
  const u64 last = reader.size() - kVabHeaderSize;
  for (u64 candidate = 0; candidate <= last && candidate <= std::numeric_limits<u32>::max(); ++candidate) {
    const u32 offset = static_cast<u32>(candidate);
    const u32 signature = reader.le32(offset);
    if (signature != kVabSignature && signature != kLogicalVabSignature) {
      continue;
    }
    if (auto layout = readSonyPs1BankLayout(reader, offset)) {
      const u32 length = layout->length;
      layouts.push_back(std::move(*layout));
      candidate += length - 1;
    }
  }
  if (layouts.size() == 1 && !layouts.front().hasSampleBody) {
    auto& layout = layouts.front();
    const auto body = matchSonyPs1SampleBody(reader, layout.sampleDataOffset, layout.sampleSizes, true);
    if (body) {
      layout.sampleDataOffset = *body;
      layout.hasSampleBody = true;
      if (layout.sampleDataOffset == layout.offset + layout.headerSize) {
        layout.length += layout.expectedSampleBytes;
      }
    }
  }
  for (auto& layout : layouts) {
    if (layout.hasSampleBody) {
      continue;
    }
    if (const auto body = findRuntimeLinkedSampleBody(reader, layout)) {
      layout.sampleDataOffset = *body;
      layout.hasSampleBody = true;
    }
  }
  return layouts;
}

}  // namespace vgmtrans::formats::sony_ps1
