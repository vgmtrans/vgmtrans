/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/KonamiPS1/KonamiPS1.h"

#include "value/formats/SonyPS1/SonyPS1.h"
#include "value/sequence/CommandSourceMap.h"
#include "value/sequence/CompiledCommandRuntime.h"
#include "value/sequence/SequenceVm.h"
#include "value/synth/PsxSpu.h"
#include "value/synth/SynthMath.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace vgmtrans::formats::konami_ps1 {

using namespace core;

namespace {

constexpr u32 kMaximumCommands = 1048576;
constexpr double kRootCounterClockHz = 33'868'800.0 / 8.0;
constexpr double kByteAccumulatorRange = 256.0;
constexpr double kVoiceUpdateDivider = 11.0;
constexpr u16 kInitialTempoStep = 0x68;
constexpr PitchBendLayerId kChannelPitchLfoLayer{1};
constexpr PitchBendLayerId kRandomPitchLayer{2};

[[nodiscard]] double driverLevel(u8 value) {
  return value == 127 ? 1.0 : std::min<u8>(value, 127) / 128.0;
}

struct DriverTiming {
  explicit DriverTiming(const SequenceLayout& layout, u16 rootCounterTarget)
      : ppqn(layout.ppqn), sequenceHz(kRootCounterClockHz / rootCounterTarget),
        voiceHz(sequenceHz / kVoiceUpdateDivider) {}

  [[nodiscard]] u32 driverPpqn() const {
    switch (ppqn) {
      case 48:
      case 96:
      case 240:
      case 360:
      case 480:
      case 960:
        return 120;
      case 192:
      case 288:
      case 384:
      case 768:
        return 96;
      default:
        return ppqn;
    }
  }

  [[nodiscard]] static u16 tempoStep(u8 value) { return static_cast<u16>(value * 2u + 2u); }

  [[nodiscard]] double tempoBpm(u16 step) const {
    return sequenceHz * step * 60.0 / (kByteAccumulatorRange * driverPpqn());
  }

  [[nodiscard]] u32 tempoMicroseconds(u16 step) const {
    return static_cast<u32>(std::lround(60000000.0 / tempoBpm(step)));
  }

  u32 ppqn;
  double sequenceHz;
  double voiceHz;
};

struct RuntimeConfig {
  DriverTiming timing;
  std::vector<Tone> tones;
};

struct ProgramState {
  explicit ProgramState(const RuntimeConfig& config)
      : timing(config.timing), tempo(timing.tempoMicroseconds(kInitialTempoStep)), tones(config.tones) {}

  [[nodiscard]] Tone* tone(u16 bank, u8 program, u8 index) {
    const auto found = std::ranges::find_if(tones, [&](const Tone& candidate) {
      return candidate.bank == bank && candidate.program == program && candidate.index == index;
    });
    return found == tones.end() ? nullptr : &*found;
  }

  [[nodiscard]] Tone* toneForKey(u16 bank, u8 program, u8 key) {
    const auto matches = [&](const Tone& candidate) {
      return candidate.bank == bank && candidate.program == program && key >= candidate.keyLow &&
             key <= candidate.keyHigh;
    };
    auto found =
        std::ranges::find_if(tones, [&](const Tone& candidate) { return candidate.dynamicAdsr && matches(candidate); });
    if (found == tones.end()) {
      found = std::ranges::find_if(tones, matches);
    }
    return found == tones.end() ? nullptr : &*found;
  }

  template <class Apply>
  void updateTones(u16 bank, u8 program, u8 target, u8 parameter, Apply apply) {
    if (target == 16 && parameter < 14) {
      for (auto& candidate : tones) {
        if (candidate.bank == bank && candidate.program == program) {
          apply(candidate);
        }
      }
      return;
    }
    if (auto* selected = tone(bank, program, target & 0x0f)) {
      apply(*selected);
    }
  }

