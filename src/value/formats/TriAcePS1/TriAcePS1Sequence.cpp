/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/TriAcePS1/TriAcePS1.h"

#include "value/base/LevelScale.h"
#include "value/sequence/CommandSourceMap.h"
#include "value/sequence/CompiledCommandRuntime.h"
#include "value/sequence/SequenceVm.h"
#include "value/synth/PsxSpu.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace vgmtrans::formats::triace_ps1 {

using namespace core;

namespace {

constexpr u32 kPpqn = 48;
constexpr u32 kMaxCommands = 262144;
constexpr u32 kSequenceHeaderSize = 0xd6;
constexpr PerformanceLaneId kHarmonyLane{1};
constexpr std::array<u8, 0x1f> kCommandSize{
    1, 3, 3, 4, 3, 3, 3, 3, 3, 3, 3, 0, 0, 1, 2, 2, 3, 3, 3, 3, 5, 3, 3, 6, 0, 3, 3, 7, 0, 0, 3,
};

[[nodiscard]] u32 totalPlays(u8 value) {
  return value == 0 ? 256 : value;
}

[[nodiscard]] double linearController(u8 value) {
  return LevelScale::linearFromLinear(value / 127.0);
}

[[nodiscard]] double panController(u8 value) {
  return std::min<u8>(value, 127) / 127.0;
}

[[nodiscard]] u8 combinedVoicePan(u8 channelPan, u8 intrinsicPan) {
  const int channel = std::min<u8>(channelPan, 127);
  const int intrinsic = std::min<u8>(intrinsicPan, 127);
  const int influence = std::min(channel, 128 - channel);
  return static_cast<u8>(channel + (intrinsic - 64) * influence / 64);
}

[[nodiscard]] u32 tempoMicros(double bpm) {
  return bpm <= 0.0 ? 0 : static_cast<u32>(std::lround(60000000.0 / bpm));
}

[[nodiscard]] std::vector<double> driverSine() {
  constexpr std::array<s16, 64> table{
      0,    25,   50,   74,   98,   121,  142,  162,  181,  198,  213,  226,  237,  245,  251,  255,
      256,  255,  251,  245,  237,  226,  213,  198,  181,  162,  142,  121,  98,   74,   50,   25,
      0,    -25,  -50,  -74,  -98,  -121, -142, -162, -181, -198, -213, -226, -237, -245, -251, -255,
      -256, -255, -251, -245, -237, -226, -213, -198, -181, -162, -142, -121, -98,  -74,  -50,  -25,
  };
  std::vector<double> samples;
  samples.reserve(table.size());
  for (const s16 value : table) {
    samples.push_back(value / 256.0);
  }
  return samples;
}

struct NoteEncoding {
  u8 duration = 0;
  u8 velocity = 0;
  bool durationImplied = false;
  bool velocityImplied = false;
};

struct RepeatTarget {
  Address destination;
  u32 patternIndex = 0;
};

struct TrackAnalysis {
  std::map<u32, NoteEncoding> notes;
  std::map<u32, RepeatTarget> repeatEnds;
};

[[nodiscard]] TrackAnalysis analyzeTrack(ByteReader reader, const TriAcePs1SequenceLayout& sequence,
                                         const TriAcePs1TrackLayout& track) {
  TrackAnalysis analysis;
  const u32 end = sequence.offset + sequence.length;
  std::optional<RepeatTarget> repeatStart;
  for (u32 patternIndex = 0; patternIndex < track.patternAddresses.size(); ++patternIndex) {
    const u32 pattern = track.patternAddresses[patternIndex];
    // Pattern end writes mode 4 in both retail drivers, making duration and
    // velocity explicit again before the next playlist entry is selected.
    u8 impliedDuration = 0;
    u8 impliedVelocity = 0;
    for (u32 offset = pattern; offset < end;) {
      const u8 opcode = reader.u8At(offset);
      if (opcode < 0x80) {
        const u32 size = 2 + (impliedDuration == 0 ? 1 : 0) + (impliedVelocity == 0 ? 1 : 0);
        if (!reader.has(offset, size) || offset + size > end) {
          break;
        }
        u32 operand = offset + 2;
        NoteEncoding encoding{
            .duration = impliedDuration,
            .velocity = impliedVelocity,
            .durationImplied = impliedDuration != 0,
            .velocityImplied = impliedVelocity != 0,
        };
        if (!encoding.durationImplied) {
          encoding.duration = reader.u8At(operand++);
        }
        if (!encoding.velocityImplied) {
          encoding.velocity = reader.u8At(operand);
        }
        analysis.notes.try_emplace(offset, encoding);
        offset += size;
        continue;
      }
      if (opcode > 0x9e || kCommandSize[opcode - 0x80] == 0 || offset + kCommandSize[opcode - 0x80] > end) {
        break;
      }
      const u32 size = kCommandSize[opcode - 0x80];
      if (opcode == 0x80) {
        break;
      }
      if (opcode == 0x8d) {
        repeatStart = RepeatTarget{.destination = Address{offset + size}, .patternIndex = patternIndex};
      } else if (opcode == 0x8e && repeatStart) {
        analysis.repeatEnds.try_emplace(offset, *repeatStart);
      } else if (opcode == 0x9e) {
        impliedDuration = reader.u8At(offset + 1);
        impliedVelocity = reader.u8At(offset + 2);
      }
      offset += size;
    }
  }
  return analysis;
}

struct ProgramState {
  explicit ProgramState(const TriAcePs1SequenceLayout& layout)
      : baseTempo(layout.tempo), timeSignatureNumerator(layout.timeSignatureNumerator),
        timeSignatureDenominator(layout.timeSignatureDenominator) {}

