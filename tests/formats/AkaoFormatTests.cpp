/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "../MidiTestSupport.h"
#include "../TestSupport.h"
#include "value/export/CollectionBinding.h"
#include "value/export/SequenceModulationProfile.h"
#include "ValueFormatTestSupport.h"

#include "../PerformanceTestSupport.h"
#include "value/formats/Akao/Akao.h"
#include "value/scan/AssetResolution.h"
#include "value/sequence/SequenceVm.h"
#include "value/session/Session.h"
#include "value/synth/PsxSpu.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

using namespace vgmtrans::core;
using namespace vgmtrans::formats::akao;

namespace {

void writeLe16(std::vector<u8>& bytes, size_t offset, u16 value) {
  bytes[offset] = static_cast<u8>(value & 0xff);
  bytes[offset + 1] = static_cast<u8>(value >> 8);
}

void writeLe32(std::vector<u8>& bytes, size_t offset, u32 value) {
  bytes[offset] = static_cast<u8>(value & 0xff);
  bytes[offset + 1] = static_cast<u8>((value >> 8) & 0xff);
  bytes[offset + 2] = static_cast<u8>((value >> 16) & 0xff);
  bytes[offset + 3] = static_cast<u8>((value >> 24) & 0xff);
}

void writeBe32(std::vector<u8>& bytes, size_t offset, u32 value) {
  bytes[offset] = static_cast<u8>((value >> 24) & 0xff);
  bytes[offset + 1] = static_cast<u8>((value >> 16) & 0xff);
  bytes[offset + 2] = static_cast<u8>((value >> 8) & 0xff);
  bytes[offset + 3] = static_cast<u8>(value & 0xff);
}

void writeLeS16(std::vector<u8>& bytes, size_t offset, s16 value) {
  writeLe16(bytes, offset, static_cast<u16>(value));
}

bool hasLinkRole(const SourceAnnotation& annotation, SourceLinkRole role) {
  return std::ranges::any_of(annotation.links, [&](const SourceLink& link) { return link.role == role; });
}

const SourceAnnotation* annotationWithKind(const SourceMap& sourceMap, SourceId source, SourceRole role,
                                           std::string_view category) {
  const auto annotations = sourceMap.withRole(source, role);
  for (const SourceAnnotationId id : annotations) {
    const SourceAnnotation& annotation = sourceMap.get(id);
    if (annotation.category() == category) {
      return &annotation;
    }
  }
  return nullptr;
}

TrackProgram decodeFixtureTrack(const std::vector<u8>& bytes, AkaoPs1Version version, u32 start, u32 end,
                                SourceMapBuilder* sourceMap = nullptr, SourceId source = SourceId{20},
                                AkaoSequenceReferences* references = nullptr) {
  const TrackDecodeScope tracks{
      .reader = ByteReader(source, bytes),
      .bytecodeEnd = end,
      .maxCommands = 64,
      .sourceMap = sourceMap,
  };
  return decodeAkaoTrack(version, tracks, 0, start, nullptr, references);
}

PerformanceSequence renderAkaoFixture(const std::vector<u8>& bytes, AkaoRuntimeConfig runtime = {},
                                       AkaoPs1Version version = AkaoPs1Version::Version1_0) {
  const auto config = makeAkaoConfig(version);
  const auto performance = SequenceVm().render(SequenceProgram{
      .runtime = akaoSequenceRuntime(std::move(runtime)),
      .timebase = config.timebase,
      .behavior = config.behavior,
      .tracks = {decodeFixtureTrack(bytes, version, 0, bytes.size())},
  });
  expect(performance.diagnostics.empty(), "Akao bytecode fixture should render without diagnostics");
  return performance;
}

template <class Event>
std::vector<Event> fixtureEvents(const PerformanceSequence& performance) {
  std::vector<Event> result;
  for (const auto& event : performance.tracks.at(0).events) {
    if (const auto* value = std::get_if<Event>(&event)) result.push_back(*value);
  }
  return result;
}

AkaoSequenceAnalysis analyzeFixtureTrack(const std::vector<u8>& bytes, AkaoPs1Version version, u32 start, u32 end) {
  AkaoSequenceAnalysis analysis;
  analysis.header = AkaoSequenceHeader{
      .offset = 0,
      .length = end,
      .version = version,
  };
  static_cast<void>(decodeFixtureTrack(bytes, version, start, end, nullptr, SourceId{20}, &analysis.references));
  return analysis;
}

}  // namespace

void akaoSequenceLayoutRejectsFalsePositiveHeaders() {
  ScanIdAllocator ids;
  const auto layout = [&](const std::vector<u8>& bytes) {
    const SourceId source{31};
    return readAkaoSequenceLayout(
        ScanInput{
            .source = SourceFile{.id = source, .name = "Final Fantasy VIII fixture"},
            .reader = ByteReader(source, bytes),
            .ids = ids,
        },
        0);
  };

  std::vector<u8> wrongProfile(0x100);
  writeBe32(wrongProfile, 0, kAkaoSignature);
  writeLe16(wrongProfile, 4, 0xf109);
  writeLe16(wrongProfile, 6, 0x80);
  writeLe32(wrongProfile, 0x10, 1);
  writeLe32(wrongProfile, 0x1c, 1);
  writeLe32(wrongProfile, 0x20, 1);
  writeLe32(wrongProfile, 0x2c, 1);
  writeLe16(wrongProfile, 0x40, 0x10);
  expect(!layout(wrongProfile), "Akao layout should validate a header against the source's effective profile");

  std::vector<u8> partialTracks(0x100);
  writeBe32(partialTracks, 0, kAkaoSignature);
  writeLe16(partialTracks, 6, 0x80);
  writeLe32(partialTracks, 0x20, 3);
  writeLe16(partialTracks, 0x40, 0x10);
  writeLe16(partialTracks, 0x42, 0x40);
  expect(!layout(partialTracks), "Akao layout should reject a sequence when any declared track pointer is invalid");

  partialTracks.resize(0x60);
  writeLe32(partialTracks, 0x20, 1);
  const auto truncated = layout(partialTracks);
  expect(truncated && truncated->header.length == partialTracks.size() && truncated->trackAddresses.size() == 1,
         "Akao layout should retain an optimized PSF whose declared track tail is truncated");
}

void akaoSequenceDecodesLegacyRelativeJumpTargets() {
  std::vector<u8> bytes(0x40, 0xa0);
  constexpr u32 start = 0x20;
  constexpr u32 target = 0x30;
  bytes[start] = 0xee;
  writeLeS16(bytes, start + 1, static_cast<s16>(target - (start + 1 + 2)));
  bytes[target] = 0xa0;

  SourceMapBuilder sourceMap;
  const TrackProgram track = decodeFixtureTrack(bytes, AkaoPs1Version::Version1_0, start, 0x40, &sourceMap);
  const SourceMap annotations = sourceMap.finish();
  expect(track.commands.size() == 2, "Akao legacy jump should decode the jump command and its target block");
  expect(hasCommandAnnotation(annotations, SourceId{20}, "akao-ps1-1.0.jump", start),
         "Akao legacy jump should publish a source annotation");
  expect(hasCommandAnnotation(annotations, SourceId{20}, "akao-ps1-1.0.end", target),
         "Akao legacy jump should expose the static target to the cursor walker");
}

void akaoSequenceDecodesConditionalBranchSideTargets() {
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

  SourceMapBuilder sourceMap;
  const TrackProgram track =
      decodeFixtureTrack(bytes, AkaoPs1Version::Version3_2, start, 0x70, &sourceMap, SourceId{21});
  const SourceMap annotations = sourceMap.finish();
  expect(track.commands.size() == 3, "Akao conditional branch should decode both fallthrough and side-target blocks");
  expect(hasCommandAnnotation(annotations, SourceId{21}, "akao-ps1-3.2.cpu-conditional-jump", start),
         "Akao conditional branch should publish a source annotation");
  expect(hasCommandAnnotation(annotations, SourceId{21}, "akao-ps1-3.2.end", fallthrough),
         "Akao conditional branch should preserve fallthrough flow");
  expect(hasCommandAnnotation(annotations, SourceId{21}, "akao-ps1-3.2.end", target),
         "Akao conditional branch should expose the branch target as static flow");
}

void akaoSequenceAnalysisUsesSemanticOperands() {
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
  expect(analysis.references.customInstrumentTableOffsets.contains(customTable),
         "Akao analysis should collect custom instrument tables from semantic operands");
  expect(analysis.references.drumInstrumentTableOffsets.contains(drumTable),
         "Akao analysis should collect drum tables from semantic operands");
  expect(analysis.references.usesIndividualArticulations && analysis.references.individualArticulationIds.contains(9),
         "Akao analysis should collect individual articulation ids from semantic operands");
  SoundBankAsset bank;
  const AkaoInstrumentSetBindingData recipe{.usesIndividualArticulations = true,
                                            .noAttackArticulationIds = analysis.references.noAttackArticulationIds};
  expect(applyAkaoArticulations(bank.instruments, recipe,
                              {{9, {.loopPoint = 32, .sample = SampleRef::resolved(AssetId{1}, 0)}}}) &&
             bank.instruments.size() == 2 && bank.instruments[0].regions[0].sampleStartFrame == 0 &&
             bank.instruments[1].regions[0].sampleStartFrame == 56 && bank.instruments[1].explicitAddress->bank == 2,
         "F2 should select a sustain variant while retaining the full-sample instrument");
}

