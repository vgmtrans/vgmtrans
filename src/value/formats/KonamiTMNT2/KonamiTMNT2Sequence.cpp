/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/KonamiTMNT2/KonamiTMNT2.h"

#include "value/base/LevelScale.h"
#include "value/sequence/CommandSourceMap.h"
#include "value/sequence/CompiledCommandRuntime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

namespace vgmtrans::formats::konami_tmnt2 {

using namespace core;

namespace {

constexpr u32 kMaxTrackCommands = 262144;

[[nodiscard]] double timerRate(u8 clkb) {
  return kChipClock / (1024.0 * (256.0 - clkb));
}

[[nodiscard]] double effectiveTickRate(u8 clkb, u8 skipInterval) {
  const double base = timerRate(clkb);
  return skipInterval > 1 ? base * (skipInterval - 1.0) / skipInterval : base;
}

[[nodiscard]] double ym2151LfoRate(u8 raw) {
  // The YM2151 advances its 30-bit LFO phase accumulator once per
  // clock/(2 * 32 operators). Register 18 is a 4.4 floating-point step with
  // an implied leading one.
  const u32 step = (0x10u | (raw & 0x0f)) << (raw >> 4);
  return (kChipClock / 64.0) * step / static_cast<double>(u64{1} << 30);
}

[[nodiscard]] u32 tempoMicrosecondsPerQuarter(u8 clkb, u8 skipInterval) {
  return static_cast<u32>(
      std::clamp<double>(std::lround(kPpqn * 1'000'000.0 / effectiveTickRate(clkb, skipInterval)), 1.0, 60'000'000.0));
}

[[nodiscard]] double sampledReleaseSeconds(Version version, u8 packed, u8 volume, double ticksPerSecond) {
  const u8 delay = version == Version::Vendetta ? 1 : packed >> 4;
  const u8 rate = version == Version::Vendetta ? packed : packed & 0x0f;
  if (delay == 0 || rate == 0 || ticksPerSecond <= 0.0) {
    return std::numeric_limits<double>::infinity();
  }
  const u32 steps = (std::max<int>(4, volume & 0x7f) - 4 + rate - 1) / rate;
  return steps * delay / ticksPerSecond;
}

[[nodiscard]] double ymReleaseSeconds(const std::array<u8, 4>& rates) {
  u8 slowest = 15;
  for (u8 rate : rates) {
    slowest = std::min(slowest, static_cast<u8>(rate & 0x0f));
  }
  // YM2151 envelope rates are exponential. This is the driver's audible
  // four-carrier approximation for portable envelopes; the native patch data
  // remains available to synth-aware exporters.
  return slowest == 0 ? std::numeric_limits<double>::infinity() : 6.0 * std::pow(0.5, slowest / 2.0);
}

[[nodiscard]] s32 signMagnitude(u8 value) {
  return (value & 0x80) != 0 ? -static_cast<s32>(value & 0x7f) : static_cast<s32>(value);
}

[[nodiscard]] s32 invertedSignMagnitude(u8 value) {
  return (value & 0x80) != 0 ? static_cast<s32>(value & 0x7f) : -static_cast<s32>(value);
}

[[nodiscard]] s32 packedTranspose(u8 value) {
  s32 semitones = (value & 0x0f) + ((value >> 4) & 3) * 12;
  return (value & 0x80) != 0 ? -semitones : semitones;
}

[[nodiscard]] std::pair<double, double> k053260Pan(u8 raw, Version version) {
  u8 index = raw & 7;
  if (index == 0) {
    index = 4;
  }
  if (version == Version::Vendetta) {
    index = static_cast<u8>(-index) & 7;
  }
  static constexpr std::array<std::pair<double, double>, 8> gains{
      std::pair{0.707106781, 0.707106781}, std::pair{1.0, 0.0},           std::pair{0.913421, 0.406738},
      std::pair{0.819153, 0.579193},       std::pair{0.707107, 0.707107}, std::pair{0.579193, 0.819153},
      std::pair{0.406738, 0.913421},       std::pair{0.0, 1.0},
  };
  return gains[index];
}

struct RuntimeConfig {
  Version version = Version::Tmnt2;
  u8 clkb = 0xf2;
  u8 tickSkipInterval = 0;
  std::array<TrackChip, 12> trackChips{};
  std::vector<SampleInstrument> instruments;
  std::vector<std::vector<Drum>> drums;
  std::vector<u8> ymPan;
};

struct ProgramState {
  explicit ProgramState(const RuntimeConfig& config)
      : version(config.version), clkb(config.clkb), tickSkipInterval(config.tickSkipInterval),
        instruments(config.instruments), drums(config.drums), ymPan(config.ymPan) {}

  Version version;
  u8 clkb;
  u8 tickSkipInterval;
  s32 globalTranspose = 0;
  s32 ymMasterAttenuation = 0;
  s32 sampleMasterAttenuation = 0;
  std::vector<SampleInstrument> instruments;
  std::vector<std::vector<Drum>> drums;
  std::vector<u8> ymPan;

  [[nodiscard]] double ticksPerSecond() const { return effectiveTickRate(clkb, tickSkipInterval); }
};

struct TrackState {
  TrackState(const TrackProgram& trackProgram, const RuntimeConfig& config)
      : version(config.version), chip(config.trackChips[trackProgram.sourceTrackNumber]),
        sourceTrackNumber(trackProgram.sourceTrackNumber), baseDuration(version == Version::Vendetta ? 1 : 3) {}

