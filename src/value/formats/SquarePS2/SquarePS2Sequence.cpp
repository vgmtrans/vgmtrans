/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/SquarePS2/SquarePS2.h"

#include "value/base/LevelScale.h"
#include "value/sequence/CompiledCommandRuntime.h"
#include "value/sequence/SequenceVm.h"
#include "value/synth/PsxSpu.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace vgmtrans::formats::square_ps2 {

using namespace core;

namespace {

constexpr u32 kMaxCommands = 1'048'576;

struct Vlq {
  u32 value = 0;
  u32 size = 0;
};

[[nodiscard]] std::optional<Vlq> readVlq(ByteReader reader, u32 offset, u32 end) {
  Vlq result;
  while (offset + result.size < end && result.size < 5) {
    const u8 byte = reader.u8At(offset + result.size++);
    if (result.value > (std::numeric_limits<u32>::max() >> 7)) {
      return std::nullopt;
    }
    result.value = (result.value << 7) | (byte & 0x7f);
    if ((byte & 0x80) == 0) {
      return result;
    }
  }
  return std::nullopt;
}

struct EventLayout {
  u32 begin = 0;
  u32 end = 0;
  u32 delta = 0;
  u32 deltaSize = 0;
  u8 status = 0;
};

[[nodiscard]] std::optional<EventLayout> eventLayout(ByteReader reader, u32 begin, u32 end) {
  const auto delta = readVlq(reader, begin, end);
  if (!delta || begin + delta->size >= end) {
    return std::nullopt;
  }
  const u8 status = reader.u8At(begin + delta->size);
  u32 position = begin + delta->size + 1;
  const auto bytes = [&](u32 count) -> bool {
    if (count > end - position) {
      return false;
    }
    position += count;
    return true;
  };
  const auto variable = [&](u32 suffix) -> bool {
    const auto value = readVlq(reader, position, end);
    if (!value || value->size + suffix > end - position) {
      return false;
    }
    position += value->size + suffix;
    return true;
  };

  bool valid = true;
  switch (status) {
    case 0x05:
    case 0x07:
    case 0x08:
    case 0x0a:
    case 0x0d:
    case 0x0e:
    case 0x12:
    case 0x13:
    case 0x1a:
    case 0x1b:
    case 0x20:
    case 0x22:
    case 0x24:
    case 0x26:
    case 0x2d:
    case 0x31:
    case 0x32:
    case 0x33:
    case 0x34:
    case 0x35:
    case 0x36:
    case 0x37:
    case 0x38:
    case 0x3c:
    case 0x3d:
    case 0x3e:
    case 0x3f:
    case 0x42:
    case 0x44:
    case 0x46:
    case 0x4a:
    case 0x4c:
    case 0x4e:
    case 0x52:
    case 0x54:
    case 0x56:
    case 0x58:
    case 0x59:
    case 0x5a:
    case 0x5b:
    case 0x5d:
    case 0x6c:
    case 0x70:
    case 0x71:
    case 0x72:
    case 0x79:
    case 0x7c:
    case 0x7d:
      valid = bytes(1);
      break;
    case 0x09:
    case 0x0b:
    case 0x0c:
    case 0x11:
    case 0x19:
    case 0x21:
    case 0x39:
    case 0x47:
    case 0x4f:
    case 0x5c:
    case 0x62:
    case 0x78:
      valid = bytes(2);
      break;
    case 0x40:
    case 0x48:
    case 0x50:
    case 0x73:
    case 0x74:
    case 0x75:
      valid = bytes(3);
      break;
    case 0x23:
    case 0x25:
    case 0x27:
    case 0x2c:
    case 0x43:
    case 0x45:
    case 0x4b:
    case 0x4d:
    case 0x53:
    case 0x55:
      valid = variable(1);
      break;
    case 0x28:
      valid = variable(0);
      break;
    case 0x76:
    case 0x77:
      valid = bytes(1) && variable(1);
      break;
    default:
      break;
  }
  if (!valid) {
    return std::nullopt;
  }
  return EventLayout{
      .begin = begin,
      .end = position,
      .delta = delta->value,
      .deltaSize = delta->size,
      .status = status,
  };
}

struct RepeatInfo {
  Address start;
  u8 slot = 0;
};

struct TrackLayout {
  std::map<u32, RepeatInfo> repeatEnds;
  std::map<u32, RepeatInfo> repeatLoops;
  std::map<u32, Address> sequenceLoops;
};

[[nodiscard]] TrackLayout analyzeTrack(ByteReader reader, u32 begin, u32 end) {
  struct OpenRepeat {
    Address start;
    u8 slot = 0;
  };
  TrackLayout result;
  std::vector<OpenRepeat> repeats;
  std::optional<Address> sequenceLoop;
  for (u32 offset = begin; offset < end;) {
    const auto event = eventLayout(reader, offset, end);
    if (!event || event->end <= offset) {
      break;
    }
    switch (event->status) {
      case 0x02:
        sequenceLoop = Address{event->end};
        break;
      case 0x03:
        if (sequenceLoop) {
          result.sequenceLoops.emplace(offset, *sequenceLoop);
        }
        break;
      case 0x04:
        repeats.push_back(OpenRepeat{
            .start = Address{event->end},
            .slot = static_cast<u8>(std::min<std::size_t>(repeats.size(), 3)),
        });
        break;
      case 0x05:
        if (!repeats.empty()) {
          result.repeatEnds.emplace(offset, RepeatInfo{.start = repeats.back().start, .slot = repeats.back().slot});
          repeats.pop_back();
        }
        break;
      case 0x06:
        if (!repeats.empty()) {
          result.repeatLoops.emplace(offset, RepeatInfo{.start = repeats.back().start, .slot = repeats.back().slot});
        }
        break;
      default:
        break;
    }
    offset = event->end;
    if (event->status == 0x00 || (event->status >= 0x7a && event->status <= 0x7b)) {
      break;
    }
  }
  return result;
}

[[nodiscard]] double controller(s8 value) {
  return value < 0 ? 0.0 : LevelScale::linearFromLinear((static_cast<u8>(value) + 1) / 128.0);
}

[[nodiscard]] double panPosition(u8 value) {
  return std::clamp((std::min<u8>(value, 127) - 64.0) / 63.0, -1.0, 1.0);
}

[[nodiscard]] u32 tempoMicros(u16 raw) {
  const double bpm = raw < 0x100 ? raw : raw / 256.0;
  return bpm <= 0.0 ? 0 : static_cast<u32>(std::lround(60'000'000.0 / bpm));
}

[[nodiscard]] u32 totalPlays(u8 raw) {
  return raw == 0 ? 256 : raw;
}

[[nodiscard]] double vibratoDepth(u8 raw) {
  return raw < 0x80 ? (raw + 1) / 128.0 : ((raw & 0x7f) + 1) * (3.0 / 32.0);
}

[[nodiscard]] double tremoloDepth(u8 raw) {
  return (raw + 1) / 256.0;
}

[[nodiscard]] double panLfoDepth(u8 raw) {
  return std::min(2.0, 2.0 * (raw + 1) / 127.0);
}

[[nodiscard]] double lfoCyclesPerTick(u8 raw) {
  return 1.0 / (4.0 * (raw == 0 ? 256.0 : raw));
}

[[nodiscard]] LfoShape lfoShape(u8 raw) {
  const u8 shape = raw & 0x0f;
  if (shape == 2 || shape == 10) {
    std::vector<double> samples(256);
    for (u32 i = 0; i < samples.size(); ++i) {
      const u8 phase = shape == 10 ? static_cast<u8>(~i) : static_cast<u8>(i);
      samples[i] = static_cast<s8>(phase) / 256.0;
    }
    return LfoShape{.waveform = shape == 10 ? LfoWaveform::SawtoothDown : LfoWaveform::SawtoothUp,
                    .samples = std::move(samples)};
  }
  const u8 base = shape & 7;
  if (base == 0) {
    return LfoShape{.waveform = LfoWaveform::Sine};
  }
  if (base == 1) {
    return LfoShape{.waveform = LfoWaveform::Triangle};
  }
  if (base == 4) {
    return LfoShape{.waveform = LfoWaveform::Noise};
  }
  return LfoShape{.waveform = LfoWaveform::Square};
}

struct LfoState {
  s8 mode = -1;
  u8 wave = 0;
  u8 depth = 0;
  u8 rate = 0;
  u8 delay = 0;
  u8 fade = 0;
};

struct ProgramState {
  explicit ProgramState(const RuntimeConfig& config) : envelopes(config.envelopes) {}

