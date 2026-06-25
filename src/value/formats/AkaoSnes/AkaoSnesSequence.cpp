/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/AkaoSnes/AkaoSnesSequence.h"

#include "value/base/LevelScale.h"
#include "value/sequence/SequenceCursorDialect.h"
#include "value/sequence/SequenceMotion.h"
#include "value/synth/SynthMath.h"

#include <fmt/format.h>

#include <algorithm>
#include <any>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace vgmtrans::formats::akao_snes {

using namespace core;

namespace {

constexpr u8 kMinTimer0Frequency = 0x24;
constexpr u8 kMaxTimer0Frequency = 0x2a;
constexpr u16 kDefaultPitchBendRangeCents = 200;
constexpr s32 kNominalDspPitch = 0x1000;
constexpr s32 kPitchFractionScale = 0x100;

enum class EventType {
  Unknown0,
  Unknown1,
  Unknown2,
  Unknown3,
  Unknown4,
  Note,
  Nop,
  Nop1,
  Volume,
  VolumeFade,
  Pan,
  PanFade,
  PitchEnvelopeOn,
  PitchEnvelopeOff,
  PitchSlide,
  VibratoOn,
  VibratoOff,
  TremoloOn,
  TremoloOff,
  PanLfoOn,
  PanLfoOnWithDelay,
  PanLfoOff,
  NoiseFreq,
  NoiseOn,
  NoiseOff,
  PitchModOn,
  PitchModOff,
  EchoOn,
  EchoOff,
  Octave,
  OctaveUp,
  OctaveDown,
  TransposeAbs,
  TransposeRel,
  Tuning,
  ProgramChange,
  VolumeEnvelope,
  GainRelease,
  DurationRate,
  AdsrAr,
  AdsrDr,
  AdsrSl,
  AdsrSr,
  AdsrDefault,
  LoopStart,
  LoopEnd,
  SlurOn,
  SlurOff,
  LegatoOn,
  LegatoOff,
  OneTimeDuration,
  JumpToSfxLo,
  JumpToSfxHi,
  End,
  Tempo,
  TempoFade,
  EchoVolume,
  EchoVolumeFade,
  EchoFeedbackFir,
  MasterVolume,
  LoopBreak,
  Goto,
  IncCpuSharedCounter,
  ZeroCpuSharedCounter,
  EchoFeedbackFade,
  EchoFirFade,
  EchoFeedback,
  EchoFir,
  CpuControlledSetValue,
  CpuControlledJump,
  CpuControlledJumpV2,
  PercOn,
  PercOff,
  VolumeAlt,
  IgnoreMasterVolume,
  IgnoreMasterVolumeBroken,
  LoopRestart,
  IgnoreMasterVolumeByPrognum,
  PlaySfx,
};

struct Context {
  AkaoSnesVersion version = AKAOSNES_NONE;
  AkaoSnesMinorVersion minorVersion = AKAOSNES_NOMINORVERSION;
  u32 romRelocBase = 0;
  u32 apuRelocBase = 0;
  std::optional<u8> initialSharedTempo;
  std::optional<u32> initialSharedTempoTrack;
};

constexpr std::array<u8, 15> kNoteDurationsV1{0xc0, 0x90, 0x60, 0x48, 0x40, 0x30, 0x24, 0x20,
                                              0x18, 0x10, 0x0c, 0x08, 0x06, 0x04, 0x03};
constexpr std::array<u8, 15> kNoteDurationsV2V3{0xc0, 0x90, 0x60, 0x40, 0x48, 0x30, 0x20, 0x24,
                                                0x18, 0x10, 0x0c, 0x08, 0x06, 0x04, 0x03};
constexpr std::array<u8, 14> kNoteDurationsV4{0xc0, 0x60, 0x40, 0x48, 0x30, 0x20, 0x24,
                                              0x18, 0x10, 0x0c, 0x08, 0x06, 0x04, 0x03};

[[nodiscard]] u8 noteDuration(AkaoSnesVersion version, u8 index) {
  if (version == AKAOSNES_V1) {
    return kNoteDurationsV1[std::min<size_t>(index, kNoteDurationsV1.size() - 1)];
  }
  if (version == AKAOSNES_V2 || version == AKAOSNES_V3) {
    return kNoteDurationsV2V3[std::min<size_t>(index, kNoteDurationsV2V3.size() - 1)];
  }
  return kNoteDurationsV4[std::min<size_t>(index, kNoteDurationsV4.size() - 1)];
}

[[nodiscard]] u32 tempoMicrosecondsPerQuarter(AkaoSnesVersion version, AkaoSnesMinorVersion minorVersion, u8 tempo) {
  if (tempo == 0) {
    return 60000000;
  }
  const u8 timer = akaoSnesTimer0Frequency(version, minorVersion);
  return static_cast<u32>(std::lround(kAkaoSnesPpqn * (125.0 * timer) * 256.0 / tempo));
}

[[nodiscard]] u8 tempoFromMicrosecondsPerQuarter(AkaoSnesVersion version, AkaoSnesMinorVersion minorVersion,
                                                 u32 microsecondsPerQuarter) {
  if (microsecondsPerQuarter == 0) {
    return 0;
  }
  const u8 timer = akaoSnesTimer0Frequency(version, minorVersion);
  const int tempo = static_cast<int>(std::lround(kAkaoSnesPpqn * (125.0 * timer) * 256.0 / microsecondsPerQuarter));
  return static_cast<u8>(std::clamp(tempo, 0, 255));
}

[[nodiscard]] double levelFromLegacyMidiVolume(u8 volume) {
  return std::clamp(static_cast<double>(volume) / 127.0, 0.0, 1.0);
}

[[nodiscard]] u8 percentPanToLegacyMidi(double percent) {
  u8 midiPan = static_cast<u8>(std::round(std::clamp(percent, 0.0, 1.0) * 126.0));
  if (midiPan != 0) {
    ++midiPan;
  }
  return midiPan;
}

[[nodiscard]] u8 linearPercentPanToLegacyMidi(double percent) {
  constexpr double kPiOverTwo = 1.57079632679489661923;
  percent = std::clamp(percent, 0.0, 1.0);
  if (percent <= 0.0) {
    return 0;
  }
  if (percent == 0.5) {
    return 64;
  }
  if (percent >= 1.0) {
    return 127;
  }
  return percentPanToLegacyMidi(std::atan2(percent, 1.0 - percent) / kPiOverTwo);
}

[[nodiscard]] u8 linear7BitPanToLegacyMidi(u8 rawPan) {
  if (rawPan == 127) {
    ++rawPan;
  }
  return linearPercentPanToLegacyMidi(static_cast<double>(rawPan) / 128.0);
}

[[nodiscard]] double stereoPositionFromRawPan(u8 pan) {
  const u8 linearPan = linearPercentPanToLegacyMidi(static_cast<double>(pan) / 255.0);
  const u8 midiPan = linear7BitPanToLegacyMidi(linearPan);
  return std::clamp((static_cast<double>(midiPan) / 127.0) * 2.0 - 1.0, -1.0, 1.0);
}

[[nodiscard]] double tuningCents(u8 tuning) {
  double pitchScale = 0.0;
  if (tuning <= 0x7f) {
    pitchScale = 1.0 + (static_cast<double>(tuning) / 256.0);
  } else {
    pitchScale = static_cast<double>(tuning) / 256.0;
  }
  return (std::log(pitchScale) / std::log(2.0)) * 1200.0;
}

[[nodiscard]] s32 akaoSnesPitchForSemitoneOffset(int semitones) {
  const double pitch = static_cast<double>(kNominalDspPitch) * std::pow(2.0, static_cast<double>(semitones) / 12.0);
  return static_cast<s32>(std::lround(pitch)) * kPitchFractionScale;
}

[[nodiscard]] s16 akaoSnesCorrectedNote(u8 note, s8 transpose) {
  return static_cast<s16>(note) + static_cast<s16>(transpose) - 10;
}

[[nodiscard]] s32 akaoSnesPitchSlideStep(AkaoSnesVersion version, s32 currentPitch, s32 targetPitch, u16 steps) {
  const s32 diff = targetPitch - currentPitch;
  if (steps == 0) {
    return 0;
  }
  if (version == AKAOSNES_V3) {
    const s32 rawDiff = diff / kPitchFractionScale;
    s32 rawStep = rawDiff / static_cast<s32>(steps);
    if (rawStep == 0 && rawDiff != 0) {
      rawStep = rawDiff > 0 ? 1 : -1;
    }
    return rawStep * kPitchFractionScale;
  }
  return diff / static_cast<s32>(steps);
}

[[nodiscard]] bool akaoSnesSupportsPitchEnvelope(AkaoSnesVersion version) {
  return version == AKAOSNES_V1 || version == AKAOSNES_V2;
}

[[nodiscard]] u16 akaoSnesPitchEnvelopeProgressStep(AkaoSnesVersion version, u8 length) {
  if (length == 0) {
    return 0;
  }
  return static_cast<u16>((version == AKAOSNES_V1 ? 0xffff : 0xff00) / length);
}

[[nodiscard]] s32 akaoSnesPitchEnvelopeOffset(s32 targetOffset, u8 progressHigh) {
  const s32 targetMagnitude = targetOffset < 0 ? -targetOffset : targetOffset;
  s32 currentMagnitude = (targetMagnitude / kPitchFractionScale) * progressHigh / 256;
  currentMagnitude *= kPitchFractionScale;
  return targetOffset < 0 ? -currentMagnitude : currentMagnitude;
}

[[nodiscard]] double akaoSnesPitchCents(s32 pitch, s32 basePitch) {
  if (pitch <= 0 || basePitch <= 0) {
    return 0.0;
  }
  return 1200.0 * std::log2(static_cast<double>(pitch) / static_cast<double>(basePitch));
}

[[nodiscard]] u16 akaoSnesPitchBendRangeCents(s32 basePitch, s32 targetPitch, u16 minimumCents) {
  const double cents = std::abs(akaoSnesPitchCents(targetPitch, basePitch));
  const int range = std::max<int>(minimumCents, static_cast<int>(std::ceil(cents)));
  return static_cast<u16>(std::min<int>(range, std::numeric_limits<u16>::max()));
}

[[nodiscard]] s16 akaoSnesPitchBendValue(s32 pitch, s32 basePitch, u16 rangeCents) {
  if (rangeCents == 0) {
    return 0;
  }
  const double bend = (akaoSnesPitchCents(pitch, basePitch) / static_cast<double>(rangeCents)) * 8192.0;
  return static_cast<s16>(std::clamp<int>(static_cast<int>(std::lround(bend)), -8192, 8191));
}

[[nodiscard]] constexpr u8 v1RateCounter(u8 rate) {
  return static_cast<u8>(rate >> 1);
}

[[nodiscard]] constexpr u8 v2RateCounter(u8 rate) {
  return static_cast<u8>(rate & 0x7f);
}

[[nodiscard]] u16 effectiveRateFrames(AkaoSnesVersion version, u8 rate, u8 depth) {
  if (version == AKAOSNES_V1) {
    return static_cast<u16>(v1RateCounter(rate)) + 1;
  }
  if (version == AKAOSNES_V2) {
    const u16 frames = v2RateCounter(rate);
    return static_cast<u16>((depth & 0x80) != 0 ? frames * 2 : frames);
  }
  if (version == AKAOSNES_V3) {
    return static_cast<u16>(rate) + 1;
  }
  return rate == 0 ? 256 : rate;
}

[[nodiscard]] u8 modulationMagnitude(AkaoSnesVersion version, u8 depth) {
  if (version == AKAOSNES_V2) {
    return static_cast<u8>((depth & 0x3f) + 1);
  }
  if (depth == 0) {
    return 0;
  }
  if (version == AKAOSNES_V1) {
    return depth;
  }
  const u8 magnitude = static_cast<u8>(depth & 0x3f);
  return version == AKAOSNES_V3 ? static_cast<u8>((magnitude * 2) + 1) : static_cast<u8>(magnitude + 1);
}

[[nodiscard]] double frameRateHz(u8 timer0Frequency) {
  return 8000.0 / timer0Frequency;
}

[[nodiscard]] double lfoRateHz(AkaoSnesVersion version, u8 rate, u8 depth, u8 timer0Frequency) {
  const u16 frames = effectiveRateFrames(version, rate, depth);
  return frames == 0 ? 0.0 : frameRateHz(timer0Frequency) / (2.0 * frames);
}

[[nodiscard]] double minLfoRateHz(AkaoSnesVersion) {
  return 1.0 / 16.0;
}

[[nodiscard]] double maxLfoRateHz(AkaoSnesVersion version) {
  if (version == AKAOSNES_V1) {
    return frameRateHz(kMinTimer0Frequency) / (2.0 * 2.0);
  }
  if (version == AKAOSNES_V2) {
    return frameRateHz(kMinTimer0Frequency) / 2.0;
  }
  return 8000.0 / kMinTimer0Frequency / 2.0;
}

[[nodiscard]] u16 v4LfoStep(u8 rate, u8 depth) {
  if (depth == 0) {
    return 0;
  }
  const u16 frames = effectiveRateFrames(AKAOSNES_V4, rate, depth);
  const u8 magnitude = modulationMagnitude(AKAOSNES_V4, depth);
  const u16 step = static_cast<u16>(64 * magnitude / frames);
  return static_cast<u16>(4 * std::max<u16>(1, step));
}

[[nodiscard]] double v4PhaseHighByteAmplitude(u8 rate, u8 depth) {
  if (depth == 0) {
    return 0.0;
  }
  return v4LfoStep(rate, depth) * effectiveRateFrames(AKAOSNES_V4, rate, depth) / 256.0;
}

[[nodiscard]] double v2PhaseHighByteAmplitude(u8 rate, u8 depth) {
  const u8 frames = v2RateCounter(rate);
  if (frames == 0) {
    return 0.0;
  }
  const u16 step = static_cast<u16>(512 * modulationMagnitude(AKAOSNES_V2, depth) / frames);
  return std::min(128.0, static_cast<double>((step * frames) >> 8));
}

[[nodiscard]] double modulationAmplitude(AkaoSnesVersion version, u8 rate, u8 depth) {
  if (version == AKAOSNES_V2) {
    return v2PhaseHighByteAmplitude(rate, depth);
  }
  if (depth == 0) {
    return 0.0;
  }
  if (version == AKAOSNES_V3) {
    const double magnitude = modulationMagnitude(version, depth);
    return ((depth & 0xc0) == 0xc0) ? magnitude : magnitude / 2.0;
  }
  return v4PhaseHighByteAmplitude(rate, depth);
}

[[nodiscard]] u8 v1VibratoHighByteAmplitude(u8 rate, u8 depth) {
  const u8 counter = v1RateCounter(rate);
  if (counter == 0 || depth == 0) {
    return 0;
  }
  const u32 step = (256u * depth) / counter;
  return static_cast<u8>((step * counter) / 256u);
}

[[nodiscard]] double vibratoDepthCentsForAmplitude(double amplitude) {
  if (amplitude <= 0.0) {
    return 0.0;
  }
  const double ratio = 15.0 * amplitude / 32768.0;
  const double centsUp = 1200.0 * std::log2(1.0 + ratio);
  const double centsDown = -1200.0 * std::log2(1.0 - ratio);
  return std::max(centsUp, centsDown);
}

[[nodiscard]] double v1VibratoDepthCentsForHighByte(u8 amplitude) {
  if (amplitude == 0) {
    return 0.0;
  }
  return 1200.0 * std::log2(1.0 + (static_cast<double>(amplitude) / 3072.0));
}

[[nodiscard]] double v2VibratoDepthCents(u8 rate, u8 depth) {
  const double amplitude = std::min(127.0, v2PhaseHighByteAmplitude(rate, depth));
  if (amplitude <= 0.0) {
    return 0.0;
  }
  if (depth < 0x40) {
    return -1200.0 * std::log2(1.0 - (15.0 * amplitude / 65536.0));
  }
  return 1200.0 * std::log2(1.0 + (15.0 * amplitude / 32768.0));
}

[[nodiscard]] double vibratoDepthCents(AkaoSnesVersion version, u8 rate, u8 depth) {
  if (version == AKAOSNES_V1) {
    return v1VibratoDepthCentsForHighByte(v1VibratoHighByteAmplitude(rate, depth));
  }
  if (version == AKAOSNES_V2) {
    return v2VibratoDepthCents(rate, depth);
  }
  return vibratoDepthCentsForAmplitude(modulationAmplitude(version, rate, depth));
}

[[nodiscard]] double tremoloDepthDbForAmplitude(double amplitude) {
  if (amplitude <= 0.0) {
    return 0.0;
  }
  const double troughScale = std::max(1.0 / 1024.0, 1.0 - (amplitude / 128.0));
  return -20.0 * std::log10(troughScale);
}

[[nodiscard]] double v3TremoloPeakToTroughDb(u8 depth) {
  if (depth == 0) {
    return 0.0;
  }
  const double magnitude = modulationMagnitude(AKAOSNES_V3, depth);
  const double troughScale = (128.0 - std::min(127.0, magnitude)) / 128.0;
  return -20.0 * std::log10(troughScale);
}

[[nodiscard]] u8 lfoDelayTicks(AkaoSnesVersion version, u8 delay) {
  if (version == AKAOSNES_V1) {
    return delay == 0xff ? 0 : delay;
  }
  if (version == AKAOSNES_V4) {
    return delay == 0 ? 0 : static_cast<u8>(delay - 1);
  }
  return delay;
}

[[nodiscard]] double delaySeconds(AkaoSnesVersion version, u8 delay, u8 tempo, u8 timer0Frequency) {
  const u8 ticks = lfoDelayTicks(version, delay);
  if (ticks == 0) {
    return 0.0;
  }
  const u8 safeTempo = tempo == 0 ? 1 : tempo;
  return ticks * (256.0 / (frameRateHz(timer0Frequency) * safeTempo));
}

[[nodiscard]] u32 driverFramesToTicks(double frames, u8 tempo) {
  const u8 safeTempo = tempo == 0 ? 1 : tempo;
  return std::max<u32>(1, static_cast<u32>(std::lround(frames * safeTempo / 256.0)));
}

[[nodiscard]] double maxVibratoDepthCents(AkaoSnesVersion version) {
  switch (version) {
    case AKAOSNES_V1:
      return v1VibratoDepthCentsForHighByte(255);
    case AKAOSNES_V2:
      return 1200.0 * std::log2(1.0 + (15.0 * 127.0 / 32768.0));
    case AKAOSNES_V3:
      return vibratoDepthCentsForAmplitude(127.0);
    case AKAOSNES_V4:
    default:
      return vibratoDepthCentsForAmplitude(64.0);
  }
}

[[nodiscard]] double maxTremoloDepthDb(AkaoSnesVersion version) {
  if (version == AKAOSNES_V3) {
    return tremoloDepthDbForAmplitude(127.0);
  }
  if (version == AKAOSNES_V4) {
    return tremoloDepthDbForAmplitude(64.0);
  }
  return 0.0;
}

[[nodiscard]] double maxDelaySeconds(AkaoSnesVersion version) {
  constexpr double kMaxV1DelaySeconds = 254.0 * 256.0 / (8000.0 / kMinTimer0Frequency);
  constexpr double kMaxV4DelaySeconds = 254.0 * 256.0 / ((8000.0 / kMaxTimer0Frequency) * 1.0);
  constexpr double kMaxDelaySeconds = 255.0 * 256.0 / ((8000.0 / kMaxTimer0Frequency) * 1.0);
  if (version == AKAOSNES_V1) {
    return kMaxV1DelaySeconds;
  }
  return version == AKAOSNES_V4 ? kMaxV4DelaySeconds : kMaxDelaySeconds;
}

[[nodiscard]] u8 midiValueForAmountInRange(s32 currentAmount, s32 minAmount, s32 maxAmount) {
  if (minAmount == maxAmount) {
    return 0;
  }
  const int midiValue =
      static_cast<int>(std::round(128.0 * (currentAmount - minAmount) / static_cast<double>(maxAmount - minAmount)));
  return static_cast<u8>(std::clamp(midiValue, 0, 127));
}

[[nodiscard]] u8 midiValueForHertzInRange(double hertz, double minHertz, double maxHertz) {
  if (hertz <= 0.0 || minHertz <= 0.0 || maxHertz <= 0.0 || !std::isfinite(hertz) || !std::isfinite(minHertz) ||
      !std::isfinite(maxHertz)) {
    return 0;
  }
  const s32 minAmount = synthAmountFromHertz(minHertz);
  const s32 rangeAmount = synthAmountFromHertzRange(minHertz, maxHertz);
  const s32 currentAmount = synthAmountFromHertz(hertz);
  return rangeAmount == 0 ? 0 : midiValueForAmountInRange(currentAmount, minAmount, minAmount + rangeAmount);
}

[[nodiscard]] u8 midiValueForSecondsInRange(double seconds, double minSeconds, double maxSeconds) {
  if (maxSeconds <= 0.0 || !std::isfinite(seconds) || !std::isfinite(minSeconds) || !std::isfinite(maxSeconds)) {
    return 0;
  }
  const s32 minAmount = synthAmountFromSeconds(synthSecondsRangeMinimum(minSeconds));
  const s32 rangeAmount = synthAmountFromSecondsRange(minSeconds, maxSeconds);
  const s32 currentAmount = synthAmountFromSeconds(synthSecondsRangeMinimum(seconds));
  return rangeAmount == 0 ? 0 : midiValueForAmountInRange(currentAmount, minAmount, minAmount + rangeAmount);
}

[[nodiscard]] u8 midiValueForDepthRange(double value, double maxValue) {
  if (maxValue <= 0.0) {
    return 0;
  }
  const int midiValue = static_cast<int>(std::lround(128.0 * value / maxValue));
  return static_cast<u8>(std::clamp(midiValue, 0, 127));
}

[[nodiscard]] bool isLfoActive(AkaoSnesVersion version, u8 rate, u8 depth) {
  if (version == AKAOSNES_V2) {
    return v2RateCounter(rate) != 0;
  }
  if (depth == 0) {
    return false;
  }
  return version != AKAOSNES_V1 || v1RateCounter(rate) != 0;
}

[[nodiscard]] bool exportsTremolo(AkaoSnesVersion version) {
  return version == AKAOSNES_V3 || version == AKAOSNES_V4;
}

[[nodiscard]] u8 vibratoDepthMidiValue(AkaoSnesVersion version, u8 rate, u8 depth) {
  if (!isLfoActive(version, rate, depth)) {
    return 0;
  }
  const int midiValue =
      static_cast<int>(std::lround(128.0 * vibratoDepthCents(version, rate, depth) / maxVibratoDepthCents(version)));
  return static_cast<u8>(std::clamp(midiValue, version == AKAOSNES_V3 ? 1 : 0, 127));
}

[[nodiscard]] u8 tremoloDepthMidiValue(AkaoSnesVersion version, u8 rate, u8 depth, u8 delay = 0) {
  if (!exportsTremolo(version) || !isLfoActive(version, rate, depth)) {
    return 0;
  }
  double depthDb = 0.0;
  if (version == AKAOSNES_V3) {
    constexpr double kV3SteppedTremoloSmoothLfoCompensation = 2.0;
    depthDb = kV3SteppedTremoloSmoothLfoCompensation * v3TremoloPeakToTroughDb(depth);
  } else if (delay != 0) {
    depthDb = tremoloDepthDbForAmplitude(v4PhaseHighByteAmplitude(rate, depth) / 4.0);
  } else {
    depthDb = tremoloDepthDbForAmplitude(v4PhaseHighByteAmplitude(rate, depth));
  }
  return midiValueForDepthRange(depthDb, maxTremoloDepthDb(version));
}

[[nodiscard]] u8 rateMidiValue(AkaoSnesVersion version, u8 rate, u8 depth, u8 timer0Frequency) {
  return midiValueForHertzInRange(lfoRateHz(version, rate, depth, timer0Frequency), minLfoRateHz(version),
                                  maxLfoRateHz(version));
}

[[nodiscard]] u8 delayMidiValue(AkaoSnesVersion version, u8 delay, u8 tempo, u8 timer0Frequency) {
  return midiValueForSecondsInRange(delaySeconds(version, delay, tempo, timer0Frequency), 0.0,
                                    maxDelaySeconds(version));
}

[[nodiscard]] u32 v1VibratoRampTicks(u8 rate, u8 tempo) {
  const u8 counter = v1RateCounter(rate);
  if (counter == 0) {
    return 0;
  }
  return driverFramesToTicks(14.0 * (counter + 1), tempo);
}

[[nodiscard]] u32 v3LfoRampTicks(u8 rate, u8 tempo) {
  return driverFramesToTicks(6.0 * effectiveRateFrames(AKAOSNES_V3, rate, 0), tempo);
}

[[nodiscard]] u32 v4VibratoRampTicks(u8 rate, u8 tempo) {
  const double rampFrames = 7.0 * effectiveRateFrames(AKAOSNES_V4, rate, 0);
  return driverFramesToTicks(rampFrames, tempo);
}

[[nodiscard]] u16 relocatedAddress(u16 romAddress, u32 romRelocBase, u32 apuRelocBase) {
  return static_cast<u16>((static_cast<u32>(romAddress) - romRelocBase) + apuRelocBase);
}

[[nodiscard]] EventType eventType(AkaoSnesVersion version, AkaoSnesMinorVersion minorVersion, u8 opcode) {
  if (opcode <= akaoSnesStatusNoteMax(version)) {
    return EventType::Note;
  }

  if (version == AKAOSNES_V1) {
    switch (opcode) {
      case 0xd2:
        return EventType::TempoFade;
      case 0xd3:
        return EventType::Nop1;
      case 0xd4:
        return EventType::EchoVolume;
      case 0xd5:
        return EventType::EchoFeedbackFir;
      case 0xd6:
        return EventType::PitchEnvelopeOn;
      case 0xd7:
        return EventType::TremoloOn;
      case 0xd8:
        return EventType::VibratoOn;
      case 0xd9:
        return EventType::PanLfoOnWithDelay;
      case 0xda:
        return EventType::Octave;
      case 0xdb:
        return EventType::ProgramChange;
      case 0xdc:
        return EventType::VolumeEnvelope;
      case 0xdd:
        return EventType::GainRelease;
      case 0xde:
        return EventType::DurationRate;
      case 0xdf:
        return EventType::NoiseFreq;
      case 0xe0:
        return EventType::LoopStart;
      case 0xe1:
        return EventType::OctaveUp;
      case 0xe2:
        return EventType::OctaveDown;
      case 0xe3:
      case 0xe4:
      case 0xe5:
        return EventType::Nop;
      case 0xe6:
        return EventType::PitchEnvelopeOff;
      case 0xe7:
        return EventType::TremoloOff;
      case 0xe8:
        return EventType::VibratoOff;
      case 0xe9:
        return EventType::PanLfoOff;
      case 0xea:
        return EventType::EchoOn;
      case 0xeb:
        return EventType::EchoOff;
      case 0xec:
        return EventType::NoiseOn;
      case 0xed:
        return EventType::NoiseOff;
      case 0xee:
        return EventType::PitchModOn;
      case 0xef:
        return EventType::PitchModOff;
      case 0xf0:
        return EventType::LoopEnd;
      case 0xf1:
      case 0xf7:
      case 0xf8:
      case 0xf9:
      case 0xfa:
      case 0xfb:
      case 0xfc:
      case 0xfd:
      case 0xfe:
      case 0xff:
        return EventType::End;
      case 0xf2:
        return EventType::VolumeFade;
      case 0xf3:
        return EventType::PanFade;
      case 0xf4:
        return EventType::Goto;
      case 0xf5:
        return EventType::LoopBreak;
      case 0xf6:
        return EventType::Unknown0;
      default:
        return EventType::End;
    }
  }

  if (version == AKAOSNES_V2) {
    switch (opcode) {
      case 0xd2:
        return EventType::Tempo;
      case 0xd3:
        return EventType::TempoFade;
      case 0xd4:
        return EventType::Volume;
      case 0xd5:
        return EventType::VolumeFade;
      case 0xd6:
        return EventType::Pan;
      case 0xd7:
        return EventType::PanFade;
      case 0xd8:
        return EventType::EchoVolume;
      case 0xd9:
        return EventType::EchoVolumeFade;
      case 0xda:
        return EventType::TransposeAbs;
      case 0xdb:
        return EventType::PitchEnvelopeOn;
      case 0xdc:
        return EventType::PitchEnvelopeOff;
      case 0xdd:
        return EventType::VibratoOn;
      case 0xde:
        return EventType::VibratoOff;
      case 0xdf:
        return EventType::TremoloOn;
      case 0xe0:
        return EventType::TremoloOff;
      case 0xe1:
        return EventType::NoiseFreq;
      case 0xe2:
        return EventType::NoiseOn;
      case 0xe3:
        return EventType::NoiseOff;
      case 0xe4:
        return EventType::PitchModOn;
      case 0xe5:
        return EventType::PitchModOff;
      case 0xe6:
        return EventType::EchoFeedbackFir;
      case 0xe7:
        return EventType::EchoOn;
      case 0xe8:
        return EventType::EchoOff;
      case 0xe9:
        return EventType::PanLfoOn;
      case 0xea:
        return EventType::PanLfoOff;
      case 0xeb:
        return EventType::Octave;
      case 0xec:
        return EventType::OctaveUp;
      case 0xed:
        return EventType::OctaveDown;
      case 0xee:
        return EventType::LoopStart;
      case 0xef:
        return EventType::LoopEnd;
      case 0xf0:
        return EventType::LoopBreak;
      case 0xf1:
        return EventType::Goto;
      case 0xf2:
        return EventType::SlurOn;
      case 0xf3:
        return EventType::ProgramChange;
      case 0xf4:
        return EventType::VolumeEnvelope;
      case 0xf5:
        return EventType::SlurOff;
      case 0xf6:
        return EventType::Unknown2;
      case 0xf7:
        return EventType::Tuning;
      case 0xf8:
      case 0xf9:
      case 0xfa:
      case 0xfb:
      case 0xfc:
      case 0xfd:
      case 0xfe:
      case 0xff:
        return EventType::End;
      default:
        return EventType::End;
    }
  }

  if (version == AKAOSNES_V3) {
    switch (opcode) {
      case 0xd2:
        return EventType::Volume;
      case 0xd3:
        return EventType::VolumeFade;
      case 0xd4:
        return EventType::Pan;
      case 0xd5:
        return EventType::PanFade;
      case 0xd6:
        return EventType::PitchSlide;
      case 0xd7:
        return EventType::VibratoOn;
      case 0xd8:
        return EventType::VibratoOff;
      case 0xd9:
        return EventType::TremoloOn;
      case 0xda:
        return EventType::TremoloOff;
      case 0xdb:
        return EventType::PanLfoOn;
      case 0xdc:
        return EventType::PanLfoOff;
      case 0xdd:
        return EventType::NoiseFreq;
      case 0xde:
        return EventType::NoiseOn;
      case 0xdf:
        return EventType::NoiseOff;
      case 0xe0:
        return EventType::PitchModOn;
      case 0xe1:
        return EventType::PitchModOff;
      case 0xe2:
        return EventType::EchoOn;
      case 0xe3:
        return EventType::EchoOff;
      case 0xe4:
        return EventType::Octave;
      case 0xe5:
        return EventType::OctaveUp;
      case 0xe6:
        return EventType::OctaveDown;
      case 0xe7:
        return EventType::TransposeAbs;
      case 0xe8:
        return EventType::TransposeRel;
      case 0xe9:
        return EventType::Tuning;
      case 0xea:
        return EventType::ProgramChange;
      case 0xeb:
        return EventType::AdsrAr;
      case 0xec:
        return EventType::AdsrDr;
      case 0xed:
        return EventType::AdsrSl;
      case 0xee:
        return EventType::AdsrSr;
      case 0xef:
        return EventType::AdsrDefault;
      case 0xf0:
        return EventType::LoopStart;
      case 0xf1:
        return EventType::LoopEnd;
      case 0xf2:
        return EventType::End;
      case 0xf3:
        return EventType::Tempo;
      case 0xf4:
        return EventType::TempoFade;
      case 0xf5:
        return EventType::EchoVolume;
      case 0xf6:
        return EventType::EchoVolumeFade;
      case 0xf7:
        return EventType::EchoFeedbackFir;
      case 0xf8:
        return EventType::MasterVolume;
      case 0xf9:
        return EventType::LoopBreak;
      case 0xfa:
        return EventType::Goto;
      case 0xfb:
        return EventType::CpuControlledJump;
      case 0xfc:
        return minorVersion == AKAOSNES_V3_SD2 ? EventType::LoopRestart : EventType::End;
      case 0xfd:
        return minorVersion == AKAOSNES_V3_SD2 ? EventType::IgnoreMasterVolumeByPrognum : EventType::End;
      case 0xfe:
      case 0xff:
        return EventType::End;
      default:
        return EventType::End;
    }
  }

  switch (opcode) {
    case 0xc4:
      return EventType::Volume;
    case 0xc5:
      return EventType::VolumeFade;
    case 0xc6:
      return EventType::Pan;
    case 0xc7:
      return EventType::PanFade;
    case 0xc8:
      return EventType::PitchSlide;
    case 0xc9:
      return EventType::VibratoOn;
    case 0xca:
      return EventType::VibratoOff;
    case 0xcb:
      return EventType::TremoloOn;
    case 0xcc:
      return EventType::TremoloOff;
    case 0xcd:
      return EventType::PanLfoOn;
    case 0xce:
      return EventType::PanLfoOff;
    case 0xcf:
      return EventType::NoiseFreq;
    case 0xd0:
      return EventType::NoiseOn;
    case 0xd1:
      return EventType::NoiseOff;
    case 0xd2:
      return EventType::PitchModOn;
    case 0xd3:
      return EventType::PitchModOff;
    case 0xd4:
      return EventType::EchoOn;
    case 0xd5:
      return EventType::EchoOff;
    case 0xd6:
      return EventType::Octave;
    case 0xd7:
      return EventType::OctaveUp;
    case 0xd8:
      return EventType::OctaveDown;
    case 0xd9:
      return EventType::TransposeAbs;
    case 0xda:
      return EventType::TransposeRel;
    case 0xdb:
      return EventType::Tuning;
    case 0xdc:
      return EventType::ProgramChange;
    case 0xdd:
      return EventType::AdsrAr;
    case 0xde:
      return EventType::AdsrDr;
    case 0xdf:
      return EventType::AdsrSl;
    case 0xe0:
      return EventType::AdsrSr;
    case 0xe1:
      return EventType::AdsrDefault;
    case 0xe2:
      return EventType::LoopStart;
    case 0xe3:
      return EventType::LoopEnd;
    case 0xe4:
      return EventType::SlurOn;
    case 0xe5:
      return EventType::SlurOff;
    case 0xe6:
      return EventType::LegatoOn;
    case 0xe7:
      return EventType::LegatoOff;
    case 0xe8:
      return EventType::OneTimeDuration;
    case 0xe9:
      return EventType::JumpToSfxLo;
    case 0xea:
      return EventType::JumpToSfxHi;
    case 0xeb:
      return minorVersion == AKAOSNES_V4_GH ? EventType::Unknown1 : EventType::End;
    case 0xec:
    case 0xed:
    case 0xee:
    case 0xef:
      return EventType::End;
    case 0xf0:
      return EventType::Tempo;
    case 0xf1:
      return EventType::TempoFade;
    case 0xf2:
      return EventType::EchoVolume;
    case 0xf3:
      return EventType::EchoVolumeFade;
    default:
      break;
  }

  if (minorVersion == AKAOSNES_V4_RS2) {
    switch (opcode) {
      case 0xf4:
        return EventType::EchoFeedbackFir;
      case 0xf5:
        return EventType::MasterVolume;
      case 0xf6:
        return EventType::LoopBreak;
      case 0xf7:
        return EventType::Goto;
      case 0xf8:
        return EventType::IncCpuSharedCounter;
      case 0xf9:
        return EventType::ZeroCpuSharedCounter;
      case 0xfa:
        return EventType::IgnoreMasterVolumeBroken;
      default:
        return EventType::End;
    }
  }
  if (minorVersion == AKAOSNES_V4_LAL) {
    switch (opcode) {
      case 0xf4:
        return EventType::EchoFeedbackFir;
      case 0xf5:
        return EventType::MasterVolume;
      case 0xf6:
        return EventType::LoopBreak;
      case 0xf7:
        return EventType::Goto;
      case 0xf8:
        return EventType::IncCpuSharedCounter;
      case 0xf9:
        return EventType::ZeroCpuSharedCounter;
      case 0xfa:
        return EventType::IgnoreMasterVolume;
      case 0xfb:
        return EventType::CpuControlledJump;
      default:
        return EventType::End;
    }
  }
  if (minorVersion == AKAOSNES_V4_FF6) {
    switch (opcode) {
      case 0xf4:
        return EventType::MasterVolume;
      case 0xf5:
        return EventType::LoopBreak;
      case 0xf6:
        return EventType::Goto;
      case 0xf7:
        return EventType::EchoFeedbackFade;
      case 0xf8:
        return EventType::EchoFirFade;
      case 0xf9:
        return EventType::IncCpuSharedCounter;
      case 0xfa:
        return EventType::ZeroCpuSharedCounter;
      case 0xfb:
        return EventType::IgnoreMasterVolume;
      case 0xfc:
        return EventType::CpuControlledJump;
      default:
        return EventType::End;
    }
  }
  if (minorVersion == AKAOSNES_V4_FM || minorVersion == AKAOSNES_V4_CT) {
    switch (opcode) {
      case 0xf4:
        return EventType::MasterVolume;
      case 0xf5:
        return EventType::LoopBreak;
      case 0xf6:
        return EventType::Goto;
      case 0xf7:
        return EventType::EchoFeedbackFade;
      case 0xf8:
        return EventType::EchoFirFade;
      case 0xf9:
        return minorVersion == AKAOSNES_V4_CT ? EventType::CpuControlledSetValue : EventType::Unknown1;
      case 0xfa:
        return EventType::CpuControlledJumpV2;
      case 0xfb:
        return EventType::PercOn;
      case 0xfc:
        return EventType::PercOff;
      case 0xfd:
        return EventType::VolumeAlt;
      default:
        return EventType::End;
    }
  }
  if (minorVersion == AKAOSNES_V4_RS3 || minorVersion == AKAOSNES_V4_GH || minorVersion == AKAOSNES_V4_BSGAME) {
    switch (opcode) {
      case 0xf4:
        return EventType::VolumeAlt;
      case 0xf5:
        return EventType::LoopBreak;
      case 0xf6:
        return EventType::Goto;
      case 0xf7:
        return EventType::EchoFeedback;
      case 0xf8:
        return EventType::EchoFir;
      case 0xf9:
        return EventType::CpuControlledSetValue;
      case 0xfa:
        return EventType::CpuControlledJumpV2;
      case 0xfb:
        return EventType::PercOn;
      case 0xfc:
        return EventType::PercOff;
      case 0xfd:
        return minorVersion == AKAOSNES_V4_RS3      ? EventType::PlaySfx
               : minorVersion == AKAOSNES_V4_BSGAME ? EventType::Unknown1
                                                    : EventType::End;
      case 0xfe:
        return minorVersion == AKAOSNES_V4_BSGAME ? EventType::Unknown0 : EventType::End;
      default:
        return EventType::End;
    }
  }
  return EventType::End;
}

struct LoopFrame {
  Address start;
  u32 totalPlays = 0;
  u32 remainingPlays = 0;
  u8 incrementCount = 0;
};

enum class LfoTarget {
  Vibrato,
  Tremolo,
};

struct LfoParams {
  u8 delay = 0;
  u8 rate = 0;
  u8 depth = 0;
};

class LfoState {
public:
  void reset() {
    delay_ = 0;
    rate_ = 0;
    depth_ = 0;
    fade_.reset(0);
    reusableTicks_ = 0;
    reusableStep_ = 0;
    lastMidiDepth_.reset();
  }