  void finalizePerformance(PerformanceSequence& performance) {
    const auto belongsToHarmony = [](const PerformanceEvent& event) {
      if (const auto* note = std::get_if<NotePerformanceEvent>(&event)) {
        return note->lane == kHarmonyLane;
      }
      if (const auto* pan = std::get_if<ChannelPanPerformanceEvent>(&event)) {
        return pan->lane == kHarmonyLane;
      }
      return false;
    };
    std::vector<PerformanceTrack> harmonyTracks;
    for (auto& source : performance.tracks) {
      const bool hasHarmony = std::ranges::any_of(source.events, [](const PerformanceEvent& event) {
        const auto* note = std::get_if<NotePerformanceEvent>(&event);
        return note != nullptr && note->lane == kHarmonyLane;
      });
      if (!hasHarmony) {
        std::erase_if(source.events, belongsToHarmony);
        continue;
      }

      PerformanceTrack harmony = source;
      std::erase_if(source.events, belongsToHarmony);
      std::erase_if(harmony.events, [&](const PerformanceEvent& event) {
        return (std::holds_alternative<NotePerformanceEvent>(event) ||
                std::holds_alternative<ChannelPanPerformanceEvent>(event)) &&
               !belongsToHarmony(event);
      });
      for (auto& event : harmony.events) {
        if (auto* note = std::get_if<NotePerformanceEvent>(&event)) {
          note->lane = PerformanceLaneId{0};
        } else if (auto* pan = std::get_if<ChannelPanPerformanceEvent>(&event)) {
          pan->lane = PerformanceLaneId{0};
        }
      }
      harmonyTracks.push_back(std::move(harmony));
    }

    for (auto& harmony : harmonyTracks) {
      harmony.id = TrackId{static_cast<u32>(performance.tracks.size())};
      for (auto& event : harmony.events) {
        std::visit([&](auto& typed) { typed.header.track = harmony.id; }, event);
      }
      for (auto& automation : harmony.automations) {
        automation.header.track = harmony.id;
      }
      performance.tracks.push_back(std::move(harmony));
    }
  }

  [[nodiscard]] s8 randomFineTune() {
    const u16 old = randomCurrent;
    randomCurrent = static_cast<u16>(randomPrevious + old + 0x3711);
    randomPrevious = old;
    return static_cast<s8>(randomCurrent % 7) - 3;
  }

  u8 baseTempo = 120;
  u8 timeSignatureNumerator = 4;
  u8 timeSignatureDenominator = 4;
  s16 reverbDepth = 0x3fff;
  bool invertReverbLeft = false;
  bool invertReverbRight = false;
  u16 randomPrevious = 0;
  u16 randomCurrent = 0;
};

struct HarmonyParameters {
  u8 delay = 0;
  s8 transpose = 0;
  s8 fine = 0;
  u8 volume = 127;
  u8 pan = 64;
};

struct Harmony {
  bool enabled = false;
  HarmonyParameters parameters;
  HarmonyParameters active;
};

struct ActiveVoice {
  PerformanceNoteId note;
  double key = 0.0;
  u64 endTick = 0;
  bool sustained = false;
};

struct TrackState {
  TrackState(const TrackProgram& program, const TriAcePs1SequenceLayout& layout) : slot(program.sourceTrackNumber) {
    const auto found =
        std::ranges::find_if(layout.tracks, [&](const TriAcePs1TrackLayout& track) { return track.slot == slot; });
    if (found != layout.tracks.end()) {
      for (const u32 address : found->patternAddresses) {
        patterns.push_back(Address{address});
      }
    }
  }

