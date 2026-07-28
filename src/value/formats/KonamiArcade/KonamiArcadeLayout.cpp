/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/KonamiArcade/KonamiArcade.h"

#include "value/extractors/MameRomSetExtractor.h"
#include "value/scan/BytePattern.h"

#include <charconv>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace vgmtrans::formats::konami_arcade {

using namespace core;

namespace {

constexpr double kK054539ClockRate = 18'432'000.0;
constexpr auto kMysticSetNmiRate = makeMaskedBytePattern("\x3e\x71\x32\x27\xe2", "x?xxx");
constexpr auto kMysticNmiSkip = makeMaskedBytePattern("\x3e\x03\xa6\xc2\x78\x00\x2c\x36\x01", "x?xxxxxxx");
constexpr auto kGxSetNmiRate = makeMaskedBytePattern("\x13\xfc\x00\x6d\x00\x20\x04\x4e", "xxx?xxxx");
constexpr auto kGxSequenceTable =
    makeMaskedBytePattern("\x20\x7c\x00\x00\x67\xe4\x22\x7c\x00\x10\x23\x44", "xx????xxxxxx");
constexpr auto kGxSampleTables = makeMaskedBytePattern("\x2c\x7c\x00\x00\x5f\xc2\x2c\x76\x60\x00", "xx????xxxx");
constexpr auto kGxDrumTables = makeMaskedBytePattern("\x21\x7c\x00\x00\x39\xb2\x01\x4e\x21\x7c\x00\x00\x65\x84\x00\xe2"
                                                     "\x21\x7c\x00\x00\x66\x9b\x00\xe6",
                                                     "xx????xxxx????xxxx????xx");

[[nodiscard]] Diagnostic warning(std::string message, SourceRange range) {
  return Diagnostic{.severity = Severity::Warning, .message = std::move(message), .range = range};
}

void warn(std::vector<Diagnostic>* diagnostics, std::string message, SourceRange range) {
  if (diagnostics != nullptr) {
    diagnostics->push_back(warning(std::move(message), range));
  }
}

[[nodiscard]] std::optional<u32> parseOffset(std::optional<std::string_view> text) {
  if (!text || text->empty()) {
    return std::nullopt;
  }
  std::string_view value = *text;
  int base = 10;
  if (value.size() > 2 && value[0] == '0' && (value[1] == 'x' || value[1] == 'X')) {
    value.remove_prefix(2);
    base = 16;
  }
  u32 result = 0;
  const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result, base);
  return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size() ? std::optional<u32>{result}
                                                                               : std::nullopt;
}

[[nodiscard]] std::optional<u32> absoluteOffset(SourceRange group, u32 relative, u32 size = 1) {
  if (relative > group.size || size > group.size - relative ||
      group.offset + relative > std::numeric_limits<u32>::max()) {
    return std::nullopt;
  }
  return static_cast<u32>(group.offset + relative);
}

[[nodiscard]] std::optional<u32> findInRange(ByteReader reader, SourceRange range, MaskedBytePattern pattern) {
  if (!pattern.valid() || pattern.size() > range.size) {
    return std::nullopt;
  }
  const u64 end = range.endOffset() - pattern.size();
  for (u64 offset = range.offset; offset <= end; ++offset) {
    if (matchesBytePattern(reader, offset, pattern)) {
      return static_cast<u32>(offset);
    }
  }
  return std::nullopt;
}

[[nodiscard]] double nmiRate(u8 timer, u8 skipCount) {
  return ((38.0 + timer) * (kK054539ClockRate / 384.0 / 14400.0)) / (skipCount + 1.0);
}

[[nodiscard]] std::optional<KonamiArcadeVersion> versionFromSource(const SourceFile& source) {
  const auto value = source.attribute(mame::kMameFormatVersionAttribute);
  if (value == "MysticWarrior") {
    return KonamiArcadeVersion::MysticWarrior;
  }
  if (value == "GX") {
    return KonamiArcadeVersion::Gx;
  }
  return std::nullopt;
}