  [[nodiscard]] const EnvelopeDefaults* envelope(u16 bank, u8 program) const {
    const auto found = std::ranges::find_if(
        envelopes, [&](const EnvelopeDefaults& value) { return value.bank == bank && value.program == program; });
    return found == envelopes.end() ? nullptr : &*found;
  }

  std::span<const EnvelopeDefaults> envelopes;
};

struct TrackState {
  explicit TrackState(const RuntimeConfig& config) : bank(config.defaultBank) {}

  u16 bank = 0;
  u8 program = 0;
  u8 previousKey = 60;
  u8 previousVelocity = 127;
  s8 pitchBendRange = 2;
  s16 coarseTuning = 0;
  u32 portamentoTicks = 0;
  bool initialized = false;
  bool hasEnvelope = false;
  bool portamento = false;
  bool hasPreviousNote = false;
  u16 adsr1 = 0;
  u16 adsr2 = 0;
  std::array<LfoState, 4> lfos{};
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
    emitInstrument(out);
  }

  [[nodiscard]] u64 eventTick(u32 delta) const {
    return delta > std::numeric_limits<u64>::max() - vm.tick() ? std::numeric_limits<u64>::max() : vm.tick() + delta;
  }

  [[nodiscard]] Effects after(u32 delta) const { return Effects::wait(delta); }

  [[nodiscard]] PerformanceEmitter delayed(u32 delta) const { return out.at(eventTick(delta)); }

  void emitInstrument(PerformanceEmitter& emitter) {
    emitter.instrument(instrumentIdentity(track.bank, track.program), InstrumentEnvelopeMode::UseInstrumentEnvelope);
  }

  void loadEnvelope() {
    const auto* envelope = programState.envelope(track.bank, track.program);
    track.hasEnvelope = envelope != nullptr;
    if (envelope != nullptr) {
      track.adsr1 = envelope->adsr1;
      track.adsr2 = envelope->adsr2;
    }
  }

  void publishEnvelope(PerformanceEmitter& emitter) {
    if (track.hasEnvelope) {
      emitter.replaceEnvelope(psxSpuEnvelope(track.adsr1, track.adsr2, PsxSpuGeneration::Ps2),
                              VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks);
    }
  }

  Effects noteOn(u8 key, u8 velocity, u32 delta) {
    const u8 previousKey = track.previousKey;
    const bool glides = track.portamento && track.hasPreviousNote && track.portamentoTicks != 0 && previousKey != key;
    track.previousKey = key;
    track.previousVelocity = velocity;
    track.hasPreviousNote = true;
    auto emitter = delayed(delta);
    const PerformanceNoteId note = emitter.noteOn(key, controller(static_cast<s8>(std::min<u8>(velocity, 127))));
    if (glides) {
      emitter.pitchSlide(note, previousKey, key, track.portamentoTicks).preferPortamento();
    }
    return after(delta);
  }

  Effects noteOff(u8 key, u32 delta) {
    track.previousKey = key;
    delayed(delta).noteOff(key);
    return after(delta);
  }

  Effects selectProgram(u16 bank, u8 program, u32 delta) {
    track.bank = bank;
    track.program = program;
    loadEnvelope();
    auto emitter = delayed(delta);
    emitInstrument(emitter);
    emitter.restoreEnvelope(EnvelopeFields::All, VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks);
    return after(delta);
  }

  Effects resetEnvelope(u32 delta) {
    loadEnvelope();
    auto emitter = delayed(delta);
    emitInstrument(emitter);
    emitter.restoreEnvelope(EnvelopeFields::All, VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks);
    return after(delta);
  }

  Effects envelopeParameter(u8 parameter, u8 value, u8 second, u32 delta) {
    switch (parameter) {
      case 1:
        track.adsr1 = static_cast<u16>((track.adsr1 & ~0x7f00u) | ((value & 0x7f) << 8));
        break;
      case 2:
        track.adsr1 = static_cast<u16>((track.adsr1 & ~0x00f0u) | ((value & 0x0f) << 4));
        break;
      case 3:
        track.adsr1 = static_cast<u16>((track.adsr1 & ~0x000fu) | (value & 0x0f));
        break;
      case 4:
        track.adsr2 = static_cast<u16>((track.adsr2 & ~0x1fc0u) | ((value & 0x7f) << 6));
        break;
      case 5:
        track.adsr2 = static_cast<u16>((track.adsr2 & ~0x001fu) | (value & 0x1f));
        break;
      case 6:
        track.adsr1 = static_cast<u16>((track.adsr1 & ~0x8000u) | ((value & 4) << 13));
        break;
      case 7:
        track.adsr2 = static_cast<u16>((track.adsr2 & ~0xc000u) | ((value & 4) << 13) | ((value & 2) << 13));
        break;
      case 8:
        track.adsr2 = static_cast<u16>((track.adsr2 & ~0x0020u) | ((value & 4) << 3));
        break;
      case 9:
        track.adsr1 = static_cast<u16>((track.adsr1 & ~0x00ffu) | ((value & 0x0f) << 4) | (second & 0x0f));
        break;
      default:
        break;
    }
    auto emitter = delayed(delta);
    publishEnvelope(emitter);
    return after(delta);
  }

