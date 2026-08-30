/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/HeartBeatPS1/HeartBeatPS1.h"

#include "value/sequence/CommandSourceMap.h"
#include "value/sequence/CompiledCommandRuntime.h"
#include "value/sequence/SequenceVm.h"
#include "value/synth/PsxSpu.h"
#include "value/synth/SynthMath.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string_view>
#include <utility>

namespace vgmtrans::formats::heartbeat_ps1 {

using namespace core;

namespace {

constexpr u32 kMaxCommands = 1048576;

[[nodiscard]] double driverLevel(u8 value) {
  return value == 127 ? 1.0 : std::min<u8>(value, 127) / 128.0;
}

[[nodiscard]] double lfoRate(u8 value) {
  const u32 divisor = ((128u - std::min<u8>(value, 127)) >> 3) + 1;
  return 60.0 / divisor;
}

[[nodiscard]] LfoShape lfoShape(u8 value) {
  switch (value & 3) {
    case 0:
      return LfoShape{.waveform = LfoWaveform::SawtoothUp};
    case 1:
      return LfoShape{.waveform = LfoWaveform::SawtoothDown};
    case 2:
      return LfoShape{.waveform = LfoWaveform::Triangle};
    default:
      return LfoShape{.waveform = LfoWaveform::Square};
  }
}

struct RuntimeConfig {
  u8 numerator = 4;
  u8 denominator = 4;
  u16 ppqn = 480;
  u32 initialTempo = 500000;
  std::array<u16, 4> bankIds{0xffff, 0xffff, 0xffff, 0xffff};
  std::vector<HeartBeatPs1InstrumentInfo> instruments;
};

struct ProgramState {
  explicit ProgramState(const RuntimeConfig& config)
      : numerator(config.numerator), denominator(config.denominator), ppqn(config.ppqn),
        initialTempo(config.initialTempo), bankIds(config.bankIds), instruments(config.instruments) {}

  u8 numerator = 4;
  u8 denominator = 4;
  u16 ppqn = 480;
  u32 initialTempo = 500000;
  std::array<u16, 4> bankIds{};
  std::vector<HeartBeatPs1InstrumentInfo> instruments;
};

struct LfoState {
  u8 waveform = 2;
  u8 rate = 0;
  u8 depth = 0;
  u8 delay = 0;
  bool enabled = false;
};

struct TrackState {
  explicit TrackState(const TrackProgram& program)
      : channel(static_cast<u8>(program.sourceTrackNumber)), program(static_cast<u8>(program.sourceTrackNumber)) {}

  u8 channel = 0;
  u16 bank = 0xffff;
  u8 program = 0;
  u8 modulation = 64;
  u32 tempo = 500000;
  u16 ppqn = 480;
  u16 dynamicAdsr1 = 0;
  u16 dynamicAdsr2 = 0;
  bool initialized = false;
  bool sustain = false;
  bool portamento = false;
  double portamentoMilliseconds = 0.0;
  std::optional<u8> previousKey;
  LfoState vibrato;
  LfoState tremolo;
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
    track.bank = programState.bankIds.front();
    track.ppqn = programState.ppqn;
    track.tempo = programState.initialTempo;
    out.instrument(heartBeatPs1InstrumentIdentity(track.bank, track.program));
    if (track.channel == 0) {
      out.timeSignature(programState.numerator, programState.denominator, 24);
    }
  }

  [[nodiscard]] u64 eventTick(u32 delta) const {
    return vm.tick() > std::numeric_limits<u64>::max() - delta ? std::numeric_limits<u64>::max() : vm.tick() + delta;
  }

  [[nodiscard]] Effects after(u32 delta) const { return Effects::wait(delta); }

