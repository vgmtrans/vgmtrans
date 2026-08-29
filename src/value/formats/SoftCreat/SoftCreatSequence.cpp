/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/SoftCreat/SoftCreat.h"

#include "value/sequence/CommandSourceMap.h"
#include "value/sequence/CompiledCommandRuntime.h"

#include <algorithm>
#include <array>
#include <bitset>
#include <cmath>
#include <compare>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <fmt/format.h>

namespace vgmtrans::formats::softcreat {

using namespace core;

namespace {

constexpr u32 kCommandLimit = 32768;
constexpr u32 kStatesPerAddress = 16;
constexpr std::array<std::array<s8, 8>, 4> kFirPresets{{
    {{0x7f, 0, 0, 0, 0, 0, 0, 0}},
    {{0x58, -0x41, -0x25, -0x10, -2, 7, 0x0c, 0x0c}},
    {{0x0c, 0x21, 0x2b, 0x2b, 0x13, -2, -0x0d, -7}},
    {{0x34, 0x33, 0, -0x27, -0x1b, 1, -4, -0x15}},
}};

namespace math {

[[nodiscard]] constexpr u32 ticks(u8 value) { return value == 0 ? 256 : value; }

[[nodiscard]] constexpr u32 tempoMicrosecondsPerQuarter(u8 timer) {
  // Timer 2 overflows at 64 kHz. The audited main loop advances the sequencer
  // after four overflows, hence 62.5 us * timer per sequence tick.
  return static_cast<u32>(kPpqn) * 250u * (timer == 0 ? 256u : timer) / 4u;
}

[[nodiscard]] constexpr double signedGain(s8 value) { return value / 128.0; }

[[nodiscard]] std::optional<u8> firPreset(const std::array<s8, 8>& coefficients) {
  const auto found = std::ranges::find(kFirPresets, coefficients);
  return found == kFirPresets.end() ? std::nullopt
                                    : std::optional<u8>{static_cast<u8>(found - kFirPresets.begin())};
}

}  // namespace math

struct GainRow {
  u8 interval = 0;
  u8 attackStart = 0x7f;
  u8 attackSteps = 0;
  u8 attackPeak = 0x7f;
  u8 decaySteps = 0;
  u8 sustain = 0x7f;
  u8 releaseSteps = 0;
};

enum class GainMode : u8 {
  Preset,
  Stream,
};

enum class GainPhase : u8 {
  Attack,
  Decay,
  Sustain,
  Release,
  End,
};

enum class StereoSide : u8 { Left, Right };

struct RuntimeConfig {
  RetainedSource source;
  Layout layout;
};

struct ProgramState {
  explicit ProgramState(const RuntimeConfig& config) : echo(config.layout.echo) {}

  EchoState echo;
  std::bitset<256> flags;
  bool echoDisabled = false;
  bool initialized = false;
};

struct VibratoState {
  bool enabled = false;
  bool startsNegative = false;
  u8 delay = 0;
  u8 step = 0;
  u8 interval = 0;
};

struct TrackState {
  TrackState(const TrackProgram& sourceTrack, const RuntimeConfig& config)
      : data(config.source.reader()), layout(config.layout), trackNumber(sourceTrack.sourceTrackNumber) {}

  ByteReader data;
  Layout layout;
  u32 trackNumber = 0;
  u8 srcn = 0;
  s8 transpose = 0;
  u8 left = 0;
  u8 right = 0;
  u8 volume = 0;
  u8 pan = 0;
  s8 autoPan = 0;
  u8 volumeFade = 0;
  u8 fadeCounter = 0;
  u8 detune = 0;
  u8 portamentoStep = 0;
  u8 glissandoStep = 0;
  u8 glissandoInterval = 0;
  u16 glissandoCounter = 0;
  u8 trillOffset = 0;
  u8 trillHighTicks = 0;
  u8 trillLowTicks = 0;
  u16 trillCounter = 0;
  bool trillHigh = false;
  bool retrigger = true;
  bool bypassTranspose = false;
  bool perNoteVolume = false;
  std::optional<u16> drumTable;
  std::array<Address, 8> repeatStarts{};
  u8 repeatDepth = 0;

  PerformanceNoteId lastNote;
  std::optional<double> lastKey;
  u32 remaining = 0;
  u32 gateRemaining = 0;
  u8 directGate = 0;
  u8 releaseRemaining = 0;
  double referencePitch = 0.0;
  double targetPitch = 0.0;
  double currentPitch = 0.0;
  std::optional<double> lastPitchBend;
  u8 targetInternalNote = 0;
  u8 currentInternalNote = 0;
  bool haveCurrentPitch = false;
  VibratoState vibrato;
  bool vibratoOutputActive = false;

  GainMode gainMode = GainMode::Preset;
  bool gainRetriggers = true;
  GainRow gain;
  GainPhase gainPhase = GainPhase::Attack;
  u16 gainStep = 0;
  u16 gainCounter = 0;
  u8 gainLevel = 0x7f;
  std::optional<u8> lastEmittedGain;
  u8 releaseStart = 0x7f;
  u8 streamInterval = 0;
  u16 streamAddress = 0;
  u8 streamOffset = 0;
  u8 streamReleaseOffset = 0;
  PerformanceAutomationBinding gainAutomation;
};

struct Playback {
  TrackState& track;
  PerformanceEmitter& out;
  VmApi& vm;
  ProgramState& program;

  [[nodiscard]] u8 voiceBit() const { return static_cast<u8>(1u << std::min<u32>(track.trackNumber, 7)); }

  void beforeCommand() {
    if (!program.initialized) {
      program.initialized = true;
      emitReverb();
    }
  }

  void emitBalance() {
    const double scale = track.layout.musicVolume / 256.0;
    out.stereoBalance(math::signedGain(static_cast<s8>(track.left)) * scale,
                      math::signedGain(static_cast<s8>(track.right)) * scale);
  }

  void deriveBalance() {
    const u8 divided = static_cast<u8>((static_cast<u16>(track.volume) * track.pan) >> 8);
    track.left = static_cast<u8>(divided << 1);
    track.right = static_cast<u8>((static_cast<u8>(track.volume - divided) << 1) - 1u);
    if ((track.pan & 0x80) != 0) {
      track.left = track.volume;
    } else {
      track.right = track.volume;
    }
    emitBalance();
  }

  void emitReverb() {
    const int peak = std::max(std::abs(program.echo.left), std::abs(program.echo.right));
    const double send = program.echoDisabled || program.echo.voiceMask == 0 ? 0.0 : peak / 128.0;
    out.reverb(ReverbPerformanceEvent{
        .voiceMask = program.echo.voiceMask,
        .send = send,
        .leftGain = math::signedGain(program.echo.left),
        .rightGain = math::signedGain(program.echo.right),
        .delayMilliseconds = program.echo.delay * 16.0,
        .feedback = math::signedGain(program.echo.feedback),
        .filterIndex = math::firPreset(program.echo.fir),
    });
  }