void akaoPointerInstrumentsSelectTheirExportedPrograms() {
  for (const auto version : {AkaoPs1Version::Version1_1, AkaoPs1Version::Version1_2, AkaoPs1Version::Version2}) {
    std::vector<u8> bytes(0x90, 0xa0);
    u32 position = 0x20;
    // Select the higher table first: program numbers follow table order, not
    // the order in which playback encounters instrument commands.
    for (const u32 table : {0x70u, 0x60u}) {
      bytes[position++] = 0xfc;
      if (version != AkaoPs1Version::Version1_1) {
        bytes[position++] = 0x14;
      }
      writeLeS16(bytes, position, static_cast<s16>(table - (position + 2)));
      position += 2;
      bytes[position++] = 0x02;
      std::fill_n(bytes.begin() + table, 8, 0);
      bytes[table] = 5;
      bytes[table + 2] = 127;
    }
    const auto analysis = analyzeFixtureTrack(bytes, version, 0x20, bytes.size());
    ScanIdAllocator ids;
    ScanInput input{
        .source = SourceFile{.id = SourceId{20}, .name = "key-split.akao", .size = bytes.size()},
        .reader = ByteReader(SourceId{20}, bytes),
        .ids = ids,
    };
    InstrumentSetBuilder builder{AssetId{99}};
    (void)buildAkaoInstrumentSet(input, analysis, builder);
    SoundBankAsset bank{.instruments = std::move(builder).finish().values};
    expect(bank.instruments.size() == 2, "pointer fixture should export both instrument tables");
    const auto config = makeAkaoConfig(version);
    const SequenceProgram program{
        .runtime = akaoSequenceRuntime(),
        .timebase = config.timebase,
        .behavior = config.behavior,
        .tracks = {decodeFixtureTrack(bytes, version, 0x20, bytes.size())},
    };
    const auto performance = SequenceVm().render(program);
    const std::array<const SoundBankAsset*, 1> banks{&bank};
    const auto midi = renderTestMidi(performance, {}, ModulationConversionPolicy::SynthModulators, banks);
    std::vector<u8> programs;
    u16 selectedBank = 0;
    size_t notes = 0;
    for (const auto& event : midi.tracks[0].events) {
      if (const auto* select = midiBankSelect(event)) {
        selectedBank = select->bank;
      }
      if (const auto* change = midiChannelMessage(event, MidiChannelMessageKind::ProgramChange)) {
        programs.push_back(static_cast<u8>(change->value));
      }
      if (midiNote(event)) {
        expect(selectedBank == 1 && programs.size() == notes + 1 && programs.back() == 1 - notes,
               "each note must select its table's SF2 bank/program before sounding");
        ++notes;
      }
    }
    expect(notes == 2, "both pointer-selected instruments should produce notes");
  }
}

void akaoTablePointersUseNonControlSourceLinks() {
  std::vector<u8> bytes(0x90, 0xa0);
  constexpr SourceId source{22};
  constexpr u32 start = 0x20;
  constexpr u32 customTable = 0x60;
  constexpr u32 drumTable = 0x70;
  bytes[start] = 0xfc;
  writeLeS16(bytes, start + 1, static_cast<s16>(customTable - (start + 1 + 2)));
  bytes[start + 3] = 0xec;
  writeLeS16(bytes, start + 4, static_cast<s16>(drumTable - (start + 4 + 2)));
  bytes[start + 6] = 0xa0;

  ScanIdAllocator ids;
  SourceMapBuilder sourceMap([&ids]() { return ids.nextSourceAnnotationId(); });
  [[maybe_unused]] const TrackProgram customTrack =
      decodeFixtureTrack(bytes, AkaoPs1Version::Version1_1, start, 0x90, &sourceMap, source);

  const SourceMap annotations = sourceMap.finish();
  const SourceAnnotation& custom = commandAnnotationAt(annotations, source, start);
  const SourceAnnotation& drum = commandAnnotationAt(annotations, source, start + 3);
  expect(hasLinkRole(custom, SourceLinkRole::PointsTo),
         "Akao custom instrument table command should point to data, not control flow");
  expect(!hasLinkRole(custom, SourceLinkRole::JumpTarget),
         "Akao custom instrument table command should not expose a jump target");
  expect(hasLinkRole(drum, SourceLinkRole::PointsTo), "Akao drum table command should point to data, not control flow");
  expect(!hasLinkRole(drum, SourceLinkRole::JumpTarget), "Akao drum table command should not expose a jump target");
}

void akaoSequenceDecodesRepeatFlowWithoutManualLayerLeaks() {
  std::vector<u8> bytes(0x40, 0xa0);
  constexpr u32 start = 0x20;
  bytes[start] = 0xc8;
  bytes[start + 1] = 0xc9;
  bytes[start + 2] = 0x02;
  bytes[start + 3] = 0xa0;

  SourceMapBuilder sourceMap;
  const TrackProgram track = decodeFixtureTrack(bytes, AkaoPs1Version::Version3_2, start, 0x40, &sourceMap);
  const SourceMap annotations = sourceMap.finish();
  expect(track.commands.size() == 3, "Akao repeat fixture should decode start, repeat-until, and fallthrough end");
  expect(hasCommandAnnotation(annotations, SourceId{20}, "akao-ps1-3.2.repeat-start", start),
         "Akao repeat start should publish a source annotation");
  expect(hasCommandAnnotation(annotations, SourceId{20}, "akao-ps1-3.2.repeat-until", start + 1),
         "Akao repeat until should publish a source annotation");
}

void akaoRepeatSourceLinksUseSpecificRolesOnly() {
  constexpr SourceId source{23};
  constexpr u32 start = 0x20;

  std::vector<u8> repeatUntilBytes(0x40, 0xa0);
  repeatUntilBytes[start] = 0xc8;
  repeatUntilBytes[start + 1] = 0xc9;
  repeatUntilBytes[start + 2] = 0x02;
  repeatUntilBytes[start + 3] = 0xa0;

  ScanIdAllocator repeatUntilIds;
  SourceMapBuilder repeatUntilMap([&repeatUntilIds]() { return repeatUntilIds.nextSourceAnnotationId(); });
  [[maybe_unused]] const TrackProgram repeatUntilTrack =
      decodeFixtureTrack(repeatUntilBytes, AkaoPs1Version::Version3_2, start, 0x40, &repeatUntilMap, source);
  const SourceMap repeatUntilAnnotations = repeatUntilMap.finish();
  const SourceAnnotation& repeatUntil = commandAnnotationAt(repeatUntilAnnotations, source, start + 1);
  expect(hasLinkRole(repeatUntil, SourceLinkRole::RepeatTarget), "Akao repeat-until should expose a repeat target");
  expect(!hasLinkRole(repeatUntil, SourceLinkRole::JumpTarget),
         "Akao repeat-until should not also expose a generic jump target");

  std::vector<u8> repeatAgainBytes(0x40, 0xa0);
  repeatAgainBytes[start] = 0xc8;
  repeatAgainBytes[start + 1] = 0xca;

  ScanIdAllocator repeatAgainIds;
  SourceMapBuilder repeatAgainMap([&repeatAgainIds]() { return repeatAgainIds.nextSourceAnnotationId(); });
  [[maybe_unused]] const TrackProgram repeatAgainTrack =
      decodeFixtureTrack(repeatAgainBytes, AkaoPs1Version::Version3_2, start, 0x40, &repeatAgainMap, source);
  const SourceMap repeatAgainAnnotations = repeatAgainMap.finish();
  const SourceAnnotation& repeatAgain = commandAnnotationAt(repeatAgainAnnotations, source, start + 1);
  expect(hasLinkRole(repeatAgain, SourceLinkRole::LoopTarget), "Akao repeat-again should expose a loop target");
  expect(!hasLinkRole(repeatAgain, SourceLinkRole::JumpTarget),
         "Akao repeat-again should not also expose a generic jump target");
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

  const SequenceProgramConfig config = makeAkaoConfig(AkaoPs1Version::Version1_0);
  const TrackProgram track = decodeFixtureTrack(bytes, AkaoPs1Version::Version1_0, start, 0x40);
  expect(track.commands.size() == 4, "Akao v1.0 overlay fixture should decode all commands");
  expect(track.commands[0].range.size == 3 && track.commands[1].range.size == 2,
         "Akao v1.0 overlay voice and balance command lengths should match legacy");
  expect(track.commands[2].range.offset == start + 5 && track.commands[2].opcode == 0xa8,
         "Akao v1.0 overlay balance should not consume the following expression command");

  AkaoSequenceAnalysis analysis = analyzeFixtureTrack(bytes, AkaoPs1Version::Version1_0, start, 0x40);
  expect(analysis.references.individualArticulationIds.contains(0x54) &&
             analysis.references.individualArticulationIds.contains(0x53),
         "Akao v1.0 overlay voice should require both articulations");

  const SequenceProgram program{
      .runtime = akaoSequenceRuntime(),
      .timebase = config.timebase,
      .behavior = config.behavior,
      .tracks = {track},
  };
  const PerformanceSequence performance = SequenceVm().render(program);
  const auto instrument = std::ranges::find_if(performance.tracks[0].events, [](const PerformanceEvent& event) {
    return std::holds_alternative<InstrumentPerformanceEvent>(event);
  });
  expect(instrument != performance.tracks[0].events.end(), "Akao v1.0 overlay voice should emit a program change");
}

