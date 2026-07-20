/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/KonamiSnes/KonamiSnes.h"

#include "value/base/LevelScale.h"
#include "value/sequence/BytecodeDecode.h"
#include "value/sequence/CommandSourceMap.h"
#include "value/sequence/CompilerCursor.h"
#include "value/sequence/SequenceMotion.h"
#include "value/synth/SynthMath.h"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vgmtrans::formats::konami_snes {

using namespace core;

namespace {

constexpr u32 kMaxTrackCommands = 8192;

constexpr std::array<u8, 21> kPanVolumeLeftV1{0x00, 0x05, 0x0c, 0x14, 0x1e, 0x28, 0x32, 0x3c, 0x46, 0x50, 0x59,
                                              0x62, 0x69, 0x6f, 0x74, 0x78, 0x7b, 0x7d, 0x7e, 0x7e, 0x7f};
constexpr std::array<u8, 21> kPanVolumeRightV1{0x7f, 0x7e, 0x7e, 0x7d, 0x7b, 0x78, 0x74, 0x6f, 0x69, 0x62, 0x59,
                                               0x50, 0x46, 0x3c, 0x32, 0x28, 0x1e, 0x14, 0x0c, 0x05, 0x00};
constexpr std::array<u8, 21> kPanVolumeLeftV2{0x00, 0x0a, 0x18, 0x28, 0x3c, 0x50, 0x64, 0x78, 0x8c, 0xa0, 0xb2,
                                              0xc4, 0xd2, 0xde, 0xe8, 0xf0, 0xf6, 0xfa, 0xfc, 0xfc, 0xfe};
constexpr std::array<u8, 21> kPanVolumeRightV2{0xfe, 0xfc, 0xfc, 0xfa, 0xf6, 0xf0, 0xe8, 0xde, 0xd2, 0xc4, 0xb2,
                                               0xa0, 0x8c, 0x78, 0x64, 0x50, 0x3c, 0x28, 0x18, 0x0a, 0x00};
constexpr std::array<u8, 42> kPanTable{0x00, 0x04, 0x08, 0x0e, 0x14, 0x1a, 0x20, 0x28, 0x30, 0x38, 0x40,
                                       0x48, 0x50, 0x5a, 0x64, 0x6e, 0x78, 0x82, 0x8c, 0x96, 0xa0, 0xa8,
                                       0xb0, 0xb8, 0xc0, 0xc8, 0xd0, 0xd6, 0xdc, 0xe0, 0xe4, 0xe8, 0xec,
                                       0xf0, 0xf4, 0xf6, 0xf8, 0xfa, 0xfc, 0xfe, 0xfe, 0xfe};
constexpr std::array<u8, 128> kVolumeTable{
    0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
    0x02, 0x02, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x05, 0x05, 0x05, 0x05, 0x06,
    0x06, 0x07, 0x07, 0x07, 0x07, 0x08, 0x08, 0x09, 0x09, 0x0a, 0x0a, 0x0a, 0x0a, 0x0b, 0x0b, 0x0c, 0x0c, 0x0d, 0x0d,
    0x0e, 0x0f, 0x10, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1b, 0x1c, 0x1d, 0x1e, 0x20,
    0x22, 0x23, 0x24, 0x26, 0x28, 0x2a, 0x2c, 0x2d, 0x2f, 0x31, 0x33, 0x35, 0x38, 0x3a, 0x3d, 0x40, 0x43, 0x46, 0x49,
    0x4c, 0x4f, 0x52, 0x56, 0x5a, 0x5e, 0x62, 0x66, 0x6b, 0x6f, 0x73, 0x77, 0x7b, 0x7f};

enum class PitchSlideKind : u8 {
  V1,
  V2,
  V3,
};

enum class FadeTarget : u8 {
  Tempo,
  Volume,
  Pan,
};

struct DecodedPitchSlide {
  PitchSlideKind kind = PitchSlideKind::V1;
  u8 delay = 0;
  u8 length = 0;
  u8 targetNote = 0;
};

struct ModulationRanges {
  u8 maxDepth = kMinVibratoMaxDepth;
  u16 maxRateFactor = 0;
};

[[nodiscard]] constexpr bool isLateVersion(KonamiSnesVersion version) {
  return version == KONAMISNES_V5 || version == KONAMISNES_V6;
}

[[nodiscard]] constexpr PitchSlideKind pitchSlideKind(KonamiSnesVersion version) {
  if (version == KONAMISNES_V1) {
    return PitchSlideKind::V1;
  }
  return isLateVersion(version) ? PitchSlideKind::V3 : PitchSlideKind::V2;
}

[[nodiscard]] u32 tempoMicrosecondsPerQuarter(KonamiSnesVersion version, u8 tempo) {
  if (tempo == 0) {
    return 60000000;
  }
  return static_cast<u32>(std::lround(kKonamiSnesPpqn * (125.0 * timerFrequency(version)) * 256.0 / tempo));
}

[[nodiscard]] double sequenceTickSeconds(KonamiSnesVersion version, u8 tempo) {
  return static_cast<double>(tempoMicrosecondsPerQuarter(version, tempo)) /
         (1'000'000.0 * static_cast<double>(kKonamiSnesPpqn));
}

[[nodiscard]] u32 vibratoDelayTicks(KonamiSnesVersion version, u8 delay, u8 tempo) {
  const double tickSeconds = sequenceTickSeconds(version, tempo);
  if (tickSeconds <= 0.0 || !std::isfinite(tickSeconds)) {
    return 0;
  }
  const double ticks = vibrato::delaySeconds(version, delay, tempo) / tickSeconds;
  if (ticks <= 0.0 || !std::isfinite(ticks)) {
    return 0;
  }
  return static_cast<u32>(std::min<double>(std::lround(ticks), std::numeric_limits<u32>::max()));
}

[[nodiscard]] u8 vibratoDelayMidiValue(KonamiSnesVersion version, u8 delay, u8 tempo) {
  const s32 minAmount = synthAmountFromSeconds(synthSecondsRangeMinimum(vibrato::minDelaySeconds(version)));
  const s32 rangeAmount =
      synthAmountFromSecondsRange(vibrato::minDelaySeconds(version), vibrato::maxDelaySeconds(version));
  if (rangeAmount == 0) {
    return 0;
  }
  const s32 currentAmount =
      synthAmountFromSeconds(synthSecondsRangeMinimum(vibrato::delaySeconds(version, delay, tempo)));
  return static_cast<u8>(std::clamp<s32>(
      static_cast<s32>(std::lround(128.0 * (currentAmount - minAmount) / static_cast<double>(rangeAmount))), 0, 127));
}

[[nodiscard]] double tuningCents(s8 tuning) {
  return tuning * (400.0 / 256.0);
}

[[nodiscard]] constexpr u32 midiBank(u32 bankMsb) {
  return bankMsb << 7;
}

[[nodiscard]] double linearGainFromRawVolume(u8 volume) {
  return static_cast<double>(volume) / 255.0;
}

[[nodiscard]] u8 clampPan(KonamiSnesVersion version, u8 pan) {
  return std::min(pan, version <= KONAMISNES_V2 ? u8{20} : u8{40});
}

// Converts the driver's version-specific pan table into the shared -1..1 scale.
[[nodiscard]] double stereoPositionFromPan(KonamiSnesVersion version, u8 rawPan) {
  const u8 pan = clampPan(version, rawPan);
  u8 left = 0;
  u8 right = 0;
  if (version == KONAMISNES_V1) {
    left = kPanVolumeLeftV1[pan];
    right = kPanVolumeRightV1[pan];
  } else if (version == KONAMISNES_V2) {
    left = kPanVolumeLeftV2[pan];
    right = kPanVolumeRightV2[pan];
  } else {
    left = kPanTable[40 - pan];
    right = kPanTable[pan];
  }
  const double total = static_cast<double>(left) + right;
  return total == 0.0 ? 0.0 : std::clamp((right / total) * 2.0 - 1.0, -1.0, 1.0);
}

class LfoState {
public:
  void configure(u8 delay, u8 rate, u8 depth) {
    delay_ = delay;
    rate_ = rate;
    depth_ = depth;
    currentDepth_.setCurrent(static_cast<s32>(depth) << 8);
    reusableFadeTicks_ = 0;
  }