  void restoreBankAdsr(u16 bank) {
    for (auto& tone : tones) {
      if (tone.bank == bank) {
        tone.adsr1 = tone.originalAdsr1;
        tone.adsr2 = tone.originalAdsr2;
        tone.dynamicAdsr = false;
      }
    }
  }

  DriverTiming timing;
  u32 tempo;
  double reverbDepth = 1.0;
  std::optional<double> reverbDelayMilliseconds;
  std::optional<double> reverbFeedback;
  std::optional<u8> reverbMode;
  bool reverbEnabled = true;
  std::vector<Tone> tones;
};

struct LfoState {
  u8 delay = 0;
  u8 rate = 0;
  u8 depth = 0;
  u8 ramp = 0;
};

struct TrackState {
  explicit TrackState(const TrackProgram& program) : channel(static_cast<u8>(program.sourceTrackNumber & 0x0f)) {}

  u8 channel = 0;
  u16 bank = 0;
  u8 program = 0;
  u8 nrpnParameter = 127;
  u8 nrpnTone = 127;
  u8 reverbOverride = 0;
  u16 portamento = 0;
  u8 channelPitchPrimary = 0;
  u8 channelPitchSecondary = 0;
  u8 channelPitchMode = 0;
  bool initialized = false;
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
    emitInstrument();
  }

  void emitInstrument() {
    out.instrument(sony_ps1::sonyPs1InstrumentIdentity(track.bank, track.program),
                   InstrumentEnvelopeMode::UseInstrumentEnvelope);
  }

  [[nodiscard]] u32 driverTicksToSequenceTicks(u32 ticks) const {
    if (ticks == 0) {
      return 0;
    }
    const double sequenceTicks = ticks * static_cast<double>(programState.timing.ppqn) * 1000000.0 /
                                 (programState.timing.voiceHz * std::max<u32>(programState.tempo, 1));
    return static_cast<u32>(
        std::clamp(std::round(sequenceTicks), 1.0, static_cast<double>(std::numeric_limits<u32>::max())));
  }

  [[nodiscard]] LfoPerformanceContext lfoContext(const LfoState& lfo) const {
    return LfoPerformanceContext{
        .frequencyHz = lfo.rate * (programState.timing.voiceHz / kByteAccumulatorRange),
        .delayTicks = driverTicksToSequenceTicks(static_cast<u32>(lfo.delay) * 2),
        .delayMilliseconds = lfo.delay * (2000.0 / programState.timing.voiceHz),
        .shape = LfoShape{.waveform = LfoWaveform::Triangle},
        .initialPhaseCycles = 0.0,
        .noteRestartInitialPhaseCycles = 0.0,
        .sampleImmediatelyOnNote = false,
        .delayUpdateMode = LfoDelayUpdateMode::FutureNotesOnly,
        .restartMode = LfoRestartMode::PhaseAndDelay,
        .phaseRunsAtZeroDepth = false,
        .delayRunsWhileInactive = false,
    };
  }

  [[nodiscard]] LfoPerformanceContext channelPitchContext() const {
    return LfoPerformanceContext{
        .shape = LfoShape{.waveform = LfoWaveform::Triangle},
        .initialPhaseCycles = 0.5,
        .sampleImmediatelyOnNote = false,
        .restartMode = LfoRestartMode::None,
        .restartsOnNote = false,
        .phaseRunsAtZeroDepth = false,
        .delayRunsWhileInactive = false,
    };
  }

  [[nodiscard]] double channelPitchDepth() const {
    if (track.channelPitchPrimary == 0) {
      return 0.0;
    }
    const u8 depth = track.channelPitchMode < 64 ? track.channelPitchPrimary : track.channelPitchSecondary;
    return depth / 64.0;
  }