[[nodiscard]] bool discoverMysticLayout(KonamiArcadeLayout& layout, const SourceSegment& codeSegment, ByteReader reader,
                                        std::vector<Diagnostic>* diagnostics) {
  const auto sequenceTable = parseOffset(codeSegment.attribute("seq_table"));
  const auto sampleTables = parseOffset(codeSegment.attribute("samp_tables"));
  const auto drumSamples = parseOffset(codeSegment.attribute("drum_samp_table"));
  const auto drumTable = parseOffset(codeSegment.attribute("drum_table"));
  if (!sequenceTable || !sampleTables || !drumSamples || !drumTable) {
    warn(diagnostics, "KonamiArcade MysticWarrior ROM definition is missing required table offsets", layout.code);
    return false;
  }

  const auto nmiSkipPattern = findInRange(reader, layout.code, kMysticNmiSkip);
  const auto nmiTimerPattern = findInRange(reader, layout.code, kMysticSetNmiRate);
  if (!nmiSkipPattern || !nmiTimerPattern || !reader.has(*nmiSkipPattern + 1, 1) ||
      !reader.has(*nmiTimerPattern + 1, 1)) {
    warn(diagnostics, "KonamiArcade MysticWarrior interrupt timing code was not found", layout.code);
    return false;
  }

  const auto sequenceAbsolute = absoluteOffset(layout.code, *sequenceTable);
  const auto samplesAbsolute = absoluteOffset(layout.code, *sampleTables, 4);
  const auto drumSamplesAbsolute = absoluteOffset(layout.code, *drumSamples);
  const auto drumTableAbsolute = absoluteOffset(layout.code, *drumTable);
  if (!sequenceAbsolute || !samplesAbsolute || !drumSamplesAbsolute || !drumTableAbsolute) {
    warn(diagnostics, "KonamiArcade MysticWarrior table offset is outside the sound CPU region", layout.code);
    return false;
  }

  layout.sequenceTableOffset = *sequenceAbsolute;
  layout.sampleTablesOffset = *samplesAbsolute;
  layout.drumSampleTableOffset = *drumSamplesAbsolute;
  layout.drumTableOffset = *drumTableAbsolute;
  layout.nmiRateHertz = nmiRate(reader.u8At(*nmiTimerPattern + 1), reader.u8At(*nmiSkipPattern + 1));
  return true;
}

[[nodiscard]] bool discoverGxLayout(KonamiArcadeLayout& layout, const SourceSegment& codeSegment, ByteReader reader,
                                    std::vector<Diagnostic>* diagnostics) {
  std::optional<u32> sequenceRelative = parseOffset(codeSegment.attribute("seq_table"));
  std::optional<u32> sampleTablesRelative = parseOffset(codeSegment.attribute("samp_tables"));
  std::optional<u32> drumSamplesRelative = parseOffset(codeSegment.attribute("drum_samp_table"));
  std::optional<u32> drumTableRelative = parseOffset(codeSegment.attribute("drum_table"));

  if (!sequenceRelative) {
    const auto pattern = findInRange(reader, layout.code, kGxSequenceTable);
    if (pattern && reader.has(*pattern + 2, 4)) {
      const u32 sequenceTableTable = reader.be32(*pattern + 2);
      if (const auto table = absoluteOffset(layout.code, sequenceTableTable, 8)) {
        const u32 playlist = reader.be32(*table + 4);
        if (const auto playlistOffset = absoluteOffset(layout.code, playlist, 4)) {
          sequenceRelative = reader.be32(*playlistOffset);
        }
      }
    }
  }

  if (!sampleTablesRelative) {
    const auto pattern = findInRange(reader, layout.code, kGxSampleTables);
    if (pattern && reader.has(*pattern + 2, 4)) {
      sampleTablesRelative = reader.be32(*pattern + 2);
    }
  }

  if (!drumSamplesRelative || !drumTableRelative) {
    const auto pattern = findInRange(reader, layout.code, kGxDrumTables);
    if (pattern && reader.has(*pattern + 10, 4) && reader.has(*pattern + 18, 4)) {
      drumSamplesRelative = reader.be32(*pattern + 10);
      drumTableRelative = reader.be32(*pattern + 18);
    }
  }

  if (!sequenceRelative || !sampleTablesRelative) {
    warn(diagnostics, "KonamiArcade GX sequence or sample tables could not be located", layout.code);
    return false;
  }
  const auto sequenceAbsolute = absoluteOffset(layout.code, *sequenceRelative);
  const auto samplesAbsolute = absoluteOffset(layout.code, *sampleTablesRelative, 8);
  if (!sequenceAbsolute || !samplesAbsolute) {
    warn(diagnostics, "KonamiArcade GX table offset is outside the sound CPU region", layout.code);
    return false;
  }
  layout.sequenceTableOffset = *sequenceAbsolute;
  layout.sampleTablesOffset = *samplesAbsolute;

  if (drumSamplesRelative && drumTableRelative) {
    layout.drumSampleTableOffset = absoluteOffset(layout.code, *drumSamplesRelative);
    layout.drumTableOffset = absoluteOffset(layout.code, *drumTableRelative);
    if (!layout.drumSampleTableOffset || !layout.drumTableOffset) {
      layout.drumSampleTableOffset.reset();
      layout.drumTableOffset.reset();
      warn(diagnostics, "KonamiArcade GX drum tables point outside the sound CPU region", layout.code);
    }
  }

  u8 timer = 109;
  if (const auto pattern = findInRange(reader, layout.code, kGxSetNmiRate); pattern && reader.has(*pattern + 3, 1)) {
    timer = reader.u8At(*pattern + 3);
  }
  layout.nmiRateHertz = nmiRate(timer, 1);
  return true;
}

