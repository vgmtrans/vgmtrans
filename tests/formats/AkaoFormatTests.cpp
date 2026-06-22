/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/Akao/AkaoInstrumentSet.h"
#include "value/formats/Akao/AkaoResolver.h"
#include "value/formats/Akao/AkaoSequenceDecoder.h"
#include "value/sequence/SequenceVm.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace vgmtrans::core;
using namespace vgmtrans::formats::akao;

namespace {

void expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void writeLe16(std::vector<u8>& bytes, size_t offset, u16 value) {
  bytes[offset] = static_cast<u8>(value & 0xff);
  bytes[offset + 1] = static_cast<u8>(value >> 8);
}

void writeLeS16(std::vector<u8>& bytes, size_t offset, s16 value) {
  writeLe16(bytes, offset, static_cast<u16>(value));
}

bool hasCommandKind(const SequenceDialect& dialect, const TrackProgram& track, std::string_view detailKind,
                    u32 offset) {
  return std::ranges::any_of(track.commands, [&](const SourceCommand& command) {
    return command.range.offset == offset && dialect.describe(track, command).detailKind == detailKind;
  });
}

TrackProgram decodeFixtureTrack(const std::vector<u8>& bytes, AkaoPs1Version version, u32 start, u32 end) {
  const SequenceDialect dialect = makeAkaoDialect(version);
  return decodeAkaoTrack(ByteReader(SourceId{20}, bytes), dialect,
                         CursorTrackDecodeInput{
                             .startOffset = start,
                             .bytecodeEnd = end,
                             .sequenceEnd = end,
                             .maxCommands = 64,
                         });
}

AkaoSequenceAnalysis analyzeFixtureTrack(const std::vector<u8>& bytes, AkaoPs1Version version, u32 start, u32 end) {
  AkaoSequenceAnalysis analysis;
  analysis.header = AkaoSequenceHeader{
      .offset = 0,
      .length = end,
      .version = version,
  };
  analyzeAkaoTrack(ByteReader(SourceId{20}, bytes), analysis, start);
  return analysis;
}

}  // namespace

void akaoDialectDecodesLegacyRelativeJumpTargets() {
  std::vector<u8> bytes(0x40, 0xa0);
  constexpr u32 start = 0x20;
  constexpr u32 target = 0x30;
  bytes[start] = 0xee;
  writeLeS16(bytes, start + 1, static_cast<s16>(target - (start + 1 + 2)));
  bytes[target] = 0xa0;

  const SequenceDialect dialect = makeAkaoDialect(AkaoPs1Version::Version1_0);
  const TrackProgram track = decodeAkaoTrack(ByteReader(SourceId{20}, bytes), dialect,
                                             CursorTrackDecodeInput{.startOffset = start, .bytecodeEnd = 0x40});
  expect(track.commands.size() == 2, "Akao legacy jump should decode the jump command and its target block");
  expect(hasCommandKind(dialect, track, "akao-ps1-1.0.jump", start),
         "Akao legacy jump should have an explicit command kind");
  expect(hasCommandKind(dialect, track, "akao-ps1-1.0.end", target),
         "Akao legacy jump should expose the static target to the cursor walker");
}

void akaoDialectDecodesConditionalBranchSideTargets() {
  std::vector<u8> bytes(0x70, 0xa0);
  constexpr u32 start = 0x40;
  constexpr u32 fallthrough = 0x45;
  constexpr u32 target = 0x50;
  bytes[start] = 0xfe;
  bytes[start + 1] = 0x07;
  bytes[start + 2] = 0x01;
  writeLeS16(bytes, start + 3, static_cast<s16>(target - (start + 3)));
  bytes[fallthrough] = 0xa0;
  bytes[target] = 0xa0;

  const SequenceDialect dialect = makeAkaoDialect(AkaoPs1Version::Version3_2);
  const TrackProgram track = decodeAkaoTrack(ByteReader(SourceId{21}, bytes), dialect,
                                             CursorTrackDecodeInput{.startOffset = start, .bytecodeEnd = 0x70});
  expect(track.commands.size() == 3, "Akao conditional branch should decode both fallthrough and side-target blocks");
  expect(hasCommandKind(dialect, track, "akao-ps1-3.2.cpu-conditional-jump", start),
         "Akao conditional branch should have an explicit command kind");
  expect(hasCommandKind(dialect, track, "akao-ps1-3.2.end", fallthrough),
         "Akao conditional branch should preserve fallthrough flow");
  expect(hasCommandKind(dialect, track, "akao-ps1-3.2.end", target),
         "Akao conditional branch should expose the branch target as static flow");
}