  void setReusableFade(u8 ticks) { reusableFadeTicks_ = ticks; }

  void beginReusableFade() {
    if (reusableFadeTicks_ == 0) {
      return;
    }
    currentDepth_.setCurrent(0);
    const auto target = static_cast<s32>(depth_) << 8;
    static_cast<void>(currentDepth_.begin(SequenceMotionPlan<s32>::targetOverTicksWithStep(
        target, target / reusableFadeTicks_, reusableFadeTicks_, delay_)));
  }

  [[nodiscard]] bool fadeActive() const { return currentDepth_.active(); }
  [[nodiscard]] SequenceMotionTick<s32> tickFade() { return currentDepth_.tick(); }
  [[nodiscard]] u8 delay() const { return delay_; }
  [[nodiscard]] u8 rate() const { return rate_; }
  [[nodiscard]] u8 depth() const { return depth_; }
  [[nodiscard]] u16 currentDepth() const {
    return static_cast<u16>(std::clamp<s32>(currentDepth_.current(), 0, static_cast<s32>(depth_) << 8));
  }

private:
  u8 delay_ = 0;
  u8 rate_ = 0;
  u8 depth_ = 0;
  SequenceAutomatedValue<s32> currentDepth_;
  u8 reusableFadeTicks_ = 0;
};

// The shared VM performs this prepass with the same compiled commands used for
// playback. It observes modulation limits without reopening source bytes or
// duplicating Konami calls, loops, timing, or opcode interpretation.
struct ProgramState {
  struct ActiveVibrato {
    u8 rate = 0;
    u8 depth = 0;
  };

  explicit ProgramState(const SequenceProgram& program)
      : version(static_cast<KonamiSnesVersion>(program.config.profile)),
        ranges{.maxDepth = kMinVibratoMaxDepth, .maxRateFactor = vibrato::minMaxRateFactor(version)},
        activeVibrato(program.tracks.size()) {}

  void observeVibrato(u32 trackNumber, u8 rate, u8 depth, u8 trackTempo) {
    if (!collecting) {
      return;
    }
    if (trackNumber >= activeVibrato.size()) {
      activeVibrato.resize(trackNumber + 1);
    }
    activeVibrato[trackNumber] = ActiveVibrato{.rate = rate, .depth = depth};
    if (!vibrato::isActive(version, rate, depth)) {
      return;
    }
    ranges.maxDepth = std::max(ranges.maxDepth, depth);
    const u8 tempo = vibrato::usesLegacy(version) ? globalTempo : trackTempo;
    ranges.maxRateFactor = std::max(ranges.maxRateFactor, vibrato::rateFactor(version, rate, tempo));
  }

  void observeTempo(u8 tempo) {
    if (!collecting || !vibrato::usesLegacy(version)) {
      return;
    }
    globalTempo = tempo;
    for (const auto& active : activeVibrato) {
      if (vibrato::isActive(version, active.rate, active.depth)) {
        ranges.maxRateFactor = std::max(ranges.maxRateFactor, vibrato::rateFactor(version, active.rate, globalTempo));
      }
    }
  }

  void finishPrepass() {
    collecting = false;
    globalTempo = kKonamiSnesDefaultTempo;
    activeVibrato.clear();
  }

  KonamiSnesVersion version = KONAMISNES_NONE;
  ModulationRanges ranges;
  std::vector<ActiveVibrato> activeVibrato;
  u8 globalTempo = kKonamiSnesDefaultTempo;
  bool collecting = true;
};

// Only values that persist from one executed command to the next live here.
struct TrackState {
  TrackState(const SequenceProgram& program, const TrackProgram& track)
      : version(static_cast<KonamiSnesVersion>(program.config.profile)), trackNumber(track.sourceTrackNumber) {
    panFade.reset(version <= KONAMISNES_V2 ? 10 : 20);
    volumeFade.reset(0xff);
    tempoFade.reset(kKonamiSnesDefaultTempo);
  }

  [[nodiscard]] u8 noteDuration(u8 length) const {
    const u8 maxRate = noteDurationRateMax(version);
    if (noteDurationRate == maxRate) {
      return length;
    }
    const u8 duration = version == KONAMISNES_V1 ? static_cast<u8>((length * noteDurationRate) / 100)
                                                 : static_cast<u8>((length * (noteDurationRate << 1)) >> 8);
    return std::max<u8>(duration, 1);
  }

  [[nodiscard]] double totalTuningCents() const {
    return sequenceTuningCents + static_cast<double>(loopPitchDelta + loopPitchDelta2) * (100.0 / 32.0);
  }