void akaoPanLawFollowsDriverProfile() {
  const SourceFile racingLagoon{
      .name = "114 Body Shop.psf",
      .title = "Racing Lagoon",
  };
  const SourceFile frontMission2{
      .name = "Front Mission 2.psf",
  };
  expect(determinePanLawFromSource(racingLagoon, AkaoPs1Version::Version3_1) == PanLaw::EqualPower,
         "Racing Lagoon should use its late Akao driver's equal-power pan law");
  expect(determinePanLawFromSource(frontMission2, AkaoPs1Version::Version1_2) == PanLaw::ConstantSum,
         "Front Mission 2 should retain its early Akao driver's constant-sum pan law");

  std::vector<u8> bytes(0x40, 0xa0);
  constexpr u32 start = 0x20;
  bytes[start] = 0xaa;
  bytes[start + 1] = 64;
  bytes[start + 2] = 0xa0;

  const SequenceProgramConfig lateConfig = makeAkaoConfig(AkaoPs1Version::Version3_1);
  const SequenceProgram lateProgram{
      .runtime = akaoSequenceRuntime(),
      .timebase = lateConfig.timebase,
      .behavior = lateConfig.behavior,
      .tracks = {decodeFixtureTrack(bytes, AkaoPs1Version::Version3_1, start, 0x40)},
  };
  const PerformanceSequence performance = SequenceVm().render(lateProgram);
  const auto pan = std::ranges::find_if(performance.tracks[0].events, [](const PerformanceEvent& event) {
    return std::holds_alternative<PanPerformanceEvent>(event);
  });
  expect(pan != performance.tracks[0].events.end() && std::get<PanPerformanceEvent>(*pan).law == PanLaw::EqualPower,
         "Akao positional pan should carry the resolved profile law in the performance IR");
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

  const SequenceProgramConfig config = makeAkaoConfig(AkaoPs1Version::Version1_0);
  const TrackProgram track = decodeFixtureTrack(bytes, AkaoPs1Version::Version1_0, start, 0x40);
  const SequenceProgram program{
      .runtime = akaoSequenceRuntime(),
      .timebase = config.timebase,
      .behavior = config.behavior,
      .tracks = {track},
  };
  const PerformanceSequence performance = SequenceVm().render(program);

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
  bytes[start] = 0x83;  // Last B note; ties start at 0x84.
  bytes[start + 1] = 0x8c;
  bytes[start + 2] = 0x91;
  bytes[start + 3] = 0x8c;
  bytes[start + 4] = 0xa0;

  const SequenceProgramConfig config = makeAkaoConfig(AkaoPs1Version::Version1_2);
  const TrackProgram track = decodeFixtureTrack(bytes, AkaoPs1Version::Version1_2, start, 0x40);
  const SequenceProgram program{
      .runtime = akaoSequenceRuntime(),
      .timebase = config.timebase,
      .behavior = config.behavior,
      .tracks = {track},
  };
  const PerformanceSequence performance = SequenceVm().render(program);
  expect(performance.diagnostics.empty(), "Akao tie-after-rest fixture should render without diagnostics");

  const auto noteCount = std::ranges::count_if(performance.tracks[0].events, [](const PerformanceEvent& event) {
    return std::holds_alternative<NotePerformanceEvent>(event);
  });
  expect(noteCount == 2, "0x83 must sound a note, and a tie after a rest must not extend it");
  const auto notes = midiNotes(renderTestMidi(performance).tracks[0].events);
  expect(notes.size() == 1 && notes[0].key == 59, "0x83 must export as B, not a tie");
}

void ff7SlurChangesPitchWithoutAnotherAttack() {
  // Underneath the Rotting Pizza, track 11:
  // B -> D -> B is one voice, followed by three separately attacked notes.
  const auto performance = renderAkaoFixture({
      0xa5, 4, 0xcc, 0xa8, 84, 0x7d, 0xa6, 0xa8, 126, 0x1a, 0xa7, 0x7d,
      0xcd, 0xa6, 0x2f, 0xa7, 0x30, 0x30, 0xa0,
  });
  const auto notes = fixtureEvents<NotePerformanceEvent>(performance);
  expect(notes.size() == 6 && notes[0].restartsEnvelope && !notes[1].restartsEnvelope &&
             !notes[2].restartsEnvelope && notes[3].restartsEnvelope && notes[2].durationTicks == 10,
         "FF7 slur should suppress only the connected attacks and restore the final two-tick gate gap");
  const auto& slides = performance.tracks[0].automations;
  expect(slides.size() == 2, "FF7 B-D-B slur should retain both immediate pitch changes");
  for (size_t i = 0; i < slides.size(); ++i) {
    const auto* slide = pitchTransitionIntent(slides[i]);
    expect(slide && notes[i].voice == notes[i + 1].voice && slide->note == notes[i + 1].note &&
               slide->timing.timelineTicks == 0 &&
               slide->preferredRendering == PitchTransitionRenderingHint::PitchBend,
           "FF7 slurs must link the previous voice without introducing a portamento ramp");
  }
  for (auto modulation : {ModulationConversionPolicy::SynthModulators,
                          ModulationConversionPolicy::SequenceEventSimulation}) {
    const auto midi = renderTestMidi(performance, {}, modulation);
    const auto attacks = midiNotes(midi.tracks[0].events);
    expect(attacks.size() == 4 && attacks[0].key == 59 && attacks[0].duration == 34,
           "FF7 slurs should export and preview as one sustained attack spanning the three source notes");
    expect(std::ranges::any_of(midi.tracks[0].events, [](const MidiEvent& event) {
             const auto* bend = midiChannelMessage(event, MidiChannelMessageKind::PitchBend);
             return bend && event.tick == 12 && bend->value > 0;
           }) && std::ranges::any_of(midi.tracks[0].events, [](const MidiEvent& event) {
             const auto* bend = midiChannelMessage(event, MidiChannelMessageKind::PitchBend);
             return bend && event.tick == 24 && bend->value == 0;
           }), "FF7 slurs should bend to D and back to B on the existing voice");
  }
}

void ff7SlurBoundariesRespectRestsTiesLegatoAndRepeats() {
  const auto check = [](std::string_view name, std::vector<u8> bytes, std::vector<u32> expected) {
    const auto performance = renderAkaoFixture(bytes);
    std::vector<u32> durations;
    for (const auto& note : midiNotes(renderTestMidi(performance).tracks[0].events)) {
      durations.push_back(note.duration);
    }
    expect(durations == expected, name);
  };
  check("Same-pitch slur", {0xcc, 0x08, 0x08, 0xcd, 0x13, 0xa0}, {30, 14});
  check("Rest clears slur", {0xcc, 0x08, 0x13, 0x97, 0x1e, 0x29, 0xa0}, {30, 14, 14});
  check("Legato still attacks", {0xd0, 0x08, 0x13, 0xd1, 0x1e, 0xa0}, {16, 14, 14});
  check("CC resets continuation", {0xcc, 0x08, 0xcc, 0x13, 0xcd, 0x1e, 0xa0}, {16, 14, 14});
  check("Separate slurs", {0xcc, 0x08, 0xcd, 0xcc, 0x13, 0x1e, 0xcd, 0xa0}, {14, 30});
  check("CD lookahead", {0xcc, 0x08, 0xcc, 0xcd, 0x13, 0x1e, 0xcd, 0xa0}, {14, 30});
  check("CB clears intervening CC", {0xcc, 0x08, 0xcc, 0xcb, 0x13, 0x1e, 0xa0}, {14, 14, 14});
  check("CB clears legato", {0xd0, 0x08, 0x13, 0xcb, 0x1e, 0xa0}, {16, 14, 14});
  check("Tie retains voice", {0xcc, 0x08, 0x8c, 0x13, 0xcd, 0xa0}, {46});
  check("Repeat stays connected", {0xcc, 0xc8, 0x08, 0x13, 0xc9, 2, 0xcd, 0xa0}, {62});
}

