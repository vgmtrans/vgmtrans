/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/AkaoSnes/AkaoSnes.h"
#include "value/formats/ValueFormats.h"
#include "value/export/midi/PerformanceMidiRenderer.h"
#include "value/sequence/SequenceVm.h"
#include "value/session/Session.h"
#include "value/synth/SnesDsp.h"
#include "ValueFormatTestSupport.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cmath>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

using namespace vgmtrans::core;
using namespace vgmtrans::formats::akao_snes;

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

u8 endOpcode(AkaoSnesProfile profile) {
  switch (profile.version) {
    case AKAOSNES_V1:
      return 0xf1;
    case AKAOSNES_V2:
      return 0xf8;
    case AKAOSNES_V3:
      return 0xf2;
    case AKAOSNES_V4:
    default:
      return 0xec;
  }
}

TrackProgram decodeTrack(const std::vector<u8>& bytes, AkaoSnesProfile profile, u32 start, u32 end, u32 trackNumber = 0,
                         std::vector<Diagnostic>* diagnostics = nullptr) {
  return decodeAkaoSnesSourceTrack(ByteReader(SourceId{8}, bytes), AkaoSnesTrackDecodeOptions{
                                                                       .profile = profile,
                                                                       .sourceTrackNumber = trackNumber,
                                                                       .startAddress = start,
                                                                       .bytecodeEnd = end,
                                                                       .diagnostics = diagnostics,
                                                                   });
}

PerformanceSequence renderTracks(AkaoSnesProfile profile, std::vector<TrackProgram> tracks,
                                 SequenceVmOptions options = SequenceVmOptions{.loopPolicy = LoopPolicy::PlayOnce}) {
  const auto& dialect = akaoSnesSequenceDialect();
  const SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .config = SequenceProgramConfig{.profile = encodeAkaoSnesProfile(profile)},
      .tracks = std::move(tracks),
  };
  return SequenceVm(options).render(program, dialect);
}

template <class Event>
std::vector<const Event*> eventsOfType(const PerformanceTrack& track) {
  std::vector<const Event*> events;
  for (const PerformanceEvent& event : track.events) {
    if (const auto* typed = std::get_if<Event>(&event)) {
      events.push_back(typed);
    }
  }
  return events;
}

const SourceAnnotation* annotationWithKind(const SourceMap& sourceMap, SourceId source, SourceRole role,
                                           std::string_view kind) {
  const auto annotations = sourceMap.withRole(source, role);
  const auto found =
      std::ranges::find_if(annotations, [&](SourceAnnotationId id) { return sourceMap.get(id).localKind == kind; });
  return found == annotations.end() ? nullptr : sourceMap.find(*found);
}

bool hasLinkRole(const SourceAnnotation& annotation, SourceLinkRole role) {
  return std::ranges::any_of(annotation.links, [role](const SourceLink& link) { return link.role == role; });
}

template <size_t Size>
void writeBytes(std::vector<u8>& bytes, size_t offset, const std::array<u8, Size>& values) {
  std::ranges::copy(values, bytes.begin() + static_cast<std::ptrdiff_t>(offset));
}

std::vector<u8> makeAkaoSnesAram() {
  std::vector<u8> bytes(0x10000);

  constexpr std::array<u8, 10> readNoteLengthV1{0xcd, 0x0f, 0x8d, 0x00, 0x9e, 0xf8, 0x27, 0xf6, 0xb1, 0x18};
  writeBytes(bytes, 0x0100, readNoteLengthV1);

  constexpr std::array<u8, 20> vcmdExecFF4{0xa8, 0xd2, 0x1c, 0xfd, 0xf6, 0xee, 0x17, 0x2d, 0xf6, 0xed,
                                           0x17, 0x2d, 0xdd, 0x5c, 0xfd, 0xf6, 0x49, 0x18, 0xf0, 0x0a};
  writeBytes(bytes, 0x0200, vcmdExecFF4);
  writeLe16(bytes, 0x0200 + 9, 0x1900);
  writeLe16(bytes, 0x0200 + 16, 0x1800);

  constexpr std::array<u8, 46> ff4Lengths{0x03, 0x03, 0x01, 0x02, 0x03, 0x03, 0x03, 0x03, 0x01, 0x01, 0x01, 0x01,
                                          0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x03, 0x02, 0x03,
                                          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  writeBytes(bytes, 0x1800, ff4Lengths);

  constexpr std::array<u8, 20> readSeqHeaderV1{0x8d, 0x01, 0xcb, 0x8d, 0xcd, 0x00, 0xf5, 0x00, 0x20, 0xd4,
                                               0x02, 0xf5, 0x01, 0x20, 0xd4, 0x03, 0xf0, 0x0a, 0xdb, 0x48};
  writeBytes(bytes, 0x0300, readSeqHeaderV1);
  writeLe16(bytes, 0x0300 + 7, 0x2000);

  constexpr std::array<u8, 7> loadDirV1{0xe8, 0x1e, 0x8d, 0x5d, 0x3f, 0xe9, 0x10};
  writeBytes(bytes, 0x0400, loadDirV1);
  bytes[0x0400 + 1] = 0x50;

  constexpr std::array<u8, 11> loadInstrV1{0xd5, 0xc1, 0x02, 0xfd, 0xf6, 0x00, 0xff, 0xd5, 0x00, 0x03, 0x6f};
  writeBytes(bytes, 0x0500, loadInstrV1);
  bytes[0x0500 + 6] = 0x52;

  writeLe16(bytes, 0x2000, 0x2100);
  for (size_t i = 1; i < 8; ++i) {
    writeLe16(bytes, 0x2000 + i * 2, 0);
  }
  bytes[0x2100] = 0xda;
  bytes[0x2101] = 5;
  bytes[0x2102] = 0xdb;
  bytes[0x2103] = 0;
  bytes[0x2104] = 0x00;
  bytes[0x2105] = 0xf1;

  writeLe16(bytes, 0x5000, 0x6000);
  writeLe16(bytes, 0x5002, 0x6000);
  bytes[0x5200] = 0;
  bytes[0x6000] = 0x01;

  return bytes;
}

}  // namespace

void akaoSnesLayoutDiscoversFf4StyleAram() {
  const auto bytes = makeAkaoSnesAram();
  const auto layout = findAkaoSnesLayout(ByteReader(SourceId{8}, bytes));
  expect(layout.has_value(), "AkaoSnes synthetic FF4 ARAM should match scanner patterns");
  expect(layout->version == AKAOSNES_V1, "synthetic FF4 ARAM should be classified as AkaoSnes V1");
  expect(layout->minorVersion == AKAOSNES_V1_FF4, "synthetic FF4 ARAM should be classified as FF4 minor version");
  expect(layout->sequenceHeaderAddress == 0x2000, "sequence header address should come from legacy V1 header reader");
  expect(layout->spcDirAddress == 0x5000, "SPC DIR address should come from legacy V1 DIR loader");
  expect(layout->tuningTableAddress == 0x5200, "V1 tuning table address should come from legacy instrument loader");
  expect(!layout->adsrTableAddress, "V1 layouts should not invent an ADSR table at ARAM offset zero");
}