  [[nodiscard]] double noteSemitones(u8 key, bool includeTuning = true) const {
    double semitones = (key & 0x7f) + transpose;
    if (includeTuning) {
      semitones += totalTuningCents() / 100.0;
    }
    return semitones;
  }

  KonamiSnesVersion version = KONAMISNES_NONE;
  u32 trackNumber = 0;
  u8 noteLength = 0;
  u8 noteDurationRate = 0;
  s32 transpose = 0;
  Address loopReturnAddress;
  Address loopReturnAddress2;
  s16 loopVolumeDelta = 0;
  s16 loopPitchDelta = 0;
  s16 loopVolumeDelta2 = 0;
  s16 loopPitchDelta2 = 0;
  bool percussion = false;
  bool inSubroutine = false;
  u8 instrument = 0;
  std::optional<u8> previousNoteKey;
  bool previousNoteSlurred = false;
  double sequenceTuningCents = 0.0;
  double lastEmittedTuningCents = std::numeric_limits<double>::quiet_NaN();
  u8 tempo = kKonamiSnesDefaultTempo;
  SequenceFixedPointAutomation<s32> panFade;
  SequenceFixedPointAutomation<s32> volumeFade;
  SequenceFixedPointAutomation<s32> tempoFade;
  LfoState vibrato;
  std::optional<double> pitchBase;
  SequenceAutomatedValue<double> pitchSlide;
  double lastVibratoDepthAmount = -1.0;
  bool emittedInitialModulationCeiling = false;
};

// History-dependent driver behavior stays close to the opcode switch below.
struct Playback {
  TrackState& track;
  PerformanceEmitter& out;
  VmApi& vm;
  ProgramState& program;

  void beforeCommand() { emitInitialModulationCeiling(); }

  void note(u8 key, u8 sourceVelocity) {
    u8 velocity =
        static_cast<u8>(std::clamp<int>(sourceVelocity + track.loopVolumeDelta + track.loopVolumeDelta2, 1, 127));
    if (track.version != KONAMISNES_V1) {
      velocity = kVolumeTable[velocity];
    }

    applyEffectiveTuning();
    const u8 duration = track.noteDuration(track.noteLength);
    const bool tied = track.previousNoteSlurred && track.previousNoteKey && key == *track.previousNoteKey;
    resetPitchForNote(key);
    out.pitchBend(0.0);
    track.vibrato.beginReusableFade();
    if (track.vibrato.fadeActive()) {
      emitVibratoDepth(true);
    }
    if (tied) {
      out.note(track.noteSemitones(key, false), LevelScale::linearFromLinear(velocity / 127.0), duration, true);
    } else {
      out.note(track.percussion ? key : track.noteSemitones(key, false), LevelScale::linearFromLinear(velocity / 127.0),
               duration);
      track.previousNoteKey = key;
    }
    track.previousNoteSlurred = track.noteDurationRate == noteDurationRateMax(track.version) && !track.percussion;
  }

  void tie() {
    if (!track.previousNoteSlurred) {
      return;
    }
    out.note(0.0, 1.0, track.noteDuration(track.noteLength), true);
    track.previousNoteSlurred = track.noteDurationRate == noteDurationRateMax(track.version);
  }

  void percussionOn() {
    if (!track.percussion) {
      out.instrument(midiBank(0x7f), 0, true);
      track.percussion = true;
    }
  }

  void percussionOff() {
    if (track.percussion) {
      out.instrument(midiBank(track.instrument >> 7), track.instrument & 0x7f, true);
      track.percussion = false;
    }
  }

  void programChange(u8 programNumber) {
    applyEffectiveTuning(true);
    track.instrument = programNumber;
    out.instrument(midiBank(programNumber >> 7), programNumber & 0x7f, true);
    pan(track.version <= KONAMISNES_V2 ? 10 : 20);
  }

  void programChangeAndVolume(u8 volumeValue, u8 programNumber) {
    applyEffectiveTuning(true);
    track.instrument = programNumber;
    out.instrument(midiBank(programNumber >> 7), programNumber & 0x7f, true);
    volume(volumeValue);
    pan(track.version <= KONAMISNES_V2 ? 10 : 20);
  }

  void volume(u8 rawVolume) {
    track.volumeFade.setCurrentRaw(rawVolume);
    out.level(LevelScale::linearFromLinear(linearGainFromRawVolume(rawVolume)), LevelPrecisionHint::FourteenBit);
  }

  void pan(u8 rawPan) {
    const u8 value = clampPan(track.version, rawPan);
    track.panFade.setCurrentRaw(value);
    out.pan(stereoPositionFromPan(track.version, value));
  }

  void tuning(s8 value) {
    track.sequenceTuningCents = tuningCents(value);
    applyEffectiveTuning();
  }

  void tempo(u8 value) {
    track.tempo = value;
    track.tempoFade.setCurrentRaw(value);
    program.observeTempo(value);
    out.tempo(tempoMicrosecondsPerQuarter(track.version, value));
    if (vibrato::usesLegacy(track.version)) {
      emitVibratoRate();
      emitVibratoDelay();
    }
  }

  void configureVibrato(u8 delay, u8 rate, u8 depth, u8 builtInFade) {
    track.vibrato.configure(delay, rate, depth);
    if (builtInFade != 0) {
      track.vibrato.setReusableFade(builtInFade);
    }
    program.observeVibrato(track.trackNumber, rate, depth, track.tempo);
    emitVibratoDelay();
    emitVibratoDepth(true);
    emitVibratoRate();
  }

  void beginPitchSlide(PitchSlideKind kind, u8 delay, u8 length, u8 targetNote) {
    if (!track.pitchBase || length == 0) {
      out.pitchBendRange(2);
      return;
    }
    const double target = track.noteSemitones(targetNote, kind == PitchSlideKind::V1);
    const double startDeviation = std::abs(track.pitchSlide.current() - *track.pitchBase);
    const double targetDeviation = std::abs(target - *track.pitchBase);
    const auto range =
        static_cast<u8>(std::max<int>(2, static_cast<int>(std::ceil(std::max(startDeviation, targetDeviation)))));
    out.pitchBendRange(range);
    static_cast<void>(track.pitchSlide.begin(SequenceMotionPlan<double>::targetOverTicksWithStep(
        target, (target - track.pitchSlide.current()) / length, length, delay)));
  }