  u32 slot = 0;
  std::vector<Address> patterns;
  u32 patternIndex = 0;
  bool initialized = false;
  u8 bank = 0;
  u8 program = 0;
  u8 bendRange = 12;
  s8 manualVibratoDepth = 0;
  u8 vibratoDelay = 0;
  s8 automaticVibratoDepth = 0;
  u8 vibratoRate = 0;
  bool automaticVibrato = false;
  bool randomPitch = false;
  bool sustainDown = false;
  u8 channelPan = 64;
  Harmony harmony;
  std::vector<ActiveVoice> activeVoices;
};

struct Playback {
  TrackState& track;
  PerformanceEmitter& out;
  VmApi& vm;
  ProgramState& program;

  void beforeCommand() {
    if (track.initialized) {
      return;
    }
    track.initialized = true;
    selectInstrument(track.program, track.bank, false);
    if (track.slot == 0 && program.timeSignatureNumerator != 0 &&
        program.timeSignatureDenominator != 0) {
      out.timeSignature(program.timeSignatureNumerator, program.timeSignatureDenominator, kPpqn);
    }
  }

  [[nodiscard]] LfoPerformanceContext vibratoContext(bool automatic, LfoRestartMode restart) const {
    const s8 depth = automatic ? track.automaticVibratoDepth : track.manualVibratoDepth;
    LfoPerformanceContext context{
        .cyclesPerTick = track.vibratoRate / 64.0,
        .shape = LfoShape{.waveform = LfoWaveform::Sine, .samples = driverSine()},
        .initialPhaseCycles = depth < 0 ? 0.5 : 0.0,
        .noteRestartInitialPhaseCycles = depth < 0 ? 0.5 : 0.0,
        .sampleImmediatelyOnNote = false,
        .delayUpdateMode = automatic ? LfoDelayUpdateMode::FutureNotesOnly : LfoDelayUpdateMode::CurrentAndFutureNotes,
        .restartMode = restart,
        .phaseRunsAtZeroDepth = false,
    };
    if (automatic) {
      context.delayTicks = track.vibratoDelay;
      context.restartMode = LfoRestartMode::PhaseAndDelay;
    }
    return context;
  }

  [[nodiscard]] u32 automaticVibratoRampTicks() const {
    const int target = track.automaticVibratoDepth;
    if (target == 0) {
      return 0;
    }
    // The MIPS driver uses an arithmetic shift, then adds this signed amount
    // once per music tick. Some negative targets therefore never advance.
    const int shifted = target >= 0 ? target / 32 : -((-target + 31) / 32);
    const int step = 1 + shifted;
    if ((target > 0 && step <= 0) || (target < 0 && step >= 0)) {
      return 0;
    }
    return static_cast<u32>((std::abs(target) + std::abs(step) - 1) / std::abs(step));
  }

  void emitVibrato(bool automatic, LfoRestartMode restart) {
    const s8 rawDepth = automatic ? track.automaticVibratoDepth : track.manualVibratoDepth;
    const LfoPerformanceContext context = vibratoContext(automatic, restart);
    out.vibratoDepth(std::abs(static_cast<double>(rawDepth)), context);
    out.vibratoRateCyclesPerTick(track.vibratoRate / 64.0, context);
  }

  void beginAutomaticVibrato() {
    const LfoPerformanceContext context = vibratoContext(true, LfoRestartMode::PhaseAndDelay);
    out.vibratoDepth(0.0, context);
    out.vibratoRateCyclesPerTick(track.vibratoRate / 64.0, context);
    if (const u32 ramp = automaticVibratoRampTicks(); ramp != 0) {
      static_cast<void>(out.noteEnvelope(PerformanceAutomationTarget::VibratoDepth,
                                         std::abs(static_cast<double>(track.automaticVibratoDepth)), ramp,
                                         track.vibratoDelay));
    }
  }