void akaoSnesModuleDiscoversSequenceInstrumentsAndSamples() {
  Session session;
  vgmtrans::formats::registerValueFormats(session);
  const SourceId source = session.addSource(SourceFile{.name = "ff4-test.spc"}, makeAkaoSnesAram());
  session.scanSource(source);
  const SessionSnapshot project = session.snapshot();

  expect(project.diagnostics().empty(), "AkaoSnes synthetic scan should not report diagnostics");
  expect(project.collections().size() == 1, "AkaoSnes synthetic scan should produce one collection");
  expect(project.assets().size() == 3, "AkaoSnes synthetic scan should produce sequence, instrument set, and samples");

  const auto* sequence = std::get_if<SequenceProgramAsset>(&project.assets()[0]);
  expect(sequence != nullptr, "first AkaoSnes asset should be a sequence");
  expect(sequence->metadata.format == "AkaoSnes", "sequence should retain AkaoSnes format name");
  expect(sequence->program.timebase.ppqn == kAkaoSnesPpqn, "AkaoSnes sequence should use SNES PPQN");
  expect(sequence->program.tracks.size() == 1, "null V1 track pointers should be skipped");

  const auto* dialect = session.formats().findDialect(sequence->program.dialect.value);
  expect(dialect != nullptr, "registered AkaoSnes dialect should render the scanned sequence");
  const PerformanceSequence performance = SequenceVm(LoopPolicy::PlayOnce).render(sequence->program, *dialect);
  expect(performance.diagnostics.empty(), "AkaoSnes performance render should not report diagnostics");
  expect(!performance.tracks.empty(), "AkaoSnes performance should contain a track");
  const bool hasNote = std::ranges::any_of(performance.tracks[0].events, [](const PerformanceEvent& event) {
    return std::holds_alternative<NotePerformanceEvent>(event);
  });
  expect(hasNote, "AkaoSnes synthetic track should render a note event");

  const auto* instrumentSet = std::get_if<InstrumentSetAsset>(&project.assets()[1]);
  expect(instrumentSet != nullptr, "second AkaoSnes asset should be an instrument set");
  expect(!instrumentSet->instruments.empty(), "AkaoSnes instrument set should contain at least one instrument");

  const auto* samples = std::get_if<SampleCollectionAsset>(&project.assets()[2]);
  expect(samples != nullptr, "third AkaoSnes asset should be a sample collection");
  expect(samples->samples.samples.size() == 1, "AkaoSnes synthetic scan should collect one used BRR sample");

  const auto pointers = project.sourceMap().withRole(source, SourceRole::Pointer);
  expect(pointers.size() == 1, "AkaoSnes source map should retain the one non-null track pointer");
  const SourceAnnotation& pointer = project.sourceMap().get(pointers.front());
  expect(fieldEquals(fieldWithName(pointer, "stored_destination"), u64{0x2100}) &&
             fieldEquals(fieldWithName(pointer, "destination"), u64{0x2100}),
         "AkaoSnes track pointers should expose both stored and effective destinations");

  const auto tuningEntries = project.sourceMap().withRole(source, SourceRole::Instrument);
  const auto tuning = std::ranges::find_if(tuningEntries, [&](SourceAnnotationId id) {
    return project.sourceMap().get(id).localKind == "akao-snes-tuning-entry";
  });
  expect(tuning != tuningEntries.end() &&
             project.sourceMap().get(*tuning).range == SourceRange{.source = source, .offset = 0x5200, .size = 1},
         "AkaoSnes instruments should retain their exact tuning records");
  const SourceAnnotation& tuningEntry = project.sourceMap().get(*tuning);
  expect(fieldEquals(fieldWithName(tuningEntry, "tuning"), u64{0}),
         "AkaoSnes tuning records should retain their raw source fields");

  const SourceAnnotation* region =
      annotationWithKind(project.sourceMap(), source, SourceRole::Region, "akao-snes-region");
  expect(region != nullptr && region->owner == ObjectRefs::region(instrumentSet->metadata.id, 0, 0) &&
             hasLinkRole(*region, SourceLinkRole::UsesSample),
         "AkaoSnes region annotations should own stable dense objects and link to their samples");
  const SourceAnnotation* directory =
      annotationWithKind(project.sourceMap(), source, SourceRole::Table, "snes-sample-dir");
  const SourceAnnotation* directoryEntry =
      annotationWithKind(project.sourceMap(), source, SourceRole::Sample, "akao-snes-sample-dir-entry");
  const SourceAnnotation* payload =
      annotationWithKind(project.sourceMap(), source, SourceRole::Payload, "snes-brr-payload");
  const SourceAnnotation* adsrTable =
      annotationWithKind(project.sourceMap(), source, SourceRole::Table, "akao-snes-adsr-table");
  expect(directory != nullptr && directory->range == SourceRange{.source = source, .offset = 0x5000, .size = 4} &&
             directoryEntry != nullptr &&
             directoryEntry->range == SourceRange{.source = source, .offset = 0x5000, .size = 4} &&
             payload != nullptr && payload->range == SourceRange{.source = source, .offset = 0x6000, .size = 9},
         "AkaoSnes synth source maps should retain the exact DIR entry and BRR payload ranges");
  expect(adsrTable == nullptr, "V1 synth source maps should not contain a nonexistent ADSR table");
}