  void instrument(u8 srcn) {
    track.srcn = srcn;
    out.instrument(InstrumentIdentity{.domain = std::string(kInstrumentDomain), .key = srcn},
                   InstrumentEnvelopeMode::PreserveDynamicOverride);
  }

  [[nodiscard]] u8 coarse(u8 srcn) const {
    const u32 address = track.layout.coarseTableAddress + srcn;
    return track.data.has(address, 1) ? track.data.u8At(address) : 0;
  }

  [[nodiscard]] u8 fine(u8 srcn) const {
    const u32 address = track.layout.fineTableAddress + srcn;
    return track.data.has(address, 1) ? track.data.u8At(address) : 0;
  }

  [[nodiscard]] u16 pitchTable(u8 internal) const {
    const u32 count = track.layout.pitchHighTableAddress - track.layout.pitchLowTableAddress;
    const u32 index = count == 0 ? 0 : std::min<u32>(internal, count - 1);
    return static_cast<u16>(track.data.u8At(track.layout.pitchLowTableAddress + index) |
                            (track.data.u8At(track.layout.pitchHighTableAddress + index) << 8));
  }

  [[nodiscard]] double tunedPitch(u8 internal, u8 srcn) const {
    const u32 base = pitchTable(internal);
    return base + ((base * fine(srcn)) >> 8);
  }

  [[nodiscard]] double outputPitch(double pitch) const {
    const u16 raw = static_cast<u16>(std::clamp(std::lround(pitch), 0l, 0xffffl));
    // The DSP writer adds detune to PITCHL after saving the carry from the
    // main pitch sum, so this adjustment deliberately cannot carry to PITCHH.
    return static_cast<double>((raw & 0xff00u) | static_cast<u8>(raw + track.detune));
  }

  void emitPitch() {
    if (!track.lastNote.valid() || track.referencePitch <= 0.0) {
      return;
    }
    const double physical = std::max(1.0, outputPitch(track.currentPitch));
    const double bend = 12.0 * std::log2(physical / track.referencePitch);
    if (!track.lastPitchBend || std::abs(*track.lastPitchBend - bend) > 0.000001) {
      out.pitchBend(bend);
      track.lastPitchBend = bend;
    }
  }

  void emitVibrato() {
    const auto& vibrato = track.vibrato;
    if (!vibrato.enabled || track.referencePitch <= 0.0 || vibrato.step == 0) {
      if (track.vibratoOutputActive) {
        out.vibratoDepth(0.0);
        track.vibratoOutputActive = false;
      }
      return;
    }
    const u32 halfCycle = math::ticks(static_cast<u8>(vibrato.interval >> 1));
    const double rawDepth = vibrato.step * halfCycle;
    const LfoPolarity polarity = vibrato.startsNegative ? LfoPolarity::Negative : LfoPolarity::Positive;
    const double minimum = vibrato.startsNegative
                               ? 12.0 * std::log2(std::max(1.0, track.referencePitch - rawDepth) /
                                                track.referencePitch)
                               : 0.0;
    const double maximum = vibrato.startsNegative
                               ? 0.0
                               : 12.0 * std::log2((track.referencePitch + rawDepth) / track.referencePitch);
    const LfoPerformanceContext context{
        .cyclesPerTick = 1.0 / (2.0 * halfCycle),
        .delayTicks = vibrato.delay,
        .delayIsTempoRelative = true,
        .shape = LfoShape{.waveform = LfoWaveform::Triangle},
        .polarity = polarity,
        .initialPhaseCycles = vibrato.startsNegative ? 0.25 : 0.75,
        .pitchRangeSemitones = ModulationRange{.minimum = minimum, .maximum = maximum},
        .sampleImmediatelyOnNote = true,
        .restartMode = LfoRestartMode::PhaseAndDelay,
    };
    out.vibratoDepth(std::max(std::abs(minimum), std::abs(maximum)), context);
    out.vibratoRateCyclesPerTick(*context.cyclesPerTick, context);
    track.vibratoOutputActive = true;
  }

  void emitGain() {
    if (track.gainAutomation.valid() && track.lastEmittedGain != track.gainLevel) {
      track.gainAutomation.output(out).expression(track.gainLevel / 127.0);
      track.lastEmittedGain = track.gainLevel;
    }
  }

  void attachGain(u32 length) {
    track.gainAutomation = out.noteEnvelope(PerformanceAutomationTarget::Expression, 1.0, length);
    track.lastEmittedGain.reset();
    out.replaceEnvelope(kNeutralGainEnvelope, VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks);
  }

  void restartGain() {
    if (track.gainMode == GainMode::Preset) {
      track.gainPhase = GainPhase::Attack;
      track.gainStep = 0;
      track.gainCounter = 1;
      tickPresetGain();
    } else {
      track.streamOffset = 0;
      track.streamReleaseOffset = 0;
      track.gainCounter = math::ticks(track.streamInterval);
      emitGain();
      tickStreamGain();
    }
  }

  void releaseGain() {
    if (track.gainMode == GainMode::Preset) {
      track.releaseStart = track.gainLevel;
      track.gainPhase = GainPhase::Release;
      track.gainStep = 0;
    } else if (track.streamReleaseOffset == 0) {
      u16 offset = 0;
      while (offset < 0x100 && track.data.has(static_cast<u16>(track.streamAddress + offset), 1) &&
             track.data.s8At(static_cast<u16>(track.streamAddress + offset)) >= 0) {
        ++offset;
      }
      track.streamReleaseOffset = static_cast<u8>(offset + 1);
      track.streamOffset = track.streamReleaseOffset;
    }
  }

  void tickPresetGain() {
    if (track.gainCounter != 0 && --track.gainCounter != 0) {
      return;
    }
    track.gainCounter = math::ticks(track.gain.interval);
    switch (track.gainPhase) {
      case GainPhase::Attack:
        if (track.gain.attackSteps == 0) {
          return;
        }
        track.gainLevel = track.gain.attackSteps == 1
                              ? track.gain.attackPeak
                              : static_cast<u8>(track.gain.attackStart +
                                                (track.gain.attackPeak - track.gain.attackStart) * track.gainStep /
                                                    (track.gain.attackSteps - 1u));
        if (++track.gainStep == track.gain.attackSteps) {
          track.gainPhase = GainPhase::Decay;
          track.gainStep = 0;
        }
        break;
      case GainPhase::Decay: {
        if (track.gain.decaySteps == 0) {
          return;
        }
        const u16 remaining = static_cast<u16>(track.gain.decaySteps - track.gainStep - 1u);
        if (remaining == 0) {
          track.gainLevel = track.gain.sustain;
          track.gainPhase = GainPhase::Sustain;
          track.gainStep = 0;
        } else {
          track.gainLevel = static_cast<u8>(
              track.gain.sustain +
              (track.gain.attackPeak - track.gain.sustain) * remaining / track.gain.decaySteps);
          ++track.gainStep;
        }
        break;
      }
      case GainPhase::Sustain:
        return;
      case GainPhase::Release: {
        if (track.gain.releaseSteps == 0) {
          return;
        }
        const u16 remaining = static_cast<u16>(track.gain.releaseSteps - track.gainStep - 1u);
        if (remaining == 0) {
          track.gainLevel = 0;
          track.gainPhase = GainPhase::End;
        } else {
          track.gainLevel = static_cast<u8>(track.releaseStart * remaining / track.gain.releaseSteps);
          ++track.gainStep;
        }
        break;
      }
      case GainPhase::End:
        return;
    }
    emitGain();
  }

