/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/FalcomSnes/FalcomSnes.h"

#include "value/sequence/CommandSourceMap.h"
#include "value/sequence/CompiledCommandRuntime.h"
#include "value/synth/SnesDsp.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace vgmtrans::formats::falcom_snes {

using namespace core;

namespace {

constexpr u32 kCommandLimit = 32768;
constexpr u8 kDefaultTempo = 0x78;

namespace math {

[[nodiscard]] u32 tempoMicroseconds(u8 tempo) {
  // Timer 0 runs every 125 us with target $25; a sequence tick is one 8-bit
  // tempo overflow. PPQN is 48.
  return tempo == 0 ? 60'000'000 : static_cast<u32>(kPpqn) * 125u * 0x25u * 256u / tempo;
}

[[nodiscard]] u32 soundingTicks(u8 length, u8 quantize, bool slurred) {
  return slurred || quantize == 0 ? length : length - (static_cast<u32>(length) * quantize >> 8);
}

[[nodiscard]] double level(u8 raw) { return std::min<u8>(raw, 0x7f) / 127.0; }

// Ys V's table stays at $40 through index $3f, then falls linearly to zero.
[[nodiscard]] constexpr u8 panGain(u8 index) { return index < 0x40 ? 0x40 : 0x7f - index; }

[[nodiscard]] StereoBalance pan(u8 raw) {
  raw &= 0x7f;
  return {
      .leftGain = panGain(raw) / 127.0,
      .rightGain = panGain(static_cast<u8>((0x80 - raw) & 0x7f)) / 127.0,
  };
}

[[nodiscard]] double panPosition(u8 raw) {
  const StereoBalance balance = pan(raw);
  const double total = balance.leftGain + balance.rightGain;
  return total == 0.0 ? 0.0 : (balance.rightGain - balance.leftGain) / total;
}

void stepPan(u8& current, bool& descending, u8 low, u8 high, u8 step) {
  int next = current + (descending ? -static_cast<int>(step) : static_cast<int>(step));
  if (!descending && next >= high) {
    next = high;
    descending = true;
  } else if (descending && next <= low) {
    next = low;
    descending = false;
  }
  current = static_cast<u8>(next);
}

[[nodiscard]] double tuningSemitones(u8 rawKey, u16 pitchScale, s8 offset) {
  if (offset == 0 || pitchScale == 0) {
    return 0.0;
  }
  // EC is added to the final signed 16-bit DSP pitch register, not to the
  // musical key. Reconstruct that register from the audited note/root mapping.
  constexpr double kPitchTableC6 = 0x10be / 4096.0;
  const double unity = 96.0 - std::log2((pitchScale / 256.0) * kPitchTableC6) * 12.0;
  const double pitch = 4096.0 * std::exp2(((rawKey + 24.0) - unity) / 12.0);
  if (pitch + offset <= 0.0) {
    return 0.0;
  }
  return 12.0 * std::log2((pitch + offset) / pitch);
}

[[nodiscard]] double vibratoDepth(u8 depth) {
  // MUL keeps its high byte, so even the peak phase reaches depth-1.
  return depth == 0 ? 0.0 : (depth - 1) / 64.0;
}

[[nodiscard]] LfoShape vibratoShape(u8 depth) {
  LfoShape result{.waveform = LfoWaveform::Triangle};
  if (depth <= 1) {
    return result;
  }
  result.samples.reserve(256);
  const double maximum = depth - 1;
  for (u32 phase = 0; phase < 256; ++phase) {
    u8 triangle = static_cast<u8>(phase * 4u);
    if ((phase & 0x40) != 0) {
      triangle ^= 0xff;
    }
    const double value = ((static_cast<u32>(triangle) * depth) >> 8) / maximum;
    result.samples.push_back((phase & 0x80) != 0 ? -value : value);
  }
  return result;
}

[[nodiscard]] LfoShape panShape(u8 start, u8 target, u8 step, u16 interval) {
  LfoShape result{.waveform = LfoWaveform::Triangle};
  const int distance = std::abs(static_cast<int>(target) - start);
  if (distance == 0 || step == 0) {
    return result;
  }
  const u32 steps = (distance + step - 1) / step;
  const u32 ticks = std::max<u32>(2, 2 * steps * interval);
  result.samples.reserve(ticks);
  const u8 low = std::min(start, target);
  const u8 high = std::max(start, target);
  u8 current = start;
  bool descending = target < start;
  const double origin = panPosition(start);
  const double excursion = std::abs(panPosition(target) - origin);
  for (u32 tick = 0; tick < ticks; ++tick) {
    result.samples.push_back(excursion == 0.0 ? 0.0 : (panPosition(current) - origin) / excursion);
    if ((tick + 1) % interval != 0) {
      continue;
    }
    stepPan(current, descending, low, high, step);
  }
  return result;
}

}  // namespace math

struct PatchState {
  u8 adsr1 = 0;
  u8 adsr2 = 0;
  u16 pitchScale = 0x100;
};

[[nodiscard]] constexpr u32 packPatch(u8 adsr1, u8 adsr2, u16 pitchScale) {
  return adsr1 | (static_cast<u32>(adsr2) << 8) | (static_cast<u32>(pitchScale) << 16);
}

[[nodiscard]] PatchState patch(std::span<const u32> patches, u8 program) {
  if (program >= patches.size()) {
    return {};
  }
  const u32 raw = patches[program];
  return PatchState{
      .adsr1 = static_cast<u8>(raw),
      .adsr2 = static_cast<u8>(raw >> 8),
      .pitchScale = static_cast<u16>(raw >> 16),
  };
}

struct ProgramState {
  ReverbPerformanceEvent echo{.voiceMask = u8{0}, .send = 0.0, .leftGain = 0.0, .rightGain = 0.0};
  s8 storedEchoLeft = 0;
  s8 storedEchoRight = 0;
  s8 storedFeedback = 0;
  std::set<u8> overwrittenFirPresets;
};

struct VibratoState {
  u8 depth = 0;
  s8 rate = 0;
  bool enabled = false;
};

struct PitchEnvelopeState {
  u8 delay = 0;
  u8 depth = 0;
  s8 interval = 0;
  u8 counter = 0;
  double offset = 0.0;
  bool enabled = false;
};

struct PanLfoState {
  u8 low = 0x40;
  u8 high = 0x40;
  u8 step = 0;
  u8 interval = 0;
  u8 counter = 0;
  bool descending = false;
  bool enabled = false;
};

struct RuntimeConfig {
  std::vector<u32> patches;
};

struct TrackState {
  TrackState(const TrackProgram& source, const RuntimeConfig& config)
      : patches(config.patches), voiceBit(static_cast<u8>(1u << std::min(source.sourceTrackNumber, u32{7}))) {}

