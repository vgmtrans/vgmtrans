/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/SuzukiPS1/SuzukiPS1.h"

#include "value/base/LevelScale.h"
#include "value/sequence/CommandSourceMap.h"
#include "value/sequence/CompiledCommandDialect.h"
#include "value/sequence/SequenceVm.h"
#include "value/synth/PsxSpu.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace vgmtrans::formats::suzuki_ps1 {

using namespace core;

namespace {

constexpr u32 kPpqn = 48;
constexpr u32 kMaxCommands = 262144;
constexpr std::array<u16, 19> kDuration{
    0, 192, 144, 96, 72, 64, 48, 36, 32, 24, 18, 16, 12, 9, 8, 6, 4, 3, 2,
};

// Total encoded sizes copied from the driver's table at SCUS_942.21:80028d0c.
// A zero marks an unassigned dispatch entry; it is not a zero-length command.
constexpr std::array<u8, 0x7f> kCommandSize{
    2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 2, 4, 1, 1, 1, 0, 0, 2, 1, 1, 3, 2, 1, 1, 0, 4, 4, 4, 0,
    2, 2, 3, 0, 2, 2, 2, 3, 0, 2, 2, 0, 2, 2, 1, 1, 1, 1, 1, 1, 2, 2, 1, 1, 4, 0, 1, 1, 0, 0, 0, 0,
    1, 4, 2, 2, 2, 2, 2, 3, 2, 2, 2, 0, 0, 0, 0, 0, 2, 2, 2, 3, 3, 1, 2, 2, 4, 4, 1, 1, 1, 0, 0, 0,
    2, 2, 3, 2, 4, 4, 1, 1, 2, 2, 3, 2, 4, 4, 1, 1, 4, 4, 3, 0, 0, 2, 2, 2, 0, 0, 0, 0, 0, 2, 2,
};

[[nodiscard]] u32 totalPlays(u8 encoded) {
  return encoded == 0 ? 256 : encoded;
}

[[nodiscard]] double linearController(u8 value) {
  return LevelScale::linearFromLinear(value / 127.0);
}

[[nodiscard]] double panPosition(u8 value) {
  return (std::min<u8>(value, 127) / 127.0) * 2.0 - 1.0;
}

[[nodiscard]] u32 tempoMicros(u8 raw) {
  if (raw == 0) {
    return 0;
  }
  const double bpm = raw * (75.0 / 64.0);
  return static_cast<u32>(std::lround(60000000.0 / bpm));
}

struct RepeatInfo {
  Address start;
  Address end;
  u32 plays = 1;
  u8 slot = 0;
};

struct TrackLayout {
  std::optional<Address> repeatPoint;
  std::map<u32, RepeatInfo> begin;
  std::map<u32, RepeatInfo> end;
  std::map<u32, RepeatInfo> breaks;
};

[[nodiscard]] u32 encodedSize(ByteReader reader, u32 offset, u32 end) {
  if (!reader.has(offset, 1) || offset >= end) {
    return 0;
  }
  const u8 status = reader.u8At(offset);
  if (status < 0x80) {
    if (!reader.has(offset + 1, 1) || offset + 2 > end) {
      return 0;
    }
    return reader.u8At(offset + 1) % kDuration.size() == 0 ? 3 : 2;
  }
  if (status > 0xfe) {
    return 0;
  }
  return kCommandSize[status - 0x80];
}

[[nodiscard]] TrackLayout analyzeTrack(ByteReader reader, u32 start, u32 end) {
  struct OpenRepeat {
    u32 command = 0;
    Address start;
    u32 plays = 1;
    u8 slot = 0;
    std::vector<u32> breaks;
  };

  TrackLayout layout;
  std::vector<OpenRepeat> stack;
  for (u32 offset = start; offset < end;) {
    const u32 size = encodedSize(reader, offset, end);
    if (size == 0 || offset + size > end) {
      break;
    }
    const u8 status = reader.u8At(offset);
    if (status == 0x91) {
      layout.repeatPoint = Address{offset + size};
    } else if (status == 0x98) {
      stack.push_back(OpenRepeat{
          .command = offset,
          .start = Address{offset + size},
          .plays = totalPlays(reader.u8At(offset + 1)),
          .slot = static_cast<u8>(std::min<std::size_t>(stack.size(), 15)),
      });
    } else if (status == 0x9a && !stack.empty()) {
      stack.back().breaks.push_back(offset);
    } else if (status == 0x99 && !stack.empty()) {
      OpenRepeat open = std::move(stack.back());
      stack.pop_back();
      const RepeatInfo info{
          .start = open.start,
          .end = Address{offset + size},
          .plays = open.plays,
          .slot = open.slot,
      };
      layout.begin.emplace(open.command, info);
      layout.end.emplace(offset, info);
      for (const u32 branch : open.breaks) {
        layout.breaks.emplace(branch, info);
      }
    }
    offset += size;
    if (status == 0x90) {
      break;
    }
  }
  return layout;
}

struct ProgramState {
  explicit ProgramState(const SequenceProgram& sequence) {
    for (std::size_t i = 0; i + 2 < sequence.config.driverData.size(); i += 3) {
      envelopes.emplace(sequence.config.driverData[i], std::pair{static_cast<u16>(sequence.config.driverData[i + 1]),
                                                                 static_cast<u16>(sequence.config.driverData[i + 2])});
    }
  }