  void beginFade(FadeTarget target, bool stepBased, u8 destination, u8 ticks, s8 step) {
    const u8 clampedDestination = target == FadeTarget::Pan ? clampPan(track.version, destination) : destination;
    const SequenceFixedPointMotionPlan<s32> motion =
        stepBased
            ? SequenceFixedPointMotion<s32>::toRawTargetByFixedStep(clampedDestination, static_cast<s32>(step) * 16)
            : SequenceFixedPointMotion<s32>::toRawTarget(clampedDestination, ticks);
    switch (target) {
      case FadeTarget::Tempo:
        static_cast<void>(track.tempoFade.begin(motion));
        break;
      case FadeTarget::Volume:
        static_cast<void>(track.volumeFade.begin(motion));
        break;
      case FadeTarget::Pan:
        static_cast<void>(track.panFade.begin(motion));
        break;
    }
  }

  [[nodiscard]] Effects loopEnd(u8 slot, u8 times, s8 volumeDelta, s8 pitchDelta) {
    const Address destination = slot == 0 ? track.loopReturnAddress : track.loopReturnAddress2;
    if (destination.value == 0) {
      return {};
    }
    if (times == 0) {
      return Effects{.step = vm.declaredLoop(destination)};
    }

    Effects effects = vm.countedRepeatUntil(slot, times, destination);
    s16& accumulatedVolume = slot == 0 ? track.loopVolumeDelta : track.loopVolumeDelta2;
    s16& accumulatedPitch = slot == 0 ? track.loopPitchDelta : track.loopPitchDelta2;
    if (effects.step.kind == StepKind::Next) {
      accumulatedVolume = 0;
      accumulatedPitch = 0;
    } else {
      accumulatedVolume += volumeDelta;
      accumulatedPitch += pitchDelta;
    }
    applyEffectiveTuning();
    return effects;
  }

  [[nodiscard]] Effects endOrReturn() {
    if (track.inSubroutine) {
      track.inSubroutine = false;
      return Effects{.step = vm.return_()};
    }
    return Effects{.step = vm.end()};
  }

  void tick() {
    static_cast<void>(track.tempoFade.tickRaw([&](s32 rawTempo) {
      const u8 value = static_cast<u8>(std::clamp<s32>(rawTempo, 0, 0xff));
      if (value == track.tempo) {
        return;
      }
      track.tempo = value;
      out.tempo(tempoMicrosecondsPerQuarter(track.version, value));
      if (vibrato::usesLegacy(track.version)) {
        emitVibratoRate();
        emitVibratoDelay();
      }
    }));
    static_cast<void>(track.volumeFade.tickRaw([&](s32 rawVolume) {
      const auto value = static_cast<u8>(std::clamp<s32>(rawVolume, 0, 0xff));
      out.level(LevelScale::linearFromLinear(linearGainFromRawVolume(value)), LevelPrecisionHint::FourteenBit);
    }));
    static_cast<void>(track.panFade.tickRaw([&](s32 rawPan) {
      const auto value = static_cast<u8>(std::clamp<s32>(rawPan, 0, 0xff));
      out.pan(stereoPositionFromPan(track.version, value));
    }));
    if (track.pitchBase && track.pitchSlide.active()) {
      const auto pitchTick = track.pitchSlide.tick();
      if (pitchTick.status != SequenceMotionStatus::Inactive && pitchTick.status != SequenceMotionStatus::Delayed) {
        out.pitchBend(track.pitchSlide.current() - *track.pitchBase);
      }
    }
    if (track.vibrato.fadeActive()) {
      const auto fadeTick = track.vibrato.tickFade();
      if (fadeTick.status != SequenceMotionStatus::Inactive && fadeTick.status != SequenceMotionStatus::Delayed) {
        emitVibratoDepth();
      }
    }
  }

private:
  void applyEffectiveTuning(bool force = false) {
    const double cents = track.totalTuningCents();
    const bool emitted = std::isfinite(track.lastEmittedTuningCents);
    const bool nonZero = std::abs(cents) > 0.001;
    const bool changed = emitted && std::abs(track.lastEmittedTuningCents - cents) > 0.001;
    if ((!emitted && nonZero) || changed || (force && (emitted || nonZero))) {
      out.tuning(cents);
      track.lastEmittedTuningCents = cents;
    }
  }

  void resetPitchForNote(u8 key) {
    track.pitchSlide.clearMotion();
    if (track.percussion) {
      track.pitchBase.reset();
      return;
    }
    track.pitchBase = track.noteSemitones(key);
    track.pitchSlide.setCurrent(*track.pitchBase);
  }

  void emitInitialModulationCeiling() {
    if (track.emittedInitialModulationCeiling) {
      return;
    }
    track.emittedInitialModulationCeiling = true;

    const double fullRangeCents = vibrato::maxDepthCents(track.version, kDefaultVibratoMaxDepth);
    const double maxCents = vibrato::maxDepthCents(track.version, program.ranges.maxDepth);
    out.modulation(ModulationPerformanceEvent{
        .target = ModulationPerformanceTarget::VibratoDepth,
        .amount = 0.0,
        .pitchDepthSemitones = 0.0,
        .controllerRangeMaxAmount =
            fullRangeCents <= 0.0 ? std::nullopt : std::optional<double>{maxCents / fullRangeCents},
        .controllerRangeOnly = true,
    });
    track.lastVibratoDepthAmount = 0.0;

    const double minHertz = vibrato::baseHz(track.version);
    const s32 fullRangeAmount =
        synthAmountFromHertzRange(minHertz, minHertz * vibrato::defaultMaxRateFactor(track.version));
    const s32 maxRangeAmount = program.ranges.maxRateFactor == 0
                                   ? 0
                                   : synthAmountFromHertzRange(minHertz, minHertz * program.ranges.maxRateFactor);
    out.modulation(ModulationPerformanceEvent{
        .target = ModulationPerformanceTarget::VibratoRate,
        .amount = 0.0,
        .frequencyHz = 0.0,
        .controllerRangeMaxAmount = fullRangeAmount <= 0 || maxRangeAmount <= 0
                                        ? std::nullopt
                                        : std::optional<double>{static_cast<double>(maxRangeAmount) / fullRangeAmount},
        .controllerRangeOnly = true,
    });
  }