  std::span<const u32> patches;
  u8 voiceBit;
  u8 octave = 0;
  u8 quantize = 0;
  u8 volume = 0;
  u8 pan = 0x40;
  u8 program = 0;
  u8 adsr1 = 0;
  u8 adsr2 = 0;
  u16 pitchScale = 0x100;
  s8 tuning = 0;
  bool echoEnabled = false;
  bool startupLatch = true;
  bool previousSlurred = false;
  std::optional<u8> previousKey;
  PerformanceNoteId lastNote;
  VibratoState vibrato;
  PitchEnvelopeState pitchEnvelope;
  PanLfoState panLfo;
  std::map<u16, u8> repeatCells;
};

struct Playback {
  TrackState& track;
  PerformanceEmitter& out;
  VmApi& vm;
  ProgramState& program;

  void emitMix() const {
    out.level(math::level(track.volume), ValueQuantization{.levels = 128});
    emitPan();
  }

  void emitPan() const {
    const StereoBalance balance = math::pan(track.pan);
    out.stereoBalance(balance.leftGain, balance.rightGain);
  }

  void updateEchoVoice() {
    const u8 mask = program.echo.voiceMask.value_or(0);
    const u8 next = track.echoEnabled ? static_cast<u8>(mask | track.voiceBit)
                                      : static_cast<u8>(mask & ~track.voiceBit);
    if (next != mask) {
      program.echo.voiceMask = next;
      out.reverb(program.echo);
    }
  }