void ff7PortamentoEnablesSlurAndStartsWithAFreshAttack(u8 boundary) {
  const auto performance = renderAkaoFixture({0x29, 0xda, 4, 0x08, 0x13, 0x1e, boundary, 0x29, 0xa0});
  const auto& slides = performance.tracks[0].automations;
  expect(!slides.empty(), "FF7 DA must produce pitch transitions");
  const auto sourceNotes = fixtureEvents<NotePerformanceEvent>(performance);
  expect(sourceNotes.size() == 5 && sourceNotes[1].voice == sourceNotes[2].voice &&
             sourceNotes[2].voice == sourceNotes[3].voice && sourceNotes[3].voice != sourceNotes[4].voice,
         "FF7 portamento must declare continued voices before pitch rendering");
  const auto* slide = pitchTransitionIntent(slides.front());
  expect(slide && slide->startKey == 48 && slide->targetKey == 49 &&
             slide->timing.timelineTicks == 4,
         "FF7 DA must enable attack-free portamento for subsequent pitches");
  const auto* finalPitch = pitchTransitionIntent(slides.back());
  expect(finalPitch && finalPitch->startKey == 49 && finalPitch->targetKey == 50 &&
             finalPitch->timing.timelineTicks == 0,
         "FF7 DB/CB lookahead must make the preceding note's pitch change immediate");
  const auto notes = midiNotes(renderTestMidi(performance).tracks[0].events);
  expect(notes.size() == 3 && notes[1].key == 48 && notes[1].duration == 46 && notes[2].key == 51,
         "FF7 DB/CB must end portamento and restore a fresh attack");
}

void ff7ExpressionFadesRetargetAndCancel() {
  const auto performance = renderAkaoFixture({
      0xa8, 0, 0xa9, 8, 64, 0xa2, 2, 0x08, 0xa9, 4, 48, 0xa2, 2, 0x13, 0xa8, 40, 0x08, 0xa0,
  });
  std::vector<std::pair<u64, int>> values;
  for (const auto& event : fixtureEvents<ExpressionPerformanceEvent>(performance)) {
    values.emplace_back(event.header.tick, static_cast<int>(std::round(event.linearGain * 127)));
  }
  expect(values == std::vector<std::pair<u64, int>>{{0, 0}, {0, 8}, {1, 16}, {2, 24}, {3, 32}, {4, 40}},
         "FF7 A9 must start from the current expression; A8 must stop all subsequent fade updates");
  const auto& fades = performance.tracks[0].automations;
  expect(fades.size() == 2 && fades[0].realization.endTick == 2 && fades[1].realization.endTick == 4 &&
             fades[0].realization.endReason == PerformanceAutomationEndReason::Interrupted &&
             fades[1].realization.endReason == PerformanceAutomationEndReason::Interrupted,
         "FF7 expression automation must end when replaced or cancelled");
  const auto longFade = renderAkaoFixture({0xa8, 0, 0xa9, 0, 127, 0x00, 0x01, 0xa0});
  const auto expression = fixtureEvents<ExpressionPerformanceEvent>(longFade);
  expect(expression.back().header.tick == 255 && expression.back().linearGain == 1.0,
         "FF7 zero fade duration means 256 ticks, beginning on the command's tick");
}

void ff7ReverbSwitchesAndResetReachMidi() {
  const std::vector<u8> commands{0xc2, 0x08, 0xc3, 0x08, 0xc2, 0x08, 0xcb, 0x08, 0xa0};
  const auto midi = renderTestMidi(renderAkaoFixture(commands));
  std::vector<std::pair<u64, int>> sends;
  for (const auto& event : midi.tracks[0].events) {
    if (const auto* cc = midiController(event, MidiController::Reverb)) sends.emplace_back(event.tick, cc->value);
  }
  expect(sends == std::vector<std::pair<u64, int>>{{0, 0}, {0, 127}, {16, 0}, {32, 127}, {48, 0}},
         "FF7 starts dry; C2 enables reverb, and C3/CB disable it");
  expect(fixtureEvents<ReverbPerformanceEvent>(
             renderAkaoFixture(commands, {}, AkaoPs1Version::Version1_1)).empty(),
         "FF7 reverb support must not change unaudited later drivers");
}

void ff7EnvelopeCommandsKeepNativeStateAndResetOnProgramChange() {
  // Underneath the Rotting Pizza sets guitar SR from 0x3b to 0x40/0x45 before
  // the note attacks. Also exercise a change during a tied note, B3, and a new program.
  const std::vector<u8> bytes{
      0xa1, 27, 0xb1, 0x40, 0x02, 0xb1, 0x45, 0x86, 0xb3, 0x02,
      0xb1, 0x40, 0xa1, 28, 0xb2, 8, 0x02, 0xa0,
  };
  constexpr u16 adsr1 = 0x00ff;
  constexpr u16 guitarAdsr2 = 0x4ec5;  // Linear decreasing SR=0x3b, RR=5.
  constexpr u16 otherAdsr2 = 0x5fc5;   // Infinite sustain, RR=5.
  const AkaoRuntimeConfig runtime{.articulationEnvelopes = {{27, {adsr1, guitarAdsr2}}, {28, {adsr1, otherAdsr2}}}};
  const auto envelopes = fixtureEvents<EnvelopePerformanceEvent>(renderAkaoFixture(bytes, runtime));
  expect(envelopes.size() == 6, "FF7 ADSR commands and resets should reach the performance");
  const auto original = psxSpuEnvelope(adsr1, guitarAdsr2);
  const auto slower = psxSpuEnvelope(adsr1, 0x5005);
  expect(envelopes[0].update.values == slower &&
             *slower.secondDecaySeconds > 2.0 * *original.secondDecaySeconds,
         "B1 40 should substantially lengthen the guitar sustain while preserving its other ADSR fields");
  expect(envelopes[1].header.tick == 48 &&
             envelopes[1].scope == VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks &&
             envelopes[1].update.values == psxSpuEnvelope(adsr1, 0x5145),
         "B1 during a note should update that voice as well as later attacks");
  expect(!envelopes[2].update.values && envelopes[2].update.fields == EnvelopeFields::All,
         "B3 should restore the selected articulation envelope");
  expect(!envelopes[4].update.values && envelopes[5].update.values == psxSpuEnvelope(adsr1, 0x5fc8),
         "A1 should discard the previous program's overrides before the next partial ADSR command");

  const auto later = renderAkaoFixture(bytes, runtime, AkaoPs1Version::Version1_1);
  expect(fixtureEvents<EnvelopePerformanceEvent>(later).empty(),
         "FF7 envelope support should not change unaudited later drivers");
}

void ff7EnvelopeRatesModesAndCombinedCommandCompose() {
  const std::vector<u8> bytes{0xa1, 27, 0xad, 0x28, 0xb7, 5, 0xae, 7, 0xaf, 4,
                             0xb0, 9, 6, 0xb1, 0x45, 0xbb, 7, 0xb2, 8, 0xbf, 7, 0x02, 0xa0};
  const auto performance = renderAkaoFixture(bytes, {.articulationEnvelopes = {{27, {0x00ff, 0x4ec5}}}});
  const auto envelopes = fixtureEvents<EnvelopePerformanceEvent>(performance);
  expect(!envelopes.empty() && envelopes.back().update.values == psxSpuEnvelope(0xa896, 0xd168),
         "FF7 partial ADSR commands should compose without discarding previous rates or modes");
}

