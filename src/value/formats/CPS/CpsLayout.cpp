/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/CPS/Cps.h"

#include "value/extractors/MameRomSetExtractor.h"

#include <algorithm>
#include <charconv>
#include <limits>
#include <map>
#include <ranges>

namespace vgmtrans::formats::cps {

using namespace core;

namespace {

[[nodiscard]] std::optional<u32> integer(std::optional<std::string_view> text) {
  if (!text) {
    return std::nullopt;
  }
  int base = 10;
  if (text->starts_with("0x") || text->starts_with("0X")) {
    text->remove_prefix(2);
    base = 16;
  }
  u32 value = 0;
  const auto [end, error] = std::from_chars(text->data(), text->data() + text->size(), value, base);
  return error == std::errc{} && end == text->data() + text->size() ? std::optional<u32>{value} : std::nullopt;
}

void warning(std::vector<Diagnostic>* diagnostics, std::string message, SourceRange range = {}) {
  if (diagnostics != nullptr) {
    diagnostics->push_back(Diagnostic{
        .severity = Severity::Warning,
        .message = std::move(message),
        .range = range.valid() ? std::optional<SourceRange>{range} : std::nullopt,
    });
  }
}

[[nodiscard]] std::optional<u32> segmentAddress(const SourceSegment& segment, std::string_view name) {
  const auto value = integer(segment.attribute(name));
  if (!value || *value > segment.size || *value > std::numeric_limits<u32>::max() - segment.offset) {
    return std::nullopt;
  }
  return static_cast<u32>(segment.offset + *value);
}

[[nodiscard]] SourceRange segmentRange(const SourceFile& source, const SourceSegment& segment) {
  return SourceRange{.source = source.id, .offset = segment.offset, .size = segment.size};
}

[[nodiscard]] bool all(ByteReader reader, u32 offset, u32 size, u8 value) {
  if (!reader.has(offset, size)) {
    return false;
  }
  return std::ranges::all_of(reader.slice(offset, size), [value](u8 byte) { return byte == value; });
}

[[nodiscard]] bool hasPlayableSequenceHeader(ByteReader reader, u32 offset, u32 maxTracks, bool littleEndianPointers) {
  if (!reader.has(offset, 1 + maxTracks * 2) || (reader.u8At(offset) & 0x80) != 0) {
    return false;
  }
  for (u32 track = 0; track < maxTracks; ++track) {
    const u32 pointer = offset + 1 + track * 2;
    if ((littleEndianPointers ? reader.le16(pointer) : reader.be16(pointer)) != 0) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] bool parseCps1Layout(CpsLayout& layout, const SourceSegment& program, ByteReader reader,
                                   std::vector<Diagnostic>* diagnostics) {
  const auto tables = segmentAddress(program, "tables");
  if (!tables || !reader.has(*tables, 4)) {
    warning(diagnostics, "CPS1 table metadata is missing or outside the audio CPU ROM", layout.program);
    return false;
  }

  u32 sequenceCount = 0;
  switch (layout.version) {
    case CpsVersion::Cps1V100:
      sequenceCount = reader.u8At(*tables);
      layout.sequenceTableOffset = *tables + 3;
      layout.instrumentTableOffset = layout.sequenceTableOffset + (sequenceCount + 1) * 2;
      layout.masterVolume = 127;
      break;
    case CpsVersion::Cps1V200:
      sequenceCount = reader.u8At(*tables);
      layout.instrumentTableOffset = program.offset + reader.be16(*tables + 1);
      layout.sequenceTableOffset = *tables + 3;
      layout.masterVolume = 127;
      break;
    case CpsVersion::Cps1V350:
      sequenceCount = reader.u8At(*tables);
      layout.masterVolume = reader.u8At(*tables + 1);
      layout.instrumentTableOffset = program.offset + reader.be16(*tables + 2);
      layout.sequenceTableOffset = *tables + 4;
      break;
    case CpsVersion::Cps1V425:
      if (!reader.has(*tables, 6)) {
        return false;
      }
      sequenceCount = reader.u8At(*tables);
      layout.masterVolume = reader.u8At(*tables + 1);
      layout.instrumentTableOffset = program.offset + reader.be16(*tables + 2);
      layout.cps1SampleInstrumentTableOffset = program.offset + reader.be16(*tables + 4);
      layout.sequenceTableOffset = *tables + 6;
      break;
    case CpsVersion::Cps1V500:
    case CpsVersion::Cps1V502: {
      const u32 patchLength = reader.be16(*tables);
      layout.instrumentTableOffset = *tables + 2;
      layout.instrumentTableLength = patchLength;
      const u32 info = layout.instrumentTableOffset + patchLength;
      const u32 infoSize = layout.version == CpsVersion::Cps1V500 ? 2 : 4;
      if (!reader.has(info, infoSize)) {
        return false;
      }
      if (layout.version == CpsVersion::Cps1V500) {
        sequenceCount = reader.u8At(info);
        layout.masterVolume = reader.u8At(info + 1);
        layout.sequenceTableOffset = info + 2;
      } else {
        sequenceCount = reader.be16(info);
        layout.masterVolume = reader.u8At(info + 2);
        layout.sequenceTableOffset = info + 4;
      }
      break;
    }
    default:
      return false;
  }

  layout.sequenceTableLength = sequenceCount * 2;
  if (!reader.has(layout.sequenceTableOffset, layout.sequenceTableLength)) {
    warning(diagnostics, "CPS1 sequence pointer table is truncated", reader.range(layout.sequenceTableOffset, 0));
    return false;
  }

  const u32 rowSize = layout.version == CpsVersion::Cps1V200 || layout.version == CpsVersion::Cps1V500 ||
                              layout.version == CpsVersion::Cps1V502
                          ? 32
                          : 40;
  if (layout.instrumentTableLength == 0) {
    const u32 maximum = 127 * rowSize;
    const u32 available =
        static_cast<u32>(std::min<u64>(maximum, layout.program.endOffset() > layout.instrumentTableOffset
                                                    ? layout.program.endOffset() - layout.instrumentTableOffset
                                                    : 0));
    layout.instrumentTableLength = available;
    if (layout.cps1SampleInstrumentTableOffset &&
        *layout.cps1SampleInstrumentTableOffset > layout.instrumentTableOffset) {
      layout.instrumentTableLength = std::min(layout.instrumentTableLength,
                                              *layout.cps1SampleInstrumentTableOffset - layout.instrumentTableOffset);
    }
  }

  for (u32 index = 0; index < sequenceCount; ++index) {
    const u32 entry = layout.sequenceTableOffset + index * 2;
    const u16 encoded = layout.version == CpsVersion::Cps1V100 ? reader.le16(entry) : reader.be16(entry);
    if (encoded == 0) {
      continue;
    }
    const u32 offset = static_cast<u32>(program.offset + encoded);
    if (!reader.has(offset, 1)) {
      warning(diagnostics, "CPS1 sequence pointer is outside the audio CPU ROM", reader.range(entry, 2));
      continue;
    }
    const u32 maxTracks = layout.version == CpsVersion::Cps1V100 ? 8 : 12;
    if (!hasPlayableSequenceHeader(reader, offset, maxTracks, layout.version == CpsVersion::Cps1V100)) {
      continue;
    }
    layout.sequences.push_back(CpsSequenceInfo{
        .index = index,
        .offset = offset,
        .pointer = reader.range(entry, 2),
        .name = layout.game + " song " + std::to_string(index),
    });
  }
  return true;
}

[[nodiscard]] bool cps2UsesFixedInstrumentTable(CpsVersion version) {
  return version >= CpsVersion::Cps2V100 && version <= CpsVersion::Cps2V115;
}

[[nodiscard]] bool cps2UsesArticulationTable(CpsVersion version) {
  return version == CpsVersion::Cps2V130 || version == CpsVersion::Cps2V131 || version == CpsVersion::Cps2V140 ||
         version == CpsVersion::Cps2V171 || version == CpsVersion::Cps2V180 || version == CpsVersion::Cps2V210 ||
         version == CpsVersion::Cps2V211;
}

[[nodiscard]] bool parseCps2Layout(CpsLayout& layout, const SourceSegment& program, ByteReader reader,
                                   std::vector<Diagnostic>* diagnostics) {
  const auto seqTable = segmentAddress(program, "seq_table");
  const auto sampleTable = segmentAddress(program, "samp_table");
  const auto banks = integer(program.attribute("num_instr_banks"));
  const auto instruments =
      segmentAddress(program, cps2UsesFixedInstrumentTable(layout.version) ? "instr_table" : "instr_table_ptrs");
  if (!seqTable || !sampleTable || !banks || !instruments || *banks == 0) {
    warning(diagnostics, "CPS sample, instrument, or sequence table metadata is incomplete", layout.program);
    return false;
  }
  layout.sequenceTableOffset = *seqTable;
  layout.sampleInfoTableOffset = *sampleTable;
  layout.instrumentTableOffset = *instruments;
  layout.instrumentBanks = *banks;

  if (const auto length = integer(program.attribute("samp_table_length"))) {
    layout.sampleInfoTableLength = *length;
  }
  if (layout.sampleInfoTableLength == 0 && !isCps3(layout.version)) {
    constexpr u32 rowSize = 8;
    while (reader.has(layout.sampleInfoTableOffset + layout.sampleInfoTableLength, rowSize)) {
      const u32 row = layout.sampleInfoTableOffset + layout.sampleInfoTableLength;
      if (all(reader, row, rowSize, 0) || all(reader, row, rowSize, 0xff)) {
        break;
      }
      layout.sampleInfoTableLength += rowSize;
    }
  }
  const u32 sampleRowSize = isCps3(layout.version) ? 16 : 8;
  layout.sampleInfoTableLength -= layout.sampleInfoTableLength % sampleRowSize;
  if (layout.sampleInfoTableLength == 0 ||
      layout.sampleInfoTableLength > layout.program.endOffset() - layout.sampleInfoTableOffset ||
      !reader.has(layout.sampleInfoTableOffset, layout.sampleInfoTableLength)) {
    warning(diagnostics, "CPS sample-info table is empty or truncated", reader.range(layout.sampleInfoTableOffset, 0));
    return false;
  }
  if (cps2UsesArticulationTable(layout.version)) {
    layout.articulationTableOffset = segmentAddress(program, "artic_table");
    layout.articulationTableLength = 0x800;
    if (!layout.articulationTableOffset ||
        layout.articulationTableLength > layout.program.endOffset() - *layout.articulationTableOffset ||
        !reader.has(*layout.articulationTableOffset, layout.articulationTableLength)) {
      warning(diagnostics, "CPS articulation table metadata is missing or truncated", layout.program);
      return false;
    }
  }

  u32 firstSequence = 0;
  u32 firstEntry = 0;
  for (u32 entry = layout.sequenceTableOffset; reader.has(entry, 4) && entry < layout.program.endOffset(); entry += 4) {
    const u32 raw = reader.be32(entry);
    if (raw == 0) {
      continue;
    }
    firstEntry = entry;
    firstSequence = isCps3(layout.version) ? static_cast<u32>(static_cast<s64>(layout.sequenceTableOffset) - 8 + raw)
                                           : static_cast<u32>(program.offset + (raw & 0x000fffff));
    break;
  }
  if (firstSequence <= layout.sequenceTableOffset || !reader.has(firstSequence, 1)) {
    warning(diagnostics, "CPS sequence table does not lead to a valid sequence", reader.range(firstEntry, 4));
    return false;
  }
  layout.sequenceTableLength = firstSequence - layout.sequenceTableOffset;
  layout.sequenceTableLength -= layout.sequenceTableLength % 4;

  for (u32 index = 0; index * 4 < layout.sequenceTableLength; ++index) {
    const u32 entry = layout.sequenceTableOffset + index * 4;
    const u32 raw = reader.be32(entry);
    if (raw == 0) {
      continue;
    }
    const u32 offset = isCps3(layout.version) ? static_cast<u32>(static_cast<s64>(layout.sequenceTableOffset) - 8 + raw)
                                              : static_cast<u32>(program.offset + (raw & 0x000fffff));
    if (!reader.has(offset, 1)) {
      warning(diagnostics, "CPS sequence pointer is outside the audio CPU ROM", reader.range(entry, 4));
      continue;
    }
    if (!hasPlayableSequenceHeader(reader, offset, 16, false)) {
      continue;
    }
    layout.sequences.push_back(CpsSequenceInfo{
        .index = index,
        .offset = offset,
        .pointer = reader.range(entry, 4),
        .name = layout.game + " song " + std::to_string(index),
    });
  }
  return !layout.sequences.empty();
}

}  // namespace

std::optional<CpsVersion> cpsVersion(std::string_view value) {
  static const std::map<std::string_view, CpsVersion, std::less<>> versions{
      {"CPS1_V1.00", CpsVersion::Cps1V100},   {"CPS1_V2.00", CpsVersion::Cps1V200},
      {"CPS1_V3.50", CpsVersion::Cps1V350},   {"CPS1_V4.25", CpsVersion::Cps1V425},
      {"CPS1_V5.00", CpsVersion::Cps1V500},   {"CPS1_V5.02", CpsVersion::Cps1V502},
      {"CPS2_V1.00", CpsVersion::Cps2V100},   {"CPS2_V1.01", CpsVersion::Cps2V101},
      {"CPS2_V1.03", CpsVersion::Cps2V103},   {"CPS2_V1.04", CpsVersion::Cps2V104},
      {"CPS2_V1.05A", CpsVersion::Cps2V105A}, {"CPS2_V1.05C", CpsVersion::Cps2V105C},
      {"CPS2_V1.05", CpsVersion::Cps2V105},   {"CPS2_V1.06B", CpsVersion::Cps2V106B},
      {"CPS2_V1.15C", CpsVersion::Cps2V115C}, {"CPS2_V1.15", CpsVersion::Cps2V115},
      {"CPS2_V1.16B", CpsVersion::Cps2V116B}, {"CPS2_V1.16", CpsVersion::Cps2V116},
      {"CPS2_V1.30", CpsVersion::Cps2V130},   {"CPS2_V1.31", CpsVersion::Cps2V131},
      {"CPS2_V1.40", CpsVersion::Cps2V140},   {"CPS2_V1.71", CpsVersion::Cps2V171},
      {"CPS2_V1.80", CpsVersion::Cps2V180},   {"CPS2_V2.00", CpsVersion::Cps2V200},
      {"CPS2_V2.01B", CpsVersion::Cps2V201B}, {"CPS2_V2.10", CpsVersion::Cps2V210},
      {"CPS2_V2.11", CpsVersion::Cps2V211},   {"CPS3", CpsVersion::Cps3},
  };
  const auto found = versions.find(value);
  return found == versions.end() ? std::nullopt : std::optional<CpsVersion>{found->second};
}

const char* cpsVersionName(CpsVersion version) {
  switch (version) {
    case CpsVersion::Cps1V100:
      return "CPS1 V1.00";
    case CpsVersion::Cps1V200:
      return "CPS1 V2.00";
    case CpsVersion::Cps1V350:
      return "CPS1 V3.50";
    case CpsVersion::Cps1V425:
      return "CPS1 V4.25";
    case CpsVersion::Cps1V500:
      return "CPS1 V5.00";
    case CpsVersion::Cps1V502:
      return "CPS1 V5.02";
    case CpsVersion::Cps2V100:
      return "CPS2 V1.00";
    case CpsVersion::Cps2V101:
      return "CPS2 V1.01";
    case CpsVersion::Cps2V103:
      return "CPS2 V1.03";
    case CpsVersion::Cps2V104:
      return "CPS2 V1.04";
    case CpsVersion::Cps2V105A:
      return "CPS2 V1.05A";
    case CpsVersion::Cps2V105C:
      return "CPS2 V1.05C";
    case CpsVersion::Cps2V105:
      return "CPS2 V1.05";
    case CpsVersion::Cps2V106B:
      return "CPS2 V1.06B";
    case CpsVersion::Cps2V115C:
      return "CPS2 V1.15C";
    case CpsVersion::Cps2V115:
      return "CPS2 V1.15";
    case CpsVersion::Cps2V116B:
      return "CPS2 V1.16B";
    case CpsVersion::Cps2V116:
      return "CPS2 V1.16";
    case CpsVersion::Cps2V130:
      return "CPS2 V1.30";
    case CpsVersion::Cps2V131:
      return "CPS2 V1.31";
    case CpsVersion::Cps2V140:
      return "CPS2 V1.40";
    case CpsVersion::Cps2V171:
      return "CPS2 V1.71";
    case CpsVersion::Cps2V180:
      return "CPS2 V1.80";
    case CpsVersion::Cps2V200:
      return "CPS2 V2.00";
    case CpsVersion::Cps2V201B:
      return "CPS2 V2.01B";
    case CpsVersion::Cps2V210:
      return "CPS2 V2.10";
    case CpsVersion::Cps2V211:
      return "CPS2 V2.11";
    case CpsVersion::Cps3:
      return "CPS3";
    case CpsVersion::Unknown:
      return "Unknown";
  }
  return "Unknown";
}

bool isCps1(CpsVersion version) {
  return version >= CpsVersion::Cps1V100 && version <= CpsVersion::Cps1V502;
}

bool isCps3(CpsVersion version) {
  return version == CpsVersion::Cps3;
}

bool usesLateSequence(CpsVersion version) {
  return version == CpsVersion::Cps2V200 || version == CpsVersion::Cps2V201B || version == CpsVersion::Cps2V210 ||
         version == CpsVersion::Cps2V211 || isCps3(version);
}

double cpsDriverRateHertz(CpsVersion version) {
  return isCps3(version) ? kCps3DriverRateHertz : kCps2DriverRateHertz;
}

std::optional<CpsLayout> findCpsLayout(const SourceFile& source, ByteReader reader,
                                       std::vector<Diagnostic>* diagnostics) {
  const auto version = cpsVersion(source.attribute(mame::kMameFormatVersionAttribute).value_or(""));
  const auto* program = source.segment("audiocpu");
  if (!version || program == nullptr || program->offset > std::numeric_limits<u32>::max() ||
      !reader.has(program->offset, program->size)) {
    return std::nullopt;
  }
  CpsLayout layout{
      .version = *version,
      .game = std::string(source.attribute(mame::kMameGameAttribute).value_or(source.name)),
      .program = segmentRange(source, *program),
  };
  const char* sampleSegment = isCps1(*version) ? "oki6295" : "qsound";
  if (const auto* sample = source.segment(sampleSegment)) {
    layout.sampleRom = segmentRange(source, *sample);
  }
  const bool ok = isCps1(*version) ? parseCps1Layout(layout, *program, reader, diagnostics)
                                   : parseCps2Layout(layout, *program, reader, diagnostics);
  return ok ? std::optional<CpsLayout>{std::move(layout)} : std::nullopt;
}

}  // namespace vgmtrans::formats::cps