  [[nodiscard]] Effects note(u8 rawKey, u8 length, bool slurred, bool rest) {
    if (rest) {
      track.previousSlurred = false;
      track.previousKey.reset();
      track.lastNote = {};
      return Effects::wait(length);
    }

    const u8 driverKey = static_cast<u8>(rawKey + track.octave * 12u);
    const bool continues = track.previousSlurred && track.previousKey == driverKey && track.lastNote.valid();
    const u32 duration = math::soundingTicks(length, track.quantize, slurred);
    const double key = driverKey + 24.0 + math::tuningSemitones(driverKey, track.pitchScale, track.tuning);
    NotePerformanceEvent event{
        .key = key,
        .linearVelocity = 1.0,
        .durationTicks = duration,
        .extendsPrevious = continues,
        .restartsEnvelope = !continues,
        .instrumentAddress = InstrumentAddress{.bank = 0, .program = track.program},
        // Falcom restarts vibrato on attacks, while its independent pan LFO
        // continues across notes.
        .restartsLfoPhase = false,
        .restartsVibratoLfoPhase = !continues,
    };
    if (continues) {
      static_cast<void>(out.setNoteEnd(track.lastNote, vm.tick()));
      track.lastNote = out.note(std::move(event));
    } else {
      emitMix();
      updateEchoVoice();
      if (track.pitchEnvelope.offset != 0.0) {
        out.pitchBend(0.0);
      }
      track.pitchEnvelope.offset = 0.0;
      track.pitchEnvelope.counter = track.pitchEnvelope.delay;
      track.lastNote = out.note(std::move(event));
    }
    track.previousKey = driverKey;
    track.previousSlurred = slurred;
    return Effects::wait(length);
  }

  [[nodiscard]] Effects programChange(u8 value) {
    track.program = value;
    const PatchState selected = patch(track.patches, value);
    track.adsr1 = selected.adsr1;
    track.adsr2 = selected.adsr2;
    track.pitchScale = selected.pitchScale;
    out.instrument(InstrumentIdentity{.domain = std::string(kInstrumentDomain), .key = value});
    out.restoreEnvelope(EnvelopeFields::All, VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks);

    return finishPatchUpdate();
  }

  [[nodiscard]] Effects finishPatchUpdate() {
    // D8 and F2 share this tail in the driver. Its one-shot startup latch
    // yields for three ticks before parsing another command.
    if (track.startupLatch) {
      track.startupLatch = false;
      return Effects::wait(3);
    }
    return {};
  }

  void emitVibrato(LfoRestartMode restart, std::optional<u32> delay = std::nullopt) const {
    const double phase = track.vibrato.rate < 0 ? 0.5 : 0.0;
    const double cycles = track.vibrato.enabled ? std::abs(static_cast<int>(track.vibrato.rate)) / 256.0 : 0.0;
    LfoPerformanceContext context{
        .cyclesPerTick = cycles,
        .delayTicks = delay,
        .delayIsTempoRelative = delay.has_value(),
        .shape = math::vibratoShape(track.vibrato.depth),
        .initialPhaseCycles = phase,
        .noteRestartInitialPhaseCycles = phase,
        .sampleImmediatelyOnNote = true,
        .restartMode = restart,
        .phaseRunsAtZeroDepth = false,
        .zeroDepthBehavior = LfoZeroDepthBehavior::HoldOutputUntilNextNote,
    };
    const double depth = track.vibrato.enabled ? math::vibratoDepth(track.vibrato.depth) : 0.0;
    out.vibratoDepth(depth, context);
    out.vibratoRateCyclesPerTick(cycles, context);
  }

  void vibrato(u8 delay, u8 depth, s8 rate) {
    track.vibrato = {.depth = depth, .rate = rate, .enabled = rate != 0};
    emitVibrato(LfoRestartMode::PhaseAndDelay, static_cast<u32>(delay) + 1);
  }

  void vibratoEnabled(bool enabled) {
    track.vibrato.enabled = enabled && track.vibrato.rate != 0;
    emitVibrato(LfoRestartMode::None);
  }

  void adjustVolume(int amount) {
    track.volume = static_cast<u8>(std::clamp<int>(track.volume + amount, 0, 0x7f));
    out.level(math::level(track.volume), ValueQuantization{.levels = 128});
  }

  void setPan(u8 value) {
    if (track.panLfo.enabled) {
      panLfoEnabled(false);
    }
    track.pan = value;
  }