  void tickStreamGain() {
    if (track.gainCounter != 0 && --track.gainCounter != 0) {
      return;
    }
    track.gainCounter = math::ticks(track.streamInterval);
    for (u32 redirects = 0; redirects < 256; ++redirects) {
      const u16 address = static_cast<u16>(track.streamAddress + track.streamOffset);
      if (!track.data.has(address, 1)) {
        return;
      }
      const u8 value = track.data.u8At(address);
      ++track.streamOffset;
      if (value < 0x80) {
        track.gainLevel = value;
        emitGain();
        return;
      }
      if (value == 0x80) {
        track.streamOffset = track.streamReleaseOffset;
        continue;
      }
      --track.streamOffset;
      return;
    }
  }

  void tickTargetPitch() {
    if (track.trillOffset != 0 && track.trillCounter != 0 && --track.trillCounter == 0) {
      track.trillHigh = !track.trillHigh;
      track.trillCounter = math::ticks(track.trillHigh ? track.trillHighTicks : track.trillLowTicks);
    }
    const u8 wanted = static_cast<u8>(track.targetInternalNote + (track.trillHigh ? track.trillOffset : 0));
    if (track.glissandoInterval != 0) {
      if (track.glissandoCounter != 0 && --track.glissandoCounter == 0) {
        track.glissandoCounter = math::ticks(track.glissandoInterval);
        const int distance = static_cast<int>(wanted) - track.currentInternalNote;
        const int step = std::max<int>(track.glissandoStep, 1);
        track.currentInternalNote =
            static_cast<u8>(track.currentInternalNote + std::clamp(distance, -step, step));
      }
      track.targetPitch = tunedPitch(track.currentInternalNote, track.srcn);
    } else {
      track.currentInternalNote = wanted;
      track.targetPitch = tunedPitch(wanted, track.srcn);
    }

    if (track.portamentoStep == 0) {
      track.currentPitch = track.targetPitch;
    } else {
      const double step = track.portamentoStep;
      const double distance = track.targetPitch - track.currentPitch;
      track.currentPitch = std::abs(distance) <= step ? track.targetPitch
                                                      : track.currentPitch + std::copysign(step, distance);
    }
  }

  void tick() {
    if (track.remaining != 0) {
      --track.remaining;
      if (track.gateRemaining != 0 && track.remaining == track.gateRemaining) {
        releaseGain();
      }
    }
    if (track.gainAutomation.valid() && track.gainMode == GainMode::Preset) {
      tickPresetGain();
    } else if (track.gainAutomation.valid() && track.gainMode == GainMode::Stream) {
      tickStreamGain();
    }
    tickTargetPitch();
    if (track.portamentoStep == 0) {
      emitPitch();
    }

    if (track.autoPan != 0) {
      const int next = track.pan + track.autoPan;
      if (next < 0 || next > 255) {
        track.autoPan = static_cast<s8>(-track.autoPan);
      }
      track.pan = static_cast<u8>(std::clamp(next, 0, 255));
    }
    if (++track.fadeCounter == 16) {
      track.fadeCounter = 0;
      if (track.volumeFade != 0) {
        track.volume = static_cast<u8>((static_cast<u16>(track.volume) * track.volumeFade) >> 8);
        if (track.volume == 0) {
          track.volumeFade = 0;
        }
        deriveBalance();
      }
    }
  }

  void noteVolume(std::optional<u8> value) {
    if (value) {
      track.volume = *value;
      deriveBalance();
    }
  }

  [[nodiscard]] std::optional<Address> resolveNoteOperands(u8& duration, std::optional<u8>& volume,
                                                           bool literalDuration, Address continuation) {
    // A loop may reach the same note before and after BF enables per-note
    // volume. Reinterpret the compiled operands in the runtime state.
    if (volume.has_value() == track.perNoteVolume) {
      return std::nullopt;
    }
    if (track.perNoteVolume) {
      if (!track.data.has(continuation.value, 1)) {
        volume.reset();
        return std::nullopt;
      }
      if (literalDuration) {
        volume = duration;
        duration = track.data.u8At(continuation.value);
      } else {
        volume = track.data.u8At(continuation.value);
      }
      return Address{continuation.value + 1};
    }
    if (literalDuration) {
      duration = *volume;
    }
    volume.reset();
    return continuation.value == 0 ? std::nullopt : std::optional<Address>{Address{continuation.value - 1}};
  }

  [[nodiscard]] static Effects wait(u32 ticks, std::optional<Address> continuation) {
    Effects effects = Effects::wait(ticks);
    if (continuation) {
      effects.flowOverride = CommandTransition::jump(*continuation, JumpSemantics::FiniteBranch);
    }
    return effects;
  }

  void beginDuration(u8 encodedDuration) {
    track.remaining = math::ticks(encodedDuration);
    track.gateRemaining = track.directGate != 0 ? static_cast<u8>(encodedDuration - track.directGate)
                                               : track.releaseRemaining;
  }

  [[nodiscard]] Effects rest(u8 encodedDuration, std::optional<u8> volume, bool literalDuration,
                             Address continuation) {
    const std::optional<Address> runtimeContinuation =
        resolveNoteOperands(encodedDuration, volume, literalDuration, continuation);
    noteVolume(volume);
    beginDuration(encodedDuration);
    if (track.lastNote.valid() && track.lastKey) {
      track.lastNote = out.note(NotePerformanceEvent{
          .key = *track.lastKey,
          .linearVelocity = 1.0,
          .durationTicks = track.remaining,
          .extendsPrevious = true,
          .restartsEnvelope = false,
          .restartsLfoPhase = false,
      });
    }
    return wait(track.remaining, runtimeContinuation);
  }