  void emitVibratoDepth(bool force = false) {
    const bool active = vibrato::isActive(track.version, track.vibrato.rate(), track.vibrato.depth());
    double amount = 0.0;
    double depthSemitones = 0.0;
    std::optional<double> rangeMaxAmount;
    if (active) {
      const double currentCents =
          vibrato::currentDepthCents(track.version, track.vibrato.depth(), track.vibrato.currentDepth());
      const double fullRangeCents = vibrato::maxDepthCents(track.version, kDefaultVibratoMaxDepth);
      amount = fullRangeCents <= 0.0 ? 0.0 : currentCents / fullRangeCents;
      const double maxCents = vibrato::maxDepthCents(track.version, program.ranges.maxDepth);
      rangeMaxAmount = fullRangeCents <= 0.0 ? 0.0 : maxCents / fullRangeCents;
      // Konami depth is a full swing; simulation needs deviation from center.
      depthSemitones = currentCents / 400.0;
    }
    amount = std::clamp(amount, 0.0, 1.0);
    if (force || std::abs(amount - track.lastVibratoDepthAmount) > 0.0001) {
      out.modulation(ModulationPerformanceEvent{
          .target = ModulationPerformanceTarget::VibratoDepth,
          .amount = amount,
          .pitchDepthSemitones = depthSemitones,
          .controllerRangeMaxAmount = rangeMaxAmount,
      });
      track.lastVibratoDepthAmount = amount;
    }
  }

  void emitVibratoRate() {
    const u16 factor = vibrato::rateFactor(track.version, track.vibrato.rate(), track.tempo);
    const double minHertz = vibrato::baseHz(track.version);
    const s32 fullRangeAmount =
        synthAmountFromHertzRange(minHertz, minHertz * vibrato::defaultMaxRateFactor(track.version));
    const s32 currentAmount = factor == 0 ? 0 : synthAmountFromHertzRange(minHertz, minHertz * factor);
    const s32 maxAmount = program.ranges.maxRateFactor == 0
                              ? 0
                              : synthAmountFromHertzRange(minHertz, minHertz * program.ranges.maxRateFactor);
    out.modulation(ModulationPerformanceEvent{
        .target = ModulationPerformanceTarget::VibratoRate,
        .amount =
            fullRangeAmount <= 0 ? 0.0 : std::clamp(static_cast<double>(currentAmount) / fullRangeAmount, 0.0, 1.0),
        .frequencyHz = minHertz * factor,
        .controllerRangeMaxAmount = fullRangeAmount <= 0 || maxAmount <= 0
                                        ? std::nullopt
                                        : std::optional<double>{static_cast<double>(maxAmount) / fullRangeAmount},
    });
  }