  void adjustPan(int amount) {
    track.pan = static_cast<u8>(std::clamp<int>(track.pan + amount, 0, 0x7f));
    emitPan();
  }

  void emitPanLfo(u8 start, u8 target, u8 step, u8 interval) const {
    const u16 ticksPerStep = interval == 0 ? 256 : interval;
    const u32 steps = step == 0 ? 0 : (std::abs(static_cast<int>(target) - start) + step - 1) / step;
    const double cycles = steps == 0 ? 0.0 : 1.0 / (2.0 * steps * ticksPerStep);
    const double depth = std::abs(math::panPosition(target) - math::panPosition(start));
    LfoPerformanceContext context{
        .cyclesPerTick = cycles,
        .shape = math::panShape(start, target, step, ticksPerStep),
        .initialPhaseCycles = 0.0,
        .sampleImmediatelyOnNote = true,
        .restartMode = LfoRestartMode::PhaseAndDelay,
    };
    out.panLfoDepth(depth, context);
    out.panLfoRateCyclesPerTick(cycles, context);
  }

  void configurePanLfo(u8 target, u8 step, u8 interval) {
    const u8 start = track.pan;
    track.panLfo = {
        .low = std::min(start, target),
        .high = std::max(start, target),
        .step = step,
        .interval = interval,
        .counter = interval,
        .descending = target < start,
        .enabled = step != 0,
    };
    emitPanLfo(start, target, step, interval);
  }

  void panLfoEnabled(bool enabled) {
    track.panLfo.enabled = enabled && track.panLfo.step != 0;
    if (track.panLfo.enabled) {
      const u8 target = track.panLfo.descending ? track.panLfo.low : track.panLfo.high;
      emitPanLfo(track.pan, target, track.panLfo.step, track.panLfo.interval);
    } else {
      emitPan();
      out.panLfoDepth(0.0, LfoPerformanceContext{.restartMode = LfoRestartMode::None});
      out.panLfoRateCyclesPerTick(0.0, LfoPerformanceContext{.restartMode = LfoRestartMode::None});
    }
  }

  void pitchEnvelope(u8 delay, u8 depth, s8 interval) {
    track.pitchEnvelope.delay = delay;
    track.pitchEnvelope.depth = depth;
    track.pitchEnvelope.interval = interval;
    track.pitchEnvelope.counter = delay;
    track.pitchEnvelope.enabled = depth != 0;
  }

  void pitchEnvelopeEnabled(bool enabled) {
    track.pitchEnvelope.enabled = enabled && track.pitchEnvelope.depth != 0;
  }

  [[nodiscard]] Effects adsr(u8 adsr1, u8 adsr2) {
    track.adsr1 = adsr1;
    track.adsr2 = adsr2;
    out.replaceEnvelope(snesDspEnvelope(static_cast<u8>(adsr1 | 0x80), adsr2, 0),
                        VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks);
    return finishPatchUpdate();
  }

  void brokenGain(u8) const {
    // The handler clears ADSR1 bit 4 instead of bit 7. GAIN remains inactive,
    // but the currently sounding voice does receive the altered decay rate.
    const u8 adsr1 = static_cast<u8>((track.adsr1 & 0xef) | 0x80);
    const Envelope envelope = snesDspEnvelope(adsr1, track.adsr2, 0);
    out.updateEnvelope(envelope, EnvelopeFields::Decay, VoiceEnvelopeScope::ActiveVoices);
  }

  void echoParameters(u8 delay, s8 feedback, u8 filter) {
    track.echoEnabled = true;
    program.storedFeedback = feedback;
    program.echo.delayMilliseconds = std::min<u8>(delay, 7) * 16.0;
    program.echo.feedback = feedback / 128.0;
    program.echo.filterIndex = program.overwrittenFirPresets.contains(filter) ? std::nullopt : std::optional{filter};
    out.reverb(program.echo);
  }

  void overwriteFirPreset(u8 preset) { program.overwrittenFirPresets.insert(preset); }

  void emitEchoVolume(s8 left, s8 right) const {
    program.echo.leftGain = left / 128.0;
    program.echo.rightGain = right / 128.0;
    program.echo.send = std::min(1.0, std::max(std::abs(left), std::abs(right)) / 127.0);
    out.reverb(program.echo);
  }