void readSampleInfos(KonamiArcadeLayout& layout, ByteReader reader, std::vector<Diagnostic>* diagnostics) {
  const bool mystic = layout.version == KonamiArcadeVersion::MysticWarrior;
  const u32 tableHeaderSize = mystic ? 4 : 8;
  if (!reader.has(layout.sampleTablesOffset, tableHeaderSize)) {
    warn(diagnostics, "KonamiArcade sample table header is truncated", layout.code);
    return;
  }

  const u32 instrumentRelative =
      mystic ? reader.le16(layout.sampleTablesOffset) : reader.be32(layout.sampleTablesOffset);
  const u32 sfxRelative =
      mystic ? reader.le16(layout.sampleTablesOffset + 2) : reader.be32(layout.sampleTablesOffset + 4);
  const auto instrumentBegin = absoluteOffset(layout.code, instrumentRelative);
  const auto instrumentEnd = absoluteOffset(layout.code, sfxRelative, 0);
  if (!instrumentBegin || !instrumentEnd || *instrumentBegin > *instrumentEnd) {
    warn(diagnostics, "KonamiArcade instrument sample-info range is invalid", layout.code);
    return;
  }

  const auto appendRange = [&](u32 begin, u32 end) {
    for (u32 offset = begin; offset < end;) {
      if (end - offset < 9 || !reader.has(offset, 9)) {
        warn(diagnostics, "KonamiArcade sample-info table has a truncated entry", reader.range(offset, end - offset));
        break;
      }
      const u8 flags = reader.u8At(offset + 6);
      auto type = static_cast<KonamiSampleType>(flags & 0x0c);
      if (type != KonamiSampleType::Pcm8 && type != KonamiSampleType::Pcm16 && type != KonamiSampleType::Adpcm) {
        warn(diagnostics, "KonamiArcade sample-info entry uses an unknown codec", reader.range(offset, 9));
        type = KonamiSampleType::Unknown;
      }
      layout.sampleInfos.push_back(KonamiArcadeSampleInfo{
          .loopOffset = static_cast<u32>(reader.u8At(offset)) | (static_cast<u32>(reader.u8At(offset + 1)) << 8) |
                        (static_cast<u32>(reader.u8At(offset + 2)) << 16),
          .startOffset = static_cast<u32>(reader.u8At(offset + 3)) | (static_cast<u32>(reader.u8At(offset + 4)) << 8) |
                         (static_cast<u32>(reader.u8At(offset + 5)) << 16),
          .type = type,
          .reverse = (flags & 0x20) != 0,
          .loops = reader.u8At(offset + 7) != 0,
          .attenuation = reader.u8At(offset + 8),
          .range = reader.range(offset, 9),
      });
      offset += 9;
    }
  };
  appendRange(*instrumentBegin, *instrumentEnd);
  layout.melodicSampleCount = static_cast<u32>(layout.sampleInfos.size());

  if (layout.drumSampleTableOffset && layout.drumTableOffset &&
      *layout.drumSampleTableOffset <= *layout.drumTableOffset) {
    appendRange(*layout.drumSampleTableOffset, *layout.drumTableOffset);
  }
}

