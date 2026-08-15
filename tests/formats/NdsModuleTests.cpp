/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/export/midi/PerformanceMidiRenderer.h"
#include "value/export/synth/ModulationScaling.h"
#include "value/formats/NDS/Nds.h"
#include "value/formats/NDS/NdsEnvelope.h"
#include "value/scan/ScanResultBuilder.h"
#include "value/scan/ScanTypes.h"
#include "value/sequence/SequenceVm.h"
#include "value/validation/SynthValidation.h"

#include "ValueFormatTestSupport.h"

#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

using namespace vgmtrans::core;
using namespace vgmtrans::formats::nds;

namespace {

void expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

const SourceAnnotation* annotationWithKind(const SourceMap& sourceMap, SourceId source, SourceRole role,
                                           std::string_view localKind) {
  const auto annotations = sourceMap.withRole(source, role);
  for (const SourceAnnotationId id : annotations) {
    const SourceAnnotation& annotation = sourceMap.get(id);
    if (annotation.localKind == localKind) {
      return &annotation;
    }
  }
  return nullptr;
}

template <typename T>
const T* assetWithId(const ScanResult& result, AssetId id) {
  for (const auto& asset : result.assets) {
    if (const auto* typed = std::get_if<T>(&asset); typed != nullptr && typed->metadata.id == id) {
      return typed;
    }
  }
  return nullptr;
}

void writeLe16(std::vector<u8>& bytes, size_t offset, u16 value) {
  bytes[offset] = static_cast<u8>(value & 0xff);
  bytes[offset + 1] = static_cast<u8>((value >> 8) & 0xff);
}

void writeLe32(std::vector<u8>& bytes, size_t offset, u32 value) {
  bytes[offset] = static_cast<u8>(value & 0xff);
  bytes[offset + 1] = static_cast<u8>((value >> 8) & 0xff);
  bytes[offset + 2] = static_cast<u8>((value >> 16) & 0xff);
  bytes[offset + 3] = static_cast<u8>((value >> 24) & 0xff);
}

void writeText(std::vector<u8>& bytes, size_t offset, std::string_view text) {
  std::ranges::copy(text, bytes.begin() + static_cast<std::ptrdiff_t>(offset));
}

std::vector<u8> makeNdsSdat(bool withSymbols = true, bool includeUnusedBank = false) {
  constexpr u32 base = 0x20;
  constexpr u32 symb = 0x80;
  constexpr u32 info = 0x120;
  constexpr u32 fat = 0x220;
  constexpr u32 sequenceFile = 0x300;
  constexpr u32 bankFile = 0x340;
  constexpr u32 waveFile = 0x380;
  constexpr u32 unusedBankFile = 0x3c0;
  constexpr u32 unusedWaveFile = 0x400;
  constexpr u32 bankCount = 2;
  constexpr u32 waveCount = 2;
  const u32 visibleBankCount = includeUnusedBank ? bankCount : 1;
  const u32 visibleWaveCount = includeUnusedBank ? waveCount : 1;
  const u32 fileCount = includeUnusedBank ? 5 : 3;

  std::vector<u8> bytes(0x480);
  writeText(bytes, base, std::string_view{"SDAT\xff\xfe\x00\x01", 8});
  writeLe32(bytes, base + 0x08, static_cast<u32>(bytes.size()) - base - 8);
  writeLe16(bytes, base + 0x0c, 0x40);
  writeLe16(bytes, base + 0x0e, withSymbols ? 4 : 3);
  if (withSymbols) {
    writeLe32(bytes, base + 0x10, symb - base);
    writeLe32(bytes, base + 0x14, info - symb);
  }
  writeLe32(bytes, base + 0x18, info - base);
  writeLe32(bytes, base + 0x1c, fat - info);
  writeLe32(bytes, base + 0x20, fat - base);
  writeLe32(bytes, base + 0x24, 12 + fileCount * 0x10);

  if (withSymbols) {
    writeText(bytes, symb, "SYMB");
    writeLe32(bytes, symb + 4, info - symb);
    writeLe32(bytes, symb + 0x08, 0x20);
    writeLe32(bytes, symb + 0x10, 0x28);
    writeLe32(bytes, symb + 0x14, 0x34);
    writeLe32(bytes, symb + 0x20, 1);
    writeLe32(bytes, symb + 0x24, 0x50);
    writeLe32(bytes, symb + 0x28, visibleBankCount);
    writeLe32(bytes, symb + 0x2c, 0x60);
    if (includeUnusedBank) {
      writeLe32(bytes, symb + 0x30, 0x70);
    }
    writeLe32(bytes, symb + 0x34, visibleWaveCount);
    writeLe32(bytes, symb + 0x38, 0x80);
    if (includeUnusedBank) {
      writeLe32(bytes, symb + 0x3c, 0x90);
    }
    writeText(bytes, symb + 0x50, "Sequence");
    writeText(bytes, symb + 0x60, "Bank Used");
    writeText(bytes, symb + 0x70, "Bank Unused");
    writeText(bytes, symb + 0x80, "Wave Used");
    writeText(bytes, symb + 0x90, "Wave Unused");
  }

  writeText(bytes, info, "INFO");
  writeLe32(bytes, info + 4, fat - info);
  writeLe32(bytes, info + 0x08, 0x20);
  writeLe32(bytes, info + 0x10, 0x28);
  writeLe32(bytes, info + 0x14, 0x34);
  writeLe32(bytes, info + 0x20, 1);
  writeLe32(bytes, info + 0x24, 0x60);
  writeLe32(bytes, info + 0x28, visibleBankCount);
  writeLe32(bytes, info + 0x2c, 0x70);
  if (includeUnusedBank) {
    writeLe32(bytes, info + 0x30, 0x80);
  }
  writeLe32(bytes, info + 0x34, visibleWaveCount);
  writeLe32(bytes, info + 0x38, 0x90);
  if (includeUnusedBank) {
    writeLe32(bytes, info + 0x3c, 0xa0);
  }

  writeLe16(bytes, info + 0x60, 0);
  writeLe16(bytes, info + 0x64, 0);
  writeLe16(bytes, info + 0x70, 1);
  writeLe16(bytes, info + 0x74, 0);
  for (u32 slot = 1; slot < 4; ++slot) {
    writeLe16(bytes, info + 0x74 + slot * 2, 0xffff);
  }
  writeLe16(bytes, info + 0x90, 2);
  if (includeUnusedBank) {
    writeLe16(bytes, info + 0x80, 3);
    writeLe16(bytes, info + 0x84, 1);
    for (u32 slot = 1; slot < 4; ++slot) {
      writeLe16(bytes, info + 0x84 + slot * 2, 0xffff);
    }
    writeLe16(bytes, info + 0xa0, 4);
  }

  writeText(bytes, fat, "FAT ");
  writeLe32(bytes, fat + 4, 12 + fileCount * 0x10);
  writeLe32(bytes, fat + 8, fileCount);
  const auto fatEntry = [&](u32 fileId, u32 offset, u32 size) {
    const u32 entry = fat + 12 + fileId * 0x10;
    writeLe32(bytes, entry, offset - base);
    writeLe32(bytes, entry + 4, size);
  };
  fatEntry(0, sequenceFile, 0x1d);
  fatEntry(1, bankFile, 0x3c);
  fatEntry(2, waveFile, 0x3c);
  if (includeUnusedBank) {
    fatEntry(3, unusedBankFile, 0x3c);
    fatEntry(4, unusedWaveFile, 0x3c);
  }

  writeText(bytes, sequenceFile, std::string_view{"SSEQ\xff\xfe\x00\x01", 8});
  writeLe32(bytes, sequenceFile + 8, 0x1d);
  writeLe32(bytes, sequenceFile + 0x18, 0x1c);
  bytes[sequenceFile + 0x1c] = 0xff;
  for (const u32 offset : {bankFile, unusedBankFile}) {
    writeText(bytes, offset, std::string_view{"SBNK\xff\xfe\x00\x01", 8});
    writeLe32(bytes, offset + 0x38, 0);
  }
  for (const u32 offset : {waveFile, unusedWaveFile}) {
    writeText(bytes, offset, std::string_view{"SWAR\xff\xfe\x00\x01", 8});
    writeLe32(bytes, offset + 0x38, 0);
  }
  return bytes;
}

SequenceProgramAsset decodeTestProgram(ByteReader reader, u32 sequenceOffset, u32 sequenceEnd,
                                       bool recoverMalformedSdatRange = false, SourceMapBuilder* sourceMap = nullptr,
                                       std::vector<Diagnostic>* diagnostics = nullptr) {
  const NdsSequenceRange range{
      .offset = sequenceOffset,
      .sequenceEnd = sequenceEnd,
      .recoverMalformedSdatRange = recoverMalformedSdatRange,
  };
  return SequenceProgramAsset{
      .metadata =
          AssetMetadata{
              .id = AssetId{1},
              .format = std::string(kNdsFormatName),
              .name = "Test",
              .range = reader.range(sequenceOffset, sequenceEnd - sequenceOffset),
          },
      .program = parseNdsSequenceProgram(reader, AssetId{1}, range, sourceMap, diagnostics),
  };
}

TrackProgram decodeTestTrack(ByteReader reader, u32 sequenceOffset, u32 sequenceEnd, u32 startOffset, u32 trackIndex,
                             bool recoverMalformedSdatRange = false, SourceMapBuilder* sourceMap = nullptr,
                             std::vector<Diagnostic>* diagnostics = nullptr) {
  const SequenceProgramAsset asset =
      decodeTestProgram(reader, sequenceOffset, sequenceEnd, recoverMalformedSdatRange, sourceMap, diagnostics);
  const auto track = std::ranges::find_if(asset.program.tracks, [&](const TrackProgram& candidate) {
    return candidate.sourceTrackNumber == trackIndex && candidate.startAddress.value == startOffset;
  });
  if (track == asset.program.tracks.end()) {
    throw std::runtime_error("NDS test sequence did not contain the requested track");
  }
  return *track;
}

SequenceProgram decodeTestSequenceProgram(std::initializer_list<u8> commands) {
  constexpr u32 sequenceOffset = 0x100;
  constexpr u32 trackStart = sequenceOffset + 0x1c;
  std::vector<u8> bytes(trackStart + commands.size());
  std::ranges::copy(commands, bytes.begin() + trackStart);

  const SequenceDialect& dialect = ndsSequenceDialect();
  const TrackProgram track =
      decodeTestTrack(ByteReader(SourceId{30}, bytes), sequenceOffset, static_cast<u32>(bytes.size()), trackStart, 0);
  return SequenceProgram{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .sourceBaseAddress = Address{trackStart},
      .tracks = {track},
  };
}

PerformanceSequence renderTestPerformance(std::initializer_list<u8> commands) {
  return SequenceVm(LoopPolicy::PlayOnce).render(decodeTestSequenceProgram(commands), ndsSequenceDialect());
}

}  // namespace