  [[nodiscard]] LfoPerformanceContext lfoContext(const LfoState& lfo, bool restart) const {
    const bool inverted = (lfo.wave & 0x0f) >= 8;
    const double initialPhase = inverted && (lfo.wave & 7) != 2 && (lfo.wave & 7) != 4 ? 0.5 : 0.0;
    return LfoPerformanceContext{
        .cyclesPerTick = lfoCyclesPerTick(lfo.rate),
        .delayTicks = lfo.delay,
        .delayIsTempoRelative = true,
        .shape = lfoShape(lfo.wave),
        .initialPhaseCycles = initialPhase,
        .noteRestartInitialPhaseCycles = initialPhase,
        .steppedDepthAttackSteps = lfo.fade == 0 ? std::nullopt : std::optional<u32>{lfo.fade},
        .restartMode = restart ? LfoRestartMode::PhaseAndDelay : LfoRestartMode::None,
        .phaseRunsAtZeroDepth = true,
        .tremoloGainMode = TremoloGainMode::BipolarAroundNominal,
        .panLaw = PanLaw::EqualPower,
    };
  }

  void emitLfoDepth(PerformanceEmitter& emitter, const LfoState& lfo, bool restart = false) {
    auto context = lfoContext(lfo, restart);
    switch (lfo.mode) {
      case 0:
        emitter.vibratoDepth(vibratoDepth(lfo.depth), std::move(context));
        break;
      case 1:
        emitter.tremoloLinearGainDepth(tremoloDepth(lfo.depth), std::move(context));
        break;
      case 2:
        emitter.panLfoDepth(panLfoDepth(lfo.depth), std::move(context));
        break;
      default:
        break;
    }
  }

  void emitLfoRate(PerformanceEmitter& emitter, const LfoState& lfo) {
    auto context = lfoContext(lfo, false);
    switch (lfo.mode) {
      case 0:
        emitter.vibratoRateCyclesPerTick(lfoCyclesPerTick(lfo.rate), std::move(context));
        break;
      case 1:
        emitter.tremoloRateCyclesPerTick(lfoCyclesPerTick(lfo.rate), std::move(context));
        break;
      case 2:
        emitter.panLfoRateCyclesPerTick(lfoCyclesPerTick(lfo.rate), std::move(context));
        break;
      default:
        break;
    }
  }

  Effects lfoInitialize(u8 slot, s8 mode, u8 depth, u8 rate, u8 wave, u32 delta) {
    auto& lfo = track.lfos[slot & 3];
    lfo.mode = mode;
    lfo.depth = depth;
    lfo.rate = rate;
    lfo.wave = wave & 0x0f;
    auto emitter = delayed(delta);
    emitLfoDepth(emitter, lfo, true);
    emitLfoRate(emitter, lfo);
    return after(delta);
  }

  Effects lfoOff(u8 slot, u32 delta) {
    auto& lfo = track.lfos[slot & 3];
    auto emitter = delayed(delta);
    lfo.depth = 0;
    switch (lfo.mode) {
      case 0:
        emitter.vibratoDepth(0.0, lfoContext(lfo, false));
        break;
      case 1:
        emitter.tremoloLinearGainDepth(0.0, lfoContext(lfo, false));
        break;
      case 2:
        emitter.panLfoDepth(0.0, lfoContext(lfo, false));
        break;
      default:
        break;
    }
    lfo.mode = -1;
    return after(delta);
  }

  Effects lfoDepthValue(u8 slot, u8 depth, u32 delta) {
    auto& lfo = track.lfos[slot & 3];
    lfo.depth = depth;
    auto emitter = delayed(delta);
    emitLfoDepth(emitter, lfo);
    return after(delta);
  }

  Effects lfoDepthFade(u8 slot, u32 duration, u8 depth, u32 delta) {
    auto& lfo = track.lfos[slot & 3];
    lfo.depth = depth;
    const u64 tick = eventTick(delta);
    auto emitter = out.at(tick);
    const auto target = lfo.mode == 0   ? PerformanceAutomationTarget::VibratoDepth
                        : lfo.mode == 1 ? PerformanceAutomationTarget::TremoloDepth
                                        : PerformanceAutomationTarget::VibratoDepth;
    const double value = lfo.mode == 0 ? vibratoDepth(depth) : tremoloDepth(depth);
    if (lfo.mode < 2) {
      const auto automation = emitter.fade(target, value, duration);
      static_cast<void>(automation.at(emitter, tick + duration));
    }
    auto terminal = out.at(tick + duration);
    emitLfoDepth(terminal, lfo);
    return after(delta);
  }

  Effects lfoRateValue(u8 slot, u8 rate, u32 delta) {
    auto& lfo = track.lfos[slot & 3];
    lfo.rate = rate;
    auto emitter = delayed(delta);
    emitLfoRate(emitter, lfo);
    return after(delta);
  }

  Effects lfoRateFade(u8 slot, u32 duration, u8 rate, u32 delta) {
    auto& lfo = track.lfos[slot & 3];
    lfo.rate = rate;
    auto emitter = out.at(eventTick(delta) + duration);
    emitLfoRate(emitter, lfo);
    return after(delta);
  }

  Effects lfoWave(u8 slot, u8 wave, u32 delta) {
    auto& lfo = track.lfos[slot & 3];
    lfo.wave = wave & 0x0f;
    auto emitter = delayed(delta);
    emitLfoDepth(emitter, lfo, true);
    return after(delta);
  }

  Effects lfoDelay(u8 slot, u8 delay, u8 fade, u32 delta) {
    auto& lfo = track.lfos[slot & 3];
    lfo.delay = delay;
    lfo.fade = fade;
    auto emitter = delayed(delta);
    emitLfoDepth(emitter, lfo, true);
    return after(delta);
  }

  Effects lfoModeWave(u8 slot, u8 mode, u8 wave, u32 delta) {
    auto& lfo = track.lfos[slot & 3];
    lfo.mode = static_cast<s8>(mode % 3);
    lfo.wave = wave & 0x0f;
    auto emitter = delayed(delta);
    emitLfoDepth(emitter, lfo, true);
    emitLfoRate(emitter, lfo);
    return after(delta);
  }

  Effects tempo(u8 raw, u32 delta) {
    if (const u32 micros = tempoMicros(raw); micros != 0) {
      delayed(delta).tempo(micros);
    }
    return after(delta);
  }