void readDrums(KonamiArcadeLayout& layout, ByteReader reader, std::vector<Diagnostic>* diagnostics) {
  if (!layout.drumTableOffset) {
    return;
  }
  for (u32 index = 0; index < layout.drums.size(); ++index) {
    const u64 offset64 = static_cast<u64>(*layout.drumTableOffset) + static_cast<u64>(index) * 8;
    if (offset64 > std::numeric_limits<u32>::max() || !reader.has(offset64, 8)) {
      warn(diagnostics, "KonamiArcade drum table is truncated", reader.range(*layout.drumTableOffset, 0));
      break;
    }
    const u32 offset = static_cast<u32>(offset64);
    const u8 unityKey = reader.u8At(offset + 1);
    if (unityKey >= 0x60) {
      break;
    }
    layout.drums[index] = KonamiArcadeDrum{
        .sample = reader.u8At(offset),
        .unityKey = unityKey,
        .pitch = reader.u8At(offset + 2),
        .pan = reader.u8At(offset + 3),
        .defaultDuration = reader.u8At(offset + 6),
        .attenuation = reader.u8At(offset + 7),
        .range = reader.range(offset, 8),
    };
    ++layout.drumCount;
  }
}

struct ParsedTrackTable {
  SourceRange range;
  std::vector<KonamiArcadeTrackLayout> tracks;
};

[[nodiscard]] std::optional<ParsedTrackTable> readTrackTable(const KonamiArcadeLayout& layout, ByteReader reader,
                                                             u32 sequenceOffset, u32 memoryBase = 0) {
  const bool mystic = layout.version == KonamiArcadeVersion::MysticWarrior;
  const u32 pointerSize = mystic ? 2 : 4;
  u32 trackCount = kKonamiArcadeMaxTracks;
  if (mystic) {
    for (u32 track = 8; track < kKonamiArcadeMaxTracks; ++track) {
      const u32 pointer = sequenceOffset + track * pointerSize;
      if (!reader.has(pointer, pointerSize) || reader.le16(pointer) == 0) {
        trackCount = 8;
        break;
      }
    }
  }

  const u32 tableSize = trackCount * pointerSize;
  if (!reader.has(sequenceOffset, tableSize)) {
    return std::nullopt;
  }

  ParsedTrackTable result{
      .range = reader.range(sequenceOffset, tableSize),
  };
  for (u32 track = 0; track < trackCount; ++track) {
    const u32 pointer = sequenceOffset + track * pointerSize;
    const u32 encoded = mystic ? reader.le16(pointer) : reader.be32(pointer);
    if (encoded == 0) {
      if (track == 0) {
        return std::nullopt;
      }
      continue;
    }

    const s64 resolved = mystic ? static_cast<s64>(sequenceOffset) + encoded - memoryBase
                                : static_cast<s64>(layout.code.offset) + encoded;
    if (resolved < static_cast<s64>(layout.code.offset) || resolved >= static_cast<s64>(layout.code.endOffset()) ||
        !reader.has(static_cast<u64>(resolved), 1)) {
      return std::nullopt;
    }

    result.tracks.push_back(KonamiArcadeTrackLayout{
        .number = track,
        .encodedAddress = encoded,
        .offset = static_cast<u32>(resolved),
        .pointer = reader.range(pointer, pointerSize),
    });
  }
  if (result.tracks.empty()) {
    return std::nullopt;
  }
  return result;
}