  void emitVibratoDelay() {
    if (!vibrato::isActive(track.version, track.vibrato.rate(), track.vibrato.depth())) {
      out.vibratoDelay(0, 0);
      return;
    }
    out.vibratoDelay(vibratoDelayTicks(track.version, track.vibrato.delay(), track.tempo),
                     vibratoDelayMidiValue(track.version, track.vibrato.delay(), track.tempo));
  }
};

using KonamiCursor = CompilerCursor<TrackState, Playback>;

// Pitch slides can be standalone commands or an immediate suffix of a note or
// rest. This reads the version-specific suffix once into the containing event.
[[nodiscard]] DecodedPitchSlide readPitchSlide(KonamiCursor::Event& event, KonamiSnesVersion version) {
  DecodedPitchSlide slide{
      .kind = pitchSlideKind(version),
      .delay = event.u8("slide_delay"),
      .length = event.u8("slide_length", SemanticOperandRole::Duration),
      .targetNote = event.u8("target_note", SourceValueDisplay::MidiNote, SemanticOperandRole::NoteKey),
  };
  if (slide.kind == PitchSlideKind::V2 && slide.length != 0) {
    static_cast<void>(event.u8("reserved", SourceValueDisplay::Hex));
    static_cast<void>(event.s16le("delta", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Pitch));
  } else if (slide.kind == PitchSlideKind::V3) {
    static_cast<void>(event.s16le("delta", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Pitch));
  }
  return slide;
}

[[nodiscard]] std::optional<DecodedPitchSlide> readInlinePitchSlide(KonamiCursor::Event& event,
                                                                    KonamiSnesVersion version) {
  if (event.peekU8() != 0xf3) {
    return std::nullopt;
  }
  static_cast<void>(event.u8("pitch_slide_opcode", SourceValueDisplay::Hex));
  return readPitchSlide(event, version);
}

void appendPitchSlide(KonamiCursor::Event& event, const DecodedPitchSlide& slide) {
  event.invoke<&Playback::beginPitchSlide>(slide.kind, slide.delay, slide.length, slide.targetNote);
}

[[nodiscard]] DecodedBytecodeCommand unknownCommand(KonamiCursor& cursor, u8 argumentCount) {
  auto event = cursor.sourceOnly("Unknown Event", "unknown");
  for (u8 index = 0; index < argumentCount; ++index) {
    static_cast<void>(event.u8(fmt::format("arg{}", index + 1), SourceValueDisplay::Hex));
  }
  return event.ignore();
}

[[nodiscard]] DecodedBytecodeCommand decodeCommand(ByteReader reader, u32 begin, KonamiSnesVersion version,
                                                   std::vector<Diagnostic>* diagnostics) {
  KonamiCursor cursor(reader, begin, "konami-snes", diagnostics);
  if (!cursor.hasOpcode()) {
    return cursor.truncated();
  }

  const u8 opcode = cursor.opcode();
  if (opcode <= 0x5f || (opcode >= 0x80 && opcode <= 0xdf)) {
    auto event = cursor.command("Note", SequenceSemantic::Note);
    const u8 key = event.opcodeValue("key", static_cast<u8>(opcode & 0x7f), SourceValueDisplay::MidiNote,
                                     SemanticOperandRole::NoteKey);
    if ((opcode & 0x80) == 0) {
      event.set<&TrackState::noteLength>(event.u8("length", SemanticOperandRole::Duration));
    }
    u8 velocity = event.u8("velocity_or_duration", SemanticOperandRole::Level);
    if ((velocity & 0x80) == 0) {
      const u8 rate = event.derived("duration_rate", std::min(velocity, noteDurationRateMax(version)),
                                    SemanticOperandRole::Duration);
      event.set<&TrackState::noteDurationRate>(rate);
      velocity = event.u8("velocity", SemanticOperandRole::Level);
    }
    velocity = static_cast<u8>(std::max<int>(velocity & 0x7f, 1));
    event.invoke<&Playback::note>(key, velocity);
    if (const auto slide = readInlinePitchSlide(event, version)) {
      appendPitchSlide(event, *slide);
    } else {
      event.emitPitchBendRange(2);
    }
    return event.wait(event.state<&TrackState::noteLength>());
  }

  if (opcode >= 0x70 && opcode <= 0x7f && isLateVersion(version)) {
    auto event = cursor.command("Instant Tuning", SequenceSemantic::Pitch);
    s8 tuning = opcode & 0x0f;
    if (tuning > 8) {
      tuning -= 16;
    }
    event.opcodeValue("tuning", tuning);
    return event.invoke<&Playback::tuning>(tuning);
  }

  switch (opcode) {
    case 0x60:
      return cursor.command("Percussion On", SequenceSemantic::Program).invoke<&Playback::percussionOn>();
    case 0x61:
      return cursor.command("Percussion Off", SequenceSemantic::Program).invoke<&Playback::percussionOff>();
    case 0x62:
      if (version == KONAMISNES_V1) {
        return unknownCommand(cursor, 1);
      } else {
        auto event = cursor.sourceOnly("GAIN");
        static_cast<void>(event.u8("gain_amount", SourceValueDisplay::Hex));
        return event.ignore();
      }
    case 0x63:
      return unknownCommand(cursor, version == KONAMISNES_V1 ? 1 : 0);
    case 0x64:
      return unknownCommand(cursor, version == KONAMISNES_V1 ? 2 : 0);
    case 0x65:
    case 0x66:
    case 0x67:
    case 0x68:
    case 0x69:
    case 0x6a:
    case 0x6b:
    case 0x6c:
    case 0x6d:
    case 0x6e:
    case 0x6f:
    case 0x70:
    case 0x71:
    case 0x72:
    case 0x73:
    case 0x74:
    case 0x75:
    case 0x76:
    case 0x77:
    case 0x78:
    case 0x79:
    case 0x7a:
    case 0x7b:
    case 0x7c:
    case 0x7d:
    case 0x7e:
    case 0x7f:
      return unknownCommand(cursor, 0);
    case 0xe0: {
      auto event = cursor.command("Rest", SequenceSemantic::Rest);
      event.set<&TrackState::noteLength>(event.u8("length", SemanticOperandRole::Duration));
      if (const auto slide = readInlinePitchSlide(event, version)) {
        appendPitchSlide(event, *slide);
      }
      event.set<&TrackState::previousNoteSlurred>(false);
      return event.wait(event.state<&TrackState::noteLength>());
    }
    case 0xe1: {
      auto event = cursor.command("Tie", SequenceSemantic::Note);
      event.set<&TrackState::noteLength>(event.u8("length", SemanticOperandRole::Duration));
      const u8 rate = std::min(event.u8("duration_rate", SemanticOperandRole::Duration), noteDurationRateMax(version));
      event.set<&TrackState::noteDurationRate>(rate);
      event.invoke<&Playback::tie>();
      return event.wait(event.state<&TrackState::noteLength>());
    }
    case 0xe2: {
      auto event = cursor.command("Program", SequenceSemantic::Program);
      const u8 program = event.u8("program", SemanticOperandRole::Instrument);
      event.derived("bank", program >> 7, SemanticOperandRole::InstrumentBank);
      event.derived("program_number", program & 0x7f, SemanticOperandRole::InstrumentProgram);
      return event.invoke<&Playback::programChange>(program);
    }
    case 0xe3: {
      auto event = cursor.command("Pan", SequenceSemantic::Pan);
      const u8 raw = event.u8("pan", SemanticOperandRole::Pan);
      const bool instrumentPanOff = version <= KONAMISNES_V2 ? raw == 0x15 : raw == 0x2a;
      const bool instrumentPanOn = version <= KONAMISNES_V2 ? raw == 0x16 : raw == 0x2c;
      event.derived("instrument_pan_off", instrumentPanOff);
      event.derived("instrument_pan_on", instrumentPanOn);
      return instrumentPanOff || instrumentPanOn ? event.ignore() : event.invoke<&Playback::pan>(raw);
    }
    case 0xe4: {
      auto event = cursor.command("Vibrato", SequenceSemantic::Modulation);
      const u8 arg1 = event.u8("delay_or_fade", SemanticOperandRole::Modulation);
      const u8 rate = event.u8("rate", SemanticOperandRole::Modulation);
      const u8 depth = event.u8("depth", SemanticOperandRole::Modulation);
      const u8 delay = event.derived("delay", vibrato::delayFromArg1(version, arg1));
      const u8 fade = event.derived("built_in_fade", vibrato::inlineFadeLength(version, arg1));
      return event.invoke<&Playback::configureVibrato>(delay, rate, depth, fade);
    }
    case 0xe5: {
      auto event = cursor.sourceOnly("Random Pitch");
      static_cast<void>(event.u8("rate"));
      static_cast<void>(event.u16le("pitch_mask", SourceValueDisplay::Hex));
      return event.ignore();
    }
    case 0xe6: {
      auto event = cursor.command("Loop Start", SequenceSemantic::Loop);
      const Address destination = event.derived("loop_start", event.nextAddress(), SourceValueDisplay::Address,
                                                SemanticOperandRole::LoopTarget);
      return event.set<&TrackState::loopReturnAddress>(destination);
    }
    case 0xe7:
    case 0xe9: {
      auto event = cursor.command(opcode == 0xe7 ? "Loop End" : "Loop End #2", SequenceSemantic::Repeat);
      const u8 slot = event.derived("slot", static_cast<u8>(opcode == 0xe7 ? 0 : 1));
      const u8 times = event.u8("times", SemanticOperandRole::Count);
      const s8 volumeDelta = event.s8("volume_delta", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Level);
      const s8 pitchDelta = event.s8("pitch_delta", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Pitch);
      event.invoke<&Playback::loopEnd>(slot, times, volumeDelta, pitchDelta);
      return event.runtimeControlFlow();
    }
    case 0xe8: {
      auto event = cursor.command("Loop Start #2", SequenceSemantic::Loop);
      const Address destination = event.derived("loop_start", event.nextAddress(), SourceValueDisplay::Address,
                                                SemanticOperandRole::LoopTarget);
      return event.set<&TrackState::loopReturnAddress2>(destination);
    }
    case 0xea: {
      auto event = cursor.command("Tempo", SequenceSemantic::Tempo);
      const u8 tempo = event.u8("tempo");
      event.derived("microseconds_per_quarter", tempoMicrosecondsPerQuarter(version, tempo));
      return event.invoke<&Playback::tempo>(tempo);
    }
    case 0xeb:
    case 0xef:
    case 0xf8: {
      const FadeTarget target =
          opcode == 0xeb ? FadeTarget::Tempo : (opcode == 0xef ? FadeTarget::Volume : FadeTarget::Pan);
      const SequenceSemantic semantic =
          opcode == 0xeb ? SequenceSemantic::Tempo : (opcode == 0xef ? SequenceSemantic::Level : SequenceSemantic::Pan);
      auto event =
          cursor.command(opcode == 0xeb ? "Tempo Fade" : (opcode == 0xef ? "Volume Fade" : "Pan Fade"), semantic);
      if (isLateVersion(version)) {
        const u8 destination = event.u8("target");
        const s8 step = event.s8("step");
        return event.invoke<&Playback::beginFade>(target, true, destination, u8{0}, step);
      }
      const u8 ticks = event.u8("length", SemanticOperandRole::Duration);
      const u8 destination = event.u8("target");
      return event.invoke<&Playback::beginFade>(target, false, destination, ticks, s8{0});
    }
    case 0xec: {
      auto event = cursor.command("Transpose", SequenceSemantic::Pitch);
      return event.set<&TrackState::transpose>(
          event.s8("semitones", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Pitch));
    }
    case 0xed:
      if (isLateVersion(version)) {
        auto event = cursor.sourceOnly("ADSR(1)");
        static_cast<void>(event.u8("adsr1", SourceValueDisplay::Hex));
        return event.ignore();
      }
      return unknownCommand(cursor, 3);
    case 0xee: {
      auto event = cursor.command("Volume", SequenceSemantic::Level);
      return event.invoke<&Playback::volume>(event.u8("volume", SemanticOperandRole::Level));
    }
    case 0xf0: {
      auto event = cursor.sourceOnly("Portamento");
      static_cast<void>(event.u8("speed"));
      return event.ignore();
    }
    case 0xf1: {
      auto event = cursor.sourceOnly("Pitch Envelope");
      static_cast<void>(event.u8("delay"));
      if (isLateVersion(version)) {
        static_cast<void>(event.u8("length", SemanticOperandRole::Duration));
        static_cast<void>(event.u8("offset", SemanticOperandRole::Pitch));
        static_cast<void>(event.s16le("delta", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Pitch));
      } else {
        static_cast<void>(event.u8("speed"));
        static_cast<void>(event.u8("depth", SemanticOperandRole::Pitch));
      }
      return event.ignore();
    }
    case 0xf2: {
      auto event = cursor.command("Tuning", SequenceSemantic::Pitch);
      return event.invoke<&Playback::tuning>(
          event.s8("tuning", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Pitch));
    }
    case 0xf3: {
      auto event = cursor.command("Pitch Slide", SequenceSemantic::Pitch);
      appendPitchSlide(event, readPitchSlide(event, version));
      return event;
    }
    case 0xf4: {
      auto event = cursor.sourceOnly("Echo");
      static_cast<void>(event.u8("channels", SourceValueDisplay::Hex));
      static_cast<void>(event.u8("volume_left"));
      static_cast<void>(event.u8("volume_right"));
      return event.ignore();
    }
    case 0xf5: {
      auto event = cursor.sourceOnly("Echo Param");
      static_cast<void>(event.u8("delay"));
      static_cast<void>(event.u8("feedback"));
      static_cast<void>(event.u8("arg3", SourceValueDisplay::Hex));
      return event.ignore();
    }
    case 0xf6:
      return cursor.sourceOnly("Loop With Volta Start").ignore();
    case 0xf7:
      return cursor.sourceOnly("Loop With Volta End").ignore();
    case 0xf9: {
      auto event = cursor.command("Vibrato Fade", SequenceSemantic::Modulation);
      return event.invoke([](Playback& playback, u8 length) { playback.track.vibrato.setReusableFade(length); },
                          event.u8("length", SemanticOperandRole::Duration));
    }
    case 0xfa:
      if (version >= KONAMISNES_V4) {
        auto event = cursor.sourceOnly("ADSR/Gain");
        static_cast<void>(event.u8("adsr1", SourceValueDisplay::Hex));
        static_cast<void>(event.u8("adsr2", SourceValueDisplay::Hex));
        static_cast<void>(event.u8("gain", SourceValueDisplay::Hex));
        return event.ignore();
      }
      return unknownCommand(cursor, 3);
    case 0xfb:
      if (version >= KONAMISNES_V4) {
        auto event = cursor.sourceOnly("ADSR(2)");
        static_cast<void>(event.u8("adsr2", SourceValueDisplay::Hex));
        return event.ignore();
      }
      return unknownCommand(cursor, 1);
    case 0xfc:
      if (version == KONAMISNES_V1) {
        auto event = cursor.command("Conditional Jump", SequenceSemantic::Jump);
        const Address destination = event.addressLe("destination", SemanticOperandRole::JumpTarget);
        const Address alternate = event.addressLe("alternate_destination", SemanticOperandRole::JumpTarget);
        event.jump(destination);
        return event.mayBranchTo(alternate, SemanticOperandRole::JumpTarget);
      }
      if (version == KONAMISNES_V2) {
        auto event = cursor.sourceOnly("Linear Pitch Envelope");
        static_cast<void>(event.u8("delta_fraction", SemanticOperandRole::Pitch));
        static_cast<void>(event.u8("delta_integer", SemanticOperandRole::Pitch));
        return event.ignore();
      }
      if (version >= KONAMISNES_V4) {
        auto event = cursor.command("Program And Volume", SequenceSemantic::Program);
        const u8 volume = event.u8("volume", SemanticOperandRole::Level);
        const u8 program = event.u8("program", SemanticOperandRole::Instrument);
        event.derived("bank", program >> 7, SemanticOperandRole::InstrumentBank);
        event.derived("program_number", program & 0x7f, SemanticOperandRole::InstrumentProgram);
        return event.invoke<&Playback::programChangeAndVolume>(volume, program);
      }
      return unknownCommand(cursor, 2);
    case 0xfd: {
      auto event = cursor.command("Jump", SequenceSemantic::Jump);
      return event.loopCandidate(event.addressLe("destination", SemanticOperandRole::JumpTarget));
    }
    case 0xfe: {
      auto event = cursor.command("Pattern Play", SequenceSemantic::Call);
      const Address destination = event.addressLe("destination", SemanticOperandRole::CallTarget);
      event.set<&TrackState::inSubroutine>(true);
      return event.call(destination);
    }
    case 0xff: {
      auto event = cursor.command("End", SequenceSemantic::End);
      event.invoke<&Playback::endOrReturn>();
      return event.discoverReturn();
    }
    default:
      return cursor.unsupported("Unknown Opcode", "unknown").stop();
  }
}

[[nodiscard]] std::string dialectId(KonamiSnesVersion version) {
  return fmt::format("konami-snes:{}", konamiSnesVersionName(version));
}

[[nodiscard]] SequenceDialect makeDialect(KonamiSnesVersion version) {
  return makeCompiledDialect<TrackState, Playback, ProgramState>(SequenceDialect{
      .id = DialectId{.value = dialectId(version)},
      .commandDetailKindPrefix = "konami-snes",
      .timebase = Timebase{.ppqn = kKonamiSnesPpqn},
      .defaultBehavior =
          SequenceProgramBehavior{
              .defaultLoopPolicy = LoopPolicy::PlayOnce,
              .initialLevel = 1.0,
              .initialReverbSend = 0.0,
              .initialPitchBendRangeSemitones = 2,
          },
      .semanticPrepass =
          vibrato::usesLegacy(version) ? SemanticPrepassMode::ScheduledPlayback : SemanticPrepassMode::DecodedCommands,
  });
}

}  // namespace