void ndsLayoutResolvesNamesFilesAndDependencies() {
  auto bytes = makeNdsSdat();
  ScanIdAllocator ids;
  ScanInput input{
      .source = SourceFile{.id = SourceId{20}, .name = "layout.sdat", .size = bytes.size()},
      .reader = ByteReader(SourceId{20}, bytes),
      .ids = ids,
  };
  ScanResultBuilder builder(input, "NDS");
  const auto layout = parseNdsLayout(builder, 0x20);
  expect(layout.has_value(), "NDS layout parser should accept a complete SDAT");
  expect(layout->range == input.reader.range(0x20, bytes.size() - 0x20),
         "NDS layout should preserve the bounded SDAT source range");
  expect(layout->sequences.size() == 1 && layout->sequences[0].name == "Sequence" &&
             layout->sequences[0].file == input.reader.range(0x300, 0x1d) && layout->sequences[0].bank == 0,
         "NDS layout should resolve a named sequence, its FAT range, and its bank");
  expect(layout->banks.size() == 1 && layout->banks[0].name == "Bank Used" &&
             layout->banks[0].file == input.reader.range(0x340, 0x3c) && layout->banks[0].waveArchives[0] == 0 &&
             !layout->banks[0].waveArchives[1],
         "NDS layout should resolve bank files and sanitize unused wave slots");
  expect(layout->waveArchives.size() == 1 && layout->waveArchives[0].name == "Wave Used" &&
             layout->waveArchives[0].file == input.reader.range(0x380, 0x3c),
         "NDS layout should resolve named wave-archive FAT ranges");

  const ScanResult result = builder.finish();
  expect(result.diagnostics.empty(), "NDS layout parser should not diagnose a complete SDAT");
  expect(annotationWithKind(result.sourceMap, SourceId{20}, SourceRole::Header, "sdat-header") != nullptr &&
             annotationWithKind(result.sourceMap, SourceId{20}, SourceRole::Section, "sdat-symb") != nullptr &&
             annotationWithKind(result.sourceMap, SourceId{20}, SourceRole::Section, "sdat-info") != nullptr &&
             annotationWithKind(result.sourceMap, SourceId{20}, SourceRole::Table, "sdat-fat") != nullptr,
         "NDS layout parsing should annotate its header and structural sections in one pass");

  bytes = makeNdsSdat(false);
  ScanInput unnamedInput{
      .source = SourceFile{.id = SourceId{21}, .name = "unnamed.sdat", .size = bytes.size()},
      .reader = ByteReader(SourceId{21}, bytes),
      .ids = ids,
  };
  ScanResultBuilder unnamedBuilder(unnamedInput, "NDS");
  const auto unnamed = parseNdsLayout(unnamedBuilder, 0x20);
  expect(unnamed && unnamed->sequences[0].name == "SSEQ_0000" && unnamed->banks[0].name == "SBNK_0000" &&
             unnamed->waveArchives[0].name == "SWAR_0000",
         "NDS layouts without SYMB should use stable fallback names");
  expect(unnamedBuilder.finish().diagnostics.empty(), "a missing optional SYMB section should not be diagnosed");
}

void ndsLayoutBoundsMalformedTablesAndPointers() {
  auto bytes = makeNdsSdat(false);
  constexpr u32 info = 0x120;
  constexpr u32 fat = 0x220;
  writeLe32(bytes, info + 0x20, std::numeric_limits<u32>::max());
  writeLe32(bytes, info + 0x2c, 0xfffffff0);
  writeLe32(bytes, fat + 12 + 2 * 0x10, 0xfffffff0);

  ScanIdAllocator ids;
  ScanInput input{
      .source = SourceFile{.id = SourceId{22}, .name = "malformed.sdat", .size = bytes.size()},
      .reader = ByteReader(SourceId{22}, bytes),
      .ids = ids,
  };
  ScanResultBuilder builder(input, "NDS");
  const auto layout = parseNdsLayout(builder, 0x20);
  expect(layout && layout->sequences.empty(),
         "NDS layout should reject a huge truncated count before reserving sequence entries");
  expect(layout->banks.size() == 1 && !layout->banks[0].file,
         "NDS layout should retain a named slot but reject an overflowing INFO record pointer");
  expect(layout->waveArchives.size() == 1 && !layout->waveArchives[0].file,
         "NDS layout should reject a FAT entry whose resolved offset overflows the source address space");

  const ScanResult result = builder.finish();
  const auto hasDiagnostic = [&](std::string_view text) {
    return std::ranges::any_of(result.diagnostics, [&](const Diagnostic& diagnostic) {
      return diagnostic.message.find(text) != std::string::npos;
    });
  };
  expect(hasDiagnostic("sequence INFO list was truncated") && hasDiagnostic("INFO record pointer was invalid") &&
             hasDiagnostic("FAT entry pointed outside the source"),
         "NDS layout should diagnose bounded-list, INFO-pointer, and FAT-pointer failures precisely");
}

void ndsSequenceFatRangesHandleNormalEmptyAndRecoveredFiles() {
  std::vector<u8> bytes(0x100);
  writeText(bytes, 0x10, std::string_view{"SSEQ\xff\xfe\x00\x01", 8});
  const ByteReader reader(SourceId{23}, bytes);
  const NdsSequenceRange normal = ndsSequenceRangeForFatEntry(reader, reader.range(0x10, 0x20));
  expect(normal.offset == 0x10 && normal.sequenceEnd == 0x30 && !normal.recoverMalformedSdatRange,
         "NDS sequence ranges should preserve normal SSEQ FAT bounds");

  const NdsSequenceRange empty = ndsSequenceRangeForFatEntry(reader, reader.range(0x30, 0x08));
  expect(empty.offset == 0x30 && empty.sequenceEnd == 0x4c && !empty.recoverMalformedSdatRange,
         "NDS zero-filled placeholder ranges should expose one empty SSEQ header");

  bytes[0x50] = 1;
  const NdsSequenceRange headerless =
      ndsSequenceRangeForFatEntry(ByteReader(SourceId{23}, bytes), ByteReader(SourceId{23}, bytes).range(0x50, 4));
  expect(headerless.offset == 0x50 && headerless.sequenceEnd == 0x54 && !headerless.recoverMalformedSdatRange,
         "NDS ordinary headerless files should retain their FAT bounds");

  std::ranges::fill(bytes, 0);
  writeText(bytes, 0x40, std::string_view{"SSEQ\xff\xfe\x00\x01", 8});
  writeLe32(bytes, 0x48, 0x20);
  const ByteReader recoveredReader(SourceId{23}, bytes);
  const NdsSequenceRange recovered = ndsSequenceRangeForFatEntry(recoveredReader, recoveredReader.range(0x20, 0x10));
  expect(recovered.offset == 0x40 && recovered.sequenceEnd == 0x60 && recovered.recoverMalformedSdatRange,
         "NDS malformed FAT ranges should recover a nearby SSEQ and its declared size");
}

void ndsModuleOnlyBuildsDependenciesOfReferencedBanks() {
  const auto bytes = makeNdsSdat(true, true);
  ScanIdAllocator ids;
  ScanInput input{
      .source = SourceFile{.id = SourceId{24}, .name = "dependencies.sdat", .size = bytes.size()},
      .reader = ByteReader(SourceId{24}, bytes),
      .ids = ids,
  };
  const ScanResult result = ndsDefinition().module.scan(input);
  expect(result.diagnostics.empty(), "NDS module should scan a complete dependency fixture without diagnostics");
  expect(result.assets.size() == 4 && result.explicitCollections.size() == 1,
         "NDS module should create PSG, one used wave archive, one used bank, and one sequence");
  const auto hasAssetNamed = [&](std::string_view name) {
    return std::ranges::any_of(result.assets, [&](const Asset& asset) {
      return std::visit([&](const auto& typed) { return typed.metadata.name == name; }, asset);
    });
  };
  expect(hasAssetNamed("Bank Used") && hasAssetNamed("Wave Used") && !hasAssetNamed("Bank Unused") &&
             !hasAssetNamed("Wave Unused"),
         "NDS module should not emit orphan assets owned only by an unused bank");
}