  void selectInstrument(u8 programNumber, u8 bankNumber, bool restore) {
    track.program = programNumber;
    track.bank = bankNumber;
    out.instrument(triAcePs1InstrumentIdentity(bankNumber, programNumber));
    if (restore) {
      out.restoreEnvelope(EnvelopeFields::All, VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks);
    }
  }

  Effects note(u8 key, u8 delta, u8 duration, u8 velocity) {
    const s8 random = track.randomPitch ? program.randomFineTune() : 0;
    const double outputKey = key + random / 64.0;
    if (track.automaticVibrato) {
      beginAutomaticVibrato();
    }
    const u64 tick = vm.tick();
    std::erase_if(track.activeVoices,
                  [tick](const ActiveVoice& voice) { return !voice.sustained && voice.endTick <= tick; });
    NotePerformanceEvent event{
        .key = outputKey,
        .linearVelocity = linearController(velocity),
        .durationTicks = duration,
        .restartsVibratoLfoPhase = track.automaticVibrato,
    };
    const auto continued = std::ranges::find_if(
        track.activeVoices, [&](const ActiveVoice& voice) { return std::abs(voice.key - outputKey) < 0.000001; });
    if (continued == track.activeVoices.end()) {
      const PerformanceNoteId note = out.note(std::move(event));
      track.activeVoices.push_back(ActiveVoice{
          .note = note,
          .key = outputKey,
          .endTick = tick + duration,
          .sustained = track.sustainDown,
      });
    } else {
      event.note = continued->note;
      event.extendsPrevious = true;
      out.note(std::move(event));
      continued->endTick = tick + duration;
      continued->sustained = track.sustainDown;
    }
    if (track.harmony.enabled) {
      const HarmonyParameters& harmony = track.harmony.active;
      const s8 harmonyRandom = track.randomPitch ? program.randomFineTune() : 0;
      const double harmonyKey = key + harmony.transpose + (harmony.fine + harmonyRandom) / 64.0;
      out.at(tick + harmony.delay)
          .note(NotePerformanceEvent{
              .key = harmonyKey,
              .linearVelocity = linearController(velocity) * linearController(harmony.volume),
              .durationTicks = duration,
              .restartsVibratoLfoPhase = track.automaticVibrato,
              .lane = kHarmonyLane,
          });
    }
    return Effects::wait(delta);
  }

  Effects patternEnd() {
    ++track.patternIndex;
    if (track.patternIndex < track.patterns.size()) {
      return vm.jump(track.patterns[track.patternIndex]);
    }
    sustain(false);
    return vm.end();
  }

  Effects repeatEnd(u8 count, Address destination, u32 patternIndex) {
    // The byte counter produces 256 hardware plays for zero; both games use
    // that as a practical song loop, so leave its duration to VM loop policy.
    Effects effects = count == 0 ? vm.declaredLoop(destination) : vm.countedRepeatUntil(0, count, destination);

    if (effects.flowOverride) {
      track.patternIndex = patternIndex;
    }
    return effects;
  }

  void tempoModifier(s8 modifier) {
    const double scale = modifier < 0 ? 1.0 + modifier / 128.0 : 1.0 + modifier / 64.0;
    const double bpm = std::min(240.0, program.baseTempo * scale);
    if (const u32 micros = tempoMicros(bpm); micros != 0) {
      out.tempo(micros);
    }
  }

  void pitchBend(s8 value) { out.pitchBend(value * track.bendRange / 64.0); }

  void manualVibrato(s8 depth) {
    const bool restart = track.manualVibratoDepth == 0 && depth != 0;
    track.manualVibratoDepth = depth;
    if (!track.automaticVibrato) {
      emitVibrato(false, restart ? LfoRestartMode::Phase : LfoRestartMode::None);
    }
  }

  void sustain(bool enabled) {
    if (track.sustainDown == enabled) {
      return;
    }
    track.sustainDown = enabled;
    const u64 tick = vm.tick();
    if (enabled) {
      for (auto& voice : track.activeVoices) {
        if (voice.endTick > tick) {
          voice.sustained = true;
        }
      }
      return;
    }
    for (auto& voice : track.activeVoices) {
      if (voice.sustained && voice.endTick <= tick) {
        static_cast<void>(out.setNoteEnd(voice.note, tick));
      }
      voice.sustained = false;
    }
    std::erase_if(track.activeVoices, [tick](const ActiveVoice& voice) { return voice.endTick <= tick; });
  }