const SequenceDialect& konamiSnesSequenceDialect(KonamiSnesVersion version) {
  static const SequenceDialect none = makeDialect(KONAMISNES_NONE);
  static const SequenceDialect v1 = makeDialect(KONAMISNES_V1);
  static const SequenceDialect v2 = makeDialect(KONAMISNES_V2);
  static const SequenceDialect v3 = makeDialect(KONAMISNES_V3);
  static const SequenceDialect v4 = makeDialect(KONAMISNES_V4);
  static const SequenceDialect v5 = makeDialect(KONAMISNES_V5);
  static const SequenceDialect v6 = makeDialect(KONAMISNES_V6);
  switch (version) {
    case KONAMISNES_V1:
      return v1;
    case KONAMISNES_V2:
      return v2;
    case KONAMISNES_V3:
      return v3;
    case KONAMISNES_V4:
      return v4;
    case KONAMISNES_V5:
      return v5;
    case KONAMISNES_V6:
      return v6;
    case KONAMISNES_NONE:
      return none;
  }
  return none;
}

void registerKonamiSnesSequenceDialects(SequenceDialectRegistry& registry) {
  registry.add(konamiSnesSequenceDialect(KONAMISNES_NONE));
  registry.add(konamiSnesSequenceDialect(KONAMISNES_V1));
  registry.add(konamiSnesSequenceDialect(KONAMISNES_V2));
  registry.add(konamiSnesSequenceDialect(KONAMISNES_V3));
  registry.add(konamiSnesSequenceDialect(KONAMISNES_V4));
  registry.add(konamiSnesSequenceDialect(KONAMISNES_V5));
  registry.add(konamiSnesSequenceDialect(KONAMISNES_V6));
}