void ndsSequenceDialectDecodesAndRendersNoteWaitCommands() {
  std::vector<u8> bytes(0x140);
  constexpr u32 sequenceOffset = 0x100;
  constexpr u32 trackStart = sequenceOffset + 0x1c;
  bytes[trackStart + 0] = 0xc7;
  bytes[trackStart + 1] = 0x01;
  bytes[trackStart + 2] = 0x3c;
  bytes[trackStart + 3] = 0x64;
  bytes[trackStart + 4] = 0x18;
  bytes[trackStart + 5] = 0xe1;
  bytes[trackStart + 6] = 0x78;
  bytes[trackStart + 7] = 0x00;
  bytes[trackStart + 8] = 0x80;
  bytes[trackStart + 9] = 0x06;
  bytes[trackStart + 10] = 0xff;

  const SequenceDialect& dialect = ndsSequenceDialect();
  expect(dialect.execute != nullptr, "NDS SSEQ should register a compiled command executor");

  ScanIdAllocator annotationIds;
  SourceMapBuilder sourceMap([&annotationIds]() { return annotationIds.nextSourceAnnotationId(); });
  std::vector<Diagnostic> decodeDiagnostics;
  const TrackProgram track = decodeTestTrack(ByteReader(SourceId{4}, bytes), sequenceOffset, trackStart + 11,
                                             trackStart, 0, false, &sourceMap, &decodeDiagnostics);
  expect(track.startAddress.value == trackStart && track.commands.size() == 5,
         "NDS SSEQ parser should find the primary track and decode all fixture commands");
  expect(std::ranges::all_of(track.commands, [](const SourceCommand& command) { return command.encodedSize != 0; }),
         "NDS SSEQ should store every complete command as named semantic data");
  const SourceMap annotations = sourceMap.finish();
  expect(commandDetailKind(annotations, track.commands[0]) == "nds.note-wait",
         "NDS SSEQ dialect should decode note-wait as a local command");
  expect(commandDetailKind(annotations, track.commands[1]) == "nds.note",
         "NDS SSEQ dialect should decode source note opcodes as local commands");
  const auto noteAnnotations = annotations.withSequenceSemantic(SourceId{4}, SequenceSemantic::Note);
  expect(noteAnnotations.size() == 1, "NDS SSEQ note command should publish a source annotation");
  const auto& noteAnnotation = annotations.get(noteAnnotations[0]);
  expect(noteAnnotation.range.offset == trackStart + 2 && noteAnnotation.range.size == 3,
         "NDS SSEQ note annotation should use the exact decoded command range");
  const auto hasNoteField = [&](std::string_view name) {
    return std::ranges::any_of(noteAnnotation.fields, [&](const SourceField& field) { return field.name == name; });
  };
  expect(hasNoteField("opcode") && hasNoteField("key") && hasNoteField("velocity") && hasNoteField("duration"),
         "NDS SSEQ note annotation should record opcode and operand fields");
  expect(fieldEquals(fieldWithName(noteAnnotation, "key"), u64{0x3c}) &&
             fieldEquals(fieldWithName(noteAnnotation, "velocity"), u64{0x64}) &&
             fieldEquals(fieldWithName(noteAnnotation, "duration"), u64{0x18}),
         "NDS SSEQ note annotation should preserve key, velocity, and duration operands");
  expect(decodeDiagnostics.empty(), "NDS SSEQ semantic decode should not emit diagnostics for valid commands");

  const SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .sourceBaseAddress = Address{trackStart},
      .tracks = {track},
  };
  const PerformanceSequence performance = SequenceVm(LoopPolicy::PlayOnce).render(program, dialect);
  expect(performance.diagnostics.empty(), "NDS SSEQ fixture should render without diagnostics");
  expect(performance.tracks.size() == 1 && performance.tracks[0].endTick == 30,
         "NDS note-wait should make notes advance time before the rest command");

  const MidiSequence midi = renderMidiSequence(performance);
  expect(midi.tracks.size() == 1, "NDS SSEQ MIDI rendering should preserve one track");
  const auto& events = midi.tracks[0].events;
  const auto port =
      std::ranges::find_if(events, [](const MidiEvent& event) { return std::holds_alternative<MidiPort>(event); });
  const auto note =
      std::ranges::find_if(events, [](const MidiEvent& event) { return std::holds_alternative<NoteDuration>(event); });
  const auto tempo =
      std::ranges::find_if(events, [](const MidiEvent& event) { return std::holds_alternative<Tempo>(event); });
  expect(port != events.end() && std::get<MidiPort>(*port).port == 0,
         "NDS SSEQ MIDI rendering should emit MIDI port metadata");
  expect(
      note != events.end() && std::get<NoteDuration>(*note).tick == 0 && std::get<NoteDuration>(*note).duration == 24,
      "NDS SSEQ note should render at the current tick with its source duration");
  expect(tempo != events.end() && std::get<Tempo>(*tempo).tick == 24 &&
             std::get<Tempo>(*tempo).microsecondsPerQuarter == 500000,
         "NDS SSEQ tempo should convert BPM to microseconds per quarter");
  expect(std::get<EndOfTrack>(events.back()).tick == 30, "NDS SSEQ MIDI rendering should preserve VM end tick");

  bytes[trackStart + 0] = 0x81;
  bytes[trackStart + 1] = 0x81;
  bytes[trackStart + 2] = 0x05;
  bytes[trackStart + 3] = 0xff;
  ScanIdAllocator ids;
  ScanInput input{
      .source = SourceFile{.name = "program.sseq"},
      .reader = ByteReader(SourceId{4}, bytes),
      .ids = ids,
  };
  SourceMapBuilder programSourceMap([&ids]() { return ids.nextSourceAnnotationId(); });
  std::vector<Diagnostic> programDiagnostics;
  const auto sourceProgram = parseNdsSequenceProgram(input.reader, AssetId{7},
                                                     NdsSequenceRange{
                                                         .offset = sequenceOffset,
                                                         .sequenceEnd = trackStart + 4,
                                                     },
                                                     &programSourceMap, &programDiagnostics);
  expect(sourceProgram.tracks.size() == 1 && sourceProgram.tracks[0].commands.size() == 2,
         "NDS program source-link fixture should still decode program and end commands");
  const SourceMap programAnnotations = programSourceMap.finish();
  const auto* sseqHeader = annotationWithKind(programAnnotations, SourceId{4}, SourceRole::Header, "sseq-header");
  expect(sseqHeader != nullptr && sseqHeader->owner == ObjectRefs::sequence(AssetId{7}),
         "NDS SSEQ header annotation should point at the semantic sequence asset");
  const auto trackAnnotations = programAnnotations.withRole(SourceId{4}, SourceRole::SequenceTrack);
  expect(trackAnnotations.size() == 1, "NDS sequence parse should publish a track annotation");
  const SourceAnnotation& trackAnnotation = programAnnotations.get(trackAnnotations.front());
  expect(!trackAnnotation.parent, "NDS track annotation should be a sibling of the SSEQ header");
  expect(trackAnnotation.owner == ObjectRefs::sequenceTrack(AssetId{7}, 0),
         "NDS track annotation should point at the semantic sequence track");
  expect(trackAnnotation.range.offset == trackStart && trackAnnotation.range.size == 4,
         "NDS track annotation should span decoded command bytes");
  const auto programAnnotationIds = programAnnotations.withSequenceSemantic(SourceId{4}, SequenceSemantic::Program);
  expect(programAnnotationIds.size() == 1, "NDS program command should publish one program annotation");
  const auto& programAnnotation = programAnnotations.get(programAnnotationIds[0]);
  const auto instrumentLink = std::ranges::find_if(
      programAnnotation.links, [](const SourceLink& link) { return link.role == SourceLinkRole::UsesInstrument; });
  expect(instrumentLink != programAnnotation.links.end(),
         "NDS program command should record a structured instrument source link");
  const auto* instrumentTarget = std::get_if<ObjectRef>(&instrumentLink->target);
  expect(instrumentTarget != nullptr && instrumentTarget->kind == ObjectKind::InstrumentProgram &&
             !instrumentTarget->asset.valid() && instrumentTarget->index0 == 1 && instrumentTarget->index1 == 5,
         "NDS program source link should preserve unresolved bank/program selectors");
  expect(programDiagnostics.empty(), "NDS program source-link decode should not emit diagnostics");

  bytes[trackStart + 0] = 0xd5;
  bytes[trackStart + 1] = 0x7f;
  bytes[trackStart + 2] = 0xff;
  SourceMapBuilder expressionSourceMap;
  const TrackProgram expressionTrack = decodeTestTrack(ByteReader(SourceId{4}, bytes), sequenceOffset, trackStart + 3,
                                                       trackStart, 0, false, &expressionSourceMap);
  const SourceMap expressionAnnotations = expressionSourceMap.finish();
  expect(expressionTrack.commands.size() == 2 &&
             commandDetailKind(expressionAnnotations, expressionTrack.commands[0]) == "nds.expression",
         "NDS expression opcode should decode as a musical command");
  const SequenceProgram expressionProgram{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .sourceBaseAddress = Address{trackStart},
      .tracks = {expressionTrack},
  };
  const MidiSequence expressionMidi =
      renderMidiSequence(SequenceVm(LoopPolicy::PlayOnce).render(expressionProgram, dialect));
  expect(std::holds_alternative<Expression>(expressionMidi.tracks[0].events[1]),
         "NDS expression opcode should render as MIDI expression");
}

void ndsSequenceDialectComposesPitchBendRangeBehavior() {
  std::vector<u8> bytes(0x130);
  constexpr u32 sequenceOffset = 0x100;
  constexpr u32 trackStart = sequenceOffset + 0x1c;
  bytes[trackStart + 0] = 0xc5;
  bytes[trackStart + 1] = 0x0c;
  bytes[trackStart + 2] = 0xc4;
  bytes[trackStart + 3] = 0x40;
  bytes[trackStart + 4] = 0xff;

  const SequenceDialect& dialect = ndsSequenceDialect();
  const TrackProgram track =
      decodeTestTrack(ByteReader(SourceId{16}, bytes), sequenceOffset, trackStart + 5, trackStart, 0);
  expect(track.commands.size() == 3 && track.commands[0].execution.valid(),
         "NDS pitch-bend range should compose its state update and output into one command body");

  const SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .sourceBaseAddress = Address{trackStart},
      .tracks = {track},
  };
  const PerformanceSequence performance = SequenceVm(LoopPolicy::PlayOnce).render(program, dialect);
  expect(performance.diagnostics.empty() && performance.tracks[0].events.size() == 2,
         "NDS composed pitch actions should render without additional commands");
  expect(std::get<PitchBendRangePerformanceEvent>(performance.tracks[0].events[0]).cents == 1200 &&
             std::get<PitchBendPerformanceEvent>(performance.tracks[0].events[1]).semitones == 6.0,
         "NDS pitch bend should observe the range state set by the preceding explicit action");
}

void ndsSequenceDialectEmitsStickyDynamicAdsr() {
  constexpr u8 attack = 0x6d;
  constexpr u8 decay = 0x20;
  constexpr u8 sustain = 0x40;
  constexpr u8 release = 0x7f;
  const auto expected = ndsEnvelope(attack, decay, sustain, release);
  expect(expected.has_value(), "NDS dynamic ADSR fixture should use valid SBNK envelope bytes");

  const SequenceProgram program = decodeTestSequenceProgram({
      0xd0, attack,   // attack override
      0xd1, decay,    // decay override
      0xd2, sustain,  // sustain override
      0xd3, release,  // release override
      0x81, 0x01,     // subsequent program changes retain the overrides
      0xff,
  });

  const auto& commands = program.tracks.front().commands;
  const auto envelopeCommandCount = std::ranges::count(commands, SequenceSemantic::Envelope, &SourceCommand::semantic);
  expect(envelopeCommandCount == 4 && std::ranges::all_of(commands,
                                                          [](const SourceCommand& command) {
                                                            return command.semantic != SequenceSemantic::Envelope ||
                                                                   (command.encodedSize == 2 &&
                                                                    command.execution.valid());
                                                          }),
         "NDS D0-D3 should decode as executable two-byte envelope commands");

  const PerformanceSequence performance = SequenceVm(LoopPolicy::PlayOnce).render(program, ndsSequenceDialect());
  expect(performance.diagnostics.empty(), "NDS dynamic ADSR fixture should render without diagnostics");

  const auto& events = performance.tracks.front().events;
  expect(events.size() == 5 && std::holds_alternative<InstrumentPerformanceEvent>(events.back()),
         "NDS dynamic ADSR should emit four envelope updates followed by the program change");
  std::array<const EnvelopePerformanceEvent*, 4> envelopes{};
  for (size_t i = 0; i < envelopes.size(); ++i) {
    envelopes[i] = std::get_if<EnvelopePerformanceEvent>(&events[i]);
  }
  expect(std::ranges::all_of(envelopes,
                             [](const EnvelopePerformanceEvent* event) {
                               return event != nullptr && event->scope == VoiceEnvelopeScope::FutureAttacks;
                             }),
         "NDS D0-D3 should affect future attacks without changing active voices");
  expect(envelopes[0]->update.fields == EnvelopeFields::Attack && envelopes[0]->update.values &&
             envelopes[0]->update.values->attackSeconds == expected->attackSeconds &&
             envelopes[1]->update.fields == EnvelopeFields::Decay && envelopes[1]->update.values &&
             envelopes[1]->update.values->decaySeconds == expected->decaySeconds &&
             envelopes[2]->update.fields == EnvelopeFields::Sustain && envelopes[2]->update.values &&
             envelopes[2]->update.values->sustainAmplitude == expected->sustainAmplitude &&
             envelopes[3]->update.fields == EnvelopeFields::Release && envelopes[3]->update.values &&
             envelopes[3]->update.values->releaseSeconds == expected->releaseSeconds,
         "NDS D0-D3 should use the same raw-byte conversions as SBNK regions");
  expect(std::get<InstrumentPerformanceEvent>(events.back()).envelopeMode ==
             InstrumentEnvelopeMode::PreserveDynamicOverride,
         "NDS program changes should preserve the track's dynamic ADSR overrides");
}