void akaoSnesCompilerCursorResolvesRelocatedBranchesWithoutRetainingBytes() {
  std::vector<u8> bytes(0x80, 0xeb);
  constexpr u32 start = 0x20;
  bytes[start] = 0xf6;
  writeLe16(bytes, start + 1, 0x10);
  bytes[0x40] = 0xeb;

  ScanIdAllocator ids;
  SourceMapBuilder sourceMap([&ids]() { return ids.nextSourceAnnotationId(); });
  std::vector<Diagnostic> diagnostics;
  const TrackProgram track =
      decodeAkaoSnesSourceTrack(ByteReader(SourceId{8}, bytes),
                                AkaoSnesTrackDecodeOptions{
                                    .profile = AkaoSnesProfile{.version = AKAOSNES_V4, .minorVersion = AKAOSNES_V4_FF6},
                                    .startAddress = start,
                                    .bytecodeEnd = static_cast<u32>(bytes.size()),
                                    .romRelocBase = 0x10,
                                    .apuRelocBase = 0x40,
                                    .sourceMap = &sourceMap,
                                    .diagnostics = &diagnostics,
                                });
  const SourceMap annotations = sourceMap.finish();

  expect(
      track.commands.size() == 2 && track.commands[0].address.value == start && track.commands[1].address.value == 0x40,
      "AkaoSnes reachable decode should follow the effective relocated jump target");
  const SourceAnnotation& jump = commandAnnotation(annotations, track.commands[0]);
  expect(fieldEquals(fieldWithName(jump, "stored_destination"), u64{0x10}) &&
             fieldEquals(fieldWithName(jump, "destination"), u64{0x40}),
         "relocated branch annotations should retain raw and effective addresses");
  const auto target =
      std::ranges::find_if(jump.links, [](const SourceLink& link) { return link.role == SourceLinkRole::LoopTarget; });
  const auto* targetRange = target == jump.links.end() ? nullptr : std::get_if<SourceRange>(&target->target);
  expect(targetRange != nullptr && targetRange->offset == 0x40,
         "relocated jump links should point to the effective source address");
  expect(diagnostics.empty(), "valid relocated AkaoSnes commands should decode without diagnostics");

  std::vector<u8> breakBytes(0x80, 0xec);
  breakBytes[start] = 0xf5;
  breakBytes[start + 1] = 1;
  writeLe16(breakBytes, start + 2, 0x10);
  breakBytes[start + 4] = 0xec;
  breakBytes[0x40] = 0xec;
  const TrackProgram loopBreak =
      decodeAkaoSnesSourceTrack(ByteReader(SourceId{8}, breakBytes),
                                AkaoSnesTrackDecodeOptions{
                                    .profile = AkaoSnesProfile{.version = AKAOSNES_V4, .minorVersion = AKAOSNES_V4_FF6},
                                    .startAddress = start,
                                    .bytecodeEnd = static_cast<u32>(breakBytes.size()),
                                    .romRelocBase = 0x10,
                                    .apuRelocBase = 0x40,
                                });
  const SemanticOperand* breakTarget = semanticOperand(loopBreak.commands.front(), "destination");
  const auto* storedBreakTarget =
      breakTarget == nullptr || !breakTarget->encodedValue ? nullptr : std::get_if<u64>(&*breakTarget->encodedValue);
  const auto* effectiveBreakTarget = breakTarget == nullptr ? nullptr : std::get_if<Address>(&breakTarget->value);
  expect(storedBreakTarget != nullptr && *storedBreakTarget == 0x10 && effectiveBreakTarget != nullptr &&
             effectiveBreakTarget->value == 0x40,
         "relocated loop breaks should retain stored targets and execute with effective targets");
}

void akaoSnesCompilerCursorCoversVersionBoundariesAndDurations() {
  struct VersionCase {
    AkaoSnesProfile profile;
    std::vector<u8> durations;
  };
  const std::array<VersionCase, 4> cases{
      VersionCase{
          .profile = AkaoSnesProfile{.version = AKAOSNES_V1, .minorVersion = AKAOSNES_V1_FF4},
          .durations = {0xc0, 0x90, 0x60, 0x48, 0x40, 0x30, 0x24, 0x20, 0x18, 0x10, 0x0c, 0x08, 0x06, 0x04, 0x03},
      },
      VersionCase{
          .profile = AkaoSnesProfile{.version = AKAOSNES_V2, .minorVersion = AKAOSNES_V2_RS1},
          .durations = {0xc0, 0x90, 0x60, 0x40, 0x48, 0x30, 0x20, 0x24, 0x18, 0x10, 0x0c, 0x08, 0x06, 0x04, 0x03},
      },
      VersionCase{
          .profile = AkaoSnesProfile{.version = AKAOSNES_V3, .minorVersion = AKAOSNES_V3_FF5},
          .durations = {0xc0, 0x90, 0x60, 0x40, 0x48, 0x30, 0x20, 0x24, 0x18, 0x10, 0x0c, 0x08, 0x06, 0x04, 0x03},
      },
      VersionCase{
          .profile = AkaoSnesProfile{.version = AKAOSNES_V4, .minorVersion = AKAOSNES_V4_FF6},
          .durations = {0xc0, 0x60, 0x40, 0x48, 0x30, 0x20, 0x24, 0x18, 0x10, 0x0c, 0x08, 0x06, 0x04, 0x03},
      },
  };

  constexpr u32 start = 0x20;
  for (const VersionCase& versionCase : cases) {
    std::vector<u8> bytes(0x100, endOpcode(versionCase.profile));
    for (size_t index = 0; index < versionCase.durations.size(); ++index) {
      bytes[start + index] = static_cast<u8>(index);
    }
    bytes[start + versionCase.durations.size()] = endOpcode(versionCase.profile);

    TrackProgram track =
        decodeTrack(bytes, versionCase.profile, start, start + static_cast<u32>(versionCase.durations.size()) + 1);
    expect(track.commands.size() == versionCase.durations.size() + 1,
           "each AkaoSnes duration-table entry should decode as one note command");

    const PerformanceSequence performance = renderTracks(versionCase.profile, {std::move(track)});
    expect(performance.diagnostics.empty(), "duration-table fixtures should render without diagnostics");
    const auto notes = eventsOfType<NotePerformanceEvent>(performance.tracks.front());
    expect(notes.size() == versionCase.durations.size(), "every duration-table note should render");
    u64 expectedTick = 0;
    for (size_t index = 0; index < notes.size(); ++index) {
      const u8 length = versionCase.durations[index];
      expect(notes[index]->header.tick == expectedTick,
             "AkaoSnes duration-table notes should advance by their exact source duration");
      expect(notes[index]->durationTicks == (length > 2 ? length - 2 : 1),
             "AkaoSnes duration-table notes should preserve the engine's two-tick key-off gap");
      expectedTick += length;
    }

    std::vector<u8> boundaryBytes(0x100, endOpcode(versionCase.profile));
    const u8 statusMax = akaoSnesStatusNoteMax(versionCase.profile.version);
    boundaryBytes[start] = statusMax;
    boundaryBytes[start + 1] = static_cast<u8>(statusMax + 1);
    boundaryBytes[start + 2] = 0;
    boundaryBytes[start + 3] = 0;
    boundaryBytes[start + 4] = 0;
    boundaryBytes[start + 5] = endOpcode(versionCase.profile);
    const TrackProgram boundary = decodeTrack(boundaryBytes, versionCase.profile, start, start + 6);
    expect(boundary.commands.size() >= 2 && semanticOperand(boundary.commands[0], "note_index") != nullptr &&
               semanticOperand(boundary.commands[1], "note_index") == nullptr,
           "each AkaoSnes version should switch from status notes to commands at its exact opcode boundary");
  }
}