  [[nodiscard]] std::optional<std::pair<u16, u16>> envelope(u16 bank, u8 program) const {
    const auto found = envelopes.find((static_cast<u32>(bank) << 8) | program);
    return found == envelopes.end() ? std::nullopt : std::optional{found->second};
  }

  std::map<u32, std::pair<u16, u16>> envelopes;
};

struct TrackState {
  explicit TrackState(const SequenceProgram& sequence) : bank(static_cast<u16>(sequence.config.driverState)) {}

  u8 octave = 3;
  u16 bank = 0;
  u8 program = 0;
  u8 volume = 127;
  u8 pan = 64;
  bool slur = false;
  bool initialized = false;
  bool hasEnvelope = false;
  u16 adsr1 = 0;
  u16 adsr2 = 0;
  std::optional<double> tieKey;
  double tieVelocity = 1.0;
  std::array<u8, 16> repeatOctave{};
  std::array<u8, 16> repeatEndOctave{};
  std::array<bool, 16> repeatEndKnown{};
  u8 repeatPointOctave = 3;
};

struct Playback {
  TrackState& track;
  PerformanceEmitter& out;
  VmApi& vm;
  ProgramState& programState;

  void beforeCommand() {
    if (track.initialized) {
      return;
    }
    track.initialized = true;
    loadEnvelope();
    emitInstrument();
  }

  void loadEnvelope() {
    const auto envelope = programState.envelope(track.bank, track.program);
    track.hasEnvelope = envelope.has_value();
    if (envelope) {
      track.adsr1 = envelope->first;
      track.adsr2 = envelope->second;
    }
  }

  void emitInstrument() {
    out.instrument(suzukiPs1InstrumentIdentity(track.bank, track.program),
                   InstrumentEnvelopeMode::UseInstrumentEnvelope);
  }

  void selectProgram(u8 program) {
    track.program = program;
    loadEnvelope();
    emitInstrument();
    out.restoreEnvelope(EnvelopeFields::All, VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks);
  }

  void setBank(u8 bank) { track.bank = bank; }

  void resetAdsr() {
    loadEnvelope();
    // C0 calls the same complete instrument loader as AC. This matters after
    // FE: the bank switch alone deliberately leaves the old program loaded.
    emitInstrument();
    out.restoreEnvelope(EnvelopeFields::All, VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks);
  }

  void publishAdsr() {
    if (track.hasEnvelope) {
      out.replaceEnvelope(psxSpuEnvelope(track.adsr1, track.adsr2), VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks);
    }
  }

  void adsrModes(u8 attack, u8 sustain, u8 release) {
    track.adsr1 = static_cast<u16>((track.adsr1 & ~0x8000u) | ((attack & 4) << 13));
    track.adsr2 = static_cast<u16>((track.adsr2 & ~0xc020u) | ((sustain & 4) << 13) | ((sustain & 2) << 13) |
                                   ((release & 4) << 3));
    publishAdsr();
  }

  void attackRate(u8 value) {
    track.adsr1 = static_cast<u16>((track.adsr1 & ~0x7f00u) | ((value & 0x7f) << 8));
    publishAdsr();
  }

  void decayRate(u8 value) {
    track.adsr1 = static_cast<u16>((track.adsr1 & ~0x00f0u) | ((value & 0x0f) << 4));
    publishAdsr();
  }