  void reverbDepth(u8 value) {
    program.reverbDepth = static_cast<s16>(static_cast<u16>(value) << 8);
    emitReverbDepth();
  }

  void emitReverbDepth() {
    const double depth = program.reverbDepth / 32767.0;
    out.reverb(ReverbPerformanceEvent{
        .send = std::abs(depth),
        .leftGain = program.invertReverbLeft ? -depth : depth,
        .rightGain = program.invertReverbRight ? -depth : depth,
    });
  }

  void reverbPhase(u8 mode) {
    program.invertReverbLeft = mode == 1 || mode == 3;
    program.invertReverbRight = mode == 2 || mode == 3;
    emitReverbDepth();
  }

  void voicePhase(u8 mode) {
    const bool invertLeft = mode == 1 || mode == 3;
    const bool invertRight = mode == 2 || mode == 3;
    out.stereoBalance(invertLeft ? -1.0 : 1.0, invertRight ? -1.0 : 1.0);
  }

  void automaticVibratoEnabled(bool enabled) {
    track.automaticVibrato = enabled;
    // The flag is copied only when a voice is allocated. Future notes publish
    // the automatic LFO; an already-sounding voice retains its prior mode.
  }

  void vibratoParameters(u8 delay, s8 depth, u8 rate) {
    track.vibratoDelay = delay;
    track.automaticVibratoDepth = depth;
    track.vibratoRate = rate;
    if (!track.automaticVibrato) {
      emitVibrato(false, LfoRestartMode::None);
    }
  }

  void fineTune(s8 value) { out.tuning(value * (100.0 / 64.0)); }

  void channelPan(u8 value) {
    track.channelPan = value;
    out.channelPan(panController(value));
    if (track.harmony.enabled) {
      const HarmonyParameters& harmony = track.harmony.active;
      out.at(vm.tick() + harmony.delay)
          .channelPan(ChannelPanPerformanceEvent{
              .position = panController(combinedVoicePan(value, harmony.pan)),
              .lane = kHarmonyLane,
          });
    }
  }

  void bendRange(u8 semitones) {
    track.bendRange = semitones;
    out.pitchBendRange(semitones);
  }

  void adsr(u16 adsr1, u16 adsr2) {
    out.replaceEnvelope(psxSpuEnvelope(adsr1, adsr2), VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks);
  }

  void randomPitch(bool enabled) { track.randomPitch = enabled; }

  void harmonyEnabled(bool enabled) {
    if (enabled && !track.harmony.enabled) {
      track.harmony.active = track.harmony.parameters;
      const HarmonyParameters& harmony = track.harmony.active;
      out.at(vm.tick() + harmony.delay)
          .channelPan(ChannelPanPerformanceEvent{
              .position = panController(combinedVoicePan(track.channelPan, harmony.pan)),
              .lane = kHarmonyLane,
          });
    }
    track.harmony.enabled = enabled;
  }