void ndsSequenceDialectModelsNitroLfoRegisters() {
  const PerformanceSequence performance = renderTestPerformance({
      0xcb,
      0x20,  // speed 32 -> 12 Hz
      0xcd,
      0x02,  // range 2
      0xe0,
      0x30,
      0x00,  // 48 driver updates -> 250 ms
      0xca,
      0x40,  // depth 64
      0xcc,
      0x01,  // volume target
      0xcc,
      0x02,  // pan target
      0xff,
  });
  expect(performance.diagnostics.empty(), "NDS LFO-register fixture should render without diagnostics");

  const ModulationPerformanceEvent* pitch = nullptr;
  const ModulationPerformanceEvent* volume = nullptr;
  const ModulationPerformanceEvent* pan = nullptr;
  const ModulationPerformanceEvent* rate = nullptr;
  for (const auto& event : performance.tracks[0].events) {
    const auto* modulation = std::get_if<ModulationPerformanceEvent>(&event);
    if (modulation == nullptr) {
      continue;
    }
    if (modulation->target == ModulationPerformanceTarget::VibratoDepth && modulation->pitchDepthSemitones &&
        *modulation->pitchDepthSemitones > 0.0) {
      pitch = modulation;
    } else if (modulation->target == ModulationPerformanceTarget::TremoloDepth &&
               modulation->volumeDepthDecibels && *modulation->volumeDepthDecibels > 0.0) {
      volume = modulation;
    } else if (modulation->target == ModulationPerformanceTarget::PanDepth && modulation->panDepth &&
               *modulation->panDepth > 0.0) {
      pan = modulation;
    } else if (modulation->target == ModulationPerformanceTarget::VibratoRate &&
               modulation->delayMilliseconds == 250.0) {
      rate = modulation;
    }
  }

  expect(rate != nullptr && rate->frequencyHz == 12.0 && rate->delayTicks == 24 && rate->shape &&
             rate->shape->waveform == LfoWaveform::Sine && rate->phaseRunsAtZeroDepth,
         "NDS should retain Nitro's fixed-clock rate, delay, sine shape, and zero-depth phase behavior");
  expect(pitch != nullptr && pitch->pitchDepthSemitones && std::abs(*pitch->pitchDepthSemitones - 0.9921875) < 0.000001,
         "NDS pitch modulation should use Nitro's depth/range scaling");
  expect(
      volume != nullptr && volume->volumeDepthDecibels && std::abs(*volume->volumeDepthDecibels - 5.953125) < 0.000001,
      "NDS volume modulation should preserve Nitro's decibel-domain LFO depth");
  expect(pan != nullptr && pan->panDepth && std::abs(*pan->panDepth - 1.0) < 0.000001,
         "NDS pan modulation should preserve Nitro's pan-domain LFO depth");
}

void ndsSynthModulatorsUseSequenceLfoRanges() {
  expect(!ndsDefinition().module.prepareCollection,
         "NDS modulation should not require a format-specific collection preparer");
  const SequenceProgram program = decodeTestSequenceProgram({
      0xcb,
      0x20,  // speed 32 -> 12 Hz
      0xcd,
      0x02,  // range 2
      0xe0,
      0x30,
      0x00,  // 48 driver updates -> 250 ms
      0xca,
      0x40,  // depth 64
      0xcc,
      0x01,  // volume target
      0xcc,
      0x02,  // pan target
      0xcb,
      0x10,  // speed 16 -> 6 Hz
      0xff,
  });
  const PerformanceSequence performance = SequenceVm(LoopPolicy::PlayOnce).render(program, ndsSequenceDialect());
  const SequenceModulationProfile profile = analyzeSequenceModulation(performance);
  expect(profile.instruments.vibrato && profile.instruments.tremolo &&
             std::abs(profile.instruments.vibrato->maxDepthCents - 99.21875) < 0.000001 &&
             std::abs(profile.instruments.tremolo->maxDepthDb - 5.953125) < 0.000001 &&
             std::abs(profile.maxPanDepth - 1.0) < 0.000001,
         "shared LFO analysis should retain each NDS target's sequence-wide physical depth");
  expect(profile.instruments.vibrato->rateHertz.minimum == 6.0 &&
             profile.instruments.vibrato->rateHertz.maximum == 12.0 &&
             profile.instruments.vibrato->delaySeconds &&
             profile.instruments.vibrato->delaySeconds->minimum == 0.0 &&
             std::abs(profile.instruments.vibrato->delaySeconds->maximum - 0.25) < 0.000001,
         "shared LFO analysis should retain the NDS sequence's physical rate and delay range");

  const MidiSequence midi = renderMidiSequence(performance, MidiExportOptions{},
                                               ModulationConversionPolicy::SynthModulators, {}, &profile);
  u8 maxVibratoDepth = 0;
  u8 maxTremoloDepth = 0;
  u8 maxVibratoFrequency = 0;
  u32 maxVibratoDelay = 0;
  for (const MidiEvent& event : midi.tracks[0].events) {
    if (const auto* depth = std::get_if<VibratoDepth>(&event)) {
      maxVibratoDepth = std::max(maxVibratoDepth, depth->value);
    } else if (const auto* depth = std::get_if<TremoloDepth>(&event)) {
      maxTremoloDepth = std::max(maxTremoloDepth, depth->value);
    } else if (const auto* rate = std::get_if<VibratoFrequency>(&event)) {
      maxVibratoFrequency = std::max(maxVibratoFrequency, rate->value);
    } else if (const auto* delay = std::get_if<VibratoDelay>(&event)) {
      maxVibratoDelay = std::max(maxVibratoDelay, delay->ticks);
    }
  }
  expect(maxVibratoDepth == 127 && maxTremoloDepth == 127 && maxVibratoFrequency == 127 && maxVibratoDelay == 127,
         "NDS synth-modulator MIDI should span each changing physical LFO range");

  std::vector<u8> bytes(0x80);
  writeLe32(bytes, 0x38, 1);
  writeLe32(bytes, 0x3c, (0x40u << 8) | 0x01);
  writeLe16(bytes, 0x40, 0);
  writeLe16(bytes, 0x42, 0);
  bytes[0x44] = 60;
  bytes[0x45] = 0x6d;
  bytes[0x46] = 0x20;
  bytes[0x47] = 0x7f;
  bytes[0x48] = 0x7f;
  bytes[0x49] = 64;

  ScanIdAllocator ids;
  ScanInput input{
      .source = SourceFile{.id = SourceId{31}, .name = "modulation-bank.sbnk", .size = bytes.size()},
      .reader = ByteReader(SourceId{31}, bytes),
      .ids = ids,
  };
  ScanResultBuilder out(input, "NDS");
  const auto psg = addNdsPsgSamples(out);
  std::array<std::optional<ScanSampleCollectionDraft>, 4> waves{};
  waves[0] = psg;
  const auto bankRef = addNdsInstrumentSet(out, input.reader.range(0, bytes.size()), "Bank", psg, waves);
  expect(bankRef.has_value(), "NDS should build a bank for sequence-derived LFO metadata");

  const ScanResult result = out.finish();
  const auto* bank = assetWithId<InstrumentSetAsset>(result, bankRef->id());
  expect(bank != nullptr && bank->instruments.size() == 1,
         "NDS should retain every parsed instrument for shared collection preparation");
  InstrumentSetAsset preparedBank = *bank;
  applySequenceModulation(preparedBank, profile);
  const InstrumentModulation& modulation = preparedBank.instruments[0].modulation;
  expect(modulation.vibrato && modulation.tremolo,
         "NDS instruments should describe both Nitro pitch and volume LFO targets");
  expect(std::abs(modulation.vibrato->maxDepthCents - 99.21875) < 0.000001 &&
             modulation.vibrato->rateHertz.minimum == 6.0 && modulation.vibrato->rateHertz.maximum == 12.0 &&
             modulation.vibrato->delaySeconds && std::abs(modulation.vibrato->delaySeconds->maximum - 0.25) < 0.000001,
         "NDS vibrato metadata should use the same physical range as its MIDI controllers");
  expect(std::abs(modulation.tremolo->maxDepthDb - 5.953125) < 0.000001 &&
             modulation.tremolo->gainMode == TremoloGainMode::BipolarAroundNominal,
         "NDS tremolo metadata should preserve Nitro's bipolar decibel swing");

  const LoweredSynthModulation lowered = lowerSynthModulation(modulation);
  expect(std::ranges::any_of(lowered.modulators,
                             [](const SynthModulator& modulator) {
                               return !modulator.source && modulator.destination == SynthDestination::VibratoDepth &&
                                      modulator.amount == 99;
                             }),
         "NDS vibrato metadata should lower to an explicit synth depth modulator");
  expect(std::ranges::any_of(lowered.modulators,
                             [](const SynthModulator& modulator) {
                               return !modulator.source && modulator.destination == SynthDestination::TremoloDepth &&
                                      modulator.amount == 60;
                             }),
         "NDS tremolo metadata should lower to an explicit synth depth modulator");
}

void ndsSequenceDialectRevealsRunningSineLfoAtDepthChange() {
  const PerformanceSequence performance = renderTestPerformance({
      0xc7,
      0x00,  // notes do not advance the sequence clock
      0x3f,
      0x7f,
      0x19,  // note for 25 ticks
      0x80,
      0x04,  // leave the default zero-depth LFO running
      0xca,
      0x30,  // reveal depth 48 four ticks into the note
      0x80,
      0x13,
      0xca,
      0x00,
      0x80,
      0x02,
      0xff,
  });
  const MidiSequence midi =
      renderMidiSequence(performance, MidiExportOptions{}, ModulationConversionPolicy::SequenceEventSimulation);

  const auto bend = std::ranges::find_if(midi.tracks[0].events, [](const MidiEvent& event) {
    const auto* pitch = std::get_if<PitchBend>(&event);
    return pitch != nullptr && pitch->tick == 5;
  });
  expect(bend != midi.tracks[0].events.end() && std::get<PitchBend>(*bend).value == 1524,
         "NDS CA should reveal the already-running default 6 Hz sine LFO instead of restarting it");
}