  void echoVolumeEnabled(bool enabled) {
    if (enabled) {
      program.echo.feedback = program.storedFeedback / 128.0;
    }
    emitEchoVolume(enabled ? program.storedEchoLeft : 0, enabled ? program.storedEchoRight : 0);
  }

  void echoVolume(s8 left, s8 right) {
    program.storedEchoLeft = left;
    program.storedEchoRight = right;
    emitEchoVolume(left, right);
  }

  void repeatStart(Address cell, u8 count) { track.repeatCells[static_cast<u16>(cell.value)] = count; }

  [[nodiscard]] u8* repeatCounter(Address cell) {
    const auto found = track.repeatCells.find(static_cast<u16>(cell.value));
    return found == track.repeatCells.end() ? nullptr : &found->second;
  }

  [[nodiscard]] Effects repeatBreak(Address cell, Address destination) {
    const u8* count = repeatCounter(cell);
    return count != nullptr && *count == 1 ? vm.finiteBranch(destination) : Effects{};
  }

  [[nodiscard]] Effects repeatEnd(Address cell, Address destination) {
    u8* count = repeatCounter(cell);
    if (count == nullptr) {
      return {};
    }
    return --*count != 0 ? vm.finiteBranch(destination) : Effects{};
  }

  void tickPanLfo() {
    PanLfoState& lfo = track.panLfo;
    if (!lfo.enabled || --lfo.counter != 0) {
      return;
    }
    math::stepPan(track.pan, lfo.descending, lfo.low, lfo.high, lfo.step);
    lfo.counter = lfo.interval;
  }

  void tickPitchEnvelope() {
    PitchEnvelopeState& envelope = track.pitchEnvelope;
    if (!envelope.enabled) {
      return;
    }
    if (envelope.counter != 0) {
      --envelope.counter;
      return;
    }
    envelope.offset += (envelope.interval < 0 ? -1.0 : 1.0) * envelope.depth / 256.0;
    out.pitchBend(envelope.offset);
    envelope.counter = static_cast<u8>(std::abs(static_cast<int>(envelope.interval)));
    --envelope.counter;
  }