  [[nodiscard]] Effects note(u8 rawNote, u8 encodedDuration, std::optional<u8> noteVolume, bool literalDuration,
                             Address continuation) {
    const std::optional<Address> runtimeContinuation =
        resolveNoteOperands(encodedDuration, noteVolume, literalDuration, continuation);
    this->noteVolume(noteVolume);
    if (track.drumTable && rawNote >= 0x12) {
      const u16 entry = static_cast<u16>(*track.drumTable + (rawNote - 0x12u) * 4u);
      if (track.data.has(entry, 4)) {
        instrument(track.data.u8At(entry));
        loadEnvelope(track.data.u8At(entry + 1));
        track.pan = track.data.u8At(entry + 2);
        rawNote = track.data.u8At(entry + 3);
        deriveBalance();
      }
    }

    const bool bypass = std::exchange(track.bypassTranspose, false);
    const s8 noteTranspose = bypass ? 0 : track.transpose;
    const double key = 24.0 + static_cast<u8>(rawNote + noteTranspose);
    const u8 internal = static_cast<u8>(rawNote + coarse(track.srcn) + noteTranspose);
    const u32 length = math::ticks(encodedDuration);
    const bool continues = !track.retrigger && track.lastNote.valid();
    const PerformanceNoteId previousNote = track.lastNote;
    NotePerformanceEvent event{
        .key = key,
        .linearVelocity = 1.0,
        .durationTicks = length,
        .restartsEnvelope = !continues,
        .restartsLfoPhase = !continues,
    };
    if (continues) {
      if (track.lastKey && std::abs(*track.lastKey - key) < 0.000001) {
        event.extendsPrevious = true;
        track.lastNote = out.note(std::move(event));
      } else {
        track.lastNote = out.continueVoice(track.lastNote, std::move(event));
      }
    } else {
      track.lastNote = out.note(std::move(event));
      attachGain(length);
    }
    if (track.gainRetriggers) {
      restartGain();
    } else if (!continues) {
      emitGain();
    }
    track.lastKey = key;
    track.targetInternalNote = internal;
    if (!track.haveCurrentPitch || track.glissandoInterval == 0) {
      track.currentInternalNote = internal;
    }
    track.targetPitch = tunedPitch(track.currentInternalNote, track.srcn);
    track.referencePitch = tunedPitch(internal, track.srcn);
    if (track.haveCurrentPitch && track.portamentoStep != 0 &&
        std::abs(track.currentPitch - track.targetPitch) >= 1.0) {
      const double startKey =
          key + 12.0 * std::log2(std::max(1.0, outputPitch(track.currentPitch)) / track.referencePitch);
      const double targetKey =
          key + 12.0 * std::log2(std::max(1.0, outputPitch(track.targetPitch)) / track.referencePitch);
      const u32 slideTicks = static_cast<u32>(
          std::ceil(std::abs(track.targetPitch - track.currentPitch) / track.portamentoStep));
      auto slide = out.pitchSlide(track.lastNote, startKey, targetKey, slideTicks).preferPitchBend();
      if (continues) {
        slide.continueFrom(previousNote);
      }
    } else {
      track.currentPitch = track.targetPitch;
    }
    track.haveCurrentPitch = true;
    track.trillHigh = false;
    track.trillCounter = math::ticks(track.trillLowTicks);
    if (track.portamentoStep == 0) {
      emitPitch();
    }
    emitVibrato();

    beginDuration(encodedDuration);
    return wait(length, runtimeContinuation);
  }

  void directVolume(StereoSide side, s8 value) {
    (side == StereoSide::Right ? track.right : track.left) = static_cast<u8>(value);
    track.volume = 0;
    emitBalance();
  }
  void streamGain(u8 interval, u16 address) {
    track.streamInterval = interval;
    track.streamAddress = address;
    setGainMode(GainMode::Stream);
    track.streamOffset = 1;
    track.streamReleaseOffset = 0;
    track.gainCounter = 1;
    if (track.data.has(address, 1)) {
      const u8 initial = track.data.u8At(address);
      if (initial < 0x80) {
        track.gainLevel = initial;
        emitGain();
      }
    }
  }
  void vibrato(bool negative, u8 delay, u8 step, u8 interval) {
    track.vibrato = {.enabled = true, .startsNegative = negative, .delay = delay, .step = step, .interval = interval};
  }
  void vibratoOff() {
    track.vibrato.enabled = false;
    emitVibrato();
  }
  void glissando(u8 step, u8 interval) {
    track.glissandoStep = step;
    track.glissandoInterval = interval;
    track.glissandoCounter = math::ticks(interval);
  }
  void trill(u8 offset, u8 highTicks, u8 lowTicks) {
    track.trillOffset = offset;
    track.trillHighTicks = highTicks;
    track.trillLowTicks = lowTicks;
    track.trillCounter = math::ticks(lowTicks);
  }
  void loadEnvelope(u8 index) {
    const u16 address = static_cast<u16>(track.layout.envelopeTableAddress + index * 7u);
    if (track.data.has(address, 7)) {
      setEnvelope(GainRow{track.data.u8At(address), track.data.u8At(address + 1), track.data.u8At(address + 2),
                          track.data.u8At(address + 3), track.data.u8At(address + 4), track.data.u8At(address + 5),
                          track.data.u8At(address + 6)});
    }
  }
  void setEnvelope(GainRow row) {
    track.gain = row;
    setGainMode(GainMode::Preset);
  }
  void setGainMode(GainMode mode) {
    track.gainMode = mode;
    track.gainRetriggers = true;
  }
  void noiseClock(u8) {
    program.echoDisabled = true;
    emitReverb();
  }
  [[nodiscard]] Effects repeatStart(u8 count, Address start) {
    if (track.repeatDepth >= track.repeatStarts.size()) {
      return vm.end();
    }
    const u8 slot = track.repeatDepth++;
    track.repeatStarts[slot] = start;
    vm.repeatCounter(slot).start(math::ticks(count));
    return {};
  }
  [[nodiscard]] Effects repeatEnd() {
    if (track.repeatDepth == 0) {
      return vm.end();
    }
    const u8 slot = static_cast<u8>(track.repeatDepth - 1);
    RepeatCounter repeat = vm.repeatCounter(slot);
    if (repeat.consumeReplay()) {
      return vm.finiteBranch(track.repeatStarts[slot]);
    }
    repeat.finish();
    track.repeatDepth = slot;
    return {};
  }
  [[nodiscard]] Effects return_() { return vm.inSubroutine() ? vm.return_() : vm.end(); }
  void setFlag(u8 value, bool enabled) { program.flags.set(value, enabled); }
  [[nodiscard]] bool flag(u8 value) const { return program.flags.test(value); }
  [[nodiscard]] Effects flagJump(u8 value, bool expected, Address destination) {
    return flag(value) == expected ? vm.finiteBranch(destination) : Effects{};
  }
  [[nodiscard]] Effects waitFlag(u8 value, bool expected, Address self) {
    if (flag(value) == expected) {
      return {};
    }
    Effects effects = vm.finiteBranch(self);
    effects.advanceTicks = 1;
    return effects;
  }
  void echoVoice(bool enabled) {
    program.echo.voiceMask = enabled ? static_cast<u8>(program.echo.voiceMask | voiceBit())
                                     : static_cast<u8>(program.echo.voiceMask & ~voiceBit());
    emitReverb();
  }
  void echoVolume(StereoSide side, s8 value) {
    (side == StereoSide::Right ? program.echo.right : program.echo.left) = value;
    emitReverb();
  }
  void echoFeedback(s8 value) {
    program.echo.feedback = value;
    emitReverb();
  }
  void echoFir(std::array<s8, 8> values) {
    program.echo.fir = values;
    emitReverb();
  }
  void volumePan(u8 volume, u8 pan) {
    track.volume = volume;
    track.pan = pan;
    deriveBalance();
  }
  void pan(u8 pan) {
    track.pan = pan;
    deriveBalance();
  }
  void volume(u8 volume) {
    track.volume = volume;
    deriveBalance();
  }
  void autoPan(s8 step) {
    track.autoPan = step;
    const double cycles = step == 0 ? 0.0 : std::abs(step) / 510.0;
    const LfoPerformanceContext context{
        .cyclesPerTick = cycles,
        .shape = LfoShape{.waveform = LfoWaveform::Triangle},
        .polarity = LfoPolarity::Bipolar,
        .sampleImmediatelyOnNote = true,
    };
    out.panLfoDepth(step == 0 ? 0.0 : 1.0, context);
    out.panLfoRateCyclesPerTick(cycles, context);
  }
};

using Cursor = CompilerCursor<TrackState, Playback>;

struct DecodeState {
  u8 defaultDuration = 0;
  bool explicitDuration = false;
  bool perNoteVolume = false;
  std::optional<u16> drumTable;

