/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/KonamiTMNT2/KonamiTMNT2.h"

#include "value/extractors/MameRomSetExtractor.h"
#include "value/scan/BytePattern.h"

#include <algorithm>
#include <charconv>
#include <compare>
#include <limits>
#include <map>
#include <set>
#include <utility>

namespace vgmtrans::formats::konami_tmnt2 {

using namespace core;

namespace {

constexpr auto kSequenceTable = makeMaskedBytePattern("\x21\x00\x00\xe6\x7f\x07\x5f\x19", "x??xxxxx");
constexpr auto kSampleInstrumentTable = makeMaskedBytePattern("\x13\x1a\x21\x00\x00\x07\x4f\x09\x4e", "xxx??xxxx");
constexpr auto kDrumTable =
    makeMaskedBytePattern("\x4f\x06\x00\xdd\x7e\x00\x07\x5f\x50\x21\x00\x00\x19", "xxxxx?xxxx??x");
constexpr auto kYmTableTmnt2 =
    makeMaskedBytePattern("\x13\x1a\xd9\xcb\x7f\xca\x00\x00\x21\x00\x00\xe6\x7f", "xxxxxx??x??xx");
constexpr auto kYmTableEarly = makeMaskedBytePattern("\x44\x4d\x21\x00\x00\x09\x4e\x23\x46", "xxx??xxxx");

[[nodiscard]] Diagnostic warning(std::string message, SourceRange range) {
  return Diagnostic{.severity = Severity::Warning, .message = std::move(message), .range = range};
}

void warn(std::vector<Diagnostic>* diagnostics, std::string message, SourceRange range) {
  if (diagnostics != nullptr) {
    diagnostics->push_back(warning(std::move(message), range));
  }
}

[[nodiscard]] std::optional<u32> integer(std::optional<std::string_view> text) {
  if (!text) {
    return std::nullopt;
  }
  int base = 10;
  if (text->starts_with("0x") || text->starts_with("0X")) {
    text->remove_prefix(2);
    base = 16;
  }
  u32 result = 0;
  const auto parsed = std::from_chars(text->data(), text->data() + text->size(), result, base);
  return parsed.ec == std::errc{} && parsed.ptr == text->data() + text->size() ? std::optional<u32>{result}
                                                                               : std::nullopt;
}

[[nodiscard]] std::optional<u32> address(const SourceSegment& segment, std::string_view attribute) {
  const auto relative = integer(segment.attribute(attribute));
  if (!relative || *relative > segment.size || segment.offset + *relative > std::numeric_limits<u32>::max()) {
    return std::nullopt;
  }
  return static_cast<u32>(segment.offset + *relative);
}

[[nodiscard]] std::optional<u32> find(ByteReader reader, SourceRange range, MaskedBytePattern pattern) {
  if (!pattern.valid() || pattern.size() > range.size) {
    return std::nullopt;
  }
  const u64 last = range.endOffset() - pattern.size();
  for (u64 offset = range.offset; offset <= last; ++offset) {
    if (matchesBytePattern(reader, offset, pattern)) {
      return static_cast<u32>(offset);
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<u32> patternPointer(ByteReader reader, SourceRange range, MaskedBytePattern pattern,
                                                u32 operandOffset) {
  const auto match = find(reader, range, pattern);
  if (!match || !reader.has(*match + operandOffset, 2)) {
    return std::nullopt;
  }
  return static_cast<u32>(range.offset + reader.le16(*match + operandOffset));
}

[[nodiscard]] Version sourceVersion(const SourceFile& source) {
  const auto version = source.attribute(mame::kMameFormatVersionAttribute);
  if (version == "vendetta") {
    return Version::Vendetta;
  }
  if (version == "ssriders") {
    return Version::SunsetRiders;
  }
  if (source.attribute(mame::kMameGameAttribute) == "blswhstl") {
    return Version::BellsWhistles;
  }
  return Version::Tmnt2;
}

[[nodiscard]] std::pair<u32, u32> trackCounts(Version version, u8 sequenceType) {
  if (version == Version::Vendetta) {
    return {8, 4};
  }
  if (version == Version::SunsetRiders) {
    switch (sequenceType) {
      case 0:
        return {6, 2};
      case 1:
        return {7, 3};
      case 2:
        return {8, 3};
      default:
        return {8, 4};
    }
  }
  return sequenceType == 0 ? std::pair{8u, 4u} : std::pair{6u, 3u};
}

void readSequences(Layout& layout, ByteReader reader, std::vector<Diagnostic>* diagnostics) {
  std::map<u16, SourceRange> pointers;
  std::vector<std::pair<u16, SourceRange>> entries;
  u32 cursor = layout.sequenceTableOffset;
  u32 firstSequence = static_cast<u32>(layout.program.endOffset());
  while (cursor + 2 <= firstSequence && reader.has(cursor, 2)) {
    const u16 pointer = reader.le16(cursor);
    if (pointer == 0 || pointer >= layout.program.size) {
      break;
    }
    if (layout.version == Version::Vendetta && pointers.contains(pointer)) {
      break;
    }
    const SourceRange range = reader.range(cursor, 2);
    pointers.try_emplace(pointer, range);
    entries.emplace_back(pointer, range);
    firstSequence = std::min(firstSequence, static_cast<u32>(layout.program.offset + pointer));
    cursor += 2;
  }
  layout.sequenceTable = reader.range(layout.sequenceTableOffset, cursor - layout.sequenceTableOffset);

  std::map<u16, u32> sequenceIndices;
  u32 index = 0;
  for (const auto& [pointer, tableEntry] : pointers) {
    sequenceIndices.emplace(pointer, index);
    const u32 sequenceOffset = static_cast<u32>(layout.program.offset + pointer);
    if (!reader.has(sequenceOffset, layout.version == Version::Vendetta ? 24 : 1)) {
      warn(diagnostics, "KonamiTMNT2 sequence pointer is outside the sound CPU ROM", tableEntry);
      ++index;
      continue;
    }

    const u32 typeSize = layout.version == Version::Vendetta ? 0 : 1;
    const u8 type = typeSize != 0 ? reader.u8At(sequenceOffset) : 0;
    const auto [ymTracks, sampleTracks] = trackCounts(layout.version, type);
    const u32 totalTracks = ymTracks + sampleTracks;
    if (!reader.has(sequenceOffset + typeSize, totalTracks * 2)) {
      warn(diagnostics, "KonamiTMNT2 sequence track table is truncated", reader.range(sequenceOffset, typeSize));
      ++index;
      continue;
    }

    SequenceLayout sequence{
        .index = index,
        .tableEntry = tableEntry,
        .trackTable = reader.range(sequenceOffset, typeSize + totalTracks * 2),
        .ymTrackCount = ymTracks,
        .totalTrackCount = totalTracks,
        .name = layout.game + " seq " + std::to_string(index),
    };
    for (u32 track = 0; track < totalTracks; ++track) {
      const u32 entry = sequenceOffset + typeSize + track * 2;
      const u16 encoded = reader.le16(entry);
      const u32 offset = static_cast<u32>(layout.program.offset + encoded);
      if (encoded == 0 || !reader.has(offset, 1)) {
        continue;
      }
      sequence.tracks.push_back(TrackLayout{
          .number = track,
          .chip = track < ymTracks ? TrackChip::Ym2151 : TrackChip::K053260,
          .offset = offset,
          .pointer = reader.range(entry, 2),
      });
    }
    if (!sequence.tracks.empty()) {
      layout.sequences.push_back(std::move(sequence));
    }
    ++index;
  }

  layout.sequencePointers.reserve(entries.size());
  for (u32 slot = 0; slot < entries.size(); ++slot) {
    const auto& [encoded, range] = entries[slot];
    layout.sequencePointers.push_back(SequencePointerLayout{
        .slot = slot,
        .encoded = encoded,
        .sequenceIndex = sequenceIndices.at(encoded),
        .range = range,
    });
  }
}

struct SampleKey {
  u32 start;
  u32 length;
  u32 loopStart;
  bool adpcm;
  bool reverse;
  bool loops;

  auto operator<=>(const SampleKey&) const = default;
};

[[nodiscard]] SampleKey sampleKey(const SampleInfo& info) {
  return SampleKey{info.start, info.length, info.loopStart, info.adpcm, info.reverse, info.loops};
}

[[nodiscard]] u32 readLe24(ByteReader reader, u32 offset) {
  return static_cast<u32>(reader.u8At(offset)) | (static_cast<u32>(reader.u8At(offset + 1)) << 8) |
         (static_cast<u32>(reader.u8At(offset + 2)) << 16);
}

[[nodiscard]] std::vector<u32> pointerTable(ByteReader reader, const Layout& layout, u32 table,
                                            u32 hardEnd = std::numeric_limits<u32>::max()) {
  std::vector<u32> result;
  u32 cursor = table;
  u32 firstData = std::min<u32>(hardEnd, static_cast<u32>(layout.program.endOffset()));
  while (cursor + 2 <= firstData && reader.has(cursor, 2)) {
    const u16 encoded = reader.le16(cursor);
    const u32 pointer = static_cast<u32>(layout.program.offset + encoded);
    if (encoded == 0 || pointer < layout.program.offset || pointer >= layout.program.endOffset()) {
      break;
    }
    result.push_back(pointer);
    firstData = std::min(firstData, pointer);
    cursor += 2;
  }
  return result;
}

[[nodiscard]] u32 addSample(Layout& layout, SampleInfo info, std::map<SampleKey, u32>& samples) {
  const SampleKey key = sampleKey(info);
  if (const auto found = samples.find(key); found != samples.end()) {
    return found->second;
  }
  const u32 index = static_cast<u32>(layout.sampleInfos.size());
  info.sampleIndex = index;
  layout.sampleInfos.push_back(info);
  samples.emplace(key, index);
  return index;
}

void readTmnt2Synth(Layout& layout, ByteReader reader, std::vector<Diagnostic>* diagnostics) {
  layout.ym2151Patches = pointerTable(reader, layout, layout.ym2151TableOffset);
  const auto instrumentPointers = pointerTable(reader, layout, layout.k053260TableOffset, layout.drumTableOffset);
  std::map<SampleKey, u32> samples;

  for (const u32 offset : instrumentPointers) {
    if (!reader.has(offset, 10)) {
      warn(diagnostics, "KonamiTMNT2 K053260 instrument is truncated", reader.range(offset, 0));
      continue;
    }
    const u8 flags = reader.u8At(offset);
    const bool splitLoop = (flags & 3) == 2;
    const u32 size = splitLoop ? 12 : 10;
    if (!reader.has(offset, size)) {
      continue;
    }
    const u32 loopStart = splitLoop ? reader.le16(offset + 1) : 0;
    const u32 length = reader.le16(offset + (splitLoop ? 3 : 1));
    const u32 startField = offset + (splitLoop ? 5 : 3);
    const u32 start = readLe24(reader, startField);
    const u32 common = offset + (splitLoop ? 8 : 6);
    const SampleInfo info{
        .start = start,
        .length = length,
        .loopStart = loopStart,
        .adpcm = (flags & 0x10) != 0,
        .reverse = (flags & 0x08) != 0,
        .loops = (flags & 3) != 0,
        .range = reader.range(offset, size),
    };
    const bool validSample = info.length != 0 && info.fitsIn(layout.sound.size);
    const u32 sample = validSample ? addSample(layout, info, samples) : kInvalidSampleIndex;
    layout.sampleInstruments.push_back(SampleInstrument{
        .volume = reader.u8At(common),
        .gate = reader.u8At(common + 1),
        .release = reader.u8At(common + 2),
        .pan = reader.u8At(common + 3),
        .range = reader.range(offset, size),
        .sampleIndex = sample,
    });
  }

  const auto bankPointers = pointerTable(reader, layout, layout.drumTableOffset);
  const std::set<u32> bankPointerSet(bankPointers.begin(), bankPointers.end());
  std::set<u32> drumPointerSet;
  for (u32 bank = 0; bank < bankPointers.size(); ++bank) {
    std::vector<Drum> drums;
    const u32 table = bankPointers[bank];
    u32 firstDrum = static_cast<u32>(layout.program.endOffset());
    for (u32 slot = 0; slot < 13; ++slot) {
      const u32 pointerOffset = table + slot * 2;
      if (!reader.has(pointerOffset, 2) || pointerOffset >= firstDrum ||
          (slot != 0 && (bankPointerSet.contains(pointerOffset) || drumPointerSet.contains(pointerOffset)))) {
        break;
      }
      const u32 offset = static_cast<u32>(layout.program.offset + reader.le16(pointerOffset));
      if (offset > pointerOffset + 0x1000 || !reader.has(offset, 14)) {
        break;
      }
      firstDrum = std::min(firstDrum, offset);
      drumPointerSet.insert(offset);
      const u8 flags = reader.u8At(offset + 3);
      const u32 start = readLe24(reader, offset + 6);
      const SampleInfo info{
          .start = start,
          .length = reader.le16(offset + 4),
          .adpcm = (flags & 0x10) != 0,
          .reverse = (flags & 0x08) != 0,
          .loops = (flags & 3) != 0,
          .range = reader.range(offset, 14),
      };
      const bool validSample = info.length != 0 && info.fitsIn(layout.sound.size);
      const u32 sample = validSample ? addSample(layout, info, samples) : kInvalidSampleIndex;
      drums.push_back(Drum{
          .valid = validSample,
          .bank = static_cast<u8>(bank),
          .slot = static_cast<u8>(slot),
          .volume = reader.u8At(offset + 9),
          .release = reader.u8At(offset + 12),
          .pan = reader.u8At(offset + 13),
          .pitch = reader.le16(offset),
          .gate = reader.le16(offset + 10),
          .range = reader.range(offset, 14),
          .sampleIndex = sample,
      });
    }
    layout.drumBanks.push_back(std::move(drums));
  }
}

struct VendettaDrumRecord {
  u32 code = 0;
  u8 sample = 0;
  u8 attenuation = 0;
  u8 release = 0;
  u8 pan = 0;
  std::optional<u16> pitch;
  SourceRange range;
};

[[nodiscard]] std::vector<VendettaDrumRecord> readVendettaDrumRecords(ByteReader reader, const Layout& layout,
                                                                      u32 begin, u32 end, u32 loadInstrument,
                                                                      u32 setPan, u32 setPitch) {
  std::vector<VendettaDrumRecord> result;
  for (u32 offset = begin; offset + 3 <= end && reader.has(offset, 3);) {
    const u32 recordBegin = offset;
    VendettaDrumRecord drum{
        .code = offset + 3,
        .sample = reader.u8At(offset),
        .attenuation = reader.u8At(offset + 1),
        .release = reader.u8At(offset + 2),
    };
    offset += 3;
    u16 hl = 0;
    u8 a = 0;
    bool ended = false;
    for (u32 instructions = 0; instructions < 64 && offset < end && reader.has(offset, 1); ++instructions) {
      const u8 opcode = reader.u8At(offset);
      if (opcode == 0x21 && reader.has(offset, 3)) {
        hl = reader.le16(offset + 1);
        offset += 3;
      } else if (opcode == 0x3e && reader.has(offset, 2)) {
        a = reader.u8At(offset + 1);
        offset += 2;
      } else if (opcode == 0xcd && reader.has(offset, 3)) {
        const u16 target = reader.le16(offset + 1);
        if (target == loadInstrument) {
          const u32 instrument = static_cast<u32>(layout.program.offset + hl);
          if (reader.has(instrument, 3)) {
            drum.sample = reader.u8At(instrument);
            drum.attenuation = reader.u8At(instrument + 1);
            drum.release = reader.u8At(instrument + 2);
          }
        } else if (target == setPan) {
          drum.pan = a;
        } else if (target == setPitch) {
          drum.pitch = hl;
        }
        offset += 3;
      } else if (opcode == 0xc3 && reader.has(offset, 3)) {
        offset += 3;
        ended = true;
        break;
      } else {
        ++offset;
      }
    }
    if (!ended) {
      break;
    }
    drum.range = reader.range(recordBegin, offset - recordBegin);
    result.push_back(drum);
  }
  return result;
}

void readVendettaSynth(Layout& layout, ByteReader reader, const SourceSegment& program,
                       std::vector<Diagnostic>* diagnostics) {
  layout.ym2151Patches = pointerTable(reader, layout, layout.ym2151TableOffset);
  const auto instrumentPointers = pointerTable(reader, layout, layout.k053260TableOffset);
  const auto sampleTable = address(program, "k053260_samp_info_table");
  const auto drumBanks = address(program, "k053260_drum_banks");
  const auto drums = address(program, "k053260_drums");
  const auto loadInstrument = integer(program.attribute("load_instr_sub"));
  const auto setPan = integer(program.attribute("set_pan_sub"));
  const auto setPitch = integer(program.attribute("set_pitch_sub"));
  if (!sampleTable || !drumBanks || !drums || !loadInstrument || !setPan || !setPitch) {
    warn(diagnostics, "Vendetta K053260 metadata is incomplete", layout.program);
    return;
  }

  u32 maximumSample = 0;
  for (const u32 pointer : instrumentPointers) {
    if (!reader.has(pointer, 3)) {
      continue;
    }
    maximumSample = std::max<u32>(maximumSample, reader.u8At(pointer));
    layout.sampleInstruments.push_back(SampleInstrument{
        .sampleInfo = reader.u8At(pointer),
        .volume = static_cast<u8>(0x7f - (reader.u8At(pointer + 1) & 0x7f)),
        .release = reader.u8At(pointer + 2),
        .range = reader.range(pointer, 3),
    });
  }

  const auto records =
      readVendettaDrumRecords(reader, layout, *drums, layout.ym2151TableOffset, *loadInstrument, *setPan, *setPitch);
  std::map<u32, VendettaDrumRecord> recordsByCode;
  for (const auto& record : records) {
    recordsByCode.emplace(record.code, record);
    maximumSample = std::max<u32>(maximumSample, record.sample);
  }

  for (u32 index = 0; index <= maximumSample; ++index) {
    const u32 offset = *sampleTable + index * 8;
    if (!reader.has(offset, 8)) {
      warn(diagnostics, "Vendetta sample-info table is truncated", reader.range(offset, 0));
      break;
    }
    layout.sampleInfos.push_back(SampleInfo{
        .start = readLe24(reader, offset + 4),
        .length = reader.le16(offset + 2),
        .pitch = reader.le16(offset),
        .adpcm = (reader.u8At(offset + 7) & 1) != 0,
        .range = reader.range(offset, 8),
        .sampleIndex = index,
    });
  }
  for (auto& instrument : layout.sampleInstruments) {
    instrument.sampleIndex =
        instrument.sampleInfo < layout.sampleInfos.size() ? instrument.sampleInfo : kInvalidSampleIndex;
  }

  constexpr u32 kBanks = 3;
  constexpr u32 kSlots = 16;
  for (u32 bank = 0; bank < kBanks; ++bank) {
    std::vector<Drum> bankDrums(kSlots);
    for (u32 slot = 0; slot < kSlots; ++slot) {
      const u32 entry = *drumBanks + (bank * kSlots + slot) * 2;
      if (!reader.has(entry, 2)) {
        continue;
      }
      const u16 encoded = reader.le16(entry);
      const auto found = recordsByCode.find(static_cast<u32>(layout.program.offset + encoded));
      if (encoded == 0 || found == recordsByCode.end()) {
        continue;
      }
      const auto& source = found->second;
      const u16 pitch = source.pitch.value_or(
          source.sample < layout.sampleInfos.size() ? layout.sampleInfos[source.sample].pitch : 0);
      bankDrums[slot] = Drum{
          .valid = true,
          .bank = static_cast<u8>(bank),
          .slot = static_cast<u8>(slot),
          .volume = static_cast<u8>(0x7f - (source.attenuation & 0x7f)),
          .release = source.release,
          .pan = source.pan,
          .pitch = pitch,
          .range = source.range,
          .sampleIndex = source.sample,
      };
    }
    layout.drumBanks.push_back(std::move(bankDrums));
  }
}

}  // namespace

const char* versionName(Version version) {
  switch (version) {
    case Version::Tmnt2:
      return "TMNT2";
    case Version::SunsetRiders:
      return "Sunset Riders";
    case Version::BellsWhistles:
      return "Bells & Whistles";
    case Version::Vendetta:
      return "Vendetta";
  }
  return "Konami TMNT2";
}

std::optional<Layout> findLayout(const SourceFile& source, ByteReader reader, std::vector<Diagnostic>* diagnostics) {
  if (source.attribute(mame::kMameFormatAttribute) != kFormatName) {
    return std::nullopt;
  }
  const SourceSegment* program = source.segment("soundcpu");
  const SourceSegment* sound = source.segment("sound");
  if (program == nullptr || sound == nullptr || program->offset + program->size > reader.size() ||
      sound->offset + sound->size > reader.size()) {
    warn(diagnostics, "KonamiTMNT2 requires soundcpu and sound ROM regions", reader.range(0, reader.size()));
    return std::nullopt;
  }

  Layout layout{
      .version = sourceVersion(source),
      .game = std::string(source.attribute(mame::kMameGameAttribute).value_or("Konami")),
      .program = reader.range(program->offset, program->size),
      .sound = reader.range(sound->offset, sound->size),
      .clkb = static_cast<u8>(integer(program->attribute("CLKB")).value_or(0xf2)),
      .defaultTickSkipInterval = static_cast<u8>(integer(program->attribute("default_tick_skip_interval")).value_or(0)),
  };

  if (layout.version == Version::Vendetta) {
    const auto sequence = address(*program, "seq_table");
    const auto ym = address(*program, "ym2151_instr_table");
    const auto sampled = address(*program, "k053260_instr_table");
    const auto drums = address(*program, "k053260_drum_banks");
    if (!sequence || !ym || !sampled || !drums) {
      warn(diagnostics, "Vendetta table metadata is incomplete", layout.program);
      return std::nullopt;
    }
    layout.sequenceTableOffset = *sequence;
    layout.ym2151TableOffset = *ym;
    layout.k053260TableOffset = *sampled;
    layout.drumTableOffset = *drums;
  } else {
    const auto sequence = patternPointer(reader, layout.program, kSequenceTable, 1);
    const auto sampled = patternPointer(reader, layout.program, kSampleInstrumentTable, 3);
    const auto drums = patternPointer(reader, layout.program, kDrumTable, 10);
    auto ym = patternPointer(reader, layout.program, kYmTableTmnt2, 9);
    if (!ym) {
      ym = patternPointer(reader, layout.program, kYmTableEarly, 3);
    }
    if (!sequence || !sampled || !drums || !ym) {
      warn(diagnostics, "KonamiTMNT2 driver tables could not be located", layout.program);
      return std::nullopt;
    }
    layout.sequenceTableOffset = *sequence;
    layout.ym2151TableOffset = *ym;
    layout.k053260TableOffset = *sampled;
    layout.drumTableOffset = *drums;
  }

  readSequences(layout, reader, diagnostics);
  if (layout.version == Version::Vendetta) {
    readVendettaSynth(layout, reader, *program, diagnostics);
  } else {
    readTmnt2Synth(layout, reader, diagnostics);
  }
  if (layout.sequences.empty()) {
    warn(diagnostics, "KonamiTMNT2 sequence table contained no playable songs", layout.program);
    return std::nullopt;
  }
  return layout;
}

}  // namespace vgmtrans::formats::konami_tmnt2