  Version version;
  TrackChip chip;
  u32 sourceTrackNumber = 0;
  u8 state = 0;
  u8 program = 0;
  bool percussion = false;
  u8 drumBank = 0;
  u8 octave = 0;
  s32 transpose = 0;
  u32 rawBaseDuration = 1;
  u32 baseDuration = 3;
  bool vendettaShortDuration = false;
  u32 extension = 0;
  bool specialDuration = false;
  bool subtractSpecialDuration = false;
  u32 specialDurationValue = 0;
  bool halfDuration = false;
  u32 halfDurationBackup = 0;
  bool halfDurationRemainder = false;
  u8 gate = 0;
  u8 attenuation = 0;
  u8 dxAttenuation = 0;
  u8 dxMultiplier = 1;
  u8 instrumentVolume = 0x7f;
  u8 instrumentRelease = 0;
  u8 sequencePan = 0;
  u8 instrumentPan = 0;
  s32 pitchBendRaw = 0;
  u8 portamento = 0;
  u8 nativeLfoFrequency = 0;
  u8 nativeLfoPitchDepth = 0;
  u8 nativeLfoAmplitudeDepth = 0;
  u8 nativeLfoWaveform = 0;
  u32 nativeLfoDelay = 0;
  u32 nativeLfoRampInterval = 0;
  std::optional<double> previousKey;
  PerformanceNoteId previousNote;
  std::array<Address, 2> loopStart;
  std::array<u8, 2> loopCount{};
  u8 warpCounter = 0;
  Address warpOrigin;
  Address warpDestination;
};

struct Playback {
  TrackState& track;
  PerformanceEmitter& out;
  VmApi& vm;
  ProgramState& program;

  [[nodiscard]] bool fm() const { return track.chip == TrackChip::Ym2151; }

  [[nodiscard]] LfoPerformanceContext nativeLfoContext() const {
    static constexpr std::array<LfoWaveform, 4> shapes{LfoWaveform::SawtoothDown, LfoWaveform::Square,
                                                       LfoWaveform::Triangle, LfoWaveform::Noise};
    const double hertz = track.nativeLfoFrequency == 0 ? 0.0 : ym2151LfoRate(track.nativeLfoFrequency);
    return LfoPerformanceContext{
        .frequencyHz = hertz,
        .shape = LfoShape{.waveform = shapes[track.nativeLfoWaveform & 3]},
        .phaseRunsAtZeroDepth = true,
        .tremoloGainMode = TremoloGainMode::NoBoost,
    };
  }

  void emitNativeLfoDepth(PerformanceEmitter emitter, u8 sensitivity) const {
    static constexpr std::array<double, 8> pitchSemitones{0.0, 0.05, 0.1, 0.2, 0.5, 1.0, 4.0, 7.0};
    static constexpr std::array<double, 4> amplitudeDecibels{0.0, 23.9, 47.8, 95.6};
    const auto context = nativeLfoContext();
    emitter.vibratoDepth(pitchSemitones[sensitivity] * track.nativeLfoPitchDepth / 127.0, context);
    emitter.tremoloDepth(amplitudeDecibels[sensitivity >> 1] * track.nativeLfoAmplitudeDepth / 127.0, context);
  }

  void beginNativeLfo(u32 noteDuration) {
    if (!fm() || track.nativeLfoFrequency == 0) {
      return;
    }
    emitNativeLfoDepth(out, 0);
    if (track.nativeLfoRampInterval == 0) {
      return;
    }
    for (u8 sensitivity = 1; sensitivity < 8; ++sensitivity) {
      const u32 tick = track.nativeLfoDelay + sensitivity * track.nativeLfoRampInterval;
      if (tick >= noteDuration) {
        break;
      }
      emitNativeLfoDepth(out.at(vm.tick() + tick), sensitivity);
    }
  }

  void tempo(u8 skip) {
    program.tickSkipInterval = skip;
    out.tempo(tempoMicrosecondsPerQuarter(program.clkb, skip));
  }

  void addTempoInterval(s8 delta) { tempo(static_cast<u8>(program.tickSkipInterval + delta)); }

  void emitPan() {
    if (fm()) {
      u8 raw = track.sequencePan;
      if (raw == 0 && track.program < program.ymPan.size()) {
        raw = program.ymPan[track.program];
      }
      const bool left = raw == 1 || raw == 3 || (raw & 0x40) != 0;
      const bool right = raw == 2 || raw == 3 || (raw & 0x80) != 0;
      out.stereoBalance(left ? 1.0 : 0.0, right ? 1.0 : 0.0);
      return;
    }
    const u8 raw = track.sequencePan != 0 ? track.sequencePan : track.instrumentPan;
    const auto [left, right] = k053260Pan(raw, track.version);
    out.stereoBalance(left, right);
  }

  void setPan(u8 raw) {
    track.sequencePan = raw;
    emitPan();
  }

  void setProgram(u8 raw) {
    track.program = track.version == Version::Tmnt2 && fm() ? raw & 0x7f : raw;
    if (fm()) {
      out.instrument(InstrumentIdentity{.domain = std::string(kYm2151Domain), .key = track.program});
      emitPan();
      return;
    }
    if (track.percussion) {
      return;
    }
    out.instrument(InstrumentIdentity{.domain = std::string(kK053260Domain), .key = track.program});
    if (track.program >= program.instruments.size()) {
      return;
    }
    const auto& instrument = program.instruments[track.program];
    track.instrumentVolume = instrument.volume;
    track.instrumentPan = instrument.pan;
    track.instrumentRelease = instrument.release;
    if (instrument.gate != 0) {
      track.gate = instrument.gate;
    }
    volumeEnvelope((instrument.volume & 0x80) != 0 ? instrument.volume & 0x7f : 0);
    emitPan();
  }

  void setPercussion(u8 raw) {
    if (fm()) {
      track.state = raw;
      return;
    }
    if (raw == 0) {
      track.percussion = false;
      setProgram(track.program);
      return;
    }
    track.percussion = true;
    track.drumBank = raw;
    out.instrument(InstrumentIdentity{.domain = std::string(kK053260Domain), .key = 0x100});
  }