void ff7CollectionBindsNativeEnvelopesAndPreservesDrumDefaults() {
  std::vector<u8> bytes(0x1a8000);
  writeBe32(bytes, 0, kAkaoSignature);
  writeLe16(bytes, 6, 0x100);
  writeLe32(bytes, 0x10, 1);
  writeLe16(bytes, 0x14, 0x20 - 0x16);
  // Avoid the zero header fields used to recognize the v2/v3 layouts.
  writeLe32(bytes, 0x1c, 1);
  writeLe32(bytes, 0x2c, 1);
  const std::vector<u8> commands{0xa1, 27, 0xb1, 0x40, 0x02, 0xec, 0x18, 0, 0x02, 0xa0};
  std::copy(commands.begin(), commands.end(), bytes.begin() + 0x20);
  // EC's signed pointer is based after the operand: 0x28 + 0x18 = 0x40.
  bytes[0x40] = 27;
  bytes[0x41] = 60;
  writeLe16(bytes, 0x42, 0x3f80);
  bytes[0x44] = 64;
  writeLe32(bytes, 0xe0000, 0x1010);
  writeLe32(bytes, 0xe0004, 0x20);
  bytes[0xe0011] = 4;
  bytes[0xe0021] = 3;
  for (const u32 id : {0u, 27u}) {
    const u32 offset = 0x156000 + id * 0x40;
    writeLe32(bytes, offset, 0x1010);
    writeLe32(bytes, offset + 4, 0x1010);
    const std::array<u8, 8> adsr{0, 15, 15, 0x3b, 5, 1, 3, 3};
    std::copy(adsr.begin(), adsr.end(), bytes.begin() + offset + 8);
    writeLe32(bytes, offset + 0x10, 4096);
  }
  Session session;
  session.registerFormat(akaoModule());
  session.addSource(SourceFile{.name = "Final Fantasy VII envelope fixture.psf"}, bytes);
  session.scanPendingSources();
  const auto snapshot = session.snapshot();
  expect(snapshot.collections().size() == 1, "FF7 fixture should resolve its split sample pool");
  const auto bound = bindCollection(snapshot, snapshot.collections()[0].id);
  expect(bound.collection.has_value(), "FF7 envelope fixture should bind successfully");
  const PreparedCollection prepared{*bound.collection, {
      .sequence = SequenceRenderOptions{}, .instruments = {.dynamicEnvelopes = true}}};
  expect(prepared.rendering.performance.has_value(), "FF7 prepared runtime should render successfully");
  const auto envelopes = fixtureEvents<EnvelopePerformanceEvent>(*prepared.rendering.performance);
  expect(!envelopes.empty() && envelopes[0].update.values == psxSpuEnvelope(0x00ff, 0x5005),
         "collection preparation should supply the selected sample's native registers to B1");
  const auto& bank = prepared.soundBanks()[0];
  expect(std::ranges::all_of(bank.instruments, [](const Instrument& instrument) { return instrument.reverb == 0.0; }),
         "FF7 melodic, drum and ADSR-variant instruments must not add reverb independently of sequence commands");
  const auto drum = std::ranges::find_if(bank.instruments, [](const Instrument& instrument) {
    return instrument.explicitAddress && instrument.explicitAddress->bank == 127;
  });
  expect(drum != bank.instruments.end() && drum->regions[0].envelope == psxSpuEnvelope(0x00ff, 0x4ec5),
         "FF7 drum rows contain no ADSR overrides and must preserve the articulation envelope");
  expect(std::ranges::any_of(bank.instruments, [](const Instrument& instrument) {
    return !instrument.regions.empty() && instrument.regions[0].envelope == psxSpuEnvelope(0x00ff, 0x5005);
  }), "combined export should materialize the slower guitar envelope in a playable instrument variant");
  const auto artifacts = session.exportCollection(snapshot.collections()[0].id,
                                                   ExportRequest{.kinds = {ExportKind::Midi, ExportKind::SoundFont2}});
  expect(artifacts.size() == 2 && std::ranges::all_of(artifacts, [](const auto& artifact) {
    return !artifact.bytes.empty();
  }), "FF7 dynamic envelopes should survive combined MIDI and SoundFont export");
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

  const SequenceProgramConfig config = makeAkaoConfig(AkaoPs1Version::Version1_2);
  const TrackProgram track = decodeFixtureTrack(bytes, AkaoPs1Version::Version1_2, start, 0x40);
  const SequenceProgram program{
      .runtime = akaoSequenceRuntime(),
      .timebase = config.timebase,
      .behavior = config.behavior,
      .tracks = {track},
  };
  const PerformanceSequence performance = SequenceVm().render(program);
  expect(performance.diagnostics.empty(), "Akao tempo-fade fixture should render without diagnostics");

  std::vector<TempoPerformanceEvent> tempos;
  for (const auto& event : performance.tracks[0].events) {
    if (const auto* tempo = std::get_if<TempoPerformanceEvent>(&event)) {
      tempos.push_back(*tempo);
    }
  }

  expect(
      performance.tracks[0].automations.size() == 1 &&
          std::get<ScalarPerformanceAutomationIntent>(performance.tracks[0].automations[0].intent).target ==
              PerformanceAutomationTarget::Tempo &&
          std::get<ScalarPerformanceAutomationIntent>(performance.tracks[0].automations[0].intent).durationTicks == 3,
      "Akao tempo fade should retain one structured automation with source duration");

  expect(tempos.size() == 4, "Akao tempo fade should emit one tempo event per driver tick");
  expect(tempos[1].header.tick == 0 && tempos[2].header.tick == 1 && tempos[3].header.tick == 2,
         "Akao tempo fade should schedule tempo changes on consecutive ticks");
  expect(tempos[3].microsecondsPerQuarter < tempos[1].microsecondsPerQuarter,
         "Akao tempo fade should move toward the target tempo");
}

void akaoPitchSlideAppliesOnceToTheNextNote() {
  std::vector<u8> bytes(0x40, 0xa0);
  constexpr u32 start = 0x20;
  bytes[start] = 0xa5;
  bytes[start + 1] = 5;
  bytes[start + 2] = 0xa4;
  bytes[start + 3] = 0x24;
  bytes[start + 4] = 2;
  bytes[start + 5] = 0x38;
  bytes[start + 6] = 0x38;
  bytes[start + 7] = 0xa0;

  const SequenceProgramConfig config = makeAkaoConfig(AkaoPs1Version::Version3_1);
  const SequenceProgram program{
      .runtime = akaoSequenceRuntime(),
      .timebase = config.timebase,
      .behavior = config.behavior,
      .tracks = {decodeFixtureTrack(bytes, AkaoPs1Version::Version3_1, start, 0x40)},
  };
  const PerformanceSequence performance = SequenceVm().render(program);
  expect(performance.diagnostics.empty(), "Akao pitch-slide fixture should render without diagnostics");

  std::vector<const NotePerformanceEvent*> notes;
  for (const auto& event : performance.tracks[0].events) {
    if (const auto* note = std::get_if<NotePerformanceEvent>(&event)) {
      notes.push_back(note);
    }
  }
  expect(notes.size() == 2 && performance.tracks[0].automations.size() == 1,
         "Akao A4 should apply once to the next note instead of remaining active");
  const auto* transition = pitchTransitionIntent(performance.tracks[0].automations.front());
  expect(transition != nullptr && transition->note == notes[0]->note && transition->startKey == 65.0 &&
             transition->targetKey == 67.0 && transition->timing.timelineTicks == 0x24 &&
             transition->preferredRendering == PitchTransitionRenderingHint::PitchBend,
         "Akao A4 should retain its upward semitone depth, tick duration, and pitch-bend intent");

  const MidiSequence midi = renderTestMidi(performance);
  expect(std::ranges::count_if(
             midi.tracks[0].events,
             [](const MidiEvent& event) { return std::holds_alternative<NoteDuration>(event.payload); }) == 2 &&
             std::ranges::any_of(midi.tracks[0].events,
                                 [](const MidiEvent& event) {
                                   const auto* bend = midiChannelMessage(event, MidiChannelMessageKind::PitchBend);
                                   return bend != nullptr && event.tick <= 0x24 && bend->value > 0;
                                 }) &&
             std::ranges::none_of(
                 midi.tracks[0].events,
                 [](const MidiEvent& event) { return isMidiController(event, MidiController::PortamentoControl); }),
         "Akao A4 should bend the original attack without creating a destination-note attack or portamento event");
}