  [[nodiscard]] const HeartBeatPs1Tone* currentTone(u8 key) const {
    const auto instrument = std::ranges::find_if(programState.instruments, [&](const auto& value) {
      return value.bank == track.bank && value.program == track.program;
    });
    if (instrument == programState.instruments.end()) {
      return nullptr;
    }
    const auto tone = std::ranges::find_if(
        instrument->tones, [&](const auto& value) { return key >= value.keys.low && key <= value.keys.high; });
    return tone == instrument->tones.end() ? nullptr : &*tone;
  }

  [[nodiscard]] u32 portamentoTicks() const {
    const double ticks = track.portamentoMilliseconds * 1000.0 * track.ppqn / std::max<u32>(track.tempo, 1);
    return static_cast<u32>(std::clamp(std::round(ticks), 1.0, static_cast<double>(std::numeric_limits<u32>::max())));
  }

  Effects noteOn(u8 channel, u8 key, u8 velocity, u32 delta) {
    if (channel != track.channel) {
      return after(delta);
    }
    auto delayed = out.at(eventTick(delta));
    const PerformanceNoteId note = delayed.noteOn(key, driverLevel(velocity));
    if (track.portamento && track.previousKey && *track.previousKey != key && track.portamentoMilliseconds > 0.0) {
      delayed
          .pitchSlide(note, *track.previousKey, key,
                      PitchSlideTiming::fixedDuration(portamentoTicks(), track.portamentoMilliseconds))
          .preferPortamento()
          .useCurrentPortamentoTiming();
    }
    track.previousKey = key;
    return after(delta);
  }

  Effects noteOff(u8 channel, u8 key, u32 delta) {
    if (channel == track.channel) {
      out.at(eventTick(delta)).noteOff(key);
    }
    return after(delta);
  }

  Effects program(u8 channel, u8 value, u32 delta) {
    if (channel == track.channel) {
      track.program = value;
      out.at(eventTick(delta)).instrument(heartBeatPs1InstrumentIdentity(track.bank, track.program));
    }
    return after(delta);
  }

  [[nodiscard]] LfoPerformanceContext lfoContext(const LfoState& state) const {
    LfoPerformanceContext context;
    context.frequencyHz = lfoRate(state.rate);
    context.shape = lfoShape(state.waveform);
    context.polarity = state.waveform % 4 == 0   ? LfoPolarity::Positive
                       : state.waveform % 4 == 1 ? LfoPolarity::Negative
                                                 : LfoPolarity::Bipolar;
    context.restartMode = LfoRestartMode::None;
    context.phaseRunsAtZeroDepth = true;
    return context;
  }

  void emitVibrato(PerformanceEmitter& delayed) const {
    auto context = lfoContext(track.vibrato);
    const double depth = track.vibrato.enabled ? track.vibrato.depth * track.modulation / 4096.0 : 0.0;
    delayed.vibratoRate(*context.frequencyHz, context);
    delayed.vibratoDepth(depth, std::move(context));
  }

  void emitTremolo(PerformanceEmitter& delayed) const {
    auto context = lfoContext(track.tremolo);
    context.delayMilliseconds = track.tremolo.delay * (1000.0 / 60.0);
    context.tremoloGainMode = TremoloGainMode::BipolarAroundNominal;
    const double depth = track.tremolo.enabled ? track.tremolo.depth / 256.0 : 0.0;
    delayed.tremoloRate(*context.frequencyHz, context);
    delayed.tremoloLinearGainDepth(depth, std::move(context));
  }

  void updateEnvelope(PerformanceEmitter& delayed, EnvelopeFields fields) const {
    delayed.updateEnvelope(psxSpuEnvelope(track.dynamicAdsr1, track.dynamicAdsr2), fields,
                           VoiceEnvelopeScope::FutureAttacks);
  }