  void setBaseDuration(u8 raw) {
    track.rawBaseDuration = raw == 0 ? 256 : raw;
    track.baseDuration = track.version == Version::Vendetta ? track.rawBaseDuration : track.rawBaseDuration * 3;
    track.halfDuration = false;
  }

  void initialize(u8 base, u8 rawProgram, u8 attenuation, u8 gate) {
    track.percussion = false;
    setBaseDuration(base);
    setProgram(rawProgram);
    track.attenuation = attenuation & 0x7f;
    track.gate = gate;
  }

  void initializeDrums(u8 packed, u8 attenuation) {
    setBaseDuration(packed & 0x0f);
    track.attenuation = attenuation & 0x7f;
    track.drumBank = static_cast<u8>((packed >> 4) - 1);
    track.percussion = true;
    out.instrument(InstrumentIdentity{.domain = std::string(kK053260Domain), .key = 0x100});
  }

  void attenuationStep(u8 step) {
    const u32 value = step * (fm() ? track.dxMultiplier : std::max<u8>(1, track.dxMultiplier));
    track.dxAttenuation = static_cast<u8>(std::min<u32>(value, 0x7f));
  }

  void attenuationMultiplier(u8 value) { track.dxMultiplier = fm() ? std::min<u8>(value, 0x12) : value; }

  void setAttenuation(u8 value) { track.attenuation = value & 0x7f; }

  void extendDuration() { track.extension += 16; }

  void toggleDurationMultiplier() {
    if (track.version == Version::Vendetta) {
      track.vendettaShortDuration = !track.vendettaShortDuration;
      return;
    }
    track.baseDuration =
        track.baseDuration == track.rawBaseDuration ? track.rawBaseDuration * 3 : track.rawBaseDuration;
  }

  void toggleHalfDuration() {
    if (track.halfDuration) {
      track.baseDuration = track.halfDurationBackup;
      track.halfDuration = false;
      track.halfDurationRemainder = false;
    } else {
      track.halfDurationBackup = track.baseDuration;
      track.baseDuration /= 2;
      track.halfDuration = true;
    }
  }

  void transpose(u8 raw) {
    if ((raw & 0x40) != 0 && (track.version == Version::Tmnt2 || track.version == Version::Vendetta)) {
      program.globalTranspose = packedTranspose(raw);
    } else if ((raw & 0x40) == 0) {
      track.transpose = packedTranspose(raw);
    }
  }

  void pitchBend(u8 raw) {
    track.pitchBendRaw = signMagnitude(raw);
    out.pitchBendRange(12);
    out.pitchBend(track.pitchBendRaw / 64.0);
  }

  void setPortamento(u8 raw) {
    track.portamento = raw;
    if (raw != 0) {
      const double ticks = track.version == Version::Vendetta ? std::pow(2.0, std::min<u8>(raw, 12)) : raw;
      out.pitchTransitionSettings(ticks / program.ticksPerSecond() * 1000.0);
    }
  }

  [[nodiscard]] u32 duration(u8 opcode) {
    u32 units = opcode & 0x0f;
    if (units == 0) {
      units = 16;
    }
    units += track.extension;
    track.extension = 0;
    if (track.version == Version::Vendetta) {
      return units * (track.vendettaShortDuration ? 2 : 6) * track.baseDuration;
    }
    if (track.specialDuration) {
      track.specialDuration = false;
      track.subtractSpecialDuration = true;
      track.specialDurationValue = units * 3;
      return track.specialDurationValue;
    }
    u32 result = units * track.baseDuration;
    if (track.halfDuration && (track.halfDurationBackup & 1) != 0) {
      track.halfDurationRemainder = !track.halfDurationRemainder;
      result += track.halfDurationRemainder ? units : 0;
    }
    if (track.subtractSpecialDuration) {
      result = result > track.specialDurationValue ? result - track.specialDurationValue : 1;
      track.subtractSpecialDuration = false;
    }
    return std::max<u32>(1, result);
  }

  [[nodiscard]] double noteGain(s32 dynamicAttenuation = 0) const {
    const s32 master = fm() ? program.ymMasterAttenuation : program.sampleMasterAttenuation;
    const s32 attenuation = track.attenuation + track.dxAttenuation + master + dynamicAttenuation;
    if (fm()) {
      return std::pow(10.0, -0.75 * std::clamp<s32>(attenuation, 0, 127) / 20.0);
    }
    return std::clamp<s32>((track.instrumentVolume & 0x7f) - attenuation, 0, 127) / 127.0;
  }

  [[nodiscard]] Effects note(u8 opcode) {
    const u32 wait = duration(opcode);
    if ((opcode & 0xf0) == 0) {
      return Effects::wait(wait);
    }

    double key = 0.0;
    u32 gate = wait;
    double gain = noteGain();
    if (!fm() && track.percussion) {
      const u8 slot = opcode >> 4;
      key = track.drumBank * 16.0 + slot;
      if (track.drumBank < program.drums.size() && slot < program.drums[track.drumBank].size()) {
        const auto& drum = program.drums[track.drumBank][slot];
        if (drum.valid) {
          track.instrumentVolume = drum.volume;
          track.instrumentPan = drum.pan;
          track.instrumentRelease = drum.release;
          gain = noteGain();
          if (track.sequencePan == 0) {
            emitPan();
          }
        }
      }
    } else {
      key = (opcode >> 4) - 1 + track.octave + track.transpose + program.globalTranspose;
      if (fm()) {
        key += 12;
      }
      if (track.gate != 0) {
        gate = std::max<u32>(1, static_cast<u64>(wait) * track.gate / 256);
      }
    }
    key = std::clamp(key, 0.0, 127.0);

    beginNativeLfo(gate);
    const bool nativeLfo = fm() && track.nativeLfoFrequency != 0;
    const auto note = out.note(NotePerformanceEvent{
        .key = key,
        .linearVelocity = gain,
        .durationTicks = gate,
        .restartsLfoPhase = !nativeLfo,
        .restartsVibratoLfoPhase = !nativeLfo,
        .restartsTremoloLfoPhase = !nativeLfo,
    });
    if (!track.percussion && track.portamento != 0 && track.previousKey && track.previousNote.valid() &&
        std::abs(*track.previousKey - key) > 0.001) {
      out.pitchSlide(note, *track.previousKey, key, track.portamento).useCurrentPortamentoTiming();
    }
    track.previousKey = key;
    track.previousNote = note;
    return Effects::wait(wait);
  }