void akaoSnesV3DynamicAdsrUsesAttackAndHeldNoteDecay() {
  constexpr u32 start = 0x20;
  std::vector<u8> bytes(0x40, 0xf2);
  std::ranges::copy(std::initializer_list<u8>{0xea, 4, 0xeb, 0xff, 0xee, 0xe5, 0xef, 0xf2},
                    bytes.begin() + start);

  const AkaoSnesProfile ff5{.version = AKAOSNES_V3, .minorVersion = AKAOSNES_V3_FF5};
  const TrackProgram ff5Track = decodeTrack(bytes, ff5, start, start + 8);
  const SemanticOperand* attackRate = semanticOperand(ff5Track.commands[1], "dsp_attack_rate");
  const SemanticOperand* sustainRate = semanticOperand(ff5Track.commands[2], "dsp_sustain_rate");
  expect(attackRate != nullptr && std::get<u64>(attackRate->value) == 15 && sustainRate != nullptr &&
             std::get<u64>(sustainRate->value) == 5,
         "FF5 ADSR commands should apply the driver's four- and five-bit rate masks");

  const PerformanceSequence ff5Performance = renderTracks(ff5, {ff5Track});
  const auto instruments = eventsOfType<InstrumentPerformanceEvent>(ff5Performance.tracks.front());
  const auto envelopes = eventsOfType<EnvelopePerformanceEvent>(ff5Performance.tracks.front());
  expect(instruments.size() == 1 &&
             instruments[0]->envelopeMode == InstrumentEnvelopeMode::UseInstrumentEnvelope,
         "FF5 program changes should select the instrument's native envelope");
  expect(envelopes.size() == 3, "FF5 ADSR attack, sustain rate, and default should emit envelope state");

  expect(envelopes[0]->scope == VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks &&
             envelopes[0]->update.fields == EnvelopeFields::Attack &&
             envelopes[0]->update.values == Envelope{.attackSeconds = snesDspAdsrAttackSeconds(15)},
         "FF5 ADSR attack should update only the attack stage");

  expect(envelopes[1]->scope == VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks &&
             envelopes[1]->update.fields == EnvelopeFields::SecondDecay &&
             envelopes[1]->update.values == Envelope{.secondDecaySeconds = snesDspAdsrSustainSeconds(5)},
         "FF5 ADSR sustain rate should control held-note decay rather than note-off release");
  expect(envelopes[2]->scope == VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks &&
             !envelopes[2]->update.values && envelopes[2]->update.fields == EnvelopeFields::All,
         "FF5 ADSR default should restore the instrument envelope for active and future voices");

  std::vector<u8> sd2Bytes(0x40, 0xf2);
  std::ranges::copy(std::initializer_list<u8>{0xea, 4, 0xee, 0x00, 0x00, 0xf2}, sd2Bytes.begin() + start);
  const AkaoSnesProfile sd2{.version = AKAOSNES_V3, .minorVersion = AKAOSNES_V3_SD2};
  const PerformanceSequence sd2Performance = renderTracks(sd2, {decodeTrack(sd2Bytes, sd2, start, start + 6)});
  const auto sd2Envelopes = eventsOfType<EnvelopePerformanceEvent>(sd2Performance.tracks.front());
  expect(sd2Envelopes.size() == 1 && sd2Envelopes[0]->update.fields == EnvelopeFields::SecondDecay &&
             sd2Envelopes[0]->update.values && sd2Envelopes[0]->update.values->secondDecaySeconds &&
             std::isinf(*sd2Envelopes[0]->update.values->secondDecaySeconds),
         "Secret of Mana EE 00 should disable held-note decay");
}

void akaoSnesCompilerCursorCoversRemapsUnknownsAndTruncation() {
  constexpr u32 start = 0x20;
  const AkaoSnesProfile ff6{.version = AKAOSNES_V4, .minorVersion = AKAOSNES_V4_FF6};
  const AkaoSnesProfile rs3{.version = AKAOSNES_V4, .minorVersion = AKAOSNES_V4_RS3};

  std::vector<u8> ff6Bytes(0x40, 0xec);
  ff6Bytes[start] = 0xf4;
  ff6Bytes[start + 1] = 0xfe;
  ff6Bytes[start + 2] = 0xec;
  const PerformanceSequence ff6Performance = renderTracks(ff6, {decodeTrack(ff6Bytes, ff6, start, start + 3)});
  expect(eventsOfType<MasterLevelPerformanceEvent>(ff6Performance.tracks.front()).size() == 1,
         "V4 FF6 opcode F4 should decode as master volume");

  std::vector<u8> rs3Bytes = ff6Bytes;
  const PerformanceSequence rs3Performance = renderTracks(rs3, {decodeTrack(rs3Bytes, rs3, start, start + 3)});
  expect(eventsOfType<ExpressionPerformanceEvent>(rs3Performance.tracks.front()).size() == 1,
         "V4 RS3 opcode F4 should decode as expression");

  const AkaoSnesProfile sd2{.version = AKAOSNES_V3, .minorVersion = AKAOSNES_V3_SD2};
  const AkaoSnesProfile ff5{.version = AKAOSNES_V3, .minorVersion = AKAOSNES_V3_FF5};
  std::vector<u8> v3Bytes(0x40, 0xf2);
  v3Bytes[start] = 0xfc;
  v3Bytes[start + 1] = 0xf2;
  expect(decodeTrack(v3Bytes, sd2, start, start + 2).commands.size() == 2 &&
             decodeTrack(v3Bytes, ff5, start, start + 2).commands.size() == 1,
         "V3 SD2 opcode FC should fall through as loop restart while FF5 treats it as end");

  struct UnknownCase {
    AkaoSnesProfile profile;
    u8 opcode = 0;
    u8 operandCount = 0;
  };
  const std::array<UnknownCase, 3> unknowns{
      UnknownCase{AkaoSnesProfile{.version = AKAOSNES_V1, .minorVersion = AKAOSNES_V1_FF4}, 0xf6, 0},
      UnknownCase{AkaoSnesProfile{.version = AKAOSNES_V4, .minorVersion = AKAOSNES_V4_GH}, 0xeb, 1},
      UnknownCase{AkaoSnesProfile{.version = AKAOSNES_V2, .minorVersion = AKAOSNES_V2_RS1}, 0xf6, 2},
  };
  for (const UnknownCase& unknown : unknowns) {
    std::vector<u8> bytes(0x40, endOpcode(unknown.profile));
    bytes[start] = unknown.opcode;
    for (u8 index = 0; index < unknown.operandCount; ++index) {
      bytes[start + 1 + index] = static_cast<u8>(0xa0 + index);
    }
    bytes[start + 1 + unknown.operandCount] = endOpcode(unknown.profile);
    const TrackProgram track = decodeTrack(bytes, unknown.profile, start, start + 2 + unknown.operandCount);
    expect(!track.commands.empty() && track.commands.front().encodedSize == 1 + unknown.operandCount &&
               !track.commands.front().execution.valid(),
           "bounded unknown AkaoSnes commands should retain their exact length as non-executable semantic IR");
  }

  std::vector<u8> truncatedBytes(0x40, 0);
  truncatedBytes[start] = 0xc9;
  truncatedBytes[start + 1] = 1;
  std::vector<Diagnostic> diagnostics;
  const TrackProgram truncated = decodeTrack(truncatedBytes, ff6, start, start + 2, 0, &diagnostics);
  expect(truncated.commands.size() == 1 && truncated.commands.front().range.offset == start &&
             truncated.commands.front().range.size == 2,
         "a truncated AkaoSnes operand should stop at and retain only the exact available source range");
  expect(!diagnostics.empty(), "a truncated AkaoSnes operand should report a diagnostic");

  ScanIdAllocator ids;
  SourceMapBuilder sourceMap([&ids]() { return ids.nextSourceAnnotationId(); });
  std::vector<u8> stateBytes(0x40, 0xec);
  stateBytes[start] = 0xe4;
  const TrackProgram stateTrack = decodeAkaoSnesSourceTrack(
      ByteReader(SourceId{8}, stateBytes),
      AkaoSnesTrackDecodeOptions{
          .profile = ff6, .startAddress = start, .bytecodeEnd = start + 2, .sourceMap = &sourceMap});
  const SourceMap stateAnnotations = sourceMap.finish();
  const SourceAnnotation& slur = commandAnnotation(stateAnnotations, stateTrack.commands.front());
  expect(slur.label == "Slur On" && slur.sequenceSemantic == SequenceSemantic::State &&
             slur.playbackStatus == CommandPlaybackStatus::AffectsPlayback,
         "state-changing commands should expose exact playback-affecting presentation");

  ScanIdAllocator unknownIds;
  SourceMapBuilder unknownSourceMap([&unknownIds]() { return unknownIds.nextSourceAnnotationId(); });
  const AkaoSnesProfile ff4{.version = AKAOSNES_V1, .minorVersion = AKAOSNES_V1_FF4};
  std::vector<u8> unknownBytes(0x40, 0xf1);
  unknownBytes[start] = 0xf6;
  const TrackProgram unknownTrack = decodeAkaoSnesSourceTrack(
      ByteReader(SourceId{8}, unknownBytes),
      AkaoSnesTrackDecodeOptions{
          .profile = ff4, .startAddress = start, .bytecodeEnd = start + 2, .sourceMap = &unknownSourceMap});
  const SourceMap unknownAnnotations = unknownSourceMap.finish();
  const SourceAnnotation& unknown = commandAnnotation(unknownAnnotations, unknownTrack.commands.front());
  expect(std::ranges::count(unknown.fields, std::string_view{"opcode"}, &SourceField::name) == 1,
         "unknown commands should project their opcode field exactly once");
}