  [[nodiscard]] double channelPitchFrequency() const {
    const u32 primary = track.channelPitchPrimary;
    const u32 secondary = track.channelPitchSecondary;
    if (primary == 0 || secondary == 0) {
      return 0.0;
    }
    const u32 maximum = (track.channelPitchMode < 64 ? primary : secondary) * 2;
    const u32 step = track.channelPitchMode < 64 ? (primary << 3) / secondary : (maximum / primary) << 1;
    if (step == 0 || maximum == 0) {
      return 0.0;
    }
    const u32 halfCycleTicks = (maximum * 2 + step - 1) / step;
    return programState.timing.voiceHz / (halfCycleTicks * 2.0);
  }

  void configureChannelPitch() {
    auto context = channelPitchContext();
    out.vibratoRate(channelPitchFrequency(), context, kChannelPitchLfoLayer);
    out.vibratoDepth(channelPitchDepth(), std::move(context), kChannelPitchLfoLayer);
  }

  [[nodiscard]] LfoPerformanceContext randomPitchContext() const {
    return LfoPerformanceContext{
        .shape = LfoShape{.waveform = LfoWaveform::Noise},
        .initialPhaseCycles = 0.0,
        .sampleImmediatelyOnNote = false,
        .restartMode = LfoRestartMode::None,
        .restartsOnNote = true,
        .phaseRunsAtZeroDepth = true,
        .delayRunsWhileInactive = false,
    };
  }

  void setRandomPitchRate(u8 value) {
    const double hertz = value == 127 ? programState.timing.voiceHz : value * (programState.timing.voiceHz / 128.0);
    out.vibratoRate(hertz, randomPitchContext(), kRandomPitchLayer);
  }

  void setRandomPitchDepth(u8 value) { out.vibratoDepth(value / 4.0, randomPitchContext(), kRandomPitchLayer); }

  void beginVibrato() {
    auto context = lfoContext(track.vibrato);
    const double depth = track.vibrato.depth / 64.0;
    out.vibratoRate(*context.frequencyHz, context);
    if (track.vibrato.ramp == 0 || depth == 0.0) {
      out.vibratoDepth(depth, std::move(context));
      return;
    }
    out.vibratoDepth(0.0, context);
    static_cast<void>(out.noteEnvelope(PerformanceAutomationTarget::VibratoDepth, depth,
                                       driverTicksToSequenceTicks(track.vibrato.ramp),
                                       driverTicksToSequenceTicks(static_cast<u32>(track.vibrato.delay) * 2)));
  }

  void beginTremolo() {
    auto context = lfoContext(track.tremolo);
    const double depth = track.tremolo.depth / 256.0;
    out.tremoloRate(*context.frequencyHz, context);
    if (track.tremolo.ramp == 0 || depth == 0.0) {
      out.tremoloLinearGainDepth(depth, std::move(context));
      return;
    }
    out.tremoloLinearGainDepth(0.0, context);
    static_cast<void>(out.noteEnvelope(PerformanceAutomationTarget::TremoloDepth, depth,
                                       driverTicksToSequenceTicks(track.tremolo.ramp),
                                       driverTicksToSequenceTicks(static_cast<u32>(track.tremolo.delay) * 2)));
  }

  void applyToneState(Tone* tone) {
    if (tone == nullptr) {
      return;
    }
    if (tone->dynamicAdsr) {
      out.replaceEnvelope(psxSpuEnvelope(tone->adsr1, tone->adsr2), VoiceEnvelopeScope::FutureAttacks);
    } else {
      out.restoreEnvelope(EnvelopeFields::All, VoiceEnvelopeScope::FutureAttacks);
    }
    const bool routed = track.reverbOverride == 0 ? (tone->flags & 4) != 0 : track.reverbOverride != 1;
    out.reverb(routed && programState.reverbEnabled ? programState.reverbDepth : 0.0);
  }

  void note(u8 key, u8 velocity) {
    if (velocity == 0) {
      out.noteOff(key);
      track.previousKey = key;
      return;
    }
    applyToneState(programState.toneForKey(track.bank, track.program, key));
    beginVibrato();
    beginTremolo();
    const PerformanceNoteId note = out.noteOn(key, driverLevel(velocity));
    if (track.portamento != 0 && track.previousKey && *track.previousKey != key) {
      const double milliseconds = track.portamento * (1000.0 / (programState.timing.voiceHz * 4.0));
      out.pitchSlide(
             note, *track.previousKey, key,
             PitchSlideTiming::fixedDuration(driverTicksToSequenceTicks((track.portamento + 3) / 4), milliseconds))
          .preferPortamento()
          .useCurrentPortamentoTiming();
    }
    track.previousKey = key;
  }