void ndsSequenceDialectPreservesPortamentoTimingIntent() {
  const PerformanceSequence performance = renderTestPerformance({
      0xc7, 0x01,        // note wait
      0xc9, 0x3c,        // portamento source C4 (and enable)
      0xcf, 0x10,        // fixed-clock portamento time
      0x40, 0x7f, 0x20,  // E4 for 32 ticks
      0xcf, 0x00,        // note-relative portamento time
      0x43, 0x7f, 0x06,  // G4 for 6 ticks
      0xce, 0x00,        // portamento off
      0x45, 0x7f, 0x04,  // A4, no transition
      0xff,
  });
  expect(performance.diagnostics.empty(), "NDS portamento fixture should render without diagnostics");

  std::vector<const NotePerformanceEvent*> notes;
  for (const auto& event : performance.tracks[0].events) {
    if (const auto* note = std::get_if<NotePerformanceEvent>(&event)) {
      notes.push_back(note);
    }
  }
  expect(notes.size() == 3 && performance.tracks[0].automations.size() == 2,
         "NDS should retain source notes while creating transitions only when portamento is enabled");

  const auto* fixed = pitchTransitionIntent(performance.tracks[0].automations[0]);
  const auto* relative = pitchTransitionIntent(performance.tracks[0].automations[1]);
  expect(fixed != nullptr && fixed->note == notes[0]->note && !fixed->previousNote && fixed->startKey == 60.0 &&
             fixed->targetKey == 64.0 && fixed->timing.timelineTicks == 16 &&
             fixed->preferredRendering == PitchTransitionRenderingHint::Portamento &&
             std::holds_alternative<FixedDurationPitchSlideTiming>(fixed->timing.physical) &&
             std::abs(std::get<FixedDurationPitchSlideTiming>(fixed->timing.physical).milliseconds - (1000.0 / 6.0)) <
                 0.000001,
         "nonzero NDS portamento time should preserve its source key and 192 Hz distance-scaled duration");
  expect(relative != nullptr && relative->note == notes[1]->note && relative->startKey == 64.0 &&
             relative->targetKey == 67.0 && relative->timing.timelineTicks == 6 &&
             std::holds_alternative<TempoRelativePitchSlideTiming>(relative->timing.physical),
         "zero NDS portamento time should derive its transition duration from the note's sequence ticks");
  expect(std::ranges::none_of(performance.tracks[0].events,
                              [](const PerformanceEvent& event) {
                                return std::holds_alternative<PortamentoPerformanceEvent>(event) ||
                                       std::holds_alternative<PortamentoEnablePerformanceEvent>(event) ||
                                       std::holds_alternative<PortamentoTimePerformanceEvent>(event) ||
                                       std::holds_alternative<PortamentoControlPerformanceEvent>(event);
                              }),
         "NDS format code should retain transition intent instead of emitting MIDI portamento controls");

  const MidiSequence native = renderMidiSequence(performance);
  expect(std::ranges::count_if(
             native.tracks[0].events,
             [](const MidiEvent& event) { return std::holds_alternative<PortamentoControl>(event); }) == 2,
         "preserve-format MIDI should lower both NDS source portamento transitions natively");

  const MidiSequence bent =
      renderMidiSequence(performance, MidiExportOptions{.pitchTransitions = MidiPitchTransitionRendering::PitchBend});
  expect(std::ranges::none_of(bent.tracks[0].events,
                              [](const MidiEvent& event) {
                                return std::holds_alternative<PortamentoTime>(event) ||
                                       std::holds_alternative<PortamentoTime14>(event) ||
                                       std::holds_alternative<PortamentoControl>(event);
                              }) &&
             std::ranges::any_of(bent.tracks[0].events,
                                 [](const MidiEvent& event) {
                                   const auto* bend = std::get_if<PitchBend>(&event);
                                   return bend != nullptr && bend->value != 0;
                                 }),
         "pitch-bend lowering should reproduce NDS portamento without native portamento events");
}

void ndsSequenceDialectPreservesTiedSweepVoices() {
  const PerformanceSequence performance = renderTestPerformance({
      0xc7, 0x01,        // note wait
      0xc8, 0x01,        // tie on
      0x3c, 0x7f, 0x04,  // first tied note starts the channel
      0xe3, 0x80, 0xff,  // start subsequent notes two semitones low
      0x40, 0x7f, 0x04,  // tied E4 sweeps up over four ticks
      0xe3, 0x00, 0x00,  // clear sweep pitch
      0x43, 0x7f, 0x04,  // tied G4 changes immediately
      0xc8, 0x00,        // tie off
      0xff,
  });
  expect(performance.diagnostics.empty(), "NDS tied-sweep fixture should render without diagnostics");

  std::vector<const NotePerformanceEvent*> notes;
  for (const auto& event : performance.tracks[0].events) {
    if (const auto* note = std::get_if<NotePerformanceEvent>(&event)) {
      notes.push_back(note);
    }
  }
  expect(notes.size() == 3 && !notes[0]->extendsPrevious && notes[1]->extendsPrevious && notes[2]->extendsPrevious &&
             notes[0]->note == notes[1]->note && notes[1]->note == notes[2]->note,
         "NDS tie should retain one voice identity after the first attack");
  expect(performance.tracks[0].automations.size() == 2,
         "NDS tie should retain both the timed sweep and the following instantaneous key change");

  const auto* sweep = pitchTransitionIntent(performance.tracks[0].automations[0]);
  const auto* jump = pitchTransitionIntent(performance.tracks[0].automations[1]);
  expect(sweep != nullptr && sweep->note == notes[0]->note && sweep->startKey == 62.0 && sweep->targetKey == 64.0 &&
             sweep->timing.timelineTicks == 4 && sweep->preferredRendering == PitchTransitionRenderingHint::PitchBend,
         "NDS sweep pitch should use 1/64-semitone units and the zero-time note duration");
  expect(jump != nullptr && jump->note == notes[0]->note && jump->startKey == 64.0 && jump->targetKey == 67.0 &&
             jump->timing.timelineTicks == 0 && jump->preferredRendering == PitchTransitionRenderingHint::PitchBend,
         "a tied NDS note without a sweep should preserve its immediate attack-free pitch change");

  const MidiSequence midi = renderMidiSequence(performance);
  const auto noteCount = std::ranges::count_if(
      midi.tracks[0].events, [](const MidiEvent& event) { return std::holds_alternative<NoteDuration>(event); });
  const auto note = std::ranges::find_if(
      midi.tracks[0].events, [](const MidiEvent& event) { return std::holds_alternative<NoteDuration>(event); });
  expect(noteCount == 1 && note != midi.tracks[0].events.end() && std::get<NoteDuration>(*note).tick == 0 &&
             std::get<NoteDuration>(*note).duration == 12,
         "pitch-bend lowering should keep tied NDS key changes on one MIDI attack");
}

void ndsSequenceDialectExecutesCallAndReturn() {
  std::vector<u8> bytes(0x160);
  constexpr u32 sequenceOffset = 0x100;
  constexpr u32 trackStart = sequenceOffset + 0x1c;
  constexpr u32 subroutineOffset = trackStart + 0x10;
  constexpr u32 subroutineRelative = subroutineOffset - trackStart;

  bytes[trackStart + 0] = 0xc7;
  bytes[trackStart + 1] = 0x01;
  bytes[trackStart + 2] = 0x95;
  bytes[trackStart + 3] = static_cast<u8>(subroutineRelative & 0xff);
  bytes[trackStart + 4] = static_cast<u8>((subroutineRelative >> 8) & 0xff);
  bytes[trackStart + 5] = static_cast<u8>((subroutineRelative >> 16) & 0xff);
  bytes[trackStart + 6] = 0x80;
  bytes[trackStart + 7] = 0x07;
  bytes[trackStart + 8] = 0xff;

  bytes[subroutineOffset + 0] = 0x3c;
  bytes[subroutineOffset + 1] = 0x64;
  bytes[subroutineOffset + 2] = 0x05;
  bytes[subroutineOffset + 3] = 0xfd;

  const SequenceDialect& dialect = ndsSequenceDialect();
  SourceMapBuilder sourceMap;
  const TrackProgram track = decodeTestTrack(ByteReader(SourceId{5}, bytes), sequenceOffset, subroutineOffset + 4,
                                             trackStart, 0, false, &sourceMap);
  const SourceMap annotations = sourceMap.finish();
  expect(track.commands.size() == 6, "NDS call fixture should decode call target and fallthrough blocks");

  const auto call = std::ranges::find_if(track.commands, [&](const SourceCommand& command) {
    return commandDetailKind(annotations, command) == "nds.call";
  });
  expect(call != track.commands.end(), "NDS call fixture should preserve the call command");
  expect(call->range.offset == trackStart + 2 && call->range.size == 4,
         "NDS call command should preserve its source range");

  const SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .sourceBaseAddress = Address{trackStart},
      .tracks = {track},
  };
  const PerformanceSequence performance = SequenceVm(LoopPolicy::PlayOnce).render(program, dialect);
  expect(performance.diagnostics.empty(), "NDS call fixture should render without diagnostics");
  expect(performance.tracks.size() == 1 && performance.tracks[0].endTick == 12,
         "NDS call fixture should return to the fallthrough rest command");

  const auto note = std::ranges::find_if(performance.tracks[0].events, [](const PerformanceEvent& event) {
    const auto* typed = std::get_if<NotePerformanceEvent>(&event);
    return typed != nullptr;
  });
  expect(note != performance.tracks[0].events.end(), "NDS call fixture should emit the subroutine note");
  const auto& noteEvent = std::get<NotePerformanceEvent>(*note);
  expect(noteEvent.header.tick == 0 && noteEvent.key == 60.0 && noteEvent.durationTicks == 5,
         "NDS subroutine note should render at the call tick and use source duration");

  SourceMapBuilder linearizedSourceMap;
  const TrackProgram linearizedTrack = decodeTestTrack(ByteReader(SourceId{5}, bytes), sequenceOffset,
                                                       subroutineOffset + 4, trackStart, 0, true, &linearizedSourceMap);
  expect(linearizedTrack.commands.size() == 6,
         "NDS linearized call fixture should still decode call target and fallthrough blocks");
  const SequenceProgram linearizedProgram{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .sourceBaseAddress = Address{trackStart},
      .tracks = {linearizedTrack},
  };
  const PerformanceSequence linearizedPerformance = SequenceVm(LoopPolicy::PlayOnce).render(linearizedProgram, dialect);
  expect(linearizedPerformance.diagnostics.empty(),
         "NDS linearized call fixture should render without missing-target diagnostics");

  std::vector<u8> overlapBytes(0x140);
  overlapBytes[trackStart + 0] = 0x95;
  overlapBytes[trackStart + 1] = 0x05;
  overlapBytes[trackStart + 2] = 0x00;
  overlapBytes[trackStart + 3] = 0x00;
  overlapBytes[trackStart + 4] = 0xc1;
  overlapBytes[trackStart + 5] = 0x3c;
  overlapBytes[trackStart + 6] = 0x64;
  overlapBytes[trackStart + 7] = 0x01;
  overlapBytes[trackStart + 8] = 0xfd;
  SourceMapBuilder overlapSourceMap;
  const TrackProgram overlapTrack = decodeTestTrack(ByteReader(SourceId{8}, overlapBytes), sequenceOffset,
                                                    trackStart + 9, trackStart, 0, true, &overlapSourceMap);
  const SourceMap overlapAnnotations = overlapSourceMap.finish();
  expect(overlapTrack.commands.size() == 4,
         "NDS linearized overlap fixture should split fallthrough from call-target bytes");
  expect(overlapTrack.commands[1].range.offset == trackStart + 4 && overlapTrack.commands[1].encodedSize == 1,
         "NDS linearized overlap fixture should stop before overlapping a queued call target");
  const SourceAnnotation& recovery = commandAnnotation(overlapAnnotations, overlapTrack.commands[1]);
  expect(recovery.detailKind == "nds.recovery-stop" && recovery.sequenceSemantic == SequenceSemantic::Unsupported &&
             recovery.playbackStatus == CommandPlaybackStatus::StopsPlayback,
         "NDS linearized overlap fixture should annotate the synthetic recovery stop command");
  expect(recovery.parent && overlapAnnotations.get(*recovery.parent).role == SourceRole::SequenceTrack,
         "NDS synthetic recovery stop command should be parented under the recovered track annotation");
  const SequenceProgram overlapProgram{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .sourceBaseAddress = Address{trackStart},
      .tracks = {overlapTrack},
  };
  const PerformanceSequence overlapPerformance = SequenceVm(LoopPolicy::PlayOnce).render(overlapProgram, dialect);
  expect(overlapPerformance.diagnostics.empty(),
         "NDS linearized overlap fixture should render without unpaired-return diagnostics");
}