  void sustainRate(u8 value) {
    track.adsr2 = static_cast<u16>((track.adsr2 & ~0x1fc0u) | ((value & 0x7f) << 6));
    publishAdsr();
  }

  void releaseRate(u8 value) {
    track.adsr2 = static_cast<u16>((track.adsr2 & ~0x001fu) | (value & 0x1f));
    publishAdsr();
  }

  void sustainLevel(u8 value) {
    track.adsr1 = static_cast<u16>((track.adsr1 & ~0x000fu) | (value & 0x0f));
    publishAdsr();
  }

  void decayAndSustainLevel(u8 decay, u8 level) {
    track.adsr1 = static_cast<u16>((track.adsr1 & ~0x00ffu) | ((decay & 0x0f) << 4) | (level & 0x0f));
    publishAdsr();
  }

  void attackMode(u8 value) {
    track.adsr1 = static_cast<u16>((track.adsr1 & ~0x8000u) | ((value & 4) << 13));
    publishAdsr();
  }

  void sustainMode(u8 value) {
    track.adsr2 = static_cast<u16>((track.adsr2 & ~0xc000u) | ((value & 4) << 13) | ((value & 2) << 13));
    publishAdsr();
  }

  void releaseMode(u8 value) {
    track.adsr2 = static_cast<u16>((track.adsr2 & ~0x0020u) | ((value & 4) << 3));
    publishAdsr();
  }

  Effects note(u8 velocity, u8 scaleStep, u16 duration) {
    const double key = track.octave * 12.0 + scaleStep;
    track.tieVelocity = linearController(velocity);
    out.note(key, track.tieVelocity, duration);
    track.tieKey = key;
    return Effects::wait(duration);
  }

  Effects rest(u8 duration) {
    track.tieKey.reset();
    return Effects::wait(duration);
  }

  Effects tie(u8 duration) {
    if (track.tieKey) {
      out.note(*track.tieKey, track.tieVelocity, duration, true);
    }
    return Effects::wait(duration);
  }

  void timeSignature(u8 numerator, u8 denominator) {
    if (numerator != 0 && denominator != 0) {
      out.timeSignature(numerator, denominator, kPpqn);
    }
  }

  void tempo(u8 raw) {
    if (const u32 micros = tempoMicros(raw); micros != 0) {
      out.tempo(micros);
    }
  }

  void tempoSlide(u8 duration, u8 raw) {
    const u32 target = tempoMicros(raw);
    if (target != 0) {
      out.fade(PerformanceAutomationTarget::Tempo, target, duration).at(out, vm.tick() + duration).tempo(target);
    }
  }

  void volumeSlide(u8 duration, u8 target) {
    track.volume = target;
    out.fade(PerformanceAutomationTarget::Level, linearController(target), duration)
        .at(out, vm.tick() + duration)
        .level(linearController(target));
  }

  void panSlide(u8 duration, u8 target) {
    track.pan = target;
    out.fade(PerformanceAutomationTarget::Pan, panPosition(target), duration)
        .at(out, vm.tick() + duration)
        .pan(panPosition(target));
  }

  void beginRepeat(u8 slot) {
    track.repeatOctave[slot] = track.octave;
    track.repeatEndKnown[slot] = false;
  }

  Effects endRepeat(u8 slot, u32 plays, Address start) {
    if (!track.repeatEndKnown[slot]) {
      track.repeatEndKnown[slot] = true;
      track.repeatEndOctave[slot] = track.octave;
    }
    Effects effects = vm.countedRepeatUntil(slot, plays, start);
    if (effects.flowOverride) {
      track.octave = track.repeatOctave[slot];
    }
    return effects;
  }