  void noteOff() {
    if (track.previousKey) {
      out.noteOff(*track.previousKey);
    }
  }

  void setChannel(u8 value) {
    track.channel = value & 0x0f;
    emitInstrument();
  }

  void setProgram(u8 value) {
    track.program = value;
    emitInstrument();
    out.restoreEnvelope(EnvelopeFields::All, VoiceEnvelopeScope::FutureAttacks);
  }

  void setBank(u8 value) {
    track.bank = value;
    emitInstrument();
    out.restoreEnvelope(EnvelopeFields::All, VoiceEnvelopeScope::FutureAttacks);
  }

  void tempo(u8 value) {
    programState.tempo = programState.timing.tempoMicroseconds(DriverTiming::tempoStep(value));
    out.tempo(programState.tempo);
  }

  void pitchBend(u8 value) {
    u8 down = 2;
    u8 up = 2;
    if (track.previousKey) {
      if (const auto* tone = programState.toneForKey(track.bank, track.program, *track.previousKey)) {
        down = tone->bendDown;
        up = tone->bendUp;
      }
    }
    const double bend = value < 64 ? (static_cast<double>(value) - 64.0) * down / 64.0
                                   : (static_cast<double>(value) - 64.0) * up / 63.0;
    out.pitchBend(bend);
  }

  void updateAdsr(u8 value) {
    const u8 parameter = track.nrpnParameter;
    programState.updateTones(track.bank, track.program, track.nrpnTone, parameter, [&](Tone& tone) {
      switch (parameter) {
        case 1:
          tone.flags = value;
          break;
        case 2:
          tone.keyLow = value;
          break;
        case 3:
          tone.keyHigh = value;
          break;
        case 4:
          tone.adsr1 = static_cast<u16>((tone.adsr1 & 0x00ff) | ((127u - value) << 8));
          break;
        case 5:
          tone.adsr1 = static_cast<u16>((tone.adsr1 & 0x00ff) | ((127u - value) << 8) | 0x8000);
          break;
        case 6:
          tone.adsr1 = static_cast<u16>((tone.adsr1 & 0xff0f) | ((value & 0x78) << 1));
          break;
        case 7:
          tone.adsr1 = static_cast<u16>((tone.adsr1 & 0xfff0) | (value >> 3));
          break;
        case 8:
          tone.adsr2 = static_cast<u16>((tone.adsr2 & 0x603f) | ((127u - value) << 6));
          break;
        case 9:
          tone.adsr2 = static_cast<u16>((tone.adsr2 & 0x603f) | ((127u - value) << 6) | 0x8000);
          break;
        case 10:
          tone.adsr2 = static_cast<u16>((tone.adsr2 & 0xffc0) | ((127u - value) >> 3));
          break;
        case 11:
          tone.adsr2 = static_cast<u16>((tone.adsr2 & 0xffc0) | ((127u - value) >> 3) | 0x20);
          break;
        case 12:
          tone.adsr2 = value < 65 ? static_cast<u16>(tone.adsr2 | 0x4000) : static_cast<u16>(tone.adsr2 & ~0x4000u);
          break;
        case 13:
          tone.adsr1 = tone.originalAdsr1;
          tone.adsr2 = tone.originalAdsr2;
          break;
        default:
          break;
      }
      if (parameter >= 4 && parameter <= 12) {
        tone.dynamicAdsr = true;
      } else if (parameter == 13) {
        tone.dynamicAdsr = false;
      }
    });
  }