void akaoSequenceAnalysisUsesCommandReaderFacts() {
  std::vector<u8> bytes(0x90, 0xa0);
  constexpr u32 start = 0x20;
  constexpr u32 customTable = 0x60;
  constexpr u32 drumTable = 0x70;
  bytes[start] = 0xfc;
  writeLeS16(bytes, start + 1, static_cast<s16>(customTable - (start + 1 + 2)));
  bytes[start + 3] = 0xec;
  writeLeS16(bytes, start + 4, static_cast<s16>(drumTable - (start + 4 + 2)));
  bytes[start + 6] = 0xf2;
  bytes[start + 7] = 0x09;
  bytes[start + 8] = 0xa0;

  const auto analysis = analyzeFixtureTrack(bytes, AkaoPs1Version::Version1_1, start, 0x90);
  expect(analysis.customInstrumentOffsets.contains(customTable),
         "Akao analysis should collect custom instrument tables from command-reader facts");
  expect(analysis.drumInstrumentOffsets.contains(drumTable),
         "Akao analysis should collect drum tables from command-reader facts");
  expect(analysis.usesIndividualArts && analysis.individualArtIds.contains(9),
         "Akao analysis should collect individual articulation ids from command-reader facts");
}

void akaoDialectDecodesRepeatFlowWithoutManualLayerLeaks() {
  std::vector<u8> bytes(0x40, 0xa0);
  constexpr u32 start = 0x20;
  bytes[start] = 0xc8;
  bytes[start + 1] = 0xc9;
  bytes[start + 2] = 0x02;
  bytes[start + 3] = 0xa0;

  const SequenceDialect dialect = makeAkaoDialect(AkaoPs1Version::Version3_2);
  const TrackProgram track = decodeFixtureTrack(bytes, AkaoPs1Version::Version3_2, start, 0x40);
  expect(track.commands.size() == 3, "Akao repeat fixture should decode start, repeat-until, and fallthrough end");
  expect(hasCommandKind(dialect, track, "akao-ps1-3.2.repeat-start", start),
         "Akao repeat start should be an explicit command");
  expect(hasCommandKind(dialect, track, "akao-ps1-3.2.repeat-until", start + 1),
         "Akao repeat until should be an explicit command");
}

void akaoVersion10OverlayCommandsUseLegacyLengthsAndProgramChange() {
  std::vector<u8> bytes(0x40, 0xa0);
  constexpr u32 start = 0x20;
  bytes[start] = 0xf4;
  bytes[start + 1] = 0x54;
  bytes[start + 2] = 0x53;
  bytes[start + 3] = 0xf6;
  bytes[start + 4] = 0x20;
  bytes[start + 5] = 0xa8;
  bytes[start + 6] = 0x04;
  bytes[start + 7] = 0xa0;

  const SequenceDialect dialect = makeAkaoDialect(AkaoPs1Version::Version1_0);
  const TrackProgram track = decodeFixtureTrack(bytes, AkaoPs1Version::Version1_0, start, 0x40);
  expect(track.commands.size() == 4, "Akao v1.0 overlay fixture should decode all commands");
  expect(track.commands[0].range.size == 3 && track.commands[1].range.size == 2,
         "Akao v1.0 overlay voice and balance command lengths should match legacy");
  expect(track.commands[2].range.offset == start + 5 && track.commands[2].opcode == 0xa8,
         "Akao v1.0 overlay balance should not consume the following expression command");

  AkaoSequenceAnalysis analysis = analyzeFixtureTrack(bytes, AkaoPs1Version::Version1_0, start, 0x40);
  expect(analysis.individualArtIds.contains(0x54) && analysis.individualArtIds.contains(0x53),
         "Akao v1.0 overlay voice should require both articulations");

  const SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .tracks = {track},
  };
  const PerformanceSequence performance = SequenceVm().render(program, dialect);
  const auto instrument = std::ranges::find_if(performance.tracks[0].events, [](const PerformanceEvent& event) {
    return std::holds_alternative<InstrumentPerformanceEvent>(event);
  });
  expect(instrument != performance.tracks[0].events.end(), "Akao v1.0 overlay voice should emit a program change");
}

void akaoLoopBranchUsesCurrentRepeatPass() {
  std::vector<u8> bytes(0x40, 0xa0);
  constexpr u32 start = 0x20;
  bytes[start] = 0xc8;
  bytes[start + 1] = 0x08;
  bytes[start + 2] = 0xf0;
  bytes[start + 3] = 0x02;
  writeLeS16(bytes, start + 4, 3);
  bytes[start + 6] = 0x13;
  bytes[start + 7] = 0xc9;
  bytes[start + 8] = 0x02;
  bytes[start + 9] = 0x1e;
  bytes[start + 10] = 0xa0;

  const SequenceDialect dialect = makeAkaoDialect(AkaoPs1Version::Version1_0);
  const TrackProgram track = decodeFixtureTrack(bytes, AkaoPs1Version::Version1_0, start, 0x40);
  const SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .tracks = {track},
  };
  const PerformanceSequence performance = SequenceVm().render(program, dialect);

  size_t skippedPhraseNotes = 0;
  bool sawExitNote = false;
  for (const auto& event : performance.tracks[0].events) {
    const auto* note = std::get_if<NotePerformanceEvent>(&event);
    if (note == nullptr || note->extendsPrevious) {
      continue;
    }
    if (note->key == 49) {
      ++skippedPhraseNotes;
    }
    if (note->key == 50 && note->header.tick == 48) {
      sawExitNote = true;
    }
  }

  expect(skippedPhraseNotes == 1, "Akao loop branch should skip the branch body on the matching repeat pass");
  expect(sawExitNote, "Akao loop branch should continue at the branch target without adding another repeat body");
}

