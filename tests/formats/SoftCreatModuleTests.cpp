/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/SoftCreat/SoftCreat.h"

#include "value/export/midi/PerformanceMidiRenderer.h"
#include "value/sequence/SequenceVm.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

using namespace vgmtrans::core;
using namespace vgmtrans::formats::softcreat;

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

PerformanceSequence render(std::initializer_list<u8> commands, Version version = Version::Early) {
  std::vector<u8> bytes(kAramSize);
  constexpr u16 start = 0x1000;
  std::ranges::copy(commands, bytes.begin() + start);
  for (u32 note = 0; note < 0x80; ++note) {
    const u16 pitch = static_cast<u16>(
        std::clamp(std::lround(0x1000 * std::exp2((static_cast<double>(note) - 62.0) / 12.0)), 1l, 0xffffl));
    bytes[0x0200 + note] = static_cast<u8>(pitch);
    bytes[0x0280 + note] = static_cast<u8>(pitch >> 8);
  }
  Layout layout{
      .version = version,
      .initialTimer = 0x84,
      .musicVolume = 0x80,
      .pitchLowTableAddress = 0x0200,
      .pitchHighTableAddress = 0x0280,
      .coarseTableAddress = 0x0300,
      .fineTableAddress = 0x0400,
      .envelopeTableAddress = 0x0500,
  };
  const ByteReader reader(SourceId{301}, bytes);
  SequenceProgram program = sequenceConfig().makeProgram();
  program.runtime = sequenceRuntime(RetainedSource::copyOf(reader), layout);
  program.tracks.push_back(decodeSourceTrack(reader, layout, 0, start));
  return SequenceVm(LoopPolicy::PlayOnce).render(program);
}

std::vector<u8> scannerFixture() {
  std::vector<u8> bytes(kAramSize);
  writeBytes(bytes, 0x0100,
             {0x7d, 0x68, 0x05, 0xb0, 0xfa, 0xfd, 0xcd, 0x00, 0xf6, 0x03, 0x20, 0xf0, 0x0a,
              0xc4, 0x31, 0xf6, 0x00, 0x20, 0xc4, 0x30, 0x3f, 0x38, 0x06, 0x3d, 0x3d});
  writeBytes(bytes, 0x0180,
             {0x3f, 0x85, 0x07, 0x10, 0x1d, 0x68, 0xc7, 0xb0, 0x0b, 0x1c,
              0xfd, 0xf6, 0x70, 0x0b, 0x2d, 0xf6, 0x00, 0x10, 0x2d, 0x6f});
  writeBytes(bytes, 0x0300,
             {0xfb, 0x20, 0x60, 0x96, 0x00, 0x42, 0x5b, 0xc0, 0xb0, 0x04,
              0x60, 0x95, 0x40, 0x02, 0xd5, 0xb0, 0x02, 0xd5, 0x90, 0x02});
  writeBytes(bytes, 0x0340,
             {0xf6, 0x00, 0x40, 0xc4, 0xd9, 0xf6, 0x55, 0x40, 0xc4, 0xda, 0xfb, 0x20, 0xf6,
              0x00, 0x41, 0xfd, 0x6d, 0xe4, 0xd9, 0xcf, 0xcb, 0xdd, 0xee, 0xe4, 0xda, 0xcf,
              0x8f, 0x00, 0xde, 0x7a, 0xdd, 0x7a, 0xd9});
  writeBytes(bytes, 0x0400, {0xe8, 0x00, 0xc4, 0xd9, 0xe8, 0x43, 0xc4, 0xda});
  writeBytes(bytes, 0x0480, {0x8f, 0x92, 0xfc, 0x8f, 0x04, 0xf1});
  writeBytes(bytes, 0x0500,
             {0x2c, 0x3c, 0x5c, 0x2d, 0x3d, 0x4d, 0x7d, 0x6d, 0x0d, 0x5d, 0x0f, 0x1f, 0x2f, 0x3f,
              0x4f, 0x5f, 0x6f, 0x7f, 0x05, 0x15, 0x25, 0x35, 0x45, 0x55, 0x65, 0x75, 0xff});
  bytes[0x0500 + 27 + 9] = 0x44;
  bytes[0xe4] = 2;
  bytes[0xe8] = 0x70;
  bytes[0x2002] = 0x00;
  bytes[0x2005] = 0x30;
  bytes[0x3000] = 0x80;
  writeLe16(bytes, 0x4400, 0x4500);
  writeLe16(bytes, 0x4402, 0x4500);
  bytes[0x4500] = 1;
  return bytes;
}

