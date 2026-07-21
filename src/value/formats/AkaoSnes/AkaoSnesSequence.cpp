/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/AkaoSnes/AkaoSnes.h"

#include "value/base/LevelScale.h"
#include "value/sequence/BytecodeDecode.h"
#include "value/sequence/CommandSourceMap.h"
#include "value/sequence/CompilerCursor.h"
#include "value/sequence/SequenceMotion.h"
#include "value/synth/SynthMath.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <unordered_set>
#include <vector>

namespace vgmtrans::formats::akao_snes {

using namespace core;

namespace {

constexpr u16 kDefaultPitchBendRangeCents = 200;
constexpr s32 kNominalDspPitch = 0x1000;
constexpr s32 kPitchFractionScale = 0x100;

enum class EventType {
  Unknown0,
  Unknown1,
  Unknown2,
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

[[nodiscard]] double levelFromLegacyMidiVolume(u8 volume) {
  return std::clamp(static_cast<double>(volume) / 127.0, 0.0, 1.0);
}

[[nodiscard]] double rightGainFromPan(u8 pan) {
  return pan / 255.0;
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

[[nodiscard]] double lfoRateHz(AkaoSnesVersion version, u8 rate, u8 depth, u8 timer0Frequency) {
  const u16 frames = effectiveRateFrames(version, rate, depth);
  return frames == 0 ? 0.0 : akaoSnesFrameRateHz(timer0Frequency) / (2.0 * frames);
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
  return ticks * (256.0 / (akaoSnesFrameRateHz(timer0Frequency) * safeTempo));
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
  const AkaoSnesLfoRateRange range = akaoSnesLfoRateRange(version);
  return midiValueForHertzInRange(lfoRateHz(version, rate, depth, timer0Frequency), range.minimum, range.maximum);
}

[[nodiscard]] u8 delayMidiValue(AkaoSnesVersion version, u8 delay, u8 tempo, u8 timer0Frequency) {
  return midiValueForSecondsInRange(delaySeconds(version, delay, tempo, timer0Frequency), 0.0,
                                    akaoSnesMaxLfoDelaySeconds(version));
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
  u64 order = 0;
};

struct ProgramState {
  explicit ProgramState(const SequenceProgram& program) : profile(decodeAkaoSnesProfile(program.config.profile)) {
    for (const TrackProgram& track : program.tracks) {
      for (const SourceCommand& command : track.commands) {
        const Address fallthrough{command.address.value + command.encodedSize};
        const auto nextIndex = track.addressIndex.find(fallthrough);
        if (!nextIndex) {
          continue;
        }
        const SourceCommand& next = track.commands[*nextIndex];
        if (next.flow.terminal) {
          terminalPitchBoundaries.insert(fallthrough.value);
          continue;
        }
        const SemanticOperand* envelopeOff = semanticOperand(next, "pitch_envelope_off");
        const bool* clearsEnvelope = envelopeOff == nullptr ? nullptr : std::get_if<bool>(&envelopeOff->value);
        if (clearsEnvelope == nullptr || !*clearsEnvelope) {
          continue;
        }
        const Address afterOff{next.address.value + next.encodedSize};
        const auto afterIndex = track.addressIndex.find(afterOff);
        if (!afterIndex) {
          continue;
        }
        const SourceCommand& after = track.commands[*afterIndex];
        if (after.flow.terminal) {
          terminalPitchBoundaries.insert(fallthrough.value);
          continue;
        }
        if (!after.flow.unconditionalJump()) {
          continue;
        }
        if (after.flow.staticTargets.front().value <= command.address.value) {
          terminalPitchBoundaries.insert(fallthrough.value);
        }
      }
    }
  }

  void observeTempo(u32 sourceTrackNumber, u64 tick, u8 tempo) {
    if (!collecting) {
      return;
    }
    tempoChanges.push_back(SharedTempoChange{
        .tick = tick,
        .tempo = tempo,
        .sourceTrackNumber = sourceTrackNumber,
        .order = nextOrder++,
    });
  }

  void finishPrepass() {
    std::ranges::stable_sort(tempoChanges, [](const SharedTempoChange& lhs, const SharedTempoChange& rhs) {
      return lhs.tick < rhs.tick || (lhs.tick == rhs.tick && lhs.order < rhs.order);
    });
    collecting = false;
  }

  [[nodiscard]] std::optional<InitialSharedTempoHint> initialTempo() const {
    if (tempoChanges.empty() || tempoChanges.front().tick != 0) {
      return std::nullopt;
    }
    return InitialSharedTempoHint{
        .tempo = tempoChanges.front().tempo,
        .sourceTrackNumber = tempoChanges.front().sourceTrackNumber,
    };
  }

  [[nodiscard]] std::optional<u8> tempoAt(u64 tick) const {
    std::optional<u8> tempo;
    for (const auto& change : tempoChanges) {
      if (change.tick > tick) {
        break;
      }
      tempo = change.tempo;
    }
    return tempo;
  }

  [[nodiscard]] bool terminalPitchBoundary(Address fallthrough) const {
    return terminalPitchBoundaries.contains(fallthrough.value);
  }

  AkaoSnesProfile profile;
  std::vector<SharedTempoChange> tempoChanges;
  std::unordered_set<u64> terminalPitchBoundaries;
  bool collecting = true;
  u64 nextOrder = 0;
};

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
  TrackState(const SequenceProgram& program, const TrackProgram& track) {
    reset(decodeAkaoSnesProfile(program.config.profile));
    trackNumber = track.sourceTrackNumber;
  }

  void reset(AkaoSnesProfile profile) {
    octave = 6;
    transpose = 0;
    onetimeDuration = 0;
    slur = false;
    legato = false;
    percussion = false;
    nonPercussionProgram = 0;
    loopLevel = 0;
    tempo = kAkaoSnesDefaultTempo;
    pan8Bit = akaoSnesUses8BitPan(profile.version, profile.minorVersion);
    sharedTempoApplied = false;
    lastTieableNoteTick.reset();
    pitchAutomationStopTick.reset();
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

  [[nodiscard]] bool pitchBendAtRest() const {
    return currentPitchBendRangeCents == kDefaultPitchBendRangeCents && currentPitchBendValue == 0;
  }

  void clearPendingPitchSlide() {
    pendingPitchSlideSteps = 0;
    pendingPitchSlideSemitones = 0;
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

  u32 trackNumber = 0;
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
  bool sharedTempoApplied = false;
  std::optional<u64> lastTieableNoteTick;
  std::optional<u64> pitchAutomationStopTick;
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

// Playback holds the history-dependent services shared by several commands or
// substantial enough to name. Short one-off effects stay beside their opcode.
struct Playback {
  TrackState& track;
  PerformanceEmitter& out;
  VmApi& vm;
  ProgramState& program;
  const AkaoSnesProfile& context;

  Playback(TrackState& track, PerformanceEmitter& out, VmApi& vm, ProgramState& program)
      : track(track), out(out), vm(vm), program(program), context(program.profile) {}

  [[nodiscard]] bool terminalPitchWaitBoundary() const {
    return track.pitchAutomationStopTick && vm.tick() == *track.pitchAutomationStopTick;
  }

  void pan(u8 rawPan) {
    const u8 panValue = static_cast<u8>(rawPan << (track.pan8Bit ? 0 : 1));
    const double rightGain = rightGainFromPan(panValue);
    out.stereoBalance(1.0 - rightGain, rightGain);
  }

  void emitPitchBendRange(u16 cents) {
    const u16 range = std::max<u16>(kDefaultPitchBendRangeCents, cents);
    if (track.currentPitchBendRangeCents == range) {
      return;
    }
    out.pitchBendRange(PitchBendRangePerformanceEvent{.cents = range});
    track.currentPitchBendRangeCents = range;
  }

  void emitPitchBendSemitones(double semitones, s16 midiBendValue) {
    if (track.currentPitchBendValue == midiBendValue) {
      return;
    }
    out.pitchBend(semitones);
    track.currentPitchBendValue = midiBendValue;
  }

  void emitPitchBendForCurrentPitch() {
    const s16 value = akaoSnesPitchBendValue(track.currentPitch, track.pitchBase, track.currentPitchBendRangeCents);
    emitPitchBendSemitones(akaoSnesPitchCents(track.currentPitch, track.pitchBase) / 100.0, value);
  }

  void resetPitchBendForNewNote() {
    track.pitchBaseValid = false;
    track.pitchSlideActive = false;
    track.pitchSlideStepsRemaining = 0;
    track.pitchSlideNoteValid = false;
    if (track.pitchBendAtRest()) {
      return;
    }
    if (akaoSnesSupportsPitchEnvelope(context.version) && track.pitchEnvelope.enabled) {
      emitPitchBendSemitones(0.0, 0);
      return;
    }
    emitPitchBendRange(kDefaultPitchBendRangeCents);
    emitPitchBendSemitones(0.0, 0);
  }

  void beginPitchEnvelopeForNote() {
    auto& envelope = track.pitchEnvelope;
    if (!akaoSnesSupportsPitchEnvelope(context.version) || !envelope.enabled || !track.pitchBaseValid) {
      return;
    }
    const s32 targetPitch = akaoSnesPitchForSemitoneOffset(envelope.semitones);
    const s32 rawDiff = (targetPitch - track.pitchBase) / kPitchFractionScale;
    const s32 rawMagnitude = rawDiff < 0 ? -rawDiff : rawDiff;
    envelope.targetOffset = (envelope.semitones < 0 ? -rawMagnitude : rawMagnitude) * kPitchFractionScale;
    envelope.activeDelay = envelope.delay;
    envelope.activeCount = context.version == AKAOSNES_V1 ? envelope.length : 0;
    envelope.progress = 0;
    envelope.active = envelope.targetOffset != 0 && envelope.progressStep != 0;
    track.currentPitch = track.pitchBase;
    if (envelope.active) {
      emitPitchBendRange(akaoSnesPitchBendRangeCents(track.pitchBase, track.pitchBase + envelope.targetOffset,
                                                     kDefaultPitchBendRangeCents));
    }
  }

  void beginNotePitch(u8 note, bool validForPitchBend) {
    resetPitchBendForNewNote();
    if (!validForPitchBend) {
      return;
    }
    track.pitchBase = kNominalDspPitch * kPitchFractionScale;
    track.currentPitch = track.pitchBase;
    track.pitchBaseValid = true;
    track.pitchSlideBaseNote = akaoSnesCorrectedNote(note, track.transpose);
    track.pitchSlideCurrentNote = track.pitchSlideBaseNote;
    track.pitchSlideNoteValid = true;
    beginPitchEnvelopeForNote();
  }

  void updatePitchSlide() {
    if (!track.pitchSlideActive || !track.pitchBaseValid) {
      return;
    }
    if (track.pitchSlideStepsRemaining == 0) {
      track.pitchSlideActive = false;
      return;
    }
    --track.pitchSlideStepsRemaining;
    track.currentPitch =
        track.pitchSlideStepsRemaining == 0 ? track.pitchSlideFinalPitch : track.currentPitch + track.pitchSlideStep;
    emitPitchBendForCurrentPitch();
    if (track.pitchSlideStepsRemaining == 0) {
      track.pitchSlideActive = false;
    }
  }

  void beginPendingPitchSlide() {
    if (track.pendingPitchSlideSteps == 0 || track.pendingPitchSlideSemitones == 0) {
      track.clearPendingPitchSlide();
      return;
    }
    const u16 steps = track.pendingPitchSlideSteps;
    const s8 semitones = track.pendingPitchSlideSemitones;
    track.clearPendingPitchSlide();
    if (!track.pitchBaseValid || !track.pitchSlideNoteValid) {
      return;
    }
    track.pitchSlideCurrentNote = static_cast<s16>(track.pitchSlideCurrentNote + semitones);
    const s32 targetPitch = akaoSnesPitchForSemitoneOffset(track.pitchSlideCurrentNote - track.pitchSlideBaseNote);
    track.pitchSlideStep = akaoSnesPitchSlideStep(context.version, track.currentPitch, targetPitch, steps);
    track.pitchSlideFinalPitch = track.currentPitch + (track.pitchSlideStep * static_cast<s32>(steps));
    emitPitchBendRange(std::max(
        akaoSnesPitchBendRangeCents(track.pitchBase, targetPitch, kDefaultPitchBendRangeCents),
        akaoSnesPitchBendRangeCents(track.pitchBase, track.pitchSlideFinalPitch, kDefaultPitchBendRangeCents)));
    emitPitchBendForCurrentPitch();
    track.pitchSlideStepsRemaining = steps;
    track.pitchSlideActive = true;
    updatePitchSlide();
  }

  void setPitchWaitBoundary(Address fallthrough, u32 waitTicks) {
    track.pitchAutomationStopTick =
        program.terminalPitchBoundary(fallthrough) ? std::optional<u64>{vm.tick() + waitTicks} : std::nullopt;
  }

  void updatePitchEnvelope() {
    if (!akaoSnesSupportsPitchEnvelope(context.version) || !track.pitchEnvelope.active || !track.pitchBaseValid ||
        terminalPitchWaitBoundary() || !track.pitchEnvelopeDelayElapsed()) {
      return;
    }
    s32 currentOffset = 0;
    if (!track.advancePitchEnvelopeTick(context.version, currentOffset)) {
      return;
    }
    track.currentPitch = track.pitchBase + currentOffset;
    emitPitchBendForCurrentPitch();
  }

  Effects note(u8 durationIndex, u8 noteIndex, Address fallthrough) {
    u8 length = noteDuration(context.version, durationIndex);
    if (track.onetimeDuration != 0) {
      length = track.onetimeDuration;
      track.onetimeDuration = 0;
    }
    const u8 duration = (!track.slur && !track.legato) ? (length > 2 ? static_cast<u8>(length - 2) : u8{1}) : length;
    setPitchWaitBoundary(fallthrough, length);

    if (noteIndex < 12) {
      const double velocity = kAkaoSnesNoteVelocity / 127.0;
      const u8 note = static_cast<u8>((track.octave * 12) + noteIndex);
      beginNotePitch(note, !track.percussion);
      beginPendingPitchSlide();
      if (!track.slur && !track.legato) {
        beginVibratoForNote();
        beginTremoloForNote();
      }
      if (track.percussion) {
        out.note(kAkaoSnesDrumKeyBias + noteIndex - track.transpose, velocity, duration);
      } else {
        out.note((track.octave * 12) + noteIndex + track.transpose, velocity, duration);
      }
      track.lastTieableNoteTick = vm.tick() + length;
      return Effects::wait(length);
    }

    if (noteIndex == akaoSnesStatusNoteIndexTie(context.version)) {
      beginPendingPitchSlide();
      if (track.lastTieableNoteTick && *track.lastTieableNoteTick >= vm.tick()) {
        out.note(0.0, 1.0, duration, true);
        track.lastTieableNoteTick = vm.tick() + length;
      }
      return Effects::wait(length);
    }

    track.lastTieableNoteTick.reset();
    return Effects::wait(length);
  }

  void emitVibratoDepth(u8 midiDepth, bool force = false) {
    track.vibrato.emitDepth(
        midiDepth,
        [&](u8 outputDepth) {
          const double amount = static_cast<double>(outputDepth) / 127.0;
          out.modulation(ModulationPerformanceEvent{
              .target = ModulationPerformanceTarget::VibratoDepth,
              .amount = amount,
              .pitchDepthSemitones = (amount * maxVibratoDepthCents(context.version)) / 100.0,
              .controllerRangeMaxAmount = 1.0,
          });
        },
        force);
  }

  void emitTremoloDepth(u8 midiDepth, bool force = false) {
    track.tremolo.emitDepth(
        midiDepth,
        [&](u8 outputDepth) {
          out.modulation(ModulationPerformanceEvent{
              .target = ModulationPerformanceTarget::TremoloDepth,
              .amount = static_cast<double>(outputDepth) / 127.0,
              .controllerRangeMaxAmount = 1.0,
          });
        },
        force);
  }

  void setLfoOutputDepth(LfoTarget target, u8 depth, bool force = false) {
    if (target == LfoTarget::Vibrato) {
      emitVibratoDepth(depth, force);
    } else {
      emitTremoloDepth(depth, force);
    }
  }

  void clearLfoRateAndDelay(LfoTarget target) {
    if (target == LfoTarget::Vibrato) {
      out.modulation(ModulationPerformanceEvent{
          .target = ModulationPerformanceTarget::VibratoRate,
          .amount = 0.0,
          .controllerRangeMaxAmount = 1.0,
      });
      out.vibratoDelay(0, 0);
    } else {
      out.modulation(ModulationPerformanceEvent{
          .target = ModulationPerformanceTarget::TremoloRate,
          .amount = 0.0,
          .controllerRangeMaxAmount = 1.0,
      });
      out.tremoloDelay(0, 0);
    }
  }

  void syncLfoRateAndDelay(LfoTarget target) {
    const bool isVibrato = target == LfoTarget::Vibrato;
    LfoState& lfo = isVibrato ? track.vibrato : track.tremolo;
    if (!isLfoActive(context.version, lfo.rate(), lfo.depth())) {
      return;
    }
    if (isVibrato) {
      track.configureVibratoFade(context.version);
    } else {
      track.configureTremoloFade(context.version);
    }
    const u8 timer = akaoSnesTimer0Frequency(context.version, context.minorVersion);
    const u8 rateValue = rateMidiValue(context.version, lfo.rate(), lfo.depth(), timer);
    if (isVibrato) {
      out.modulation(ModulationPerformanceEvent{
          .target = ModulationPerformanceTarget::VibratoRate,
          .amount = static_cast<double>(rateValue) / 127.0,
          .frequencyHz = lfoRateHz(context.version, lfo.rate(), lfo.depth(), timer),
          .controllerRangeMaxAmount = 1.0,
      });
      out.vibratoDelay(lfoDelayTicks(context.version, lfo.delay()),
                       delayMidiValue(context.version, lfo.delay(), track.tempo, timer));
    } else {
      out.modulation(ModulationPerformanceEvent{
          .target = ModulationPerformanceTarget::TremoloRate,
          .amount = static_cast<double>(rateValue) / 127.0,
          .frequencyHz = lfoRateHz(context.version, lfo.rate(), lfo.depth(), timer),
          .controllerRangeMaxAmount = 1.0,
      });
      out.tremoloDelay(lfoDelayTicks(context.version, lfo.delay()),
                       delayMidiValue(context.version, lfo.delay(), track.tempo, timer));
    }
  }

  void setLfo(LfoTarget target, u8 delay, u8 rate, u8 depth) {
    const bool isVibrato = target == LfoTarget::Vibrato;
    const bool active = (isVibrato || exportsTremolo(context.version)) && isLfoActive(context.version, rate, depth);
    LfoState& lfo = isVibrato ? track.vibrato : track.tremolo;
    lfo.configure(delay, rate, depth);
    const auto initialTempo = program.initialTempo();
    const bool beforeInitialTempoTrack = initialTempo && track.trackNumber < initialTempo->sourceTrackNumber;
    const bool afterInitialTempoTrack = initialTempo && track.trackNumber > initialTempo->sourceTrackNumber;
    if (initialTempo && !track.sharedTempoApplied && afterInitialTempoTrack) {
      track.tempo = initialTempo->tempo;
      track.sharedTempoApplied = true;
    }
    if (isVibrato) {
      track.configureVibratoFade(context.version);
    } else {
      track.configureTremoloFade(context.version);
    }
    u8 midiDepth = 0;
    if (active) {
      midiDepth = isVibrato ? vibratoDepthMidiValue(context.version, rate, depth)
                            : tremoloDepthMidiValue(context.version, rate, depth, delay);
    }
    if (isVibrato && context.version == AKAOSNES_V4 && active && track.vibrato.hasReusableFade()) {
      const u32 delayTicks = lfoDelayTicks(context.version, track.vibrato.delay());
      const s32 initialDepth = track.vibrato.configuredDepth(8) / 4;
      track.vibrato.beginReusableFade(delayTicks, track.vibrato.configuredDepth(8), initialDepth);
      midiDepth = delayTicks == 0 ? track.vibratoFadeDepthMidiValue(context.version, initialDepth) : 0;
    }
    setLfoOutputDepth(target, midiDepth, true);
    if (active) {
      syncLfoRateAndDelay(target);
      if (vm.tick() == 0 && initialTempo && beforeInitialTempoTrack && initialTempo->tempo != track.tempo) {
        track.tempo = initialTempo->tempo;
        track.sharedTempoApplied = true;
        syncLfoRateAndDelay(target);
      }
    } else {
      clearLfoRateAndDelay(target);
    }
  }

  void clearLfo(LfoTarget target) {
    LfoState& lfo = target == LfoTarget::Vibrato ? track.vibrato : track.tremolo;
    lfo.setDepth(0);
    lfo.clearReusableFade();
    setLfoOutputDepth(target, 0, true);
  }

  void beginVibratoForNote() {
    if (context.version == AKAOSNES_V2 || !track.vibrato.hasReusableFade() ||
        !isLfoActive(context.version, track.vibrato.rate(), track.vibrato.depth())) {
      return;
    }
    const u32 delay = lfoDelayTicks(context.version, track.vibrato.delay());
    const s32 initialDepth = context.version == AKAOSNES_V4 ? track.vibrato.configuredDepth(8) / 4 : 0;
    track.vibrato.beginReusableFade(delay, track.vibrato.configuredDepth(8), initialDepth);
    emitVibratoDepth(delay == 0 ? track.vibratoFadeDepthMidiValue(context.version, initialDepth) : 0, true);
  }

  void beginTremoloForNote() {
    if (context.version != AKAOSNES_V3 || !track.tremolo.hasReusableFade()) {
      return;
    }
    track.tremolo.beginReusableFadeToConfiguredDepth(8);
    emitTremoloDepth(0, true);
  }

  void updateVibratoFade() {
    if (context.version == AKAOSNES_V2 || !track.vibrato.fadeActive()) {
      return;
    }
    const auto fadeTick = track.vibrato.tickFade();
    if (fadeTick.status != SequenceMotionStatus::Inactive && fadeTick.status != SequenceMotionStatus::Delayed) {
      const s32 current = track.vibrato.clampToConfiguredDepth(fadeTick.current, 8);
      track.vibrato.setCurrentDepthPreservingMotion(current);
      emitVibratoDepth(track.vibratoFadeDepthMidiValue(context.version, current));
    }
  }

  void updateTremoloFade() {
    if (context.version != AKAOSNES_V3 || !track.tremolo.fadeActive()) {
      return;
    }
    const auto fadeTick = track.tremolo.tickFade();
    if (fadeTick.status != SequenceMotionStatus::Inactive && fadeTick.status != SequenceMotionStatus::Delayed) {
      const s32 current = track.tremolo.clampToConfiguredDepth(fadeTick.current, 8);
      track.tremolo.setCurrentDepthPreservingMotion(current);
      emitTremoloDepth(track.tremoloFadeDepthMidiValue(context.version, current));
    }
  }

  void tempoChange(u8 rawTempo) {
    if (context.minorVersion == AKAOSNES_V4_FM || context.minorVersion == AKAOSNES_V4_CT) {
      rawTempo = static_cast<u8>(rawTempo + ((rawTempo * 0x14) >> 8));
    }
    track.tempo = rawTempo;
    program.observeTempo(track.trackNumber, vm.tick(), rawTempo);
    if (const auto initial = program.initialTempo(); initial && rawTempo == initial->tempo) {
      track.sharedTempoApplied = true;
    }
    out.tempo(tempoMicrosecondsPerQuarter(context.version, context.minorVersion, rawTempo));
    syncLfoRateAndDelay(LfoTarget::Vibrato);
    syncLfoRateAndDelay(LfoTarget::Tremolo);
  }

  void syncSharedTempoAtTick() {
    const std::optional<u8> sharedTempo = program.tempoAt(vm.tick());
    if (!sharedTempo || (track.sharedTempoApplied && track.tempo == *sharedTempo)) {
      return;
    }
    track.tempo = *sharedTempo;
    track.sharedTempoApplied = true;
    syncLfoRateAndDelay(LfoTarget::Vibrato);
    syncLfoRateAndDelay(LfoTarget::Tremolo);
  }

  Effects loopEnd() {
    const u8 slot = (track.loopLevel == 0 ? static_cast<u8>(track.loops.size()) : track.loopLevel) - 1;
    LoopFrame& frame = track.loops[slot];
    if (context.version == AKAOSNES_V4) {
      ++frame.incrementCount;
    }
    if (frame.totalPlays == 0) {
      return Effects{.step = vm.declaredLoop(frame.start)};
    }
    Effects effects = vm.countedRepeatUntil(slot, frame.totalPlays, frame.start);
    if (effects.step.kind == StepKind::Next) {
      track.loopLevel = slot;
      frame.remainingPlays = 1;
    } else if (frame.remainingPlays > 1) {
      --frame.remainingPlays;
    }
    return effects;
  }

  Effects loopBreak(u8 count, Address destination) {
    const u8 slot = (track.loopLevel == 0 ? static_cast<u8>(track.loops.size()) : track.loopLevel) - 1;
    LoopFrame& frame = track.loops[slot];
    if (context.version != AKAOSNES_V4) {
      ++frame.incrementCount;
    }
    if (count != frame.incrementCount) {
      return Effects{};
    }
    if (context.version == AKAOSNES_V1) {
      if (frame.remainingPlays != 0 && --frame.remainingPlays == 0) {
        track.loopLevel = slot;
      }
    } else if (context.version != AKAOSNES_V2 && context.version != AKAOSNES_V3 && frame.remainingPlays <= 1) {
      track.loopLevel = slot;
    }
    RepeatCounter counter = vm.repeatCounter(slot);
    if (counter.active()) {
      counter.finish();
    }
    return Effects{.step = vm.finiteBranch(destination)};
  }

  void tickAutomation() {
    syncSharedTempoAtTick();
    if (terminalPitchWaitBoundary()) {
      return;
    }
    updateVibratoFade();
    updateTremoloFade();
    updatePitchSlide();
    updatePitchEnvelope();
  }

  void tick() { tickAutomation(); }
};

using AkaoSnesCursor = CompilerCursor<TrackState, Playback>;

[[nodiscard]] DecodedBytecodeCommand decodeCommand(ByteReader reader, u32 begin, u32 end, AkaoSnesProfile profile,
                                                   u32 romRelocBase, u32 apuRelocBase,
                                                   std::vector<Diagnostic>* diagnostics) {
  AkaoSnesCursor cursor(reader, begin, end, "akao-snes", diagnostics);
  if (!cursor.hasOpcode()) {
    return cursor.truncated();
  }

  const u8 opcode = cursor.opcode();
  const EventType type = eventType(profile.version, profile.minorVersion, opcode);
  const auto relocated = [&](auto& event, SemanticOperandRole role = SemanticOperandRole::Address) {
    const auto stored = event.rawU16le("stored_destination", SourceValueDisplay::Address);
    return event.resolved(
        "destination", stored,
        [&](u16 address) { return Address{relocatedAddress(address, romRelocBase, apuRelocBase)}; },
        SourceValueDisplay::Address, role);
  };
  const auto ignored = [&](u8 operandCount) -> DecodedBytecodeCommand {
    auto event = cursor.unsupported("Unknown Event", "unknown");
    event.opcodeValue("opcode", opcode, SourceValueDisplay::Hex);
    event.rawBytes("arguments", operandCount);
    return event.ignore();
  };

  switch (type) {
    case EventType::Unknown0:
      return ignored(0);
    case EventType::Unknown1:
      return ignored(1);
    case EventType::Unknown2:
      return ignored(2);

    case EventType::Note: {
      const u8 tableSize = akaoSnesNoteDurationTableSize(profile.version);
      const u8 durationIndex = opcode % tableSize;
      const u8 noteIndex = opcode / tableSize;
      const bool rest = noteIndex >= 12 && noteIndex != akaoSnesStatusNoteIndexTie(profile.version);
      auto event = cursor.command(rest ? "Rest" : "Note", rest ? SequenceSemantic::Rest : SequenceSemantic::Note);
      event.opcodeValue("duration_index", durationIndex);
      event.opcodeValue("note_index", noteIndex);
      return event.invoke<&Playback::note>(durationIndex, noteIndex, event.nextAddress());
    }

    case EventType::Nop:
      return cursor.noOp("NOP");
    case EventType::Nop1: {
      auto event = cursor.noOp("NOP");
      event.u8("arg1");
      return event;
    }

    case EventType::Volume: {
      auto event = cursor.command("Volume", SequenceSemantic::Level);
      return event.emitLevel(levelFromLegacyMidiVolume(static_cast<u8>(event.u8("volume") >> 1)));
    }
    case EventType::VolumeFade: {
      auto event = cursor.command("Volume Fade", SequenceSemantic::Level);
      const u16 length = profile.version == AKAOSNES_V1 ? event.u16le("length") : event.u8("length");
      const double level = levelFromLegacyMidiVolume(static_cast<u8>(event.u8("volume") >> 1));
      if (length != 0) {
        return event.ignore();
      }
      return event.emitLevel(level);
    }
    case EventType::Pan: {
      auto event = cursor.command("Pan", SequenceSemantic::Pan);
      return event.invoke<&Playback::pan>(event.u8("pan"));
    }
    case EventType::PanFade: {
      auto event = cursor.command("Pan Fade", SequenceSemantic::Pan);
      const u16 length = profile.version == AKAOSNES_V1 ? event.u16le("length") : event.u8("length");
      const u8 pan = event.u8("pan");
      if (length != 0) {
        return event.ignore();
      }
      return event.invoke<&Playback::pan>(pan);
    }

    case EventType::PitchEnvelopeOn: {
      auto event = cursor.command("Pitch Envelope On", SequenceSemantic::Pitch);
      s8 semitones = 0;
      u8 delay = 0;
      u8 length = 0;
      if (profile.version == AKAOSNES_V1) {
        delay = static_cast<u8>(event.u8("delay") + 1);
        length = event.u8("length");
        semitones = event.s8("semitones");
      } else {
        semitones = event.s8("semitones");
        delay = event.u8("delay");
        length = event.u8("length");
      }
      return event.invoke(
          [](Playback& playback, s8 pitch, u8 wait, u8 duration) {
            if (pitch == 0 || duration == 0) {
              playback.track.pitchEnvelope = {};
              return;
            }
            auto& envelope = playback.track.pitchEnvelope;
            envelope.enabled = true;
            envelope.semitones = pitch;
            envelope.delay = wait;
            envelope.length = duration;
            envelope.progressStep = akaoSnesPitchEnvelopeProgressStep(playback.context.version, duration);
          },
          semitones, delay, length);
    }
    case EventType::PitchEnvelopeOff: {
      auto event = cursor.command("Pitch Envelope Off", SequenceSemantic::Pitch);
      event.derived("pitch_envelope_off", true, SemanticOperandRole::State);
      return event.invoke([](Playback& playback) { playback.track.pitchEnvelope = {}; });
    }
    case EventType::PitchSlide: {
      auto event = cursor.command("Pitch Slide", SequenceSemantic::Pitch);
      const u16 steps = static_cast<u16>(event.u8("time")) + 1;
      return event.invoke(
          [](Playback& playback, u16 duration, s8 pitch) {
            playback.track.pendingPitchSlideSteps = duration;
            playback.track.pendingPitchSlideSemitones = pitch;
            if (pitch == 0) {
              playback.track.clearPendingPitchSlide();
            }
          },
          steps, event.s8("semitones"));
    }

    case EventType::VibratoOn:
    case EventType::TremoloOn: {
      const LfoTarget target = type == EventType::VibratoOn ? LfoTarget::Vibrato : LfoTarget::Tremolo;
      auto event = cursor.command(type == EventType::VibratoOn ? "Vibrato" : "Tremolo", SequenceSemantic::Modulation);
      u8 delay = 0;
      u8 rate = 0;
      u8 depth = 0;
      if (profile.version == AKAOSNES_V2) {
        depth = event.u8("depth");
        delay = event.u8("delay");
        rate = event.u8("rate");
      } else {
        delay = event.u8("delay");
        rate = event.u8("rate");
        depth = event.u8("depth");
      }
      return event.invoke<&Playback::setLfo>(target, delay, rate, depth);
    }
    case EventType::VibratoOff:
      return cursor.command("Vibrato Off", SequenceSemantic::Modulation)
          .invoke<&Playback::clearLfo>(LfoTarget::Vibrato);
    case EventType::TremoloOff:
      return cursor.command("Tremolo Off", SequenceSemantic::Modulation)
          .invoke<&Playback::clearLfo>(LfoTarget::Tremolo);
    case EventType::PanLfoOn: {
      auto event = cursor.command("Pan LFO", SequenceSemantic::Modulation, CommandPlaybackStatus::SourceOnly);
      event.u8("depth");
      event.u8("rate");
      return event;
    }
    case EventType::PanLfoOnWithDelay: {
      auto event = cursor.command("Pan LFO", SequenceSemantic::Modulation, CommandPlaybackStatus::SourceOnly);
      event.u8("delay");
      event.u8("rate");
      event.u8("depth");
      return event;
    }

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
    case EventType::LoopRestart: {
      auto event = cursor.sourceOnly("State Change");
      if (type == EventType::SlurOn) {
        event.set<&TrackState::slur>(true);
      } else if (type == EventType::SlurOff) {
        event.set<&TrackState::slur>(false);
      } else if (type == EventType::LegatoOn) {
        event.set<&TrackState::legato>(true);
      } else if (type == EventType::LegatoOff) {
        event.set<&TrackState::legato>(false);
      }
      return event;
    }

    case EventType::NoiseFreq: {
      auto event = cursor.sourceOnly("Noise Frequency");
      event.u8("frequency");
      return event;
    }
    case EventType::Octave: {
      auto event = cursor.command("Octave", SequenceSemantic::Pitch);
      return event.set<&TrackState::octave>(event.u8("octave"));
    }
    case EventType::OctaveUp:
      return cursor.command("Octave Up", SequenceSemantic::Pitch).add<&TrackState::octave>(u8{1});
    case EventType::OctaveDown:
      return cursor.command("Octave Down", SequenceSemantic::Pitch).add<&TrackState::octave>(s8{-1});
    case EventType::TransposeAbs: {
      auto event = cursor.command("Transpose", SequenceSemantic::Pitch);
      return event.set<&TrackState::transpose>(event.s8("semitones"));
    }
    case EventType::TransposeRel: {
      auto event = cursor.command("Transpose Relative", SequenceSemantic::Pitch);
      return event.add<&TrackState::transpose>(event.s8("semitones"));
    }
    case EventType::Tuning: {
      auto event = cursor.command("Tuning", SequenceSemantic::Pitch);
      return event.emitTuning(tuningCents(event.u8("tuning")));
    }
    case EventType::ProgramChange: {
      auto event = cursor.command("Program", SequenceSemantic::Program);
      event.derived("bank", u8{0}, SemanticOperandRole::InstrumentBank);
      const u8 program = event.u8("program", SemanticOperandRole::InstrumentProgram);
      return event.invoke(
          [](Playback& playback, u8 programNumber) {
            playback.track.nonPercussionProgram = programNumber;
            if (!playback.track.percussion) {
              playback.out.instrument(0, programNumber);
            }
          },
          program);
    }

    case EventType::VolumeEnvelope: {
      auto event = cursor.sourceOnly("Volume Envelope");
      event.u8("envelope");
      return event;
    }
    case EventType::GainRelease: {
      auto event = cursor.sourceOnly("Gain Release");
      event.u8("gain");
      return event;
    }
    case EventType::DurationRate: {
      auto event = cursor.sourceOnly("Duration Rate");
      event.u8("rate");
      return event;
    }
    case EventType::AdsrAr:
    case EventType::AdsrDr:
    case EventType::AdsrSl:
    case EventType::AdsrSr: {
      auto event = cursor.sourceOnly("ADSR");
      event.u8("value");
      return event;
    }

    case EventType::LoopStart: {
      auto event = cursor.command("Loop Start", SequenceSemantic::Loop);
      const u8 count = event.u8("count");
      return event.invoke(
          [](Playback& playback, u8 repeatCount, Address start) {
            const u32 totalPlays = repeatCount == 0 ? 0u : static_cast<u32>(repeatCount + 1);
            const u8 slot = playback.track.loopLevel % playback.track.loops.size();
            playback.track.loops[slot] = LoopFrame{
                .start = start,
                .totalPlays = totalPlays,
                .remainingPlays = totalPlays,
                .incrementCount = playback.context.version == AKAOSNES_V4 ? u8{1} : u8{0},
            };
            playback.track.loopLevel = static_cast<u8>((playback.track.loopLevel + 1) % playback.track.loops.size());
          },
          count, event.nextAddress());
    }
    case EventType::LoopEnd:
      return cursor.command("Loop End", SequenceSemantic::Repeat).invoke<&Playback::loopEnd>().runtimeControlFlow();
    case EventType::OneTimeDuration: {
      auto event = cursor.command("Duration One-Time", SequenceSemantic::Meta);
      return event.set<&TrackState::onetimeDuration>(event.u8("duration"));
    }
    case EventType::JumpToSfxLo:
    case EventType::JumpToSfxHi: {
      auto event = cursor.unsupported("Jump To SFX");
      event.u8("sfx");
      return event.stop();
    }
    case EventType::End:
      return cursor.command("End", SequenceSemantic::End).end();
    case EventType::Tempo: {
      auto event = cursor.command("Tempo", SequenceSemantic::Tempo);
      return event.invoke<&Playback::tempoChange>(event.u8("tempo"));
    }
    case EventType::TempoFade: {
      auto event = cursor.command("Tempo Fade", SequenceSemantic::Tempo);
      const u16 length = profile.version == AKAOSNES_V1 ? event.u16le("length") : event.u8("length");
      const u8 tempo = event.u8("tempo");
      if (length != 0) {
        return event.ignore();
      }
      return event.invoke<&Playback::tempoChange>(tempo);
    }

    case EventType::EchoVolume: {
      auto event = cursor.sourceOnly("Echo Volume");
      event.u8("volume");
      return event;
    }
    case EventType::EchoVolumeFade: {
      auto event = cursor.sourceOnly("Echo Volume Fade");
      event.u8("length");
      event.u8("volume");
      return event;
    }
    case EventType::EchoFeedbackFir: {
      auto event = cursor.sourceOnly("Echo Feedback/FIR");
      event.u8("feedback");
      event.u8("fir");
      return event;
    }
    case EventType::MasterVolume: {
      auto event = cursor.command("Master Volume", SequenceSemantic::Level);
      return event.emitMasterLevel(levelFromLegacyMidiVolume(static_cast<u8>(event.u8("volume") >> 1)));
    }
    case EventType::LoopBreak: {
      auto event = cursor.command("Loop Break", SequenceSemantic::RepeatBreak);
      const u8 count = event.u8("count");
      const Address destination = relocated(event, SemanticOperandRole::JumpTarget);
      return event.invoke<&Playback::loopBreak>(count, destination)
          .mayBranchTo(destination, SemanticOperandRole::JumpTarget)
          .runtimeControlFlow();
    }
    case EventType::Goto: {
      auto event = cursor.command("Jump", SequenceSemantic::Jump);
      return event.loopCandidate(relocated(event, SemanticOperandRole::LoopTarget));
    }

    case EventType::EchoFeedbackFade:
    case EventType::EchoFirFade:
    case EventType::EchoFeedback:
    case EventType::EchoFir: {
      auto event = cursor.sourceOnly("Echo");
      event.u8("value");
      if (type == EventType::EchoFeedbackFade || type == EventType::EchoFirFade) {
        event.u8("target");
      }
      return event;
    }
    case EventType::CpuControlledSetValue: {
      auto event = cursor.sourceOnly("CPU-Controlled Set Value");
      event.u8("value");
      return event;
    }
    case EventType::CpuControlledJump: {
      auto event =
          cursor.command("CPU-Controlled Jump", SequenceSemantic::Jump, CommandPlaybackStatus::AffectsControlFlow);
      const Address destination = relocated(event, SemanticOperandRole::JumpTarget);
      return event.mayBranchTo(destination, SemanticOperandRole::JumpTarget);
    }
    case EventType::CpuControlledJumpV2: {
      auto event = cursor.command("CPU-Controlled Jump", SequenceSemantic::Jump, CommandPlaybackStatus::SourceOnly);
      event.u8("arg");
      relocated(event, SemanticOperandRole::JumpTarget);
      return event;
    }
    case EventType::PercOn:
      return cursor.command("Percussion On", SequenceSemantic::Program).invoke([](Playback& playback) {
        playback.track.percussion = true;
        playback.out.instrument(kAkaoSnesDrumKitBank << 7, kAkaoSnesDrumKitProgram);
      });
    case EventType::PercOff:
      return cursor.command("Percussion Off", SequenceSemantic::Program).invoke([](Playback& playback) {
        playback.track.percussion = false;
        playback.out.instrument(0, playback.track.nonPercussionProgram);
      });
    case EventType::VolumeAlt: {
      auto event = cursor.command("Expression", SequenceSemantic::Level);
      return event.emitExpression(levelFromLegacyMidiVolume(event.u8("volume") & 0x7f));
    }
    case EventType::IgnoreMasterVolumeByPrognum: {
      auto event = cursor.sourceOnly("Ignore Master Volume By Program");
      event.u8("program");
      return event;
    }
    case EventType::PlaySfx: {
      auto event = cursor.unsupported("Play SFX");
      event.u8("arg");
      return event;
    }
  }

  return cursor.truncated();
}

[[nodiscard]] const SequenceDialect& sharedDialect() {
  static const SequenceDialect dialect = makeCompiledDialect<TrackState, Playback, ProgramState>(SequenceDialect{
      .id = DialectId{.value = "akao-snes"},
      .commandDetailKindPrefix = "akao-snes",
      .timebase = Timebase{.ppqn = kAkaoSnesPpqn},
      .defaultBehavior =
          SequenceProgramBehavior{
              .defaultLoopPolicy = LoopPolicy::PlayOnce,
              .initialReverbSend = 0.0,
              .stopAllTracksAtFirstLoop = false,
          },
      .semanticPrepass = SemanticPrepassMode::ScheduledPlayback,
  });
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

}  // namespace

const SequenceDialect& akaoSnesSequenceDialect() {
  return sharedDialect();
}

TrackProgram decodeAkaoSnesSourceTrack(ByteReader reader, const AkaoSnesTrackDecodeOptions& options) {
  TrackDecodeInput input{
      .sequenceAsset = options.sequenceAsset,
      .trackIndex = options.sourceTrackNumber,
      .startOffset = options.startAddress,
      .bytecodeEnd = options.bytecodeEnd,
      .sequenceOffset = options.startAddress,
      .sequenceEnd = options.bytecodeEnd,
      .parentAnnotation = options.parentAnnotation,
      .sourceMap = options.sourceMap,
      .diagnostics = options.diagnostics,
      .maxCommands = 16384,
  };
  return makeTrackDecodeScope(reader, input).reachable(input.trackIndex, input.startOffset, [&](u32 offset) {
    return decodeCommand(reader, offset, options.bytecodeEnd, options.profile, options.romRelocBase,
                         options.apuRelocBase, options.diagnostics);
  });
}

SequenceProgram parseAkaoSnesSequence(ByteReader reader, const AkaoSnesLayout& layout, AssetId sequenceId,
                                      SourceMapBuilder* sourceMap, std::vector<Diagnostic>* diagnostics) {
  const SequenceHeaderInfo header = sequenceHeaderInfo(reader, layout);
  const SourceRange headerRange = reader.range(header.headerOffset, header.headerSize);
  SequenceDecodeSession session(reader, sharedDialect(), sequenceId, headerRange, sourceMap, 16384, header.sequenceEnd);
  if (sourceMap != nullptr) {
    if (const auto annotation = session.headerAnnotation()) {
      auto source = AnnotationBuilder{*sourceMap, *annotation}
                        .derived("version", akaoSnesVersionName(layout.version))
                        .derived("minor_version", akaoSnesMinorVersionName(layout.minorVersion))
                        .derived("apu_reloc_base", header.apuRelocBase, SourceValueDisplay::Address);
      if (akaoSnesRelocatable(layout.version) && reader.has(header.headerOffset, 2)) {
        source.field("rom_reloc_base", reader.range(header.headerOffset, 2), reader.le16(header.headerOffset),
                     SourceValueDisplay::Address);
        const u32 endPointerOffset = layout.version == AKAOSNES_V4 ? header.headerOffset + 2
                                                                   : header.trackPointerOffset + kAkaoSnesMaxTracks * 2;
        if (reader.has(endPointerOffset, 2)) {
          source.field("stored_sequence_end", reader.range(endPointerOffset, 2), reader.le16(endPointerOffset),
                       SourceValueDisplay::Address);
        }
      }
      source.derived("sequence_end", header.sequenceEnd, SourceValueDisplay::Address);
    }
  }
  const AkaoSnesProfile profile{.version = layout.version, .minorVersion = layout.minorVersion};

  for (u32 trackNumber = 0; trackNumber < kAkaoSnesMaxTracks; ++trackNumber) {
    const u32 pointerOffset = header.trackPointerOffset + trackNumber * 2;
    if (!reader.has(pointerOffset, 2)) {
      break;
    }
    const u16 rawTrackAddress = reader.le16(pointerOffset);
    const u16 trackAddress = relocatedAddress(rawTrackAddress, header.romRelocBase, header.apuRelocBase);
    const bool rawZeroIsNull = layout.version == AKAOSNES_V1 || layout.version == AKAOSNES_V2;
    if ((rawZeroIsNull && rawTrackAddress == 0) || trackAddress == header.sequenceEnd) {
      continue;
    }

    session.addReachableTrack(
        trackNumber, reader.range(pointerOffset, 2), trackAddress,
        [&](u32 offset) {
          return decodeCommand(reader, offset, header.sequenceEnd, profile, header.romRelocBase, header.apuRelocBase,
                               diagnostics);
        },
        rawTrackAddress);
  }

  SequenceProgram program = session.finish();
  program.sourceBaseAddress = Address{layout.sequenceHeaderAddress};
  program.config.profile = encodeAkaoSnesProfile(profile);

  return program;
}

}  // namespace vgmtrans::formats::akao_snes
