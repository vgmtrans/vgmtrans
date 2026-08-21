/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/CompileSnes/CompileSnes.h"

#include "value/export/midi/PerformanceMidiRenderer.h"
#include "value/sequence/SequenceVm.h"
#include "value/session/Session.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

using namespace vgmtrans::core;
using namespace vgmtrans::formats::compile_snes;

namespace {

void expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void writeLe16(std::vector<u8>& bytes, u32 offset, u16 value) {
  bytes[offset] = static_cast<u8>(value);
  bytes[offset + 1] = static_cast<u8>(value >> 8);
}

void writeBytes(std::vector<u8>& bytes, u32 offset, std::initializer_list<u8> values) {
  std::ranges::copy(values, bytes.begin() + offset);
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

std::vector<u8> fixture(std::initializer_list<u8> score = {
                            0x9f,
                            0x01,
                            0x9f,
                            0x81,
                            0x83,
                            0x01,
                            0x88,
                            0x01,
                            0xa3,
                            0x01,
                            0xa8,
                            0x01,
                            0xb0,
                            0x01,
                            0x60,
                            0xdf,
                            0x82,
                        }) {
  std::vector<u8> bytes(kAramSize);
  bytes[1] = 4;
  writeBytes(bytes, 0x00fc, {0x28, 0x0c});
  writeBytes(bytes, 0x0100, {0xe5, 0x00, 0x18, 0xc4, 0x50, 0xe5, 0x01, 0x18, 0xc4, 0x51, 0xe5, 0x02, 0x18, 0xc4, 0x52,
                             0x60, 0x88, 0x11, 0xc4, 0x54, 0xe5, 0x03, 0x18, 0xc4, 0x53, 0x88, 0x00, 0xc4, 0x55});

  writeLe16(bytes, 0x1800, 0x2000);
  writeLe16(bytes, 0x1802, 0x2200);
  writeLe16(bytes, 0x1804, 0x2400);
  writeLe16(bytes, 0x1806, 0x2600);
  writeLe16(bytes, 0x180a, 0x2800);
  writeLe16(bytes, 0x180c, 0x2a00);
  bytes[0x180e] = 0x40;
  writeLe16(bytes, 0x1810, 0x2c00);
  writeLe16(bytes, 0x1812, 0x2e00);
  writeLe16(bytes, 0x1814, 0x3000);
  writeLe16(bytes, 0x1816, 0x3200);

  writeLe16(bytes, 0x2002, 0x2100);
  bytes[0x0080] = 1;
  bytes[0x01f0] = 1;
  writeBytes(bytes, 0x2100, {1, 0, 0, 31, 0, 0, 0, 0x80, 2, 0x00, 0x36, 0, 0, 0, 0});
  std::ranges::copy(score, bytes.begin() + 0x3600);

  bytes[0x2201] = 4;
  writeLe16(bytes, 0x2402, 0x2500);
  writeBytes(bytes, 0x2500, {1, 31, 1, 15, 0x80});
  writeLe16(bytes, 0x2602, 0x2700);
  writeBytes(bytes, 0x2700, {1, 0, 1, 16, 0x81, 4});
  writeLe16(bytes, 0x2802, 0x2900);
  writeBytes(bytes, 0x2900, {1, 0x9f, 1, 0x7f, 0x80});
  writeBytes(bytes, 0x2a00, {0x8f, 0xe0, 0x8e, 0xc0});
  writeLe16(bytes, 0x2c02, 0x3800);
  writeBytes(bytes, 0x3800, {0x20, 2, 64, 0xc0, 32, 0, 0, 0, 0x7f, 0, 0, 0, 0, 0, 0, 0});
  writeLe16(bytes, 0x3002, 0x3100);
  writeBytes(bytes, 0x3100, {0, 4, 0xff, 1, 4, 4, 0, 0});

  constexpr std::array<u16, 8> prefix{0x12, 0x13, 0x14, 0x15, 0x17, 0x18, 0x19, 0x1b};
  for (u32 key = 1; key <= 120; ++key) {
    const double pitch = 4096.0 * std::exp2((static_cast<int>(key) - 96) / 12.0);
    writeLe16(bytes, 0x3400 + key * 2, static_cast<u16>(std::clamp(std::lround(pitch), 1l, 0x3fffl)));
  }
  for (u32 key = 0; key < prefix.size(); ++key) {
    writeLe16(bytes, 0x3402 + key * 2, prefix[key]);
  }

  writeLe16(bytes, 0x4000, 0x4100);
  writeLe16(bytes, 0x4002, 0x4100);
  writeBytes(bytes, 0x4100, {1, 0, 0, 0, 0, 0, 0, 0, 0});
  return bytes;
}

SequenceParse parse(const std::vector<u8>& bytes) {
  const ByteReader reader(SourceId{301}, bytes);
  const auto layout = findLayout(reader);
  expect(layout.has_value(), "CompileSnes fixture should be recognized");
  return decodeSequence(RetainedSource::copyOf(reader), *layout, AssetId{1});
}

PerformanceSequence render(const std::vector<u8>& bytes) {
  SequenceParse parsed = parse(bytes);
  return SequenceVm(LoopPolicy::PlayOnce).render(parsed.program);
}

void layoutAndModuleUseLiveSongState() {
  const std::vector<u8> bytes = fixture();
  const auto layout = findLayout(ByteReader(SourceId{302}, bytes));
  expect(layout && layout->version == Version::SuperPuyo && layout->engineHeaderAddress == 0x1800 &&
             layout->songIndex == 1 && layout->songHeaderAddress == 0x2100 &&
             layout->regularPitchTableAddress == 0x3400 && layout->spcDirAddress == 0x4000 && layout->stereoEnabled,
         "CompileSnes should recover its revision, live song, tables, fixed pitch base, and DIR");

  Session session;
  session.registerFormat(module());
  session.addSource(SourceFile{.name = "CompileSnes fixture.aram"}, bytes);
  session.scanPendingSources();
  const SessionSnapshot snapshot = session.snapshot();
  expect(snapshot.diagnostics().empty() && snapshot.collections().size() == 1 && snapshot.assets().size() == 2,
         "CompileSnes scanning should publish a sequence and self-contained sound bank");
}

void commandWidthsFollowEachDriverRevision() {
  std::vector<u8> bytes = fixture();
  auto layout = *findLayout(ByteReader(SourceId{303}, bytes));
  layout.version = Version::Standard;
  writeBytes(bytes, 0, {0xa8, 0xb0, 0x97, 1, 2, 0x82});
  TrackProgram standard = decodeSourceTrack(ByteReader(SourceId{303}, bytes), layout, 0, 0);
  expect(standard.commands.size() == 4 && standard.commands[0].range.size == 1 &&
             standard.commands[1].range.size == 1 && standard.commands[2].range.size == 3,
         "late non-echo builds should keep A8/B0 one-byte and tuning three-byte aligned");

  layout.version = Version::SuperPuyo;
  writeBytes(bytes, 0, {0xa8, 1, 0xb0, 1, 0x97, 1, 2, 0x82});
  TrackProgram puyo = decodeSourceTrack(ByteReader(SourceId{304}, bytes), layout, 0, 0);
  expect(puyo.commands.size() == 4 && puyo.commands[0].range.size == 2 && puyo.commands[1].range.size == 2 &&
             puyo.commands[2].range.size == 3,
         "Super Puyo should consume the audited echo preset/global arguments");

  layout.version = Version::Aleste;
  writeBytes(bytes, 0, {0xa4, 0xa5, 0x97, 1, 0x82});
  TrackProgram aleste = decodeSourceTrack(ByteReader(SourceId{305}, bytes), layout, 0, 0);
  expect(aleste.commands.size() == 4 && aleste.commands[0].range.size == 1 && aleste.commands[1].range.size == 1 &&
             aleste.commands[2].range.size == 2,
         "Aleste should retain its zero-argument A4/A5 controls and one-byte tuning delta");
}

void frameCurvesDynamicAdsrAndEchoRenderPhysically() {
  const PerformanceSequence performance = render(fixture());
  const PerformanceTrack& track = performance.tracks.front();
  const auto notes = events<NotePerformanceEvent>(track);
  const auto envelopes = events<EnvelopePerformanceEvent>(track);
  const auto levels = events<LevelPerformanceEvent>(track);
  const auto bends = events<PitchBendPerformanceEvent>(track);
  const auto pans = events<StereoBalancePerformanceEvent>(track);
  const auto reverbs = events<ReverbPerformanceEvent>(track);
  const auto tempos = events<TempoPerformanceEvent>(track);
  expect(performance.diagnostics.empty() && performance.timebase.ppqn == 12 && notes.size() == 1 &&
             std::abs(notes.front()->key - 96.0) < 0.001 && notes.front()->durationTicks == 4 &&
             tempos.front()->microsecondsPerQuarter == 384000,
         "Compile notes and the tempo accumulator should render at their source timing");
  expect(envelopes.size() >= 4 && levels.size() >= 3 && bends.size() >= 3 && pans.size() >= 2,
         "ADSR/GAIN, software volume, vibrato, and pan curves should advance on driver frames");
  const double echoLeft = 64.0 / 127.0 * 64.0 / 256.0;
  const double echoRight = 64.0 / 127.0 * 191.0 / 256.0;
  expect(!reverbs.empty() && reverbs.back()->delayMilliseconds == 32.0 && reverbs.back()->leftGain == echoLeft &&
             reverbs.back()->rightGain == echoRight && reverbs.back()->feedback == 0.25 &&
             reverbs.back()->voiceMask == 1 && reverbs.back()->send == echoRight,
         "echo presets should preserve EDL, signed EVOL, feedback, voice mask, and global enable");
}

void standaloneDurationsRepeatTheCurrentNoteAndGate() {
  const PerformanceSequence repeated = render(fixture({0x60, 0xdf, 0xdf, 0x82}));
  const auto repeatedNotes = events<NotePerformanceEvent>(repeated.tracks.front());
  expect(repeated.diagnostics.empty() && repeatedNotes.size() == 2 && repeatedNotes[0]->header.tick == 0 &&
             repeatedNotes[1]->header.tick == 4,
         "standalone duration opcodes should replay the current note instead of acting as metadata only");

  const PerformanceSequence gated = render(fixture({0x60, 0xf0, 2, 0x82}));
  const auto gatedNotes = events<NotePerformanceEvent>(gated.tracks.front());
  expect(gated.diagnostics.empty() && gatedNotes.size() == 1 && gatedNotes.front()->durationTicks == 2,
         "duration-table commands with a gate byte should preserve early key-off timing");
}

void trackAndPercussionFlagsDoNotBecomeStereoPhase() {
  std::vector<u8> bytes = fixture({0x90, 0x01, 0xc0, 0xdf, 0x82});
  bytes[0x2102] = 0x01;
  writeBytes(bytes, 0x2211, {0, 0, 0, 0, 0x20, 0, 0, 0x60});

  const PerformanceSequence performance = render(bytes);
  const auto balances = events<StereoBalancePerformanceEvent>(performance.tracks.front());
  expect(balances.size() >= 2 &&
             std::ranges::all_of(
                 balances, [](const auto* balance) { return balance->leftGain > 0.49 && balance->rightGain > 0.49; }),
         "track and percussion flags must not be mistaken for the separate stereo-phase register");
}

void monoModeForcesCenterAndIgnoresStereoPhase() {
  std::vector<u8> bytes = fixture({0xab, 0xe2, 0x92, 0x02, 0x60, 0xdf, 0x82});
  bytes[1] = 0;

  const auto layout = findLayout(ByteReader(SourceId{306}, bytes));
  const PerformanceSequence performance = render(bytes);
  const auto balances = events<StereoBalancePerformanceEvent>(performance.tracks.front());
  expect(layout && !layout->stereoEnabled && !balances.empty() &&
             std::ranges::all_of(
                 balances, [](const auto* balance) { return balance->leftGain > 0.49 && balance->rightGain > 0.49; }),
         "Compile mono mode should force pan to center and ignore stereo-phase commands");
}

void pitchSweepAdvancesThroughThePitchTable() {
  const PerformanceSequence downward = render(fixture({0x94, 0x02, 0x60, 0xde, 6, 0x94, 0x00, 0x82}));
  expect(!downward.tracks.front().automations.empty(), "an active pitch sweep should create a pitch transition");
  const auto* downSlide = pitchTransitionIntent(downward.tracks.front().automations.front());
  expect(downSlide != nullptr && std::holds_alternative<LinearAutomationCurve>(downSlide->curve) &&
             std::abs(pitchTransitionValueAt(*downSlide, 1) - downSlide->startKey + 4.0) < 0.05 &&
             std::abs(pitchTransitionValueAt(*downSlide, 2) - downSlide->startKey + 8.0) < 0.05 &&
             pitchTransitionValueAt(*downSlide, 6) - downSlide->startKey < -20.0 &&
             downSlide->preferredRendering == PitchTransitionRenderingHint::PitchBend,
         "positive pitch-sweep rates should become smooth transitions over the note-table-derived range");

  const MidiSequence midi = renderMidiSequence(downward);
  const auto hasBendAt = [&](u64 tick) {
    return std::ranges::any_of(midi.tracks.front().events, [tick](const MidiEvent& event) {
      const auto* bend = std::get_if<PitchBend>(&event);
      return bend != nullptr && bend->tick == tick;
    });
  };
  expect(midi.timebase.ppqn == 12 && hasBendAt(1) && hasBendAt(2) && hasBendAt(3),
         "Compile MIDI should preserve the 12 PPQN source grid");

  const PerformanceSequence upward = render(fixture({0x94, 0x82, 0x60, 0xde, 3, 0x94, 0x00, 0x82}));
  expect(!upward.tracks.front().automations.empty(), "an upward pitch sweep should create a pitch transition");
  const auto* upSlide = pitchTransitionIntent(upward.tracks.front().automations.front());
  expect(upSlide != nullptr && std::abs(pitchTransitionValueAt(*upSlide, 1) - upSlide->startKey - 4.0) < 0.05,
         "pitch-sweep rates with bit 7 set should ascend by note-table steps");
}

void portamentoUsesDriverRateAndRetriggersFirstNote() {
  const PerformanceSequence performance =
      render(fixture({0x60, 0xdf, 0xa0, 0x03, 0xa1, 0x84, 0x07, 0x62, 0xdf, 0x64, 0xdf, 0x82}));
  const auto* slide = performance.tracks.front().automations.empty()
                          ? nullptr
                          : pitchTransitionIntent(performance.tracks.front().automations.back());
  const auto* timing = slide == nullptr ? nullptr : std::get_if<FixedDurationPitchSlideTiming>(&slide->timing.physical);
  const MidiSequence midi =
      renderMidiSequence(performance, MidiExportOptions{.pitchTransitions = MidiPitchTransitionRendering::PitchBend});
  const auto noteCount = std::ranges::count_if(
      midi.tracks.front().events, [](const MidiEvent& event) { return std::holds_alternative<NoteDuration>(event); });
  const auto portamentoCount = std::ranges::count_if(midi.tracks.front().events, [](const MidiEvent& event) {
    return std::holds_alternative<PortamentoEnable>(event) || std::holds_alternative<PortamentoTime>(event) ||
           std::holds_alternative<PortamentoTime14>(event) || std::holds_alternative<PortamentoControl>(event);
  });
  expect(slide != nullptr && slide->timing.timelineTicks == 1 && timing != nullptr && timing->milliseconds == 16.0 &&
             noteCount == 2 && portamentoCount == 0,
         "Compile portamento should use driver-rate timing, retrigger its first note, and lower cleanly to pitch bend");
}

void sourceBackedRuntimeOutlivesItsInputBuffer() {
  SequenceProgram program = [] {
    const std::vector<u8> bytes = fixture();
    return parse(bytes).program;
  }();
  const PerformanceSequence performance = SequenceVm(LoopPolicy::PlayOnce).render(program);
  expect(performance.diagnostics.empty() && !performance.tracks.empty(),
         "deferred playback should retain the source tables it reads");
}

}  // namespace

void runCompileSnesModuleTests() {
  layoutAndModuleUseLiveSongState();
  commandWidthsFollowEachDriverRevision();
  frameCurvesDynamicAdsrAndEchoRenderPhysically();
  standaloneDurationsRepeatTheCurrentNoteAndGate();
  trackAndPercussionFlagsDoNotBecomeStereoPhase();
  monoModeForcesCenterAndIgnoresStereoPhase();
  pitchSweepAdvancesThroughThePitchTable();
  portamentoUsesDriverRateAndRetriggersFirstNote();
  sourceBackedRuntimeOutlivesItsInputBuffer();
}