  Effects controller(u8 channel, u8 controller, u8 value, u32 delta) {
    if (channel != track.channel) {
      return after(delta);
    }
    auto delayed = out.at(eventTick(delta));
    switch (controller) {
      case 1:
        track.modulation = value;
        emitVibrato(delayed);
        break;
      case 2:
      case 11:
        delayed.expression(driverLevel(value));
        break;
      case 5:
        track.portamento = value != 0;
        track.portamentoMilliseconds = (128u - value) * (1000.0 / 60.0);
        delayed.portamentoEnable(track.portamento);
        delayed.pitchTransitionSettings(track.portamentoMilliseconds);
        break;
      case 7:
        delayed.level(driverLevel(value));
        break;
      case 10:
        delayed.channelPan(panPositionFrom7Bit(std::min<u8>(value, 127)));
        break;
      case 20:
        delayed.masterLevel(driverLevel(value));
        break;
      case 22:
        // The SPU effect-return depth is global; reverb send is value-core's
        // closest portable representation of the audible wet-depth change.
        delayed.reverb(value / 127.0);
        break;
      case 32:
        if (value < programState.bankIds.size() && programState.bankIds[value] != 0xffff) {
          track.bank = programState.bankIds[value];
          delayed.instrument(heartBeatPs1InstrumentIdentity(track.bank, track.program));
        }
        break;
      case 52:
        track.tremolo.waveform = value;
        emitTremolo(delayed);
        break;
      case 53:
        track.tremolo.delay = value;
        delayed.tremoloDelayPhysical(0, value * (1000.0 / 60.0));
        break;
      case 54:
        track.tremolo.rate = value;
        emitTremolo(delayed);
        break;
      case 55:
        track.tremolo.depth = value;
        emitTremolo(delayed);
        break;
      case 56:
        track.vibrato.waveform = value;
        emitVibrato(delayed);
        break;
      case 64:
        track.sustain = value != 0;
        delayed.sustainPedal(track.sustain);
        break;
      case 71:
        track.dynamicAdsr2 = static_cast<u16>((track.dynamicAdsr2 & 0xc03f) | ((127u - value) & 0x7f) << 6);
        updateEnvelope(delayed, EnvelopeFields::SecondDecay);
        break;
      case 72:
        track.dynamicAdsr2 = static_cast<u16>((track.dynamicAdsr2 & 0xffe0) | ((~value) & 0x1f));
        updateEnvelope(delayed, EnvelopeFields::Release);
        break;
      case 73:
        track.dynamicAdsr1 = static_cast<u16>((track.dynamicAdsr1 & 0x80ff) | ((127u - value) & 0x7f) << 8);
        updateEnvelope(delayed, EnvelopeFields::Attack);
        break;
      case 74:
        track.dynamicAdsr2 = static_cast<u16>((track.dynamicAdsr2 & 0x803f) | 0x4000 | (((127u - value) & 0x7f) << 6));
        updateEnvelope(delayed, EnvelopeFields::SecondDecay);
        break;
      case 75:
        track.dynamicAdsr1 = static_cast<u16>((track.dynamicAdsr1 & 0xff0f) | (((~value) & 0x0f) << 4));
        updateEnvelope(delayed, EnvelopeFields::Decay);
        break;
      case 76:
        track.vibrato.rate = value;
        emitVibrato(delayed);
        break;
      case 77:
        track.vibrato.depth = value;
        emitVibrato(delayed);
        break;
      case 78:
        track.vibrato.enabled = value != 0;
        emitVibrato(delayed);
        break;
      case 79:
        track.dynamicAdsr1 = static_cast<u16>((track.dynamicAdsr1 & 0xfff0) | (value & 0x0f));
        updateEnvelope(delayed, EnvelopeFields::Sustain);
        break;
      case 91:
        // This is a future-voice routing override in the driver. The current
        // performance model has channel send rather than future-attack scope.
        delayed.reverb(value == 0 ? 0.0 : 1.0);
        break;
      case 92:
        track.tremolo.enabled = value != 0;
        emitTremolo(delayed);
        break;
      case 121:
        track.modulation = 64;
        track.sustain = false;
        track.portamento = false;
        track.vibrato = {};
        track.tremolo = {};
        track.dynamicAdsr1 = 0;
        track.dynamicAdsr2 = 0;
        delayed.sustainPedal(false);
        delayed.portamentoEnable(false);
        delayed.restoreEnvelope(EnvelopeFields::All, VoiceEnvelopeScope::FutureAttacks);
        delayed.level(1.0);
        delayed.expression(1.0);
        delayed.channelPan(0.5);
        delayed.pitchBend(0.0);
        delayed.vibratoDepth(0.0);
        delayed.tremoloLinearGainDepth(0.0);
        break;
      default:
        // CC4/9/21/23/69/126/127 control SPU noise, signed phase,
        // reverb configuration, voice allocation, and mono/poly flags. They
        // remain fully decoded even though value-core has no matching event.
        break;
    }
    return after(delta);
  }