  void sampledRelease(u8 packed) {
    track.instrumentRelease = packed;
    const double seconds =
        sampledReleaseSeconds(track.version, packed, track.instrumentVolume, program.ticksPerSecond());
    out.updateEnvelope(Envelope{.releaseSeconds = seconds}, EnvelopeFields::Release,
                       VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks);
  }

  void ymRelease(u8 first, u8 second) {
    const std::array<u8, 4> rates{static_cast<u8>(first >> 4), static_cast<u8>(first & 0x0f),
                                  static_cast<u8>(second >> 4), static_cast<u8>(second & 0x0f)};
    out.updateEnvelope(Envelope{.releaseSeconds = ymReleaseSeconds(rates)}, EnvelopeFields::Release,
                       VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks);
  }

  void ymReleaseUniform(u8 rate) {
    // The TMNT2 handler executes `AND $0f; SUB $0f` before writing the
    // operator RR nibbles. In four-bit arithmetic that maps 0..15 to
    // 1..15,0 rather than storing the sequence nibble directly.
    const u8 nativeRate = static_cast<u8>((rate + 1) & 0x0f);
    ymRelease(static_cast<u8>((nativeRate << 4) | nativeRate), static_cast<u8>((nativeRate << 4) | nativeRate));
  }

  void keyOff() {
    if (track.previousNote.valid()) {
      static_cast<void>(out.setPreviousNoteEnd(vm.tick()));
    }
  }

  void softwareVibrato(u8 selector, u8 packed) {
    const bool enabled = selector != 0;
    u8 depth = packed & 0x0f;
    if (enabled && track.version == Version::SunsetRiders && depth == 0) {
      depth = 1;
    }
    const u32 delay =
        track.version == Version::Tmnt2 ? ((packed >> 4) * 2 + 1) : ((packed >> 4) * track.baseDuration + 1);
    const LfoPerformanceContext context{
        .cyclesPerTick = enabled ? std::optional<double>{1.0 / 16.0} : std::optional<double>{0.0},
        .delayTicks = delay,
        .delayIsTempoRelative = true,
        .shape = LfoShape{.waveform = LfoWaveform::Triangle},
        .initialPhaseCycles = 0.0,
        .phaseRunsAtZeroDepth = false,
    };
    out.vibratoDepth(enabled ? depth / 8.0 : 0.0, context);
    out.vibratoRateCyclesPerTick(enabled ? 1.0 / 16.0 : 0.0, context);
    out.vibratoDelayTicks(enabled ? delay : 0);
  }

  void volumeEnvelope(u8 selector) {
    const bool enabled = selector != 0;
    const double depth = enabled ? std::min(1.0, std::abs(static_cast<int>(selector & 0x7f) - 0x40) / 64.0) : 0.0;
    const LfoPerformanceContext context{
        .cyclesPerTick = enabled ? std::optional<double>{1.0 / 16.0} : std::optional<double>{0.0},
        .shape = LfoShape{.waveform = LfoWaveform::Triangle},
        .tremoloGainMode = TremoloGainMode::NoBoost,
    };
    out.tremoloLinearGainDepth(depth, context);
    out.tremoloRateCyclesPerTick(enabled ? 1.0 / 16.0 : 0.0, context);
  }

  void nativeLfo(u8 frequency, u8 pmd, u8 amd, u8 waveform, u8 delay) {
    track.nativeLfoFrequency = frequency;
    track.nativeLfoPitchDepth = pmd;
    track.nativeLfoAmplitudeDepth = amd;
    track.nativeLfoWaveform = waveform & 3;
    if (frequency == 0) {
      track.nativeLfoRampInterval = 0;
      emitNativeLfoDepth(out, 0);
      out.vibratoRate(0.0, nativeLfoContext());
      out.tremoloRate(0.0, nativeLfoContext());
      return;
    }
    if (track.version == Version::Vendetta) {
      track.nativeLfoDelay = 0;
      track.nativeLfoRampInterval = delay / 2;
    } else {
      track.nativeLfoDelay = track.version == Version::Tmnt2 ? delay : std::max<u32>(1, delay);
      track.nativeLfoRampInterval = std::max<u32>(1, (waveform & 0xf0) >> 3);
    }
    const auto context = nativeLfoContext();
    emitNativeLfoDepth(out, 0);
    out.vibratoRate(*context.frequencyHz, context);
    out.tremoloRate(*context.frequencyHz, context);
  }

  void nativeLfoTiming(u8 raw) {
    if (track.version == Version::Vendetta) {
      track.nativeLfoRampInterval = raw / 2;
    } else {
      track.nativeLfoDelay = track.version == Version::Tmnt2 ? raw : std::max<u32>(1, raw);
    }
  }