  void configure(u8 delay, u8 rate, u8 depth) {
    delay_ = delay;
    rate_ = rate;
    depth_ = depth;
    clearReusableFade();
  }

  void setDepth(u8 depth) { depth_ = depth; }
  [[nodiscard]] u8 delay() const { return delay_; }
  [[nodiscard]] u8 rate() const { return rate_; }
  [[nodiscard]] u8 depth() const { return depth_; }

  [[nodiscard]] s32 configuredDepth(u8 fractionalBits = 0) const { return static_cast<s32>(depth_) << fractionalBits; }

  [[nodiscard]] s32 clampToConfiguredDepth(s32 depth, u8 fractionalBits = 0) const {
    return std::min(configuredDepth(fractionalBits), depth);
  }

  void clearReusableFade() {
    reusableTicks_ = 0;
    reusableStep_ = 0;
    fade_.clearMotion();
  }

  void setReusableFade(u32 ticks, s32 step) {
    reusableTicks_ = ticks;
    reusableStep_ = step;
  }

  void setReusableFadeToConfiguredDepth(u32 ticks, u8 fractionalBits = 0) {
    const s32 target = configuredDepth(fractionalBits);
    setReusableFade(ticks, ticks == 0 ? 0 : target / static_cast<s32>(ticks));
  }