  Effects repeatBreak(u8 slot, Address destination) {
    const BranchResult branch = vm.countedRepeatBreak(slot, destination);
    if (branch.taken && track.repeatEndKnown[slot]) {
      track.octave = track.repeatEndOctave[slot];
    }
    return branch.effects;
  }
};

using Cursor = CompilerCursor<TrackState, Playback>;

[[nodiscard]] DecodedBytecodeCommand decodeCommand(ByteReader reader, u32 begin, u32 end, const TrackLayout& layout,
                                                   std::vector<Diagnostic>* diagnostics) {
  Cursor cursor(reader, begin, end, kSuzukiPs1DialectId, diagnostics);
  if (!cursor.hasOpcode()) {
    return cursor.truncated();
  }
  const u8 status = cursor.opcode();
  if (status < 0x80) {
    auto event = cursor.command("Note", SequenceSemantic::Note);
    const u8 packed = event.u8("note_and_duration", SourceValueDisplay::Hex);
    const u8 scaleStep = event.derived("scale_step", static_cast<u8>(packed / 19), SourceValueDisplay::Default,
                                       SemanticOperandRole::NoteKey);
    const u8 index = packed % 19;
    const u16 duration = index == 0 ? event.u8("duration", SemanticOperandRole::Duration) : kDuration[index];
    event.derived("velocity", status, SemanticOperandRole::Level);
    return event.invoke<&Playback::note>(status, scaleStep, duration);
  }
  if (status > 0xfe || kCommandSize[status - 0x80] == 0) {
    return cursor.unsupported("Undefined SuzukiPS1 Event").stop();
  }

  switch (status) {
    case 0x80: {
      auto event = cursor.command("Rest", SequenceSemantic::Rest);
      return event.invoke<&Playback::rest>(event.u8("duration", SemanticOperandRole::Duration));
    }
    case 0x81: {
      auto event = cursor.command("Tie", SequenceSemantic::Note);
      return event.invoke<&Playback::tie>(event.u8("duration", SemanticOperandRole::Duration));
    }
    case 0x90: {
      auto event = cursor.command("End of Track", SequenceSemantic::End);
      if (!layout.repeatPoint) {
        return event.end();
      }
      return event.set<&TrackState::octave>(event.state<&TrackState::repeatPointOctave>())
          .loopCandidate(*layout.repeatPoint);
    }
    case 0x91: {
      auto event = cursor.command("Track Repeat Point", SequenceSemantic::Loop);
      return event.set<&TrackState::repeatPointOctave>(event.state<&TrackState::octave>());
    }
    case 0x94: {
      auto event = cursor.command("Set Octave", SequenceSemantic::Pitch);
      return event.set<&TrackState::octave>(event.u8("octave"));
    }
    case 0x95:
      return cursor.command("Octave Up", SequenceSemantic::Pitch).add<&TrackState::octave>(1);
    case 0x96:
      return cursor.command("Octave Down", SequenceSemantic::Pitch).add<&TrackState::octave>(-1);
    case 0x97: {
      auto event = cursor.command("Time Signature", SequenceSemantic::Meta);
      const u8 numerator = event.u8("numerator");
      const u8 denominator = event.u8("denominator");
      return event.invoke<&Playback::timeSignature>(numerator, denominator);
    }
    case 0x98: {
      auto event = cursor.command("Repeat Begin", SequenceSemantic::Repeat);
      const u8 rawCount = event.u8("count");
      const auto found = layout.begin.find(begin);
      if (found == layout.begin.end()) {
        return event.ignore();
      }
      event.derived("total_plays", totalPlays(rawCount));
      event.derived("destination", found->second.start, SourceValueDisplay::Address, SemanticOperandRole::RepeatTarget);
      return event.invoke<&Playback::beginRepeat>(found->second.slot);
    }
    case 0x99: {
      auto event = cursor.command("Repeat End", SequenceSemantic::Repeat);
      const auto found = layout.end.find(begin);
      if (found == layout.end.end()) {
        return event.ignore();
      }
      event.derived("destination", found->second.start, SourceValueDisplay::Address, SemanticOperandRole::RepeatTarget);
      event.mayBranchTo(found->second.start).runtimeControlFlow();
      return event.invoke<&Playback::endRepeat>(found->second.slot, found->second.plays, found->second.start);
    }
    case 0x9a: {
      auto event = cursor.command("Repeat Break", SequenceSemantic::RepeatBreak);
      const auto found = layout.breaks.find(begin);
      if (found == layout.breaks.end()) {
        return event.ignore();
      }
      event.derived("destination", found->second.end, SourceValueDisplay::Address, SemanticOperandRole::JumpTarget);
      event.mayBranchTo(found->second.end).runtimeControlFlow();
      return event.invoke<&Playback::repeatBreak>(found->second.slot, found->second.end);
    }
    case 0xa0: {
      auto event = cursor.command("Tempo", SequenceSemantic::Tempo);
      const u8 raw = event.u8("raw");
      event.derived("tempo", raw * (75.0 / 64.0), SourceValueDisplay::BeatsPerMinute);
      return event.invoke<&Playback::tempo>(raw);
    }
    case 0xa2: {
      auto event = cursor.command("Tempo Slide", SequenceSemantic::Tempo);
      const u8 duration = event.u8("duration", SemanticOperandRole::Duration);
      const u8 target = event.u8("target");
      event.derived("target_tempo", target * (75.0 / 64.0), SourceValueDisplay::BeatsPerMinute);
      return event.invoke<&Playback::tempoSlide>(duration, target);
    }
    case 0xac: {
      auto event = cursor.command("Program Change", SequenceSemantic::Program);
      return event.invoke<&Playback::selectProgram>(event.u8("program", SemanticOperandRole::InstrumentProgram));
    }
    case 0xae:
      return cursor.noOp("Percussion On");
    case 0xaf:
      return cursor.noOp("Percussion Off");
    case 0xb0:
      return cursor.command("Slur On", SequenceSemantic::State).set<&TrackState::slur>(true).emitLegatoPedal(true);
    case 0xb1:
      return cursor.command("Slur Off", SequenceSemantic::State).set<&TrackState::slur>(false).emitLegatoPedal(false);
    case 0xba:
      return cursor.command("Reverb On", SequenceSemantic::State).emitReverb(1.0);
    case 0xbb:
      return cursor.command("Reverb Off", SequenceSemantic::State).emitReverb(0.0);
    case 0xc0:
      return cursor.command("ADSR Reset", SequenceSemantic::Envelope).invoke<&Playback::resetAdsr>();
    case 0xc1: {
      auto event = cursor.command("ADSR Modes", SequenceSemantic::Envelope);
      const u8 attack = event.u8("attack_mode", SourceValueDisplay::Hex);
      const u8 sustain = event.u8("sustain_mode", SourceValueDisplay::Hex);
      const u8 release = event.u8("release_mode", SourceValueDisplay::Hex);
      return event.invoke<&Playback::adsrModes>(attack, sustain, release);
    }
    case 0xc2: {
      auto event = cursor.command("Attack Rate", SequenceSemantic::Envelope);
      return event.invoke<&Playback::attackRate>(event.u8("rate"));
    }
    case 0xc3: {
      auto event = cursor.command("Decay Rate", SequenceSemantic::Envelope);
      return event.invoke<&Playback::decayRate>(event.u8("rate"));
    }
    case 0xc4: {
      auto event = cursor.command("Sustain Rate", SequenceSemantic::Envelope);
      return event.invoke<&Playback::sustainRate>(event.u8("rate"));
    }
    case 0xc5: {
      auto event = cursor.command("Release Rate", SequenceSemantic::Envelope);
      return event.invoke<&Playback::releaseRate>(event.u8("rate"));
    }
    case 0xc6: {
      auto event = cursor.command("Sustain Level", SequenceSemantic::Envelope);
      return event.invoke<&Playback::sustainLevel>(event.u8("level"));
    }
    case 0xc7: {
      auto event = cursor.command("Decay Rate and Sustain Level", SequenceSemantic::Envelope);
      const u8 decay = event.u8("decay_rate");
      const u8 level = event.u8("sustain_level");
      return event.invoke<&Playback::decayAndSustainLevel>(decay, level);
    }
    case 0xc8: {
      auto event = cursor.command("Attack Mode", SequenceSemantic::Envelope);
      return event.invoke<&Playback::attackMode>(event.u8("mode", SourceValueDisplay::Hex));
    }
    case 0xc9: {
      auto event = cursor.command("Sustain Mode", SequenceSemantic::Envelope);
      return event.invoke<&Playback::sustainMode>(event.u8("mode", SourceValueDisplay::Hex));
    }
    case 0xca: {
      auto event = cursor.command("Release Mode", SequenceSemantic::Envelope);
      return event.invoke<&Playback::releaseMode>(event.u8("mode", SourceValueDisplay::Hex));
    }
    case 0xd0:
    case 0xd1:
    case 0xd2: {
      auto event = cursor.command("Pitch Parameter", SequenceSemantic::Pitch, CommandPlaybackStatus::SourceOnly);
      event.s8("value", SemanticOperandRole::Pitch);
      return event.ignore();
    }
    case 0xd4: {
      auto event = cursor.command("Portamento", SequenceSemantic::Portamento, CommandPlaybackStatus::SourceOnly);
      event.u8("duration", SemanticOperandRole::Duration);
      event.s8("depth", SemanticOperandRole::Pitch);
      return event.ignore();
    }
    case 0xd6: {
      auto event = cursor.command("Detune Parameter", SequenceSemantic::Pitch, CommandPlaybackStatus::SourceOnly);
      event.s8("value", SemanticOperandRole::Pitch);
      return event.ignore();
    }
    case 0xd7: {
      auto event =
          cursor.command("Pitch Modulation Depth", SequenceSemantic::Modulation, CommandPlaybackStatus::SourceOnly);
      event.u8("depth", SemanticOperandRole::Modulation);
      return event.ignore();
    }
    case 0xd8:
    case 0xd9: {
      auto event = cursor.command(status == 0xd8 ? "Pitch Modulation Shape" : "Pitch Modulation Envelope",
                                  SequenceSemantic::Modulation, CommandPlaybackStatus::SourceOnly);
      event.u8("parameter_1", SemanticOperandRole::Modulation);
      event.u8("parameter_2", SemanticOperandRole::Modulation);
      event.u8("parameter_3", SemanticOperandRole::Modulation);
      return event.ignore();
    }
    case 0xe0: {
      auto event = cursor.command("Volume", SequenceSemantic::Level);
      const u8 value = event.u8("volume", SemanticOperandRole::Level);
      return event.set<&TrackState::volume>(value).emitLevel(linearController(value));
    }
    case 0xe1: {
      auto event =
          cursor.command("Relative Volume Parameter", SequenceSemantic::Level, CommandPlaybackStatus::SourceOnly);
      event.s8("value", SemanticOperandRole::Level);
      return event.ignore();
    }
    case 0xe2: {
      auto event = cursor.command("Volume Slide", SequenceSemantic::Level);
      const u8 duration = event.u8("duration", SemanticOperandRole::Duration);
      const u8 target = event.u8("target", SemanticOperandRole::Level);
      return event.invoke<&Playback::volumeSlide>(duration, target);
    }
    case 0xe3: {
      auto event =
          cursor.command("Volume Modulation Depth", SequenceSemantic::Modulation, CommandPlaybackStatus::SourceOnly);
      event.u8("depth", SemanticOperandRole::Modulation);
      return event.ignore();
    }
    case 0xe4:
    case 0xe5: {
      auto event = cursor.command(status == 0xe4 ? "Volume Modulation Shape" : "Volume Modulation Envelope",
                                  SequenceSemantic::Modulation, CommandPlaybackStatus::SourceOnly);
      event.u8("parameter_1", SemanticOperandRole::Modulation);
      event.u8("parameter_2", SemanticOperandRole::Modulation);
      event.u8("parameter_3", SemanticOperandRole::Modulation);
      return event.ignore();
    }
    case 0xe8: {
      auto event = cursor.command("Pan", SequenceSemantic::Pan);
      const u8 value = event.u8("pan", SemanticOperandRole::Pan);
      return event.set<&TrackState::pan>(value).emitPan(panPosition(value));
    }
    case 0xe9: {
      auto event = cursor.command("Relative Pan Parameter", SequenceSemantic::Pan, CommandPlaybackStatus::SourceOnly);
      event.s8("value", SemanticOperandRole::Pan);
      return event.ignore();
    }
    case 0xea: {
      auto event = cursor.command("Pan Slide", SequenceSemantic::Pan);
      const u8 duration = event.u8("duration", SemanticOperandRole::Duration);
      const u8 target = event.u8("target", SemanticOperandRole::Pan);
      return event.invoke<&Playback::panSlide>(duration, target);
    }
    case 0xeb: {
      auto event =
          cursor.command("Pan Modulation Depth", SequenceSemantic::Modulation, CommandPlaybackStatus::SourceOnly);
      event.u8("depth", SemanticOperandRole::Modulation);
      return event.ignore();
    }
    case 0xec:
    case 0xed: {
      auto event = cursor.command(status == 0xec ? "Pan Modulation Shape" : "Pan Modulation Envelope",
                                  SequenceSemantic::Modulation, CommandPlaybackStatus::SourceOnly);
      event.u8("parameter_1", SemanticOperandRole::Modulation);
      event.u8("parameter_2", SemanticOperandRole::Modulation);
      event.u8("parameter_3", SemanticOperandRole::Modulation);
      return event.ignore();
    }
    case 0xfe: {
      auto event = cursor.command("WDS Bank", SequenceSemantic::Program);
      return event.invoke<&Playback::setBank>(event.u8("bank", SemanticOperandRole::InstrumentBank));
    }
    default:
      return cursor.ignored("Driver Command", kCommandSize[status - 0x80] - 1, "driver-command");
  }
}

[[nodiscard]] TrackProgram decodeTrack(ByteReader reader, AssetId sequence, u32 trackIndex, u32 start, u32 end,
                                       SourceMapBuilder* sourceMap, std::vector<Diagnostic>* diagnostics) {
  const TrackLayout layout = analyzeTrack(reader, start, end);
  const TrackDecodeScope tracks{
      .reader = reader,
      .bytecodeEnd = end,
      .maxCommands = kMaxCommands,
      .sequenceAsset = sequence,
      .sourceMap = sourceMap,
  };
  return tracks.reachable(trackIndex, start,
                          [&](u32 offset) { return decodeCommand(reader, offset, end, layout, diagnostics); });
}

}  // namespace