  friend auto operator<=>(const DecodeState&, const DecodeState&) = default;
};

[[nodiscard]] u8 canonicalOpcode(Version version, u8 opcode) {
  if (version == Version::MaximumCarnage) {
    if (opcode >= 0x8c && opcode <= 0xa8) {
      return static_cast<u8>(opcode + 1);
    }
    if (opcode >= 0xa9 && opcode <= 0xbc) {
      return static_cast<u8>(opcode + 6);
    }
  }
  if (version == Version::LateNoEcho && opcode >= 0xaa && opcode <= 0xb0) {
    return 0xb0;
  }
  return opcode;
}

[[nodiscard]] bool isAlias(Version version, u8 opcode) {
  return dialect(version).noteAliasOpcode == opcode;
}

[[nodiscard]] GainRow readGainRow(Cursor::Event& event) {
  return {
      event.u8("interval", SemanticOperandRole::Duration),
      event.u8("attack_start", SemanticOperandRole::Level),
      event.u8("attack_steps", SemanticOperandRole::Count),
      event.u8("attack_peak", SemanticOperandRole::Level),
      event.u8("decay_steps", SemanticOperandRole::Count),
      event.u8("sustain", SemanticOperandRole::Level),
      event.u8("release_steps", SemanticOperandRole::Count),
  };
}

[[nodiscard]] DecodedBytecodeCommand decodeCommand(ByteReader reader, const Layout& layout, u32 begin,
                                                   DecodeState& state, std::vector<Diagnostic>* diagnostics,
                                                   SequenceReferences* references) {
  Cursor cursor(reader, begin, "softcreat", diagnostics);
  if (!cursor.hasOpcode()) {
    return cursor.truncated();
  }
  const u8 opcode = cursor.opcode();
  if (opcode < 0x80 || isAlias(layout.version, opcode)) {
    u8 note = opcode;
    auto event = cursor.command(opcode == 0 ? "Rest" : (opcode < 0x80 ? "Note" : "Indexed Note"),
                                opcode == 0 ? SequenceSemantic::Rest : SequenceSemantic::Note);
    if (opcode >= 0x80) {
      const u8 index = event.u8("index", SemanticOperandRole::NoteKey);
      if (layout.noteAliasTableAddress && reader.has(*layout.noteAliasTableAddress + index, 1)) {
        note = reader.u8At(*layout.noteAliasTableAddress + index);
        event.derived("note", note, SourceValueDisplay::MidiNote, SemanticOperandRole::NoteKey);
      }
    } else if (opcode != 0) {
      event.opcodeValue("note", static_cast<u8>(opcode + 24), SourceValueDisplay::MidiNote,
                        SemanticOperandRole::NoteKey);
    }
    std::optional<u8> volume;
    // The driver fetches per-note volume before duration. Its rest branch
    // rejoins this path, so rests carry and apply the value too.
    if (state.perNoteVolume) {
      volume = event.u8("volume", SemanticOperandRole::Level);
    }
    const bool literalDuration = state.defaultDuration == 0 || state.explicitDuration;
    const u8 duration =
        literalDuration ? event.u8("duration", SemanticOperandRole::Duration) : state.defaultDuration;
    state.explicitDuration = false;
    if (references != nullptr && note != 0 && state.drumTable && note >= 0x12) {
      const u16 entry = static_cast<u16>(*state.drumTable + (note - 0x12u) * 4u);
      if (reader.has(entry, 4)) {
        references->srcns.insert(reader.u8At(entry));
      }
    }
    const Address continuation = event.nextAddress();
    return note == 0 ? event.invoke<&Playback::rest>(duration, volume, literalDuration, continuation)
                     : event.invoke<&Playback::note>(note, duration, volume, literalDuration, continuation);
  }
  if (opcode >= dialect(layout.version).commandCutoff || opcode == 0x80) {
    return cursor.command("End", SequenceSemantic::End).end();
  }
  if (layout.version == Version::Plok && opcode == 0xb9) {
    return cursor.sourceOnly("Driver Stack Assertion", "stack-assertion");
  }

  const u8 command = canonicalOpcode(layout.version, opcode);
  switch (command) {
    case 0x81: {
      auto event = cursor.command("Jump", SequenceSemantic::Jump);
      return event.loopCandidate(event.addressLe("destination", SemanticOperandRole::JumpTarget));
    }
    case 0x82: {
      auto event = cursor.command("Call", SequenceSemantic::Call);
      return event.call(event.addressLe("destination", SemanticOperandRole::CallTarget));
    }
    case 0x83:
      return cursor.command("Return", SequenceSemantic::Return).invokeFlow<&Playback::return_>().return_();
    case 0x84: {
      auto event = cursor.command("Repeat Start", SequenceSemantic::Repeat);
      const u8 count = event.u8("count", SemanticOperandRole::Count);
      return event.invokeFlow<&Playback::repeatStart>(count, event.nextAddress());
    }
    case 0x85:
      return cursor.command("Repeat End", SequenceSemantic::Repeat).invokeFlow<&Playback::repeatEnd>();
    case 0x86: {
      auto event = cursor.command("Default Duration", SequenceSemantic::State);
      state.defaultDuration = event.u8("duration", SemanticOperandRole::Duration);
      return event;
    }
    case 0x87:
      state.explicitDuration = true;
      return cursor.command("Explicit Next Duration", SequenceSemantic::State);
    case 0x88: {
      auto event = cursor.command("Transpose", SequenceSemantic::Pitch);
      return event.set<&TrackState::transpose>(event.s8("semitones", SemanticOperandRole::Pitch));
    }
    case 0x89: {
      auto event = cursor.command("Instrument", SequenceSemantic::Program);
      const u8 srcn = event.u8("srcn", SemanticOperandRole::InstrumentProgram);
      if (references != nullptr) {
        references->srcns.insert(srcn);
      }
      return event.invoke<&Playback::instrument>(srcn);
    }
    case 0x8a:
    case 0x8b: {
      auto event = cursor.command(command == 0x8a ? "Direct Left Volume" : "Direct Right Volume",
                                  SequenceSemantic::Level);
      const StereoSide side = command == 0x8a ? StereoSide::Left : StereoSide::Right;
      return event.invoke<&Playback::directVolume>(side, event.s8("volume", SemanticOperandRole::Level));
    }
    case 0x8c: {
      auto event = cursor.command("Streamed GAIN Envelope", SequenceSemantic::Envelope);
      const u8 interval = event.u8("interval", SemanticOperandRole::Duration);
      return event.invoke<&Playback::streamGain>(interval, event.u16le("address", SourceValueDisplay::Address,
                                                                      SemanticOperandRole::Address));
    }
    case 0x8d: {
      auto event = cursor.command("Pitch Detune", SequenceSemantic::Pitch);
      return event.set<&TrackState::detune>(event.u8("pitch", SemanticOperandRole::Pitch));
    }
    case 0x8e:
    case 0x8f: {
      auto event = cursor.command(command == 0x8e ? "Vibrato +" : "Vibrato -", SequenceSemantic::Modulation);
      const u8 delay = event.u8("delay", SemanticOperandRole::Duration);
      const u8 step = event.u8("pitch_step", SemanticOperandRole::Modulation);
      return event.invoke<&Playback::vibrato>(command == 0x8f, delay, step,
                                              event.u8("half_cycle", SemanticOperandRole::Duration));
    }
    case 0x90: {
      auto event = cursor.command("Raw Pitch Portamento", SequenceSemantic::Portamento);
      return event.set<&TrackState::portamentoStep>(event.u8("pitch_step", SemanticOperandRole::Pitch));
    }
    case 0x91:
      return cursor.command("Vibrato Off", SequenceSemantic::Modulation).invoke<&Playback::vibratoOff>();
    case 0x92: {
      auto event = cursor.command("Gate Time", SequenceSemantic::Envelope);
      const u8 value = event.u8("elapsed_ticks", SemanticOperandRole::Duration);
      return event.set<&TrackState::directGate>(value).set<&TrackState::releaseRemaining>(u8{0});
    }
    case 0x93: {
      auto event = cursor.command("Release Time", SequenceSemantic::Envelope);
      const u8 value = event.u8("remaining_ticks", SemanticOperandRole::Duration);
      return event.set<&TrackState::releaseRemaining>(value).set<&TrackState::directGate>(u8{0});
    }
    case 0x94: {
      auto event = cursor.command("Stepped Glissando", SequenceSemantic::Portamento);
      const u8 step = event.u8("semitone_step", SemanticOperandRole::Pitch);
      return event.invoke<&Playback::glissando>(step, event.u8("interval", SemanticOperandRole::Duration));
    }
    case 0x95:
      return cursor.command("Glissando Off", SequenceSemantic::Portamento)
          .invoke<&Playback::glissando>(u8{0}, u8{0});
    case 0x96: {
      auto event = cursor.command("Trill", SequenceSemantic::Modulation);
      const u8 offset = event.u8("semitones", SemanticOperandRole::Pitch);
      const u8 highTicks = event.u8("high_ticks", SemanticOperandRole::Duration);
      return event.invoke<&Playback::trill>(offset, highTicks,
                                            event.u8("low_ticks", SemanticOperandRole::Duration));
    }
    case 0x97: {
      auto event = cursor.command("GAIN Envelope Preset", SequenceSemantic::Envelope);
      return event.invoke<&Playback::loadEnvelope>(event.u8("preset", SemanticOperandRole::InstrumentProgram));
    }
    case 0x98:
    case 0x99:
      return cursor.command(command == 0x98 ? "Noise On" : "Noise Off", SequenceSemantic::State);
    case 0x9a: {
      auto event = cursor.command("Noise Clock / Echo Disable", SequenceSemantic::State);
      return event.invoke<&Playback::noiseClock>(event.u8("clock", SourceValueDisplay::Hex));
    }
    case 0x9b:
      return cursor.command("Software GAIN Envelope", SequenceSemantic::Envelope)
          .invoke<&Playback::setGainMode>(GainMode::Preset);
    case 0x9c:
      return cursor.command("Keep GAIN Envelope", SequenceSemantic::Envelope)
          .set<&TrackState::gainRetriggers>(false);
    case 0x9d:
      return cursor.command("Streamed GAIN Mode", SequenceSemantic::Envelope)
          .invoke<&Playback::setGainMode>(GainMode::Stream);
    case 0x9e:
    case 0x9f:
      return cursor.command(command == 0x9e ? "Retrigger On" : "Legato / Retrigger Off", SequenceSemantic::State)
          .set<&TrackState::retrigger>(command == 0x9e);
    case 0xa0:
      return cursor.command("Bypass Transpose Once", SequenceSemantic::Pitch).set<&TrackState::bypassTranspose>(true);
    case 0xa1:
      return cursor.sourceOnly("Load Song 1 / Driver Reset", "driver-reset").end();
    case 0xa2: {
      auto event = cursor.command("Dynamic GAIN Envelope", SequenceSemantic::Envelope);
      return event.invoke<&Playback::setEnvelope>(readGainRow(event));
    }
    case 0xa3:
    case 0xa4: {
      auto event = cursor.command(command == 0xa3 ? "Random Jump" : "Random Call",
                                  command == 0xa3 ? SequenceSemantic::Jump : SequenceSemantic::Call);
      const u8 count = event.u8("choices", SemanticOperandRole::Count);
      std::vector<Address> choices;
      choices.reserve(count);
      for (u32 choice = 0; choice < count && event.ok(); ++choice) {
        choices.push_back(event.addressLe(fmt::format("destination_{}", choice),
                                          command == 0xa3 ? SemanticOperandRole::JumpTarget
                                                          : SemanticOperandRole::CallTarget));
      }
      if (choices.empty()) {
        return event.stop();
      }
      for (size_t choice = 1; choice < choices.size(); ++choice) {
        event.mayBranchTo(choices[choice]);
      }
      // Randomness is external to the portable sequence VM. Preserve all
      // source paths and render the first path deterministically.
      return command == 0xa3 ? event.jump(choices.front()) : event.call(choices.front());
    }
    case 0xa5:
    case 0xa6: {
      auto event = cursor.command(command == 0xa5 ? "Set Flag" : "Clear Flag", SequenceSemantic::State);
      return event.invoke<&Playback::setFlag>(event.u8("flag", SemanticOperandRole::State), command == 0xa5);
    }
    case 0xa7:
    case 0xa8: {
      auto event = cursor.command(command == 0xa7 ? "Jump If Flag Set" : "Jump If Flag Clear", SequenceSemantic::Jump);
      const u8 flag = event.u8("flag", SemanticOperandRole::State);
      const Address destination = event.addressLe("destination", SemanticOperandRole::JumpTarget);
      return event.invoke<&Playback::flagJump>(flag, command == 0xa7, destination).mayBranchTo(destination);
    }
    case 0xa9:
    case 0xba: {
      auto event = cursor.command(command == 0xa9 ? "Wait Until Flag Set" : "Wait Until Flag Clear",
                                  SequenceSemantic::Wait);
      const u8 flag = event.u8("flag", SemanticOperandRole::State);
      return event.invokeFlow<&Playback::waitFlag>(flag, command == 0xa9, Address{begin}).mayBranchTo(Address{begin});
    }
    case 0xaa:
    case 0xab:
      return cursor.command(command == 0xaa ? "Echo Voice On" : "Echo Voice Off", SequenceSemantic::State)
          .invoke<&Playback::echoVoice>(command == 0xaa);
    case 0xac:
    case 0xad: {
      auto event = cursor.command(command == 0xac ? "Echo Left Volume" : "Echo Right Volume",
                                  SequenceSemantic::Level);
      const StereoSide side = command == 0xac ? StereoSide::Left : StereoSide::Right;
      return event.invoke<&Playback::echoVolume>(side, event.s8("volume", SemanticOperandRole::Level));
    }
    case 0xae: {
      auto event = cursor.command("Echo Feedback", SequenceSemantic::State);
      return event.invoke<&Playback::echoFeedback>(event.s8("feedback"));
    }
    case 0xaf: {
      auto event = cursor.command("Echo FIR Coefficients", SequenceSemantic::State);
      std::array<s8, 8> values{};
      for (u32 coefficient = 0; coefficient < values.size(); ++coefficient) {
        values[coefficient] = event.s8(fmt::format("coefficient_{}", coefficient));
      }
      return event.invoke<&Playback::echoFir>(values);
    }
    case 0xb0: {
      auto event = cursor.command("Volume Decay", SequenceSemantic::Level);
      return event.set<&TrackState::volumeFade>(event.u8("factor", SemanticOperandRole::Level));
    }
    case 0xb1:
    case 0xb2:
      return cursor.sourceOnly(command == 0xb1 ? "Driver Voice Flag On" : "Driver Voice Flag Off", "driver-flag");
    case 0xb3: {
      auto event = cursor.command("Volume and Pan", SequenceSemantic::Pan);
      const u8 volume = event.u8("volume", SemanticOperandRole::Level);
      return event.invoke<&Playback::volumePan>(volume, event.u8("pan", SemanticOperandRole::Pan));
    }
    case 0xb4: {
      auto event = cursor.command("Auto Pan", SequenceSemantic::Modulation);
      return event.invoke<&Playback::autoPan>(event.s8("step", SemanticOperandRole::Modulation));
    }
    case 0xb5: {
      auto event = cursor.sourceOnly("Trigger Sound Effect", "sound-effect");
      static_cast<void>(event.u8("effect", SemanticOperandRole::InstrumentProgram));
      return event;
    }
    case 0xb6: {
      auto event = cursor.command("Timer / Tempo", SequenceSemantic::Tempo);
      return event.emitTempo(
          math::tempoMicrosecondsPerQuarter(event.u8("timer", SemanticOperandRole::Duration)));
    }
    case 0xb7:
      return cursor.sourceOnly("Live Volume/Pan Mode", "live-mixer");
    case 0xb8:
      return cursor.sourceOnly("Wait for Host Stop", "host-wait").end();
    case 0xbb:
    case 0xbc:
      return cursor.sourceOnly(command == 0xbb ? "SFX Allocation On" : "SFX Allocation Off", "sfx-allocation");
    case 0xbd: {
      auto event = cursor.command("Pan", SequenceSemantic::Pan);
      return event.invoke<&Playback::pan>(event.u8("pan", SemanticOperandRole::Pan));
    }
    case 0xbe: {
      auto event = cursor.command("Volume", SequenceSemantic::Level);
      return event.invoke<&Playback::volume>(event.u8("volume", SemanticOperandRole::Level));
    }
    case 0xbf:
    case 0xc0:
      state.perNoteVolume = command == 0xbf;
      return cursor.command(command == 0xbf ? "Per-Note Volume On" : "Per-Note Volume Off", SequenceSemantic::State)
          .set<&TrackState::perNoteVolume>(command == 0xbf);
    case 0xc1: {
      auto event = cursor.command("Drum Table On", SequenceSemantic::Instrument);
      const u16 table = event.u16le("table", SourceValueDisplay::Address, SemanticOperandRole::InstrumentTablePointer);
      state.drumTable = table;
      return event.set<&TrackState::drumTable>(std::optional<u16>{table});
    }
    case 0xc2:
      state.drumTable.reset();
      return cursor.command("Drum Table Off", SequenceSemantic::Instrument)
          .set<&TrackState::drumTable>(std::optional<u16>{});
    case 0xc3:
    case 0xc4: {
      auto event = cursor.sourceOnly(command == 0xc3 ? "External SFX Slot" : "External SFX Priority",
                                     "sound-effect-state");
      static_cast<void>(event.u8(command == 0xc3 ? "slot" : "priority"));
      return event;
    }
    case 0xc5:
      return cursor.sourceOnly("Play External Sound Effect", "sound-effect");
    case 0xc6:
      return cursor.sourceOnly("Conditional SFX End", "sound-effect-condition");
    default:
      return cursor.unsupported(fmt::format("Opcode ${:02X}", opcode)).stop();
  }
}

struct DiscoveryPoint {
  u32 offset = 0;
  DecodeState state;
  std::vector<u32> returns;
  std::vector<u32> repeats;

