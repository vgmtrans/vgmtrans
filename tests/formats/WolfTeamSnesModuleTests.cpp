/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/WolfTeamSnes/WolfTeamSnes.h"

#include "value/export/midi/PerformanceMidiRenderer.h"
#include "value/platform/SnesSampleDirectory.h"
#include "value/sequence/SequenceVm.h"
#include "value/session/Session.h"
#include "value/validation/SequenceValidation.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

using namespace vgmtrans::core;
using namespace vgmtrans::formats::wolf_team_snes;

namespace {

void expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void writeBytes(std::vector<u8>& bytes, u32 offset, std::initializer_list<u8> values) {
  std::ranges::copy(values, bytes.begin() + offset);
}

void writeLe16(std::vector<u8>& bytes, u32 offset, u16 value) {
  bytes[offset] = static_cast<u8>(value);
  bytes[offset + 1] = static_cast<u8>(value >> 8);
}

void addLatePitchTable(std::vector<u8>& bytes, u32 offset = 0x0868) {
  for (u32 index = 0; index < 0x60; ++index) {
    const double pitch = 0x10be * std::pow(2.0, (static_cast<int>(index) - 72) / 12.0);
    writeLe16(bytes, offset + index * 2, static_cast<u16>(std::lround(pitch)));
  }
}

void addSegmentedPitchTable(std::vector<u8>& bytes) {
  for (u32 index = 0; index < 14; ++index) {
    const double pitch = (0x10be / 2.0) * std::pow(2.0, index / 12.0);
    writeLe16(bytes, 0x0180 + index * 2, static_cast<u16>(std::lround(pitch)));
  }
}

void addBrrSample(std::vector<u8>& bytes, u16 directory, u8 srcn, u16 start) {
  writeLe16(bytes, directory + srcn * 4, start);
  writeLe16(bytes, directory + srcn * 4 + 2, start);
  bytes[start] = 0x01;
}

Layout directLateLayout(Variant variant, std::vector<u16> streams, LateTraits traits = {}) {
  return Layout{
      .variant = variant,
      .sequenceHeaderAddress = 0x2000,
      .relocationPage = 0x20,
      .headerLength = 0x4d,
      .lateTraits = traits,
      .channels = {ChannelLayout{
          .index = 0, .status = 0x80, .pointerTableAddress = 0x2800, .streamStarts = std::move(streams)}},
      .instruments = InstrumentLayout{.sampleDirAddress = 0xff00,
                                      .patchTableAddress = 0x0400,
                                      .volumeTableAddress = 0x0140,
                                      .entrySize = 4,
                                      .confirmed = true},
  };
}

Layout directArcusLayout(std::vector<u16> streams) {
  return Layout{
      .variant = Variant::Arcus,
      .sequenceHeaderAddress = 0x1800,
      .relocationPage = 0x18,
      .headerLength = 0x44,
      .channels = {ChannelLayout{
          .index = 0, .status = 1, .pointerTableAddress = 0x2000, .streamStarts = std::move(streams)}},
      .instruments = InstrumentLayout{.sampleDirAddress = 0x0400,
                                      .patchTableAddress = 0x0500,
                                      .patchMapAddress = 0x1800,
                                      .entrySize = 6,
                                      .confirmed = true},
  };
}

template <class Event>
std::vector<const Event*> events(const PerformanceTrack& track) {
  std::vector<const Event*> result;
  for (const PerformanceEvent& event : track.events) {
    if (const auto* typed = std::get_if<Event>(&event)) {
      result.push_back(typed);
    }
  }
  return result;
}

PerformanceSequence render(const Layout& layout, std::vector<u8> bytes) {
  const ByteReader reader(SourceId{170}, bytes);
  SequenceParse parsed = decodeSequence(reader, layout, AssetId{170});
  return SequenceVm(LoopPolicy::PlayOnce).render(parsed.program);
}

std::vector<u8> lateScannerFixture() {
  std::vector<u8> bytes(kAramSize);
  writeBytes(bytes, 0x0100,
             {0x8f, 0x00, 0x86, 0x8f, 0x02, 0x87, 0x8f, 0x00, 0xe0, 0xfa, 0x99, 0xe1, 0x60, 0x98, 0x40, 0xe1});
  writeBytes(bytes, 0x0300,
             {0x8d, 0x01, 0xf7, 0xa2, 0x8d, 0x07, 0xd7, 0x86, 0x68, 0x40, 0x90, 0x0a, 0x68, 0x48, 0x0d, 0x28,
              0x01, 0x8e, 0xb0, 0x02, 0xbc, 0xbc, 0x8f, 0x04, 0xe1, 0x1c, 0x1c, 0xc4, 0xe0, 0x8d, 0x00, 0xf7,
              0xe0, 0x8d, 0x0f, 0xd7, 0x86, 0x8d, 0x01, 0xf7, 0xe0, 0x8d, 0x08, 0xd7, 0x86, 0x8d, 0x02, 0xf7,
              0xe0, 0x8d, 0x09, 0xd7, 0x86, 0x8d, 0x03, 0xf7, 0xe0, 0x80, 0xa8, 0x40, 0x8d, 0x12, 0xd7, 0x86});
  bytes[0x4000 + 0x22] = 0x40;
  writeBytes(bytes, 0x4000 + 0x23, {0x80, 0x00, 0x10});
  writeBytes(bytes, 0x5000, {0x00, 0x20, 0x00, 0xff});
  writeBytes(bytes, 0x6000, {0x30, 0x04, 0x03, 0xff, 0x91});
  bytes[0x0200] = 0x80;
  writeLe16(bytes, 0x0201, 0x5000);
  writeLe16(bytes, 0x0203, 0x6000);
  writeBytes(bytes, 0x0400, {0x00, 0x8f, 0xe0, 0x40});
  bytes[0x0140] = 0x08;
  addBrrSample(bytes, 0xff00, 0, 0x7000);
  writeLe16(bytes, 0xff04, 0x7100);
  writeLe16(bytes, 0xff06, 0xffff);
  bytes[0x7100] = 0x01;
  addLatePitchTable(bytes);
  return bytes;
}

std::vector<u8> arcusScannerFixture() {
  std::vector<u8> bytes(kAramSize);
  writeBytes(bytes, 0x0100, {0x8d, 0x5d, 0xe8, 0x04, 0xcb, 0xf2, 0xc4, 0xf3});
  bytes[0x1800 + 0x22] = 0x40;
  writeBytes(bytes, 0x1800 + 0x23, {0x01, 0x00, 0x08, 0x01, 0x10, 0x08});
  writeBytes(bytes, 0x2000, {0x00, 0x09, 0x00, 0xff});
  writeBytes(bytes, 0x2010, {0x20, 0x09, 0x00, 0xff});
  writeBytes(bytes, 0x2100, {0x60, 0x04, 0x03, 0x7f, 0xfd});
  writeBytes(bytes, 0x2120, {0x61, 0x04, 0x03, 0x7f, 0xfd});
  bytes[0x0000] = 1;
  writeLe16(bytes, 0x0001, 0x2000);
  writeLe16(bytes, 0x0003, 0x2100);
  bytes[0x0010] = 1;
  writeLe16(bytes, 0x0011, 0x2010);
  writeLe16(bytes, 0x0013, 0x2120);
  addBrrSample(bytes, 0x0400, 0, 0x0800);
  writeBytes(bytes, 0x0500, {0x00, 0x8f, 0xe0, 0x00, 0x00, 0x00});
  addSegmentedPitchTable(bytes);
  return bytes;
}

std::vector<u8> middleScannerFixture() {
  std::vector<u8> bytes(kAramSize);
  bytes[0x00ad] = 0x47;
  for (u32 opcode = 0xe0; opcode <= 0xff; ++opcode) {
    writeLe16(bytes, 0x2035 + (opcode - 0xe0) * 2, 0x1000);
  }
  bytes[0x4700 + 0x22] = 0x40;
  writeBytes(bytes, 0x4700 + 0x23, {0x01, 0x00, 0x09, 0x01, 0x10, 0x09});
  writeBytes(bytes, 0x5000, {0x00, 0x0a, 0x00, 0xff});
  writeBytes(bytes, 0x5010, {0x20, 0x0a, 0x00, 0xff});
  writeBytes(bytes, 0x5100, {0x60, 0x04, 0x03, 0xff, 0xfd});
  writeBytes(bytes, 0x5120, {0x61, 0x04, 0x03, 0xff, 0xfd});
  bytes[0x0200] = 1;
  writeLe16(bytes, 0x0201, 0x5000);
  writeLe16(bytes, 0x0203, 0x5100);
  bytes[0x0220] = 1;
  writeLe16(bytes, 0x0221, 0x5010);
  writeLe16(bytes, 0x0223, 0x5120);
  return bytes;
}

void scannersIdentifyAllThreeDriverEras() {
  const auto late = findLayout(ByteReader(SourceId{171}, lateScannerFixture()));
  expect(late && late->variant == Variant::TalesOfPhantasia &&
             late->channels.front().streamStarts == std::vector<u16>{0x6000} && late->instruments.confirmed,
         "late signatures should identify the Tales-style remap, relocated phrase table, and synth layout");

  const auto arcus = findLayout(ByteReader(SourceId{172}, arcusScannerFixture()));
  expect(arcus && arcus->variant == Variant::Arcus && arcus->channels.size() == 11 &&
             arcus->channels[1].streamStarts == std::vector<u16>{0x2120} && arcus->instruments.confirmed,
         "Arcus DIR signature should recover both active segmented channels and the six-byte patch model");

  const auto middle = findLayout(ByteReader(SourceId{173}, middleScannerFixture()));
  expect(middle && middle->variant == Variant::DarkKingdom && middle->middleCommandTableAddress == 0x2035 &&
             middle->channels[1].streamStarts == std::vector<u16>{0x5120},
         "the audited middle command table should select Dark Kingdom and its relative segment pointers");

  std::vector<u8> malformed(kAramSize - 1);
  expect(!findLayout(ByteReader(SourceId{174}, malformed)),
         "scanner must reject sources that are not a full ARAM image");
}

void lateCommandsRenderLoopsSplitsLfoAndDynamicAdsr() {
  std::vector<u8> bytes(kAramSize);
  bytes[0x2000 + 0x22] = 0x40;
  addLatePitchTable(bytes);
  writeBytes(bytes, 0x0400 + 6 * 4, {0x00, 0x8f, 0xe0, 0x40});
  writeBytes(bytes, 0x0400 + 10 * 4, {0x00, 0x8f, 0xe0, 0x40});
  writeBytes(bytes, 0x3000, {0x96, 0x05, 0x9c, 0x02, 0x10, 0x08, 0x9b, 0x01, 0x20, 0x04, 0xff, 0xff,
                             0x99, 0x00, 0x40, 0xaf, 0x0f, 0xe0, 0x92, 0x90, 0x01, 0x93, 0x02, 0x91});
  writeBytes(bytes, 0x3040, {0x60, 0x01, 0xff, 0x80, 0x91});
  const Layout layout = directLateLayout(Variant::StarOcean, {0x3000, 0x3040}, LateTraits{0x50, true, true, false});
  const PerformanceSequence performance = render(layout, bytes);
  expect(performance.diagnostics.empty() && performance.tracks.size() == 1,
         "source-free late playback should finish without VM diagnostics");
  const PerformanceTrack& track = performance.tracks.front();
  expect(!sequenceDialect().behavior.initialMonoModeChannels && events<MonoModePerformanceEvent>(track).empty(),
         "polyphonic Wolf Team tracks must not emit a MIDI mono-mode initialization");
  const auto notes = events<NotePerformanceEvent>(track);
  const auto instruments = events<InstrumentPerformanceEvent>(track);
  const auto envelopes = events<EnvelopePerformanceEvent>(track);
  const auto balances = events<StereoBalancePerformanceEvent>(track);
  expect(notes.size() == 2 && notes[0]->key == 32.0 && notes[0]->durationTicks == 4 && notes[1]->key == 84.0,
         "late notes should preserve delay/gate duration and normalize driver pitch indexes into its 0-95 table");
  expect(instruments.size() >= 4 && instruments[instruments.size() - 2]->sourceInstrument->key == 10 &&
             instruments.back()->sourceInstrument->key == 6,
         "Star Ocean program 5 should select its low and high key-split instruments at note attack");
  expect(envelopes.size() == 1 && envelopes.front()->scope == VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks,
         "AF must replace the active and future ADSR rather than remain a display-only event");
  expect(!track.automations.empty() && !balances.empty() && std::abs(balances.back()->leftGain - 65.0 / 128.0) < 1e-9 &&
             std::abs(balances.back()->rightGain - 63.0 / 128.0) < 1e-9,
         "late LFO ramp and asymmetric center pan gains should survive semantic lowering");
}

void disassemblyCommandSetsHaveAuditedSizes() {
  std::vector<u8> late(kAramSize);
  writeBytes(late, 0x3000,
             {0x20, 1,    2,    3,    0x90, 1,    0x92, 0x93, 2,    0x94, 1,    0x40, 0x95, 1,    0x40, 0x96, 2, 0x97,
              1,    2,    0x98, 1,    2,    0x99, 1,    2,    0x9a, 1,    2,    0x9b, 1,    0x9c, 1,    2,    3, 0xa2,
              2,    0xa3, 1,    0xaa, 1,    2,    0xad, 1,    0xae, 1,    0xaf, 1,    2,    0xb0, 1,    0xb2, 1, 0x91});
  const Layout lateLayout = directLateLayout(Variant::TalesOfPhantasia, {0x3000});
  const TrackProgram lateTrack =
      decodeSourceTrack(ByteReader(SourceId{175}, late), lateLayout, lateLayout.channels.front());
  expect(lateTrack.commands.size() == 22 &&
             std::ranges::none_of(
                 lateTrack.commands,
                 [](const SourceCommand& command) { return command.semantic == SequenceSemantic::Unsupported; }),
         "every Star Ocean/Tales command in the disassembly dispatch table should decode with its audited size");

  std::vector<u8> arcus(kAramSize);
  writeBytes(arcus, 0x3000, {0x60, 1,    2,    3,    0xe0, 1,    0xe1, 1,    2,    0xe2, 1,    2,    0xe3, 1,
                             0xe4, 1,    0xe5, 1,    2,    3,    0xe7, 1,    2,    0xec, 1,    0xee, 1,    2,
                             0xef, 1,    0xf0, 1,    0x80, 0xe8, 0xe9, 0xea, 0xeb, 0xed, 0xf1, 0xf2, 0xf3, 0xf5,
                             0xf6, 0xfa, 0xfc, 0xfe, 0xff, 0xf4, 1,    0xf7, 1,    0xf9, 0xfd});
  const Layout arcusLayout = directArcusLayout({0x3000});
  const TrackProgram arcusTrack =
      decodeSourceTrack(ByteReader(SourceId{176}, arcus), arcusLayout, arcusLayout.channels.front());
  expect(arcusTrack.commands.size() == 31 &&
             std::all_of(arcusTrack.commands.begin() + 12, arcusTrack.commands.begin() + 27,
                         [](const SourceCommand& command) {
                           return command.encodedSize == 1 && command.semantic == SequenceSemantic::Meta;
                         }) &&
             arcusTrack.commands[27].opcode == 0xf4,
         "Arcus's 80-df fallback and every disassembled undefined high command must remain one-byte NOPs");

  Layout middleLayout = directArcusLayout({0x3000});
  middleLayout.variant = Variant::DarkKingdom;
  middleLayout.instruments.entrySize = 8;
  std::vector<u8> middle(kAramSize);
  writeBytes(middle, 0x3000,
             {0x60, 1, 2,    3,    0xe0, 1,    0xe1, 1, 2,    0xe2, 1,    2, 0xe3, 1,    0xe4, 1,   0xe5,
              1,    2, 3,    0xe6, 1,    2,    0xe7, 1, 2,    0xe8, 1,    2, 3,    0xe9, 1,    2,   3,
              0xec, 1, 0xee, 1,    2,    0xef, 1,    2, 0xf0, 1,    0xf2, 1, 0xf4, 1,    0xf9, 0xf1});
  const TrackProgram middleTrack =
      decodeSourceTrack(ByteReader(SourceId{177}, middle), middleLayout, middleLayout.channels.front());
  expect(middleTrack.commands.size() == 19 && middleTrack.commands.back().opcode == 0xf1 &&
             middleTrack.commands.back().semantic == SequenceSemantic::End,
         "middle-family E6/E8/E9 controls, three-byte EF, and F1 end must retain their distinct lengths");
}

void arcusSustainsUntilKeyOffAndUsesPhysicalPitch() {
  std::vector<u8> bytes(kAramSize);
  bytes[0x1800 + 0x22] = 0x40;
  bytes[0xcf] = 0x40;
  bytes[0xe2] = 0x40;
  bytes[0xfa] = 0x80;
  addSegmentedPitchTable(bytes);
  bytes[0x1800 + 2] = 0;
  writeBytes(bytes, 0x0500, {0x00, 0x8f, 0xe0, 0x00, 0, 0});
  writeBytes(bytes, 0x3000,
             {0xec, 0x02, 0xe5, 0x01, 0x10, 0x08, 0xe4, 0x01, 0x60, 0x04, 0x00, 0x7f, 0xee, 0x01, 0x50, 0xe0, 0x02,
              0xf9, 0xfd});
  writeBytes(bytes, 0x3040, {0xf8});
  const PerformanceSequence performance = render(directArcusLayout({0x3000, 0x3040}), bytes);
  const PerformanceTrack& track = performance.tracks.front();
  const auto notes = events<NotePerformanceEvent>(track);
  const auto bends = events<PitchBendPerformanceEvent>(track);
  expect(performance.diagnostics.empty() && notes.size() == 1 && notes.front()->durationTicks == 5,
         "Arcus gate zero should remain sustained through pitch commands until E0 keys the voice off");
  expect(notes.front()->key == 72.0 && !bends.empty() && bends.back()->semitones > 0.0 && !track.automations.empty(),
         "Arcus packed octave pitch, active-voice DSP bend, and delayed depth ramp should be physical events");
}

void duplicateSegmentPointersUseRuntimeIndex() {
  std::vector<u8> bytes(kAramSize);
  bytes[0x1800 + 0x22] = 0x40;
  addSegmentedPitchTable(bytes);
  writeBytes(bytes, 0x0500, {0x00, 0x8f, 0xe0, 0x00, 0, 0, 0, 0});
  bytes[0x3000] = 0xfd;
  writeBytes(bytes, 0x3040, {0x60, 0x01, 0x01, 0xff, 0xf1});
  Layout layout = directArcusLayout({0x3000, 0x3000, 0x3040});
  layout.variant = Variant::DarkKingdom;
  layout.instruments.patchMapAddress.reset();
  layout.instruments.entrySize = 8;
  const PerformanceSequence performance = render(layout, bytes);
  expect(performance.diagnostics.empty() && events<NotePerformanceEvent>(performance.tracks.front()).size() == 1,
         "duplicate segment pointers must advance by runtime segment index instead of looping on one source address");
}

void segmentedKeyOffRevisesEveryActiveNoteIdentity() {
  std::vector<u8> bytes(kAramSize);
  bytes[0x1800 + 0x22] = 0x40;
  addSegmentedPitchTable(bytes);
  writeBytes(bytes, 0x0500, {0x00, 0x8f, 0xe0, 0x00, 0, 0});
  writeBytes(bytes, 0x3000, {0x60, 0x01, 0x0a, 0x7f, 0x61, 0x02, 0x00, 0x7f, 0xe0, 0x00, 0xfd});
  const PerformanceSequence performance = render(directArcusLayout({0x3000}), bytes);
  const auto notes = events<NotePerformanceEvent>(performance.tracks.front());
  expect(performance.diagnostics.empty() && notes.size() == 2 && notes[0]->header.tick == 0 &&
             notes[0]->durationTicks == 3 && notes[1]->header.tick == 1 && notes[1]->durationTicks == 2,
         "E0 must truncate every still-active gated or sustained note without revising unrelated note identities");
}

void duplicatePhrasePointersUseRuntimeIndex() {
  std::vector<u8> bytes(kAramSize);
  bytes[0x2000 + 0x22] = 0x40;
  addLatePitchTable(bytes);
  writeBytes(bytes, 0x3000, {0x92, 0x90, 0x01, 0x93, 0x02, 0x91});
  writeBytes(bytes, 0x3040, {0x30, 0x01, 0xff, 0xff, 0x91});
  const Layout layout = directLateLayout(Variant::TalesOfPhantasia, {0x3000, 0x3000, 0x3040});
  const PerformanceSequence performance = render(layout, bytes);
  const auto notes = events<NotePerformanceEvent>(performance.tracks.front());
  expect(performance.diagnostics.empty() && notes.size() == 1 && notes.front()->header.tick == 4,
         "duplicate phrase pointers and their loop markers must use runtime phrase state instead of a source address");
}

void nestedLateLoopMarkersPreferTheInnerCounter() {
  std::vector<u8> bytes(kAramSize);
  bytes[0x2000 + 0x22] = 0x40;
  addLatePitchTable(bytes);
  writeBytes(bytes, 0x3000, {0x92, 0x90, 0x01, 0x92, 0x90, 0x01, 0x93, 0x02, 0x93, 0x02, 0x30, 0x01, 0xff, 0xff, 0x91});
  const PerformanceSequence performance = render(directLateLayout(Variant::TalesOfPhantasia, {0x3000}), bytes);
  const auto notes = events<NotePerformanceEvent>(performance.tracks.front());
  expect(performance.diagnostics.empty() && notes.size() == 1 && notes.front()->header.tick == 6,
         "nested 92/93 loops must exhaust and reset the inner marker before advancing the outer counter");
}

void leadingJockeyProgramChangesCarryTheirDelay() {
  std::vector<u8> bytes(kAramSize);
  bytes[0x2000 + 0x22] = 0x40;
  addLatePitchTable(bytes);
  writeBytes(bytes, 0x3000, {0x96, 0x05, 0x03, 0x30, 0x01, 0xff, 0xff, 0x91});
  const Layout layout = directLateLayout(Variant::LeadingJockey, {0x3000}, LateTraits{0x48, false, false, true});
  const PerformanceSequence performance = render(layout, bytes);
  const auto instruments = events<InstrumentPerformanceEvent>(performance.tracks.front());
  const auto notes = events<NotePerformanceEvent>(performance.tracks.front());
  expect(performance.diagnostics.empty() && notes.size() == 1 && notes.front()->header.tick == 5 &&
             instruments.size() == 2 && instruments.back()->sourceInstrument->key == 3,
         "Leading Jockey's three-byte 96 must select the direct SRCN before applying its encoded delay");
}

void middleFinePitchInterpolatesAdjacentDspWords() {
  std::vector<u8> bytes(kAramSize);
  bytes[0x1800 + 0x22] = 0x40;
  addSegmentedPitchTable(bytes);
  writeBytes(bytes, 0x0500, {0x00, 0x8f, 0xe0, 0x00, 0, 0, 0, 0});
  writeBytes(bytes, 0x3000, {0xf4, 0x60, 0x60, 0x01, 0x01, 0xff, 0xf1});
  Layout layout = directArcusLayout({0x3000});
  layout.variant = Variant::DarkKingdom;
  layout.instruments.patchMapAddress.reset();
  layout.instruments.entrySize = 8;
  const PerformanceSequence performance = render(layout, bytes);
  const auto notes = events<NotePerformanceEvent>(performance.tracks.front());
  const auto tunings = events<TuningPerformanceEvent>(performance.tracks.front());
  const int previous = static_cast<int>(bytes[0x0180] | (bytes[0x0181] << 8)) * 2;
  const int base = static_cast<int>(bytes[0x0182] | (bytes[0x0183] << 8)) * 2;
  const int next = static_cast<int>(bytes[0x0184] | (bytes[0x0185] << 8)) * 2;
  const int interpolated = base + ((next - base) * 32) / 64;
  const double physicalKey = 60.0 + 12.0 * std::log2(interpolated / 4096.0);
  const double roundedKey = std::round(physicalKey);
  expect(previous < base && base < interpolated && interpolated < next && performance.diagnostics.empty() &&
             notes.size() == 1 && notes.front()->key == roundedKey && !tunings.empty() &&
             std::abs(tunings.back()->cents - (physicalKey - roundedKey) * 100.0) < 1e-9,
         "middle-family F4 must interpolate the previous/base/next DSP words around table index semitone+1");
}

void timerArithmeticKeepsLateWidthAndSegmentedByteWrap() {
  std::vector<u8> lateBytes(kAramSize);
  lateBytes[0x2000 + 0x22] = 1;
  addLatePitchTable(lateBytes);
  writeBytes(lateBytes, 0x3000, {0x95, 0x00, 0x01, 0x91});
  const PerformanceSequence late = render(directLateLayout(Variant::TalesOfPhantasia, {0x3000}), lateBytes);
  const auto lateTempos = events<TempoPerformanceEvent>(late.tracks.front());
  expect(lateTempos.size() == 1 && lateTempos.front()->microsecondsPerQuarter == 60000000,
         "late-family timer division must preserve the full 10000 target instead of wrapping it to a byte");

  std::vector<u8> middleBytes(kAramSize);
  middleBytes[0x1800 + 0x22] = 1;
  addSegmentedPitchTable(middleBytes);
  writeBytes(middleBytes, 0x0500, {0x00, 0x8f, 0xe0, 0x00, 0, 0, 0, 0});
  writeBytes(middleBytes, 0x3000, {0xe7, 0x00, 0x01, 0xf1});
  Layout middleLayout = directArcusLayout({0x3000});
  middleLayout.variant = Variant::DarkKingdom;
  middleLayout.instruments.patchMapAddress.reset();
  middleLayout.instruments.entrySize = 8;
  const PerformanceSequence middle = render(middleLayout, middleBytes);
  const auto middleTempos = events<TempoPerformanceEvent>(middle.tracks.front());
  expect(middleTempos.size() == 1 && middleTempos.front()->microsecondsPerQuarter == 96000,
         "segmented timer targets must retain the driver's byte wrap after rounded division");
}

void latePitchBendClampsToTheLegacyTwelveSemitoneWheel() {
  std::vector<u8> bytes(kAramSize);
  bytes[0x2000 + 0x22] = 0x40;
  addLatePitchTable(bytes);
  writeBytes(bytes, 0x3000, {0x94, 0x00, 0xff, 0x94, 0x00, 0xbf, 0x91});
  const PerformanceSequence performance = render(directLateLayout(Variant::TalesOfPhantasia, {0x3000}), bytes);
  const auto bends = events<PitchBendPerformanceEvent>(performance.tracks.front());
  expect(bends.size() == 2 && bends[0]->semitones == -12.0 &&
             std::abs(bends[1]->semitones - 8191.0 * 12.0 / 8192.0) < 1e-12,
         "94 must clamp wrapped centered bytes to the legacy signed 14-bit pitch-wheel endpoints");
}

void scannerBuildsTunedSynthAndCollection() {
  Session session;
  session.registerFormat(module());
  session.addSource(SourceFile{.name = "Wolf Team fixture.aram"}, lateScannerFixture());
  session.scanPendingSources();
  const SessionSnapshot snapshot = session.snapshot();
  expect(snapshot.collections().size() == 1, "Wolf Team scanner should publish one source collection");
  const Collection& collection = snapshot.collections().front();
  expect(collection.members.sequence && collection.members.instrumentSets.size() == 1 &&
             collection.members.sampleCollections.size() == 1,
         "the collection should connect the decoded sequence, patch set, and BRR sample collection");
  const auto* set = snapshot.asset<InstrumentSetAsset>(collection.members.instrumentSets.front());
  expect(set != nullptr && set->instruments.size() == 1 && set->instruments.front().regions.size() == 1,
         "invalid sparse sample slots must not hide the one valid late instrument");
  const Region& region = set->instruments.front().regions.front();
  expect(std::abs(region.unityKey - (72.0 - 12.0 * std::log2(4286.0 / 4096.0))) < 1e-9 &&
             std::abs(region.attenuationDb - (-20.0 * std::log10(0.5))) < 1e-9 && region.envelope.attackSeconds >= 0.0,
         "late key/tuning bias, sample volume, and fixed-gain DSP ADSR should match the driver tables");

  const auto* sequence = snapshot.asset<SequenceProgramAsset>(*collection.members.sequence);
  expect(sequence != nullptr && !snapshot.sources().empty(), "the scanned collection should retain its sequence asset");
  const SourceId source = snapshot.sources().front().id;
  const SourceMap& sourceMap = snapshot.sourceMap();
  const auto headers = sourceMap.withRole(source, SourceRole::Header);
  const auto tables = sourceMap.withRole(source, SourceRole::Table);
  const auto pointers = sourceMap.withRole(source, SourceRole::Pointer);
  const auto commands = sourceMap.withRole(source, SourceRole::Command);
  const bool commandHasStreamTargetFanout = std::ranges::any_of(commands, [&](SourceAnnotationId id) {
    return std::ranges::any_of(sourceMap.get(id).fields, [](const SourceField& field) {
      const std::string_view name = field.name;
      return name.starts_with("phrase_target_") || name.starts_with("segment_target_") ||
             name.starts_with("repeat_target_");
    });
  });
  const auto header = std::ranges::find_if(
      headers, [&](SourceAnnotationId id) { return sourceMap.get(id).localKind == "wolf-team-snes-sequence-header"; });
  const auto table = std::ranges::find_if(tables, [&](SourceAnnotationId id) {
    return sourceMap.get(id).localKind == "wolf-team-snes-stream-pointer-table";
  });
  const auto note =
      std::ranges::find_if(commands, [&](SourceAnnotationId id) { return sourceMap.get(id).range.offset == 0x6000; });
  expect(header != headers.end() && sourceMap.get(*header).owner == ObjectRefs::sequence(sequence->metadata.id) &&
             table != tables.end() &&
             sourceMap.get(*table).range == SourceRange{.source = source, .offset = 0x5000, .size = 4} &&
             std::ranges::count_if(pointers,
                                   [&](SourceAnnotationId id) {
                                     return sourceMap.get(id).localKind == "wolf-team-snes-stream-pointer";
                                   }) == 1 &&
             !commandHasStreamTargetFanout && note != commands.end() &&
             sourceMap.get(*note).detailKind == "wolf-team-snes.note" &&
             sourceMap.get(*note).range == SourceRange{.source = source, .offset = 0x6000, .size = 4},
         "sequence headers, stream tables, pointers, and exact command bytes should be connected in the source map");
}

void scannerBuildsArcusPitchModel() {
  Session session;
  session.registerFormat(module());
  session.addSource(SourceFile{.name = "Arcus fixture.aram"}, arcusScannerFixture());
  session.scanPendingSources();
  const SessionSnapshot snapshot = session.snapshot();
  expect(snapshot.collections().size() == 1, "Arcus scanner fixture should publish one source collection");
  const Collection& collection = snapshot.collections().front();
  expect(collection.members.instrumentSets.size() == 1, "Arcus collection should include its segmented patch set");
  const auto* set = snapshot.asset<InstrumentSetAsset>(collection.members.instrumentSets.front());
  expect(set != nullptr && set->instruments.size() == 1 && set->instruments.front().regions.size() == 1,
         "Arcus SRCN map should retain the one valid BRR-backed patch");
  const Region& region = set->instruments.front().regions.front();
  expect(std::abs(region.unityKey - (60.0 - 12.0 * std::log2(0x10be / 4096.0))) < 1e-9 &&
             region.envelope.attackSeconds >= 0.0,
         "Arcus region tuning must combine its coarse pitch and 0x10be pitch-table bias exactly once");
}

void brrAliasesIncludeLoopPosition() {
  const SnesBrrStream stream{.loops = true};
  const SnesBrrCatalog catalog{.samples = {
                                   {.srcn = 18, .startAddress = 0x8e8a, .loopAddress = 0x91b4, .stream = stream},
                                   {.srcn = 49, .startAddress = 0x8e8a, .loopAddress = 0x937f, .stream = stream},
                                   {.srcn = 53, .startAddress = 0x8e8a, .loopAddress = 0x937f, .stream = stream},
                               }};
  expect(catalog.canonicalIndex(18) == 0 && catalog.canonicalIndex(49) == 1 && catalog.canonicalIndex(53) == 1,
         "BRR aliases must include the Tales sample's loop position in their identity");
}

void sameKeyTimedNotesMoveThePendingNoteOff() {
  std::vector<u8> bytes(kAramSize);
  bytes[0x2000 + 0x22] = 0x40;
  addLatePitchTable(bytes);
  writeBytes(bytes, 0x4753, {0x40, 0x04, 0x60, 0xc8, 0x90, 0x5c, 0xfd});
  writeBytes(bytes, 0x47dc, {0x40, 0x60, 0x5f, 0xc8, 0xfd});

  const PerformanceSequence performance =
      render(directLateLayout(Variant::TalesOfPhantasia, {0x4753, 0x47dc}), std::move(bytes));
  const auto notes = events<NotePerformanceEvent>(performance.tracks.front());
  expect(
      notes.size() == 1 && notes[0]->header.tick == 0 && notes[0]->durationTicks == 192 && !notes[0]->extendsPrevious,
      "Freeze's 0x47dc note must move the pending 0x4753 note-off without another attack");

  const MidiSequence midi = renderMidiSequence(performance);
  std::vector<NoteDuration> midiNotes;
  for (const MidiEvent& event : midi.tracks.front().events) {
    if (const auto* note = std::get_if<NoteDuration>(&event)) {
      midiNotes.push_back(*note);
    }
  }
  expect(midiNotes.size() == 1 && midiNotes.front().tick == 0 && midiNotes.front().duration == 192,
         "Freeze MIDI lowering must keep one attack and move its note-off to the 0x47dc command's new end");

  std::vector<u8> segmented(kAramSize);
  segmented[0x1800 + 0x22] = 0x40;
  addSegmentedPitchTable(segmented);
  writeBytes(segmented, 0x3000, {0x60, 0x04, 0x0a, 0x7f, 0x60, 0x05, 0x14, 0x7f, 0xfd});
  writeBytes(segmented, 0x3040, {0xe1, 0x1e, 0x7f, 0xfd});
  const PerformanceSequence segmentedPerformance = render(directArcusLayout({0x3000, 0x3040}), std::move(segmented));
  const auto segmentedNotes = events<NotePerformanceEvent>(segmentedPerformance.tracks.front());
  expect(segmentedNotes.size() == 1 && segmentedNotes.front()->durationTicks == 24,
         "segmented Wolf Team timed notes must use the same pending-note-off behavior");
}

void streamListsStayOutOfCommandAnnotations() {
  std::vector<u8> bytes(kAramSize);
  addLatePitchTable(bytes);
  writeBytes(bytes, 0x3000, {0x92, 0x60, 0x01, 0x00, 0x7f, 0x93, 0x02, 0x91});
  writeBytes(bytes, 0x3040, {0x61, 0x01, 0x00, 0x7f, 0x91});
  writeBytes(bytes, 0x3080, {0x62, 0x01, 0x00, 0x7f, 0x91});

  const Layout layout = directLateLayout(Variant::TalesOfPhantasia, {0x3000, 0x3040, 0x3080});
  const SequenceParse parsed = decodeSequence(ByteReader(SourceId{190}, bytes), layout, AssetId{190});
  const auto hasRuntimeTargetOperand = [](const SequenceProgram& program) {
    return std::ranges::any_of(program.tracks, [](const TrackProgram& track) {
      return std::ranges::any_of(track.commands, [](const SourceCommand& command) {
        return std::ranges::any_of(command.operands, [](const SemanticOperand& operand) {
          const std::string_view name = operand.name;
          return name.starts_with("phrase_target_") || name.starts_with("segment_target_") ||
                 name.starts_with("repeat_target_");
        });
      });
    });
  };
  const auto hasOnlyEndBoundaries = [](const SequenceProgram& program, u8 opcode, size_t expected) {
    const auto& commands = program.tracks.front().commands;
    return std::ranges::count_if(
               commands, [opcode](const SourceCommand& command) { return command.opcode == opcode; }) == expected &&
           std::ranges::none_of(commands, [opcode](const SourceCommand& command) {
             return command.opcode == opcode && command.semantic != SequenceSemantic::End;
           });
  };
  expect(validateSequenceProgram(parsed.program).empty() && !hasRuntimeTargetOperand(parsed.program) &&
             hasOnlyEndBoundaries(parsed.program, 0x91, 3) && parsed.program.runtime.valid() &&
             parsed.program.tracks.front().startAddress.value == 0x3000,
         "phrase ordering belongs to compact track runtime state, while each source boundary remains context-neutral");

  std::vector<u8> segmented(kAramSize);
  addSegmentedPitchTable(segmented);
  writeBytes(segmented, 0x3000, {0x60, 0x01, 0x00, 0x7f, 0xfd});
  writeBytes(segmented, 0x3040, {0x61, 0x01, 0x00, 0x7f, 0xfd});
  writeBytes(segmented, 0x3080, {0x62, 0x01, 0x00, 0x7f, 0xfd});
  const Layout segmentedLayout = directArcusLayout({0x3000, 0x3040, 0x3080});
  const SequenceParse segmentedParsed =
      decodeSequence(ByteReader(SourceId{191}, segmented), segmentedLayout, AssetId{191});
  expect(validateSequenceProgram(segmentedParsed.program).empty() &&
             !hasRuntimeTargetOperand(segmentedParsed.program) &&
             hasOnlyEndBoundaries(segmentedParsed.program, 0xfd, 3) && segmentedParsed.program.runtime.valid() &&
             segmentedParsed.program.tracks.front().startAddress.value == 0x3000,
         "segment ordering belongs to compact track runtime state, not control-command annotations");
}

}  // namespace

void runWolfTeamSnesModuleTests() {
  scannersIdentifyAllThreeDriverEras();
  lateCommandsRenderLoopsSplitsLfoAndDynamicAdsr();
  disassemblyCommandSetsHaveAuditedSizes();
  arcusSustainsUntilKeyOffAndUsesPhysicalPitch();
  duplicateSegmentPointersUseRuntimeIndex();
  segmentedKeyOffRevisesEveryActiveNoteIdentity();
  duplicatePhrasePointersUseRuntimeIndex();
  nestedLateLoopMarkersPreferTheInnerCounter();
  leadingJockeyProgramChangesCarryTheirDelay();
  middleFinePitchInterpolatesAdjacentDspWords();
  timerArithmeticKeepsLateWidthAndSegmentedByteWrap();
  latePitchBendClampsToTheLegacyTwelveSemitoneWheel();
  scannerBuildsTunedSynthAndCollection();
  scannerBuildsArcusPitchModel();
  brrAliasesIncludeLoopPosition();
  sameKeyTimedNotesMoveThePendingNoteOff();
  streamListsStayOutOfCommandAnnotations();
}