  Effects tempoFade(u8 duration, u8 raw, u32 delta) {
    if (const u32 micros = tempoMicros(raw); micros != 0) {
      const u64 tick = eventTick(delta);
      auto emitter = out.at(tick);
      emitter.fade(PerformanceAutomationTarget::Tempo, micros, duration).at(emitter, tick + duration).tempo(micros);
    }
    return after(delta);
  }

  Effects level(s8 raw, u32 delta) {
    delayed(delta).level(controller(raw), ValueQuantization{.levels = 128});
    return after(delta);
  }

  Effects masterLevel(s8 raw, u32 delta) {
    delayed(delta).masterLevel(controller(raw));
    return after(delta);
  }

  Effects expression(s8 raw, u32 delta) {
    delayed(delta).expression(controller(raw), ValueQuantization{.levels = 128});
    return after(delta);
  }

  Effects controllerFade(u8 kind, u32 duration, s8 raw, u32 delta) {
    const u64 tick = eventTick(delta);
    auto emitter = out.at(tick);
    const double value = controller(raw);
    const auto target = kind == 0   ? PerformanceAutomationTarget::Level
                        : kind == 1 ? PerformanceAutomationTarget::MasterLevel
                                    : PerformanceAutomationTarget::Expression;
    auto fade = emitter.fade(target, value, duration);
    auto terminal = fade.at(emitter, tick + duration);
    if (kind == 0) {
      terminal.level(value);
    } else if (kind == 1) {
      terminal.masterLevel(value);
    } else {
      terminal.expression(value);
    }
    return after(delta);
  }

  Effects pan(u8 raw, u32 delta) {
    delayed(delta).pan(panPosition(raw));
    return after(delta);
  }

  Effects panFade(u32 duration, u8 raw, u32 delta) {
    const u64 tick = eventTick(delta);
    auto emitter = out.at(tick);
    const double value = panPosition(raw);
    emitter.fade(PerformanceAutomationTarget::Pan, value, duration).at(emitter, tick + duration).pan(value);
    return after(delta);
  }

  Effects portamentoTime(u32 duration, u32 delta) {
    track.portamentoTicks = duration;
    track.portamento = true;
    delayed(delta).portamentoEnable(true);
    return after(delta);
  }

  Effects portamentoOff(u32 delta) {
    track.portamento = false;
    delayed(delta).portamentoEnable(false);
    return after(delta);
  }

  Effects pitchBend(u8 lsb, u8 msb, u32 delta) {
    const s32 wheel = static_cast<s32>(lsb) + static_cast<s32>(msb) * 128 - 8192;
    const double normalized = std::clamp(wheel / 8192.0, -1.0, 1.0);
    delayed(delta).pitchBend(PitchBendPerformanceEvent{
        .semitones = normalized * track.pitchBendRange,
        .normalizedWheelPosition = normalized,
    });
    return after(delta);
  }

  Effects bendRange(s8 range, u32 delta) {
    track.pitchBendRange = range;
    delayed(delta).pitchBendRange(static_cast<u8>(std::abs(static_cast<int>(range))));
    return after(delta);
  }

  Effects tuning(s16 semitones256, u32 delta) {
    track.coarseTuning = semitones256;
    delayed(delta).tuning(semitones256 * (100.0 / 256.0));
    return after(delta);
  }

  Effects reverb(double send, u32 delta) {
    delayed(delta).reverb(send);
    return after(delta);
  }

  Effects routing(u8 left, u8 right, u32 delta) {
    const double wet = ((left == 1 || left == 2 ? 1.0 : 0.0) + (right == 1 || right == 2 ? 1.0 : 0.0)) / 2.0;
    delayed(delta).reverb(wet);
    return after(delta);
  }

  Effects repeatEnd(u8 slot, u32 plays, Address destination, u32 delta) {
    Effects effects = after(delta);
    effects.flowOverride = vm.countedRepeatUntil(slot, plays, destination).flowOverride;
    return effects;
  }

  Effects declaredLoop(Address destination, u32 delta) {
    Effects effects = after(delta);
    effects.flowOverride = vm.declaredLoop(destination).flowOverride;
    return effects;
  }
};

using Cursor = CompilerCursor<TrackState, Playback>;

[[nodiscard]] Cursor::Event beginEvent(Cursor& cursor, const EventLayout& source, std::string_view label,
                                       SequenceSemantic semantic,
                                       CommandPlaybackStatus playback = CommandPlaybackStatus::AffectsPlayback) {
  auto event = cursor.command(label, semantic, playback);
  event.opcodeValue("delta_byte_0", cursor.opcode(), SourceValueDisplay::Hex, SemanticOperandRole::Duration);
  for (u32 i = 1; i < source.deltaSize; ++i) {
    event.u8("delta_byte", SourceValueDisplay::Hex, SemanticOperandRole::Duration);
  }
  event.derived("delta", source.delta, SemanticOperandRole::Duration);
  event.u8("status", SourceValueDisplay::Hex);
  return event;
}

[[nodiscard]] DecodedBytecodeCommand sourceOnly(Cursor& cursor, const EventLayout& source, std::string_view label,
                                                SequenceSemantic semantic, u32 parameters) {
  auto event = beginEvent(cursor, source, label, semantic, CommandPlaybackStatus::SourceOnly);
  if (parameters != 0) {
    event.rawBytes("parameters", parameters);
  }
  return event.wait(source.delta);
}