const SequenceDialect& suzukiPs1SequenceDialect() {
  static const SequenceDialect dialect = makeCompiledDialect<TrackState, Playback, ProgramState>(SequenceDialect{
      .id = DialectId{.value = std::string(kSuzukiPs1DialectId)},
      .commandDetailKindPrefix = std::string(kSuzukiPs1DialectId),
      .timebase = Timebase{.ppqn = kPpqn},
      .defaultBehavior =
          SequenceProgramBehavior{
              .defaultLoopPolicy = LoopPolicy::Default,
              .commandLimit = kMaxCommands,
              .panLaw = PanLaw::EqualPower,
              .initialLevel = 1.0,
          },
  });
  return dialect;
}

SequenceProgram parseSuzukiPs1Sequence(ByteReader reader, AssetId id, const SuzukiPs1SequenceLayout& layout,
                                       const std::vector<SuzukiPs1EnvelopeRegisters>& envelopes,
                                       SourceMapBuilder* sourceMap, std::vector<Diagnostic>* diagnostics) {
  SequenceProgram program = suzukiPs1SequenceDialect().makeProgram(Address{layout.offset});
  program.config.driverState = layout.defaultBank;
  program.config.driverData.reserve(envelopes.size() * 3);
  for (const auto& envelope : envelopes) {
    program.config.driverData.push_back((static_cast<u32>(envelope.bank) << 8) | envelope.program);
    program.config.driverData.push_back(envelope.adsr1);
    program.config.driverData.push_back(envelope.adsr2);
  }

  if (sourceMap != nullptr) {
    sourceMap->header("SuzukiPS1 Sequence Header", reader.range(layout.offset, 0x22))
        .kind("suzuki-ps1-sequence-header")
        .owner(ObjectRefs::sequence(id))
        .field("size", reader.range(layout.offset + 0x08, 2), layout.length)
        .field("track_count", reader.range(layout.offset + 0x14, 1), layout.trackCount)
        .field("percussion_count", reader.range(layout.offset + 0x15, 1), layout.percussionCount)
        .field("default_bank", reader.range(layout.offset + 0x16, 2), layout.defaultBank)
        .field("title_offset", reader.range(layout.offset + 0x1e, 2), layout.titleOffset, SourceValueDisplay::Address)
        .field("percussion_offset", reader.range(layout.offset + 0x20, 2), layout.percussionOffset,
               SourceValueDisplay::Address);
    sourceMap->table("Track Pointers", reader.range(layout.offset + 0x22, layout.trackCount * 2))
        .kind("suzuki-ps1-track-pointers")
        .owner(ObjectRefs::sequence(id));
  }

  const u32 end = layout.offset + layout.length;
  for (u32 i = 0; i < layout.trackAddresses.size(); ++i) {
    auto track = decodeTrack(reader, id, i, layout.trackAddresses[i], end, sourceMap, diagnostics);
    track.sourceTrackNumber = i;
    program.tracks.push_back(std::move(track));
  }
  return program;
}

}  // namespace vgmtrans::formats::suzuki_ps1