void akaoSnesV3VibratoPreservesSquareWaveModesAndSteppedAttack() {
  constexpr u32 start = 0x20;
  const AkaoSnesProfile ff5{.version = AKAOSNES_V3, .minorVersion = AKAOSNES_V3_FF5};

  const auto renderVibrato = [&](u8 delay, u8 rate, u8 depth) {
    std::vector<u8> bytes(0x80, 0xf2);
    bytes[start] = 0xd7;
    bytes[start + 1] = delay;
    bytes[start + 2] = rate;
    bytes[start + 3] = depth;
    bytes[start + 4] = 0x00;
    bytes[start + 5] = 0xf2;
    return renderTracks(ff5, {decodeTrack(bytes, ff5, start, start + 6)});
  };
  const auto depthEvent = [](const PerformanceSequence& performance) -> const ModulationPerformanceEvent* {
    for (const PerformanceEvent& event : performance.tracks.front().events) {
      const auto* modulation = std::get_if<ModulationPerformanceEvent>(&event);
      if (modulation != nullptr && modulation->target == ModulationPerformanceTarget::VibratoDepth) {
        return modulation;
      }
    }
    return nullptr;
  };
  const auto bendValues = [](const MidiSequence& midi) {
    std::vector<const PitchBend*> bends;
    for (const MidiEvent& event : midi.tracks.front().events) {
      if (const auto* bend = std::get_if<PitchBend>(&event)) {
        bends.push_back(bend);
      }
    }
    return bends;
  };

  struct ModeCase {
    u8 depth = 0;
    double initialPhase = 0.0;
    bool hasDownwardExcursion = false;
    bool hasUpwardExcursion = false;
  };
  const std::array<ModeCase, 3> modes{
      ModeCase{.depth = 0x3f, .initialPhase = 0.5, .hasDownwardExcursion = true},
      ModeCase{.depth = 0x7f, .initialPhase = 0.0, .hasUpwardExcursion = true},
      ModeCase{.depth = 0xff, .initialPhase = 0.0, .hasDownwardExcursion = true, .hasUpwardExcursion = true},
  };

  for (const ModeCase mode : modes) {
    const PerformanceSequence performance = renderVibrato(0, 0x0c, mode.depth);
    expect(performance.diagnostics.empty(), "valid AkaoSnes V3 vibrato should render without diagnostics");
    const ModulationPerformanceEvent* depth = depthEvent(performance);
    expect(depth != nullptr && depth->waveform == LfoWaveform::Square &&
               depth->initialPhaseCycles == mode.initialPhase && depth->pitchRangeSemitones &&
               depth->steppedDepthAttackSteps == 0,
           "AkaoSnes V3 vibrato should retain its square waveform, phase, and packed direction mode");
    expect(
        (depth->pitchRangeSemitones->minimum < 0.0) == mode.hasDownwardExcursion &&
            (depth->pitchRangeSemitones->maximum > 0.0) == mode.hasUpwardExcursion && depth->pitchDepthSemitones &&
            std::abs(*depth->pitchDepthSemitones - std::max(std::abs(depth->pitchRangeSemitones->minimum),
                                                            std::abs(depth->pitchRangeSemitones->maximum))) < 0.000001,
        "AkaoSnes V3 packed depth modes should retain their full asymmetric pitch endpoints");

    const auto rates = eventsOfType<ModulationPerformanceEvent>(performance.tracks.front());
    const auto rate = std::ranges::find_if(rates, [](const ModulationPerformanceEvent* event) {
      return event->target == ModulationPerformanceTarget::VibratoRate;
    });
    const double expectedRate = akaoSnesFrameRateHz(0x24) / 26.0;
    expect(rate != rates.end() && (*rate)->frequencyHz && std::abs(*(*rate)->frequencyHz - expectedRate) < 0.000001,
           "AkaoSnes V3 vibrato rate should use rate plus one driver frames per held state");

    const MidiSequence midi =
        renderMidiSequence(performance, MidiExportOptions{}, ModulationConversionPolicy::SequenceEventSimulation);
    const auto bends = bendValues(midi);
    const bool hasNegative = std::ranges::any_of(bends, [](const PitchBend* bend) { return bend->value < -3000; });
    const bool hasPositive = std::ranges::any_of(bends, [](const PitchBend* bend) { return bend->value > 3000; });
    expect(hasNegative == mode.hasDownwardExcursion && hasPositive == mode.hasUpwardExcursion,
           "AkaoSnes V3 sequence-event simulation should preserve downward, upward, and bipolar square modes");
    expect(std::ranges::any_of(bends,
                               [&](const PitchBend* bend) {
                                 return bend->tick == 0 &&
                                        (mode.hasUpwardExcursion ? bend->value > 3000 : bend->value < -3000);
                               }),
           "AkaoSnes V3 zero-delay vibrato should calculate its first held sample on the note-on tick");
  }

  const PerformanceSequence delayed = renderVibrato(1, 0x1f, 0x7f);
  const ModulationPerformanceEvent* delayedDepth = depthEvent(delayed);
  expect(delayedDepth != nullptr && delayedDepth->steppedDepthAttackSteps == 4,
         "a nonzero AkaoSnes V3 delay should configure the four-stage per-note depth attack");
  expect(std::ranges::count_if(delayed.tracks.front().events,
                               [](const PerformanceEvent& event) {
                                 const auto* modulation = std::get_if<ModulationPerformanceEvent>(&event);
                                 return modulation != nullptr &&
                                        modulation->target == ModulationPerformanceTarget::VibratoDepth;
                               }) == 1,
         "AkaoSnes V3 should not replace its held depth stages with a linear sequence-tick fade");

  const MidiSequence delayedMidi =
      renderMidiSequence(delayed, MidiExportOptions{}, ModulationConversionPolicy::SequenceEventSimulation);
  const auto delayedBends = bendValues(delayedMidi);
  const auto firstExcursion = std::ranges::find_if(delayedBends, [](const PitchBend* bend) { return bend->value > 0; });
  expect(firstExcursion != delayedBends.end() && (*firstExcursion)->tick == 1 && (*firstExcursion)->value > 800 &&
             (*firstExcursion)->value < 1200 &&
             std::ranges::any_of(
                 delayedBends, [](const PitchBend* bend) { return bend->value > 3500; }),
         "AkaoSnes V3 delayed vibrato should begin one tick later at quarter depth and reach full depth by stages");
}