[[nodiscard]] DecodedBytecodeCommand decodeCommand(ByteReader reader, u32 begin, u32 end, const TrackLayout& layout,
                                                   std::vector<Diagnostic>* diagnostics) {
  Cursor cursor(reader, begin, end, kSquarePs2CommandKindPrefix, diagnostics);
  if (!cursor.hasOpcode()) {
    return cursor.truncated();
  }
  const auto source = eventLayout(reader, begin, end);
  if (!source) {
    return cursor.unsupported("Truncated SquarePS2 Event").stop();
  }

  switch (source->status) {
    case 0x00:
      return beginEvent(cursor, *source, "End of Track", SequenceSemantic::End, CommandPlaybackStatus::StopsPlayback)
          .wait(source->delta)
          .end();
    case 0x01:
      return beginEvent(cursor, *source, "Section Marker", SequenceSemantic::Meta).wait(source->delta);
    case 0x02:
      return beginEvent(cursor, *source, "Sequence Loop Point", SequenceSemantic::Loop).wait(source->delta);
    case 0x03: {
      auto event = beginEvent(cursor, *source, "Sequence Loop", SequenceSemantic::Loop);
      const auto found = layout.sequenceLoops.find(begin);
      if (found == layout.sequenceLoops.end()) {
        return event.wait(source->delta).end();
      }
      event.derived("destination", found->second, SourceValueDisplay::Address, SemanticOperandRole::LoopTarget);
      event.mayBranchTo(found->second);
      return event.invoke<&Playback::declaredLoop>(found->second, source->delta);
    }
    case 0x04:
      return beginEvent(cursor, *source, "Repeat Begin", SequenceSemantic::Repeat).wait(source->delta);
    case 0x05: {
      auto event = beginEvent(cursor, *source, "Repeat End", SequenceSemantic::Repeat);
      const u8 count = event.u8("count", SemanticOperandRole::Count);
      const auto found = layout.repeatEnds.find(begin);
      if (found == layout.repeatEnds.end()) {
        return event.wait(source->delta);
      }
      event.derived("total_plays", totalPlays(count), SemanticOperandRole::Count);
      event.derived("destination", found->second.start, SourceValueDisplay::Address, SemanticOperandRole::RepeatTarget);
      event.mayBranchTo(found->second.start);
      return event.invoke<&Playback::repeatEnd>(found->second.slot, totalPlays(count), found->second.start,
                                                source->delta);
    }
    case 0x06: {
      auto event = beginEvent(cursor, *source, "Repeat Forever", SequenceSemantic::Loop);
      const auto found = layout.repeatLoops.find(begin);
      if (found == layout.repeatLoops.end()) {
        return event.wait(source->delta).end();
      }
      event.derived("destination", found->second.start, SourceValueDisplay::Address, SemanticOperandRole::LoopTarget);
      event.mayBranchTo(found->second.start);
      return event.invoke<&Playback::declaredLoop>(found->second.start, source->delta);
    }
    case 0x07:
      return sourceOnly(cursor, *source, "External Repeat Condition", SequenceSemantic::RepeatBreak, 1);
    case 0x08: {
      auto event = beginEvent(cursor, *source, "Tempo", SequenceSemantic::Tempo);
      const u8 raw = event.u8("tempo");
      event.derived("beats_per_minute", static_cast<double>(raw), SourceValueDisplay::BeatsPerMinute);
      return event.invoke<&Playback::tempo>(raw, source->delta);
    }
    case 0x09: {
      auto event = beginEvent(cursor, *source, "Tempo Fade", SequenceSemantic::Tempo);
      const u8 duration = event.u8("duration", SemanticOperandRole::Duration);
      const u8 target = event.u8("target");
      event.derived("target_beats_per_minute", static_cast<double>(target), SourceValueDisplay::BeatsPerMinute);
      return event.invoke<&Playback::tempoFade>(duration, target, source->delta);
    }
    case 0x0a: {
      auto event = beginEvent(cursor, *source, "Master Level", SequenceSemantic::Level);
      return event.invoke<&Playback::masterLevel>(event.s8("level", SemanticOperandRole::Level), source->delta);
    }
    case 0x0b: {
      auto event = beginEvent(cursor, *source, "Master Level Fade", SequenceSemantic::Level);
      const u8 duration = event.u8("duration", SemanticOperandRole::Duration);
      const s8 target = event.s8("target", SemanticOperandRole::Level);
      return event.invoke<&Playback::controllerFade>(1, duration, target, source->delta);
    }
    case 0x0c: {
      auto event = beginEvent(cursor, *source, "Time Signature", SequenceSemantic::Meta);
      const u8 numerator = event.u8("numerator");
      const u8 denominator = event.u8("denominator");
      return event.invoke(
          [delta = source->delta](Playback& playback, u8 n, u8 d) {
            if (n != 0 && d != 0) {
              playback.delayed(delta).timeSignature(n, d, 48);
            }
            return playback.after(delta);
          },
          numerator, denominator);
    }
    case 0x0d:
      return sourceOnly(cursor, *source, "Driver Mix Preset", SequenceSemantic::State, 1);
    case 0x0e:
      return sourceOnly(cursor, *source, "Reserved Parameter", SequenceSemantic::State, 1);
    case 0x10:
      return beginEvent(cursor, *source, "Note On (Previous Key and Velocity)", SequenceSemantic::Note)
          .invoke([delta = source->delta](Playback& playback) {
            return playback.noteOn(playback.track.previousKey, playback.track.previousVelocity, delta);
          });
    case 0x11: {
      auto event = beginEvent(cursor, *source, "Note On", SequenceSemantic::Note);
      const u8 key = event.u8("key", SourceValueDisplay::MidiNote, SemanticOperandRole::NoteKey);
      const u8 velocity = event.u8("velocity", SemanticOperandRole::Level);
      return event.invoke<&Playback::noteOn>(key, velocity, source->delta);
    }
    case 0x12: {
      auto event = beginEvent(cursor, *source, "Note On (Previous Velocity)", SequenceSemantic::Note);
      const u8 key = event.u8("key", SourceValueDisplay::MidiNote, SemanticOperandRole::NoteKey);
      return event.invoke(
          [delta = source->delta](Playback& playback, u8 value) {
            return playback.noteOn(value, playback.track.previousVelocity, delta);
          },
          key);
    }
    case 0x13: {
      auto event = beginEvent(cursor, *source, "Note On (Previous Key)", SequenceSemantic::Note);
      const u8 velocity = event.u8("velocity", SemanticOperandRole::Level);
      return event.invoke(
          [delta = source->delta](Playback& playback, u8 value) {
            return playback.noteOn(playback.track.previousKey, value, delta);
          },
          velocity);
    }
    case 0x18:
      return beginEvent(cursor, *source, "Note Off (Previous Key)", SequenceSemantic::Note)
          .invoke([delta = source->delta](Playback& playback) {
            return playback.noteOff(playback.track.previousKey, delta);
          });
    case 0x19: {
      auto event = beginEvent(cursor, *source, "Note Off", SequenceSemantic::Note);
      const u8 key = event.u8("key", SourceValueDisplay::MidiNote, SemanticOperandRole::NoteKey);
      event.u8("release_parameter");
      return event.invoke<&Playback::noteOff>(key, source->delta);
    }
    case 0x1a: {
      auto event = beginEvent(cursor, *source, "Note Off", SequenceSemantic::Note);
      return event.invoke<&Playback::noteOff>(
          event.u8("key", SourceValueDisplay::MidiNote, SemanticOperandRole::NoteKey), source->delta);
    }
    case 0x1b: {
      auto event = beginEvent(cursor, *source, "Note Off (Previous Key)", SequenceSemantic::Note);
      event.u8("release_parameter");
      return event.invoke(
          [delta = source->delta](Playback& playback) { return playback.noteOff(playback.track.previousKey, delta); });
    }
    case 0x20: {
      auto event = beginEvent(cursor, *source, "Program Change", SequenceSemantic::Program);
      const u8 program = event.u8("program", SemanticOperandRole::InstrumentProgram);
      return event.invoke(
          [delta = source->delta](Playback& playback, u8 value) {
            return playback.selectProgram(playback.track.bank, value, delta);
          },
          program);
    }
    case 0x21: {
      auto event = beginEvent(cursor, *source, "Bank and Program Change", SequenceSemantic::Program);
      const u8 bank = event.u8("bank", SemanticOperandRole::InstrumentBank);
      const u8 program = event.u8("program", SemanticOperandRole::InstrumentProgram);
      return event.invoke<&Playback::selectProgram>(bank, program, source->delta);
    }
    case 0x22: {
      auto event = beginEvent(cursor, *source, "Level", SequenceSemantic::Level);
      return event.invoke<&Playback::level>(event.s8("level", SemanticOperandRole::Level), source->delta);
    }
    case 0x23: {
      auto event = beginEvent(cursor, *source, "Level Fade", SequenceSemantic::Level);
      const u32 duration = event.varLen("duration", SemanticOperandRole::Duration);
      const s8 target = event.s8("target", SemanticOperandRole::Level);
      return event.invoke<&Playback::controllerFade>(0, duration, target, source->delta);
    }
    case 0x24: {
      auto event = beginEvent(cursor, *source, "Expression", SequenceSemantic::Level);
      return event.invoke<&Playback::expression>(event.s8("expression", SemanticOperandRole::Level), source->delta);
    }
    case 0x25: {
      auto event = beginEvent(cursor, *source, "Expression Fade", SequenceSemantic::Level);
      const u32 duration = event.varLen("duration", SemanticOperandRole::Duration);
      const s8 target = event.s8("target", SemanticOperandRole::Level);
      return event.invoke<&Playback::controllerFade>(2, duration, target, source->delta);
    }
    case 0x26: {
      auto event = beginEvent(cursor, *source, "Pan", SequenceSemantic::Pan);
      return event.invoke<&Playback::pan>(event.u8("pan", SemanticOperandRole::Pan), source->delta);
    }
    case 0x27: {
      auto event = beginEvent(cursor, *source, "Pan Fade", SequenceSemantic::Pan);
      const u32 duration = event.varLen("duration", SemanticOperandRole::Duration);
      const u8 target = event.u8("target", SemanticOperandRole::Pan);
      return event.invoke<&Playback::panFade>(duration, target, source->delta);
    }
    case 0x28: {
      auto event = beginEvent(cursor, *source, "Portamento Time", SequenceSemantic::Portamento);
      return event.invoke<&Playback::portamentoTime>(event.varLen("duration", SemanticOperandRole::Duration),
                                                     source->delta);
    }
    case 0x29:
      return beginEvent(cursor, *source, "Portamento Off", SequenceSemantic::Portamento)
          .invoke<&Playback::portamentoOff>(source->delta);
    case 0x2a:
      return beginEvent(cursor, *source, "Legato On", SequenceSemantic::State)
          .invoke([delta = source->delta](Playback& playback) {
            playback.delayed(delta).legatoPedal(true);
            return playback.after(delta);
          });
    case 0x2b:
      return beginEvent(cursor, *source, "Legato Off", SequenceSemantic::State)
          .invoke([delta = source->delta](Playback& playback) {
            playback.delayed(delta).legatoPedal(false);
            return playback.after(delta);
          });
    case 0x2c: {
      auto event = beginEvent(cursor, *source, "Pitch Slide", SequenceSemantic::Pitch);
      const u32 duration = event.varLen("duration", SemanticOperandRole::Duration);
      const s8 semitones = event.s8("semitones", SemanticOperandRole::Pitch);
      return event.invoke(
          [delta = source->delta](Playback& playback, u32 ticks, s8 pitch) {
            const u64 at = playback.eventTick(delta);
            auto emitter = playback.out.at(at);
            emitter.fade(PerformanceAutomationTarget::Pitch, pitch, ticks).at(emitter, at + ticks).pitchBend(pitch);
            return playback.after(delta);
          },
          duration, semitones);
    }
    case 0x2d:
      return sourceOnly(cursor, *source, "Voice Flag", SequenceSemantic::State, 1);
    case 0x2e:
      return sourceOnly(cursor, *source, "Voice Flag On", SequenceSemantic::State, 0);
    case 0x2f:
      return sourceOnly(cursor, *source, "Voice Flag Off", SequenceSemantic::State, 0);
    case 0x30:
      return beginEvent(cursor, *source, "ADSR Reset", SequenceSemantic::Envelope)
          .invoke<&Playback::resetEnvelope>(source->delta);
    case 0x31:
    case 0x32:
    case 0x33:
    case 0x34:
    case 0x35:
    case 0x36:
    case 0x37:
    case 0x38: {
      static constexpr std::array labels{"Attack Rate",  "Decay Rate",  "Sustain Level", "Sustain Rate",
                                         "Release Rate", "Attack Mode", "Sustain Mode",  "Release Mode"};
      auto event = beginEvent(cursor, *source, labels[source->status - 0x31], SequenceSemantic::Envelope);
      const u8 value = event.u8(source->status >= 0x36 ? "mode" : "value");
      return event.invoke<&Playback::envelopeParameter>(static_cast<u8>(source->status - 0x30), value, 0,
                                                        source->delta);
    }
    case 0x39: {
      auto event = beginEvent(cursor, *source, "Decay Rate and Sustain Level", SequenceSemantic::Envelope);
      const u8 decay = event.u8("decay_rate");
      const u8 sustain = event.u8("sustain_level");
      return event.invoke<&Playback::envelopeParameter>(9, decay, sustain, source->delta);
    }
    case 0x3c:
      return sourceOnly(cursor, *source, "Track Voice Flag", SequenceSemantic::State, 1);
    case 0x3d:
      return sourceOnly(cursor, *source, "Sequence Parameter", SequenceSemantic::State, 1);
    case 0x3e:
      return sourceOnly(cursor, *source, "Track Flag 0x20", SequenceSemantic::State, 1);
    case 0x3f:
      return sourceOnly(cursor, *source, "Track Flag 0x10", SequenceSemantic::State, 1);
    case 0x40:
    case 0x48:
    case 0x50: {
      const u8 slot = static_cast<u8>((source->status - 0x40) / 8);
      const s8 mode = static_cast<s8>(slot);
      auto event = beginEvent(cursor, *source,
                              slot == 0   ? "Vibrato"
                              : slot == 1 ? "Tremolo"
                                          : "Pan LFO",
                              SequenceSemantic::Modulation);
      const u8 depth = event.u8("depth", SemanticOperandRole::Modulation);
      const u8 rate = event.u8("rate", SemanticOperandRole::Modulation);
      const u8 wave = event.u8("wave", SemanticOperandRole::Modulation);
      return event.invoke<&Playback::lfoInitialize>(slot, mode, depth, rate, wave, source->delta);
    }
    case 0x41:
    case 0x49:
    case 0x51: {
      const u8 slot = static_cast<u8>((source->status - 0x41) / 8);
      return beginEvent(cursor, *source, "LFO Off", SequenceSemantic::Modulation)
          .invoke<&Playback::lfoOff>(slot, source->delta);
    }
    case 0x42:
    case 0x4a:
    case 0x52: {
      const u8 slot = static_cast<u8>((source->status - 0x42) / 8);
      auto event = beginEvent(cursor, *source, "LFO Depth", SequenceSemantic::Modulation);
      return event.invoke<&Playback::lfoDepthValue>(slot, event.u8("depth", SemanticOperandRole::Modulation),
                                                    source->delta);
    }
    case 0x43:
    case 0x4b:
    case 0x53: {
      const u8 slot = static_cast<u8>((source->status - 0x43) / 8);
      auto event = beginEvent(cursor, *source, "LFO Depth Fade", SequenceSemantic::Modulation);
      const u32 duration = event.varLen("duration", SemanticOperandRole::Duration);
      const u8 depth = event.u8("depth", SemanticOperandRole::Modulation);
      return event.invoke<&Playback::lfoDepthFade>(slot, duration, depth, source->delta);
    }
    case 0x44:
    case 0x4c:
    case 0x54: {
      const u8 slot = static_cast<u8>((source->status - 0x44) / 8);
      auto event = beginEvent(cursor, *source, "LFO Rate", SequenceSemantic::Modulation);
      return event.invoke<&Playback::lfoRateValue>(slot, event.u8("rate", SemanticOperandRole::Modulation),
                                                   source->delta);
    }
    case 0x45:
    case 0x4d:
    case 0x55: {
      const u8 slot = static_cast<u8>((source->status - 0x45) / 8);
      auto event = beginEvent(cursor, *source, "LFO Rate Fade", SequenceSemantic::Modulation);
      const u32 duration = event.varLen("duration", SemanticOperandRole::Duration);
      const u8 rate = event.u8("rate", SemanticOperandRole::Modulation);
      return event.invoke<&Playback::lfoRateFade>(slot, duration, rate, source->delta);
    }
    case 0x46:
    case 0x4e:
    case 0x56: {
      const u8 slot = static_cast<u8>((source->status - 0x46) / 8);
      auto event = beginEvent(cursor, *source, "LFO Waveform", SequenceSemantic::Modulation);
      return event.invoke<&Playback::lfoWave>(slot, event.u8("wave", SemanticOperandRole::Modulation), source->delta);
    }
    case 0x47:
    case 0x4f: {
      const u8 slot = static_cast<u8>((source->status - 0x47) / 8);
      auto event = beginEvent(cursor, *source, "LFO Delay and Fade", SequenceSemantic::Modulation);
      const u8 delay = event.u8("delay", SemanticOperandRole::Duration);
      const u8 fade = event.u8("fade", SemanticOperandRole::Duration);
      return event.invoke<&Playback::lfoDelay>(slot, delay, fade, source->delta);
    }
    case 0x58:
      return sourceOnly(cursor, *source, "Track Pitch Scale", SequenceSemantic::Pitch, 1);
    case 0x59: {
      auto event = beginEvent(cursor, *source, "Relative Track Pitch Scale", SequenceSemantic::Pitch,
                              CommandPlaybackStatus::SourceOnly);
      event.s8("amount", SemanticOperandRole::Pitch);
      return event.wait(source->delta);
    }
    case 0x5a: {
      auto event = beginEvent(cursor, *source, "Coarse Tuning", SequenceSemantic::Pitch);
      return event.invoke<&Playback::tuning>(static_cast<s16>(event.u8("semitones", SemanticOperandRole::Pitch)) << 8,
                                             source->delta);
    }
    case 0x5b: {
      auto event = beginEvent(cursor, *source, "Relative Coarse Tuning", SequenceSemantic::Pitch);
      const s8 amount = event.s8("semitones", SemanticOperandRole::Pitch);
      return event.invoke(
          [delta = source->delta](Playback& playback, s8 value) {
            return playback.tuning(static_cast<s16>(playback.track.coarseTuning + value * 256), delta);
          },
          amount);
    }
    case 0x5c: {
      auto event = beginEvent(cursor, *source, "Pitch Bend", SequenceSemantic::Pitch);
      const u8 lsb = event.u8("lsb", SemanticOperandRole::Pitch);
      const u8 msb = event.u8("msb", SemanticOperandRole::Pitch);
      return event.invoke<&Playback::pitchBend>(lsb, msb, source->delta);
    }
    case 0x5d: {
      auto event = beginEvent(cursor, *source, "Pitch Bend Range", SequenceSemantic::Pitch);
      return event.invoke<&Playback::bendRange>(event.s8("semitones", SemanticOperandRole::Pitch), source->delta);
    }
    case 0x60:
      return beginEvent(cursor, *source, "Wet Routing On", SequenceSemantic::State)
          .invoke<&Playback::reverb>(1.0, source->delta);
    case 0x61:
      return beginEvent(cursor, *source, "Wet Routing Off", SequenceSemantic::State)
          .invoke<&Playback::reverb>(0.0, source->delta);
    case 0x62: {
      auto event = beginEvent(cursor, *source, "Output Routing", SequenceSemantic::State);
      const u8 left = event.u8("left", SemanticOperandRole::State);
      const u8 right = event.u8("right", SemanticOperandRole::State);
      return event.invoke<&Playback::routing>(left, right, source->delta);
    }
    case 0x64:
    case 0x65:
    case 0x68:
    case 0x69:
      return sourceOnly(cursor, *source, "Voice Control Flag", SequenceSemantic::State, 0);
    case 0x6c:
      return sourceOnly(cursor, *source, "SPU2 Core", SequenceSemantic::State, 1);
    case 0x70:
    case 0x71:
      return sourceOnly(cursor, *source, "Timing Correction", SequenceSemantic::State, 1);
    case 0x72: {
      auto event = beginEvent(cursor, *source, "LFO Slot Off", SequenceSemantic::Modulation);
      return event.invoke<&Playback::lfoOff>(event.u8("slot") & 3, source->delta);
    }
    case 0x73: {
      auto event = beginEvent(cursor, *source, "LFO Slot Delay and Fade", SequenceSemantic::Modulation);
      const u8 slot = event.u8("slot");
      const u8 delay = event.u8("delay", SemanticOperandRole::Duration);
      const u8 fade = event.u8("fade", SemanticOperandRole::Duration);
      return event.invoke<&Playback::lfoDelay>(slot, delay, fade, source->delta);
    }
    case 0x74: {
      auto event = beginEvent(cursor, *source, "LFO Slot Mode and Waveform", SequenceSemantic::Modulation);
      const u8 slot = event.u8("slot");
      const u8 mode = event.u8("mode", SemanticOperandRole::Modulation);
      const u8 wave = event.u8("wave", SemanticOperandRole::Modulation);
      return event.invoke<&Playback::lfoModeWave>(slot, mode, wave, source->delta);
    }
    case 0x75: {
      auto event = beginEvent(cursor, *source, "LFO Slot Depth and Rate", SequenceSemantic::Modulation);
      const u8 slot = event.u8("slot");
      const u8 depth = event.u8("depth", SemanticOperandRole::Modulation);
      const u8 rate = event.u8("rate", SemanticOperandRole::Modulation);
      return event.invoke(
          [delta = source->delta](Playback& playback, u8 index, u8 d, u8 r) {
            auto effects = playback.lfoDepthValue(index, d, delta);
            playback.lfoRateValue(index, r, delta);
            return effects;
          },
          slot, depth, rate);
    }
    case 0x76:
    case 0x77: {
      auto event = beginEvent(cursor, *source, source->status == 0x76 ? "LFO Slot Depth Fade" : "LFO Slot Rate Fade",
                              SequenceSemantic::Modulation);
      const u8 slot = event.u8("slot");
      const u32 duration = event.varLen("duration", SemanticOperandRole::Duration);
      const u8 target = event.u8("target", SemanticOperandRole::Modulation);
      return source->status == 0x76 ? event.invoke<&Playback::lfoDepthFade>(slot, duration, target, source->delta)
                                    : event.invoke<&Playback::lfoRateFade>(slot, duration, target, source->delta);
    }
    case 0x78:
      return sourceOnly(cursor, *source, "External Counter Set", SequenceSemantic::State, 2);
    case 0x79:
      return sourceOnly(cursor, *source, "External Counter Clear", SequenceSemantic::State, 1);
    case 0x7c:
      return sourceOnly(cursor, *source, "Driver Table Jump", SequenceSemantic::Jump, 1);
    case 0x7d:
      return sourceOnly(cursor, *source, "Driver Table Call", SequenceSemantic::Call, 1);
    case 0x7e:
    case 0x7f:
      return beginEvent(cursor, *source, "No Operation", SequenceSemantic::Meta).wait(source->delta);
    default:
      return beginEvent(cursor, *source, "Undefined Event", SequenceSemantic::End, CommandPlaybackStatus::StopsPlayback)
          .wait(source->delta)
          .end();
  }
}

