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

struct VarLen {
  u32 value = 0;
  u32 size = 0;
};

[[nodiscard]] std::optional<VarLen> readVarLen(ByteReader reader, u32 offset, u32 end) {
  VarLen result;
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

struct EventPrefix {
  u32 delta = 0;
  u32 deltaSize = 0;
  u8 status = 0;
};

[[nodiscard]] std::optional<EventPrefix> readEventPrefix(ByteReader reader, u32 begin, u32 end) {
  const auto delta = readVarLen(reader, begin, end);
  if (!delta || begin + delta->size >= end) {
    return std::nullopt;
  }
  return EventPrefix{
      .delta = delta->value,
      .deltaSize = delta->size,
      .status = reader.u8At(begin + delta->size),
  };
}

struct RepeatInfo {
  Address start;
  u8 slot = 0;
};

struct TrackLayout {
  std::map<u32, RepeatInfo> repeatTargets;
};

[[nodiscard]] DecodedBytecodeCommand decodeCommand(ByteReader reader, u32 begin, u32 end, const TrackLayout& layout,
                                                   u32 trackIndex, std::vector<Diagnostic>* diagnostics);

[[nodiscard]] TrackLayout analyzeTrack(ByteReader reader, u32 begin, u32 end) {
  TrackLayout result;
  std::vector<RepeatInfo> repeats;
  for (u32 offset = begin; offset < end;) {
    const auto event = readEventPrefix(reader, offset, end);
    if (!event) {
      break;
    }
    const auto command = decodeCommand(reader, offset, end, {}, 0, nullptr);
    const u32 next = offset + command.range.size;
    if (next <= offset || next > end) {
      break;
    }
    switch (event->status) {
      case 0x04:
        repeats.push_back(RepeatInfo{
            .start = Address{next},
            .slot = static_cast<u8>(std::min<std::size_t>(repeats.size(), 3)),
        });
        break;
      case 0x05:
        if (!repeats.empty()) {
          result.repeatTargets.emplace(offset, repeats.back());
          repeats.pop_back();
        }
        break;
      case 0x06:
        if (!repeats.empty()) {
          result.repeatTargets.emplace(offset, repeats.back());
        }
        break;
      default:
        break;
    }
    offset = next;
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
  static constexpr std::array waveforms{LfoWaveform::Sine,   LfoWaveform::Triangle, LfoWaveform::Square,
                                        LfoWaveform::Square, LfoWaveform::Noise,    LfoWaveform::Square,
                                        LfoWaveform::Square, LfoWaveform::Square};
  return LfoShape{.waveform = waveforms[shape & 7]};
}

struct LfoState {
  s8 mode = -1;
  u8 wave = 0;
  u8 depth = 0;
  u8 rate = 0;
  u8 delay = 0;
  u8 fade = 0;
};

struct TrackState {
  explicit TrackState(const RuntimeConfig& config) : bank(config.defaultBank), envelopes(config.envelopes) {
    loadEnvelope();
  }

  [[nodiscard]] const EnvelopeDefaults* envelope() const {
    const auto found = std::ranges::find_if(
        envelopes, [&](const EnvelopeDefaults& value) { return value.bank == bank && value.program == program; });
    return found == envelopes.end() ? nullptr : &*found;
  }

  void loadEnvelope() {
    const auto* defaults = envelope();
    hasEnvelope = defaults != nullptr;
    if (defaults != nullptr) {
      adsr1 = defaults->adsr1;
      adsr2 = defaults->adsr2;
    }
  }

  u16 bank = 0;
  u8 program = 0;
  u8 previousKey = 60;
  std::optional<u8> previousPitchKey;
  u8 previousVelocity = 127;
  s8 pitchBendRange = 2;
  s16 coarseTuning = 0;
  u32 portamentoTicks = 0;
  bool hasEnvelope = false;
  bool portamento = false;
  bool legato = false;
  u16 adsr1 = 0;
  u16 adsr2 = 0;
  std::array<LfoState, 4> lfos{};
  std::span<const EnvelopeDefaults> envelopes;
};

struct AdsrParameter {
  std::string_view label;
  u16 TrackState::* word;
  u16 mask;
  u8 shift;
};

constexpr std::array kAdsrParameters{
    AdsrParameter{"Attack Rate", &TrackState::adsr1, 0x7f00, 8},
    AdsrParameter{"Decay Rate", &TrackState::adsr1, 0x00f0, 4},
    AdsrParameter{"Sustain Level", &TrackState::adsr1, 0x000f, 0},
    AdsrParameter{"Sustain Rate", &TrackState::adsr2, 0x1fc0, 6},
    AdsrParameter{"Release Rate", &TrackState::adsr2, 0x001f, 0},
    AdsrParameter{"Attack Mode", &TrackState::adsr1, 0x8000, 13},
    AdsrParameter{"Sustain Mode", &TrackState::adsr2, 0xc000, 13},
    AdsrParameter{"Release Mode", &TrackState::adsr2, 0x0020, 3},
};

struct Playback {
  TrackState& track;
  PerformanceEmitter& out;
  VmApi& vm;

  [[nodiscard]] u64 eventTick() const { return vm.tick(); }

  [[nodiscard]] PerformanceEmitter atEvent() const { return out; }

  void publishEnvelope(PerformanceEmitter& emitter) {
    if (track.hasEnvelope) {
      emitter.replaceEnvelope(psxSpuEnvelope(track.adsr1, track.adsr2, PsxSpuGeneration::Ps2),
                              VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks);
    }
  }

  void noteOn(u8 key, u8 velocity) {
    auto emitter = atEvent();
    const bool glides =
        track.portamento && track.previousPitchKey && track.portamentoTicks != 0 && *track.previousPitchKey != key;
    if (glides && !track.legato) {
      // Only Legato On prevents the driver from activating a new destination
      // voice. Otherwise, end the overlap so MIDI portamento also activates one.
      emitter.noteOff(*track.previousPitchKey);
    }
    const PerformanceNoteId note = emitter.noteOn(key, controller(static_cast<s8>(std::min<u8>(velocity, 127))));
    if (glides) {
      // Pitch bend would also retune unrelated and releasing voices.
      emitter.pitchSlide(note, *track.previousPitchKey, key, track.portamentoTicks).requirePortamento();
    }
    track.previousKey = key;
    track.previousPitchKey = key;
    track.previousVelocity = velocity;
  }

  void noteOff(u8 key) {
    track.previousKey = key;
    atEvent().noteOff(key);
  }

  void noteOffPrevious() { noteOff(track.previousKey); }

  void selectProgram(u16 bank, u8 program) {
    track.bank = bank;
    track.program = program;
    resetEnvelope();
  }

  void resetEnvelope() {
    track.loadEnvelope();
    auto emitter = atEvent();
    emitter.instrument(instrumentIdentity(track.bank, track.program), InstrumentEnvelopeMode::UseInstrumentEnvelope);
    emitter.restoreEnvelope(EnvelopeFields::All, VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks);
  }

  void envelopeParameter(u8 parameter, u8 value, u8 second) {
    if (parameter >= 1 && parameter <= kAdsrParameters.size()) {
      const auto& field = kAdsrParameters[parameter - 1];
      auto& word = track.*field.word;
      word = static_cast<u16>((word & ~field.mask) | ((static_cast<u16>(value) << field.shift) & field.mask));
    } else if (parameter == 9) {
      track.adsr1 = static_cast<u16>((track.adsr1 & ~0x00ffu) | ((value & 0x0f) << 4) | (second & 0x0f));
    }
    auto emitter = atEvent();
    publishEnvelope(emitter);
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

  void emitLfoDepth(PerformanceEmitter& emitter, const LfoState& lfo, bool restart = false, double depthScale = 1.0) {
    auto context = lfoContext(lfo, restart);
    switch (lfo.mode) {
      case 0:
        emitter.vibratoDepth(vibratoDepth(lfo.depth) * depthScale, std::move(context));
        break;
      case 1:
        emitter.tremoloLinearGainDepth(tremoloDepth(lfo.depth) * depthScale, std::move(context));
        break;
      case 2:
        emitter.panLfoDepth(panLfoDepth(lfo.depth) * depthScale, std::move(context));
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

  void lfoOff(u8 slot) {
    auto& lfo = track.lfos[slot & 3];
    auto emitter = atEvent();
    lfo.depth = 0;
    emitLfoDepth(emitter, lfo, false, 0.0);
    lfo.mode = -1;
  }

  void lfoDepthValue(u8 slot, u8 depth) {
    auto& lfo = track.lfos[slot & 3];
    lfo.depth = depth;
    auto emitter = atEvent();
    emitLfoDepth(emitter, lfo);
  }

  void lfoDepthFade(u8 slot, u32 duration, u8 depth) {
    auto& lfo = track.lfos[slot & 3];
    lfo.depth = depth;
    const u64 tick = eventTick();
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
  }

  void lfoRateValue(u8 slot, u8 rate) {
    auto& lfo = track.lfos[slot & 3];
    lfo.rate = rate;
    auto emitter = atEvent();
    emitLfoRate(emitter, lfo);
  }

  void lfoRateFade(u8 slot, u32 duration, u8 rate) {
    auto& lfo = track.lfos[slot & 3];
    lfo.rate = rate;
    auto emitter = out.at(eventTick() + duration);
    emitLfoRate(emitter, lfo);
  }

  void lfoDelay(u8 slot, u8 delay, u8 fade) {
    auto& lfo = track.lfos[slot & 3];
    lfo.delay = delay;
    lfo.fade = fade;
    auto emitter = atEvent();
    emitLfoDepth(emitter, lfo, true);
  }

  void controllerFade(u8 kind, u32 duration, s8 raw) {
    const u64 tick = eventTick();
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
  }

  void tuning(s16 semitones256) {
    track.coarseTuning = semitones256;
    atEvent().tuning(semitones256 * (100.0 / 256.0));
  }
};

using Cursor = CompilerCursor<TrackState, Playback>;

[[nodiscard]] Cursor::Event beginEvent(Cursor& cursor, const EventPrefix& source, std::string_view label,
                                       SequenceSemantic semantic,
                                       CommandPlaybackStatus playback = CommandPlaybackStatus::AffectsPlayback) {
  auto event = cursor.command(label, semantic, playback);
  event.opcodeValue("delta_byte_0", cursor.opcode(), SourceValueDisplay::Hex, SemanticOperandRole::Duration);
  for (u32 i = 1; i < source.deltaSize; ++i) {
    event.u8("delta_byte", SourceValueDisplay::Hex, SemanticOperandRole::Duration);
  }
  event.derived("delta", source.delta, SemanticOperandRole::Duration);
  event.u8("status", SourceValueDisplay::Hex);
  return event.delay(source.delta);
}

[[nodiscard]] DecodedBytecodeCommand sourceOnly(Cursor& cursor, const EventPrefix& source, std::string_view label,
                                                u32 parameters = 0,
                                                SequenceSemantic semantic = SequenceSemantic::State) {
  auto event = beginEvent(cursor, source, label, semantic, CommandPlaybackStatus::SourceOnly);
  if (parameters != 0) {
    event.rawBytes("parameters", parameters);
  }
  return event;
}

[[nodiscard]] DecodedBytecodeCommand decodeLfoCommand(Cursor& cursor, const EventPrefix& source) {
  const u8 slot = static_cast<u8>((source.status - 0x40) / 8);
  switch (source.status & 7) {
    case 0: {
      static constexpr std::array names{"Vibrato", "Tremolo", "Pan LFO"};
      auto event = beginEvent(cursor, source, names[slot], SequenceSemantic::Modulation);
      const u8 depth = event.u8("depth", SemanticOperandRole::Modulation);
      const u8 rate = event.u8("rate", SemanticOperandRole::Modulation);
      const u8 wave = event.u8("wave", SemanticOperandRole::Modulation);
      return event.invoke([slot, depth, rate, wave](Playback& playback) {
        auto& lfo = playback.track.lfos[slot & 3];
        lfo.mode = static_cast<s8>(slot);
        lfo.depth = depth;
        lfo.rate = rate;
        lfo.wave = wave & 0x0f;
        auto emitter = playback.atEvent();
        playback.emitLfoDepth(emitter, lfo, true);
        playback.emitLfoRate(emitter, lfo);
      });
    }
    case 1:
      return beginEvent(cursor, source, "LFO Off", SequenceSemantic::Modulation).invoke<&Playback::lfoOff>(slot);
    case 2: {
      auto event = beginEvent(cursor, source, "LFO Depth", SequenceSemantic::Modulation);
      return event.invoke<&Playback::lfoDepthValue>(slot, event.u8("depth", SemanticOperandRole::Modulation));
    }
    case 3: {
      auto event = beginEvent(cursor, source, "LFO Depth Fade", SequenceSemantic::Modulation);
      const u32 duration = event.varLen("duration", SemanticOperandRole::Duration);
      const u8 depth = event.u8("depth", SemanticOperandRole::Modulation);
      return event.invoke<&Playback::lfoDepthFade>(slot, duration, depth);
    }
    case 4: {
      auto event = beginEvent(cursor, source, "LFO Rate", SequenceSemantic::Modulation);
      return event.invoke<&Playback::lfoRateValue>(slot, event.u8("rate", SemanticOperandRole::Modulation));
    }
    case 5: {
      auto event = beginEvent(cursor, source, "LFO Rate Fade", SequenceSemantic::Modulation);
      const u32 duration = event.varLen("duration", SemanticOperandRole::Duration);
      const u8 rate = event.u8("rate", SemanticOperandRole::Modulation);
      return event.invoke<&Playback::lfoRateFade>(slot, duration, rate);
    }
    case 6: {
      auto event = beginEvent(cursor, source, "LFO Waveform", SequenceSemantic::Modulation);
      const u8 wave = event.u8("wave", SemanticOperandRole::Modulation);
      return event.invoke([slot, wave](Playback& playback) {
        auto& lfo = playback.track.lfos[slot & 3];
        lfo.wave = wave & 0x0f;
        auto emitter = playback.atEvent();
        playback.emitLfoDepth(emitter, lfo, true);
      });
    }
    case 7: {
      auto event = beginEvent(cursor, source, "LFO Delay and Fade", SequenceSemantic::Modulation);
      const u8 delay = event.u8("delay", SemanticOperandRole::Duration);
      const u8 fade = event.u8("fade", SemanticOperandRole::Duration);
      return event.invoke<&Playback::lfoDelay>(slot, delay, fade);
    }
    default:
      return cursor.unsupported("Undefined LFO Event").stop();
  }
}

[[nodiscard]] DecodedBytecodeCommand decodeCommand(ByteReader reader, u32 begin, u32 end, const TrackLayout& layout,
                                                   u32 trackIndex, std::vector<Diagnostic>* diagnostics) {
  Cursor cursor(reader, begin, end, kSquarePs2CommandKindPrefix, diagnostics);
  if (!cursor.hasOpcode()) {
    return cursor.truncated();
  }
  const auto source = readEventPrefix(reader, begin, end);
  if (!source) {
    return cursor.unsupported("Truncated SquarePS2 Event").stop();
  }
  if (source->status >= 0x40 && source->status <= 0x56) {
    return decodeLfoCommand(cursor, *source);
  }

  switch (source->status) {
    case 0x00:
      return beginEvent(cursor, *source, "End of Track", SequenceSemantic::End, CommandPlaybackStatus::StopsPlayback)
          .end();
    case 0x01:
      return beginEvent(cursor, *source, "Section Marker", SequenceSemantic::Meta);
    case 0x02: {
      auto event = beginEvent(cursor, *source, "Sequence Loop Point", SequenceSemantic::Loop);
      // The driver takes the song-wide snapshot only from source track 1.
      if (trackIndex == 1) {
        return event.synchronizedLoopStart();
      }
      return event;
    }
    case 0x03:
      return beginEvent(cursor, *source, "Sequence Loop", SequenceSemantic::Loop).synchronizedLoopEnd();
    case 0x04:
      return beginEvent(cursor, *source, "Repeat Begin", SequenceSemantic::Repeat);
    case 0x05: {
      auto event = beginEvent(cursor, *source, "Repeat End", SequenceSemantic::Repeat);
      const u8 count = event.u8("count", SemanticOperandRole::Count);
      const auto found = layout.repeatTargets.find(begin);
      if (found == layout.repeatTargets.end()) {
        return event;
      }
      event.derived("total_plays", totalPlays(count), SemanticOperandRole::Count);
      event.derived("destination", found->second.start, SourceValueDisplay::Address, SemanticOperandRole::RepeatTarget);
      return event.repeatUntil(found->second.slot, totalPlays(count), found->second.start);
    }
    case 0x06: {
      auto event = beginEvent(cursor, *source, "Repeat Forever", SequenceSemantic::Loop);
      const auto found = layout.repeatTargets.find(begin);
      if (found == layout.repeatTargets.end()) {
        return event.end();
      }
      event.derived("destination", found->second.start, SourceValueDisplay::Address, SemanticOperandRole::LoopTarget);
      return event.declaredLoop(found->second.start);
    }
    case 0x07:
      return sourceOnly(cursor, *source, "External Repeat Condition", 1, SequenceSemantic::RepeatBreak);
    case 0x08: {
      auto event = beginEvent(cursor, *source, "Tempo", SequenceSemantic::Tempo);
      const u8 raw = event.u8("tempo");
      event.derived("beats_per_minute", static_cast<double>(raw), SourceValueDisplay::BeatsPerMinute);
      return event.invoke([raw](Playback& playback) {
        if (const u32 micros = tempoMicros(raw); micros != 0) {
          playback.atEvent().tempo(micros);
        }
      });
    }
    case 0x09: {
      auto event = beginEvent(cursor, *source, "Tempo Fade", SequenceSemantic::Tempo);
      const u8 duration = event.u8("duration", SemanticOperandRole::Duration);
      const u8 target = event.u8("target");
      event.derived("target_beats_per_minute", static_cast<double>(target), SourceValueDisplay::BeatsPerMinute);
      return event.invoke([duration, target](Playback& playback) {
        if (const u32 micros = tempoMicros(target); micros != 0) {
          const u64 tick = playback.eventTick();
          auto emitter = playback.out.at(tick);
          emitter.fade(PerformanceAutomationTarget::Tempo, micros, duration).at(emitter, tick + duration).tempo(micros);
        }
      });
    }
    case 0x0a: {
      auto event = beginEvent(cursor, *source, "Master Level", SequenceSemantic::Level);
      const s8 level = event.s8("level", SemanticOperandRole::Level);
      return event.invoke([level](Playback& playback) { playback.atEvent().masterLevel(controller(level)); });
    }
    case 0x0b: {
      auto event = beginEvent(cursor, *source, "Master Level Fade", SequenceSemantic::Level);
      const u8 duration = event.u8("duration", SemanticOperandRole::Duration);
      const s8 target = event.s8("target", SemanticOperandRole::Level);
      return event.invoke<&Playback::controllerFade>(1, duration, target);
    }
    case 0x0c: {
      auto event = beginEvent(cursor, *source, "Time Signature", SequenceSemantic::Meta);
      const u8 numerator = event.u8("numerator");
      const u8 denominator = event.u8("denominator");
      return event.invoke([numerator, denominator](Playback& playback) {
        if (numerator != 0 && denominator != 0) {
          playback.atEvent().timeSignature(numerator, denominator, 48);
        }
      });
    }
    case 0x0d:
      return sourceOnly(cursor, *source, "Driver Mix Preset", 1);
    case 0x0e:
      return sourceOnly(cursor, *source, "Reserved Parameter", 1);
    case 0x10:
      return beginEvent(cursor, *source, "Note On (Previous Key and Velocity)", SequenceSemantic::Note)
          .invoke(
              [](Playback& playback) { playback.noteOn(playback.track.previousKey, playback.track.previousVelocity); });
    case 0x11: {
      auto event = beginEvent(cursor, *source, "Note On", SequenceSemantic::Note);
      const u8 key = event.u8("key", SourceValueDisplay::MidiNote, SemanticOperandRole::NoteKey);
      const u8 velocity = event.u8("velocity", SemanticOperandRole::Level);
      return event.invoke<&Playback::noteOn>(key, velocity);
    }
    case 0x12: {
      auto event = beginEvent(cursor, *source, "Note On (Previous Velocity)", SequenceSemantic::Note);
      const u8 key = event.u8("key", SourceValueDisplay::MidiNote, SemanticOperandRole::NoteKey);
      return event.invoke([key](Playback& playback) { playback.noteOn(key, playback.track.previousVelocity); });
    }
    case 0x13: {
      auto event = beginEvent(cursor, *source, "Note On (Previous Key)", SequenceSemantic::Note);
      const u8 velocity = event.u8("velocity", SemanticOperandRole::Level);
      return event.invoke([velocity](Playback& playback) { playback.noteOn(playback.track.previousKey, velocity); });
    }
    case 0x18:
      return beginEvent(cursor, *source, "Note Off (Previous Key)", SequenceSemantic::Note)
          .invoke<&Playback::noteOffPrevious>();
    case 0x19: {
      auto event = beginEvent(cursor, *source, "Note Off", SequenceSemantic::Note);
      const u8 key = event.u8("key", SourceValueDisplay::MidiNote, SemanticOperandRole::NoteKey);
      event.u8("release_parameter");
      return event.invoke<&Playback::noteOff>(key);
    }
    case 0x1a: {
      auto event = beginEvent(cursor, *source, "Note Off", SequenceSemantic::Note);
      return event.invoke<&Playback::noteOff>(
          event.u8("key", SourceValueDisplay::MidiNote, SemanticOperandRole::NoteKey));
    }
    case 0x1b: {
      auto event = beginEvent(cursor, *source, "Note Off (Previous Key)", SequenceSemantic::Note);
      event.u8("release_parameter");
      return event.invoke<&Playback::noteOffPrevious>();
    }
    case 0x20: {
      auto event = beginEvent(cursor, *source, "Program Change", SequenceSemantic::Program);
      const u8 program = event.u8("program", SemanticOperandRole::InstrumentProgram);
      return event.invoke([program](Playback& playback) { playback.selectProgram(playback.track.bank, program); });
    }
    case 0x21: {
      auto event = beginEvent(cursor, *source, "Bank and Program Change", SequenceSemantic::Program);
      const u8 bank = event.u8("bank", SemanticOperandRole::InstrumentBank);
      const u8 program = event.u8("program", SemanticOperandRole::InstrumentProgram);
      return event.invoke<&Playback::selectProgram>(bank, program);
    }
    case 0x22: {
      auto event = beginEvent(cursor, *source, "Level", SequenceSemantic::Level);
      const s8 level = event.s8("level", SemanticOperandRole::Level);
      return event.invoke([level](Playback& playback) {
        playback.atEvent().level(controller(level), ValueQuantization{.levels = 128});
      });
    }
    case 0x23: {
      auto event = beginEvent(cursor, *source, "Level Fade", SequenceSemantic::Level);
      const u32 duration = event.varLen("duration", SemanticOperandRole::Duration);
      const s8 target = event.s8("target", SemanticOperandRole::Level);
      return event.invoke<&Playback::controllerFade>(0, duration, target);
    }
    case 0x24: {
      auto event = beginEvent(cursor, *source, "Expression", SequenceSemantic::Level);
      const s8 expression = event.s8("expression", SemanticOperandRole::Level);
      return event.invoke([expression](Playback& playback) {
        playback.atEvent().expression(controller(expression), ValueQuantization{.levels = 128});
      });
    }
    case 0x25: {
      auto event = beginEvent(cursor, *source, "Expression Fade", SequenceSemantic::Level);
      const u32 duration = event.varLen("duration", SemanticOperandRole::Duration);
      const s8 target = event.s8("target", SemanticOperandRole::Level);
      return event.invoke<&Playback::controllerFade>(2, duration, target);
    }
    case 0x26: {
      auto event = beginEvent(cursor, *source, "Pan", SequenceSemantic::Pan);
      const u8 pan = event.u8("pan", SemanticOperandRole::Pan);
      return event.invoke([pan](Playback& playback) { playback.atEvent().pan(panPosition(pan)); });
    }
    case 0x27: {
      auto event = beginEvent(cursor, *source, "Pan Fade", SequenceSemantic::Pan);
      const u32 duration = event.varLen("duration", SemanticOperandRole::Duration);
      const u8 target = event.u8("target", SemanticOperandRole::Pan);
      return event.invoke([duration, target](Playback& playback) {
        const u64 tick = playback.eventTick();
        auto emitter = playback.out.at(tick);
        const double pan = panPosition(target);
        emitter.fade(PerformanceAutomationTarget::Pan, pan, duration).at(emitter, tick + duration).pan(pan);
      });
    }
    case 0x28: {
      auto event = beginEvent(cursor, *source, "Portamento Time", SequenceSemantic::Portamento);
      const u32 duration = event.varLen("duration", SemanticOperandRole::Duration);
      return event.invoke([duration](Playback& playback) {
        playback.track.portamentoTicks = duration;
        playback.track.portamento = true;
        playback.atEvent().portamentoEnable(true);
      });
    }
    case 0x29:
      return beginEvent(cursor, *source, "Portamento Off", SequenceSemantic::Portamento).invoke([](Playback& playback) {
        playback.track.portamento = false;
        playback.track.previousPitchKey.reset();
        playback.atEvent().portamentoEnable(false);
      });
    case 0x2a:
    case 0x2b: {
      const bool enabled = source->status == 0x2a;
      return beginEvent(cursor, *source, enabled ? "Legato On" : "Legato Off", SequenceSemantic::State)
          .invoke([enabled](Playback& playback) {
            playback.track.legato = enabled;
            playback.atEvent().legatoPedal(enabled);
          });
    }
    case 0x2c: {
      auto event = beginEvent(cursor, *source, "Pitch Slide", SequenceSemantic::Pitch);
      const u32 duration = event.varLen("duration", SemanticOperandRole::Duration);
      const s8 semitones = event.s8("semitones", SemanticOperandRole::Pitch);
      return event.invoke([duration, semitones](Playback& playback) {
        const u64 tick = playback.eventTick();
        auto emitter = playback.out.at(tick);
        emitter.fade(PerformanceAutomationTarget::Pitch, semitones, duration)
            .at(emitter, tick + duration)
            .pitchBend(semitones);
      });
    }
    case 0x2d:
      return sourceOnly(cursor, *source, "Voice Flag", 1);
    case 0x2e:
    case 0x2f:
      return sourceOnly(cursor, *source, source->status == 0x2e ? "Voice Flag On" : "Voice Flag Off");
    case 0x30:
      return beginEvent(cursor, *source, "ADSR Reset", SequenceSemantic::Envelope).invoke<&Playback::resetEnvelope>();
    case 0x31:
    case 0x32:
    case 0x33:
    case 0x34:
    case 0x35:
    case 0x36:
    case 0x37:
    case 0x38: {
      const auto& parameter = kAdsrParameters[source->status - 0x31];
      auto event = beginEvent(cursor, *source, parameter.label, SequenceSemantic::Envelope);
      const u8 value = event.u8(source->status >= 0x36 ? "mode" : "value");
      return event.invoke<&Playback::envelopeParameter>(static_cast<u8>(source->status - 0x30), value, 0);
    }
    case 0x39: {
      auto event = beginEvent(cursor, *source, "Decay Rate and Sustain Level", SequenceSemantic::Envelope);
      const u8 decay = event.u8("decay_rate");
      const u8 sustain = event.u8("sustain_level");
      return event.invoke<&Playback::envelopeParameter>(9, decay, sustain);
    }
    case 0x3c: {
      auto event = beginEvent(cursor, *source, "Sustain Pedal", SequenceSemantic::State);
      const bool enabled = event.u8("enabled", SourceValueDisplay::Boolean, SemanticOperandRole::State) != 0;
      return event.invoke([enabled](Playback& playback) { playback.atEvent().sustainPedal(enabled); });
    }
    case 0x3d:
      return sourceOnly(cursor, *source, "Sequence Parameter", 1);
    case 0x3e:
      return sourceOnly(cursor, *source, "Track Flag 0x20", 1);
    case 0x3f:
      return sourceOnly(cursor, *source, "Track Flag 0x10", 1);
    case 0x58:
      return sourceOnly(cursor, *source, "Track Pitch Scale", 1, SequenceSemantic::Pitch);
    case 0x59: {
      auto event = beginEvent(cursor, *source, "Relative Track Pitch Scale", SequenceSemantic::Pitch,
                              CommandPlaybackStatus::SourceOnly);
      event.s8("amount", SemanticOperandRole::Pitch);
      return event;
    }
    case 0x5a: {
      auto event = beginEvent(cursor, *source, "Coarse Tuning", SequenceSemantic::Pitch);
      return event.invoke<&Playback::tuning>(static_cast<s16>(event.u8("semitones", SemanticOperandRole::Pitch)) << 8);
    }
    case 0x5b: {
      auto event = beginEvent(cursor, *source, "Relative Coarse Tuning", SequenceSemantic::Pitch);
      const s8 amount = event.s8("semitones", SemanticOperandRole::Pitch);
      return event.invoke([amount](Playback& playback) {
        playback.tuning(static_cast<s16>(playback.track.coarseTuning + amount * 256));
      });
    }
    case 0x5c: {
      auto event = beginEvent(cursor, *source, "Pitch Bend", SequenceSemantic::Pitch);
      const u8 lsb = event.u8("lsb", SemanticOperandRole::Pitch);
      const u8 msb = event.u8("msb", SemanticOperandRole::Pitch);
      return event.invoke([lsb, msb](Playback& playback) {
        const s32 wheel = static_cast<s32>(lsb) + static_cast<s32>(msb) * 128 - 8192;
        const double normalized = std::clamp(wheel / 8192.0, -1.0, 1.0);
        playback.atEvent().pitchBend(PitchBendPerformanceEvent{
            .semitones = normalized * playback.track.pitchBendRange,
            .normalizedWheelPosition = normalized,
        });
      });
    }
    case 0x5d: {
      auto event = beginEvent(cursor, *source, "Pitch Bend Range", SequenceSemantic::Pitch);
      const s8 range = event.s8("semitones", SemanticOperandRole::Pitch);
      return event.invoke([range](Playback& playback) {
        playback.track.pitchBendRange = range;
        playback.atEvent().pitchBendRange(static_cast<u8>(std::abs(static_cast<int>(range))));
      });
    }
    case 0x60:
    case 0x61: {
      const bool enabled = source->status == 0x60;
      return beginEvent(cursor, *source, enabled ? "Reverb On" : "Reverb Off", SequenceSemantic::State)
          .invoke([enabled](Playback& playback) { playback.atEvent().reverb(enabled ? 1.0 : 0.0); });
    }
    case 0x62: {
      auto event = beginEvent(cursor, *source, "Output Routing", SequenceSemantic::State);
      const u8 left = event.u8("left", SemanticOperandRole::State);
      const u8 right = event.u8("right", SemanticOperandRole::State);
      return event.invoke([left, right](Playback& playback) {
        const double wet = ((left == 1 || left == 2 ? 1.0 : 0.0) + (right == 1 || right == 2 ? 1.0 : 0.0)) / 2.0;
        playback.atEvent().reverb(wet);
      });
    }
    case 0x64:
    case 0x65:
    case 0x68:
    case 0x69:
      return sourceOnly(cursor, *source, "Voice Control Flag");
    case 0x6c:
      return sourceOnly(cursor, *source, "SPU2 Core", 1);
    case 0x70:
    case 0x71:
      return sourceOnly(cursor, *source, "Timing Correction", 1);
    case 0x72: {
      auto event = beginEvent(cursor, *source, "LFO Slot Off", SequenceSemantic::Modulation);
      return event.invoke<&Playback::lfoOff>(event.u8("slot") & 3);
    }
    case 0x73: {
      auto event = beginEvent(cursor, *source, "LFO Slot Delay and Fade", SequenceSemantic::Modulation);
      const u8 slot = event.u8("slot");
      const u8 delay = event.u8("delay", SemanticOperandRole::Duration);
      const u8 fade = event.u8("fade", SemanticOperandRole::Duration);
      return event.invoke<&Playback::lfoDelay>(slot, delay, fade);
    }
    case 0x74: {
      auto event = beginEvent(cursor, *source, "LFO Slot Mode and Waveform", SequenceSemantic::Modulation);
      const u8 slot = event.u8("slot");
      const u8 mode = event.u8("mode", SemanticOperandRole::Modulation);
      const u8 wave = event.u8("wave", SemanticOperandRole::Modulation);
      return event.invoke([slot, mode, wave](Playback& playback) {
        auto& lfo = playback.track.lfos[slot & 3];
        lfo.mode = static_cast<s8>(mode % 3);
        lfo.wave = wave & 0x0f;
        auto emitter = playback.atEvent();
        playback.emitLfoDepth(emitter, lfo, true);
        playback.emitLfoRate(emitter, lfo);
      });
    }
    case 0x75: {
      auto event = beginEvent(cursor, *source, "LFO Slot Depth and Rate", SequenceSemantic::Modulation);
      const u8 slot = event.u8("slot");
      const u8 depth = event.u8("depth", SemanticOperandRole::Modulation);
      const u8 rate = event.u8("rate", SemanticOperandRole::Modulation);
      return event.invoke([slot, depth, rate](Playback& playback) {
        playback.lfoDepthValue(slot, depth);
        playback.lfoRateValue(slot, rate);
      });
    }
    case 0x76:
    case 0x77: {
      auto event = beginEvent(cursor, *source, source->status == 0x76 ? "LFO Slot Depth Fade" : "LFO Slot Rate Fade",
                              SequenceSemantic::Modulation);
      const u8 slot = event.u8("slot");
      const u32 duration = event.varLen("duration", SemanticOperandRole::Duration);
      const u8 target = event.u8("target", SemanticOperandRole::Modulation);
      return source->status == 0x76 ? event.invoke<&Playback::lfoDepthFade>(slot, duration, target)
                                    : event.invoke<&Playback::lfoRateFade>(slot, duration, target);
    }
    case 0x78:
      return sourceOnly(cursor, *source, "External Counter Set", 2);
    case 0x79:
      return sourceOnly(cursor, *source, "External Counter Clear", 1);
    case 0x7c:
      return sourceOnly(cursor, *source, "Driver Table Jump", 1, SequenceSemantic::Jump);
    case 0x7d:
      return sourceOnly(cursor, *source, "Driver Table Call", 1, SequenceSemantic::Call);
    case 0x7e:
    case 0x7f:
      return beginEvent(cursor, *source, "No Operation", SequenceSemantic::Meta);
    default:
      return beginEvent(cursor, *source, "Undefined Event", SequenceSemantic::End, CommandPlaybackStatus::StopsPlayback)
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
                       [&](u32 offset) { return decodeCommand(reader, offset, end, layout, trackIndex, diagnostics); });
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
  return makeCompiledRuntime<Cursor>(std::move(config));
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
  program.behavior.initialSourceInstrument = instrumentIdentity(config.defaultBank, 0);
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
    const auto& track = layout.tracks[index];
    auto trackProgram = decodeTrack(reader, id, index, track, sourceMap, diagnostics);
    if (sourceMap != nullptr && trackProgram.annotation.valid()) {
      AnnotationBuilder{*sourceMap, trackProgram.annotation}.range(reader.range(track.blockOffset, 4 + track.length));
      sourceMap->field("Track Length", reader.range(track.blockOffset, 4), track.length).parent(trackProgram.annotation);
    }
    trackProgram.sourceTrackNumber = index;
    program.tracks.push_back(std::move(trackProgram));
  }
  return program;
}

}  // namespace vgmtrans::formats::square_ps2