void ndsSequenceDialectDiscoversSecondaryTrackAddresses() {
  std::vector<u8> bytes(0x180);
  constexpr u32 sequenceOffset = 0x100;
  constexpr u32 trackStart = sequenceOffset + 0x1c;
  constexpr u32 primaryStart = trackStart + 8;
  constexpr u32 secondaryStart = trackStart + 0x20;
  constexpr u32 secondaryRelative = secondaryStart - trackStart;

  bytes[trackStart + 0] = 0xfe;
  bytes[trackStart + 1] = 0x00;
  bytes[trackStart + 2] = 0x00;
  bytes[trackStart + 3] = 0x93;
  bytes[trackStart + 4] = 0x02;
  bytes[trackStart + 5] = static_cast<u8>(secondaryRelative & 0xff);
  bytes[trackStart + 6] = static_cast<u8>((secondaryRelative >> 8) & 0xff);
  bytes[trackStart + 7] = static_cast<u8>((secondaryRelative >> 16) & 0xff);
  bytes[primaryStart] = 0xff;

  bytes[secondaryStart + 0] = 0x80;
  bytes[secondaryStart + 1] = 0x03;
  bytes[secondaryStart + 2] = 0xff;

  SourceMapBuilder sourceMap;
  const SequenceProgramAsset asset =
      decodeTestProgram(ByteReader(SourceId{6}, bytes), sequenceOffset, secondaryStart + 3, false, &sourceMap);
  expect(asset.program.tracks.size() == 2 && asset.program.tracks[0].startAddress.value == primaryStart &&
             asset.program.tracks[0].commands.size() == 1 &&
             asset.program.tracks[1].startAddress.value == secondaryStart,
         "NDS SSEQ bootstrap should discover both track starts without becoming part of the primary track");
  const TrackProgram& secondary = asset.program.tracks[1];
  const SourceMap annotations = sourceMap.finish();
  expect(secondary.sourceTrackNumber == 1 && secondary.commands.size() == 2,
         "NDS secondary track should decode independently from the primary bootstrap");
  expect(commandDetailKind(annotations, secondary.commands[0]) == "nds.rest",
         "NDS secondary track should preserve decoded source commands");
}

void ndsSequenceTrackAddressDiscoveryKeepsMalformedBootstrapCommands() {
  std::vector<u8> bytes(0x130);
  constexpr u32 sequenceOffset = 0x100;
  constexpr u32 trackStart = sequenceOffset + 0x1c;

  bytes[trackStart + 0] = 0xfe;
  bytes[trackStart + 1] = 0x00;
  bytes[trackStart + 2] = 0x00;
  bytes[trackStart + 3] = 0x80;
  bytes[trackStart + 4] = 0x81;

  SourceMapBuilder sourceMap;
  const SequenceProgramAsset asset =
      decodeTestProgram(ByteReader(SourceId{12}, bytes), sequenceOffset, trackStart + 5, false, &sourceMap);
  expect(asset.program.tracks.size() == 1 && asset.program.tracks.front().startAddress.value == trackStart + 3,
         "NDS track discovery should keep an unterminated bootstrap delay as the primary track");
  const TrackProgram& track = asset.program.tracks.front();
  const SourceMap annotations = sourceMap.finish();
  expect(track.commands.size() == 1 && commandDetailKind(annotations, track.commands[0]) == "nds.truncated",
         "NDS malformed bootstrap command should be preserved as a truncated source command");
}

void ndsSequenceDialectAnnotatesModulationDelayOperands() {
  std::vector<u8> bytes(0x130);
  constexpr u32 sequenceOffset = 0x100;
  constexpr u32 trackStart = sequenceOffset + 0x1c;

  bytes[trackStart + 0] = 0xe0;
  bytes[trackStart + 1] = 0x12;
  bytes[trackStart + 2] = 0x34;
  bytes[trackStart + 3] = 0xff;

  SourceMapBuilder sourceMap;
  const TrackProgram track =
      decodeTestTrack(ByteReader(SourceId{7}, bytes), sequenceOffset, trackStart + 4, trackStart, 0, false, &sourceMap);
  const SourceMap annotations = sourceMap.finish();
  expect(track.commands.size() == 2,
         "NDS modulation-delay fixture should decode the modulation command and end command");

  const SourceCommand& command = track.commands[0];
  expect(commandDetailKind(annotations, command) == "nds.modulation-delay",
         "NDS modulation delay should stay annotated as its source-driver command");
  expect(command.encodedSize == 3 && command.execution.valid(),
         "NDS modulation delay should retain executable playback behavior");
  const SemanticOperand* delay = semanticOperand(command, "delay");
  expect(delay != nullptr && std::get<u64>(delay->value) == 0x3412,
         "NDS modulation delay should retain its little-endian operand");

  const SourceField* delayField = fieldWithName(commandAnnotation(annotations, command), "delay");
  expect(fieldEquals(delayField, u64{0x3412}),
         "NDS modulation delay should preserve its operand as source annotation data");
  expect(delayField->range.offset == trackStart + 1 && delayField->range.size == 2,
         "NDS modulation-delay annotation should preserve its source range");
}

void ndsSequenceDialectAnnotatesPartialModulationDelayOperands() {
  std::vector<u8> bytes(0x130);
  constexpr u32 sequenceOffset = 0x100;
  constexpr u32 trackStart = sequenceOffset + 0x1c;

  bytes[trackStart + 0] = 0xe0;
  bytes[trackStart + 1] = 0x12;

  SourceMapBuilder sourceMap;
  std::vector<Diagnostic> diagnostics;
  const TrackProgram track = decodeTestTrack(ByteReader(SourceId{15}, bytes), sequenceOffset, trackStart + 2,
                                             trackStart, 0, false, &sourceMap, &diagnostics);
  const SourceMap annotations = sourceMap.finish();
  expect(track.commands.size() == 1, "NDS partial modulation-delay fixture should decode one truncated command");
  expect(commandDetailKind(annotations, track.commands[0]) == "nds.truncated",
         "NDS partial modulation delay should use the truncated-command fallback");
  expect(track.commands[0].range.size == 2,
         "NDS partial modulation delay should preserve the opcode and available operand source range");
  expect(diagnostics.size() == 1 && diagnostics[0].message == "Truncated field 'delay'",
         "NDS partial modulation delay should diagnose the missing operand byte");
}

void ndsSequenceDialectKeepsEmptyPlaceholderTrack() {
  std::vector<u8> bytes(0x130);
  constexpr u32 sequenceOffset = 0x100;
  constexpr u32 trackStart = sequenceOffset + 0x1c;

  const SequenceProgramAsset asset = decodeTestProgram(ByteReader(SourceId{8}, bytes), sequenceOffset, trackStart);
  expect(asset.program.tracks.size() == 1 && asset.program.tracks.front().startAddress.value == trackStart &&
             asset.program.tracks.front().commands.empty(),
         "NDS empty placeholder sequences should keep one empty primary track");
}

void ndsSequenceDialectMarksUnterminatedVarLenAsTruncated() {
  std::vector<u8> bytes(0x130);
  constexpr u32 sequenceOffset = 0x100;
  constexpr u32 trackStart = sequenceOffset + 0x1c;

  bytes[trackStart + 0] = 0x80;
  bytes[trackStart + 1] = 0x81;

  SourceMapBuilder sourceMap;
  const TrackProgram track = decodeTestTrack(ByteReader(SourceId{10}, bytes), sequenceOffset, trackStart + 2,
                                             trackStart, 0, false, &sourceMap);
  const SourceMap annotations = sourceMap.finish();
  expect(track.commands.size() == 1, "NDS unterminated variable-length command should decode as one command");
  expect(commandDetailKind(annotations, track.commands[0]) == "nds.truncated",
         "NDS unterminated variable-length command should use the truncated-command fallback");
  expect(track.commands[0].range.size == 2, "NDS truncated command should preserve its available partial source range");
}