[[nodiscard]] TrackProgram decodeTrack(ByteReader reader, AssetId sequence, u32 trackIndex,
                                       const BgmTrackLayout& source, SourceMapBuilder* sourceMap,
                                       std::vector<Diagnostic>* diagnostics) {
  const u32 end = source.dataOffset + source.length;
  const TrackLayout layout = analyzeTrack(reader, source.dataOffset, end);
  const TrackDecodeScope tracks{
      .reader = reader,
      .bytecodeEnd = end,
      .maxCommands = kMaxCommands,
      .sequenceAsset = sequence,
      .sourceMap = sourceMap,
  };
  return tracks.decode(trackIndex, source.dataOffset,
                       [&](u32 offset) { return decodeCommand(reader, offset, end, layout, diagnostics); });
}

}  // namespace

const SequenceProgramConfig& sequenceConfig() {
  static const SequenceProgramConfig config{
      .commandKindPrefix = std::string(kSquarePs2CommandKindPrefix),
      .timebase = Timebase{.ppqn = 48},
      .behavior =
          SequenceProgramBehavior{
              .commandLimit = kMaxCommands,
              .panLaw = PanLaw::EqualPower,
              .initialLevel = 1.0,
              .initialMasterLevel = 1.0,
              .initialExpression = 1.0,
              .initialReverbSend = 0.0,
              .initialPitchBendRangeSemitones = 2,
              .initialTempoMicrosecondsPerQuarter = 500'000,
          },
  };
  return config;
}