void layoutUsesLiveSongAndAuditedTables() {
  const std::vector<u8> bytes = scannerFixture();
  const auto layout = findLayout(ByteReader(SourceId{302}, bytes));
  expect(layout && layout->version == Version::LateEcho && layout->songIndex == 2 &&
             layout->tracks[0].address == 0x3000 && layout->pitchLowTableAddress == 0x4000 &&
             layout->pitchHighTableAddress == 0x4055 && layout->coarseTableAddress == 0x4200 &&
             layout->fineTableAddress == 0x4100 && layout->envelopeTableAddress == 0x4300 &&
             layout->spcDirAddress == 0x4400 && layout->initialTimer == 0x92 && layout->musicVolume == 0x70,
         "SoftCreat layout should use the live song and recover every relocated driver table");
}

void versionedOpcodesRetainTheirRealOperandLengths() {
  std::vector<u8> bytes(kAramSize);
  writeBytes(bytes, 0x1000, {0xa1, 1, 2, 3, 4, 5, 6, 7, 0x80});
  Layout maximum{.version = Version::MaximumCarnage};
  const TrackProgram maxTrack = decodeSourceTrack(ByteReader(SourceId{303}, bytes), maximum, 0, 0x1000);
  expect(maxTrack.commands.size() == 2 && maxTrack.commands.front().range.size == 8 &&
             maxTrack.commands.front().semantic == SequenceSemantic::Envelope,
         "Maximum Carnage A1 should be the seven-byte inline software envelope");

  writeBytes(bytes, 0x1000, {0xaa, 0x40, 0x80});
  Layout late{.version = Version::LateNoEcho};
  const TrackProgram lateTrack = decodeSourceTrack(ByteReader(SourceId{304}, bytes), late, 0, 0x1000);
  expect(lateTrack.commands.size() == 2 && lateTrack.commands.front().range.size == 2 &&
             lateTrack.commands.front().semantic == SequenceSemantic::Level,
         "Tin Star/Foreman AA should consume a volume-decay factor, not toggle echo");
}

void physicalEffectsAndSoftwareGainRender() {
  const PerformanceSequence performance = render(
      {0x86, 8, 0xb3, 0x70, 0x80, 0xa2, 1, 0, 3, 0x7f, 3, 0x40, 3, 0x92, 2,
       0x8e, 0, 4, 2, 1, 0xaa, 0xac, 0x40, 0xad, 0xc0, 0xae, 0xe0,
       0xaf, 0x7f, 0, 0, 0, 0, 0, 0, 0, 0x80});
  const PerformanceTrack& track = performance.tracks.front();
  const auto notes = events<NotePerformanceEvent>(track);
  const auto expression = events<ExpressionPerformanceEvent>(track);
  const auto modulation = events<ModulationPerformanceEvent>(track);
  const auto reverb = events<ReverbPerformanceEvent>(track);
  const auto balance = events<StereoBalancePerformanceEvent>(track);
  expect(performance.diagnostics.empty() && notes.size() == 1 && notes.front()->durationTicks == 8,
         "the feature fixture should render one eight-tick note without diagnostics");
  expect(!expression.empty() && std::ranges::any_of(expression, [](const auto* event) {
           return event->linearGain < 0.01;
         }),
         "dynamic GAIN should emit its zero attack/release level");
  expect(std::ranges::any_of(modulation, [](const auto* event) {
           return event->target == ModulationPerformanceTarget::VibratoDepth && event->pitchDepthSemitones &&
                  *event->pitchDepthSemitones > 0.001 && event->context.shape &&
                  event->context.shape->waveform == LfoWaveform::Triangle &&
                  event->context.polarity == LfoPolarity::Positive && event->context.pitchRangeSemitones &&
                  event->context.pitchRangeSemitones->minimum == 0.0 &&
                  event->context.pitchRangeSemitones->maximum > 0.0;
         }),
         "additive one-sided triangle vibrato should retain its physical pitch range and shape");
  expect(reverb.size() >= 5 && reverb.back()->voiceMask == 1 && reverb.back()->leftGain &&
             *reverb.back()->leftGain == 0.5 && reverb.back()->rightGain && *reverb.back()->rightGain == -0.5 &&
             reverb.back()->feedback == -0.25 && reverb.back()->filterIndex == 0,
         "sequence echo commands should preserve signed DSP volumes, EON, feedback, and FIR presets");
  expect(!balance.empty() && balance.front()->leftGain != balance.front()->rightGain,
         "the driver's signed stereo mixer should remain a stereo-balance event");
}