void ndsSequenceDialectDoesNotLinkInvalidControlTargets() {
  constexpr u32 sequenceOffset = 0x100;
  constexpr u32 trackStart = sequenceOffset + 0x1c;
  constexpr u32 invalidRelativeTarget = 0x80;
  constexpr u32 sequenceEnd = trackStart + 4;
  const auto checkInvalidTarget = [&](u8 opcode, SequenceSemantic semantic, SourceLinkRole role,
                                      std::string_view detailKind, std::string_view warning) {
    std::vector<u8> bytes(0x1c0);
    bytes[trackStart + 0] = opcode;
    bytes[trackStart + 1] = static_cast<u8>(invalidRelativeTarget & 0xff);
    bytes[trackStart + 2] = static_cast<u8>((invalidRelativeTarget >> 8) & 0xff);
    bytes[trackStart + 3] = static_cast<u8>((invalidRelativeTarget >> 16) & 0xff);

    ScanIdAllocator ids;
    SourceMapBuilder sourceMap([&ids]() { return ids.nextSourceAnnotationId(); });
    std::vector<Diagnostic> diagnostics;
    const TrackProgram track = decodeTestTrack(ByteReader(SourceId{14}, bytes), sequenceOffset, sequenceEnd, trackStart,
                                               0, false, &sourceMap, &diagnostics);

    const SourceMap annotations = sourceMap.finish();
    expect(track.commands.size() == 1 && commandDetailKind(annotations, track.commands[0]) == detailKind,
           "NDS invalid control target should preserve the source command");
    expect(diagnostics.size() == 1 && diagnostics[0].message == warning,
           "NDS invalid control target should report a decode warning");
    expect(diagnostics[0].range && diagnostics[0].range->source == SourceId{14} &&
               diagnostics[0].range->offset == trackStart && diagnostics[0].range->size == 4,
           "NDS invalid control-target diagnostic should use the command range");

    const auto commandAnnotations = annotations.withSequenceSemantic(SourceId{14}, semantic);
    expect(commandAnnotations.size() == 1, "NDS invalid control target should publish a source annotation");
    const SourceAnnotation& command = annotations.get(commandAnnotations.front());
    expect(fieldEquals(fieldWithName(command, "destination"), u64{trackStart + invalidRelativeTarget}),
           "NDS invalid control target should keep the decoded destination operand field");
    const auto link =
        std::ranges::find_if(command.links, [role](const SourceLink& sourceLink) { return sourceLink.role == role; });
    expect(link == command.links.end(), "NDS invalid control target should not publish an invalid source link");
  };

  checkInvalidTarget(0x94, SequenceSemantic::Jump, SourceLinkRole::JumpTarget, "nds.jump",
                     "Jump target outside sequence data");
  checkInvalidTarget(0x95, SequenceSemantic::Call, SourceLinkRole::CallTarget, "nds.call",
                     "Call target outside sequence data");

  std::vector<u8> truncatedBytes(trackStart + 2);
  truncatedBytes[trackStart] = 0x94;
  truncatedBytes[trackStart + 1] = 0x01;
  SourceMapBuilder truncatedSourceMap;
  const TrackProgram truncated = decodeTestTrack(ByteReader(SourceId{14}, truncatedBytes), sequenceOffset,
                                                 trackStart + 2, trackStart, 0, false, &truncatedSourceMap);
  const SourceMap truncatedAnnotations = truncatedSourceMap.finish();
  expect(truncated.commands.size() == 1 &&
             commandDetailKind(truncatedAnnotations, truncated.commands[0]) == "nds.truncated",
         "NDS partial control target should remain a truncated source command");
  const SourceAnnotation& truncatedCommand = commandAnnotation(truncatedAnnotations, truncated.commands[0]);
  expect(std::ranges::none_of(truncatedCommand.links,
                              [](const SourceLink& link) {
                                return link.role == SourceLinkRole::JumpTarget ||
                                       link.role == SourceLinkRole::CallTarget;
                              }),
         "NDS partial control target should not publish a link from an incomplete operand");
}

void ndsMalformedRecoveryKeepsExecutableJumps() {
  std::vector<u8> bytes(0x180);
  constexpr u32 sequenceOffset = 0x100;
  constexpr u32 trackStart = sequenceOffset + 0x1c;
  constexpr u32 subroutineOffset = trackStart + 0x20;
  constexpr u32 subroutineRelative = subroutineOffset - trackStart;

  bytes[trackStart + 0] = 0xc7;
  bytes[trackStart + 1] = 0x01;
  bytes[trackStart + 2] = 0x95;
  bytes[trackStart + 3] = static_cast<u8>(subroutineRelative & 0xff);
  bytes[trackStart + 4] = static_cast<u8>((subroutineRelative >> 8) & 0xff);
  bytes[trackStart + 5] = static_cast<u8>((subroutineRelative >> 16) & 0xff);
  bytes[trackStart + 6] = 0x80;
  bytes[trackStart + 7] = 0x01;
  bytes[trackStart + 8] = 0xff;

  bytes[subroutineOffset + 0] = 0x3c;
  bytes[subroutineOffset + 1] = 0x64;
  bytes[subroutineOffset + 2] = 0x02;
  bytes[subroutineOffset + 3] = 0x94;
  bytes[subroutineOffset + 4] = static_cast<u8>(subroutineRelative & 0xff);
  bytes[subroutineOffset + 5] = static_cast<u8>((subroutineRelative >> 8) & 0xff);
  bytes[subroutineOffset + 6] = static_cast<u8>((subroutineRelative >> 16) & 0xff);

  const SequenceDialect& dialect = ndsSequenceDialect();
  SourceMapBuilder sourceMap;
  const TrackProgram track = decodeTestTrack(ByteReader(SourceId{9}, bytes), sequenceOffset, subroutineOffset + 7,
                                             trackStart, 0, true, &sourceMap);
  const SourceMap annotations = sourceMap.finish();
  const auto jump = std::ranges::find_if(track.commands, [&](const SourceCommand& command) {
    return commandDetailKind(annotations, command) == "nds.jump";
  });
  expect(jump != track.commands.end(), "NDS malformed recovery should preserve recovered jumps as jump commands");

  const SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .sourceBaseAddress = Address{trackStart},
      .behavior = SequenceProgramBehavior{.commandLimit = 64},
      .tracks = {track},
  };
  const PerformanceSequence performance = SequenceVm(LoopPolicy::PlayOnce).render(program, dialect);
  expect(performance.diagnostics.empty(), "NDS recovered jump loop should stop without hitting the command limit");
  expect(performance.tracks.size() == 1 && performance.tracks[0].endTick == 2,
         "NDS recovered jump loop should render one pass through the subroutine loop");
}

void ndsSynthParserConvertsMaximumReleaseRate() {
  std::vector<u8> bytes(0x80);
  writeLe32(bytes, 0x38, 1);
  writeLe32(bytes, 0x3c, (0x40u << 8) | 0x01);
  writeLe16(bytes, 0x40, 0);
  writeLe16(bytes, 0x42, 0);
  bytes[0x44] = 60;
  bytes[0x45] = 0x6d;
  bytes[0x46] = 0x20;
  bytes[0x47] = 0x7f;
  bytes[0x48] = 0x7f;
  bytes[0x49] = 64;

  ScanIdAllocator ids;
  ScanInput input{
      .source = SourceFile{.id = SourceId{11}, .name = "bank.sbnk", .size = bytes.size()},
      .reader = ByteReader(SourceId{11}, bytes),
      .ids = ids,
  };
  ScanResultBuilder out(input, "NDS");
  const auto psg = addNdsPsgSamples(out);
  std::array<std::optional<ScanSampleCollectionDraft>, 4> waves{};
  waves[0] = psg;

  const auto bankRef = addNdsInstrumentSet(out, input.reader.range(0, bytes.size()), "Bank", psg, waves);
  expect(bankRef.has_value(), "NDS synth parser should add a valid instrument bank");

  bytes[0x45] = 0x80;
  const auto malformedBankRef =
      addNdsInstrumentSet(out, input.reader.range(0, bytes.size()), "Malformed Bank", psg, waves);
  expect(malformedBankRef.has_value(), "NDS synth parser should retain an empty malformed bank");

  const ScanResult result = out.finish();
  const auto* bank = assetWithId<InstrumentSetAsset>(result, bankRef->id());
  const auto* malformedBank = assetWithId<InstrumentSetAsset>(result, malformedBankRef->id());
  expect(bank != nullptr && bank->instruments.size() == 1 && bank->instruments[0].regions.size() == 1,
         "NDS synth parser should keep a valid instrument with the maximum release rate");
  const Envelope& envelope = bank->instruments[0].regions[0].envelope;
  constexpr double expectedReleaseSeconds = (0x16980 / 0xffff) * ((2728.0 * 64.0) / 33513982.0);
  expect(envelope.releaseSeconds && std::isfinite(*envelope.releaseSeconds) &&
             std::abs(*envelope.releaseSeconds - expectedReleaseSeconds) < 0.000001,
         "NDS release 0x7f should convert to one short finite envelope interval");
  expect(validateInstrumentSet(*bank).empty(), "NDS maximum release rate should pass synth validation");
  expect(malformedBank != nullptr && malformedBank->instruments.empty(),
         "NDS synth parser should skip regions with malformed envelope-rate bytes");

  const SourceMap& annotations = result.sourceMap;
  const auto* pointerTable =
      annotationWithKind(annotations, SourceId{11}, SourceRole::Table, "sbnk-instrument-pointer-table");
  expect(pointerTable != nullptr && pointerTable->range.offset == 0x38 && pointerTable->range.size == 8,
         "NDS SBNK parser should annotate the instrument pointer table");
  const auto* pointer = annotationWithKind(annotations, SourceId{11}, SourceRole::Pointer, "sbnk-instrument-pointer");
  expect(pointer != nullptr && pointer->range.offset == 0x3c && pointer->range.size == 4,
         "NDS SBNK parser should annotate instrument pointers");
  const auto* instrument = annotationWithKind(annotations, SourceId{11}, SourceRole::Instrument, "sbnk-instrument");
  expect(instrument != nullptr && instrument->range.offset == 0x40 && instrument->range.size == 10,
         "NDS SBNK parser should annotate parsed instrument rows");
  const auto sampleLink = std::ranges::find_if(
      instrument->links, [](const SourceLink& link) { return link.role == SourceLinkRole::UsesSample; });
  expect(sampleLink != instrument->links.end(), "NDS instrument annotations should link to referenced samples");
  const auto* region = annotationWithKind(annotations, SourceId{11}, SourceRole::Region, "sbnk-region");
  expect(region != nullptr && region->range.offset == 0x40 && region->range.size == 10,
         "NDS region annotations should preserve their exact source range");
  const auto sampleIndex = std::ranges::find(region->fields, "sample_index", &SourceField::name);
  const auto attack = std::ranges::find(region->fields, "attack", &SourceField::name);
  expect(sampleIndex != region->fields.end() && sampleIndex->range.offset == 0x40 && sampleIndex->range.size == 2 &&
             attack != region->fields.end() && attack->range.offset == 0x45 && attack->range.size == 1,
         "NDS region annotations should preserve exact sample and articulation fields");
}