void akaoPortamentoRetainsPitchTransitionIntent() {
  std::vector<u8> bytes(0x40, 0xa0);
  constexpr u32 start = 0x20;
  bytes[start] = 0xda;
  bytes[start + 1] = 4;
  bytes[start + 2] = 0x08;
  bytes[start + 3] = 0x13;
  bytes[start + 4] = 0xdb;
  bytes[start + 5] = 0x1e;
  bytes[start + 6] = 0xa0;

  const SequenceProgramConfig config = makeAkaoConfig(AkaoPs1Version::Version1_2);
  const SequenceProgram program{
      .runtime = akaoSequenceRuntime(),
      .timebase = config.timebase,
      .behavior = config.behavior,
      .tracks = {decodeFixtureTrack(bytes, AkaoPs1Version::Version1_2, start, 0x40)},
  };
  const PerformanceSequence performance = SequenceVm().render(program);
  expect(performance.diagnostics.empty(), "Akao portamento fixture should render without diagnostics");

  std::vector<const NotePerformanceEvent*> notes;
  for (const auto& event : performance.tracks[0].events) {
    if (const auto* note = std::get_if<NotePerformanceEvent>(&event)) {
      notes.push_back(note);
    }
  }
  expect(notes.size() == 3, "Akao portamento should retain each source note");
  expect(performance.tracks[0].automations.size() == 1,
         "Akao portamento should create transitions only while the persistent setting is active");
  const auto* transition = pitchTransitionIntent(performance.tracks[0].automations.front());
  expect(transition != nullptr && transition->note == notes[1]->note && notes[0]->voice != notes[1]->voice &&
             transition->startKey == 48.0 && transition->targetKey == 49.0 && transition->timing.timelineTicks == 4 &&
             std::holds_alternative<TempoRelativePitchSlideTiming>(transition->timing.physical),
         "Akao portamento should retain its note anchor, keys, new attack, and tick-relative duration");
  expect(std::ranges::none_of(performance.tracks[0].events,
                              [](const PerformanceEvent& event) {
                                return std::holds_alternative<PortamentoPerformanceEvent>(event) ||
                                       std::holds_alternative<PortamentoEnablePerformanceEvent>(event);
                              }),
         "Akao format code should not preselect a MIDI portamento representation");

  const MidiSequence native = renderTestMidi(
      performance, MidiExportOptions{.pitchTransitions = MidiPitchTransitionRendering::PreserveFormat});
  expect(std::ranges::any_of(native.tracks[0].events,
                             [](const MidiEvent& event) {
                               const auto* control = midiController(event, MidiController::PortamentoControl);
                               return control != nullptr && event.tick == 16 && control->value == 48;
                             }),
         "preserve-format MIDI should lower Akao portamento with its explicit source key");

  const MidiSequence pitchBend =
      renderTestMidi(performance, MidiExportOptions{.pitchTransitions = MidiPitchTransitionRendering::PitchBend});
  expect(std::ranges::none_of(pitchBend.tracks[0].events,
                              [](const MidiEvent& event) {
                                return isMidiController(event, MidiController::PortamentoTime) ||
                                       isMidiControllerLsb(event, MidiController::PortamentoTime) ||
                                       isMidiController(event, MidiController::PortamentoControl);
                              }) &&
             std::ranges::any_of(pitchBend.tracks[0].events,
                                 [](const MidiEvent& event) {
                                   const auto* bend = midiChannelMessage(event, MidiChannelMessageKind::PitchBend);
                                   return bend != nullptr && event.tick >= 16 && bend->value != 0;
                                 }) &&
             std::ranges::any_of(pitchBend.tracks[0].events,
                                 [](const MidiEvent& event) {
                                   const auto* note = std::get_if<NoteDuration>(&event.payload);
                                   return note != nullptr && event.tick == 16 && note->key == 49;
                                 }),
         "pitch-bend lowering should preserve Akao's destination-note attack without native portamento events");
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
      .sampleSetId = 1,
      .soundBankOffset = instrSet,
  };

  ScanIdAllocator ids;
  ScanInput input{
      .source = SourceFile{.id = SourceId{22}, .name = "requirements.akao", .size = bytes.size()},
      .reader = ByteReader(SourceId{22}, bytes),
      .ids = ids,
  };
  InstrumentSetBuilder builder{AssetId{99}};
  const auto built = buildAkaoInstrumentSet(input, analysis, builder);
  expect(built.requiredArticulations == std::vector<u32>{5},
         "Akao required articulations should include parsed melodic region articulation ids");
}

void akaoMelodicRegionsDropAdvancingOverlaps() {
  std::vector<u8> bytes(0x100);
  constexpr u32 instrSet = 0x20;
  constexpr u32 melodicTable = 0x40;
  writeLe16(bytes, instrSet, 0);
  bytes[melodicTable] = 0x01;
  bytes[melodicTable + 1] = 0x00;
  bytes[melodicTable + 2] = 0x20;
  bytes[melodicTable + 8] = 0x02;
  bytes[melodicTable + 9] = 0x21;
  bytes[melodicTable + 10] = 0x2d;
  bytes[melodicTable + 16] = 0x03;
  bytes[melodicTable + 17] = 0x22;
  bytes[melodicTable + 18] = 0x36;
  bytes[melodicTable + 24] = 0x04;
  bytes[melodicTable + 25] = 0x37;
  bytes[melodicTable + 26] = 0x41;

  ScanIdAllocator ids;
  AkaoSequenceAnalysis analysis;
  analysis.header = AkaoSequenceHeader{
      .offset = 0,
      .length = 0x100,
      .version = AkaoPs1Version::Version3_2,
      .sequenceId = 7,
      .sampleSetId = 1,
      .soundBankOffset = instrSet,
  };
  ScanInput input{
      .source = SourceFile{.id = SourceId{22}, .name = "overlap-keys.akao", .size = bytes.size()},
      .reader = ByteReader(SourceId{22}, bytes),
      .ids = ids,
  };

  InstrumentSetBuilder builder{AssetId{99}};
  (void)buildAkaoInstrumentSet(input, analysis, builder);
  const auto instruments = std::move(builder).finish();
  expect(instruments.values.size() == 1, "Akao overlap fixture should parse one melodic instrument");
  const auto& regions = instruments.values.front().regions;
  expect(regions.size() == 3, "Akao advancing overlapping key regions should match legacy filtering");
  expect(regions[0].keyRange.low == 0 && regions[0].keyRange.high == 0x20,
         "Akao first overlap fixture region should keep its high key");
  expect(regions[1].keyRange.low == 0x21 && regions[1].keyRange.high == 0x2d,
         "Akao second overlap fixture region should be contiguous");
  expect(regions[2].keyRange.low == 0x2e && regions[2].keyRange.high == 0x7f,
         "Akao region after dropped overlap should bridge the uncovered key range");
}

void akaoSampleSelectionUsesPlayableArticulations() {
  SourceStore sources;
  const SourceId sequenceSource = sources.add(SourceFile{.name = "sequence.bin"}, std::vector<u8>(1024));
  const SourceId unrelatedSource = sources.add(SourceFile{.name = "unrelated.bin"}, std::vector<u8>(1024));
  const AssetId sequenceId{1};
  const AssetId bankId{2};
  const AssetId localSamplesId{3};
  const AssetId unrelatedSamplesId{4};

  std::vector<Asset> assets;
  assets.emplace_back(SequenceProgramAsset{
      .metadata = AssetMetadata{.id = sequenceId,
                                .format = std::string(kAkaoFormatName),
                                .name = "Sequence",
                                .range = sources.reader(sequenceSource).range(10, 20)},
      .recipe = {.banks = {DependencyTarget{bankId, {}}}},
  });
  assets.emplace_back(SoundBankAsset{
      .metadata = AssetMetadata{.id = bankId,
                                .format = std::string(kAkaoFormatName),
                                .name = "Bank",
                                .range = sources.reader(sequenceSource).range(40, 20)},
      .recipe = {.samples = {AkaoSamples{.sampleSetId = 7, .requiredArticulations = {5, 9}}}},
  });
  const auto samples = [&](AssetId id, SourceId source, u16 sampleSet, std::vector<AkaoArticulation> articulations) {
    return SamplePoolAsset{
        .metadata = AssetMetadata{.id = id,
                                  .format = std::string(kAkaoFormatName),
                                  .name = "Samples",
                                  .range = sources.reader(source).range(100, 20)},
        .pool = {.samples = std::vector<Sample>(articulations.size())},
        .privateData = AssetPrivateData::make(AkaoSamplePoolData{
            .sampleSetId = sampleSet,
            .firstArticulationId = articulations.empty() ? 0 : articulations.front().articulationId,
            .articulations = std::move(articulations),
        }),
    };
  };
  const auto resolve = [&](std::vector<AkaoArticulation> local, std::vector<AkaoArticulation> unrelated,
                           u16 unrelatedSet = 7) {
    auto candidates = assets;
    candidates.emplace_back(samples(localSamplesId, sequenceSource, 7, std::move(local)));
    candidates.emplace_back(samples(unrelatedSamplesId, unrelatedSource, unrelatedSet, std::move(unrelated)));
    const AssetCatalog context(sources, SharedSequence<Asset>{std::move(candidates)});
    return dependencyCollections(context);
  };
  const AkaoArticulation localFive{.articulationId = 5, .sample = SampleRef::resolved(localSamplesId, 0)};
  const AkaoArticulation localNine{.articulationId = 9, .sample = SampleRef::resolved(localSamplesId, 1)};
  const AkaoArticulation unrelatedFive{.articulationId = 5, .sample = SampleRef::resolved(unrelatedSamplesId, 0)};
  const AkaoArticulation unrelatedNine{.articulationId = 9, .sample = SampleRef::resolved(unrelatedSamplesId, 0)};
  const auto local = resolve({localFive, localNine}, {unrelatedFive, unrelatedNine});
  expect(local.size() == 1 && local.front().members().samplePools == std::vector{localSamplesId},
         "Akao matching should not let a newer unrelated pool outrank complete local samples");

  const auto supplemented = resolve({localFive, {.articulationId = 9}}, {unrelatedNine}, 29);
  expect(supplemented.size() == 1 &&
             supplemented.front().members().samplePools == std::vector{localSamplesId, unrelatedSamplesId} &&
             supplemented.front().issues.empty(),
         "a different sample set should fill a preferred pool's gap when an articulation has no playable sample");
  const auto missing = resolve({localFive, {.articulationId = 9}}, {});
  expect(missing.size() == 1 && missing.front().issues.size() == 1 &&
             missing.front().issues.front().code == "missing-articulation-coverage" &&
             missing.front().issues.front().message.ends_with(" 9"),
         "a declared articulation without a playable sample should remain missing from coverage");
}