void akaoSnesCompiledAutomationTicksControllerAndTempoFades() {
  constexpr u32 start = 0x20;
  const AkaoSnesProfile ff6{.version = AKAOSNES_V4, .minorVersion = AKAOSNES_V4_FF6};
  std::vector<u8> bytes(0x80, 0xec);
  size_t offset = start;
  bytes[offset++] = 0xc4;
  bytes[offset++] = 0xff;
  bytes[offset++] = 0xc5;
  bytes[offset++] = 4;
  bytes[offset++] = 0x7f;
  bytes[offset++] = 0xc6;
  bytes[offset++] = 0x40;
  bytes[offset++] = 0xc7;
  bytes[offset++] = 4;
  bytes[offset++] = 0;
  bytes[offset++] = 0xf0;
  bytes[offset++] = 0x20;
  bytes[offset++] = 0xf1;
  bytes[offset++] = 4;
  bytes[offset++] = 0x40;
  bytes[offset++] = 0xc0;
  bytes[offset++] = 0xec;

  const PerformanceSequence performance = renderTracks(ff6, {decodeTrack(bytes, ff6, start, static_cast<u32>(offset))});
  expect(performance.diagnostics.empty(), "valid AkaoSnes fade fixtures should render without diagnostics");
  const auto levels = eventsOfType<LevelPerformanceEvent>(performance.tracks.front());
  const auto pans = eventsOfType<StereoBalancePerformanceEvent>(performance.tracks.front());
  const auto tempos = eventsOfType<TempoPerformanceEvent>(performance.tracks.front());
  expect(levels.size() == 5 && levels.back()->header.tick == 4 &&
             std::abs(levels.back()->linearGain - (63.0 / 127.0)) < 1e-9,
         "volume fades should advance once per driver tick and land exactly on their target");
  expect(pans.size() == 5 && pans.back()->header.tick == 4 && pans.back()->rightGain == 0.0,
         "pan fades should advance once per driver tick and land exactly on their target");
  expect(tempos.size() == 5 && tempos.back()->header.tick == 4 && tempos.back()->microsecondsPerQuarter == 936000,
         "tempo fades should emit their intermediate shared tempo changes and final target");

  constexpr u32 lfoTrackStart = 0x50;
  bytes[lfoTrackStart] = 0xc9;
  bytes[lfoTrackStart + 1] = 0;
  bytes[lfoTrackStart + 2] = 0x20;
  bytes[lfoTrackStart + 3] = 0x20;
  bytes[lfoTrackStart + 4] = 0xc0;
  bytes[lfoTrackStart + 5] = 0xec;
  std::vector<TrackProgram> tracks;
  tracks.push_back(decodeTrack(bytes, ff6, lfoTrackStart, lfoTrackStart + 6, 0));
  tracks.push_back(decodeTrack(bytes, ff6, start + 10, static_cast<u32>(offset), 1));
  const PerformanceSequence sharedTempo = renderTracks(ff6, std::move(tracks));
  const auto delays = eventsOfType<VibratoDelayPerformanceEvent>(sharedTempo.tracks.front());
  expect(std::ranges::all_of(std::array<u64, 4>{1, 2, 3, 4},
                             [&](u64 tick) {
                               return std::ranges::any_of(delays, [tick](const VibratoDelayPerformanceEvent* delay) {
                                 return delay->header.tick == tick;
                               });
                             }),
         "tempo-fade steps should resynchronize tempo-dependent LFOs on other tracks");
}

void akaoSnesCompilerCursorCoversLoopsAndCpuBranches() {
  constexpr u32 start = 0x20;
  const AkaoSnesProfile profile{.version = AKAOSNES_V4, .minorVersion = AKAOSNES_V4_FF6};
  const auto renderedNotes = [&](std::vector<u8> bytes, SequenceVmOptions options) {
    TrackProgram track = decodeTrack(bytes, profile, start, static_cast<u32>(bytes.size()));
    const PerformanceSequence performance = renderTracks(profile, {std::move(track)}, options);
    expect(performance.diagnostics.empty(), "valid AkaoSnes loop fixtures should render without diagnostics");
    return eventsOfType<NotePerformanceEvent>(performance.tracks.front()).size();
  };

  std::vector<u8> finite(0x40, 0xec);
  finite[start] = 0xe2;
  finite[start + 1] = 2;
  finite[start + 2] = 0x0d;
  finite[start + 3] = 0xe3;
  finite[start + 4] = 0xec;
  expect(renderedNotes(finite, SequenceVmOptions{.loopPolicy = LoopPolicy::PlayOnce}) == 3,
         "a finite AkaoSnes loop count should include the initial play and both repeats");

  std::vector<u8> nested(0x40, 0xec);
  nested[start] = 0xe2;
  nested[start + 1] = 1;
  nested[start + 2] = 0xe2;
  nested[start + 3] = 1;
  nested[start + 4] = 0x0d;
  nested[start + 5] = 0xe3;
  nested[start + 6] = 0xe3;
  nested[start + 7] = 0xec;
  expect(renderedNotes(nested, SequenceVmOptions{.loopPolicy = LoopPolicy::PlayOnce}) == 4,
         "nested AkaoSnes loop slots should retain independent repeat counters");

  std::vector<u8> infinite(0x40, 0xec);
  infinite[start] = 0xe2;
  infinite[start + 1] = 0;
  infinite[start + 2] = 0x0d;
  infinite[start + 3] = 0xe3;
  infinite[start + 4] = 0xec;
  expect(renderedNotes(infinite, SequenceVmOptions{.loopPolicy = LoopPolicy::PlayOnce}) == 1 &&
             renderedNotes(infinite, SequenceVmOptions{.loopPolicy = LoopPolicy::PlayOnce, .sequenceLoops = 2}) == 3,
         "declared infinite AkaoSnes loops should honor play-once and requested loop counts");

  std::vector<u8> broken(0x40, 0xec);
  broken[start] = 0xe2;
  broken[start + 1] = 1;
  broken[start + 2] = 0x0d;
  broken[start + 3] = 0xf5;
  broken[start + 4] = 1;
  writeLe16(broken, start + 5, start + 9);
  broken[start + 7] = 0x0d;
  broken[start + 8] = 0xe3;
  broken[start + 9] = 0xec;
  expect(renderedNotes(broken, SequenceVmOptions{.loopPolicy = LoopPolicy::PlayOnce}) == 1,
         "a matching AkaoSnes loop break should leave the current loop at its effective destination");

  std::vector<u8> cpuBranch(0x40, 0xec);
  cpuBranch[start] = 0xfc;
  writeLe16(cpuBranch, start + 1, 0x30);
  cpuBranch[start + 3] = 0x0d;
  cpuBranch[start + 4] = 0xec;
  cpuBranch[0x30] = 0x0d;
  cpuBranch[0x31] = 0xec;
  const TrackProgram branch = decodeTrack(cpuBranch, profile, start, static_cast<u32>(cpuBranch.size()));
  expect(branch.commands.size() == 5 && branch.commands.front().flow.discoveryContinuation() &&
             branch.commands.front().flow.continuation.value == 0x23 &&
             branch.commands.front().flow.additionalTargets.size() == 1 &&
             branch.commands.front().flow.additionalTargets.front().value == 0x30,
         "CPU-controlled AkaoSnes jumps should retain both indeterminate fallthrough and branch blocks");
}