  Effects pitchBend(u8 channel, u16 value, u32 delta) {
    if (channel != track.channel) {
      return after(delta);
    }
    const double wheel =
        value < 8192 ? (static_cast<double>(value) - 8192.0) / 8192.0 : (static_cast<double>(value) - 8192.0) / 8191.0;
    double semitones = wheel * 2.0;
    if (track.previousKey) {
      if (const auto* tone = currentTone(*track.previousKey)) {
        semitones = wheel < 0.0 ? wheel * tone->bendDownSemitones : wheel * tone->bendUpSemitones;
      }
    }
    out.at(eventTick(delta))
        .pitchBend(PitchBendPerformanceEvent{.semitones = semitones, .normalizedWheelPosition = wheel});
    return after(delta);
  }

  Effects tempo(u32 microsecondsPerQuarter, u32 delta) {
    if (microsecondsPerQuarter != 0) {
      track.tempo = microsecondsPerQuarter;
      if (track.channel == 0) {
        out.at(eventTick(delta)).tempo(microsecondsPerQuarter);
      }
    }
    return after(delta);
  }

  Effects sourceOnly(u32 delta) { return after(delta); }

  Effects loopEnd(u8 count, Address destination, u32 delta) {
    Effects effects = after(delta);
    if (count == 127) {
      effects.flowOverride = vm.declaredLoop(destination).flowOverride;
    } else if (count != 0) {
      effects.flowOverride = vm.countedRepeatUntil(0, static_cast<u32>(count) + 1, destination).flowOverride;
    }
    return effects;
  }
};

using Cursor = CompilerCursor<TrackState, Playback>;

[[nodiscard]] bool controllerAffectsPlayback(u8 controller) {
  switch (controller) {
    case 1:
    case 2:
    case 5:
    case 7:
    case 10:
    case 11:
    case 20:
    case 22:
    case 32:
    case 52:
    case 53:
    case 54:
    case 55:
    case 56:
    case 64:
    case 71:
    case 72:
    case 73:
    case 74:
    case 75:
    case 76:
    case 77:
    case 78:
    case 79:
    case 91:
    case 92:
    case 121:
      return true;
    default:
      return false;
  }
}

[[nodiscard]] std::string_view controllerLabel(u8 controller) {
  switch (controller) {
    case 1:
      return "Modulation Wheel";
    case 2:
      return "Expression";
    case 4:
      return "Noise Enable";
    case 5:
      return "Portamento Time";
    case 6:
      return "NRPN Data Entry";
    case 7:
      return "Channel Volume";
    case 9:
      return "Stereo Phase";
    case 10:
      return "Channel Pan";
    case 11:
      return "Expression";
    case 20:
      return "Sequence Volume";
    case 21:
      return "Reverb Mode";
    case 22:
      return "Reverb Depth";
    case 23:
      return "Reverb Feedback";
    case 32:
      return "Wave Bank Select";
    case 52:
      return "Tremolo Waveform";
    case 53:
      return "Tremolo Delay";
    case 54:
      return "Tremolo Rate";
    case 55:
      return "Tremolo Depth";
    case 56:
      return "Vibrato Waveform";
    case 64:
      return "Sustain Pedal";
    case 69:
      return "Voice Hold Mode";
    case 71:
      return "Sustain Rate";
    case 72:
      return "Release Rate";
    case 73:
      return "Attack Rate";
    case 74:
      return "Sustain Decay Rate";
    case 75:
      return "Decay Rate";
    case 76:
      return "Vibrato Rate";
    case 77:
      return "Vibrato Depth";
    case 78:
      return "Vibrato Enable";
    case 79:
      return "Sustain Level";
    case 91:
      return "Reverb Routing Override";
    case 92:
      return "Tremolo Enable";
    case 98:
      return "NRPN LSB";
    case 99:
      return "NRPN MSB";
    case 121:
      return "Reset All Controllers";
    case 126:
      return "Mono Mode";
    case 127:
      return "Poly Mode";
    default:
      return "Control Change";
  }
}

[[nodiscard]] DecodedBytecodeCommand decodeEvent(ByteReader reader, u32 end, const HeartBeatPs1EventLayout& source,
                                                 std::vector<Diagnostic>* diagnostics) {
  Cursor cursor(reader, source.offset, end, kHeartBeatPs1CommandKindPrefix, diagnostics);
  if (!cursor.hasOpcode()) {
    return cursor.truncated();
  }
  const u8 family = source.status & 0xf0;
  const u8 channel = source.status & 0x0f;
  std::string_view label = "MIDI Event";
  SequenceSemantic semantic = SequenceSemantic::State;
  CommandPlaybackStatus playback = CommandPlaybackStatus::AffectsPlayback;
  if (family == 0x80) {
    label = "Note Off";
    semantic = SequenceSemantic::Note;
  } else if (family == 0x90) {
    label = "Note On";
    semantic = SequenceSemantic::Note;
  } else if (family == 0xa0) {
    label = "Polyphonic Key Pressure";
    playback = CommandPlaybackStatus::SourceOnly;
  } else if (source.loopDestination) {
    label = "Loop End";
    semantic = SequenceSemantic::Loop;
    playback = CommandPlaybackStatus::AffectsControlFlow;
  } else if (family == 0xb0) {
    label = source.data1 == 99 && source.data2 == 20 ? "Loop Start" : controllerLabel(source.data1);
    semantic = source.data1 == 99 && source.data2 == 20 ? SequenceSemantic::Loop : SequenceSemantic::State;
    playback = controllerAffectsPlayback(source.data1) ? CommandPlaybackStatus::AffectsPlayback
                                                       : CommandPlaybackStatus::SourceOnly;
  } else if (family == 0xc0) {
    label = "Program Change";
    semantic = SequenceSemantic::Program;
  } else if (family == 0xd0) {
    label = "Channel Pressure";
    playback = CommandPlaybackStatus::SourceOnly;
  } else if (family == 0xe0) {
    label = "Pitch Bend";
    semantic = SequenceSemantic::Pitch;
  } else if (source.status == 0xff && source.data1 == 0x51) {
    label = "Tempo";
    semantic = SequenceSemantic::Tempo;
  } else if (source.status == 0xff && source.data1 == 0x2f) {
    label = "End of Sequence";
    semantic = SequenceSemantic::End;
    playback = CommandPlaybackStatus::StopsPlayback;
  } else if (source.status == 0xff) {
    label = "Meta Event";
    semantic = SequenceSemantic::Meta;
    playback = CommandPlaybackStatus::SourceOnly;
  }

  auto event = cursor.command(label, semantic, playback);
  if (source.end > source.offset + 1) {
    static_cast<void>(event.rawBytes("encoded_bytes", source.end - source.offset - 1));
  }
  event.derived("delta", source.delta, SemanticOperandRole::Duration);
  event.derived("status", source.status, SourceValueDisplay::Hex);
  if (source.status < 0xf0) {
    event.derived("channel", channel, SemanticOperandRole::Channel);
  }
  if (family == 0x80) {
    event.derived("key", source.data1, SourceValueDisplay::MidiNote, SemanticOperandRole::NoteKey);
    event.derived("velocity", source.data2, SemanticOperandRole::Level);
    return event.invoke<&Playback::noteOff>(channel, source.data1, source.delta);
  }
  if (family == 0x90) {
    event.derived("key", source.data1, SourceValueDisplay::MidiNote, SemanticOperandRole::NoteKey);
    event.derived("velocity", source.data2, SemanticOperandRole::Level);
    return event.invoke<&Playback::noteOn>(channel, source.data1, source.data2, source.delta);
  }
  if (family == 0xa0 || family == 0xd0) {
    return event.invoke<&Playback::sourceOnly>(source.delta);
  }
  if (family == 0xb0) {
    event.derived("controller", source.data1);
    event.derived("value", source.data2,
                  source.data1 == 32 ? SemanticOperandRole::InstrumentBank : SemanticOperandRole::Value);
    if (source.loopDestination) {
      const Address destination{*source.loopDestination};
      event.derived("repeat_count", source.loopCount, SemanticOperandRole::Count);
      event.derived("destination", destination, SourceValueDisplay::Address, SemanticOperandRole::LoopTarget);
      return event.invoke<&Playback::loopEnd>(source.loopCount, destination, source.delta).mayBranchTo(destination);
    }
    if (source.data1 == 99 && source.data2 == 20) {
      event.derived("loop_start", Address{source.end}, SourceValueDisplay::Address, SemanticOperandRole::LoopTarget);
    }
    return event.invoke<&Playback::controller>(channel, source.data1, source.data2, source.delta);
  }
  if (family == 0xc0) {
    event.derived("program", source.data1, SemanticOperandRole::InstrumentProgram);
    return event.invoke<&Playback::program>(channel, source.data1, source.delta);
  }
  if (family == 0xe0) {
    const u16 value = static_cast<u16>((source.data2 << 7) | source.data1);
    event.derived("wheel", value, SemanticOperandRole::Pitch);
    return event.invoke<&Playback::pitchBend>(channel, value, source.delta);
  }
  if (source.status == 0xff && source.data1 == 0x51 && source.dataBytes == 3) {
    const u32 payload = source.end - 3;
    const u32 tempo = (static_cast<u32>(reader.u8At(payload)) << 16) |
                      (static_cast<u32>(reader.u8At(payload + 1)) << 8) | reader.u8At(payload + 2);
    event.derived("microseconds_per_quarter", tempo);
    return event.invoke<&Playback::tempo>(tempo, source.delta);
  }
  if (source.status == 0xff && source.data1 == 0x2f) {
    return event.end();
  }
  return event.invoke<&Playback::sourceOnly>(source.delta);
}

}  // namespace