  void tick() {
    tickPitchEnvelope();
    tickPanLfo();
  }
};

using Cursor = CompilerCursor<TrackState, Playback>;

[[nodiscard]] Address relativeTarget(u32 continuation, s16 relative) {
  return Address{static_cast<u16>(continuation + relative)};
}

[[nodiscard]] DecodedBytecodeCommand decodeCommand(ByteReader reader, u32 begin, std::span<const u8, 7> durations,
                                                   std::vector<Diagnostic>* diagnostics,
                                                   std::set<u8>* programs = nullptr) {
  Cursor cursor(reader, begin, "falcom-snes", diagnostics);
  if (!cursor.hasOpcode()) {
    return cursor.truncated();
  }
  const u8 opcode = cursor.opcode();
  if (opcode < 0xd0) {
    const u8 lengthIndex = opcode & 7;
    auto event = cursor.command((opcode >> 4) == 12 ? "Rest" : "Note",
                                (opcode >> 4) == 12 ? SequenceSemantic::Rest : SequenceSemantic::Note);
    const u8 rawKey = event.opcodeValue("key", static_cast<u8>(opcode >> 4), SourceValueDisplay::Default,
                                        SemanticOperandRole::NoteKey);
    const bool slurred = event.opcodeValue("slurred", (opcode & 8) != 0);
    const u8 length = lengthIndex == 0 ? event.u8("length", SemanticOperandRole::Duration)
                                       : event.derived("length", durations[lengthIndex - 1],
                                                       SemanticOperandRole::Duration);
    return event.invoke<&Playback::note>(rawKey, length, slurred, rawKey == 12);
  }
  if (opcode <= 0xd6) {
    auto event = cursor.command("Octave", SequenceSemantic::Pitch);
    const u8 octave = event.opcodeValue("octave", static_cast<u8>(opcode - 0xd0));
    return event.set<&TrackState::octave>(octave);
  }

  switch (opcode) {
    case 0xd7: {
      auto event = cursor.command("Tempo", SequenceSemantic::Tempo);
      return event.emitTempo(math::tempoMicroseconds(event.u8("tempo")));
    }
    case 0xd8: {
      auto event = cursor.command("Program Change", SequenceSemantic::Program);
      const u8 value = event.u8("program", SemanticOperandRole::InstrumentProgram);
      if (programs != nullptr) {
        programs->insert(value);
      }
      return event.invoke<&Playback::programChange>(value);
    }
    case 0xd9: {
      auto event = cursor.command("Vibrato", SequenceSemantic::Modulation);
      const u8 delay = event.u8("delay", SemanticOperandRole::Duration);
      const u8 depth = event.u8("depth", SemanticOperandRole::Modulation);
      return event.invoke<&Playback::vibrato>(delay, depth, event.s8("rate", SemanticOperandRole::Modulation));
    }
    case 0xda: {
      auto event = cursor.command("Vibrato On/Off", SequenceSemantic::Modulation);
      return event.invoke<&Playback::vibratoEnabled>(event.u8("enabled") != 0);
    }
    case 0xdb:
      return cursor.ignored("No Operation", 3, "nop");
    case 0xdc:
      return cursor.ignored("No Operation", 1, "nop");
    case 0xdd: {
      auto event = cursor.command("Quantize", SequenceSemantic::State);
      return event.set<&TrackState::quantize>(event.u8("keyoff_remainder"));
    }
    case 0xde: {
      auto event = cursor.command("Volume", SequenceSemantic::Level);
      return event.set<&TrackState::volume>(event.u8("volume", SemanticOperandRole::Level));
    }
    case 0xdf:
    case 0xe0:
    case 0xe1:
    case 0xe2: {
      constexpr std::array<int, 4> amounts{-8, -1, -2, -4};
      auto event = cursor.command("Volume Decrease", SequenceSemantic::Level);
      const int amount = event.derived("delta", amounts[opcode - 0xdf]);
      return event.invoke<&Playback::adjustVolume>(amount);
    }
    case 0xe3:
    case 0xe4:
    case 0xe5:
    case 0xe6: {
      constexpr std::array<int, 4> amounts{8, 1, 2, 4};
      auto event = cursor.command("Volume Increase", SequenceSemantic::Level);
      const int amount = event.derived("delta", amounts[opcode - 0xe3]);
      return event.invoke<&Playback::adjustVolume>(amount);
    }
    case 0xe7: {
      auto event = cursor.command("Pan", SequenceSemantic::Pan);
      return event.invoke<&Playback::setPan>(event.u8("pan", SemanticOperandRole::Pan));
    }
    case 0xe8:
      return cursor.command("Pan Decrease", SequenceSemantic::Pan).invoke<&Playback::adjustPan>(-8);
    case 0xe9:
      return cursor.command("Pan Increase", SequenceSemantic::Pan).invoke<&Playback::adjustPan>(8);
    case 0xea: {
      auto event = cursor.command("Pan LFO", SequenceSemantic::Modulation);
      const u8 target = event.u8("target", SemanticOperandRole::Pan);
      const u8 step = event.u8("step", SemanticOperandRole::Modulation);
      return event.invoke<&Playback::configurePanLfo>(target, step,
                                                      event.u8("interval", SemanticOperandRole::Duration));
    }
    case 0xeb: {
      auto event = cursor.command("Pan LFO On/Off", SequenceSemantic::Modulation);
      return event.invoke<&Playback::panLfoEnabled>(event.u8("enabled") != 0);
    }
    case 0xec: {
      auto event = cursor.command("DSP Pitch Offset", SequenceSemantic::Pitch);
      return event.set<&TrackState::tuning>(event.s8("pitch_register_delta", SemanticOperandRole::Pitch));
    }
    case 0xed: {
      auto event = cursor.command("Repeat Start", SequenceSemantic::Loop);
      const u8 count = event.u8("count", SemanticOperandRole::Count);
      const Address cell = event.nextAddress();
      event.derived("counter", cell, SourceValueDisplay::Address, SemanticOperandRole::Address);
      static_cast<void>(event.u8("counter_initial"));
      return event.invoke<&Playback::repeatStart>(cell, count);
    }
    case 0xee: {
      auto event = cursor.command("Repeat Break", SequenceSemantic::RepeatBreak);
      const s16 relative = event.s16le("relative", SourceValueDisplay::SignedDecimal,
                                      SemanticOperandRole::JumpTarget);
      const Address destination = relativeTarget(begin + 3, relative);
      Address cell{};
      if (destination.value >= 2 && reader.has(destination.value - 2, 2)) {
        cell = relativeTarget(destination.value, static_cast<s16>(reader.le16(destination.value - 2)));
      }
      event.derived("destination", destination, SourceValueDisplay::Address, SemanticOperandRole::JumpTarget);
      event.derived("counter", cell, SourceValueDisplay::Address, SemanticOperandRole::Address);
      return event.invoke<&Playback::repeatBreak>(cell, destination).mayBranchTo(destination);
    }
    case 0xef: {
      auto event = cursor.command("Repeat End", SequenceSemantic::Repeat);
      const s16 relative = event.s16le("relative", SourceValueDisplay::SignedDecimal,
                                      SemanticOperandRole::RepeatTarget);
      const Address cell = relativeTarget(begin + 3, relative);
      const Address destination{static_cast<u16>(cell.value + 1)};
      event.derived("counter", cell, SourceValueDisplay::Address, SemanticOperandRole::Address);
      event.derived("destination", destination, SourceValueDisplay::Address, SemanticOperandRole::RepeatTarget);
      return event.invoke<&Playback::repeatEnd>(cell, destination).mayBranchTo(destination);
    }
    case 0xf0: {
      auto event = cursor.command("Pitch Envelope", SequenceSemantic::Pitch);
      const u8 delay = event.u8("delay", SemanticOperandRole::Duration);
      const u8 depth = event.u8("step", SemanticOperandRole::Pitch);
      return event.invoke<&Playback::pitchEnvelope>(delay, depth,
                                                    event.s8("interval", SemanticOperandRole::Duration));
    }
    case 0xf1: {
      auto event = cursor.command("Pitch Envelope On/Off", SequenceSemantic::Pitch);
      return event.invoke<&Playback::pitchEnvelopeEnabled>(event.u8("enabled") != 0);
    }
    case 0xf2: {
      auto event = cursor.command("ADSR", SequenceSemantic::Envelope);
      const u8 adsr1 = event.u8("adsr1", SourceValueDisplay::Hex);
      return event.invoke<&Playback::adsr>(adsr1, event.u8("adsr2", SourceValueDisplay::Hex));
    }
    case 0xf3: {
      auto event = cursor.command("Broken GAIN", SequenceSemantic::Envelope);
      return event.invoke<&Playback::brokenGain>(event.u8("gain", SourceValueDisplay::Hex));
    }
    case 0xf4: {
      auto event = cursor.sourceOnly("DSP FLG / Noise", "noise");
      static_cast<void>(event.u8("flg", SourceValueDisplay::Hex));
      return event;
    }
    case 0xf5: {
      auto event = cursor.sourceOnly("DSP Pitch Modulation", "pitch-modulation");
      static_cast<void>(event.u8("enabled"));
      return event;
    }
    case 0xf6: {
      auto event = cursor.command("Echo Voice On/Off", SequenceSemantic::State);
      return event.set<&TrackState::echoEnabled>(event.u8("enabled") != 0);
    }
    case 0xf7: {
      auto event = cursor.command("Echo Parameters", SequenceSemantic::State);
      const u8 delay = event.u8("delay");
      const s8 feedback = event.s8("feedback");
      return event.invoke<&Playback::echoParameters>(delay, feedback, event.u8("fir_preset"));
    }
    case 0xf8: {
      auto event = cursor.command("Echo Volume On/Off", SequenceSemantic::State);
      return event.invoke<&Playback::echoVolumeEnabled>(event.u8("enabled") != 0);
    }
    case 0xf9: {
      auto event = cursor.command("Echo Volume", SequenceSemantic::State);
      const s8 left = event.s8("left", SemanticOperandRole::Level);
      return event.invoke<&Playback::echoVolume>(left, event.s8("right", SemanticOperandRole::Level));
    }
    case 0xfa: {
      constexpr std::array<std::string_view, 8> names{
          "coefficient_0", "coefficient_1", "coefficient_2", "coefficient_3",
          "coefficient_4", "coefficient_5", "coefficient_6", "coefficient_7",
      };
      auto event = cursor.command("Overwrite FIR Preset", SequenceSemantic::State);
      const u8 preset = event.u8("preset");
      for (u32 coefficient = 0; coefficient < 8; ++coefficient) {
        static_cast<void>(event.s8(names[coefficient]));
      }
      return event.invoke<&Playback::overwriteFirPreset>(preset);
    }
    case 0xfb:
      return cursor.ignored("No Operation", 1, "nop");
    case 0xfc: {
      auto event = cursor.command("Goto / End", SequenceSemantic::Jump);
      const s16 relative = event.s16le("relative", SourceValueDisplay::SignedDecimal,
                                      SemanticOperandRole::JumpTarget);
      if (relative == 0) {
        return event.label("End").end();
      }
      const Address destination = relativeTarget(begin + 3, relative);
      event.derived("destination", destination, SourceValueDisplay::Address, SemanticOperandRole::JumpTarget);
      return destination.value < begin ? event.loopCandidate(destination) : event.jump(destination);
    }
    default:
      return cursor.unsupported("Invalid Command").stop();
  }
}

[[nodiscard]] std::vector<u32> runtimePatches(ByteReader reader, const Layout& layout) {
  std::vector<u32> result(256);
  for (u32 program = 0; program < 256; ++program) {
    const u16 row = static_cast<u16>(layout.instrumentTableAddress + static_cast<u8>(program * 5u));
    if (!reader.has(row, 5)) {
      continue;
    }
    result[program] = packPatch(reader.u8At(row), reader.u8At(row + 1), reader.be16(row + 3));
  }
  return result;
}

}  // namespace