  void dataEntry(u8 value) {
    if (track.nrpnTone == 20) {
      return;
    }
    switch (track.nrpnParameter) {
      case 15:
        programState.reverbMode = value;
        programState.reverbEnabled = value != 0;
        emitGlobalReverb();
        break;
      case 16:
        programState.reverbDepth = value / 128.0;
        programState.reverbEnabled = true;
        emitGlobalReverb();
        break;
      case 17:
        programState.reverbFeedback = value / 128.0;
        emitGlobalReverb();
        break;
      case 18:
      case 19:
        programState.reverbDelayMilliseconds = value;
        emitGlobalReverb();
        break;
      case 23:
        track.previousKey = value;
        break;
      default:
        updateAdsr(value);
        break;
    }
  }

  void emitGlobalReverb() {
    const double depth = programState.reverbEnabled ? programState.reverbDepth : 0.0;
    out.reverb(ReverbPerformanceEvent{
        .voiceMask = 0xff,
        .send = depth,
        .leftGain = depth,
        .rightGain = depth,
        .delayMilliseconds = programState.reverbDelayMilliseconds,
        .feedback = programState.reverbFeedback,
        .filterIndex = programState.reverbMode,
    });
  }

  void restoreBankAdsr() {
    programState.restoreBankAdsr(track.bank);
    out.restoreEnvelope(EnvelopeFields::All, VoiceEnvelopeScope::FutureAttacks);
  }

  void controller(u8 number, u8 value) {
    switch (number) {
      case 0:
        setBank(value);
        break;
      case 1:
        track.channelPitchPrimary = value;
        configureChannelPitch();
        break;
      case 2:
        track.channelPitchSecondary = value;
        if (track.channelPitchMode >= 64) {
          out.vibratoDepth(channelPitchDepth(), channelPitchContext(), kChannelPitchLfoLayer);
        }
        break;
      case 3:
        track.channelPitchMode = value;
        break;
      case 5:
        track.portamento = value;
        out.pitchTransitionSettings(value * (1000.0 / (programState.timing.voiceHz * 4.0)));
        break;
      case 6:
        dataEntry(value);
        break;
      case 7:
        out.level(driverLevel(value));
        break;
      case 10:
        out.channelPan(panPositionFrom7Bit(value == 0 ? 1 : value));
        break;
      case 11:
        out.expression(driverLevel(value));
        break;
      case 13:
        setRandomPitchRate(value);
        break;
      case 14:
        setRandomPitchDepth(value);
        break;
      case 20:
        track.vibrato.delay = value;
        break;
      case 21:
        track.vibrato.rate = value;
        break;
      case 22:
        track.vibrato.depth = value;
        break;
      case 23:
        track.vibrato.ramp = value;
        break;
      case 25:
        track.tremolo.delay = value;
        break;
      case 26:
        track.tremolo.rate = value;
        break;
      case 27:
        track.tremolo.depth = value;
        break;
      case 28:
        track.tremolo.ramp = value;
        break;
      case 30:
        out.masterLevel(driverLevel(value));
        break;
      case 64:
        out.sustainPedal(value >= 64);
        break;
      case 91:
        track.reverbOverride = value;
        if (value != 0) {
          out.reverb(value == 1 ? 0.0 : programState.reverbDepth);
        }
        break;
      case 98:
        track.nrpnParameter = value;
        break;
      case 99:
        track.nrpnTone = value;
        break;
      case 119:
        restoreBankAdsr();
        break;
      case 120:
      case 121:
      case 123:
        out.allNotesOff();
        break;
      default:
        break;
    }
  }

  Effects loopEnd(u8 count, Address destination) {
    track.nrpnTone = 30;
    if (count == 127) {
      return vm.declaredLoop(destination);
    }
    return count == 0 ? Effects{} : vm.countedRepeatUntil(0, static_cast<u32>(count) + 1, destination);
  }
};

using Cursor = CompilerCursor<TrackState, Playback>;

