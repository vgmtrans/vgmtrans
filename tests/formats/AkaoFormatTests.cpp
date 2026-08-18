/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/Akao/Akao.h"
#include "value/export/midi/PerformanceMidiRenderer.h"
#include "value/scan/CollectionDiscovery.h"
#include "value/sequence/SequenceVm.h"
#include "value/session/Session.h"

#include "ValueFormatTestSupport.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
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

TrackProgram decodeFixtureTrack(const std::vector<u8>& bytes, AkaoPs1Version version, u32 start, u32 end,
                                SourceMapBuilder* sourceMap = nullptr, SourceId source = SourceId{20}) {
  const TrackDecodeScope tracks{
      .reader = ByteReader(source, bytes),
      .bytecodeEnd = end,
      .maxCommands = 64,
      .sourceMap = sourceMap,
  };
  return decodeAkaoTrack(version, tracks, 0, start);
}

AkaoSequenceAnalysis analyzeFixtureTrack(const std::vector<u8>& bytes, AkaoPs1Version version, u32 start, u32 end) {
  AkaoSequenceAnalysis analysis;
  analysis.header = AkaoSequenceHeader{
      .offset = 0,
      .length = end,
      .version = version,
  };
  analysis.references = akaoSequenceReferences(decodeFixtureTrack(bytes, version, start, end));
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
  bytes[start] = 0x08;
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

  const MidiSequence midi = renderMidiSequence(performance);
  expect(
      std::ranges::count_if(midi.tracks[0].events,
                            [](const MidiEvent& event) { return std::holds_alternative<NoteDuration>(event); }) == 2 &&
          std::ranges::any_of(midi.tracks[0].events,
                              [](const MidiEvent& event) {
                                const auto* bend = std::get_if<PitchBend>(&event);
                                return bend != nullptr && bend->tick <= 0x24 && bend->value > 0;
                              }) &&
          std::ranges::none_of(midi.tracks[0].events,
                               [](const MidiEvent& event) { return std::holds_alternative<PortamentoControl>(event); }),
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
  expect(transition != nullptr && transition->note == notes[1]->note && !transition->previousNote &&
             transition->startKey == 48.0 && transition->targetKey == 49.0 && transition->timing.timelineTicks == 4 &&
             std::holds_alternative<TempoRelativePitchSlideTiming>(transition->timing.physical),
         "Akao portamento should retain its note anchor, keys, new attack, and tick-relative duration");
  expect(std::ranges::none_of(performance.tracks[0].events,
                              [](const PerformanceEvent& event) {
                                return std::holds_alternative<PortamentoPerformanceEvent>(event) ||
                                       std::holds_alternative<PortamentoEnablePerformanceEvent>(event) ||
                                       std::holds_alternative<PortamentoTimePerformanceEvent>(event) ||
                                       std::holds_alternative<PortamentoControlPerformanceEvent>(event);
                              }),
         "Akao format code should not preselect a MIDI portamento representation");

  const MidiSequence native = renderMidiSequence(
      performance, MidiExportOptions{.pitchTransitions = MidiPitchTransitionRendering::PreserveFormat});
  expect(std::ranges::any_of(native.tracks[0].events,
                             [](const MidiEvent& event) {
                               const auto* control = std::get_if<PortamentoControl>(&event);
                               return control != nullptr && control->tick == 16 && control->key == 48;
                             }),
         "preserve-format MIDI should lower Akao portamento with its explicit source key");

  const MidiSequence pitchBend =
      renderMidiSequence(performance, MidiExportOptions{.pitchTransitions = MidiPitchTransitionRendering::PitchBend});
  expect(std::ranges::none_of(pitchBend.tracks[0].events,
                              [](const MidiEvent& event) {
                                return std::holds_alternative<PortamentoTime>(event) ||
                                       std::holds_alternative<PortamentoTime14>(event) ||
                                       std::holds_alternative<PortamentoControl>(event);
                              }) &&
             std::ranges::any_of(pitchBend.tracks[0].events,
                                 [](const MidiEvent& event) {
                                   const auto* bend = std::get_if<PitchBend>(&event);
                                   return bend != nullptr && bend->tick >= 16 && bend->value != 0;
                                 }) &&
             std::ranges::any_of(pitchBend.tracks[0].events,
                                 [](const MidiEvent& event) {
                                   const auto* note = std::get_if<NoteDuration>(&event);
                                   return note != nullptr && note->tick == 16 && note->key == 49;
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

void akaoSampleSelectionKeepsPreferredAndRequiredCollections() {
  const std::vector<AkaoSampleCoverageProvider> candidates{
      AkaoSampleCoverageProvider{.index = 0, .sampleSetId = 0, .first = 0, .count = 32},
      AkaoSampleCoverageProvider{.index = 1, .sampleSetId = 5, .first = 32, .count = 81},
      AkaoSampleCoverageProvider{.index = 2, .sampleSetId = 29, .first = 128, .count = 22},
  };
  const std::vector<u32> required{32, 128};

  const auto selected = selectAkaoSampleCoverage(29, required, candidates);
  expect(selected.providers == std::vector<std::size_t>{1, 2} && selected.missing.empty() &&
             selected.requestedSampleSetFound,
         "Akao sample selection should combine the preferred sample set with required-articulation coverage");
}

void akaoCollectionPrefersCompleteSamplesFromSequenceSource() {
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
      .privateData = AssetPrivateData::make(AkaoSequenceData{
          .sequenceId = 1,
          .sampleSetId = 7,
          .requiredArticulations = {5},
          .structuralInstrumentSet = bankId,
      }),
  });
  assets.emplace_back(SoundBankAsset{
      .metadata = AssetMetadata{.id = bankId,
                                .format = std::string(kAkaoFormatName),
                                .name = "Bank",
                                .range = sources.reader(sequenceSource).range(40, 20)},
  });
  const auto samples = [&](AssetId id, SourceId source, u64 offset) {
    return SamplePoolAsset{
        .metadata = AssetMetadata{.id = id,
                                  .format = std::string(kAkaoFormatName),
                                  .name = "Samples",
                                  .range = sources.reader(source).range(offset, 20)},
        .privateData = AssetPrivateData::make(AkaoSamplePoolData{
            .sampleSetId = 7,
            .firstArticulationId = 0,
            .articulationCount = 16,
        }),
    };
  };
  assets.emplace_back(samples(localSamplesId, sequenceSource, 100));
  assets.emplace_back(samples(unrelatedSamplesId, unrelatedSource, 900));

  const CollectionDiscoveryContext context(sources, SharedSequence<Asset>{std::move(assets)});
  const auto collections = resolveAkaoCollections(context);
  expect(collections.size() == 1 && collections.front().members.samplePools == std::vector{localSamplesId},
         "Akao matching should not let a newer unrelated pool outrank complete local samples");
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
  constexpr u32 sampleDataOffset = artOffset + 0x10;
  writeBe32(bytes, sampleOffset, 0x414b414f);
  writeLe16(bytes, sampleOffset + 4, 1);
  writeLe32(bytes, sampleOffset + 0x14, 0x10);
  writeLe32(bytes, sampleOffset + 0x18, 5);
  writeLe32(bytes, sampleOffset + 0x1c, 1);
  writeLe16(bytes, artOffset + 0x0a, 60);
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
  expect(collection.members.sequence == sequenceId, "Akao collection should reference the scanned sequence");
  expect(collection.members.samplePools.size() == 1, "Akao collection should reference the scanned sample collection");
  expect(collection.members.soundBanks == std::vector<AssetId>{soundBankId},
         "Akao collection should reference its detected structural instrument set");
  const auto sequenceHeaders = project.sourceMap().withRole(SourceId{0}, SourceRole::Header);
  const auto header = std::ranges::find_if(sequenceHeaders, [&](SourceAnnotationId id) {
    const SourceAnnotation& annotation = project.sourceMap().get(id);
    return annotation.localKind == "akao-sequence-header";
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
             articulationTable->range.size == 0x10,
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

  const auto* sequence = project.asset<SequenceProgramAsset>(sequenceId);
  const auto* sequenceData = sequence->privateData.get<AkaoSequenceData>();
  expect(sequenceData != nullptr && sequenceData->requiredArticulations == std::vector<u32>{5},
         "Akao articulation requirements should remain typed sequence data");
  SequenceRuntime runtime = sequence->program.runtime;
  const SoundBankAsset foreignBank{
      .metadata = AssetMetadata{.id = AssetId{999}, .format = "Foreign", .name = "Foreign Bank"},
      .instruments = {Instrument{
          .explicitAddress = InstrumentAddress{.bank = 42, .program = 7},
          .name = "Foreign Instrument",
      }},
  };
  std::vector<SoundBankAsset> boundInstruments{
      foreignBank,
      *project.asset<SoundBankAsset>(collection.members.soundBanks.front()),
  };
  std::vector<const SamplePoolAsset*> boundSamples;
  for (const AssetId id : collection.members.samplePools) {
    boundSamples.push_back(project.asset<SamplePoolAsset>(id));
  }
  std::vector<Diagnostic> bindingDiagnostics;
  CollectionBindingContext binding{
      sequence, runtime, boundInstruments, boundSamples, {}, bindingDiagnostics,
  };
  bindAkaoCollection(binding);
  expect(boundInstruments.size() == 2 && boundInstruments.front().metadata.id == foreignBank.metadata.id &&
             boundInstruments.front().instruments.front().name == "Foreign Instrument" &&
             boundInstruments.back().metadata.id == collection.members.soundBanks.front() &&
             boundInstruments.back().instruments.size() == 1 &&
             boundInstruments.back().instruments.front().regions.size() == 1 &&
             boundInstruments.back().instruments.front().regions.front().sample.owner() ==
                 collection.members.samplePools.front(),
         "Akao binding should locate its exact structural bank, preserve foreign members, and connect its samples");

  const auto artifacts = session.exportCollection(collection.id, ExportRequest{.kinds = {ExportKind::Dls}});
  expect(artifacts.size() == 1 && !artifacts[0].bytes.empty(),
         "Akao export should prepare its collection-specific instruments on demand");
  const auto directInstrumentExport = session.exportSoundBank(soundBankId, SynthExportFormat::Dls, ExportRequest{});
  expect(!directInstrumentExport.bytes.empty(),
         "direct Akao instrument-set export should use the bound collection view");
}