const SequenceDialect& sequenceDialect() {
  static const SequenceDialect dialect{
      .commandDetailKindPrefix = "falcom-snes",
      .timebase = Timebase{.ppqn = kPpqn},
      .behavior =
          SequenceProgramBehavior{
              .commandLimit = kCommandLimit,
              .panLaw = PanLaw::ConstantSum,
              .initialSourceInstrument = InstrumentIdentity{.domain = std::string(kInstrumentDomain), .key = 0},
              .initialLevel = 0.0,
              .initialMasterLevel = 1.0,
              .initialReverbSend = 0.0,
              .initialStereoBalance = math::pan(0x40),
              .initialPitchBendRangeSemitones = 24,
              .initialTempoMicrosecondsPerQuarter = math::tempoMicroseconds(kDefaultTempo),
          },
  };
  return dialect;
}

TrackProgram decodeSourceTrack(ByteReader reader, u32 trackNumber, u32 startAddress,
                               std::span<const u8, 7> durations, std::vector<Diagnostic>* diagnostics) {
  const TrackDecodeScope tracks{.reader = reader, .maxCommands = kCommandLimit};
  return tracks.decode(trackNumber, startAddress, [&](u32 offset) {
    return decodeCommand(reader, offset, durations, diagnostics);
  });
}