  void harmonyParameters(u8 delay, s8 transpose, s8 fine, u8 volume, u8 pan) {
    track.harmony.parameters = HarmonyParameters{
        .delay = delay,
        .transpose = transpose,
        .fine = fine,
        .volume = volume,
        .pan = pan,
    };
  }
};

using Cursor = CompilerCursor<TrackState, Playback>;

[[nodiscard]] DecodedBytecodeCommand decodeCommand(ByteReader reader, u32 begin, u32 end,
                                                   const TriAcePs1TrackLayout& layout, const TrackAnalysis& analysis,
                                                   std::vector<Diagnostic>* diagnostics) {
  Cursor cursor(reader, begin, end, kTriAcePs1CommandKindPrefix, diagnostics);
  if (!cursor.hasOpcode()) {
    return cursor.truncated();
  }
  const u8 opcode = cursor.opcode();
  if (opcode < 0x80) {
    const auto found = analysis.notes.find(begin);
    if (found == analysis.notes.end()) {
      return cursor.unsupported("Undecodable TriAcePS1 Note").stop();
    }
    auto event = cursor.command("Note", SequenceSemantic::Note);
    event.derived("key", opcode, SourceValueDisplay::MidiNote, SemanticOperandRole::NoteKey);
    const u8 delta = event.u8("delta", SemanticOperandRole::Duration);
    const u8 duration = found->second.durationImplied
                            ? event.derived("duration", found->second.duration, SemanticOperandRole::Duration)
                            : event.u8("duration", SemanticOperandRole::Duration);
    const u8 velocity = found->second.velocityImplied
                            ? event.derived("velocity", found->second.velocity, SemanticOperandRole::Level)
                            : event.u8("velocity", SemanticOperandRole::Level);
    return event.invoke<&Playback::note>(opcode, delta, duration, velocity);
  }
  if (opcode > 0x9e || kCommandSize[opcode - 0x80] == 0) {
    return cursor.unsupported("Undefined TriAcePS1 Event").stop();
  }

  switch (opcode) {
    case 0x80: {
      auto event = cursor.command("Pattern End", SequenceSemantic::End);
      for (const u32 pattern : layout.patternAddresses) {
        event.mayBranchTo(Address{pattern});
      }
      return event.invokeFlow<&Playback::patternEnd>().end();
    }
    case 0x81: {
      auto event = cursor.command("Voice Priority", SequenceSemantic::State);
      const u8 delta = event.u8("delta", SemanticOperandRole::Duration);
      event.u8("priority");
      return event.wait(delta);
    }
    case 0x82: {
      auto event = cursor.command("Tempo Modifier", SequenceSemantic::Tempo);
      const u8 delta = event.u8("delta", SemanticOperandRole::Duration);
      const s8 modifier = event.s8("modifier");
      return event.invoke<&Playback::tempoModifier>(modifier).wait(delta);
    }
    case 0x83: {
      auto event = cursor.command("Instrument", SequenceSemantic::Program);
      const u8 delta = event.u8("delta", SemanticOperandRole::Duration);
      const u8 program = event.u8("program", SemanticOperandRole::InstrumentProgram);
      const u8 bank = event.u8("bank", SemanticOperandRole::InstrumentBank);
      return event.invoke<&Playback::selectInstrument>(program, bank, true).wait(delta);
    }
    case 0x84: {
      auto event = cursor.command("Pitch Bend", SequenceSemantic::Pitch);
      const u8 delta = event.u8("delta", SemanticOperandRole::Duration);
      return event.invoke<&Playback::pitchBend>(event.s8("value", SemanticOperandRole::Pitch)).wait(delta);
    }
    case 0x85: {
      auto event = cursor.command("Volume", SequenceSemantic::Level);
      const u8 delta = event.u8("delta", SemanticOperandRole::Duration);
      const u8 value = event.u8("volume", SemanticOperandRole::Level);
      return event.emitLevel(linearController(value), ValueQuantization{.levels = 128}).wait(delta);
    }
    case 0x86: {
      auto event = cursor.command("Expression", SequenceSemantic::Level);
      const u8 delta = event.u8("delta", SemanticOperandRole::Duration);
      const u8 value = event.u8("expression", SemanticOperandRole::Level);
      return event.emitExpression(linearController(value)).wait(delta);
    }
    case 0x87: {
      auto event = cursor.command("Pan", SequenceSemantic::Pan);
      const u8 delta = event.u8("delta", SemanticOperandRole::Duration);
      const u8 value = event.u8("pan", SemanticOperandRole::Pan);
      return event.invoke<&Playback::channelPan>(value).wait(delta);
    }
    case 0x88: {
      auto event = cursor.command("Manual Vibrato Depth", SequenceSemantic::Modulation);
      const u8 delta = event.u8("delta", SemanticOperandRole::Duration);
      return event.invoke<&Playback::manualVibrato>(event.s8("depth", SemanticOperandRole::Modulation)).wait(delta);
    }
    case 0x89: {
      auto event = cursor.command("Sustain", SequenceSemantic::State);
      const u8 delta = event.u8("delta", SemanticOperandRole::Duration);
      return event.invoke<&Playback::sustain>(event.u8("enabled", SourceValueDisplay::Boolean) != 0).wait(delta);
    }
    case 0x8a: {
      auto event = cursor.command("Global Reverb Depth", SequenceSemantic::State);
      const u8 delta = event.u8("delta", SemanticOperandRole::Duration);
      return event.invoke<&Playback::reverbDepth>(event.u8("depth")).wait(delta);
    }
    case 0x8d:
      return cursor.command("Repeat Begin", SequenceSemantic::Repeat);
    case 0x8e: {
      auto event = cursor.command("Repeat End", SequenceSemantic::Repeat);
      const u8 count = event.u8("total_plays");
      const auto found = analysis.repeatEnds.find(begin);
      if (found == analysis.repeatEnds.end()) {
        return event.ignore();
      }
      event.derived("decoded_total_plays", totalPlays(count));
      event.derived("destination", found->second.destination, SourceValueDisplay::Address,
                    SemanticOperandRole::RepeatTarget);
      event.derived("playlist_index", found->second.patternIndex);
      event.mayBranchTo(found->second.destination);
      return event.invokeFlow<&Playback::repeatEnd>(count, found->second.destination, found->second.patternIndex);
    }
    case 0x8f: {
      auto event = cursor.command("Rest", SequenceSemantic::Rest);
      return event.wait(event.u8("delta", SemanticOperandRole::Duration));
    }
    case 0x90: {
      auto event = cursor.command("Reverb Send", SequenceSemantic::State);
      const u8 delta = event.u8("delta", SemanticOperandRole::Duration);
      return event.emitReverb(event.u8("enabled", SourceValueDisplay::Boolean) != 0 ? 1.0 : 0.0).wait(delta);
    }
    case 0x91: {
      auto event = cursor.command("Reverb Phase", SequenceSemantic::State);
      const u8 delta = event.u8("delta", SemanticOperandRole::Duration);
      return event.invoke<&Playback::reverbPhase>(event.u8("mode")).wait(delta);
    }
    case 0x92: {
      auto event = cursor.command("Voice Phase", SequenceSemantic::State);
      const u8 delta = event.u8("delta", SemanticOperandRole::Duration);
      return event.invoke<&Playback::voicePhase>(event.u8("mode")).wait(delta);
    }
    case 0x93: {
      auto event = cursor.command("Automatic Vibrato", SequenceSemantic::Modulation);
      const u8 delta = event.u8("delta", SemanticOperandRole::Duration);
      return event.invoke<&Playback::automaticVibratoEnabled>(event.u8("enabled", SourceValueDisplay::Boolean) != 0)
          .wait(delta);
    }
    case 0x94: {
      auto event = cursor.command("Vibrato Parameters", SequenceSemantic::Modulation);
      const u8 delta = event.u8("delta", SemanticOperandRole::Duration);
      const u8 delay = event.u8("delay", SemanticOperandRole::Duration);
      const s8 depth = event.s8("target_depth", SemanticOperandRole::Modulation);
      const u8 rate = event.u8("rate", SemanticOperandRole::Modulation);
      return event.invoke<&Playback::vibratoParameters>(delay, depth, rate).wait(delta);
    }
    case 0x95: {
      auto event = cursor.command("Fine Tune", SequenceSemantic::Pitch);
      const u8 delta = event.u8("delta", SemanticOperandRole::Duration);
      return event.invoke<&Playback::fineTune>(event.s8("value", SemanticOperandRole::Pitch)).wait(delta);
    }
    case 0x96: {
      auto event = cursor.command("Pitch Bend Range", SequenceSemantic::Pitch);
      const u8 delta = event.u8("delta", SemanticOperandRole::Duration);
      return event.invoke<&Playback::bendRange>(event.u8("semitones", SemanticOperandRole::Pitch)).wait(delta);
    }
    case 0x97: {
      auto event = cursor.command("Dynamic ADSR", SequenceSemantic::Envelope);
      const u8 delta = event.u8("delta", SemanticOperandRole::Duration);
      const u16 adsr1 = event.u16le("adsr1", SourceValueDisplay::Hex);
      const u16 adsr2 = event.u16le("adsr2", SourceValueDisplay::Hex);
      return event.invoke<&Playback::adsr>(adsr1, adsr2).wait(delta);
    }
    case 0x99: {
      auto event = cursor.command("Random Pitch", SequenceSemantic::Pitch);
      const u8 delta = event.u8("delta", SemanticOperandRole::Duration);
      return event.invoke<&Playback::randomPitch>(event.u8("enabled", SourceValueDisplay::Boolean) != 0).wait(delta);
    }
    case 0x9a: {
      auto event = cursor.command("Harmony Track", SequenceSemantic::State);
      const u8 delta = event.u8("delta", SemanticOperandRole::Duration);
      return event.invoke<&Playback::harmonyEnabled>(event.u8("enabled", SourceValueDisplay::Boolean) != 0).wait(delta);
    }
    case 0x9b: {
      auto event = cursor.command("Harmony Parameters", SequenceSemantic::State);
      const u8 delta = event.u8("delta", SemanticOperandRole::Duration);
      const u8 delay = event.u8("harmony_delay", SemanticOperandRole::Duration);
      const s8 transpose = event.s8("transpose", SemanticOperandRole::Pitch);
      const s8 fine = event.s8("fine_tune", SemanticOperandRole::Pitch);
      const u8 volume = event.u8("volume", SemanticOperandRole::Level);
      const u8 pan = event.u8("pan", SemanticOperandRole::Pan);
      return event.invoke<&Playback::harmonyParameters>(delay, transpose, fine, volume, pan).wait(delta);
    }
    case 0x9e: {
      auto event = cursor.command("Implied Note Parameters", SequenceSemantic::State);
      event.u8("duration", SemanticOperandRole::Duration);
      event.u8("velocity", SemanticOperandRole::Level);
      return event;
    }
    default:
      return cursor.unsupported("Undefined TriAcePS1 Event").stop();
  }
}

[[nodiscard]] TrackProgram decodeTrack(ByteReader reader, AssetId sequence, const TriAcePs1SequenceLayout& layout,
                                       const TriAcePs1TrackLayout& track, SourceMapBuilder* sourceMap,
                                       std::vector<Diagnostic>* diagnostics) {
  const TrackAnalysis analysis = analyzeTrack(reader, layout, track);
  const u32 end = layout.offset + layout.length;
  const TrackDecodeScope tracks{
      .reader = reader,
      .bytecodeEnd = end,
      .maxCommands = kMaxCommands,
      .sequenceAsset = sequence,
      .sourceMap = sourceMap,
  };
  return tracks.decode(track.slot, track.patternAddresses.front(),
                       [&](u32 offset) { return decodeCommand(reader, offset, end, track, analysis, diagnostics); });
}

}  // namespace