void akaoScanPublishesStructuralInstrumentSetAndBindsCollectionView() {
  std::vector<u8> bytes(0x280);

  constexpr u32 sequenceOffset = 0x00;
  constexpr u32 trackOffset = 0x50;
  constexpr u32 instrumentTableOffset = 0x80;
  constexpr u32 melodicRegionOffset = 0xa0;
  writeBe32(bytes, sequenceOffset, 0x414b414f);
  writeLe16(bytes, sequenceOffset + 4, 7);
  writeLe16(bytes, sequenceOffset + 6, 0x100);
  writeLe16(bytes, sequenceOffset + 0x14, 1);
  writeLe32(bytes, sequenceOffset + 0x20, 1);
  writeLe32(bytes, sequenceOffset + 0x30, instrumentTableOffset - (sequenceOffset + 0x30));
  writeLe16(bytes, sequenceOffset + 0x40, trackOffset - (sequenceOffset + 0x40));
  bytes[trackOffset] = 0xa0;

  writeLe16(bytes, instrumentTableOffset, 0);
  bytes[melodicRegionOffset] = 5;
  bytes[melodicRegionOffset + 1] = 0;
  bytes[melodicRegionOffset + 2] = 127;
  bytes[melodicRegionOffset + 7] = 127;

  constexpr u32 sampleOffset = 0x200;
  constexpr u32 artOffset = sampleOffset + 0x40;
  constexpr u32 sampleDataOffset = artOffset + 0x20;
  writeBe32(bytes, sampleOffset, 0x414b414f);
  writeLe16(bytes, sampleOffset + 4, 1);
  writeLe32(bytes, sampleOffset + 0x14, 0x20);
  writeLe32(bytes, sampleOffset + 0x18, 5);
  writeLe32(bytes, sampleOffset + 0x1c, 2);
  writeLe16(bytes, artOffset + 0x0a, 60);
  // The second articulation points to an incomplete eight-byte ADPCM block.
  writeLe32(bytes, artOffset + 0x10, 0x18);
  writeLe32(bytes, artOffset + 0x14, 0x18);
  writeLe16(bytes, artOffset + 0x1a, 60);
  bytes[sampleDataOffset + 1] = 1;

  Session session;
  session.registerFormat(akaoModule());
  session.addSource(SourceFile{.name = "Chrono Cross synthetic.psf"}, bytes);
  session.scanPendingSources();
  const SessionSnapshot project = session.snapshot();

  u32 sequenceAssets = 0;
  u32 instrumentAssets = 0;
  u32 sampleAssets = 0;
  AssetId sequenceId;
  AssetId soundBankId;
  const SoundBankAsset* detectedInstrumentSet = nullptr;
  for (const auto& asset : project.assets()) {
    if (metadata(asset).format != kAkaoFormatName) {
      continue;
    }
    if (const auto* sequence = std::get_if<SequenceProgramAsset>(&asset)) {
      ++sequenceAssets;
      sequenceId = sequence->metadata.id;
    } else if (const auto* soundBank = std::get_if<SoundBankAsset>(&asset)) {
      ++instrumentAssets;
      soundBankId = soundBank->metadata.id;
      detectedInstrumentSet = soundBank;
    } else if (std::holds_alternative<SamplePoolAsset>(asset)) {
      ++sampleAssets;
    }
  }

  expect(sequenceAssets == 1, "Akao synthetic scan should produce one sequence asset");
  expect(sampleAssets == 1, "Akao synthetic scan should produce one sample collection asset");
  expect(instrumentAssets == 1, "Akao synthetic scan should publish one structural instrument set");
  expect(detectedInstrumentSet != nullptr && detectedInstrumentSet->instruments.size() == 1,
         "Akao detected instrument set should expose parsed instruments");
  expect(detectedInstrumentSet->instruments.front().regions.size() == 1 &&
             detectedInstrumentSet->instruments.front().regions.front().sample.empty(),
         "Akao structural regions should remain without samples until collection binding");
  expect(project.collections().size() == 1, "Akao synthetic scan should resolve one collection");
  const auto& collection = project.collections().front();
  expect(collection.selection.sequence == sequenceId, "Akao collection should reference the scanned sequence");
  expect(collection.members().samplePools.size() == 1, "Akao collection should reference the scanned sample collection");
  expect(collection.members().soundBanks == std::vector<AssetId>{soundBankId},
         "Akao collection should reference its detected structural instrument set");
  const auto sequenceHeaders = project.sourceMap().withRole(SourceId{0}, SourceRole::Header);
  const auto header = std::ranges::find_if(sequenceHeaders, [&](SourceAnnotationId id) {
    const SourceAnnotation& annotation = project.sourceMap().get(id);
    return annotation.category() == "akao-sequence-header";
  });
  expect(header != sequenceHeaders.end(), "Akao source map should expose the sequence header annotation");
  const SourceAnnotation& sequenceHeader = project.sourceMap().get(*header);
  expect(sequenceHeader.owner == ObjectRefs::sequence(sequenceId),
         "Akao sequence header annotation should point at the semantic sequence asset");
  const auto trackAnnotations = project.sourceMap().withRole(SourceId{0}, SourceRole::SequenceTrack);
  expect(!trackAnnotations.empty(), "Akao source map should expose decoded track annotations");
  const auto trackAnnotation = std::ranges::find_if(trackAnnotations, [&](SourceAnnotationId id) {
    return project.sourceMap().get(id).owner == ObjectRefs::sequenceTrack(sequenceId, 0);
  });
  expect(trackAnnotation != trackAnnotations.end(),
         "Akao track annotation should point at the semantic sequence track");
  expect(!project.sourceMap().get(*trackAnnotation).parent,
         "Akao track annotation should be a sibling of the sequence header");
  const auto* instrumentLayout =
      annotationWithKind(project.sourceMap(), SourceId{0}, SourceRole::SoundBank, "akao-instrument-set");
  expect(instrumentLayout != nullptr && instrumentLayout->range.offset == instrumentTableOffset &&
             instrumentLayout->range.size == 0x28,
         "Akao detected bank should cover its pointer table and instrument rows");
  expect(instrumentLayout->owner == ObjectRefs::asset(soundBankId),
         "Akao instrument-set annotation should belong to the detected bank asset");
  const auto* instrumentPointers =
      annotationWithKind(project.sourceMap(), SourceId{0}, SourceRole::Table, "akao-instrument-pointer-table");
  expect(instrumentPointers != nullptr && instrumentPointers->range.offset == instrumentTableOffset &&
             instrumentPointers->range.size == 0x20,
         "Akao detected bank should expose the fixed instrument pointer table");
  const auto* firstInstrumentPointer =
      annotationWithKind(project.sourceMap(), SourceId{0}, SourceRole::TableEntry, "akao-instrument-pointer");
  expect(firstInstrumentPointer != nullptr && firstInstrumentPointer->range.offset == instrumentTableOffset &&
             hasLinkRole(*firstInstrumentPointer, SourceLinkRole::PointsTo),
         "Akao instrument pointer entries should expose their target relationship");
  const auto* instrument =
      annotationWithKind(project.sourceMap(), SourceId{0}, SourceRole::Instrument, "akao-instrument");
  expect(instrument != nullptr && instrument->range.offset == melodicRegionOffset && instrument->range.size == 8,
         "Akao scan should annotate parsed instrument data before collection binding");
  expect(instrument->owner == ObjectRefs::instrument(soundBankId, 0),
         "Akao instrument annotations should point into the detected bank");
  const auto* region = annotationWithKind(project.sourceMap(), SourceId{0}, SourceRole::Region, "akao-region");
  expect(region != nullptr && region->range.offset == melodicRegionOffset && region->range.size == 8,
         "Akao scan should annotate parsed regions");
  expect(region->owner == ObjectRefs::region(soundBankId, 0, 0),
         "Akao region annotations should point into the detected bank");
  expect(fieldEquals(fieldWithName(*region, "articulation_id"), u64{5}),
         "Akao region annotation should expose the articulation id");
  expect(!hasLinkRole(*region, SourceLinkRole::UsesSample),
         "Akao structural regions should not claim a sample binding before collection binding");
  const auto inspection = session.inspect(soundBankId);
  expect(inspection != nullptr && inspection->metadata().id == soundBankId &&
             inspection->range().offset == instrumentTableOffset && inspection->bytes().size() == 0x28,
         "Akao instrument sets should be directly inspectable as detected files");
  const auto* articulationTable =
      annotationWithKind(project.sourceMap(), SourceId{0}, SourceRole::Table, "akao-articulation-table");
  expect(articulationTable != nullptr && articulationTable->range.offset == artOffset &&
             articulationTable->range.size == 0x20,
         "Akao sample scan should annotate the articulation table");
  const auto* articulationEntry =
      annotationWithKind(project.sourceMap(), SourceId{0}, SourceRole::TableEntry, "akao-articulation");
  expect(articulationEntry != nullptr && articulationEntry->range.offset == artOffset &&
             articulationEntry->range.size == 0x10,
         "Akao sample scan should annotate articulation entries");
  expect(fieldEquals(fieldWithName(*articulationEntry, "articulation_id"), u64{5}),
         "Akao articulation annotation should expose the articulation id");
  expect(hasLinkRole(*articulationEntry, SourceLinkRole::UsesSample),
         "Akao articulation annotation should link to the sample it resolves to");
  const auto& annotations = project.sourceMap().annotations();
  const auto missingSample = std::ranges::find_if(annotations, [&](const SourceAnnotation& annotation) {
    return annotation.kind == "akao-articulation" && annotation.range.offset == artOffset + 0x10;
  });
  expect(missingSample != annotations.end() && !hasLinkRole(*missingSample, SourceLinkRole::UsesSample),
         "an articulation with an incomplete sample must not bind to sample zero");

  const auto* sequence = project.asset<SequenceProgramAsset>(sequenceId);
  expect(sequence->recipe.banks.size() == 1 && detectedInstrumentSet->recipe.samples.size() == 1,
         "Akao sequence should name its bank, whose recipe owns sample requirements");
  const auto prepared = bindCollection(project, collection.id);
  expect(prepared.collection && prepared.collection->soundBanks().size() == 1 &&
             prepared.collection->soundBanks().front().instruments.front().regions.front().sample.owner() ==
                 collection.members().samplePools.front(),
         "Akao bank preparation should connect the structural instrument regions to its selected samples");

  const auto artifacts = session.exportCollection(collection.id, ExportRequest{.kinds = {ExportKind::Dls}});
  expect(artifacts.size() == 1 && !artifacts[0].bytes.empty(),
         "Akao export should prepare its collection-specific instruments on demand");
  const auto directInstrumentExport = session.exportSoundBank(soundBankId, SynthExportFormat::Dls, ExportRequest{});
  expect(!directInstrumentExport.bytes.empty(),
         "direct Akao instrument-set export should use the bound collection view");
}