SequenceParse decodeSequence(ByteReader reader, const Layout& layout, AssetId sequenceId, SourceMapBuilder* sourceMap,
                             std::vector<Diagnostic>* diagnostics) {
  const SourceRange header = reader.range(layout.sequenceHeaderAddress, 0x20);
  std::array<u8, 7> durations;
  std::ranges::copy(reader.slice(layout.sequenceHeaderAddress + 0x18, durations.size()), durations.begin());
  std::set<u8> programs{0};
  SequenceDecodeSession sequence{reader, sequenceDialect(), sequenceId, header, sourceMap, kCommandLimit, kAramSize};
  for (u32 track = 0; track < kTrackCount; ++track) {
    if (!layout.trackStarts[track]) {
      continue;
    }
    const u32 pointer = layout.sequenceHeaderAddress + track * 2;
    const u16 relative = reader.le16(pointer);
    sequence.addTrack(
        track, reader.range(pointer, 2), *layout.trackStarts[track],
        [&](u32 offset) { return decodeCommand(reader, offset, durations, diagnostics, &programs); }, relative);
  }
  SequenceProgram program = sequence.finish(
      makeCompiledRuntime<Cursor, ProgramState>(RuntimeConfig{.patches = runtimePatches(reader, layout)}));
  return SequenceParse{
      .program = std::move(program),
      .programs = std::move(programs),
      .headerRange = header,
  };
}

}  // namespace vgmtrans::formats::falcom_snes