void gainHoldContinuesTheCurrentEnvelope() {
  const PerformanceSequence performance =
      render({0xa2, 1, 0, 6, 50, 1, 50, 6, 0x3d, 1, 0x9f, 0x90, 16, 0x9c, 0x3c, 30, 0x80});
  const auto expression = events<ExpressionPerformanceEvent>(performance.tracks.front());
  const auto modulation = events<ModulationPerformanceEvent>(performance.tracks.front());
  expect(performance.diagnostics.empty() && expression.size() >= 6 && expression[0]->header.tick == 0 &&
             expression[0]->linearGain == 0.0 && expression[1]->header.tick == 1 &&
             std::abs(expression[1]->linearGain - 10.0 / 127.0) < 0.000001 &&
             expression[5]->header.tick == 5 && std::abs(expression[5]->linearGain - 50.0 / 127.0) < 0.000001,
         "GAIN should attack on the note tick and keep advancing after retrigger is disabled");
  expect(modulation.empty(), "legato notes without vibrato should not emit redundant modulation resets");
}

void restsPreserveTheKeyedVoice() {
  const PerformanceSequence performance =
      render({0xa2, 1, 120, 1, 120, 1, 120, 14, 0x93, 5, 0x84, 2, 0x18, 20, 0x9f, 0x18, 10, 0x18, 10,
              0x93, 0, 0x18, 40, 0, 160, 0, 80, 0x85, 0x9e, 0x19, 1, 0x80});
  const auto notes = events<NotePerformanceEvent>(performance.tracks.front());
  const MidiSequence midi = renderMidiSequence(performance);
  const auto heldNote = std::ranges::find_if(midi.tracks.front().events, [](const MidiEvent& event) {
    const auto* note = std::get_if<NoteDuration>(&event.payload);
    return note != nullptr && event.tick == 0 && note->duration == 640;
  });
  expect(performance.diagnostics.empty() && notes.size() == 13 && notes[4]->header.tick == 80 &&
             notes[4]->extendsPrevious && notes[5]->header.tick == 240 && notes[5]->extendsPrevious &&
             notes[6]->header.tick == 320 && notes[6]->extendsPrevious && notes[6]->note == notes[0]->note &&
             notes[12]->header.tick == 640 && !notes[12]->extendsPrevious && notes[12]->note != notes[0]->note &&
             heldNote != midi.tracks.front().events.end(),
         "rests should extend a keyed voice until a retriggering note sends KOFF/KON");

  const PerformanceSequence releasedRest =
      render({0xa2, 1, 120, 1, 120, 1, 120, 14, 0x93, 0, 0x18, 20, 0x93, 5, 0, 20, 0x80});
  const auto expression = events<ExpressionPerformanceEvent>(releasedRest.tracks.front());
  expect(std::ranges::any_of(expression, [](const auto* event) { return event->header.tick == 35; }),
         "rest durations should schedule software release just like note durations");
}