[[nodiscard]] bool controllerAffectsPlayback(u8 controller) {
  // CC4's raw-SPU pitch mode, CC12's post-mix squaring, CC15's phase inversion,
  // CC16's per-key pan, CC118's beat state, and CC126's voice allocator mode
  // have no lossless per-track performance representation. They remain named,
  // source-visible commands instead of being approximated as different effects.
  switch (controller) {
    case 0:
    case 1:
    case 2:
    case 3:
    case 5:
    case 6:
    case 7:
    case 10:
    case 11:
    case 13:
    case 14:
    case 20:
    case 21:
    case 22:
    case 23:
    case 25:
    case 26:
    case 27:
    case 28:
    case 30:
    case 64:
    case 91:
    case 98:
    case 99:
    case 119:
    case 120:
    case 121:
    case 123:
      return true;
    default:
      return false;
  }
}

[[nodiscard]] std::string_view controllerLabel(u8 controller) {
  switch (controller) {
    case 0:
      return "Bank Select";
    case 1:
      return "Channel Pitch LFO Depth";
    case 2:
      return "Channel Pitch LFO Period";
    case 3:
      return "Channel Pitch LFO Mode";
    case 4:
      return "Pitch Calculation Mode";
    case 5:
      return "Portamento Time";
    case 6:
      return "NRPN Data Entry";
    case 7:
      return "Channel Volume";
    case 10:
      return "Channel Pan";
    case 11:
      return "Expression";
    case 12:
      return "Quadratic Volume Curve";
    case 13:
      return "Random Pitch Rate";
    case 14:
      return "Random Pitch Depth";
    case 15:
      return "Right Channel Phase Invert";
    case 16:
      return "Key Pan Scale";
    case 20:
      return "Vibrato Delay";
    case 21:
      return "Vibrato Rate";
    case 22:
      return "Vibrato Depth";
    case 23:
      return "Vibrato Depth Ramp";
    case 25:
      return "Tremolo Delay";
    case 26:
      return "Tremolo Rate";
    case 27:
      return "Tremolo Depth";
    case 28:
      return "Tremolo Depth Ramp";
    case 30:
      return "Sequence Volume";
    case 64:
      return "Sustain Pedal";
    case 91:
      return "Reverb Routing Override";
    case 98:
      return "NRPN Parameter";
    case 99:
      return "NRPN Tone / Loop";
    case 118:
      return "Sequence Beat";
    case 119:
      return "Restore Bank ADSR";
    case 120:
      return "All Sound Off";
    case 121:
      return "Reset Channel";
    case 123:
      return "All Notes Off";
    case 126:
      return "Mono Voice Mode";
    default:
      return "Control Change";
  }
}