void akaoVibratoPreservesDriverRules() {
  // One representative for each distinct clock, note-reset, and waveform family.
  for (auto version : {AkaoPs1Version::Version1_0, AkaoPs1Version::Version1_1,
                       AkaoPs1Version::Version3_0, AkaoPs1Version::Version3_2}) {
    const bool modern = version >= AkaoPs1Version::Version3_0;
    const bool newest = version == AkaoPs1Version::Version3_2;
    const auto performance = renderAkaoFixture({
        0xb5, 0x20, 0xb4, 2, 3, 6, 0xcc, 0x08, 0x08, 0x8c, 0xcd, 0x08,
        0xb5, 0xc0, 0x08, 0xb6, 0xb5, 0x40, 0x08, 0xb4, 2, 0, 15, 0x08, 0xa0}, {}, version);
    auto depths = fixtureEvents<ModulationPerformanceEvent>(performance);
    std::erase_if(depths, [](const auto& event) { return event.target != ModulationPerformanceTarget::VibratoDepth; });
    expect(depths.size() == 4 && *depths[2].pitchDepthSemitones == 0 && *depths[3].pitchDepthSemitones > 0,
           "B5 retains depth while disabled; B4 enables vibrato and B6 cancels it");
    const auto& start = depths[0].context;
    const auto& restart = depths[3].context;
    const double clock = AkaoProfile{version}.driverTickHz();
    const int finalCycleSteps = newest ? 2 : modern ? 10 : 16;
    expect(start.delay->ticks == 2 && start.delay->tempoRelative && !start.cyclesPerTick &&
               std::abs(*start.frequencyHz - clock / (3 * (newest ? 4 : 39))) < 1e-9 &&
               std::abs(*restart.frequencyHz - clock / (256 * finalCycleSteps)) < 1e-9,
           "B4 uses fixed-clock rate, musical-tick delay, and period 256 for a zero rate");
    expect(start.steppedDepthAttackSteps == 4 &&
               start.shape->waveform == (newest ? LfoWaveform::Triangle : LfoWaveform::Sine) &&
               restart.shape->waveform == (newest ? LfoWaveform::Noise : modern ? LfoWaveform::Sine : LfoWaveform::Triangle),
           "B4 selects the driver's waveform numbering and depth buildup");
    expect(start.restartMode == (newest ? LfoRestartMode::Delay : LfoRestartMode::PhaseAndDelay) &&
               depths[1].context.restartMode == LfoRestartMode::None,
           "only v3.2 B4 retains phase; B5 always preserves phase and delay");
    const auto wide = *depths[1].context.pitchRangeSemitones;
    expect(std::abs(wide.minimum - 12 * std::log2(0.75)) < 1e-9 &&
               std::abs(wide.maximum - 12 * std::log2(1.5)) < 1e-9,
           "wide depth preserves the driver's asymmetric pitch excursion");
    const auto notes = fixtureEvents<NotePerformanceEvent>(performance);
    expect(notes.size() == 7 && notes[0].restartsVibratoLfoPhase == true &&
               notes[1].restartsVibratoLfoPhase == !modern &&
               notes[2].restartsVibratoLfoPhase == false && notes[3].restartsVibratoLfoPhase == true,
           "early slurs restart vibrato, v3 slurs preserve phase, and ties never restart it");
    const auto profile = analyzeSequenceModulation(performance);
    expect(profile.instruments.vibrato && profile.instruments.vibrato->delaySeconds &&
               profile.instruments.vibrato->rateHertz.minimum > 0,
           "rate and delay must also reach synth export");
  }
}

void akaoVibratoRateFadeRetargetsAndB4CancelsIt() {
  const auto performance = renderAkaoFixture({
      0xb5, 0x40, 0xb4, 0, 4, 1, 0xe4, 6, 10, 0x06,
      0xe4, 2, 3, 0x06, 0xe4, 8, 20, 0xb4, 0, 2, 1, 0x08, 0xa0}, {}, AkaoPs1Version::Version3_2);
  const auto events = fixtureEvents<ModulationPerformanceEvent>(performance);
  std::vector<double> rates;
  for (const auto& event : events) {
    if (event.target == ModulationPerformanceTarget::VibratoRate) rates.push_back(*event.context.frequencyHz);
  }
  const double quarterClock = AkaoProfile{AkaoPs1Version::Version3_2}.driverTickHz() / 4;
  constexpr double periods[]{4, 5, 6, 7, 5, 3, 2};
  expect(rates.size() == std::size(periods), "B4 must set the rate and cancel any pending E4 fade");
  for (size_t i = 0; i < rates.size(); ++i) {
    expect(std::abs(rates[i] - quarterClock / periods[i]) < 1e-9,
           "E4 must interpolate the period from its current value");
  }
}

void runAkaoFormatTests() {
  akaoVibratoPreservesDriverRules();
  akaoVibratoRateFadeRetargetsAndB4CancelsIt();
  akaoSequenceLayoutRejectsFalsePositiveHeaders();
  akaoSequenceDecodesLegacyRelativeJumpTargets();
  akaoSequenceDecodesConditionalBranchSideTargets();
  akaoSequenceAnalysisUsesSemanticOperands();
  akaoPointerInstrumentsSelectTheirExportedPrograms();
  akaoTablePointersUseNonControlSourceLinks();
  akaoSequenceDecodesRepeatFlowWithoutManualLayerLeaks();
  akaoRepeatSourceLinksUseSpecificRolesOnly();
  akaoVersion10OverlayCommandsUseLegacyLengthsAndProgramChange();
  akaoPanLawFollowsDriverProfile();
  akaoLoopBranchUsesCurrentRepeatPass();
  akaoTieAfterRestDoesNotExtendPreviousNote();
  ff7SlurChangesPitchWithoutAnotherAttack();
  ff7SlurBoundariesRespectRestsTiesLegatoAndRepeats();
  ff7PortamentoEnablesSlurAndStartsWithAFreshAttack(0xdb);
  ff7PortamentoEnablesSlurAndStartsWithAFreshAttack(0xcb);
  ff7ExpressionFadesRetargetAndCancel();
  ff7ReverbSwitchesAndResetReachMidi();
  ff7EnvelopeCommandsKeepNativeStateAndResetOnProgramChange();
  ff7EnvelopeRatesModesAndCombinedCommandCompose();
  ff7CollectionBindsNativeEnvelopesAndPreservesDrumDefaults();
  akaoTempoFadeEmitsDriverTickRamp();
  akaoPitchSlideAppliesOnceToTheNextNote();
  akaoPortamentoRetainsPitchTransitionIntent();
  akaoRequiredArticulationsComeFromInstrumentRows();
  akaoMelodicRegionsDropAdvancingOverlaps();
  akaoSampleSelectionUsesPlayableArticulations();
  akaoScanPublishesStructuralInstrumentSetAndBindsCollectionView();
}