void readSequences(KonamiArcadeLayout& layout, ByteReader reader, std::vector<Diagnostic>* diagnostics) {
  const u32 codeEnd = static_cast<u32>(layout.code.endOffset());
  u32 offset = layout.sequenceTableOffset;
  if (layout.version == KonamiArcadeVersion::Gx) {
    u32 entrySize = 12;
    if (reader.has(offset + 12, 4)) {
      const u32 candidate = reader.be32(offset + 12);
      if (candidate > 0x1000 && candidate < 0x20000) {
        entrySize = 16;
      }
    }
    for (u32 index = 0; offset < codeEnd && index < 1024; ++index, offset += entrySize) {
      if (!reader.has(offset, 12)) {
        break;
      }
      const u32 unknown = reader.be32(offset);
      const u32 sequenceRelative = reader.be32(offset + 8);
      const auto sequenceOffset = absoluteOffset(layout.code, sequenceRelative);
      if (unknown >= 0x5000 || sequenceRelative == 0 || !sequenceOffset) {
        break;
      }
      auto trackTable = readTrackTable(layout, reader, *sequenceOffset);
      if (!trackTable) {
        continue;
      }
      layout.sequences.push_back(KonamiArcadeSequenceLayout{
          .index = index,
          .offset = *sequenceOffset,
          .initialAttenuation = reader.s8At(offset + 3),
          .initialTranspose = reader.s8At(offset + 4),
          .tempoOffset = reader.s8At(offset + 5),
          .tableEntry = reader.range(offset, 12),
          .trackTable = trackTable->range,
          .tracks = std::move(trackTable->tracks),
          .name = layout.game + " " + std::to_string(index),
      });
    }
  } else {
    for (u32 index = 0; offset < codeEnd && index < 1024; ++index, offset += 14) {
      if (!reader.has(offset, 14) || reader.le16(offset) != 0) {
        break;
      }
      const u8 bank = reader.u8At(offset + 7);
      const u16 destination =
          static_cast<u16>(reader.u8At(offset + 8) | (static_cast<u16>(reader.u8At(offset + 9)) << 8));
      const s64 signedRelative = static_cast<s64>(bank) * 0x400 + static_cast<s64>(destination) - 0x8000;
      if (signedRelative <= 0 || signedRelative > std::numeric_limits<u32>::max()) {
        break;
      }
      const u32 sequenceRelative = static_cast<u32>(signedRelative);
      const auto sequenceOffset = absoluteOffset(layout.code, sequenceRelative);
      if (sequenceRelative == 0 || !sequenceOffset) {
        break;
      }
      auto trackTable = readTrackTable(layout, reader, *sequenceOffset, destination);
      if (!trackTable) {
        continue;
      }
      const u16 indexedNoteTable = reader.le16(offset + 10);
      layout.sequences.push_back(KonamiArcadeSequenceLayout{
          .index = index,
          .offset = *sequenceOffset,
          .memoryBase = destination,
          .indexedNoteTableOffset =
              indexedNoteTable == 0 ? 0 : absoluteOffset(layout.code, indexedNoteTable).value_or(0),
          // The Z80 loader copies these three table bytes into each channel
          // as tempo, loudness, and transpose offsets, respectively.
          .initialAttenuation = reader.s8At(offset + 4),
          .initialTranspose = reader.s8At(offset + 5),
          .tempoOffset = reader.s8At(offset + 3),
          .tableEntry = reader.range(offset, 14),
          .trackTable = trackTable->range,
          .tracks = std::move(trackTable->tracks),
          .name = layout.game + " " + std::to_string(index),
      });
    }
  }
  if (layout.sequences.empty()) {
    warn(diagnostics, "KonamiArcade sequence table did not contain a valid sequence", layout.code);
  }
}

}  // namespace

const char* konamiArcadeVersionName(KonamiArcadeVersion version) {
  switch (version) {
    case KonamiArcadeVersion::MysticWarrior:
      return "MysticWarrior";
    case KonamiArcadeVersion::Gx:
      return "GX";
  }
  return "Unknown";
}

std::optional<KonamiArcadeLayout> findKonamiArcadeLayout(const SourceFile& source, ByteReader reader,
                                                         std::vector<Diagnostic>* diagnostics) {
  if (source.attribute(mame::kMameFormatAttribute) != kKonamiArcadeFormatName) {
    return std::nullopt;
  }
  const auto version = versionFromSource(source);
  const auto code = source.segmentRange("soundcpu");
  const auto sound = source.segmentRange("sound");
  const auto* codeSegment = source.segment("soundcpu");
  if (!version || !code || !sound || codeSegment == nullptr) {
    warn(diagnostics, "KonamiArcade ROM set is missing version, soundcpu, or sound metadata",
         reader.range(0, reader.size()));
    return std::nullopt;
  }

  KonamiArcadeLayout layout{
      .version = *version,
      .game = std::string(source.attribute(mame::kMameGameAttribute).value_or(source.name)),
      .code = *code,
      .sound = *sound,
  };
  if (layout.code.endOffset() > reader.size() || layout.sound.endOffset() > reader.size() ||
      layout.code.endOffset() > std::numeric_limits<u32>::max() ||
      layout.sound.endOffset() > std::numeric_limits<u32>::max()) {
    warn(diagnostics, "KonamiArcade ROM regions are outside the assembled source", reader.range(0, reader.size()));
    return std::nullopt;
  }

  const bool discovered = layout.version == KonamiArcadeVersion::MysticWarrior
                              ? discoverMysticLayout(layout, *codeSegment, reader, diagnostics)
                              : discoverGxLayout(layout, *codeSegment, reader, diagnostics);
  if (!discovered) {
    return std::nullopt;
  }

  readSampleInfos(layout, reader, diagnostics);
  readDrums(layout, reader, diagnostics);
  readSequences(layout, reader, diagnostics);
  if (layout.sampleInfos.empty()) {
    warn(diagnostics, "KonamiArcade sample tables did not contain valid entries", layout.code);
  }
  return layout;
}

}  // namespace vgmtrans::formats::konami_arcade