void akaoSnesCompilerCursorCoversNoteModesPitchAndSharedTempo() {
  constexpr u32 start = 0x20;
  const AkaoSnesProfile ct{.version = AKAOSNES_V4, .minorVersion = AKAOSNES_V4_CT};
  std::vector<u8> noteModes(0x60, 0xec);
  noteModes[start] = 0xdc;
  noteModes[start + 1] = 5;
  noteModes[start + 2] = 0xfb;
  noteModes[start + 3] = 0x0d;
  noteModes[start + 4] = 0xfc;
  noteModes[start + 5] = 0xe4;
  noteModes[start + 6] = 0x0d;
  noteModes[start + 7] = 0xe5;
  noteModes[start + 8] = 0xe6;
  noteModes[start + 9] = 0x0d;
  noteModes[start + 10] = 0xe7;
  noteModes[start + 11] = 0xe8;
  noteModes[start + 12] = 7;
  noteModes[start + 13] = 0x0d;
  noteModes[start + 14] = 0x0d;
  noteModes[start + 15] = 0xb5;
  noteModes[start + 16] = 0xc3;
  noteModes[start + 17] = 0xec;
  const PerformanceSequence noteModePerformance = renderTracks(ct, {decodeTrack(noteModes, ct, start, start + 18)});
  expect(noteModePerformance.diagnostics.empty(), "valid note-mode fixture should render without diagnostics");
  const auto notes = eventsOfType<NotePerformanceEvent>(noteModePerformance.tracks.front());
  expect(notes.size() == 6 && notes[0]->key == kAkaoSnesDrumKeyBias && notes[0]->durationTicks == 1 &&
             notes[1]->durationTicks == 3 && notes[2]->durationTicks == 3 && notes[3]->durationTicks == 5 &&
             notes[4]->durationTicks == 1 && notes[5]->extendsPrevious,
         "percussion, slur, legato, one-time duration, rest, and tie should preserve their distinct note behavior");
  const auto instruments = eventsOfType<InstrumentPerformanceEvent>(noteModePerformance.tracks.front());
  expect(instruments.size() == 3 && instruments[0]->program == 5 && instruments[1]->bank == kAkaoSnesDrumKitBank &&
             instruments[2]->bank == 0 && instruments[2]->program == 5,
         "percussion mode should restore the remembered melodic program when it turns off");

  const AkaoSnesProfile ff6{.version = AKAOSNES_V4, .minorVersion = AKAOSNES_V4_FF6};
  std::vector<u8> slideBytes(0x40, 0xec);
  slideBytes[start] = 0xc8;
  slideBytes[start + 1] = 2;
  slideBytes[start + 2] = 12;
  slideBytes[start + 3] = 0x08;
  slideBytes[start + 4] = 0xec;
  const PerformanceSequence slide = renderTracks(ff6, {decodeTrack(slideBytes, ff6, start, start + 5)});
  const auto slideNotes = eventsOfType<NotePerformanceEvent>(slide.tracks.front());
  const auto slideAutomation = std::ranges::find_if(
      slide.tracks.front().automations,
      [](const PerformanceAutomation& automation) { return pitchTransitionIntent(automation) != nullptr; });
  const auto* slideIntent =
      slideAutomation == slide.tracks.front().automations.end() ? nullptr : pitchTransitionIntent(*slideAutomation);
  const auto* slideCurve = slideIntent == nullptr ? nullptr : std::get_if<SampledAutomationCurve>(&slideIntent->curve);
  const bool retainsDriverCurve = slideCurve != nullptr && slideCurve->samples.size() == 3 &&
                                  std::abs((slideCurve->samples[1].value - slideCurve->samples[0].value) -
                                           (slideCurve->samples[2].value - slideCurve->samples[1].value)) > 0.1;
  expect(slideNotes.size() == 1 && slideIntent != nullptr && slideIntent->note == slideNotes.front()->note &&
             slideIntent->preferredRendering == PitchTransitionRenderingHint::PitchBend &&
             slideIntent->timing.timelineTicks == 2 && retainsDriverCurve &&
             slideCurve->samples.front().tickOffset == 0 && slideCurve->samples.back().tickOffset == 2,
         "a pending AkaoSnes pitch slide should attach its exact driver curve to the next sounding note");

  const MidiSequence bendSlide = renderMidiSequence(slide);
  expect(std::ranges::count_if(bendSlide.tracks.front().events,
                               [](const MidiEvent& event) { return std::holds_alternative<PitchBend>(event); }) >= 3,
         "preserve-format MIDI should lower an AkaoSnes pitch slide through its sampled pitch-bend curve");

  MidiExportOptions portamentoOptions;
  portamentoOptions.pitchTransitions = MidiPitchTransitionRendering::Portamento;
  const MidiSequence portamentoSlide = renderMidiSequence(slide, portamentoOptions);
  expect(std::ranges::any_of(portamentoSlide.tracks.front().events,
                             [](const MidiEvent& event) { return std::holds_alternative<PortamentoControl>(event); }) &&
             std::ranges::none_of(portamentoSlide.tracks.front().events,
                                  [](const MidiEvent& event) { return std::holds_alternative<PitchBend>(event); }),
         "explicit portamento MIDI should lower the same AkaoSnes pitch-slide intent natively");

  slideBytes[start + 1] = 0;
  const PerformanceSequence immediateSlide = renderTracks(ff6, {decodeTrack(slideBytes, ff6, start, start + 5)});
  expect(immediateSlide.tracks.front().automations.empty() &&
             eventsOfType<PitchBendPerformanceEvent>(immediateSlide.tracks.front()).size() == 1,
         "a one-step AkaoSnes pitch change should remain instantaneous instead of inventing a timed transition");

  const AkaoSnesProfile ff4{.version = AKAOSNES_V1, .minorVersion = AKAOSNES_V1_FF4};
  const auto envelopeBytes = [&](u8 commandAfterNote) {
    std::vector<u8> bytes(0x60, 0xf1);
    bytes[start] = 0xd6;
    bytes[start + 1] = 0;
    bytes[start + 2] = 8;
    bytes[start + 3] = 12;
    bytes[start + 4] = 0x0b;
    bytes[start + 5] = commandAfterNote;
    return bytes;
  };

  std::vector<u8> offThenEnd = envelopeBytes(0xe6);
  offThenEnd[start + 6] = 0xf1;
  TrackProgram offThenEndTrack = decodeTrack(offThenEnd, ff4, start, start + 7);
  const SemanticOperand* envelopeOff = semanticOperand(offThenEndTrack.commands[2], "pitch_envelope_off");
  expect(envelopeOff != nullptr && std::get<bool>(envelopeOff->value),
         "pitch-envelope-off should carry an explicit semantic marker for source-free boundary analysis");
  const PerformanceSequence terminalEnvelope = renderTracks(ff4, {std::move(offThenEndTrack)});
  const auto terminalBends = eventsOfType<PitchBendPerformanceEvent>(terminalEnvelope.tracks.front());
  expect(
      std::ranges::none_of(terminalBends, [](const PitchBendPerformanceEvent* bend) { return bend->header.tick >= 8; }),
      "pitch envelopes should stop at a note boundary followed by envelope-off and end");

  std::vector<u8> offThenJump = envelopeBytes(0xe6);
  offThenJump[start + 6] = 0xf4;
  writeLe16(offThenJump, start + 7, start);
  const PerformanceSequence loopEnvelope = renderTracks(ff4, {decodeTrack(offThenJump, ff4, start, start + 9)});
  const auto loopBends = eventsOfType<PitchBendPerformanceEvent>(loopEnvelope.tracks.front());
  expect(std::ranges::none_of(loopBends, [](const PitchBendPerformanceEvent* bend) { return bend->header.tick >= 8; }),
         "pitch envelopes should stop before envelope-off and a backward loop boundary");

  std::vector<u8> noteTransition = envelopeBytes(0x0b);
  noteTransition[start + 6] = 0xe6;
  noteTransition[start + 7] = 0xf1;
  const PerformanceSequence transition = renderTracks(ff4, {decodeTrack(noteTransition, ff4, start, start + 8)});
  const auto transitionBends = eventsOfType<PitchBendPerformanceEvent>(transition.tracks.front());
  expect(std::ranges::any_of(transitionBends,
                             [](const PitchBendPerformanceEvent* bend) { return bend->header.tick == 8; }),
         "pitch automation should reach a transition from one sounding note to the next before resetting");

  std::vector<u8> noteThenRest = envelopeBytes(0xbf);
  noteThenRest[start + 6] = 0xe6;
  noteThenRest[start + 7] = 0xf1;
  const PerformanceSequence restEnvelope = renderTracks(ff4, {decodeTrack(noteThenRest, ff4, start, start + 8)});
  const auto restBends = eventsOfType<PitchBendPerformanceEvent>(restEnvelope.tracks.front());
  expect(std::ranges::any_of(
             restBends,
             [](const PitchBendPerformanceEvent* bend) { return bend->header.tick >= 8 && bend->header.tick < 16; }) &&
             std::ranges::none_of(restBends,
                                  [](const PitchBendPerformanceEvent* bend) { return bend->header.tick >= 16; }),
         "rests should let active pitch automation advance but stop it at a terminal boundary");

  std::vector<u8> sharedTempoBytes(0x100, 0xec);
  sharedTempoBytes[start] = 0xc9;
  sharedTempoBytes[start + 1] = 0;
  sharedTempoBytes[start + 2] = 0x20;
  sharedTempoBytes[start + 3] = 0x20;
  sharedTempoBytes[start + 4] = 0x00;
  sharedTempoBytes[start + 5] = 0xec;
  constexpr u32 tempoTrackStart = 0x80;
  sharedTempoBytes[tempoTrackStart] = 0xc0;
  sharedTempoBytes[tempoTrackStart + 1] = 0xf0;
  sharedTempoBytes[tempoTrackStart + 2] = 0x40;
  sharedTempoBytes[tempoTrackStart + 3] = 0xec;
  std::vector<TrackProgram> tempoTracks;
  tempoTracks.push_back(decodeTrack(sharedTempoBytes, ff6, start, 0x40, 0));
  tempoTracks.push_back(decodeTrack(sharedTempoBytes, ff6, tempoTrackStart, 0x90, 1));
  const PerformanceSequence sharedTempo = renderTracks(ff6, std::move(tempoTracks));
  const auto vibratoDelays = eventsOfType<VibratoDelayPerformanceEvent>(sharedTempo.tracks[0]);
  expect(std::ranges::any_of(vibratoDelays,
                             [](const VibratoDelayPerformanceEvent* delay) { return delay->header.tick == 8; }),
         "a delayed tempo change on one track should resynchronize another track's active LFO at that tick");
}

