/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/HeartBeatPS1/HeartBeatPS1.h"

#include <array>
#include <limits>

namespace vgmtrans::formats::heartbeat_ps1 {

using namespace core;

namespace {

constexpr u32 kContainerHeaderSize = 0x3c;
constexpr u32 kDescriptorOffset = 0x0c;
constexpr u32 kDescriptorSize = 0x0c;
constexpr u32 kSequenceHeaderSize = 0x10;
constexpr u32 kMaximumSectionSize = 0x200000;

[[nodiscard]] bool rangeValid(ByteReader reader, u64 offset, u64 size) {
  return offset <= reader.size() && size <= reader.size() - offset;
}

[[nodiscard]] bool qQesAt(ByteReader reader, u32 offset) {
  return rangeValid(reader, offset, 4) && reader.u8At(offset) == 'q' && reader.u8At(offset + 1) == 'Q' &&
         reader.u8At(offset + 2) == 'E' && reader.u8At(offset + 3) == 'S';
}

[[nodiscard]] std::optional<u32> readVlq(ByteReader reader, u32& cursor, u32 end) {
  u32 value = 0;
  for (u8 size = 0; cursor < end && size < 4; ++size) {
    const u8 byte = reader.u8At(cursor++);
    value = (value << 7) | (byte & 0x7f);
    if ((byte & 0x80) == 0) {
      return value;
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<std::vector<HeartBeatPs1EventLayout>> readEvents(ByteReader reader, u32 begin, u32 end) {
  std::vector<HeartBeatPs1EventLayout> events;
  std::array<u8, 16> nrpnMsb{};
  nrpnMsb.fill(127);
  std::array<std::optional<u32>, 16> loopDestination{};
  std::array<u8, 16> loopCount{};
  u8 runningStatus = 0;
  u32 cursor = begin;

  while (cursor < end && events.size() < 1048576) {
    const u32 offset = cursor;
    // Heart Beat's terminal marker is not preceded by a delta time.
    if (end - cursor >= 3 && reader.u8At(cursor) == 0xff && reader.u8At(cursor + 1) == 0x2f &&
        reader.u8At(cursor + 2) == 0) {
      events.push_back(HeartBeatPs1EventLayout{
          .offset = cursor,
          .end = cursor + 3,
          .status = 0xff,
          .data1 = 0x2f,
      });
      return events;
    }

    const auto delta = readVlq(reader, cursor, end);
    if (!delta || cursor >= end) {
      return std::nullopt;
    }

    u8 status = reader.u8At(cursor);
    if ((status & 0x80) != 0) {
      ++cursor;
      // The driver retains every status byte, including FF. Dragon Warrior
      // VII consequently encodes adjacent meta events with FF running status.
      runningStatus = status;
    } else {
      if (runningStatus == 0) {
        return std::nullopt;
      }
      status = runningStatus;
    }

    HeartBeatPs1EventLayout event{
        .offset = offset,
        .delta = *delta,
        .status = status,
    };
    const u8 family = status & 0xf0;
    if (family >= 0x80 && family <= 0xe0) {
      const u32 bytes = family == 0xc0 || family == 0xd0 ? 1 : 2;
      if (bytes > end - cursor) {
        return std::nullopt;
      }
      event.data1 = reader.u8At(cursor++);
      if (bytes == 2) {
        event.data2 = reader.u8At(cursor++);
      }
      if ((event.data1 & 0x80) != 0 || (bytes == 2 && (event.data2 & 0x80) != 0)) {
        return std::nullopt;
      }

      if (family == 0xb0) {
        const u8 channel = status & 0x0f;
        if (event.data1 == 99) {
          nrpnMsb[channel] = event.data2;
          if (event.data2 == 20) {
            loopDestination[channel].reset();
          } else if (event.data2 == 30 && loopDestination[channel]) {
            event.loopDestination = loopDestination[channel];
            event.loopCount = loopCount[channel];
          }
        } else if (event.data1 == 6 && nrpnMsb[channel] == 20) {
          loopDestination[channel] = cursor;
          loopCount[channel] = event.data2;
        }
      }
    } else if (status == 0xff) {
      if (cursor >= end) {
        return std::nullopt;
      }
      event.data1 = reader.u8At(cursor++);
      const auto payloadSize = readVlq(reader, cursor, end);
      if (!payloadSize || *payloadSize > end - cursor) {
        return std::nullopt;
      }
      event.payloadSize = *payloadSize;
      cursor += *payloadSize;
    } else {
      return std::nullopt;
    }
    event.end = cursor;
    events.push_back(event);
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<HeartBeatPs1SequenceLayout> readSequence(ByteReader reader, u32 containerOffset,
                                                                     u32 containerSize, u32 qQesOffset,
                                                                     u32 sequenceSize, u16 sequenceId,
                                                                     const std::array<u16, 4>& bankIds) {
  if (sequenceSize < kSequenceHeaderSize || sequenceSize > kMaximumSectionSize ||
      !rangeValid(reader, qQesOffset, sequenceSize) || !qQesAt(reader, qQesOffset)) {
    return std::nullopt;
  }
  const u16 ppqn = reader.be16(qQesOffset + 8);
  const u32 tempo = (static_cast<u32>(reader.u8At(qQesOffset + 10)) << 16) |
                    (static_cast<u32>(reader.u8At(qQesOffset + 11)) << 8) | reader.u8At(qQesOffset + 12);
  const u8 numerator = reader.u8At(qQesOffset + 13);
  const u8 denominatorPower = reader.u8At(qQesOffset + 14);
  const u8 trackCount = reader.u8At(qQesOffset + 15);
  if (ppqn == 0 || tempo == 0 || numerator == 0 || denominatorPower > 7 || trackCount == 0 || trackCount > 16) {
    return std::nullopt;
  }
  auto events = readEvents(reader, qQesOffset + kSequenceHeaderSize, qQesOffset + sequenceSize);
  if (!events) {
    return std::nullopt;
  }
  return HeartBeatPs1SequenceLayout{
      .containerOffset = containerOffset,
      .containerSize = containerSize,
      .qQesOffset = qQesOffset,
      .dataOffset = qQesOffset + kSequenceHeaderSize,
      .dataEnd = qQesOffset + sequenceSize,
      .sequenceId = sequenceId,
      .version = reader.be16(qQesOffset + 4),
      .ppqn = ppqn,
      .initialTempo = tempo,
      .rhythmNumerator = numerator,
      .rhythmDenominatorPower = denominatorPower,
      .trackCount = trackCount,
      .bankIds = bankIds,
      .events = std::move(*events),
  };
}

}  // namespace

std::optional<HeartBeatPs1ContainerLayout> readHeartBeatPs1Container(ByteReader reader, u32 offset) {
  if (!rangeValid(reader, offset, kContainerHeaderSize)) {
    return std::nullopt;
  }
  const u32 sequenceSize = reader.le32(offset);
  const u16 sequenceId = reader.le16(offset + 4);
  const u8 descriptorCount = reader.u8At(offset + 6);
  if (sequenceSize > kMaximumSectionSize || descriptorCount == 0 || descriptorCount > 4) {
    return std::nullopt;
  }

  struct Descriptor {
    u32 sampleSize = 0;
    u32 attributeSize = 0;
    u16 bankId = 0xffff;
  };
  std::array<Descriptor, 4> descriptors{};
  std::array<u16, 4> bankIds{0xffff, 0xffff, 0xffff, 0xffff};
  for (u32 index = 0; index < 4; ++index) {
    const u32 descriptorOffset = offset + kDescriptorOffset + index * kDescriptorSize;
    const u32 sampleSize = reader.le32(descriptorOffset);
    const u32 attributeSize = reader.le32(descriptorOffset + 4);
    const u16 bankId = reader.le16(descriptorOffset + 8);
    if (sampleSize > kMaximumSectionSize || attributeSize > kMaximumSectionSize || sampleSize % 16 != 0 ||
        attributeSize % 4 != 0 || (sampleSize == 0) != (attributeSize == 0) || (bankId != 0xffff && bankId > 4)) {
      return std::nullopt;
    }
    descriptors[index] = Descriptor{.sampleSize = sampleSize, .attributeSize = attributeSize, .bankId = bankId};
    bankIds[index] = bankId;
  }

  HeartBeatPs1ContainerLayout container;
  u64 cursor = static_cast<u64>(offset) + kContainerHeaderSize;
  for (u32 index = 0; index < descriptorCount; ++index) {
    const auto descriptor = descriptors[index];
    if (descriptor.sampleSize == 0) {
      continue;
    }
    if (!rangeValid(reader, cursor, static_cast<u64>(descriptor.sampleSize) + descriptor.attributeSize)) {
      return std::nullopt;
    }
    const u32 attributeOffset = static_cast<u32>(cursor) + descriptor.sampleSize;
    if (descriptor.attributeSize < 8) {
      return std::nullopt;
    }
    const u8 programCount = reader.u8At(attributeOffset + 1);
    const u16 toneCount = reader.le16(attributeOffset + 2);
    const u64 required = 8ull + static_cast<u64>(programCount) * 0x24 + static_cast<u64>(toneCount) * 0x14;
    if (programCount == 0 || toneCount == 0 || required > descriptor.attributeSize) {
      return std::nullopt;
    }
    container.banks.push_back(HeartBeatPs1BankLayout{
        .sampleOffset = static_cast<u32>(cursor),
        .sampleSize = descriptor.sampleSize,
        .attributeOffset = attributeOffset,
        .attributeSize = descriptor.attributeSize,
        .bank = descriptor.bankId,
        .programCount = programCount,
        .toneCount = toneCount,
        .masterVolume = reader.u8At(attributeOffset + 4),
        .masterPan = reader.u8At(attributeOffset + 5),
    });
    cursor += static_cast<u64>(descriptor.sampleSize) + descriptor.attributeSize;
  }

  if (cursor > std::numeric_limits<u32>::max() || !rangeValid(reader, cursor, sequenceSize)) {
    return std::nullopt;
  }
  const u64 totalLength = cursor + sequenceSize - offset;
  if (totalLength > std::numeric_limits<u32>::max()) {
    return std::nullopt;
  }
  container.length = static_cast<u32>(totalLength);
  if (sequenceSize != 0) {
    container.sequence =
        readSequence(reader, offset, container.length, static_cast<u32>(cursor), sequenceSize, sequenceId, bankIds);
    if (!container.sequence) {
      return std::nullopt;
    }
  }
  if (container.banks.empty() && !container.sequence) {
    return std::nullopt;
  }
  return container;
}

std::vector<HeartBeatPs1ContainerLayout> findHeartBeatPs1Containers(ByteReader reader) {
  std::vector<HeartBeatPs1ContainerLayout> layouts;
  for (u64 offset = 0; offset + kContainerHeaderSize <= reader.size(); ++offset) {
    if (offset > std::numeric_limits<u32>::max()) {
      break;
    }
    if (auto layout = readHeartBeatPs1Container(reader, static_cast<u32>(offset))) {
      const u32 length = layout->length;
      layouts.push_back(std::move(*layout));
      offset += length - 1;
    }
  }
  return layouts;
}

}  // namespace vgmtrans::formats::heartbeat_ps1