void ndsSynthParserDerivesAdpcmLengthsSafely() {
  std::vector<u8> bytes(0x60);
  bytes[0] = 'S';
  bytes[1] = 'W';
  bytes[2] = 'A';
  bytes[3] = 'R';
  bytes[4] = 0xff;
  bytes[5] = 0xfe;
  bytes[6] = 0x00;
  bytes[7] = 0x01;
  writeLe32(bytes, 0x38, 1);
  writeLe32(bytes, 0x3c, 0x40);
  bytes[0x40] = 2;
  bytes[0x41] = 0;
  writeLe16(bytes, 0x42, 32768);
  writeLe16(bytes, 0x44, 0);
  writeLe16(bytes, 0x46, 0);
  writeLe16(bytes, 0x48, 2);
  bytes[0x50] = 0;
  bytes[0x51] = 0;
  bytes[0x52] = 0;
  bytes[0x53] = 0;

  ScanIdAllocator ids;
  ScanInput input{
      .source = SourceFile{.id = SourceId{12}, .name = "wave.swar", .size = bytes.size()},
      .reader = ByteReader(SourceId{12}, bytes),
      .ids = ids,
  };

  ScanResultBuilder out(input, "NDS");
  const auto waveRef = addNdsWaveArchive(out, input.reader.range(0, bytes.size()), "Wave");
  expect(waveRef.has_value(), "NDS parser should add a valid SWAR");

  bytes[0x41] = 1;
  const auto malformedLoopRef = addNdsWaveArchive(out, input.reader.range(0, bytes.size()), "Malformed Wave");
  expect(malformedLoopRef.has_value(), "NDS parser should retain a SWAR containing an invalid loop");

  const ScanResult result = out.finish();
  const auto* wave = assetWithId<SampleCollectionAsset>(result, waveRef->id());
  const auto* malformedLoop = assetWithId<SampleCollectionAsset>(result, malformedLoopRef->id());
  expect(wave != nullptr && wave->samples.samples.size() == 1,
         "NDS parser should keep non-looping ADPCM with loop offset zero");
  const Sample& sample = wave->samples.samples[0];
  expect(sample.encodedData.offset == 0x50 && sample.encodedData.size == 4,
         "NDS ADPCM encoded data should skip the predictor header");
  expect(!sample.loop.enabled && sample.loop.start == 0 && sample.loop.length == 9,
         "NDS non-looping ADPCM should keep sane decoded loop metadata");
  const auto* waveHeader = annotationWithKind(result.sourceMap, SourceId{12}, SourceRole::Header, "swar-header");
  expect(waveHeader != nullptr && waveHeader->range.offset == 0 && waveHeader->range.size == 0x3c,
         "NDS SWAR parser should annotate the archive header");
  const auto* sampleTable =
      annotationWithKind(result.sourceMap, SourceId{12}, SourceRole::Table, "swar-sample-offset-table");
  expect(sampleTable != nullptr && sampleTable->range.offset == 0x3c && sampleTable->range.size == 4,
         "NDS SWAR parser should annotate the sample offset table");
  const auto* sampleHeader =
      annotationWithKind(result.sourceMap, SourceId{12}, SourceRole::Sample, "swar-sample-header");
  expect(sampleHeader != nullptr && sampleHeader->range.offset == 0x40 && sampleHeader->range.size == 0x0c,
         "NDS SWAR parser should annotate parsed sample headers");

  expect(malformedLoop != nullptr && malformedLoop->samples.samples.empty(),
         "NDS parser should skip looped ADPCM with an unusable loop offset instead of underflowing");
}

void ndsWaveArchiveReportsTruncatedSampleHeaders() {
  std::vector<u8> bytes(0x44);
  bytes[0] = 'S';
  bytes[1] = 'W';
  bytes[2] = 'A';
  bytes[3] = 'R';
  bytes[4] = 0xff;
  bytes[5] = 0xfe;
  bytes[6] = 0x00;
  bytes[7] = 0x01;
  writeLe32(bytes, 0x38, 1);
  writeLe32(bytes, 0x3c, 0x40);

  ScanIdAllocator ids;
  ScanInput input{
      .source = SourceFile{.id = SourceId{13}, .name = "truncated-wave.swar", .size = bytes.size()},
      .reader = ByteReader(SourceId{13}, bytes),
      .ids = ids,
  };
  ScanResultBuilder out(input, "NDS");

  const auto waveRef = addNdsWaveArchive(out, input.reader.range(0, bytes.size()), "Truncated Wave");
  expect(waveRef.has_value(), "NDS parser should retain a SWAR with truncated samples");

  const ScanResult result = out.finish();
  const auto* wave = assetWithId<SampleCollectionAsset>(result, waveRef->id());
  expect(wave != nullptr && wave->samples.samples.empty(), "NDS parser should skip truncated SWAR sample headers");
  expect(!result.diagnostics.empty(), "NDS parser should diagnose truncated SWAR sample headers");
  expect(result.diagnostics[0].message.find("SWAR sample header") != std::string::npos,
         "NDS SWAR diagnostic should name the truncated field");
}

void ndsSynthBuilderPreservesSparseWaveIndexesAcrossArchives() {
  std::vector<u8> bytes(0x300);
  const auto writeArchive = [&](u32 base, std::span<const u32> offsets) {
    writeText(bytes, base, std::string_view{"SWAR\xff\xfe\x00\x01", 8});
    writeLe32(bytes, base + 0x38, static_cast<u32>(offsets.size()));
    for (u32 i = 0; i < offsets.size(); ++i) {
      writeLe32(bytes, base + 0x3c + i * 4, offsets[i]);
    }
  };
  const auto writePcm8 = [&](u32 base, u32 relativeOffset) {
    const u32 offset = base + relativeOffset;
    bytes[offset] = 0;
    bytes[offset + 1] = 0;
    writeLe16(bytes, offset + 2, 8000);
    writeLe16(bytes, offset + 4, 0);
    writeLe16(bytes, offset + 6, 0);
    writeLe16(bytes, offset + 8, 1);
  };

  constexpr std::array<u32, 3> firstOffsets{0x50, 0xfc, 0x70};
  writeArchive(0x000, firstOffsets);
  writePcm8(0x000, 0x50);
  writePcm8(0x000, 0x70);
  constexpr std::array<u32, 1> secondOffsets{0x50};
  writeArchive(0x100, secondOffsets);
  writePcm8(0x100, 0x50);

  // Program 1 refers to the malformed sample and must be skipped. Programs 2
  // and 4 remain and reference two different SWAR slots.
  writeLe32(bytes, 0x238, 5);
  writeLe32(bytes, 0x240, (0x80u << 8) | 0x01);
  writeLe32(bytes, 0x244, (0x60u << 8) | 0x01);
  writeLe32(bytes, 0x24c, (0x70u << 8) | 0x01);
  const auto writeInstrument = [&](u32 offset, u16 sampleIndex, u16 archiveSlot) {
    writeLe16(bytes, offset, sampleIndex);
    writeLe16(bytes, offset + 2, archiveSlot);
    bytes[offset + 4] = 60;
    bytes[offset + 5] = 0x6d;
    bytes[offset + 6] = 0x20;
    bytes[offset + 7] = 0x7f;
    bytes[offset + 8] = 0x7f;
    bytes[offset + 9] = 64;
  };
  writeInstrument(0x280, 1, 0);
  writeInstrument(0x260, 2, 0);
  writeInstrument(0x270, 0, 2);

  ScanIdAllocator ids;
  ScanInput input{
      .source = SourceFile{.id = SourceId{14}, .name = "sparse-synth.bin", .size = bytes.size()},
      .reader = ByteReader(SourceId{14}, bytes),
      .ids = ids,
  };
  ScanResultBuilder out(input, "NDS");
  const auto psg = addNdsPsgSamples(out);
  const auto firstWave = addNdsWaveArchive(out, input.reader.range(0x000, 0x100), "Sparse Wave");
  const auto secondWave = addNdsWaveArchive(out, input.reader.range(0x100, 0x100), "Second Wave");
  expect(firstWave && secondWave, "NDS builder fixture should create both wave archives");
  const auto laterFirstWaveSample = firstWave->find(2);
  expect(!firstWave->find(1) && laterFirstWaveSample && laterFirstWaveSample->index == 1,
         "a skipped SWAR entry must not shift the lookup for a later source sample index");

  std::array<std::optional<ScanSampleCollectionDraft>, 4> waves{};
  waves[0] = *firstWave;
  waves[2] = *secondWave;
  const auto bankRef = addNdsInstrumentSet(out, input.reader.range(0x200, 0x100), "Sparse Bank", psg, waves);
  expect(bankRef.has_value(), "NDS builder fixture should create its instrument bank");
  const ScanResult result = out.finish();

  const auto* firstSamples = assetWithId<SampleCollectionAsset>(result, firstWave->id());
  const auto* bank = assetWithId<InstrumentSetAsset>(result, bankRef->id());
  expect(firstSamples != nullptr && firstSamples->samples.samples.size() == 2,
         "NDS builder should retain the two valid samples around a malformed SWAR entry");
  expect(bank != nullptr && bank->instruments.size() == 2,
         "sparse SBNK programs should become two dense instruments without losing their program addresses");
  expect(std::ranges::any_of(result.diagnostics,
                             [](const Diagnostic& diagnostic) {
                               return diagnostic.message == "Sample 1 in wave archive slot 0 was not found";
                             }),
         "an SBNK reference to a rejected SWAR entry should produce an understandable warning");
  expect(bank->instruments[0].explicitAddress == InstrumentAddress{.bank = 0, .program = 2} &&
             bank->instruments[0].regions[0].sample.collection == firstWave->id() &&
             bank->instruments[0].regions[0].sample.index == 1,
         "SBNK program 2 should resolve source sample 2 to its actual dense sample index");
  expect(bank->instruments[1].explicitAddress == InstrumentAddress{.bank = 0, .program = 4} &&
             bank->instruments[1].regions[0].sample.collection == secondWave->id() &&
             bank->instruments[1].regions[0].sample.index == 0,
         "an SBNK should resolve a region through any of its four independent SWAR slots");

  expect(result.sourceMap.ownedBy(ObjectRefs::instrument(bankRef->id(), 0)).size() == 1 &&
             result.sourceMap.ownedBy(ObjectRefs::instrument(bankRef->id(), 2)).empty(),
         "sparse SBNK program numbers must not leak into dense instrument annotation owners");
  expect(result.sourceMap.ownedBy(ObjectRefs::sample(firstWave->id(), 1)).size() == 1 &&
             result.sourceMap.ownedBy(ObjectRefs::sample(firstWave->id(), 2)).empty(),
         "sparse SWAR source indexes must not leak into dense sample annotation owners");
  expect(result.sourceMap.ownedBy(ObjectRefs::region(bankRef->id(), 0, 0)).size() == 1,
         "NDS regions should retain stable instrument and region ownership");
}