void akaoSnesV4TieExtendsShortenedPreviousNote() {
  std::vector<u8> bytes(0x40, 0xeb);
  constexpr u32 start = 0x20;
  bytes[start] = 0x7f;
  bytes[start + 1] = 0xac;
  bytes[start + 2] = 0xeb;

  const auto& dialect = akaoSnesSequenceDialect();
  const AkaoSnesProfile profile{.version = AKAOSNES_V4, .minorVersion = AKAOSNES_V4_FF6};
  const TrackProgram track = decodeAkaoSnesSourceTrack(
      ByteReader(SourceId{8}, bytes),
      AkaoSnesTrackDecodeOptions{.profile = profile, .startAddress = start, .bytecodeEnd = 0x40});
  const SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .config = SequenceProgramConfig{.profile = encodeAkaoSnesProfile(profile)},
      .tracks = {track},
  };

  const PerformanceSequence performance = SequenceVm(LoopPolicy::PlayOnce).render(program, dialect);
  expect(performance.diagnostics.empty(), "AkaoSnes V4 tie fixture should render without diagnostics");

  const MidiSequence midi = renderMidiSequence(performance);
  const auto note = std::ranges::find_if(
      midi.tracks[0].events, [](const MidiEvent& event) { return std::holds_alternative<NoteDuration>(event); });
  expect(note != midi.tracks[0].events.end(), "AkaoSnes V4 tie fixture should render a MIDI note");
  expect(std::get<NoteDuration>(*note).duration == 142,
         "AkaoSnes V4 tie should extend the previous shortened note instead of leaving it at 94 ticks");
}