void akaoTieAfterRestDoesNotExtendPreviousNote() {
  std::vector<u8> bytes(0x40, 0xa0);
  constexpr u32 start = 0x20;
  bytes[start] = 0x08;
  bytes[start + 1] = 0x8c;
  bytes[start + 2] = 0x91;
  bytes[start + 3] = 0x8c;
  bytes[start + 4] = 0xa0;

  const SequenceDialect dialect = makeAkaoDialect(AkaoPs1Version::Version1_2);
  const TrackProgram track = decodeFixtureTrack(bytes, AkaoPs1Version::Version1_2, start, 0x40);
  const SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .tracks = {track},
  };
  const PerformanceSequence performance = SequenceVm().render(program, dialect);
  expect(performance.diagnostics.empty(), "Akao tie-after-rest fixture should render without diagnostics");

  const auto noteCount = std::ranges::count_if(performance.tracks[0].events, [](const PerformanceEvent& event) {
    return std::holds_alternative<NotePerformanceEvent>(event);
  });
  expect(noteCount == 2, "Akao tie after a rest should not extend the previous note");
}

void akaoTempoFadeEmitsDriverTickRamp() {
  std::vector<u8> bytes(0x40, 0xa0);
  constexpr u32 start = 0x20;
  bytes[start] = 0xfc;
  bytes[start + 1] = 0x00;
  writeLe16(bytes, start + 2, 0x3000);
  bytes[start + 4] = 0xfc;
  bytes[start + 5] = 0x01;
  bytes[start + 6] = 0x03;
  writeLe16(bytes, start + 7, 0x6000);
  bytes[start + 9] = 0xa0;

  const SequenceDialect dialect = makeAkaoDialect(AkaoPs1Version::Version1_2);
  const TrackProgram track = decodeFixtureTrack(bytes, AkaoPs1Version::Version1_2, start, 0x40);
  const SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .tracks = {track},
  };
  const PerformanceSequence performance = SequenceVm().render(program, dialect);
  expect(performance.diagnostics.empty(), "Akao tempo-fade fixture should render without diagnostics");

  std::vector<TempoPerformanceEvent> tempos;
  for (const auto& event : performance.tracks[0].events) {
    if (const auto* tempo = std::get_if<TempoPerformanceEvent>(&event)) {
      tempos.push_back(*tempo);
    }
  }

  expect(tempos.size() == 4, "Akao tempo fade should emit one tempo event per driver tick");
  expect(tempos[1].header.tick == 0 && tempos[2].header.tick == 1 && tempos[3].header.tick == 2,
         "Akao tempo fade should schedule tempo changes on consecutive ticks");
  expect(tempos[3].microsecondsPerQuarter < tempos[1].microsecondsPerQuarter,
         "Akao tempo fade should move toward the target tempo");
}

void akaoRequiredArticulationsComeFromInstrumentRows() {
  std::vector<u8> bytes(0x100);
  constexpr u32 instrSet = 0x20;
  constexpr u32 melodicTable = 0x40;
  writeLe16(bytes, instrSet, 0);
  bytes[melodicTable] = 0x05;
  bytes[melodicTable + 1] = 0x20;
  bytes[melodicTable + 2] = 0x40;
  bytes[melodicTable + 3] = 0x10;
  bytes[melodicTable + 4] = 0x20;
  bytes[melodicTable + 5] = 0x00;
  bytes[melodicTable + 6] = 0x08;
  bytes[melodicTable + 7] = 0x7f;

  AkaoSequenceAnalysis analysis;
  analysis.header = AkaoSequenceHeader{
      .offset = 0,
      .length = 0x100,
      .version = AkaoPs1Version::Version3_2,
      .sequenceId = 7,
      .instrumentSetOffset = instrSet,
      .sampleSetId = 1,
  };

  const auto required = requiredArticulations(ByteReader(SourceId{22}, bytes), analysis);
  expect(required == std::vector<u32>{5}, "Akao required articulations should include parsed melodic row art ids");
}

void akaoSampleSelectionKeepsPreferredAndRequiredCollections() {
  const std::vector<AkaoSampleCandidate> candidates{
      AkaoSampleCandidate{.index = 0, .sampleSetId = 0, .firstArt = 0, .artCount = 32, .scanOrdinal = 0},
      AkaoSampleCandidate{.index = 1, .sampleSetId = 5, .firstArt = 32, .artCount = 81, .scanOrdinal = 1},
      AkaoSampleCandidate{.index = 2, .sampleSetId = 29, .firstArt = 128, .artCount = 22, .scanOrdinal = 2},
  };
  const std::vector<u32> required{32, 128};

  const auto selected = selectAkaoSampleCandidates(29, required, candidates);
  expect(selected == std::vector<std::size_t>{1, 2},
         "Akao sample selection should combine the preferred sample set with required-art coverage");
}