TrackProgram decodeKonamiSnesSourceTrack(ByteReader reader, KonamiSnesVersion version, u32 sourceTrackNumber,
                                         u32 startAddress, SourceMapBuilder* sourceMap,
                                         std::vector<Diagnostic>* diagnostics,
                                         std::optional<SourceAnnotationId> parentAnnotation,
                                         std::optional<AssetId> sequenceAsset) {
  const TrackDecodeScope tracks{
      .reader = reader,
      .maxCommands = kMaxTrackCommands,
      .sequenceAsset = sequenceAsset,
      .parentAnnotation = parentAnnotation,
      .sourceMap = sourceMap,
  };
  return tracks.linear(sourceTrackNumber, startAddress,
                       [&](u32 offset) { return decodeCommand(reader, offset, version, diagnostics); });
}

SourceRange konamiSnesSequenceHeaderRange(ByteReader reader, const KonamiSnesLayout& layout) {
  u32 trackCount = kKonamiSnesMaxTracks;
  for (u32 trackNumber = 0; trackNumber < kKonamiSnesMaxTracks; ++trackNumber) {
    const u32 pointerOffset = layout.sequenceHeaderAddress + trackNumber * 2;
    if (!reader.has(pointerOffset, 2)) {
      trackCount = trackNumber;
      break;
    }
    const u16 trackAddress = reader.le16(pointerOffset);
    if (trackAddress >= layout.sequenceHeaderAddress &&
        trackAddress - layout.sequenceHeaderAddress < kKonamiSnesMaxTracks * 2) {
      trackCount = (trackAddress - layout.sequenceHeaderAddress) / 2;
      break;
    }
  }
  return reader.range(layout.sequenceHeaderAddress, trackCount * 2);
}

SequenceProgram decodeKonamiSnesSequence(ByteReader reader, const KonamiSnesLayout& layout, AssetId sequenceId,
                                         SourceMapBuilder* sourceMap, std::vector<Diagnostic>* diagnostics) {
  const SourceRange headerRange = konamiSnesSequenceHeaderRange(reader, layout);
  const u32 trackCount = headerRange.size / 2;

  const auto& dialect = konamiSnesSequenceDialect(layout.version);
  SequenceDecodeSession sequence{
      reader, dialect, sequenceId, headerRange, sourceMap, kMaxTrackCommands,
  };
  const auto decode = [&](u32 offset) { return decodeCommand(reader, offset, layout.version, diagnostics); };
  for (u32 trackNumber = 0; trackNumber < trackCount; ++trackNumber) {
    const u32 pointerOffset = layout.sequenceHeaderAddress + trackNumber * 2;
    const u16 trackAddress = reader.le16(pointerOffset);
    if (trackAddress == 0 || !reader.has(trackAddress, 1)) {
      continue;
    }
    sequence.addLinearTrack(trackNumber, reader.range(pointerOffset, 2), trackAddress, decode);
  }

  SequenceProgram program = sequence.finish();
  program.config.profile = static_cast<u32>(layout.version);
  return program;
}

}  // namespace vgmtrans::formats::konami_snes