const SequenceProgramConfig& heartBeatPs1SequenceConfig() {
  static const SequenceProgramConfig config = SequenceProgramConfig{
      .commandKindPrefix = std::string(kHeartBeatPs1CommandKindPrefix),
      .timebase = Timebase{.ppqn = 480},
      .behavior =
          SequenceProgramBehavior{
              .commandLimit = kMaxCommands,
              .initialLevel = 1.0,
              .initialExpression = 1.0,
              .initialChannelPan = 0.5,
              .initialPitchBendRangeSemitones = 2,
              .initialTempoMicrosecondsPerQuarter = 500000,
          },
  };
  return config;
}

SequenceProgram parseHeartBeatPs1Sequence(ByteReader reader, AssetId id, const HeartBeatPs1SequenceLayout& layout,
                                          const std::vector<HeartBeatPs1InstrumentInfo>& instruments,
                                          SourceMapBuilder* sourceMap, std::vector<Diagnostic>* diagnostics) {
  SequenceProgram program = heartBeatPs1SequenceConfig().makeProgram();
  program.timebase.ppqn = layout.ppqn;
  program.behavior.initialTempoMicrosecondsPerQuarter = layout.initialTempo;
  program.runtime = makeCompiledRuntime<Cursor, ProgramState>(RuntimeConfig{
      .numerator = layout.rhythmNumerator,
      .denominator = static_cast<u8>(1u << layout.rhythmDenominatorPower),
      .ppqn = layout.ppqn,
      .initialTempo = layout.initialTempo,
      .bankIds = layout.bankIds,
      .instruments = instruments,
  });

  if (sourceMap != nullptr) {
    auto soundHeader =
        sourceMap->header("HeartBeatPS1 Sound Header", reader.range(layout.offset, 0x3c))
            .kind("heartbeat-ps1-sound-header")
            .owner(ObjectRefs::sequence(id))
            .fieldsAsChildren()
            .field("sequence_size", reader.range(layout.offset, 4), layout.dataEnd - layout.qQesOffset)
            .field("sequence_id", reader.range(layout.offset + 4, 2), layout.sequenceId)
            .field("descriptor_count", reader.range(layout.offset + 6, 1), reader.u8At(layout.offset + 6))
            .field("load_position", reader.range(layout.offset + 7, 1), reader.u8At(layout.offset + 7));
    for (u32 index = 0; index < layout.bankIds.size(); ++index) {
      soundHeader.field("bank_id", reader.range(layout.offset + 0x14 + index * 0x0c, 2), layout.bankIds[index]);
    }
    sourceMap->header("qQES Sequence Header", reader.range(layout.qQesOffset, 0x10))
        .kind("heartbeat-ps1-sequence-header")
        .owner(ObjectRefs::sequence(id))
        .fieldsAsChildren()
        .field("signature", reader.range(layout.qQesOffset, 4), reader.le32(layout.qQesOffset), SourceValueDisplay::Hex)
        .field("version", reader.range(layout.qQesOffset + 4, 2), layout.version)
        .field("ppqn", reader.range(layout.qQesOffset + 8, 2), layout.ppqn)
        .field("initial_tempo", reader.range(layout.qQesOffset + 10, 3), layout.initialTempo)
        .field("rhythm_numerator", reader.range(layout.qQesOffset + 13, 1), layout.rhythmNumerator)
        .field("rhythm_denominator_power", reader.range(layout.qQesOffset + 14, 1), layout.rhythmDenominatorPower)
        .field("track_count", reader.range(layout.qQesOffset + 15, 1), layout.trackCount);
  }

  TrackDecodeScope tracks{
      .reader = reader,
      .bytecodeEnd = layout.dataEnd,
      .maxCommands = kMaxCommands,
      .sourceHasTracks = false,
      .sequenceAsset = id,
      .sourceMap = sourceMap,
  };
  auto eventAt = [&](u32 offset) -> const HeartBeatPs1EventLayout* {
    const auto found = std::ranges::lower_bound(layout.events, offset, {}, &HeartBeatPs1EventLayout::offset);
    return found != layout.events.end() && found->offset == offset ? &*found : nullptr;
  };
  auto track = tracks.decode(0, layout.dataOffset, [&](u32 offset) -> DecodedBytecodeCommand {
    const auto* event = eventAt(offset);
    if (event == nullptr) {
      Cursor cursor(reader, offset, layout.dataEnd, kHeartBeatPs1CommandKindPrefix, diagnostics);
      return cursor.unsupported("Invalid HeartBeatPS1 event address").stop();
    }
    return decodeEvent(reader, layout.dataEnd, *event, diagnostics);
  });
  track.sourceTrackNumber = 0;
  program.tracks.push_back(track);
  for (u32 channel = 1; channel < layout.trackCount; ++channel) {
    TrackProgram copy = track;
    copy.sourceTrackNumber = channel;
    program.tracks.push_back(std::move(copy));
  }
  return program;
}

}  // namespace vgmtrans::formats::heartbeat_ps1