[[nodiscard]] DecodedBytecodeCommand decodeEvent(ByteReader reader, u32 trackEnd, const EventLayout& source,
                                                 const DriverTiming& timing, std::vector<Diagnostic>* diagnostics) {
  Cursor cursor(reader, source.offset, trackEnd, kKonamiPs1CommandKindPrefix, diagnostics);
  if (!cursor.hasOpcode()) {
    return cursor.truncated();
  }

  std::string_view label = "Control Change";
  SequenceSemantic semantic = SequenceSemantic::State;
  CommandPlaybackStatus playback = CommandPlaybackStatus::AffectsPlayback;
  switch (source.kind) {
    case EventKind::Note:
      label = source.value == 0 ? "Note Off" : "Note On";
      semantic = SequenceSemantic::Note;
      break;
    case EventKind::SetChannel:
      label = "Set Channel";
      break;
    case EventKind::Tempo:
      label = "Tempo";
      semantic = SequenceSemantic::Tempo;
      break;
    case EventKind::PitchBend:
      label = "Pitch Bend";
      semantic = SequenceSemantic::Pitch;
      break;
    case EventKind::Program:
      label = "Program Change";
      semantic = SequenceSemantic::Program;
      break;
    case EventKind::NoteOff:
      label = "Note Off Current Key";
      semantic = SequenceSemantic::Note;
      break;
    case EventKind::End:
      label = "End of Track";
      semantic = SequenceSemantic::End;
      playback = CommandPlaybackStatus::StopsPlayback;
      break;
    case EventKind::Controller:
      if (source.loopDestination) {
        label = "Loop End";
        semantic = SequenceSemantic::Loop;
        playback = CommandPlaybackStatus::AffectsControlFlow;
      } else if (source.command == 99 && source.value == 20) {
        label = "Loop Start";
        semantic = SequenceSemantic::Loop;
      } else {
        label = controllerLabel(source.command);
        playback = controllerAffectsPlayback(source.command) ? CommandPlaybackStatus::AffectsPlayback
                                                             : CommandPlaybackStatus::SourceOnly;
      }
      break;
  }

  auto event = cursor.command(label, semantic, playback);
  if (source.end > source.offset + 1) {
    static_cast<void>(event.rawBytes("encoded_bytes", source.end - source.offset - 1));
  }
  event.derived("delta", source.delta, SemanticOperandRole::Duration);
  event.derived("chained", source.chained, SourceValueDisplay::Boolean);
  event.delay(source.delta);

  switch (source.kind) {
    case EventKind::Note:
      event.derived("key", source.command, SourceValueDisplay::MidiNote, SemanticOperandRole::NoteKey);
      event.derived("velocity", source.value, SemanticOperandRole::Level);
      return event.invoke<&Playback::note>(source.command, source.value);
    case EventKind::SetChannel:
      event.derived("channel", source.value, SemanticOperandRole::Channel);
      return event.invoke<&Playback::setChannel>(source.value);
    case EventKind::Tempo: {
      event.derived("encoded_tempo", source.value);
      const u16 step = DriverTiming::tempoStep(source.value);
      event.derived("accumulator_step", step);
      event.derived("beats_per_minute", timing.tempoBpm(step), SourceValueDisplay::BeatsPerMinute);
      return event.invoke<&Playback::tempo>(source.value);
    }
    case EventKind::PitchBend:
      event.derived("wheel_msb", source.value, SemanticOperandRole::Pitch);
      return event.invoke<&Playback::pitchBend>(source.value);
    case EventKind::Program:
      event.derived("program", source.value, SemanticOperandRole::InstrumentProgram);
      return event.invoke<&Playback::setProgram>(source.value);
    case EventKind::NoteOff:
      return event.invoke<&Playback::noteOff>();
    case EventKind::End:
      return event.end();
    case EventKind::Controller:
      event.derived("controller", source.command);
      event.derived("value", source.value,
                    source.command == 0 ? SemanticOperandRole::InstrumentBank : SemanticOperandRole::Value);
      if (source.loopDestination) {
        const Address destination{*source.loopDestination};
        event.derived("repeat_count", source.loopCount, SemanticOperandRole::Count);
        event.derived("destination", destination, SourceValueDisplay::Address, SemanticOperandRole::LoopTarget);
        return event.invokeFlow<&Playback::loopEnd>(source.loopCount, destination).mayBranchTo(destination);
      }
      if (source.command == 99 && source.value == 20) {
        event.derived("loop_start", Address{source.end}, SourceValueDisplay::Address, SemanticOperandRole::LoopTarget);
      }
      return event.invoke<&Playback::controller>(source.command, source.value);
  }
  return event.stop();
}

}  // namespace

const SequenceProgramConfig& konamiPs1SequenceConfig() {
  static const SequenceProgramConfig config{
      .commandKindPrefix = std::string(kKonamiPs1CommandKindPrefix),
      .timebase = Timebase{.ppqn = 480},
      .behavior =
          SequenceProgramBehavior{
              .commandLimit = kMaximumCommands,
              .initialLevel = 1.0,
              .initialExpression = 1.0,
              .initialChannelPan = 0.5,
              .initialPitchBendRangeSemitones = 2,
              .initialTempoMicrosecondsPerQuarter = 500000,
          },
  };
  return config;
}

