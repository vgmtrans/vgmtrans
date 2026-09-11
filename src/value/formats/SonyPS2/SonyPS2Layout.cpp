/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/SonyPS2/SonyPS2.h"

#include <algorithm>
#include <array>
#include <limits>

namespace vgmtrans::formats::sony_ps2 {

using namespace core;

namespace {

constexpr std::array<u8, 8> kVersionTag{'I', 'E', 'C', 'S', 's', 'r', 'e', 'V'};
constexpr std::array<u8, 8> kSequenceTag{'I', 'E', 'C', 'S', 'u', 'q', 'e', 'S'};
constexpr std::array<u8, 8> kHeaderTag{'I', 'E', 'C', 'S', 'd', 'a', 'e', 'H'};
constexpr std::array<u8, 8> kMidiTag{'I', 'E', 'C', 'S', 'i', 'd', 'i', 'M'};
constexpr std::array<u8, 8> kSongTag{'I', 'E', 'C', 'S', 'g', 'n', 'o', 'S'};
constexpr std::array<u8, 8> kSeSequenceTag{'I', 'E', 'C', 'S', 'q', 'e', 'S', 'S'};
constexpr std::array<u8, 8> kSeSongTag{'I', 'E', 'C', 'S', 'g', 'n', 'S', 'S'};

[[nodiscard]] bool tagAt(ByteReader reader, u32 offset, const std::array<u8, 8>& tag) {
  return reader.has(offset, tag.size()) && std::ranges::equal(tag, reader.slice(offset, tag.size()));
}

[[nodiscard]] std::optional<SparseChunkLayout> readSparseChunk(ByteReader reader, u32 fileOffset, u32 fileEnd,
                                                               u32 declaredFileSize, u32 relativeOffset,
                                                               const std::array<u8, 8>& tag) {
  if (relativeOffset == 0xffffffff || relativeOffset > declaredFileSize || relativeOffset > fileEnd - fileOffset) {
    return std::nullopt;
  }
  const u32 offset = fileOffset + relativeOffset;
  if (!tagAt(reader, offset, tag) || !reader.has(offset, 16)) {
    return std::nullopt;
  }
  const u32 declaredSize = reader.le32(offset + 8);
  if (declaredSize < 16 || declaredSize > declaredFileSize - relativeOffset) {
    return std::nullopt;
  }
  // The Sony driver validates tags and offset tables but never consults these
  // size fields. Shipped SQs can retain an oversized file/chunk length after a
  // MIDI payload was shortened, so parse the physically available prefix.
  const u32 availableSize = std::min(declaredSize, fileEnd - offset);
  const u32 maximumIndex = reader.le32(offset + 12);
  if (maximumIndex > 65535 || static_cast<u64>(maximumIndex + 1) * 4 > availableSize - 16) {
    return std::nullopt;
  }
  SparseChunkLayout chunk{.offset = offset, .size = availableSize};
  chunk.entries.reserve(maximumIndex + 1);
  for (u32 index = 0; index <= maximumIndex; ++index) {
    const u32 relative = reader.le32(offset + 16 + index * 4);
    if (relative == 0xffffffff) {
      chunk.entries.push_back(std::nullopt);
    } else if (relative < availableSize) {
      chunk.entries.push_back(offset + relative);
    } else {
      return std::nullopt;
    }
  }
  return chunk;
}

[[nodiscard]] u32 entryEnd(const SparseChunkLayout& chunk, u32 entry) {
  u32 end = chunk.offset + chunk.size;
  for (const auto candidate : chunk.entries) {
    if (candidate && *candidate > entry) {
      end = std::min(end, *candidate);
    }
  }
  return end;
}

}  // namespace

std::optional<SequenceLayout> readSequenceLayout(ByteReader reader, u32 offset) {
  if (!tagAt(reader, offset, kVersionTag) || !reader.has(offset, 0x30) || reader.le32(offset + 8) != 16 ||
      !tagAt(reader, offset + 16, kSequenceTag)) {
    return std::nullopt;
  }
  const u32 headerSize = reader.le32(offset + 24);
  const u32 fileSize = reader.le32(offset + 0x1c);
  if (headerSize < 0x20 || fileSize < 0x30 || fileSize > std::numeric_limits<u32>::max() - offset) {
    return std::nullopt;
  }
  const u32 availableFileSize = static_cast<u32>(std::min<u64>(fileSize, reader.size() - offset));
  const u32 fileEnd = offset + availableFileSize;
  SequenceLayout layout{
      .offset = offset,
      .length = availableFileSize,
      .majorVersion = reader.u8At(offset + 14),
      .minorVersion = reader.u8At(offset + 15),
  };
  layout.songs = readSparseChunk(reader, offset, fileEnd, fileSize, reader.le32(offset + 0x20), kSongTag);
  layout.midi = readSparseChunk(reader, offset, fileEnd, fileSize, reader.le32(offset + 0x24), kMidiTag);
  const u32 seRelative = reader.le32(offset + 0x28);
  if (seRelative != 0xffffffff && seRelative <= fileEnd - offset) {
    const u32 seOffset = offset + seRelative;
    if (tagAt(reader, seOffset, kSeSequenceTag) && reader.has(seOffset, 28)) {
      const u32 size = reader.le32(seOffset + 8);
      const u32 maximum = reader.le32(seOffset + 12);
      const u32 tableRelative = reader.le32(seOffset + 16);
      if (size >= 28 && size <= fileEnd - seOffset && maximum < 128 && tableRelative < size &&
          reader.has(seOffset + tableRelative, static_cast<u64>(maximum + 1) * 4)) {
        SparseChunkLayout chunk{.offset = seOffset, .size = size};
        for (u32 index = 0; index <= maximum; ++index) {
          const u32 relative = reader.le32(seOffset + tableRelative + index * 4);
          chunk.entries.push_back(relative == 0xffffffff || relative >= size ? std::nullopt
                                                                             : std::optional{seOffset + relative});
        }
        layout.seSequences = std::move(chunk);
      }
    }
  }
  layout.seSongs = readSparseChunk(reader, offset, fileEnd, fileSize, reader.le32(offset + 0x2c), kSeSongTag);
  if (!layout.midi && !layout.songs && !layout.seSequences && !layout.seSongs) {
    return std::nullopt;
  }

  if (layout.midi) {
    for (u32 index = 0; index < layout.midi->entries.size(); ++index) {
      const auto entry = layout.midi->entries[index];
      if (!entry || !reader.has(*entry, 6)) {
        continue;
      }
      const u32 relativeData = reader.le32(*entry);
      if (relativeData < 6 || relativeData > entryEnd(*layout.midi, *entry) - *entry) {
        continue;
      }
      MidiBlockLayout block{
          .index = index,
          .offset = *entry,
          .dataOffset = *entry + relativeData,
          .dataEnd = entryEnd(*layout.midi, *entry),
          .ppqn = reader.le16(*entry + 4),
      };
      if (relativeData != 6) {
        if (relativeData < 10) {
          continue;
        }
        block.compression = reader.le16(*entry + 6);
        const u16 dictionaryBytes = reader.le16(*entry + 8);
        if (block.compression != 1 || (dictionaryBytes & 1) != 0 || 10u + dictionaryBytes > relativeData) {
          continue;
        }
        const auto dictionary = reader.slice(*entry + 10, dictionaryBytes);
        block.noteDictionary.assign(dictionary.begin(), dictionary.end());
      }
      if (block.ppqn == 0) {
        block.ppqn = 480;
      }
      layout.midiBlocks.push_back(std::move(block));
    }
  }

  if (layout.seSequences) {
    const u8 masterVolume = reader.u8At(layout.seSequences->offset + 20);
    const s8 masterPan = static_cast<s8>(reader.u8At(layout.seSequences->offset + 21));
    const u16 masterScale = reader.le16(layout.seSequences->offset + 22);
    for (u32 set = 0; set < layout.seSequences->entries.size(); ++set) {
      const auto setAddress = layout.seSequences->entries[set];
      if (!setAddress || !reader.has(*setAddress, 16)) {
        continue;
      }
      const u32 maximumSequence = reader.le32(*setAddress);
      const u32 tableRelative = reader.le32(*setAddress + 4);
      const u8 setVolume = reader.u8At(*setAddress + 8);
      const s8 setPan = static_cast<s8>(reader.u8At(*setAddress + 9));
      const u16 setScale = reader.le16(*setAddress + 10);
      if (maximumSequence >= 128 || tableRelative >= layout.seSequences->size ||
          !reader.has(layout.seSequences->offset + tableRelative, static_cast<u64>(maximumSequence + 1) * 4)) {
        continue;
      }
      for (u32 sequence = 0; sequence <= maximumSequence; ++sequence) {
        const u32 relative = reader.le32(layout.seSequences->offset + tableRelative + sequence * 4);
        if (relative == 0xffffffff || relative >= layout.seSequences->size) {
          continue;
        }
        const u32 sequenceOffset = layout.seSequences->offset + relative;
        if (!reader.has(sequenceOffset, 16)) {
          continue;
        }
        const u32 dataRelative = reader.le32(sequenceOffset);
        const u32 dataSize = reader.le32(sequenceOffset + 8);
        const u64 dataStart = static_cast<u64>(sequenceOffset) + dataRelative;
        const u64 chunkEnd = static_cast<u64>(layout.seSequences->offset) + layout.seSequences->size;
        if (dataRelative < 16 || dataStart > chunkEnd || dataSize > chunkEnd - dataStart) {
          continue;
        }
        const u8 sequenceVolume = reader.u8At(sequenceOffset + 4);
        const s8 sequencePan = static_cast<s8>(reader.u8At(sequenceOffset + 5));
        const u16 sequenceScale = reader.le16(sequenceOffset + 6);
        const u64 combinedScale = static_cast<u64>(masterScale) * setScale * sequenceScale;
        const int combinedPan = std::clamp(std::abs(static_cast<int>(masterPan)) + std::abs(static_cast<int>(setPan)) +
                                               std::abs(static_cast<int>(sequencePan)) - 128,
                                           0, 127);
        layout.seSequenceBlocks.push_back(SeSequenceLayout{
            .offset = sequenceOffset,
            .dataOffset = sequenceOffset + dataRelative,
            .dataEnd = sequenceOffset + dataRelative + dataSize,
            .ppqn = 1000,
            .set = static_cast<u8>(set),
            .sequence = static_cast<u8>(sequence),
            .volume = static_cast<u8>(
                std::min<u32>(128, static_cast<u32>(masterVolume) * setVolume * sequenceVolume / (128 * 128))),
            .pan = static_cast<s8>(combinedPan),
            .timeScale = static_cast<u16>(std::clamp<u64>(combinedScale / 1000000, 1, 65535)),
        });
      }
    }
  }
  return layout;
}

std::vector<SequenceLayout> findSequenceLayouts(ByteReader reader) {
  std::vector<SequenceLayout> layouts;
  for (u64 offset = 0; offset + 0x30 <= reader.size(); ++offset) {
    if (offset > std::numeric_limits<u32>::max() || !tagAt(reader, static_cast<u32>(offset), kVersionTag)) {
      continue;
    }
    if (auto layout = readSequenceLayout(reader, static_cast<u32>(offset))) {
      const u32 length = layout->length;
      layouts.push_back(std::move(*layout));
      offset += length - 1;
    }
  }
  return layouts;
}

std::optional<SoundBankData> readSoundBankLayout(ByteReader reader, u32 offset) {
  if (!tagAt(reader, offset, kVersionTag) || !reader.has(offset, 0x40) || reader.le32(offset + 8) != 16 ||
      !tagAt(reader, offset + 16, kHeaderTag) || reader.le32(offset + 24) < 0x40) {
    return std::nullopt;
  }
  const u32 headerBytes = reader.le32(offset + 0x1c);
  if (headerBytes < 0x40 || headerBytes > std::numeric_limits<u32>::max() - offset ||
      !reader.has(offset, headerBytes)) {
    return std::nullopt;
  }
  const u32 end = offset + headerBytes;
  const auto vagi = readSparseChunk(reader, offset, end, headerBytes, reader.le32(offset + 0x30),
                                    std::array<u8, 8>{'I', 'E', 'C', 'S', 'i', 'g', 'a', 'V'});
  if (!vagi) {
    return std::nullopt;
  }
  SoundBankData data{.expectedBodyBytes = reader.le32(offset + 0x20)};
  data.vags.resize(vagi->entries.size());
  for (u32 index = 0; index < vagi->entries.size(); ++index) {
    const auto entry = vagi->entries[index];
    if (!entry || !reader.has(*entry, 8)) {
      continue;
    }
    const u16 rate = reader.le16(*entry + 4);
    data.vags[index] = VagInfo{
        .bodyOffset = reader.le32(*entry),
        .sampleRate = rate == 0 ? u16{48000} : rate,
        .loops = (reader.u8At(*entry + 6) & 1) != 0,
    };
  }
  return data;
}

}  // namespace vgmtrans::formats::sony_ps2