  friend auto operator<=>(const DiscoveryPoint&, const DiscoveryPoint&) = default;
};

struct DiscoveredCommand {
  DecodedBytecodeCommand command;
  DecodeState initialState;
};

[[nodiscard]] TrackProgram decodeTrack(ByteReader reader, const Layout& layout, u32 trackNumber, u32 startAddress,
                                       std::optional<AssetId> sequence, std::optional<SourceAnnotationId> parent,
                                       SourceMapBuilder* sourceMap, std::vector<Diagnostic>* diagnostics,
                                       SequenceReferences* references) {
  TrackDecodeScope scope{
      .reader = reader,
      .bytecodeEnd = kAramSize,
      .maxCommands = kCommandLimit,
      .sequenceAsset = sequence,
      .parentAnnotation = parent,
      .sourceMap = sourceMap,
  };
  auto session = scope.begin(trackNumber, startAddress);
  std::vector<DiscoveryPoint> pending{{.offset = startAddress}};
  std::set<DiscoveryPoint> visited;
  std::map<u32, DiscoveredCommand> commands;
  std::map<u32, u32> stateVisits;

  const auto queue = [&](Address address, DiscoveryPoint point) {
    if (address.value < kAramSize && reader.has(address.value, 1)) {
      point.offset = static_cast<u32>(address.value);
      pending.push_back(std::move(point));
    }
  };

  while (!pending.empty() && visited.size() < kCommandLimit) {
    DiscoveryPoint point = std::move(pending.back());
    pending.pop_back();
    if (!reader.has(point.offset, 1) || stateVisits[point.offset] >= kStatesPerAddress ||
        !visited.insert(point).second) {
      continue;
    }
    ++stateVisits[point.offset];
    DecodeState nextState = point.state;
    DecodedBytecodeCommand decoded =
        decodeCommand(reader, layout, point.offset, nextState, diagnostics, references);
    const auto [existing, inserted] = commands.try_emplace(
        point.offset, DiscoveredCommand{.command = decoded, .initialState = point.state});
    if (!inserted && existing->second.command.range.size != decoded.range.size) {
      const DecodeState& original = existing->second.initialState;
      DecodeState withoutVolumeDifference = point.state;
      withoutVolumeDifference.perNoteVolume = original.perNoteVolume;
      const bool volumeSuffixOnly = (decoded.opcode < 0x80 || isAlias(layout.version, decoded.opcode)) &&
                                    original == withoutVolumeDifference &&
                                    original.perNoteVolume != point.state.perNoteVolume;
      if (!volumeSuffixOnly) {
        if (diagnostics != nullptr) {
          diagnostics->push_back(Diagnostic{.severity = Severity::Warning,
                                            .message = fmt::format("SoftCreat command ${:04X} has incompatible state",
                                                                  point.offset),
                                            .range = decoded.range});
        }
        continue;
      }
    }
    point.state = nextState;

    const u8 command = canonicalOpcode(layout.version, decoded.opcode);
    const Address continuation = decoded.flow.continuation;
    if (command == 0x84) {
      point.repeats.push_back(static_cast<u32>(continuation.value));
      queue(continuation, std::move(point));
      continue;
    }
    if (command == 0x85) {
      if (point.repeats.empty()) {
        continue;
      }
      queue(Address{point.repeats.back()}, point);
      point.repeats.pop_back();
      queue(continuation, std::move(point));
      continue;
    }

    const auto queueAlternatives = [&] {
      for (const Address target : decoded.discoveryTargets) {
        queue(target, point);
      }
    };
    switch (decoded.flow.defaultTransition.kind) {
      case CommandTransitionKind::Fallthrough:
        queueAlternatives();
        queue(continuation, std::move(point));
        break;
      case CommandTransitionKind::Jump:
        queueAlternatives();
        if (const auto target = decoded.flow.defaultDestination()) {
          queue(*target, std::move(point));
        }
        break;
      case CommandTransitionKind::Call:
        point.returns.push_back(static_cast<u32>(continuation.value));
        queueAlternatives();
        if (const auto target = decoded.flow.defaultDestination()) {
          queue(*target, std::move(point));
        }
        break;
      case CommandTransitionKind::Return:
        if (!point.returns.empty()) {
          const Address target{point.returns.back()};
          point.returns.pop_back();
          queue(target, std::move(point));
        }
        break;
      case CommandTransitionKind::End:
      case CommandTransitionKind::EndSection:
        break;
    }
  }

  for (auto& [offset, discovered] : commands) {
    session.findOrAppend(std::move(discovered.command), offset);
  }
  return session.finish();
}

}  // namespace

const SequenceProgramConfig& sequenceConfig() {
  static const SequenceProgramConfig config{
      .commandKindPrefix = "softcreat",
      .timebase = Timebase{.ppqn = kPpqn},
      .behavior =
          SequenceProgramBehavior{
              .commandLimit = kCommandLimit,
              .preferredPitchTransitionRendering = PitchTransitionRenderingHint::PitchBend,
              .initialSourceInstrument = InstrumentIdentity{.domain = std::string(kInstrumentDomain), .key = 0},
              .initialLevel = 1.0,
              .initialPitchBendRangeSemitones = 24,
              .initialTempoMicrosecondsPerQuarter = math::tempoMicrosecondsPerQuarter(0x85),
          },
  };
  return config;
}

SequenceRuntime sequenceRuntime(RetainedSource source, const Layout& layout) {
  return makeCompiledRuntime<Cursor, ProgramState>(RuntimeConfig{.source = std::move(source), .layout = layout});
}

TrackProgram decodeSourceTrack(ByteReader reader, const Layout& layout, u32 trackNumber, u32 startAddress,
                               std::vector<Diagnostic>* diagnostics) {
  return decodeTrack(reader, layout, trackNumber, startAddress, std::nullopt, std::nullopt, nullptr, diagnostics,
                     nullptr);
}

SequenceParse decodeSequence(ByteReader reader, const Layout& layout, AssetId sequenceId, SourceMapBuilder* sourceMap,
                             std::vector<Diagnostic>* diagnostics) {
  SequenceProgram program = sequenceConfig().makeProgram();
  program.behavior.initialTempoMicrosecondsPerQuarter = math::tempoMicrosecondsPerQuarter(layout.initialTimer);
  SequenceReferences references;

  std::optional<SourceAnnotationId> headerParent;
  if (sourceMap != nullptr) {
    auto header = sourceMap->header("SoftCreat Sequence Header", layout.sequenceHeaderRange)
                      .kind("softcreat-sequence-header")
                      .owner(ObjectRefs::asset(sequenceId));
    headerParent = header.id();
    sourceMap->field("Selected Song", reader.range(0xe4, 1), layout.songIndex)
        .kind("softcreat-song-index")
        .owner(ObjectRefs::asset(sequenceId))
        .parent(*headerParent);
  }

  for (u32 track = 0; track < layout.tracks.size(); ++track) {
    const TrackPointer& pointer = layout.tracks[track];
    if (pointer.address == 0 || !reader.has(pointer.address, 1)) {
      continue;
    }
    if (sourceMap != nullptr && headerParent) {
      sourceMap->pointer(fmt::format("Track {} Pointer", track), pointer.lowSource,
                         SourceTarget{reader.range(pointer.address, 1)})
          .kind("softcreat-track-pointer")
          .field("low", pointer.lowSource, pointer.address & 0xff, SourceValueDisplay::Address)
          .field("high", pointer.highSource, pointer.address >> 8, SourceValueDisplay::Address)
          .owner(ObjectRefs::sequenceTrack(sequenceId, track))
          .parent(*headerParent);
    }
    program.tracks.push_back(decodeTrack(reader, layout, track, pointer.address, sequenceId, headerParent, sourceMap,
                                         diagnostics, &references));
  }
  program.runtime = sequenceRuntime(RetainedSource::copyOf(reader), layout);
  return SequenceParse{.program = std::move(program), .references = std::move(references)};
}

}  // namespace vgmtrans::formats::softcreat