const SequenceProgramConfig& triAcePs1SequenceConfig() {
  static const SequenceProgramConfig config{
      .commandKindPrefix = std::string(kTriAcePs1CommandKindPrefix),
      .timebase = Timebase{.ppqn = kPpqn},
      .behavior =
          SequenceProgramBehavior{
              .commandLimit = kMaxCommands,
              .inferLoopsFromRepeatedState = false,
              .panLaw = PanLaw::ConstantSum,
              .initialLevel = 1.0,
              .initialExpression = 1.0,
              .initialReverbSend = 0.0,
              .initialChannelPan = 0.5,
              .initialStereoBalance = StereoBalance{},
              .initialPitchBendRangeSemitones = 12,
          },
  };
  return config;
}

SequenceProgram parseTriAcePs1Sequence(ByteReader reader, AssetId id, const TriAcePs1SequenceLayout& layout,
                                       SourceMapBuilder* sourceMap, std::vector<Diagnostic>* diagnostics) {
  SequenceProgram program = triAcePs1SequenceConfig().makeProgram();
  program.behavior.initialTempoMicrosecondsPerQuarter = tempoMicros(layout.tempo);
  program.runtime = makeCompiledRuntime<Cursor, ProgramState>(layout);

  if (sourceMap != nullptr) {
    sourceMap->header("TriAcePS1 Sequence Header", reader.range(layout.offset, kSequenceHeaderSize))
        .kind("triace-ps1-sequence-header")
        .owner(ObjectRefs::sequence(id))
        .field("size", reader.range(layout.offset + 2, 2), layout.length - 2)
        .field("tempo", reader.range(layout.offset + 0x0f, 1), layout.tempo, SourceValueDisplay::BeatsPerMinute)
        .field("time_signature_numerator", reader.range(layout.offset + 0x10, 1), layout.timeSignatureNumerator)
        .field("time_signature_denominator", reader.range(layout.offset + 0x11, 1), layout.timeSignatureDenominator);
    sourceMap->table("Track Records", reader.range(layout.offset + 0x16, 32 * 6))
        .kind("triace-ps1-track-records")
        .owner(ObjectRefs::sequence(id));
    for (const auto& track : layout.tracks) {
      sourceMap->table("Pattern Playlist", reader.range(track.playlistOffset, track.playlistLength))
          .kind("triace-ps1-pattern-playlist")
          .owner(ObjectRefs::sequenceTrack(id, track.slot));
    }
  }

  for (const auto& layoutTrack : layout.tracks) {
    auto track = decodeTrack(reader, id, layout, layoutTrack, sourceMap, diagnostics);
    track.sourceTrackNumber = layoutTrack.slot;
    program.tracks.push_back(std::move(track));
  }
  return program;
}

}  // namespace vgmtrans::formats::triace_ps1