SequenceProgram parseKonamiPs1Sequence(ByteReader reader, AssetId id, const SequenceLayout& layout,
                                       u16 rootCounterTarget, std::vector<Tone> tones, SourceMapBuilder* sourceMap,
                                       std::vector<Diagnostic>* diagnostics) {
  const DriverTiming timing(layout, rootCounterTarget);
  SequenceProgram program = konamiPs1SequenceConfig().makeProgram();
  program.timebase.ppqn = layout.ppqn;
  program.behavior.initialTempoMicrosecondsPerQuarter = timing.tempoMicroseconds(kInitialTempoStep);
  program.runtime = makeCompiledRuntime<Cursor, ProgramState>(RuntimeConfig{
      .timing = timing,
      .tones = std::move(tones),
  });

  std::optional<SourceAnnotationId> trackParent;
  if (sourceMap != nullptr) {
    if (layout.hasKdt2Header) {
      sourceMap->header("KDT2 Container Header", reader.range(layout.containerOffset, 0x10))
          .kind("konami-ps1-kdt2-header")
          .owner(ObjectRefs::sequence(id))
          .fieldsAsChildren()
          .field("signature", reader.range(layout.containerOffset, 4), reader.le32(layout.containerOffset),
                 SourceValueDisplay::Hex)
          .field("sequence_size", reader.range(layout.containerOffset + 4, 4), layout.length)
          .field("sequence_id", reader.range(layout.containerOffset + 8, 4), layout.sequenceId);
    }
    const u32 headerSize = layout.version == 1 ? 0x10 + layout.tracks.size() * 2 : 0x50;
    auto header =
        sourceMap
            ->header(layout.version == 1 ? "KDT1 Sequence Header" : "Fixed-Table KDT Sequence Header",
                     reader.range(layout.offset, headerSize))
            .kind(layout.version == 1 ? "konami-ps1-kdt1-header" : "konami-ps1-fixed-kdt-header")
            .owner(ObjectRefs::sequence(id))
            .fieldsAsChildren()
            .field("signature", reader.range(layout.offset, 4), reader.le32(layout.offset), SourceValueDisplay::Hex)
            .field("size", reader.range(layout.offset + 4, 4), layout.length)
            .field("ppqn", reader.range(layout.offset + 8, 4), layout.ppqn)
            .field("track_count", reader.range(layout.offset + 12, 4), layout.tracks.size());
    for (u32 index = 0; index < layout.tracks.size(); ++index) {
      header.field("track_size", reader.range(layout.offset + 0x10 + index * 2, 2),
                   layout.tracks[index].end - layout.tracks[index].offset);
    }
    trackParent = header.id();
  }

  for (u32 index = 0; index < layout.tracks.size(); ++index) {
    const auto& sourceTrack = layout.tracks[index];
    TrackDecodeScope tracks{
        .reader = reader,
        .bytecodeEnd = sourceTrack.end,
        .maxCommands = kMaximumCommands,
        .sourceHasTracks = true,
        .sequenceAsset = id,
        .parentAnnotation = trackParent,
        .sourceMap = sourceMap,
    };
    auto eventAt = [&](u32 offset) -> const EventLayout* {
      const auto found = std::ranges::lower_bound(sourceTrack.events, offset, {}, &EventLayout::offset);
      return found != sourceTrack.events.end() && found->offset == offset ? &*found : nullptr;
    };
    auto track = tracks.decode(index, sourceTrack.offset, [&](u32 offset) -> DecodedBytecodeCommand {
      const auto* event = eventAt(offset);
      if (event == nullptr) {
        Cursor cursor(reader, offset, sourceTrack.end, kKonamiPs1CommandKindPrefix, diagnostics);
        return cursor.unsupported("Invalid KonamiPS1 event address").stop();
      }
      return decodeEvent(reader, sourceTrack.end, *event, timing, diagnostics);
    });
    track.sourceTrackNumber = index;
    program.tracks.push_back(std::move(track));
  }
  return program;
}

}  // namespace vgmtrans::formats::konami_ps1