SequenceRuntime sequenceRuntime(RuntimeConfig config) {
  return makeCompiledRuntime<Cursor, ProgramState>(std::move(config));
}

SequenceProgram parseBgm(ByteReader reader, AssetId id, const BgmLayout& layout, RuntimeConfig config,
                         SourceMapBuilder* sourceMap, std::vector<Diagnostic>* diagnostics) {
  SequenceProgram program = sequenceConfig().makeProgram();
  program.timebase.ppqn = layout.ppqn;
  if (const u32 initialTempo = tempoMicros(layout.initialTempo); initialTempo != 0) {
    program.behavior.initialTempoMicrosecondsPerQuarter = initialTempo;
  }
  program.behavior.initialMasterLevel = controller(static_cast<s8>(layout.initialMasterLevel));
  if (config.defaultBank == 0) {
    config.defaultBank = layout.waveBankId;
  }
  program.runtime = sequenceRuntime(std::move(config));

  if (sourceMap != nullptr) {
    sourceMap->header("SquarePS2 BGM Header", reader.range(layout.offset, 0x20))
        .kind("square-ps2-bgm-header")
        .owner(ObjectRefs::sequence(id))
        .field("sequence_id", reader.range(layout.offset + 4, 2), layout.sequenceId)
        .field("wave_bank_id", reader.range(layout.offset + 6, 2), layout.waveBankId)
        .field("track_count", reader.range(layout.offset + 8, 1), layout.trackCount)
        .field("initial_tempo", reader.range(layout.offset + 0x0a, 2), layout.initialTempo)
        .field("initial_master_level", reader.range(layout.offset + 0x0c, 1), layout.initialMasterLevel)
        .field("ppqn", reader.range(layout.offset + 0x0e, 2), layout.ppqn)
        .field("length", reader.range(layout.offset + 0x10, 4), layout.declaredLength)
        .field("flags", reader.range(layout.offset + 0x14, 4), layout.flags, SourceValueDisplay::Hex);
  }

  for (u32 index = 0; index < layout.tracks.size(); ++index) {
    if (sourceMap != nullptr) {
      sourceMap->table("Track Block", reader.range(layout.tracks[index].blockOffset, 4))
          .kind("square-ps2-track-block")
          .owner(ObjectRefs::sequence(id))
          .field("length", reader.range(layout.tracks[index].blockOffset, 4), layout.tracks[index].length);
    }
    auto track = decodeTrack(reader, id, index, layout.tracks[index], sourceMap, diagnostics);
    track.sourceTrackNumber = index;
    program.tracks.push_back(std::move(track));
  }
  return program;
}

}  // namespace vgmtrans::formats::square_ps2