  void steppedModulation(u8 count, u8 pitchStep, u8 packedVolume) {
    const u32 ticks = count == 0 ? 256 : count;
    const s32 pitchDelta = signMagnitude(pitchStep);
    const s32 volumeDelta = (packedVolume & 0x80) != 0 ? packedVolume & 0x0f : -static_cast<s32>(packedVolume & 0x0f);
    const u32 interval = ((packedVolume >> 4) & 7) == 0 ? 256 : (packedVolume >> 4) & 7;
    s32 pitch = 0;
    s32 volume = 0;
    u32 volumeCountdown = interval;
    out.pitchBendRange(24);
    for (u32 tick = 1; tick < ticks; ++tick) {
      pitch += pitchDelta;
      out.at(vm.tick() + tick).pitchBend((track.pitchBendRaw + pitch) / 64.0);
      if (--volumeCountdown == 0) {
        volume += volumeDelta;
        volumeCountdown = interval;
        out.at(vm.tick() + tick).expression(noteGain(-volume));
      }
    }
    out.at(vm.tick() + ticks).pitchBend(track.pitchBendRaw / 64.0);
    out.at(vm.tick() + ticks).expression(noteGain());
  }

  void masterAdjust(u8 ym, u8 sampled) {
    const bool invertedYm = program.version == Version::Tmnt2 || program.version == Version::BellsWhistles;
    const bool invertedSample = program.version == Version::Tmnt2;
    program.ymMasterAttenuation = invertedYm ? invertedSignMagnitude(ym) : signMagnitude(ym);
    program.sampleMasterAttenuation = invertedSample ? invertedSignMagnitude(sampled) : signMagnitude(sampled);
    out.expression(noteGain());
  }

  void setOctave(u8 opcode) {
    if (!fm() && track.percussion) {
      track.drumBank = static_cast<u8>(opcode - 0xf1);
    } else {
      track.octave = static_cast<u8>((opcode & 7) * 12);
    }
  }

  void loopStart(u8 slot, Address address) {
    track.loopStart[slot] = address;
    track.loopCount[slot] = 0;
  }

  [[nodiscard]] Effects loopEnd(u8 slot, u16 plays, Address destination) {
    if (plays == 0xffff) {
      return vm.declaredLoop(destination);
    }
    return vm.countedRepeatUntil(slot, std::max<u16>(1, plays), destination);
  }

  [[nodiscard]] Effects warp(Address next, bool followedByWarp) {
    ++track.warpCounter;
    if (track.warpCounter == 1) {
      track.warpOrigin = next;
    } else if ((track.warpCounter & 1) == 0) {
      if (track.warpCounter != 2) {
        return vm.finiteBranch(track.warpDestination);
      }
    } else {
      track.warpDestination = Address{next.value + (followedByWarp ? 1 : 0)};
      if (followedByWarp) {
        track.warpCounter = 0xff;
      }
      return vm.finiteBranch(track.warpOrigin);
    }
    return {};
  }