void durationModesLegatoAndRepeatsAreStateful() {
  const PerformanceSequence repeated = render({0x86, 4, 0x84, 2, 1, 0x85, 0x9f, 2, 0x80});
  const auto notes = events<NotePerformanceEvent>(repeated.tracks.front());
  expect(repeated.diagnostics.empty() && notes.size() == 3 && notes[0]->header.tick == 0 &&
             notes[1]->header.tick == 4 && notes[2]->header.tick == 8 && !notes[2]->restartsEnvelope,
         "persistent duration, repeat-stack counts, and retrigger suppression should share runtime state");

  const PerformanceSequence wrappedRepeat = render({0x86, 1, 0x84, 0, 1, 0x85, 0x80});
  expect(wrappedRepeat.diagnostics.empty() &&
             events<NotePerformanceEvent>(wrappedRepeat.tracks.front()).size() == 256,
         "a zero repeat byte should wrap through all 256 SPC700 counter values");

  const PerformanceSequence perNote =
      render({0x86, 3, 0xbf, 0, 0x20, 1, 0x40, 0xc0, 2, 0x80}, Version::LateEcho);
  expect(perNote.diagnostics.empty() && events<NotePerformanceEvent>(perNote.tracks.front()).size() == 2 &&
             !events<StereoBalancePerformanceEvent>(perNote.tracks.front()).empty(),
         "late per-note volume mode should consume a suffix on rests and notes and affect the mixer");

  const PerformanceSequence polymorphicTail =
      render({0x84, 2, 0, 1, 0xbf, 0x86, 1, 1, 0x40, 0x86, 0, 0x85, 0x80}, Version::LateEcho);
  const auto polymorphicNotes = events<NotePerformanceEvent>(polymorphicTail.tracks.front());
  expect(polymorphicTail.diagnostics.empty() &&
             std::ranges::count_if(polymorphicNotes, [](const auto* note) { return !note->extendsPrevious; }) == 2,
         "a byte that becomes a per-note suffix on a later pass should retain both control-flow interpretations");
}

void perNoteVolumePrecedesLiteralDuration() {
  const PerformanceSequence performance =
      render({0xb9, 0x19, 100, 12, 0x80}, Version::MaximumCarnage);
  const auto notes = events<NotePerformanceEvent>(performance.tracks.front());
  const auto balance = events<StereoBalancePerformanceEvent>(performance.tracks.front());
  expect(performance.diagnostics.empty() && notes.size() == 1 && notes.front()->durationTicks == 12 &&
             balance.size() == 1 && std::abs(balance.front()->rightGain - 100.0 / 256.0) < 0.000001,
         "per-note mode should read volume before a literal duration, matching the SPC700 driver");
}

void pitchEffectsRetainPhysicalTiming() {
  const PerformanceSequence portamento = render({0x86, 4, 0x32, 0x90, 0x40, 0x3e, 0x80});
  expect(portamento.diagnostics.empty() &&
             std::ranges::any_of(portamento.tracks.front().automations, [](const PerformanceAutomation& automation) {
               const auto* slide = std::get_if<PitchTransitionIntent>(&automation.intent);
               return slide != nullptr && slide->timing.timelineTicks == 32 && slide->targetKey - slide->startKey > 11.9;
             }),
         "raw-pitch portamento should retain the driver's fixed step rate as a physical pitch transition");

  const PerformanceSequence retriggered =
      render({0x3b, 1, 0x90, 16, 0x9f, 0x3d, 29, 0x90, 8, 0x84, 2, 0x3d, 10, 0x3e, 5, 0x85, 0x9e, 0x90, 0,
              0x3b, 30, 0x80});
  expect(retriggered.diagnostics.empty() &&
             std::ranges::any_of(retriggered.tracks.front().automations, [](const auto& automation) {
               const auto* slide = std::get_if<PitchTransitionIntent>(&automation.intent);
               return slide != nullptr && automation.header.tick == 55 && automation.realization.endTick == 60 &&
                      automation.realization.endReason == PerformanceAutomationEndReason::Interrupted &&
                      !slide->continuesAcrossNotes;
             }),
         "a new attack should cancel the preceding legato portamento instead of bending the fresh note");

  const PerformanceSequence trill = render({0x86, 8, 0x96, 12, 2, 3, 0x32, 0x80});
  const auto bends = events<PitchBendPerformanceEvent>(trill.tracks.front());
  expect(trill.diagnostics.empty() && std::ranges::any_of(bends, [](const auto* event) {
           return event->header.tick == 3 && event->semitones > 11.9;
         }),
         "trill should begin with the low-phase duration and then reach its high pitch");
}

}  // namespace

void runSoftCreatModuleTests() {
  layoutUsesLiveSongAndAuditedTables();
  versionedOpcodesRetainTheirRealOperandLengths();
  physicalEffectsAndSoftwareGainRender();
  gainHoldContinuesTheCurrentEnvelope();
  restsPreserveTheKeyedVoice();
  durationModesLegatoAndRepeatsAreStateful();
  perNoteVolumePrecedesLiteralDuration();
  pitchEffectsRetainPhysicalTiming();
}