  [[nodiscard]] bool hasReusableFade() const { return reusableTicks_ != 0; }

  void beginReusableFade(u32 delayTicks, s32 targetDepth, s32 initialDepth = 0) {
    if (!hasReusableFade()) {
      fade_.clearMotion();
      return;
    }
    fade_.setCurrent(initialDepth);
    static_cast<void>(fade_.begin(
        SequenceMotionPlan<s32>::targetOverTicksWithStep(targetDepth, reusableStep_, reusableTicks_, delayTicks)));
  }

  bool beginReusableFadeToConfiguredDepth(u8 fractionalBits = 0, s32 initialDepth = 0) {
    if (!hasReusableFade()) {
      return false;
    }
    beginReusableFade(delay_, configuredDepth(fractionalBits), initialDepth);
    return true;
  }

  [[nodiscard]] bool fadeActive() const { return fade_.active(); }
  [[nodiscard]] SequenceMotionTick<s32> tickFade() { return fade_.tick(); }
  void setCurrentDepthPreservingMotion(s32 depth) { fade_.setCurrentPreservingMotion(depth); }

  template <typename EmitDepth>
  bool emitDepth(u8 depth, EmitDepth&& emitDepth, bool force = false) {
    if (!force && lastMidiDepth_ && *lastMidiDepth_ == depth) {
      return false;
    }
    std::forward<EmitDepth>(emitDepth)(depth);
    lastMidiDepth_ = depth;
    return true;
  }

private:
  u8 delay_ = 0;
  u8 rate_ = 0;
  u8 depth_ = 0;
  SequenceAutomatedValue<s32> fade_;
  u32 reusableTicks_ = 0;
  s32 reusableStep_ = 0;
  std::optional<u8> lastMidiDepth_;
};

struct InitialSharedTempoHint {
  u8 tempo = 0;
  u32 sourceTrackNumber = 0;
};

struct SharedTempoChange {
  u64 tick = 0;
  u8 tempo = 0;
  u32 sourceTrackNumber = 0;
};

[[nodiscard]] std::optional<size_t> zeroTimeCommandSize(AkaoSnesVersion version, AkaoSnesMinorVersion minorVersion,
                                                        std::span<const u8> bytes) {
  if (bytes.empty()) {
    return std::nullopt;
  }

  const auto require = [&](size_t size) -> std::optional<size_t> {
    return bytes.size() >= size ? std::optional<size_t>(size) : std::nullopt;
  };

  switch (eventType(version, minorVersion, bytes.front())) {
    case EventType::Note:
    case EventType::Goto:
    case EventType::LoopBreak:
    case EventType::CpuControlledJump:
    case EventType::End:
      return std::nullopt;

    case EventType::TempoFade:
    case EventType::VolumeFade:
    case EventType::PanFade:
      return require(version == AKAOSNES_V1 ? 4 : 3);

    case EventType::PitchEnvelopeOn:
    case EventType::TremoloOn:
    case EventType::VibratoOn:
    case EventType::PanLfoOnWithDelay:
      return require(4);

    case EventType::EchoFeedbackFir:
    case EventType::EchoVolumeFade:
    case EventType::EchoFeedbackFade:
    case EventType::EchoFirFade:
    case EventType::PitchSlide:
    case EventType::PanLfoOn:
    case EventType::CpuControlledJumpV2:
      return require(3);

    case EventType::Nop1:
    case EventType::Tempo:
    case EventType::Volume:
    case EventType::Pan:
    case EventType::EchoVolume:
    case EventType::Octave:
    case EventType::TransposeAbs:
    case EventType::TransposeRel:
    case EventType::Tuning:
    case EventType::ProgramChange:
    case EventType::VolumeEnvelope:
    case EventType::GainRelease:
    case EventType::DurationRate:
    case EventType::NoiseFreq:
    case EventType::LoopStart:
    case EventType::AdsrAr:
    case EventType::AdsrDr:
    case EventType::AdsrSl:
    case EventType::AdsrSr:
    case EventType::MasterVolume:
    case EventType::OneTimeDuration:
    case EventType::JumpToSfxLo:
    case EventType::JumpToSfxHi:
    case EventType::EchoFeedback:
    case EventType::EchoFir:
    case EventType::CpuControlledSetValue:
    case EventType::VolumeAlt:
    case EventType::IgnoreMasterVolumeByPrognum:
    case EventType::PlaySfx:
    case EventType::Unknown1:
      return require(2);

    case EventType::Nop:
    case EventType::PitchEnvelopeOff:
    case EventType::TremoloOff:
    case EventType::VibratoOff:
    case EventType::PanLfoOff:
    case EventType::NoiseOn:
    case EventType::NoiseOff:
    case EventType::PitchModOn:
    case EventType::PitchModOff:
    case EventType::EchoOn:
    case EventType::EchoOff:
    case EventType::OctaveUp:
    case EventType::OctaveDown:
    case EventType::LoopEnd:
    case EventType::LoopRestart:
    case EventType::SlurOn:
    case EventType::SlurOff:
    case EventType::LegatoOn:
    case EventType::LegatoOff:
    case EventType::AdsrDefault:
    case EventType::PercOn:
    case EventType::PercOff:
    case EventType::IgnoreMasterVolume:
    case EventType::IgnoreMasterVolumeBroken:
    case EventType::IncCpuSharedCounter:
    case EventType::ZeroCpuSharedCounter:
    case EventType::Unknown0:
      return require(1);

    case EventType::Unknown2:
    case EventType::Unknown3:
    case EventType::Unknown4:
      return std::nullopt;
  }

  return std::nullopt;
}

[[nodiscard]] std::optional<u8> tempoFromPreludeCommand(AkaoSnesVersion version, AkaoSnesMinorVersion minorVersion,
                                                        std::span<const u8> bytes) {
  if (bytes.empty()) {
    return std::nullopt;
  }

  const EventType type = eventType(version, minorVersion, bytes.front());
  if (type == EventType::Tempo && bytes.size() >= 2) {
    u8 rawTempo = bytes[1];
    if (minorVersion == AKAOSNES_V4_FM || minorVersion == AKAOSNES_V4_CT) {
      rawTempo = static_cast<u8>(rawTempo + ((rawTempo * 0x14) >> 8));
    }
    return rawTempo;
  }

  if (type != EventType::TempoFade) {
    return std::nullopt;
  }

  if (version == AKAOSNES_V1) {
    if (bytes.size() >= 4 && bytes[1] == 0 && bytes[2] == 0) {
      return bytes[3];
    }
    return std::nullopt;
  }

  if (bytes.size() >= 3 && bytes[1] == 0) {
    u8 rawTempo = bytes[2];
    if (minorVersion == AKAOSNES_V4_FM || minorVersion == AKAOSNES_V4_CT) {
      rawTempo = static_cast<u8>(rawTempo + ((rawTempo * 0x14) >> 8));
    }
    return rawTempo;
  }

  return std::nullopt;
}

[[nodiscard]] std::optional<u8> initialSharedTempoInPrelude(AkaoSnesVersion version, AkaoSnesMinorVersion minorVersion,
                                                            std::span<const u8> bytes) {
  size_t offset = 0;
  while (offset < bytes.size()) {
    const std::span<const u8> remaining = bytes.subspan(offset);
    const auto size = zeroTimeCommandSize(version, minorVersion, remaining);
    if (!size) {
      return std::nullopt;
    }

    if (const auto tempo = tempoFromPreludeCommand(version, minorVersion, remaining)) {
      return tempo;
    }

    offset += *size;
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<InitialSharedTempoHint> initialSharedTempoFromProgram(const SequenceProgram& program,
                                                                                  const Context& context) {
  for (const TrackProgram& track : program.tracks) {
    if (const auto tempo = initialSharedTempoInPrelude(context.version, context.minorVersion, track.commandBytes)) {
      return InitialSharedTempoHint{
          .tempo = *tempo,
          .sourceTrackNumber = track.sourceTrackNumber,
      };
    }
  }
  return std::nullopt;
}

struct PitchEnvelopeState {
  bool enabled = false;
  s8 semitones = 0;
  u8 delay = 0;
  u8 length = 0;
  u16 progressStep = 0;

  bool active = false;
  u8 activeDelay = 0;
  u8 activeCount = 0;
  u32 progress = 0;
  s32 targetOffset = 0;
};

struct TrackState {
  TrackState() = default;
  TrackState(const BytecodeDecodeContext&, const Context& context) { reset(context); }
  TrackState(const SequenceProgram& program, const TrackProgram& track, const Context& context) {
    reset(context);
    if (const auto hint = initialSharedTempoFromProgram(program, context)) {
      initialSharedTempo = hint->tempo;
      initialSharedTempoTrack = hint->sourceTrackNumber;
      lfoBeforeInitialSharedTempoTrack = track.sourceTrackNumber < hint->sourceTrackNumber;
      lfoAfterInitialSharedTempoTrack = track.sourceTrackNumber > hint->sourceTrackNumber;
    }
  }

  void reset(const Context& context) {
    octave = 6;
    transpose = 0;
    onetimeDuration = 0;
    slur = false;
    legato = false;
    percussion = false;
    nonPercussionProgram = 0;
    loopLevel = 0;
    tempo = kAkaoSnesDefaultTempo;
    pan8Bit = akaoSnesUses8BitPan(context.version, context.minorVersion);
    initialSharedTempo = context.initialSharedTempo;
    initialSharedTempoTrack = context.initialSharedTempoTrack;
    lfoBeforeInitialSharedTempoTrack = false;
    lfoAfterInitialSharedTempoTrack = false;
    initialSharedTempoApplied = false;
    lastTieableNoteTick.reset();
    pitchWaitEndTick.reset();
    pitchWaitFallthrough.reset();
    pitchWaitBoundaryClassified = false;
    pitchWaitStopsPitchEnvelope = false;
    sharedTempoCacheBuilt = false;
    sharedTempoChanges.clear();
    sharedTempoCursor = 0;
    pitchEnvelope = {};
    pitchBaseValid = false;
    pitchBase = kNominalDspPitch * kPitchFractionScale;
    currentPitch = pitchBase;
    currentPitchBendRangeCents = kDefaultPitchBendRangeCents;
    currentPitchBendValue = 0;
    pendingPitchSlideSteps = 0;
    pendingPitchSlideSemitones = 0;
    pitchSlideActive = false;
    pitchSlideStepsRemaining = 0;
    pitchSlideStep = 0;
    pitchSlideFinalPitch = currentPitch;
    pitchSlideNoteValid = false;
    pitchSlideBaseNote = 0;
    pitchSlideCurrentNote = 0;
    vibrato.reset();
    tremolo.reset();
  }

  template <class Runtime>
  void emitTempo(Runtime& rt, u8 rawTempo) {
    if (rt.context.minorVersion == AKAOSNES_V4_FM || rt.context.minorVersion == AKAOSNES_V4_CT) {
      rawTempo = static_cast<u8>(rawTempo + ((rawTempo * 0x14) >> 8));
    }
    tempo = rawTempo;
    if (initialSharedTempo && rawTempo == *initialSharedTempo) {
      initialSharedTempoApplied = true;
    }
    rt.tempo(tempoMicrosecondsPerQuarter(rt.context.version, rt.context.minorVersion, rawTempo));
    syncLfoRateAndDelay(rt, LfoTarget::Vibrato);
    syncLfoRateAndDelay(rt, LfoTarget::Tremolo);
  }

  template <class Runtime>
  void syncSharedTempoAtTick(Runtime& rt) {
    const std::optional<u8> sharedTempo = rt.sharedTempoAtTick();
    if (!sharedTempo) {
      return;
    }
    tempo = *sharedTempo;
    initialSharedTempoApplied = true;
    syncLfoRateAndDelay(rt, LfoTarget::Vibrato);
    syncLfoRateAndDelay(rt, LfoTarget::Tremolo);
  }

  template <class Runtime>
  void emitProgram(Runtime& rt, u8 program) {
    nonPercussionProgram = program;
    if (!percussion) {
      rt.instrument(0, program);
    }
  }

  template <class Runtime>
  void emitPitchBendRange(Runtime& rt, u16 cents) {
    const u16 range = std::max<u16>(kDefaultPitchBendRangeCents, cents);
    if (currentPitchBendRangeCents == range) {
      return;
    }
    rt.pitchBendRange(PitchBendRangePerformanceEvent{
        .cents = range,
    });
    currentPitchBendRangeCents = range;
  }

  template <class Runtime>
  void emitPitchBendSemitones(Runtime& rt, double semitones, s16 midiBendValue) {
    if (currentPitchBendValue == midiBendValue) {
      return;
    }
    rt.pitchBend(semitones);
    currentPitchBendValue = midiBendValue;
  }

  template <class Runtime>
  void emitPitchBendForCurrentPitch(Runtime& rt) {
    const s16 value = akaoSnesPitchBendValue(currentPitch, pitchBase, currentPitchBendRangeCents);
    emitPitchBendSemitones(rt, akaoSnesPitchCents(currentPitch, pitchBase) / 100.0, value);
  }

  [[nodiscard]] bool pitchBendAtRest() const {
    return currentPitchBendRangeCents == kDefaultPitchBendRangeCents && currentPitchBendValue == 0;
  }

  template <class Runtime>
  void resetPitchBendForNewNote(Runtime& rt) {
    pitchBaseValid = false;
    pitchSlideActive = false;
    pitchSlideStepsRemaining = 0;
    pitchSlideNoteValid = false;
    if (pitchBendAtRest()) {
      return;
    }
    if (akaoSnesSupportsPitchEnvelope(rt.context.version) && pitchEnvelope.enabled) {
      emitPitchBendSemitones(rt, 0.0, 0);
      return;
    }
    emitPitchBendRange(rt, kDefaultPitchBendRangeCents);
    emitPitchBendSemitones(rt, 0.0, 0);
  }

  void setPitchEnvelope(AkaoSnesVersion version, s8 semitones, u8 delay, u8 length) {
    if (semitones == 0 || length == 0) {
      clearPitchEnvelope();
      return;
    }
    pitchEnvelope.enabled = true;
    pitchEnvelope.semitones = semitones;
    pitchEnvelope.delay = delay;
    pitchEnvelope.length = length;
    pitchEnvelope.progressStep = akaoSnesPitchEnvelopeProgressStep(version, length);
  }

  void clearPitchEnvelope() { pitchEnvelope = {}; }

  template <class Runtime>
  void beginPitchEnvelopeForNote(Runtime& rt) {
    if (!akaoSnesSupportsPitchEnvelope(rt.context.version) || !pitchEnvelope.enabled || !pitchBaseValid) {
      return;
    }
    const s32 targetPitch = akaoSnesPitchForSemitoneOffset(pitchEnvelope.semitones);
    const s32 rawDiff = (targetPitch - pitchBase) / kPitchFractionScale;
    const s32 rawMagnitude = rawDiff < 0 ? -rawDiff : rawDiff;
    const s32 signedMagnitude = pitchEnvelope.semitones < 0 ? -rawMagnitude : rawMagnitude;
    pitchEnvelope.targetOffset = signedMagnitude * kPitchFractionScale;
    pitchEnvelope.activeDelay = pitchEnvelope.delay;
    pitchEnvelope.activeCount = rt.context.version == AKAOSNES_V1 ? pitchEnvelope.length : 0;
    pitchEnvelope.progress = 0;
    pitchEnvelope.active = pitchEnvelope.targetOffset != 0 && pitchEnvelope.progressStep != 0;
    currentPitch = pitchBase;

    if (pitchEnvelope.active) {
      emitPitchBendRange(rt, akaoSnesPitchBendRangeCents(pitchBase, pitchBase + pitchEnvelope.targetOffset,
                                                         kDefaultPitchBendRangeCents));
    }
  }

  template <class Runtime>
  void beginNotePitch(Runtime& rt, u8 note, bool validForPitchBend) {
    resetPitchBendForNewNote(rt);
    if (!validForPitchBend) {
      return;
    }
    pitchBase = kNominalDspPitch * kPitchFractionScale;
    currentPitch = pitchBase;
    pitchBaseValid = true;
    pitchSlideBaseNote = akaoSnesCorrectedNote(note, transpose);
    pitchSlideCurrentNote = pitchSlideBaseNote;
    pitchSlideNoteValid = true;
    beginPitchEnvelopeForNote(rt);
  }

  void setPendingPitchSlide(u16 steps, s8 semitones) {
    pendingPitchSlideSteps = steps;
    pendingPitchSlideSemitones = semitones;
    if (pendingPitchSlideSemitones == 0) {
      clearPendingPitchSlide();
    }
  }

  void clearPendingPitchSlide() {
    pendingPitchSlideSteps = 0;
    pendingPitchSlideSemitones = 0;
  }

  template <class Runtime>
  void updatePitchSlide(Runtime& rt) {
    if (!pitchSlideActive || !pitchBaseValid) {
      return;
    }
    if (pitchSlideStepsRemaining == 0) {
      pitchSlideActive = false;
      return;
    }

    --pitchSlideStepsRemaining;
    currentPitch = pitchSlideStepsRemaining == 0 ? pitchSlideFinalPitch : currentPitch + pitchSlideStep;
    emitPitchBendForCurrentPitch(rt);
    if (pitchSlideStepsRemaining == 0) {
      pitchSlideActive = false;
    }
  }

  template <class Runtime>
  void beginPendingPitchSlide(Runtime& rt) {
    if (pendingPitchSlideSteps == 0 || pendingPitchSlideSemitones == 0) {
      clearPendingPitchSlide();
      return;
    }

    const u16 steps = pendingPitchSlideSteps;
    const s8 semitones = pendingPitchSlideSemitones;
    clearPendingPitchSlide();

    if (!pitchBaseValid || !pitchSlideNoteValid) {
      return;
    }

    pitchSlideCurrentNote = static_cast<s16>(pitchSlideCurrentNote + semitones);
    const s32 targetPitch = akaoSnesPitchForSemitoneOffset(pitchSlideCurrentNote - pitchSlideBaseNote);
    pitchSlideStep = akaoSnesPitchSlideStep(rt.context.version, currentPitch, targetPitch, steps);
    pitchSlideFinalPitch = currentPitch + (pitchSlideStep * static_cast<s32>(steps));
    const u16 rangeCents =
        std::max(akaoSnesPitchBendRangeCents(pitchBase, targetPitch, kDefaultPitchBendRangeCents),
                 akaoSnesPitchBendRangeCents(pitchBase, pitchSlideFinalPitch, kDefaultPitchBendRangeCents));
    emitPitchBendRange(rt, rangeCents);
    emitPitchBendForCurrentPitch(rt);
    pitchSlideStepsRemaining = steps;
    pitchSlideActive = true;
    updatePitchSlide(rt);
  }

  template <class Runtime>
  void setPitchWaitBoundary(Runtime& rt, VmCommandCursor& cmd, u32 waitTicks) {
    pitchWaitEndTick = rt.tick() + waitTicks;
    pitchWaitFallthrough = cmd.addressAtCursor();
    pitchWaitBoundaryClassified = false;
    pitchWaitStopsPitchEnvelope = false;
    if constexpr (requires(const Runtime& runtime, const VmCommandCursor& cursor) { runtime.nextCommand(cursor); }) {
      pitchWaitBoundaryClassified = true;
      if (const SourceCommand* next = rt.nextCommand(cmd)) {
        const EventType nextType = eventType(rt.context.version, rt.context.minorVersion, next->opcode);
        pitchWaitStopsPitchEnvelope = nextType == EventType::End;
        if (!pitchWaitStopsPitchEnvelope && nextType == EventType::PitchEnvelopeOff) {
          if (const SourceCommand* after = rt.commandAfter(*next)) {
            const EventType afterType = eventType(rt.context.version, rt.context.minorVersion, after->opcode);
            if (afterType == EventType::End) {
              pitchWaitStopsPitchEnvelope = true;
            } else if (afterType == EventType::Goto) {
              const auto bytes = rt.commandBytes(*after);
              if (bytes.size() >= 3) {
                const u16 destination = static_cast<u16>(bytes[1] | (bytes[2] << 8));
                pitchWaitStopsPitchEnvelope = destination <= rt.commandAddress();
              }
            }
          }
        }
      }
    }
  }

  [[nodiscard]] bool pitchEnvelopeDelayElapsed() {
    if (pitchEnvelope.activeDelay > 1) {
      --pitchEnvelope.activeDelay;
      return false;
    }
    if (pitchEnvelope.activeDelay == 1) {
      pitchEnvelope.activeDelay = 0;
    }
    return true;
  }

  [[nodiscard]] bool advancePitchEnvelopeTick(AkaoSnesVersion version, s32& currentOffset) {
    if (version == AKAOSNES_V1 && pitchEnvelope.activeCount == 0) {
      pitchEnvelope.active = false;
      return false;
    }
    if (version == AKAOSNES_V1) {
      --pitchEnvelope.activeCount;
      pitchEnvelope.progress += pitchEnvelope.progressStep;
      currentOffset =
          akaoSnesPitchEnvelopeOffset(pitchEnvelope.targetOffset, static_cast<u8>(pitchEnvelope.progress >> 8));
      if (pitchEnvelope.activeCount == 0) {
        pitchEnvelope.active = false;
      }
      return true;
    }

    if (pitchEnvelope.progress + pitchEnvelope.progressStep > 0xffff) {
      currentOffset = pitchEnvelope.targetOffset;
      pitchEnvelope.progress = 0x10000;
      pitchEnvelope.active = false;
      return true;
    }

    pitchEnvelope.progress += pitchEnvelope.progressStep;
    currentOffset =
        akaoSnesPitchEnvelopeOffset(pitchEnvelope.targetOffset, static_cast<u8>(pitchEnvelope.progress >> 8));
    return true;
  }

  template <class Runtime>
  void updatePitchEnvelope(Runtime& rt) {
    if (!akaoSnesSupportsPitchEnvelope(rt.context.version) || !pitchEnvelope.active || !pitchBaseValid) {
      return;
    }
    if (rt.terminalPitchWaitBoundary()) {
      return;
    }
    if (!pitchEnvelopeDelayElapsed()) {
      return;
    }
    s32 currentOffset = 0;
    if (!advancePitchEnvelopeTick(rt.context.version, currentOffset)) {
      return;
    }
    currentPitch = pitchBase + currentOffset;
    emitPitchBendForCurrentPitch(rt);
  }

  void configureVibratoFade(AkaoSnesVersion version) {
    if (!isLfoActive(version, vibrato.rate(), vibrato.depth())) {
      vibrato.clearReusableFade();
      return;
    }
    if (version == AKAOSNES_V1) {
      vibrato.setReusableFadeToConfiguredDepth(v1VibratoRampTicks(vibrato.rate(), tempo), 8);
      return;
    }
    if (version == AKAOSNES_V3 && vibrato.delay() != 0) {
      vibrato.setReusableFadeToConfiguredDepth(v3LfoRampTicks(vibrato.rate(), tempo), 8);
      return;
    }
    if (version == AKAOSNES_V4 && vibrato.delay() != 0) {
      const u32 ticks = v4VibratoRampTicks(vibrato.rate(), tempo);
      const s32 targetDepth = vibrato.configuredDepth(8);
      const s32 initialDepth = targetDepth / 4;
      const s32 step = ticks == 0 ? 0 : (targetDepth - initialDepth) / static_cast<s32>(ticks);
      vibrato.setReusableFade(ticks, step);
      return;
    }
    vibrato.clearReusableFade();
  }

  void configureTremoloFade(AkaoSnesVersion version) {
    if (version != AKAOSNES_V3 || !isLfoActive(version, tremolo.rate(), tremolo.depth()) || tremolo.delay() == 0) {
      tremolo.clearReusableFade();
      return;
    }
    tremolo.setReusableFadeToConfiguredDepth(v3LfoRampTicks(tremolo.rate(), tempo), 8);
  }

  [[nodiscard]] u8 vibratoFadeDepthMidiValue(AkaoSnesVersion version, s32 depth) const {
    const s32 targetDepth = vibrato.configuredDepth(8);
    if (targetDepth <= 0) {
      return 0;
    }
    const int fullDepth = vibratoDepthMidiValue(version, vibrato.rate(), vibrato.depth());
    return static_cast<u8>(std::clamp<int>((fullDepth * depth + (targetDepth / 2)) / targetDepth, 0, 127));
  }

  [[nodiscard]] u8 tremoloFadeDepthMidiValue(AkaoSnesVersion version, s32 depth) const {
    const s32 targetDepth = tremolo.configuredDepth(8);
    if (targetDepth <= 0) {
      return 0;
    }
    const int fullDepth = tremoloDepthMidiValue(version, tremolo.rate(), tremolo.depth());
    return static_cast<u8>(std::clamp<int>((fullDepth * depth + (targetDepth / 2)) / targetDepth, 0, 127));
  }

  template <class Runtime>
  void emitVibratoDepth(Runtime& rt, u8 midiDepth, bool force = false) {
    vibrato.emitDepth(
        midiDepth,
        [&](u8 outputDepth) {
          const double amount = static_cast<double>(outputDepth) / 127.0;
          rt.modulation(ModulationPerformanceEvent{
              .target = ModulationPerformanceTarget::VibratoDepth,
              .amount = amount,
              .pitchDepthSemitones = (amount * maxVibratoDepthCents(rt.context.version)) / 100.0,
              .controllerRangeMaxAmount = 1.0,
          });
        },
        force);
  }

  template <class Runtime>
  void emitTremoloDepth(Runtime& rt, u8 midiDepth, bool force = false) {
    tremolo.emitDepth(
        midiDepth,
        [&](u8 outputDepth) {
          rt.modulation(ModulationPerformanceEvent{
              .target = ModulationPerformanceTarget::TremoloDepth,
              .amount = static_cast<double>(outputDepth) / 127.0,
              .controllerRangeMaxAmount = 1.0,
          });
        },
        force);
  }

  template <class Runtime>
  void setLfoOutputDepth(Runtime& rt, LfoTarget target, u8 depth, bool force = false) {
    if (target == LfoTarget::Vibrato) {
      emitVibratoDepth(rt, depth, force);
    } else {
      emitTremoloDepth(rt, depth, force);
    }
  }

  template <class Runtime>
  void clearLfoRateAndDelay(Runtime& rt, LfoTarget target) {
    if (target == LfoTarget::Vibrato) {
      rt.modulation(ModulationPerformanceEvent{
          .target = ModulationPerformanceTarget::VibratoRate,
          .amount = 0.0,
          .controllerRangeMaxAmount = 1.0,
      });
      rt.vibratoDelay(0, 0);
    } else {
      rt.modulation(ModulationPerformanceEvent{
          .target = ModulationPerformanceTarget::TremoloRate,
          .amount = 0.0,
          .controllerRangeMaxAmount = 1.0,
      });
      rt.tremoloDelay(0, 0);
    }
  }

  template <class Runtime>
  void syncLfoRateAndDelay(Runtime& rt, LfoTarget target) {
    const bool isVibrato = target == LfoTarget::Vibrato;
    LfoState& lfo = isVibrato ? vibrato : tremolo;
    if (!isLfoActive(rt.context.version, lfo.rate(), lfo.depth())) {
      return;
    }
    if (isVibrato) {
      configureVibratoFade(rt.context.version);
    } else {
      configureTremoloFade(rt.context.version);
    }
    const u8 rateValue = rateMidiValue(rt.context.version, lfo.rate(), lfo.depth(),
                                       akaoSnesTimer0Frequency(rt.context.version, rt.context.minorVersion));
    if (isVibrato) {
      rt.modulation(ModulationPerformanceEvent{
          .target = ModulationPerformanceTarget::VibratoRate,
          .amount = static_cast<double>(rateValue) / 127.0,
          .frequencyHz = lfoRateHz(rt.context.version, lfo.rate(), lfo.depth(),
                                   akaoSnesTimer0Frequency(rt.context.version, rt.context.minorVersion)),
          .controllerRangeMaxAmount = 1.0,
      });
      rt.vibratoDelay(lfoDelayTicks(rt.context.version, lfo.delay()),
                      delayMidiValue(rt.context.version, lfo.delay(), tempo,
                                     akaoSnesTimer0Frequency(rt.context.version, rt.context.minorVersion)));
    } else {
      rt.modulation(ModulationPerformanceEvent{
          .target = ModulationPerformanceTarget::TremoloRate,
          .amount = static_cast<double>(rateValue) / 127.0,
          .frequencyHz = lfoRateHz(rt.context.version, lfo.rate(), lfo.depth(),
                                   akaoSnesTimer0Frequency(rt.context.version, rt.context.minorVersion)),
          .controllerRangeMaxAmount = 1.0,
      });
      rt.tremoloDelay(lfoDelayTicks(rt.context.version, lfo.delay()),
                      delayMidiValue(rt.context.version, lfo.delay(), tempo,
                                     akaoSnesTimer0Frequency(rt.context.version, rt.context.minorVersion)));
    }
  }

  template <class Runtime>
  void applyLfo(Runtime& rt, LfoTarget target, const LfoParams& params) {
    const bool isVibrato = target == LfoTarget::Vibrato;
    const bool supported = isVibrato || exportsTremolo(rt.context.version);
    const bool active = supported && isLfoActive(rt.context.version, params.rate, params.depth);
    LfoState& lfo = isVibrato ? vibrato : tremolo;
    lfo.configure(params.delay, params.rate, params.depth);
    if (initialSharedTempo && !initialSharedTempoApplied && lfoAfterInitialSharedTempoTrack) {
      tempo = *initialSharedTempo;
      initialSharedTempoApplied = true;
    }
    if (isVibrato) {
      configureVibratoFade(rt.context.version);
    } else {
      configureTremoloFade(rt.context.version);
    }
    u8 midiDepth = 0;
    if (active) {
      midiDepth = isVibrato ? vibratoDepthMidiValue(rt.context.version, params.rate, params.depth)
                            : tremoloDepthMidiValue(rt.context.version, params.rate, params.depth, params.delay);
    }
    if (isVibrato && rt.context.version == AKAOSNES_V4 && active && vibrato.hasReusableFade()) {
      const u32 delay = lfoDelayTicks(rt.context.version, vibrato.delay());
      const s32 initialDepth = vibrato.configuredDepth(8) / 4;
      vibrato.beginReusableFade(delay, vibrato.configuredDepth(8), initialDepth);
      midiDepth = delay == 0 ? vibratoFadeDepthMidiValue(rt.context.version, initialDepth) : 0;
    }
    setLfoOutputDepth(rt, target, midiDepth, true);
    if (active) {
      syncLfoRateAndDelay(rt, target);
      if (rt.tick() == 0 && initialSharedTempo && lfoBeforeInitialSharedTempoTrack && *initialSharedTempo != tempo) {
        tempo = *initialSharedTempo;
        initialSharedTempoApplied = true;
        syncLfoRateAndDelay(rt, target);
      }
    } else {
      clearLfoRateAndDelay(rt, target);
    }
  }

  template <class Runtime>
  void clearLfo(Runtime& rt, LfoTarget target) {
    LfoState& lfo = target == LfoTarget::Vibrato ? vibrato : tremolo;
    lfo.setDepth(0);
    lfo.clearReusableFade();
    setLfoOutputDepth(rt, target, 0, true);
  }

  template <class Runtime>
  void beginVibratoForNote(Runtime& rt) {
    if (rt.context.version == AKAOSNES_V2 || !vibrato.hasReusableFade() ||
        !isLfoActive(rt.context.version, vibrato.rate(), vibrato.depth())) {
      return;
    }
    const u32 delay = lfoDelayTicks(rt.context.version, vibrato.delay());
    const s32 initialDepth = rt.context.version == AKAOSNES_V4 ? vibrato.configuredDepth(8) / 4 : 0;
    vibrato.beginReusableFade(delay, vibrato.configuredDepth(8), initialDepth);
    emitVibratoDepth(rt, delay == 0 ? vibratoFadeDepthMidiValue(rt.context.version, initialDepth) : 0, true);
  }

  template <class Runtime>
  void beginTremoloForNote(Runtime& rt) {
    if (rt.context.version != AKAOSNES_V3 || !tremolo.hasReusableFade()) {
      return;
    }
    tremolo.beginReusableFadeToConfiguredDepth(8);
    emitTremoloDepth(rt, 0, true);
  }

  template <class Runtime>
  void updateVibratoFade(Runtime& rt) {
    if (rt.context.version == AKAOSNES_V2 || !vibrato.fadeActive()) {
      return;
    }
    const auto fadeTick = vibrato.tickFade();
    if (fadeTick.status != SequenceMotionStatus::Inactive && fadeTick.status != SequenceMotionStatus::Delayed) {
      const s32 current = vibrato.clampToConfiguredDepth(fadeTick.current, 8);
      vibrato.setCurrentDepthPreservingMotion(current);
      emitVibratoDepth(rt, vibratoFadeDepthMidiValue(rt.context.version, current));
    }
  }

  template <class Runtime>
  void updateTremoloFade(Runtime& rt) {
    if (rt.context.version != AKAOSNES_V3 || !tremolo.fadeActive()) {
      return;
    }
    const auto fadeTick = tremolo.tickFade();
    if (fadeTick.status != SequenceMotionStatus::Inactive && fadeTick.status != SequenceMotionStatus::Delayed) {
      const s32 current = tremolo.clampToConfiguredDepth(fadeTick.current, 8);
      tremolo.setCurrentDepthPreservingMotion(current);
      emitTremoloDepth(rt, tremoloFadeDepthMidiValue(rt.context.version, current));
    }
  }

  u8 octave = 6;
  s8 transpose = 0;
  u8 onetimeDuration = 0;
  bool slur = false;
  bool legato = false;
  bool percussion = false;
  u8 nonPercussionProgram = 0;
  u8 loopLevel = 0;
  std::array<LoopFrame, 4> loops{};
  u8 tempo = kAkaoSnesDefaultTempo;
  bool pan8Bit = true;
  std::optional<u8> initialSharedTempo;
  std::optional<u32> initialSharedTempoTrack;
  bool lfoBeforeInitialSharedTempoTrack = false;
  bool lfoAfterInitialSharedTempoTrack = false;
  bool initialSharedTempoApplied = false;
  std::optional<u64> lastTieableNoteTick;
  std::optional<u64> pitchWaitEndTick;
  std::optional<Address> pitchWaitFallthrough;
  bool pitchWaitBoundaryClassified = false;
  bool pitchWaitStopsPitchEnvelope = false;
  bool sharedTempoCacheBuilt = false;
  std::vector<SharedTempoChange> sharedTempoChanges;
  size_t sharedTempoCursor = 0;
  PitchEnvelopeState pitchEnvelope;
  bool pitchBaseValid = false;
  s32 pitchBase = kNominalDspPitch * kPitchFractionScale;
  s32 currentPitch = kNominalDspPitch * kPitchFractionScale;
  u16 currentPitchBendRangeCents = kDefaultPitchBendRangeCents;
  s16 currentPitchBendValue = 0;
  u16 pendingPitchSlideSteps = 0;
  s8 pendingPitchSlideSemitones = 0;
  bool pitchSlideActive = false;
  u16 pitchSlideStepsRemaining = 0;
  s32 pitchSlideStep = 0;
  s32 pitchSlideFinalPitch = kNominalDspPitch * kPitchFractionScale;
  bool pitchSlideNoteValid = false;
  s16 pitchSlideBaseNote = 0;
  s16 pitchSlideCurrentNote = 0;
  LfoState vibrato;
  LfoState tremolo;
};

CommandFlow readUnknown(VmCommandCursor& cmd, u8 operandCount) {
  cmd.name("Unknown Event", SequenceSemantic::Unsupported).kind("unknown").sourceOnly();
  cmd.derived("opcode", cmd.opcode(), SourceValueDisplay::Hex);
  for (u8 i = 0; i < operandCount; ++i) {
    static_cast<void>(cmd.u8(fmt::format("arg{}", i + 1)));
  }
  return cmd.next();
}

template <class Runtime>
CommandFlow readLfo(Runtime& rt, VmCommandCursor& cmd, std::string_view name, LfoTarget target) {
  cmd.name(name, SequenceSemantic::Modulation);
  LfoParams params;
  if (rt.context.version == AKAOSNES_V2) {
    params.depth = cmd.u8("depth");
    params.delay = cmd.u8("delay");
    params.rate = cmd.u8("rate");
  } else {
    params.delay = cmd.u8("delay");
    params.rate = cmd.u8("rate");
    params.depth = cmd.u8("depth");
  }
  rt.state.applyLfo(rt, target, params);
  return cmd.next();
}

template <class Runtime>
u16 readRelocatedAddress(Runtime& rt, VmCommandCursor& cmd, std::string_view name) {
  const u16 raw = cmd.u16le(name);
  const u16 resolved = relocatedAddress(raw, rt.context.romRelocBase, rt.context.apuRelocBase);
  cmd.derived(std::string(name) + "_resolved", resolved, SourceValueDisplay::Address);
  return resolved;
}

struct AkaoSnesCursorReader {
  template <class Runtime>
  static CommandFlow read(Runtime& rt, VmCommandCursor& cmd) {
    const u8 opcode = cmd.opcode();
    const EventType type = eventType(rt.context.version, rt.context.minorVersion, opcode);
    auto& state = rt.state;

    switch (type) {
      case EventType::Unknown0:
        return readUnknown(cmd, 0);
      case EventType::Unknown1:
        return readUnknown(cmd, 1);
      case EventType::Unknown2:
        return readUnknown(cmd, 2);
      case EventType::Unknown3:
        return readUnknown(cmd, 3);
      case EventType::Unknown4:
        return readUnknown(cmd, 4);

      case EventType::Note: {
        cmd.name("Note", SequenceSemantic::Note);
        const u8 tableSize = akaoSnesNoteDurationTableSize(rt.context.version);
        const u8 durationIndex = opcode % tableSize;
        const u8 noteIndex = opcode / tableSize;
        cmd.derived("duration_index", durationIndex).derived("note_index", noteIndex);
        u8 length = noteDuration(rt.context.version, durationIndex);
        if (state.onetimeDuration != 0) {
          length = state.onetimeDuration;
          state.onetimeDuration = 0;
        }
        const u8 duration =
            (!state.slur && !state.legato) ? ((length > 2) ? static_cast<u8>(length - 2) : u8{1}) : length;
        if (noteIndex < 12) {
          const double velocity = kAkaoSnesNoteVelocity / 127.0;
          state.setPitchWaitBoundary(rt, cmd, length);
          const u8 note = static_cast<u8>((state.octave * 12) + noteIndex);
          state.beginNotePitch(rt, note, !state.percussion);
          state.beginPendingPitchSlide(rt);
          if (!state.slur && !state.legato) {
            state.beginVibratoForNote(rt);
            state.beginTremoloForNote(rt);
          }
          if (state.percussion) {
            rt.note(kAkaoSnesDrumKeyBias + noteIndex - state.transpose, velocity, duration);
          } else {
            rt.note((state.octave * 12) + noteIndex + state.transpose, velocity, duration);
          }
          state.lastTieableNoteTick = rt.tick() + length;
          return cmd.wait(length);
        }
        if (noteIndex == akaoSnesStatusNoteIndexTie(rt.context.version)) {
          state.setPitchWaitBoundary(rt, cmd, length);
          state.beginPendingPitchSlide(rt);
          if (state.lastTieableNoteTick && *state.lastTieableNoteTick >= rt.tick()) {
            rt.note(0.0, 1.0, duration, true);
            state.lastTieableNoteTick = rt.tick() + length;
          }
          return cmd.wait(length);
        }
        state.setPitchWaitBoundary(rt, cmd, length);
        cmd.name("Rest", SequenceSemantic::Rest);
        state.lastTieableNoteTick.reset();
        return cmd.wait(length);
      }

      case EventType::Nop:
        cmd.name("NOP", SequenceSemantic::Meta).noOp();
        return cmd.next();

      case EventType::Nop1:
        cmd.name("NOP", SequenceSemantic::Meta).noOp();
        static_cast<void>(cmd.u8("arg1"));
        return cmd.next();

      case EventType::Volume: {
        cmd.name("Volume", SequenceSemantic::Level);
        const u8 volume = static_cast<u8>(cmd.u8("volume") >> 1);
        rt.level(levelFromLegacyMidiVolume(volume));
        return cmd.next();
      }

      case EventType::VolumeFade: {
        cmd.name("Volume Fade", SequenceSemantic::Level);
        u16 fadeLength = 0;
        if (rt.context.version == AKAOSNES_V1) {
          fadeLength = cmd.u16le("length");
        } else {
          fadeLength = cmd.u8("length");
        }
        const u8 volume = static_cast<u8>(cmd.u8("volume") >> 1);
        if (fadeLength == 0) {
          rt.level(levelFromLegacyMidiVolume(volume));
        } else {
          cmd.sourceOnly();
        }
        return cmd.next();
      }

      case EventType::Pan: {
        cmd.name("Pan", SequenceSemantic::Pan);
        const u8 pan = static_cast<u8>(cmd.u8("pan") << (state.pan8Bit ? 0 : 1));
        rt.pan(stereoPositionFromRawPan(pan));
        return cmd.next();
      }

      case EventType::PanFade: {
        cmd.name("Pan Fade", SequenceSemantic::Pan);
        u16 fadeLength = 0;
        if (rt.context.version == AKAOSNES_V1) {
          fadeLength = cmd.u16le("length");
        } else {
          fadeLength = cmd.u8("length");
        }
        const u8 pan = static_cast<u8>(cmd.u8("pan") << (state.pan8Bit ? 0 : 1));
        if (fadeLength == 0) {
          rt.pan(stereoPositionFromRawPan(pan));
        } else {
          cmd.sourceOnly();
        }
        return cmd.next();
      }

      case EventType::PitchEnvelopeOn: {
        cmd.name("Pitch Envelope On", SequenceSemantic::Pitch);
        s8 semitones = 0;
        u8 delay = 0;
        u8 length = 0;
        if (rt.context.version == AKAOSNES_V1) {
          delay = cmd.u8("delay");
          length = cmd.u8("length");
          semitones = cmd.s8("semitones");
          state.setPitchEnvelope(rt.context.version, semitones, static_cast<u8>(delay + 1), length);
        } else {
          semitones = cmd.s8("semitones");
          delay = cmd.u8("delay");
          length = cmd.u8("length");
          state.setPitchEnvelope(rt.context.version, semitones, delay, length);
        }
        return cmd.next();
      }

      case EventType::PitchEnvelopeOff:
        cmd.name("Pitch Envelope Off", SequenceSemantic::Pitch);
        state.clearPitchEnvelope();
        return cmd.next();

      case EventType::PitchSlide:
        cmd.name("Pitch Slide", SequenceSemantic::Pitch);
        state.setPendingPitchSlide(static_cast<u16>(cmd.u8("time")) + 1, cmd.s8("semitones"));
        return cmd.next();

      case EventType::VibratoOn:
        return readLfo(rt, cmd, "Vibrato", LfoTarget::Vibrato);

      case EventType::VibratoOff:
        cmd.name("Vibrato Off", SequenceSemantic::Modulation);
        state.clearLfo(rt, LfoTarget::Vibrato);
        return cmd.next();

      case EventType::TremoloOn:
        return readLfo(rt, cmd, "Tremolo", LfoTarget::Tremolo);

      case EventType::TremoloOff:
        cmd.name("Tremolo Off", SequenceSemantic::Modulation);
        state.clearLfo(rt, LfoTarget::Tremolo);
        return cmd.next();

      case EventType::PanLfoOn:
        cmd.name("Pan LFO", SequenceSemantic::Modulation).sourceOnly();
        static_cast<void>(cmd.u8("depth"));
        static_cast<void>(cmd.u8("rate"));
        return cmd.next();

      case EventType::PanLfoOnWithDelay:
        cmd.name("Pan LFO", SequenceSemantic::Modulation).sourceOnly();
        static_cast<void>(cmd.u8("delay"));
        static_cast<void>(cmd.u8("rate"));
        static_cast<void>(cmd.u8("depth"));
        return cmd.next();

      case EventType::PanLfoOff:
      case EventType::NoiseOn:
      case EventType::NoiseOff:
      case EventType::PitchModOn:
      case EventType::PitchModOff:
      case EventType::EchoOn:
      case EventType::EchoOff:
      case EventType::AdsrDefault:
      case EventType::SlurOn:
      case EventType::SlurOff:
      case EventType::LegatoOn:
      case EventType::LegatoOff:
      case EventType::IncCpuSharedCounter:
      case EventType::ZeroCpuSharedCounter:
      case EventType::IgnoreMasterVolume:
      case EventType::IgnoreMasterVolumeBroken:
      case EventType::LoopRestart:
        cmd.name("State Change", SequenceSemantic::Meta).sourceOnly();
        if (type == EventType::SlurOn) {
          state.slur = true;
        } else if (type == EventType::SlurOff) {
          state.slur = false;
        } else if (type == EventType::LegatoOn) {
          state.legato = true;
        } else if (type == EventType::LegatoOff) {
          state.legato = false;
        }
        return cmd.next();

      case EventType::NoiseFreq:
        cmd.name("Noise Frequency", SequenceSemantic::Meta).sourceOnly();
        static_cast<void>(cmd.u8("frequency"));
        return cmd.next();

      case EventType::Octave:
        cmd.name("Octave", SequenceSemantic::Pitch);
        state.octave = cmd.u8("octave");
        return cmd.next();

      case EventType::OctaveUp:
        cmd.name("Octave Up", SequenceSemantic::Pitch);
        ++state.octave;
        return cmd.next();

      case EventType::OctaveDown:
        cmd.name("Octave Down", SequenceSemantic::Pitch);
        --state.octave;
        return cmd.next();

      case EventType::TransposeAbs:
        cmd.name("Transpose", SequenceSemantic::Pitch);
        state.transpose = cmd.s8("semitones");
        return cmd.next();

      case EventType::TransposeRel:
        cmd.name("Transpose Relative", SequenceSemantic::Pitch);
        state.transpose = static_cast<s8>(state.transpose + static_cast<s8>(cmd.s8("semitones")));
        return cmd.next();

      case EventType::Tuning:
        cmd.name("Tuning", SequenceSemantic::Pitch);
        rt.tuning(tuningCents(cmd.u8("tuning")));
        return cmd.next();

      case EventType::ProgramChange: {
        cmd.name("Program", SequenceSemantic::Program);
        const u8 program = cmd.u8("program");
        cmd.instrumentRef(0, program);
        state.emitProgram(rt, program);
        return cmd.next();
      }

      case EventType::VolumeEnvelope:
        cmd.name("Volume Envelope", SequenceSemantic::Meta).sourceOnly();
        static_cast<void>(cmd.u8("envelope"));
        return cmd.next();

      case EventType::GainRelease:
        cmd.name("Gain Release", SequenceSemantic::Meta).sourceOnly();
        static_cast<void>(cmd.u8("gain"));
        return cmd.next();

      case EventType::DurationRate:
        cmd.name("Duration Rate", SequenceSemantic::Meta).sourceOnly();
        static_cast<void>(cmd.u8("rate"));
        return cmd.next();

      case EventType::AdsrAr:
      case EventType::AdsrDr:
      case EventType::AdsrSl:
      case EventType::AdsrSr:
        cmd.name("ADSR", SequenceSemantic::Meta).sourceOnly();
        static_cast<void>(cmd.u8("value"));
        return cmd.next();

      case EventType::LoopStart: {
        cmd.name("Loop Start", SequenceSemantic::Loop);
        const u8 count = cmd.u8("count");
        const u32 totalPlays = count == 0 ? 0u : static_cast<u32>(count + 1);
        const u8 slot = state.loopLevel % state.loops.size();
        state.loops[slot] = LoopFrame{
            .start = cmd.addressAtCursor(),
            .totalPlays = totalPlays,
            .remainingPlays = totalPlays,
            .incrementCount = rt.context.version == AKAOSNES_V4 ? u8{1} : u8{0},
        };
        state.loopLevel = static_cast<u8>((state.loopLevel + 1) % state.loops.size());
        return cmd.next();
      }

      case EventType::LoopEnd: {
        cmd.name("Loop End", SequenceSemantic::Repeat);
        const u8 slot = (state.loopLevel == 0 ? static_cast<u8>(state.loops.size()) : state.loopLevel) - 1;
        LoopFrame& frame = state.loops[slot];
        if (rt.context.version == AKAOSNES_V4) {
          ++frame.incrementCount;
        }
        if (frame.totalPlays == 0) {
          return cmd.declaredLoop(frame.start);
        }
        const auto flow = rt.countedRepeatUntil(cmd, slot, frame.totalPlays, frame.start);
        if (flow.fallsThrough()) {
          state.loopLevel = slot;
          frame.remainingPlays = 1;
        } else if (frame.remainingPlays > 1) {
          --frame.remainingPlays;
        }
        return flow;
      }

      case EventType::OneTimeDuration:
        cmd.name("Duration One-Time", SequenceSemantic::Meta);
        state.onetimeDuration = cmd.u8("duration");
        return cmd.next();

      case EventType::JumpToSfxLo:
      case EventType::JumpToSfxHi:
        cmd.name("Jump To SFX", SequenceSemantic::Unsupported).sourceOnly();
        static_cast<void>(cmd.u8("sfx"));
        return cmd.stop();

      case EventType::End:
        cmd.name("End", SequenceSemantic::End);
        return cmd.end();

      case EventType::Tempo:
        cmd.name("Tempo", SequenceSemantic::Tempo);
        state.emitTempo(rt, cmd.u8("tempo"));
        return cmd.next();

      case EventType::TempoFade: {
        cmd.name("Tempo Fade", SequenceSemantic::Tempo);
        u16 fadeLength = 0;
        if (rt.context.version == AKAOSNES_V1) {
          fadeLength = cmd.u16le("length");
        } else {
          fadeLength = cmd.u8("length");
        }
        const u8 tempo = cmd.u8("tempo");
        if (fadeLength == 0) {
          state.emitTempo(rt, tempo);
        } else {
          cmd.sourceOnly();
        }
        return cmd.next();
      }

      case EventType::EchoVolume:
        cmd.name("Echo Volume", SequenceSemantic::Meta).sourceOnly();
        static_cast<void>(cmd.u8("volume"));
        return cmd.next();

      case EventType::EchoVolumeFade:
        cmd.name("Echo Volume Fade", SequenceSemantic::Meta).sourceOnly();
        static_cast<void>(cmd.u8("length"));
        static_cast<void>(cmd.u8("volume"));
        return cmd.next();

      case EventType::EchoFeedbackFir:
        cmd.name("Echo Feedback/FIR", SequenceSemantic::Meta).sourceOnly();
        static_cast<void>(cmd.u8("feedback"));
        static_cast<void>(cmd.u8("fir"));
        return cmd.next();

      case EventType::MasterVolume:
        cmd.name("Master Volume", SequenceSemantic::Level);
        rt.masterLevel(levelFromLegacyMidiVolume(static_cast<u8>(cmd.u8("volume") >> 1)));
        return cmd.next();

      case EventType::LoopBreak: {
        cmd.name("Loop Break", SequenceSemantic::RepeatBreak);
        const u8 count = cmd.u8("count");
        const Address destination{readRelocatedAddress(rt, cmd, "destination")};
        cmd.target(destination, SourceLinkRole::JumpTarget);
        const u8 slot = (state.loopLevel == 0 ? static_cast<u8>(state.loops.size()) : state.loopLevel) - 1;
        LoopFrame& frame = state.loops[slot];
        if (rt.context.version != AKAOSNES_V4) {
          ++frame.incrementCount;
        }
        const bool taken = count == frame.incrementCount;
        if (taken) {
          if (rt.context.version == AKAOSNES_V1) {
            if (frame.remainingPlays != 0) {
              --frame.remainingPlays;
              if (frame.remainingPlays == 0) {
                state.loopLevel = slot;
              }
            }
          } else if (rt.context.version != AKAOSNES_V2 && rt.context.version != AKAOSNES_V3) {
            if (frame.remainingPlays <= 1) {
              state.loopLevel = slot;
            }
          }
          rt.finishRepeat(slot);
        }
        return rt.conditionalFiniteBranch(cmd, destination, taken);
      }

      case EventType::Goto: {
        cmd.name("Jump", SequenceSemantic::Jump);
        return cmd.loopCandidate(Address{readRelocatedAddress(rt, cmd, "destination")});
      }

      case EventType::EchoFeedbackFade:
      case EventType::EchoFirFade:
      case EventType::EchoFeedback:
      case EventType::EchoFir:
        cmd.name("Echo", SequenceSemantic::Meta).sourceOnly();
        static_cast<void>(cmd.u8("value"));
        if (type == EventType::EchoFeedbackFade || type == EventType::EchoFirFade) {
          static_cast<void>(cmd.u8("target"));
        }
        return cmd.next();

      case EventType::CpuControlledSetValue:
        cmd.name("CPU-Controlled Set Value", SequenceSemantic::Meta).sourceOnly();
        static_cast<void>(cmd.u8("value"));
        return cmd.next();

      case EventType::CpuControlledJump: {
        cmd.name("CPU-Controlled Jump", SequenceSemantic::Jump);
        const Address destination{readRelocatedAddress(rt, cmd, "destination")};
        return cmd.conditionalBranch(destination);
      }

      case EventType::CpuControlledJumpV2:
        cmd.name("CPU-Controlled Jump", SequenceSemantic::Jump).sourceOnly();
        static_cast<void>(cmd.u8("arg") & 0x0f);
        cmd.target(Address{readRelocatedAddress(rt, cmd, "destination")}, SourceLinkRole::JumpTarget);
        return cmd.next();

      case EventType::PercOn:
        cmd.name("Percussion On", SequenceSemantic::Program);
        state.percussion = true;
        rt.instrument(kAkaoSnesDrumKitBank << 7, kAkaoSnesDrumKitProgram);
        return cmd.next();

      case EventType::PercOff:
        cmd.name("Percussion Off", SequenceSemantic::Program);
        state.percussion = false;
        rt.instrument(0, state.nonPercussionProgram);
        return cmd.next();

      case EventType::VolumeAlt:
        cmd.name("Expression", SequenceSemantic::Level);
        rt.expression(levelFromLegacyMidiVolume(cmd.u8("volume") & 0x7f));
        return cmd.next();

      case EventType::IgnoreMasterVolumeByPrognum:
        cmd.name("Ignore Master Volume By Program", SequenceSemantic::Meta).sourceOnly();
        static_cast<void>(cmd.u8("program"));
        return cmd.next();

      case EventType::PlaySfx:
        cmd.name("Play SFX", SequenceSemantic::Unsupported).sourceOnly();
        static_cast<void>(cmd.u8("arg"));
        return cmd.next();
    }

    return cmd.end();
  }
};

void tickAkaoSnesTrack(const SourceCommand&, const TrackProgram& track, std::any& trackState, PerformanceEmitter& out,
                       VmApi& vm, const std::any& context) {
  auto& state = std::any_cast<TrackState&>(trackState);
  const auto& typedContext = std::any_cast<const Context&>(context);
  struct TickRuntime {
    TrackState& state;
    PerformanceEmitter& out;
    VmApi& vm;
    const TrackProgram& track;
    const Context& context;

    [[nodiscard]] u64 tick() const noexcept { return vm.tick(); }
    [[nodiscard]] bool terminalPitchWaitBoundary() const {
      if (!state.pitchWaitEndTick || !state.pitchWaitFallthrough || vm.tick() != *state.pitchWaitEndTick) {
        return false;
      }
      if (state.pitchWaitBoundaryClassified) {
        return state.pitchWaitStopsPitchEnvelope;
      }
      if (state.pitchWaitStopsPitchEnvelope) {
        return true;
      }
      const auto index = track.addressIndex.find(*state.pitchWaitFallthrough);
      if (!index) {
        return false;
      }
      const EventType nextType = eventType(context.version, context.minorVersion, track.commands.at(*index).opcode);
      return nextType == EventType::End;
    }
    [[nodiscard]] std::optional<u8> sharedTempoAtTick() {
      if (!state.sharedTempoCacheBuilt) {
        for (const PerformanceTrack& renderedTrack : vm.sequence().tracks) {
          for (const PerformanceEvent& event : renderedTrack.events) {
            if (const auto* tempoEvent = std::get_if<TempoPerformanceEvent>(&event)) {
              state.sharedTempoChanges.push_back(SharedTempoChange{
                  .tick = tempoEvent->header.tick,
                  .tempo = tempoFromMicrosecondsPerQuarter(context.version, context.minorVersion,
                                                           tempoEvent->microsecondsPerQuarter),
                  .sourceTrackNumber = renderedTrack.sourceTrackNumber,
              });
            }
          }
        }
        std::ranges::stable_sort(state.sharedTempoChanges, {}, &SharedTempoChange::tick);
        state.sharedTempoCacheBuilt = true;
      }

      while (state.sharedTempoCursor < state.sharedTempoChanges.size() &&
             state.sharedTempoChanges[state.sharedTempoCursor].tick < vm.tick()) {
        ++state.sharedTempoCursor;
      }
      std::optional<u8> tempo;
      size_t cursor = state.sharedTempoCursor;
      while (cursor < state.sharedTempoChanges.size() && state.sharedTempoChanges[cursor].tick == vm.tick()) {
        if (state.sharedTempoChanges[cursor].sourceTrackNumber != track.sourceTrackNumber) {
          tempo = state.sharedTempoChanges[cursor].tempo;
        }
        ++cursor;
      }
      return tempo;
    }
    void modulation(ModulationPerformanceEvent event) { out.modulation(std::move(event)); }
    void modulation(ModulationPerformanceTarget target, double amount) { out.modulation(target, amount); }
    void vibratoDelay(u32 delayTicks, u8 midiValue) { out.vibratoDelay(delayTicks, midiValue); }
    void tremoloDelay(u32 delayTicks, u8 midiValue) { out.tremoloDelay(delayTicks, midiValue); }
    void pitchBend(double semitones) { out.pitchBend(semitones); }
    void pitchBendRange(PitchBendRangePerformanceEvent event) { out.pitchBendRange(std::move(event)); }
  } rt{state, out, vm, track, typedContext};

  state.syncSharedTempoAtTick(rt);
  if (rt.terminalPitchWaitBoundary()) {
    return;
  }
  state.updateVibratoFade(rt);
  state.updateTremoloFade(rt);
  state.updatePitchSlide(rt);
  state.updatePitchEnvelope(rt);
}

[[nodiscard]] std::string dialectId(AkaoSnesVersion version, AkaoSnesMinorVersion minorVersion) {
  return fmt::format("akao-snes:{}:{}", akaoSnesVersionName(version), akaoSnesMinorVersionName(minorVersion));
}

[[nodiscard]] SequenceDialect makeDialect(AkaoSnesVersion version, AkaoSnesMinorVersion minorVersion,
                                          u32 romRelocBase = 0, u32 apuRelocBase = 0,
                                          std::optional<InitialSharedTempoHint> initialSharedTempo = std::nullopt) {
  SequenceDialect dialect = makeCursorDialect<TrackState, Context, AkaoSnesCursorReader>(CursorDialectSpec<Context>{
      .id = dialectId(version, minorVersion),
      .commandDetailKindPrefix = "akao-snes",
      .timebase = Timebase{.ppqn = kAkaoSnesPpqn},
      .defaultBehavior =
          SequenceProgramBehavior{
              .defaultLoopPolicy = LoopPolicy::PlayOnce,
              .initialReverbSend = 0.0,
              .stopAllTracksAtFirstLoop = false,
          },
      .context =
          Context{
              .version = version,
              .minorVersion = minorVersion,
              .romRelocBase = romRelocBase,
              .apuRelocBase = apuRelocBase,
              .initialSharedTempo = initialSharedTempo ? std::optional<u8>(initialSharedTempo->tempo) : std::nullopt,
              .initialSharedTempoTrack =
                  initialSharedTempo ? std::optional<u32>(initialSharedTempo->sourceTrackNumber) : std::nullopt,
          },
  });
  dialect.tick = tickAkaoSnesTrack;
  dialect.requiresCompleteSequencePrepass = true;
  return dialect;
}

struct SequenceHeaderInfo {
  u32 headerOffset = 0;
  u32 trackPointerOffset = 0;
  u32 romRelocBase = 0;
  u32 apuRelocBase = 0;
  u32 sequenceEnd = 0;
  u32 headerSize = 0;
};

[[nodiscard]] SequenceHeaderInfo sequenceHeaderInfo(ByteReader reader, const AkaoSnesLayout& layout) {
  SequenceHeaderInfo info{
      .headerOffset = layout.sequenceHeaderAddress,
      .trackPointerOffset = layout.sequenceHeaderAddress,
      .romRelocBase = layout.apuRelocBase,
      .apuRelocBase = layout.apuRelocBase,
      .sequenceEnd = 0,
      .headerSize = kAkaoSnesMaxTracks * 2,
  };

  if (layout.version == AKAOSNES_V3) {
    info.romRelocBase = reader.has(layout.sequenceHeaderAddress, 2) ? reader.le16(layout.sequenceHeaderAddress) : 0;
    if (layout.minorVersion != AKAOSNES_V3_FFMQ) {
      info.trackPointerOffset += 2;
      info.headerSize += 2;
    }
    const u32 endPointerOffset = info.trackPointerOffset + kAkaoSnesMaxTracks * 2;
    info.sequenceEnd = reader.has(endPointerOffset, 2)
                           ? relocatedAddress(reader.le16(endPointerOffset), info.romRelocBase, info.apuRelocBase)
                           : kAkaoSnesAramSize;
    info.headerSize += 2;
  } else if (layout.version == AKAOSNES_V4) {
    info.romRelocBase = reader.has(layout.sequenceHeaderAddress, 2) ? reader.le16(layout.sequenceHeaderAddress) : 0;
    const u32 endPointerOffset = layout.sequenceHeaderAddress + 2;
    info.sequenceEnd = reader.has(endPointerOffset, 2)
                           ? relocatedAddress(reader.le16(endPointerOffset), info.romRelocBase, info.apuRelocBase)
                           : kAkaoSnesAramSize;
    info.trackPointerOffset += 4;
    info.headerSize += 4;
  } else {
    info.sequenceEnd = 0;
  }
  if (info.sequenceEnd == 0 || info.sequenceEnd > reader.size()) {
    info.sequenceEnd = static_cast<u32>(reader.size());
  }
  return info;
}

[[nodiscard]] std::optional<InitialSharedTempoHint> initialSharedTempo(ByteReader reader, const AkaoSnesLayout& layout,
                                                                       const SequenceHeaderInfo& header) {
  for (u32 trackNumber = 0; trackNumber < kAkaoSnesMaxTracks; ++trackNumber) {
    const u32 pointerOffset = header.trackPointerOffset + trackNumber * 2;
    if (!reader.has(pointerOffset, 2)) {
      break;
    }
    const u16 rawTrackAddress = reader.le16(pointerOffset);
    if (rawTrackAddress == 0) {
      continue;
    }
    const u16 trackAddress = relocatedAddress(rawTrackAddress, header.romRelocBase, header.apuRelocBase);
    if (!reader.has(trackAddress, 1)) {
      continue;
    }
    const u32 readableEnd = std::min<u32>(header.sequenceEnd, reader.size());
    const size_t available = trackAddress < readableEnd ? readableEnd - trackAddress : 0;
    if (available == 0) {
      continue;
    }
    const auto bytes = reader.slice(trackAddress, available);
    if (const auto tempo = initialSharedTempoInPrelude(layout.version, layout.minorVersion, bytes)) {
      return InitialSharedTempoHint{
          .tempo = *tempo,
          .sourceTrackNumber = trackNumber,
      };
    }
  }

  return std::nullopt;
}

[[nodiscard]] u16 readLe16FromBytes(const std::vector<u8>& bytes, size_t offset) {
  if (offset + 2 > bytes.size()) {
    return 0;
  }
  return static_cast<u16>(bytes[offset] | (bytes[offset + 1] << 8));
}

void rewriteLe16InBytes(std::vector<u8>& bytes, size_t offset, u16 value) {
  if (offset + 2 > bytes.size()) {
    return;
  }
  bytes[offset] = static_cast<u8>(value & 0xff);
  bytes[offset + 1] = static_cast<u8>((value >> 8) & 0xff);
}

void resolveStoredBranchOperand(DecodedBytecodeCommand& decoded, const Context& context) {
  if (decoded.bytes.empty() || (context.romRelocBase == 0 && context.apuRelocBase == 0)) {
    return;
  }

  const EventType type = eventType(context.version, context.minorVersion, decoded.bytes.front());
  std::optional<size_t> operandOffset;
  switch (type) {
    case EventType::Goto:
    case EventType::CpuControlledJump:
      operandOffset = 1;
      break;
    case EventType::LoopBreak:
    case EventType::CpuControlledJumpV2:
      operandOffset = 2;
      break;
    default:
      break;
  }
  if (!operandOffset || *operandOffset + 2 > decoded.bytes.size()) {
    return;
  }

  const u16 raw = readLe16FromBytes(decoded.bytes, *operandOffset);
  rewriteLe16InBytes(decoded.bytes, *operandOffset, relocatedAddress(raw, context.romRelocBase, context.apuRelocBase));
}

}  // namespace

AkaoSnesSequenceDescriptor akaoSnesSequenceDescriptor(AkaoSnesVersion version, AkaoSnesMinorVersion minorVersion) {
  return AkaoSnesSequenceDescriptor{.dialect = makeDialect(version, minorVersion)};
}

void registerAkaoSnesSequenceDialects(SequenceDialectRegistry& registry) {
  const std::array<AkaoSnesMinorVersion, 14> minors{
      AKAOSNES_NOMINORVERSION, AKAOSNES_V1_FF4, AKAOSNES_V2_RS1, AKAOSNES_V3_FF5,   AKAOSNES_V3_SD2,
      AKAOSNES_V3_FFMQ,        AKAOSNES_V4_RS2, AKAOSNES_V4_LAL, AKAOSNES_V4_FF6,   AKAOSNES_V4_FM,
      AKAOSNES_V4_CT,          AKAOSNES_V4_RS3, AKAOSNES_V4_GH,  AKAOSNES_V4_BSGAME};
  for (const AkaoSnesVersion version : {AKAOSNES_NONE, AKAOSNES_V1, AKAOSNES_V2, AKAOSNES_V3, AKAOSNES_V4}) {
    for (const AkaoSnesMinorVersion minor : minors) {
      registry.add(makeDialect(version, minor));
    }
  }
}

TrackProgram decodeAkaoSnesSourceTrack(ByteReader reader, const AkaoSnesSequenceDescriptor& descriptor,
                                       u32 sourceTrackNumber, u32 startAddress, u32 bytecodeEnd, u32 sequenceOffset,
                                       u32 sequenceEnd, SourceMapBuilder* sourceMap,
                                       std::vector<Diagnostic>* diagnostics,
                                       std::optional<SourceAnnotationId> parentAnnotation,
                                       std::optional<AssetId> sequenceAsset) {
  CursorTrackDecodeInput input{
      .sequenceAsset = sequenceAsset,
      .trackIndex = sourceTrackNumber,
      .startOffset = startAddress,
      .bytecodeEnd = bytecodeEnd,
      .sequenceOffset = sequenceOffset,
      .sequenceEnd = sequenceEnd,
      .parentAnnotation = parentAnnotation,
      .sourceMap = sourceMap,
      .diagnostics = diagnostics,
      .maxCommands = 16384,
  };
  BytecodeDecodeContext decodeContext = cursorBytecodeDecodeContext(input);
  const auto trackAnnotation = createCursorTrackAnnotation(reader, input);
  if (trackAnnotation) {
    decodeContext.parentAnnotation = trackAnnotation;
  }

  const auto& context = cursorContext<Context>(descriptor.dialect);
  TrackState decodeState = makeDecodeCursorState<TrackState, Context>(decodeContext, context);
  const auto decodeCommand = [&](u32 offset) {
    auto decoded = decodeCursorCommandWithState<TrackState, Context, AkaoSnesCursorReader>(
        reader, offset, descriptor.dialect, decodeState, decodeContext);
    resolveStoredBranchOperand(decoded, context);
    return decoded;
  };

  TrackProgram track =
      decodeReachableBytecodeBlocks(reader, cursorBytecodeEnd(reader, input), input.startOffset, input.trackIndex,
                                    ReachableBytecodeDecodePolicy{.maxCommands = input.maxCommands}, decodeCommand);
  updateCursorTrackAnnotation(reader, input, trackAnnotation, track);
  return track;
}

SequenceProgramAsset parseAkaoSnesSequence(const ScanInput& input, const AkaoSnesLayout& layout, AssetId sequenceId,
                                           std::string_view displayName, SourceMapBuilder* sourceMap,
                                           std::vector<Diagnostic>* diagnostics) {
  const SequenceHeaderInfo header = sequenceHeaderInfo(input.reader, layout);
  const SourceRange headerRange = input.reader.range(header.headerOffset, header.headerSize);
  SourceAnnotationId headerAnnotation;
  if (sourceMap != nullptr) {
    auto annotation = sourceMap->header("Sequence Header", headerRange)
                          .kind("akao-snes-sequence-header")
                          .owner(ObjectRefs::sequence(sequenceId))
                          .field("version", headerRange, akaoSnesVersionName(layout.version))
                          .derived("minor_version", akaoSnesMinorVersionName(layout.minorVersion))
                          .derived("apu_reloc_base", header.apuRelocBase, SourceValueDisplay::Address)
                          .derived("rom_reloc_base", header.romRelocBase, SourceValueDisplay::Address)
                          .derived("sequence_end", header.sequenceEnd, SourceValueDisplay::Address);
    headerAnnotation = annotation.id();
  }

  const std::optional<InitialSharedTempoHint> sharedTempo = initialSharedTempo(input.reader, layout, header);
  AkaoSnesSequenceDescriptor descriptor{
      .dialect =
          makeDialect(layout.version, layout.minorVersion, header.romRelocBase, header.apuRelocBase, sharedTempo),
  };
  SequenceProgram program{
      .dialect = descriptor.dialect.id,
      .timebase = descriptor.dialect.timebase,
      .sourceBaseAddress = Address{layout.sequenceHeaderAddress},
      .behavior = descriptor.dialect.defaultBehavior,
  };

  for (u32 trackNumber = 0; trackNumber < kAkaoSnesMaxTracks; ++trackNumber) {
    const u32 pointerOffset = header.trackPointerOffset + trackNumber * 2;
    if (!input.reader.has(pointerOffset, 2)) {
      break;
    }
    const u16 rawTrackAddress = input.reader.le16(pointerOffset);
    const u16 trackAddress = relocatedAddress(rawTrackAddress, header.romRelocBase, header.apuRelocBase);
    const bool rawZeroIsNull = layout.version == AKAOSNES_V1 || layout.version == AKAOSNES_V2;
    if ((rawZeroIsNull && rawTrackAddress == 0) || trackAddress == header.sequenceEnd) {
      continue;
    }

    std::optional<SourceAnnotationId> pointerAnnotation;
    if (sourceMap != nullptr) {
      auto pointer =
          sourceMap
              ->pointer("Track Pointer", input.reader.range(pointerOffset, 2),
                        SourceTarget{input.reader.range(trackAddress, 1)})
              .kind("akao-snes-track-pointer")
              .owner(ObjectRefs::sequenceTrack(sequenceId, trackNumber))
              .derived("source_track", trackNumber)
              .field("destination", input.reader.range(pointerOffset, 2), trackAddress, SourceValueDisplay::Address);
      if (headerAnnotation.valid()) {
        pointer.parent(headerAnnotation);
      }
      pointerAnnotation = pointer.id();
    }

    auto track = decodeAkaoSnesSourceTrack(input.reader, descriptor, trackNumber, trackAddress, header.sequenceEnd,
                                           layout.sequenceHeaderAddress, header.sequenceEnd, sourceMap, diagnostics,
                                           pointerAnnotation, sequenceId);
    program.tracks.push_back(std::move(track));
  }

  return SequenceProgramAsset{
      .metadata =
          AssetMetadata{
              .id = sequenceId,
              .format = "AkaoSnes",
              .name = std::string(displayName),
              .range = headerRange,
          },
      .program = std::move(program),
  };
}

}  // namespace vgmtrans::formats::akao_snes