  void tick() {}
};

using Cursor = CompilerCursor<TrackState, Playback>;

[[nodiscard]] Address destination(Cursor::Event& event, u32 programOffset, SemanticOperandRole role) {
  const auto encoded = event.rawU16le("encoded_destination", SourceValueDisplay::Address);
  return event.resolvedValue("destination", encoded, Address{programOffset + encoded.value},
                             SourceValueDisplay::Address, role);
}

[[nodiscard]] DecodedBytecodeCommand ignored(Cursor& cursor, std::string_view label, u8 bytes = 0) {
  auto event = cursor.sourceOnly(label);
  for (u8 index = 0; index < bytes; ++index) {
    static_cast<void>(event.u8("data_" + std::to_string(index + 1), SourceValueDisplay::Hex));
  }
  return event;
}

struct StaticFlow {
  std::array<std::map<u32, Address>, 2> loopEnds;
  std::set<u32> returns;
};

struct DecodeState {
  std::array<Address, 2> loops;
  std::array<bool, 2> calls{};
  const StaticFlow* flow = nullptr;
};

[[nodiscard]] DecodedBytecodeCommand decodeCommand(ByteReader reader, u32 begin, const Layout& layout,
                                                   const TrackLayout& trackLayout, DecodeState& state,
                                                   std::vector<Diagnostic>* diagnostics) {
  Cursor cursor(reader, begin, static_cast<u32>(layout.program.endOffset()), "konami-tmnt2", diagnostics);
  if (!cursor.hasOpcode()) {
    return cursor.truncated();
  }
  const u8 opcode = cursor.opcode();
  const bool vendetta = layout.version == Version::Vendetta;
  const bool fm = trackLayout.chip == TrackChip::Ym2151;

  if (opcode < 0xd0) {
    return cursor
        .command((opcode & 0xf0) == 0 ? "Rest" : "Note",
                 (opcode & 0xf0) == 0 ? SequenceSemantic::Rest : SequenceSemantic::Note)
        .invokeFlow<&Playback::note>(opcode);
  }
  if (opcode <= 0xd7) {
    return cursor.command("Attenuation Step", SequenceSemantic::Level)
        .invoke<&Playback::attenuationStep>(static_cast<u8>(opcode & 7));
  }

  switch (opcode) {
    case 0xd8: {
      auto event = cursor.command("Attenuation Multiplier", SequenceSemantic::Level);
      return event.invoke<&Playback::attenuationMultiplier>(event.u8("multiplier"));
    }
    case 0xd9:
      return cursor.command("Extend Next Duration", SequenceSemantic::State).invoke<&Playback::extendDuration>();
    case 0xda: {
      auto event = cursor.command("Pan", SequenceSemantic::Pan);
      return event.invoke<&Playback::setPan>(event.u8("pan"));
    }
    case 0xdb: {
      auto event = cursor.command("Gate Time", SequenceSemantic::Envelope);
      return event.set<&TrackState::gate>(event.u8("fraction"));
    }
    case 0xdc: {
      if (vendetta) {
        return ignored(cursor, "Unused");
      }
      if (fm) {
        if (layout.version == Version::Tmnt2) {
          auto event = cursor.command("Operator Release Rate", SequenceSemantic::Envelope);
          return event.invoke<&Playback::ymReleaseUniform>(event.u8("rate"));
        }
        auto event = cursor.command("Operator Release Rates", SequenceSemantic::Envelope);
        const u8 first = event.u8("operators_1_2", SourceValueDisplay::Hex, SemanticOperandRole::Value);
        const u8 second = event.u8("operators_3_4", SourceValueDisplay::Hex, SemanticOperandRole::Value);
        return event.invoke<&Playback::ymRelease>(first, second);
      }
      auto event = cursor.command("Sample Release", SequenceSemantic::Envelope);
      const u8 packed = event.u8("delay_and_rate", SourceValueDisplay::Hex, SemanticOperandRole::Value);
      event.derived("delay", static_cast<u8>(packed >> 4), SemanticOperandRole::Duration);
      event.derived("attenuation_step", static_cast<u8>(packed & 0x0f), SemanticOperandRole::Level);
      return event.invoke<&Playback::sampledRelease>(packed);
    }
    case 0xdd:
      return cursor.command("Duration Multiplier Toggle", SequenceSemantic::State)
          .invoke<&Playback::toggleDurationMultiplier>();
    case 0xde:
      if (vendetta) {
        return ignored(cursor, "Unused");
      }
      return cursor.command("One-Shot Duration Scale", SequenceSemantic::State).invoke([](Playback& playback) {
        playback.track.specialDuration = true;
      });
    case 0xdf:
      if (vendetta) {
        return ignored(cursor, "Unused");
      }
      return cursor.command("Half Duration Toggle", SequenceSemantic::State).invoke<&Playback::toggleHalfDuration>();
    case 0xe0: {
      auto event = cursor.command("Initialize Channel", SequenceSemantic::State);
      u8 base = event.u8("base_duration", SemanticOperandRole::Duration);
      if (base == 0) {
        const u8 skip = event.u8("tick_skip_interval", SemanticOperandRole::Duration);
        event.invoke<&Playback::tempo>(skip);
        base = event.u8("effective_base_duration", SemanticOperandRole::Duration);
      }
      if (!fm && (base & 0xf0) != 0) {
        const u8 attenuation = event.u8("attenuation", SemanticOperandRole::Level);
        return event.invoke<&Playback::initializeDrums>(base, attenuation);
      }
      const u8 instrument = event.u8("program", SemanticOperandRole::InstrumentProgram);
      const u8 attenuation = event.u8("attenuation", SemanticOperandRole::Level);
      const u8 gate = event.u8("gate_fraction", SemanticOperandRole::Duration);
      return event.invoke<&Playback::initialize>(base, instrument, attenuation, gate);
    }
    case 0xe1: {
      auto event = cursor.command(fm ? "Channel State" : "Percussion Bank", SequenceSemantic::Instrument);
      return event.invoke<&Playback::setPercussion>(event.u8("value"));
    }
    case 0xe2: {
      auto event = cursor.command("Base Duration", SequenceSemantic::State);
      return event.invoke<&Playback::setBaseDuration>(event.u8("ticks", SemanticOperandRole::Duration));
    }
    case 0xe3: {
      auto event = cursor.command("Program", SequenceSemantic::Program);
      return event.invoke<&Playback::setProgram>(event.u8("program", SemanticOperandRole::InstrumentProgram));
    }
    case 0xe4: {
      auto event = cursor.command("Attenuation", SequenceSemantic::Level);
      return event.invoke<&Playback::setAttenuation>(event.u8("attenuation", SemanticOperandRole::Level));
    }
    case 0xe5: {
      const bool supported = !vendetta;
      if (!supported) {
        return ignored(cursor, "Unused");
      }
      auto event = cursor.command("Software Vibrato", SequenceSemantic::Modulation);
      const u8 selector = event.u8("waveform");
      const u8 packed = selector == 0 ? 0 : event.u8("delay_and_depth", SourceValueDisplay::Hex);
      return event.invoke<&Playback::softwareVibrato>(selector, packed);
    }
    case 0xe6: {
      if (!fm || vendetta || layout.version != Version::SunsetRiders) {
        return ignored(cursor, "Unused");
      }
      auto event = cursor.command("Tick Interval Add", SequenceSemantic::Tempo);
      return event.invoke<&Playback::addTempoInterval>(event.s8("delta", SourceValueDisplay::SignedDecimal));
    }
    case 0xe7: {
      if (vendetta) {
        return ignored(cursor, "Unused");
      }
      if (!fm) {
        auto event = cursor.command("Volume Envelope", SequenceSemantic::Modulation);
        return event.invoke<&Playback::volumeEnvelope>(event.u8("selector"));
      }
      return ignored(cursor, "Unused");
    }
    case 0xe8: {
      return ignored(cursor, "Unused");
    }
    case 0xe9: {
      if (!fm && !vendetta) {
        return ignored(cursor, "Unused");
      }
      auto event = cursor.command("YM2151 Native LFO", SequenceSemantic::Modulation);
      const u8 frequency = event.u8("frequency", SemanticOperandRole::Modulation);
      if (frequency == 0) {
        return event.invoke<&Playback::nativeLfo>(frequency, u8{0}, u8{0}, u8{0}, u8{0});
      }
      const u8 pmd = event.u8("pitch_depth", SemanticOperandRole::Modulation);
      const u8 amd = event.u8("amplitude_depth", SemanticOperandRole::Modulation);
      const u8 waveform = event.u8("waveform_and_ramp", SourceValueDisplay::Hex);
      const u8 delay = event.u8("delay_or_ramp", SemanticOperandRole::Duration);
      return event.invoke<&Playback::nativeLfo>(frequency, pmd, amd, waveform, delay);
    }
    case 0xea: {
      if (!fm && !vendetta) {
        return ignored(cursor, "Unused");
      }
      auto event = cursor.command(vendetta ? "LFO Ramp Interval" : "LFO Delay", SequenceSemantic::Modulation);
      return event.invoke<&Playback::nativeLfoTiming>(event.u8("ticks", SemanticOperandRole::Duration));
    }
    case 0xeb: {
      if (!fm && layout.version == Version::SunsetRiders) {
        return ignored(cursor, "Unused");
      }
      auto event = cursor.command("Portamento", SequenceSemantic::Portamento);
      return event.invoke<&Playback::setPortamento>(event.u8("rate", SemanticOperandRole::Duration));
    }
    case 0xec: {
      auto event = cursor.command("Transpose", SequenceSemantic::Pitch);
      return event.invoke<&Playback::transpose>(
          event.u8("packed_semitones", SourceValueDisplay::Hex, SemanticOperandRole::Pitch));
    }
    case 0xed: {
      auto event = cursor.command("Pitch Bend", SequenceSemantic::Pitch);
      return event.invoke<&Playback::pitchBend>(event.u8("bend", SourceValueDisplay::Hex, SemanticOperandRole::Pitch));
    }
    case 0xee: {
      if (vendetta) {
        return ignored(cursor, "Unused");
      }
      auto event = cursor.command("Stepped Pitch / Volume Modulation", SequenceSemantic::Modulation);
      const u8 count = event.u8("steps", SemanticOperandRole::Count);
      const u8 pitch = event.u8("pitch_step", SourceValueDisplay::Hex, SemanticOperandRole::Pitch);
      const u8 volume = event.u8("volume_step_and_interval", SourceValueDisplay::Hex, SemanticOperandRole::Level);
      event.invoke<&Playback::steppedModulation>(count, pitch, volume);
      return event.wait(count == 0 ? 256 : count);
    }
    case 0xef: {
      if (vendetta || (!fm && layout.version == Version::Tmnt2)) {
        return ignored(cursor, "Unused");
      }
      auto event = cursor.command("Master Attenuation", SequenceSemantic::Level);
      const u8 ym = event.u8("ym2151", SourceValueDisplay::Hex);
      const u8 sampled = event.u8("k053260", SourceValueDisplay::Hex);
      return event.invoke<&Playback::masterAdjust>(ym, sampled);
    }
    case 0xf0:
    case 0xf1:
    case 0xf2:
    case 0xf3:
    case 0xf4:
    case 0xf5:
    case 0xf6:
    case 0xf7:
      return cursor.command("Octave / Drum Bank", SequenceSemantic::Pitch).invoke<&Playback::setOctave>(opcode);
    case 0xf8:
      return fm || vendetta ? cursor.command("Key Off", SequenceSemantic::Note).invoke<&Playback::keyOff>()
                            : ignored(cursor, "Unused");
    case 0xf9: {
      auto event = cursor.command("Jump", SequenceSemantic::Jump);
      const Address target =
          destination(event, static_cast<u32>(layout.program.offset), SemanticOperandRole::JumpTarget);
      return target.value < begin ? event.loopCandidate(target) : event.jump(target);
    }
    case 0xfa:
    case 0xfb: {
      const u8 slot = opcode - 0xfa;
      Address target;
      bool isEnd = false;
      if (state.flow == nullptr) {
        target = state.loops[slot];
        isEnd = target.value != 0;
      } else if (const auto found = state.flow->loopEnds[slot].find(begin); found != state.flow->loopEnds[slot].end()) {
        target = found->second;
        isEnd = true;
      }
      auto event =
          cursor.command(isEnd ? "Loop End" : "Loop Start", isEnd ? SequenceSemantic::Repeat : SequenceSemantic::Loop);
      if (!isEnd) {
        state.loops[slot] = event.nextAddress();
        return event.invoke<&Playback::loopStart>(slot, state.loops[slot]);
      }
      const u8 rawCount = event.u8("plays", SemanticOperandRole::Count);
      const u16 plays = rawCount == 0 ? 256 : rawCount == 0xff ? 0xffff : rawCount;
      event.derived("destination", target, SourceValueDisplay::Address, SemanticOperandRole::RepeatTarget);
      event.mayBranchTo(target);
      state.loops[slot] = {};
      return event.invokeFlow<&Playback::loopEnd>(slot, plays, target);
    }
    case 0xfc:
    case 0xfd: {
      if (vendetta && opcode == 0xfd) {
        return ignored(cursor, "Unused");
      }
      const u8 slot = opcode - 0xfc;
      const bool isReturn = state.flow == nullptr ? state.calls[slot] : state.flow->returns.contains(begin);
      if (isReturn) {
        state.calls[slot] = false;
        return cursor.command("Return", SequenceSemantic::Return).return_();
      }
      auto event = cursor.command("Call", SequenceSemantic::Call);
      const Address target =
          destination(event, static_cast<u32>(layout.program.offset), SemanticOperandRole::CallTarget);
      state.calls[slot] = true;
      return event.call(target);
    }
    case 0xfe:
      if (vendetta) {
        return ignored(cursor, "Channel Priority", 1);
      } else {
        auto event = cursor.command("Warp", SequenceSemantic::Repeat);
        const bool followedByWarp =
            reader.has(event.nextAddress().value, 1) && reader.u8At(event.nextAddress().value) == 0xfe;
        return event.invokeFlow<&Playback::warp>(event.nextAddress(), followedByWarp);
      }
    case 0xff:
      return cursor.command("End", SequenceSemantic::End).end();
    default:
      return cursor.unsupported("Unknown Opcode").stop();
  }
}

[[nodiscard]] StaticFlow classifyStaticFlow(ByteReader reader, const Layout& layout, const TrackLayout& track) {
  StaticFlow result;
  DecodeState state;
  std::set<std::tuple<u32, u8, u64, u64>> visited;
  u32 remaining = kMaxTrackCommands;

  std::function<void(u32, DecodeState&)> walk = [&](u32 offset, DecodeState& path) {
    while (remaining != 0 && reader.has(offset, 1) && offset < layout.program.endOffset()) {
      const u8 callMask = static_cast<u8>(path.calls[0] | (path.calls[1] << 1));
      if (!visited.emplace(offset, callMask, path.loops[0].value, path.loops[1].value).second) {
        return;
      }
      --remaining;

      const u8 opcode = reader.u8At(offset);
      if (opcode == 0xfa || opcode == 0xfb) {
        const u8 slot = opcode - 0xfa;
        if (path.loops[slot].value != 0) {
          result.loopEnds[slot].emplace(offset, path.loops[slot]);
        }
      } else if ((opcode == 0xfc || opcode == 0xfd) && !(layout.version == Version::Vendetta && opcode == 0xfd)) {
        const u8 slot = opcode - 0xfc;
        if (path.calls[slot]) {
          result.returns.insert(offset);
        }
      }

      const auto callsBefore = path.calls;
      auto command = decodeCommand(reader, offset, layout, track, path, nullptr);
      const auto transition = command.flow.defaultTransition;
      switch (transition.kind) {
        case CommandTransitionKind::Call:
          if (transition.destination.value < layout.program.endOffset()) {
            walk(static_cast<u32>(transition.destination.value), path);
          }
          path.calls = callsBefore;
          offset = static_cast<u32>(command.flow.continuation.value);
          break;
        case CommandTransitionKind::Jump:
          if (transition.destination.value >= layout.program.endOffset()) {
            return;
          }
          offset = static_cast<u32>(transition.destination.value);
          break;
        case CommandTransitionKind::Fallthrough:
          offset = static_cast<u32>(command.flow.continuation.value);
          break;
        case CommandTransitionKind::Return:
        case CommandTransitionKind::End:
        case CommandTransitionKind::EndSection:
          return;
      }
    }
  };
  walk(track.offset, state);
  return result;
}

[[nodiscard]] SequenceProgramConfig makeConfig() {
  return SequenceProgramConfig{
      .commandKindPrefix = "konami-tmnt2",
      .timebase = Timebase{.ppqn = kPpqn},
      .behavior =
          SequenceProgramBehavior{
              .commandLimit = kMaxTrackCommands,
              .inferLoopsFromRepeatedState = false,
              .initialLevel = 1.0,
              .initialExpression = 1.0,
              .initialReverbSend = 0.0,
          },
  };
}

}  // namespace

const SequenceProgramConfig& sequenceConfig() {
  static const SequenceProgramConfig config = makeConfig();
  return config;
}

SequenceProgram decodeSequence(ByteReader reader, const Layout& layout, const SequenceLayout& sequenceLayout,
                               AssetId sequenceAsset, SourceMapBuilder* sourceMap,
                               std::vector<Diagnostic>* diagnostics) {
  SequenceDecodeSession session{
      reader,
      sequenceConfig(),
      sequenceAsset,
      sequenceLayout.trackTable,
      sourceMap,
      kMaxTrackCommands,
      static_cast<u32>(layout.program.endOffset()),
  };

  RuntimeConfig runtime{
      .version = layout.version,
      .clkb = layout.clkb,
      .tickSkipInterval = layout.defaultTickSkipInterval,
      .instruments = layout.sampleInstruments,
      .drums = layout.drumBanks,
  };
  runtime.trackChips.fill(TrackChip::Ym2151);
  for (const auto& track : sequenceLayout.tracks) {
    if (track.number < runtime.trackChips.size()) {
      runtime.trackChips[track.number] = track.chip;
    }
  }
  runtime.ymPan.reserve(layout.ym2151Patches.size());
  for (const u32 patch : layout.ym2151Patches) {
    runtime.ymPan.push_back(reader.has(patch, 1) ? static_cast<u8>(reader.u8At(patch) & 0xc0) : 0xc0);
  }

  for (const auto& track : sequenceLayout.tracks) {
    const auto flow = classifyStaticFlow(reader, layout, track);
    DecodeState state{.flow = &flow};
    const auto decode = [&](u32 offset) { return decodeCommand(reader, offset, layout, track, state, diagnostics); };
    session.addTrack(track.number, track.pointer, track.offset, decode, track.offset);
  }
  auto program = session.finish(makeCompiledRuntime<Cursor, ProgramState>(std::move(runtime)));
  for (auto& decodedTrack : program.tracks) {
    const auto layoutTrack =
        std::ranges::find(sequenceLayout.tracks, decodedTrack.sourceTrackNumber, &TrackLayout::number);
    if (layoutTrack == sequenceLayout.tracks.end()) {
      continue;
    }
    const u32 localTrack = layoutTrack->chip == TrackChip::Ym2151 ? layoutTrack->number
                                                                  : layoutTrack->number - sequenceLayout.ymTrackCount;
    decodedTrack.name =
        (layoutTrack->chip == TrackChip::Ym2151 ? "FM Track " : "Sampled Track ") + std::to_string(localTrack);
    if (sourceMap != nullptr && decodedTrack.annotation.valid()) {
      AnnotationBuilder{*sourceMap, decodedTrack.annotation}.label(decodedTrack.name);
    }
  }
  program.behavior.initialTempoMicrosecondsPerQuarter =
      tempoMicrosecondsPerQuarter(layout.clkb, layout.defaultTickSkipInterval);
  return program;
}

}  // namespace vgmtrans::formats::konami_tmnt2
