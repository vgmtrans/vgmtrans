/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/AkaoSnes/AkaoSnes.h"

#include "value/base/LevelScale.h"
#include "value/sequence/BytecodeDecode.h"
#include "value/sequence/CommandSourceMap.h"
#include "value/sequence/CompiledCommandDialect.h"
#include "value/sequence/SequenceLfo.h"
#include "value/sequence/SequenceMotion.h"
#include "value/synth/SnesDsp.h"

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
constexpr u8 kDefaultTempo = 0x20;
constexpr u8 kNoteVelocity = 100;

[[nodiscard]] constexpr bool usesDynamicAdsr(AkaoSnesProfile profile) {
  return profile.version == AKAOSNES_V3;
}

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

[[nodiscard]] u8 normalizeTempoValue(AkaoSnesMinorVersion minorVersion, u8 rawTempo) {
  if (minorVersion == AKAOSNES_V4_FM || minorVersion == AKAOSNES_V4_CT) {
    rawTempo = static_cast<u8>(rawTempo + ((rawTempo * 0x14) >> 8));
  }
  return rawTempo;
}

[[nodiscard]] double levelFromLegacyMidiVolume(u8 volume) {
  return std::clamp(static_cast<double>(volume) / 127.0, 0.0, 1.0);
}

[[nodiscard]] double channelLevel(u8 volume) {
  return levelFromLegacyMidiVolume(static_cast<u8>(volume >> 1));
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
    return modulationMagnitude(version, depth);
  }
  return v4PhaseHighByteAmplitude(rate, depth);
}

[[nodiscard]] ModulationRange v3VibratoPitchRangeSemitones(u8 depth) {
  if (depth == 0) {
    return {};
  }

  const double amplitude = modulationMagnitude(AKAOSNES_V3, depth);
  const double relativeExcursion = 15.0 * amplitude / 32768.0;
  const double upwardSemitones = 12.0 * std::log2(1.0 + relativeExcursion);
  const double downwardSemitones = 12.0 * std::log2(1.0 - relativeExcursion);

  if ((depth & 0x40) == 0) {
    return ModulationRange{.minimum = downwardSemitones, .maximum = 0.0};
  }
  if ((depth & 0x80) == 0) {
    return ModulationRange{.minimum = 0.0, .maximum = upwardSemitones};
  }
  return ModulationRange{.minimum = downwardSemitones, .maximum = upwardSemitones};
}

[[nodiscard]] u8 v1VibratoHighByteAmplitude(u8 rate, u8 depth) {
  const u8 counter = v1RateCounter(rate);
  if (counter == 0 || depth == 0) {
    return 0;
  }
  const u32 step = (256u * depth) / counter;
  return static_cast<u8>((step * counter) / 256u);
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
    return akaoSnesV1VibratoDepthCents(v1VibratoHighByteAmplitude(rate, depth));
  }
  if (version == AKAOSNES_V2) {
    return v2VibratoDepthCents(rate, depth);
  }
  if (version == AKAOSNES_V3) {
    const ModulationRange range = v3VibratoPitchRangeSemitones(depth);
    return 100.0 * std::max(std::abs(range.minimum), std::abs(range.maximum));
  }
  return akaoSnesVibratoDepthCentsForAmplitude(modulationAmplitude(version, rate, depth));
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

[[nodiscard]] u32 driverFramesToTicks(double frames, u8 tempo) {
  const u8 safeTempo = tempo == 0 ? 1 : tempo;
  return std::max<u32>(1, static_cast<u32>(std::lround(frames * safeTempo / 256.0)));
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

[[nodiscard]] double vibratoDepthSemitones(AkaoSnesVersion version, u8 rate, u8 depth) {
  if (!isLfoActive(version, rate, depth)) {
    return 0.0;
  }
  return vibratoDepthCents(version, rate, depth) / 100.0;
}

[[nodiscard]] double tremoloDepthDecibels(AkaoSnesVersion version, u8 rate, u8 depth, u8 delay = 0) {
  if (!akaoSnesExportsTremolo(version) || !isLfoActive(version, rate, depth)) {
    return 0.0;
  }
  if (version == AKAOSNES_V3) {
    constexpr double kV3SteppedTremoloSmoothLfoCompensation = 2.0;
    return kV3SteppedTremoloSmoothLfoCompensation * v3TremoloPeakToTroughDb(depth);
  }
  const double amplitude =
      delay != 0 ? v4PhaseHighByteAmplitude(rate, depth) / 4.0 : v4PhaseHighByteAmplitude(rate, depth);
  return akaoSnesTremoloDepthDbForAmplitude(amplitude);
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

template <size_t Size>
[[nodiscard]] EventType tableEventType(u8 opcode, u8 firstOpcode, const std::array<EventType, Size>& table) {
  return opcode >= firstOpcode && opcode - firstOpcode < table.size() ? table[opcode - firstOpcode] : EventType::End;
}

constexpr std::array<EventType, 46> kV1EventTypes{
    EventType::TempoFade,
    EventType::Nop1,
    EventType::EchoVolume,
    EventType::EchoFeedbackFir,
    EventType::PitchEnvelopeOn,
    EventType::TremoloOn,
    EventType::VibratoOn,
    EventType::PanLfoOnWithDelay,
    EventType::Octave,
    EventType::ProgramChange,
    EventType::VolumeEnvelope,
    EventType::GainRelease,
    EventType::DurationRate,
    EventType::NoiseFreq,
    EventType::LoopStart,
    EventType::OctaveUp,
    EventType::OctaveDown,
    EventType::Nop,
    EventType::Nop,
    EventType::Nop,
    EventType::PitchEnvelopeOff,
    EventType::TremoloOff,
    EventType::VibratoOff,
    EventType::PanLfoOff,
    EventType::EchoOn,
    EventType::EchoOff,
    EventType::NoiseOn,
    EventType::NoiseOff,
    EventType::PitchModOn,
    EventType::PitchModOff,
    EventType::LoopEnd,
    EventType::End,
    EventType::VolumeFade,
    EventType::PanFade,
    EventType::Goto,
    EventType::LoopBreak,
    EventType::Unknown0,
    EventType::End,
    EventType::End,
    EventType::End,
    EventType::End,
    EventType::End,
    EventType::End,
    EventType::End,
    EventType::End,
    EventType::End,
};

constexpr std::array<EventType, 46> kV2EventTypes{
    EventType::Tempo,
    EventType::TempoFade,
    EventType::Volume,
    EventType::VolumeFade,
    EventType::Pan,
    EventType::PanFade,
    EventType::EchoVolume,
    EventType::EchoVolumeFade,
    EventType::TransposeAbs,
    EventType::PitchEnvelopeOn,
    EventType::PitchEnvelopeOff,
    EventType::VibratoOn,
    EventType::VibratoOff,
    EventType::TremoloOn,
    EventType::TremoloOff,
    EventType::NoiseFreq,
    EventType::NoiseOn,
    EventType::NoiseOff,
    EventType::PitchModOn,
    EventType::PitchModOff,
    EventType::EchoFeedbackFir,
    EventType::EchoOn,
    EventType::EchoOff,
    EventType::PanLfoOn,
    EventType::PanLfoOff,
    EventType::Octave,
    EventType::OctaveUp,
    EventType::OctaveDown,
    EventType::LoopStart,
    EventType::LoopEnd,
    EventType::LoopBreak,
    EventType::Goto,
    EventType::SlurOn,
    EventType::ProgramChange,
    EventType::VolumeEnvelope,
    EventType::SlurOff,
    EventType::Unknown2,
    EventType::Tuning,
    EventType::End,
    EventType::End,
    EventType::End,
    EventType::End,
    EventType::End,
    EventType::End,
    EventType::End,
    EventType::End,
};

constexpr std::array<EventType, 46> kV3EventTypes{
    EventType::Volume,
    EventType::VolumeFade,
    EventType::Pan,
    EventType::PanFade,
    EventType::PitchSlide,
    EventType::VibratoOn,
    EventType::VibratoOff,
    EventType::TremoloOn,
    EventType::TremoloOff,
    EventType::PanLfoOn,
    EventType::PanLfoOff,
    EventType::NoiseFreq,
    EventType::NoiseOn,
    EventType::NoiseOff,
    EventType::PitchModOn,
    EventType::PitchModOff,
    EventType::EchoOn,
    EventType::EchoOff,
    EventType::Octave,
    EventType::OctaveUp,
    EventType::OctaveDown,
    EventType::TransposeAbs,
    EventType::TransposeRel,
    EventType::Tuning,
    EventType::ProgramChange,
    EventType::AdsrAr,
    EventType::AdsrDr,
    EventType::AdsrSl,
    EventType::AdsrSr,
    EventType::AdsrDefault,
    EventType::LoopStart,
    EventType::LoopEnd,
    EventType::End,
    EventType::Tempo,
    EventType::TempoFade,
    EventType::EchoVolume,
    EventType::EchoVolumeFade,
    EventType::EchoFeedbackFir,
    EventType::MasterVolume,
    EventType::LoopBreak,
    EventType::Goto,
    EventType::CpuControlledJump,
    EventType::End,
    EventType::End,
    EventType::End,
    EventType::End,
};

constexpr std::array<EventType, 48> kV4CommonEventTypes{
    EventType::Volume,
    EventType::VolumeFade,
    EventType::Pan,
    EventType::PanFade,
    EventType::PitchSlide,
    EventType::VibratoOn,
    EventType::VibratoOff,
    EventType::TremoloOn,
    EventType::TremoloOff,
    EventType::PanLfoOn,
    EventType::PanLfoOff,
    EventType::NoiseFreq,
    EventType::NoiseOn,
    EventType::NoiseOff,
    EventType::PitchModOn,
    EventType::PitchModOff,
    EventType::EchoOn,
    EventType::EchoOff,
    EventType::Octave,
    EventType::OctaveUp,
    EventType::OctaveDown,
    EventType::TransposeAbs,
    EventType::TransposeRel,
    EventType::Tuning,
    EventType::ProgramChange,
    EventType::AdsrAr,
    EventType::AdsrDr,
    EventType::AdsrSl,
    EventType::AdsrSr,
    EventType::AdsrDefault,
    EventType::LoopStart,
    EventType::LoopEnd,
    EventType::SlurOn,
    EventType::SlurOff,
    EventType::LegatoOn,
    EventType::LegatoOff,
    EventType::OneTimeDuration,
    EventType::JumpToSfxLo,
    EventType::JumpToSfxHi,
    EventType::End,
    EventType::End,
    EventType::End,
    EventType::End,
    EventType::End,
    EventType::Tempo,
    EventType::TempoFade,
    EventType::EchoVolume,
    EventType::EchoVolumeFade,
};

constexpr std::array<EventType, 12> kV4Rs2EventTypes{
    EventType::EchoFeedbackFir,
    EventType::MasterVolume,
    EventType::LoopBreak,
    EventType::Goto,
    EventType::IncCpuSharedCounter,
    EventType::ZeroCpuSharedCounter,
    EventType::IgnoreMasterVolumeBroken,
    EventType::End,
    EventType::End,
    EventType::End,
    EventType::End,
    EventType::End,
};
constexpr std::array<EventType, 12> kV4LalEventTypes{
    EventType::EchoFeedbackFir,
    EventType::MasterVolume,
    EventType::LoopBreak,
    EventType::Goto,
    EventType::IncCpuSharedCounter,
    EventType::ZeroCpuSharedCounter,
    EventType::IgnoreMasterVolume,
    EventType::CpuControlledJump,
    EventType::End,
    EventType::End,
    EventType::End,
    EventType::End,
};
constexpr std::array<EventType, 12> kV4Ff6EventTypes{
    EventType::MasterVolume,
    EventType::LoopBreak,
    EventType::Goto,
    EventType::EchoFeedbackFade,
    EventType::EchoFirFade,
    EventType::IncCpuSharedCounter,
    EventType::ZeroCpuSharedCounter,
    EventType::IgnoreMasterVolume,
    EventType::CpuControlledJump,
    EventType::End,
    EventType::End,
    EventType::End,
};
constexpr std::array<EventType, 12> kV4FmEventTypes{
    EventType::MasterVolume,        EventType::LoopBreak,   EventType::Goto,
    EventType::EchoFeedbackFade,    EventType::EchoFirFade, EventType::Unknown1,
    EventType::CpuControlledJumpV2, EventType::PercOn,      EventType::PercOff,
    EventType::VolumeAlt,           EventType::End,         EventType::End,
};
constexpr std::array<EventType, 12> kV4CtEventTypes{
    EventType::MasterVolume,        EventType::LoopBreak,   EventType::Goto,
    EventType::EchoFeedbackFade,    EventType::EchoFirFade, EventType::CpuControlledSetValue,
    EventType::CpuControlledJumpV2, EventType::PercOn,      EventType::PercOff,
    EventType::VolumeAlt,           EventType::End,         EventType::End,
};
constexpr std::array<EventType, 12> kV4Rs3EventTypes{
    EventType::VolumeAlt,
    EventType::LoopBreak,
    EventType::Goto,
    EventType::EchoFeedback,
    EventType::EchoFir,
    EventType::CpuControlledSetValue,
    EventType::CpuControlledJumpV2,
    EventType::PercOn,
    EventType::PercOff,
    EventType::PlaySfx,
    EventType::End,
    EventType::End,
};
constexpr std::array<EventType, 12> kV4GhEventTypes{
    EventType::VolumeAlt,
    EventType::LoopBreak,
    EventType::Goto,
    EventType::EchoFeedback,
    EventType::EchoFir,
    EventType::CpuControlledSetValue,
    EventType::CpuControlledJumpV2,
    EventType::PercOn,
    EventType::PercOff,
    EventType::End,
    EventType::End,
    EventType::End,
};
constexpr std::array<EventType, 12> kV4BsGameEventTypes{
    EventType::VolumeAlt,
    EventType::LoopBreak,
    EventType::Goto,
    EventType::EchoFeedback,
    EventType::EchoFir,
    EventType::CpuControlledSetValue,
    EventType::CpuControlledJumpV2,
    EventType::PercOn,
    EventType::PercOff,
    EventType::Unknown1,
    EventType::Unknown0,
    EventType::End,
};

[[nodiscard]] EventType v4EventType(AkaoSnesMinorVersion minorVersion, u8 opcode) {
  if (opcode < 0xf4) {
    if (opcode == 0xeb && minorVersion == AKAOSNES_V4_GH) {
      return EventType::Unknown1;
    }
    return tableEventType(opcode, 0xc4, kV4CommonEventTypes);
  }

  switch (minorVersion) {
    case AKAOSNES_V4_RS2:
      return tableEventType(opcode, 0xf4, kV4Rs2EventTypes);
    case AKAOSNES_V4_LAL:
      return tableEventType(opcode, 0xf4, kV4LalEventTypes);
    case AKAOSNES_V4_FF6:
      return tableEventType(opcode, 0xf4, kV4Ff6EventTypes);
    case AKAOSNES_V4_FM:
      return tableEventType(opcode, 0xf4, kV4FmEventTypes);
    case AKAOSNES_V4_CT:
      return tableEventType(opcode, 0xf4, kV4CtEventTypes);
    case AKAOSNES_V4_RS3:
      return tableEventType(opcode, 0xf4, kV4Rs3EventTypes);
    case AKAOSNES_V4_GH:
      return tableEventType(opcode, 0xf4, kV4GhEventTypes);
    case AKAOSNES_V4_BSGAME:
      return tableEventType(opcode, 0xf4, kV4BsGameEventTypes);
    default:
      return EventType::End;
  }
}

[[nodiscard]] EventType eventType(AkaoSnesVersion version, AkaoSnesMinorVersion minorVersion, u8 opcode) {
  if (opcode <= akaoSnesStatusNoteMax(version)) {
    return EventType::Note;
  }
  if (version == AKAOSNES_V3 && minorVersion == AKAOSNES_V3_SD2) {
    if (opcode == 0xfc) {
      return EventType::LoopRestart;
    }
    if (opcode == 0xfd) {
      return EventType::IgnoreMasterVolumeByPrognum;
    }
  }

  switch (version) {
    case AKAOSNES_V1:
      return tableEventType(opcode, 0xd2, kV1EventTypes);
    case AKAOSNES_V2:
      return tableEventType(opcode, 0xd2, kV2EventTypes);
    case AKAOSNES_V3:
      return tableEventType(opcode, 0xd2, kV3EventTypes);
    case AKAOSNES_V4:
      return v4EventType(minorVersion, opcode);
    case AKAOSNES_NONE:
    default:
      return EventType::End;
  }
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

struct LfoState {
  void configure(u8 delayValue, u8 rateValue, u8 depthValue) {
    delay = delayValue;
    rate = rateValue;
    depth = depthValue;
    depthState.resetDepth(static_cast<s32>(depth) << 8);
  }

  [[nodiscard]] s32 targetDepthFixed() const { return depthState.targetDepth(); }
  [[nodiscard]] s32 currentDepthFixed() const { return depthState.currentDepth(); }

  u8 delay = 0;
  u8 rate = 0;
  u8 depth = 0;
  SequenceLfoDepthFadeState depthState;
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
        const Address fallthrough = command.flow.continuation;
        const auto nextIndex = track.addressIndex.find(fallthrough);
        if (!nextIndex) {
          continue;
        }
        const SourceCommand& next = track.commands[*nextIndex];
        if (next.flow.endsPlayback()) {
          terminalPitchBoundaries.insert(fallthrough.value);
          continue;
        }
        const SemanticOperand* envelopeOff = semanticOperand(next, "pitch_envelope_off");
        const bool* clearsEnvelope = envelopeOff == nullptr ? nullptr : std::get_if<bool>(&envelopeOff->value);
        if (clearsEnvelope == nullptr || !*clearsEnvelope) {
          continue;
        }
        const Address afterOff = next.flow.continuation;
        const auto afterIndex = track.addressIndex.find(afterOff);
        if (!afterIndex) {
          continue;
        }
        const SourceCommand& after = track.commands[*afterIndex];
        if (after.flow.endsPlayback()) {
          terminalPitchBoundaries.insert(fallthrough.value);
          continue;
        }
        if (!after.flow.unconditionalJump()) {
          continue;
        }
        if (after.flow.defaultDestination()->value <= command.address.value) {
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

  [[nodiscard]] const SharedTempoChange* initialTempo() const {
    return !tempoChanges.empty() && tempoChanges.front().tick == 0 ? &tempoChanges.front() : nullptr;
  }

  [[nodiscard]] std::optional<u8> tempoAt(u64 tick) const {
    if (collecting) {
      const SharedTempoChange* latest = nullptr;
      for (const auto& change : tempoChanges) {
        if (change.tick <= tick && (latest == nullptr || change.tick > latest->tick ||
                                    (change.tick == latest->tick && change.order > latest->order))) {
          latest = &change;
        }
      }
      return latest == nullptr ? std::nullopt : std::optional<u8>{latest->tempo};
    }
    const auto next = std::ranges::upper_bound(tempoChanges, tick, {}, &SharedTempoChange::tick);
    return next == tempoChanges.begin() ? std::nullopt : std::optional<u8>{(next - 1)->tempo};
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
  TrackState(const SequenceProgram& program, const TrackProgram& track)
      : trackNumber(track.sourceTrackNumber),
        pan8Bit(akaoSnesUses8BitPan(decodeAkaoSnesProfile(program.config.profile))) {
    volume.reset(0xff);
    pan.reset(0x80);
    tempoState.reset(kDefaultTempo);
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
    if (!isLfoActive(version, vibrato.rate, vibrato.depth)) {
      vibrato.depthState.clearFade();
      return;
    }
    if (version == AKAOSNES_V1) {
      vibrato.depthState.configureLinearFade(v1VibratoRampTicks(vibrato.rate, tempo));
      return;
    }
    if (version == AKAOSNES_V4 && vibrato.delay != 0) {
      const u32 ticks = v4VibratoRampTicks(vibrato.rate, tempo);
      const s32 targetDepth = vibrato.targetDepthFixed();
      const s32 initialDepth = targetDepth / 4;
      const s32 step = ticks == 0 ? 0 : (targetDepth - initialDepth) / static_cast<s32>(ticks);
      vibrato.depthState.configureFade(ticks, step);
      return;
    }
    vibrato.depthState.clearFade();
  }

  void configureTremoloFade(AkaoSnesVersion version) {
    if (version != AKAOSNES_V3 || !isLfoActive(version, tremolo.rate, tremolo.depth) || tremolo.delay == 0) {
      tremolo.depthState.clearFade();
      return;
    }
    tremolo.depthState.configureLinearFade(v3LfoRampTicks(tremolo.rate, tempo));
  }

  [[nodiscard]] double vibratoFadeDepthSemitones(AkaoSnesVersion version, s32 depth) const {
    const s32 targetDepth = vibrato.targetDepthFixed();
    if (targetDepth <= 0) {
      return 0.0;
    }
    return vibratoDepthSemitones(version, vibrato.rate, vibrato.depth) *
           std::clamp(static_cast<double>(depth) / targetDepth, 0.0, 1.0);
  }

  [[nodiscard]] double tremoloFadeDepthDecibels(AkaoSnesVersion version, s32 depth) const {
    const s32 targetDepth = tremolo.targetDepthFixed();
    if (targetDepth <= 0) {
      return 0.0;
    }
    return tremoloDepthDecibels(version, tremolo.rate, tremolo.depth) *
           std::clamp(static_cast<double>(depth) / targetDepth, 0.0, 1.0);
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
  u8 tempo = kDefaultTempo;
  bool pan8Bit = true;
  bool sharedTempoApplied = false;
  PerformanceBoundValue<SequenceFixedPointAutomation<s32>> volume;
  PerformanceBoundValue<SequenceFixedPointAutomation<s32>> pan;
  PerformanceBoundValue<SequenceFixedPointAutomation<s32>> tempoState;
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
  s16 pitchSlideBaseNote = 0;
  s16 pitchSlideCurrentNote = 0;
  double pitchSlideBaseKey = 0.0;
  PerformanceNoteId pitchSlideNote;
  PitchSlideBinding pitchSlideAutomation;
  PerformanceAutomationBinding pitchEnvelopeAutomation;
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

  void emitVolume(PerformanceEmitter output, u8 volume) { output.level(channelLevel(volume)); }

  void volume(u8 value) {
    track.volume.setCurrentAt(vm.tick(), value);
    emitVolume(out, value);
  }

  void programChange(u8 programNumber) {
    track.nonPercussionProgram = programNumber;
    if (track.percussion) {
      return;
    }
    out.instrument(0, programNumber);
  }

  void emitPan(PerformanceEmitter output, u8 panValue) {
    const double rightGain = rightGainFromPan(panValue);
    output.stereoBalance(1.0 - rightGain, rightGain);
  }

  void pan(u8 rawPan) {
    const u8 panValue = static_cast<u8>(rawPan << (track.pan8Bit ? 0 : 1));
    track.pan.setCurrentAt(vm.tick(), panValue);
    emitPan(out, panValue);
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

  void emitPitchBendForCurrentPitch(PerformanceEmitter output) {
    const s16 value = akaoSnesPitchBendValue(track.currentPitch, track.pitchBase, track.currentPitchBendRangeCents);
    const double semitones = akaoSnesPitchCents(track.currentPitch, track.pitchBase) / 100.0;
    if (track.currentPitchBendValue == value) {
      return;
    }
    output.pitchBend(semitones);
    track.currentPitchBendValue = value;
  }

  void resetPitchBendForNewNote() {
    track.pitchBaseValid = false;
    track.pitchSlideActive = false;
    track.pitchSlideStepsRemaining = 0;
    track.pitchSlideNote = {};
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
    track.pitchSlideBaseKey = static_cast<double>(note) + track.transpose;
    beginPitchEnvelopeForNote();
  }

  [[nodiscard]] double pitchSlideKey(s32 pitch) const {
    return track.pitchSlideBaseKey + (akaoSnesPitchCents(pitch, track.pitchBase) / 100.0);
  }

  void samplePitchSlide() { track.pitchSlideAutomation.sample(out, pitchSlideKey(track.currentPitch)); }

  [[nodiscard]] bool advancePitchSlide() {
    if (!track.pitchSlideActive || !track.pitchBaseValid) {
      return false;
    }
    if (track.pitchSlideStepsRemaining == 0) {
      track.pitchSlideActive = false;
      return false;
    }
    --track.pitchSlideStepsRemaining;
    track.currentPitch =
        track.pitchSlideStepsRemaining == 0 ? track.pitchSlideFinalPitch : track.currentPitch + track.pitchSlideStep;
    if (track.pitchSlideStepsRemaining == 0) {
      track.pitchSlideActive = false;
    }
    return true;
  }

  void beginPendingPitchSlide() {
    if (track.pendingPitchSlideSteps == 0 || track.pendingPitchSlideSemitones == 0) {
      track.clearPendingPitchSlide();
      return;
    }
    const u16 steps = track.pendingPitchSlideSteps;
    const s8 semitones = track.pendingPitchSlideSemitones;
    track.clearPendingPitchSlide();
    if (!track.pitchBaseValid || !track.pitchSlideNote.valid()) {
      return;
    }
    track.pitchSlideCurrentNote = static_cast<s16>(track.pitchSlideCurrentNote + semitones);
    const s32 targetPitch = akaoSnesPitchForSemitoneOffset(track.pitchSlideCurrentNote - track.pitchSlideBaseNote);
    track.pitchSlideStep = akaoSnesPitchSlideStep(context.version, track.currentPitch, targetPitch, steps);
    track.pitchSlideFinalPitch = track.currentPitch + (track.pitchSlideStep * static_cast<s32>(steps));
    track.pitchSlideStepsRemaining = steps;
    track.pitchSlideActive = true;
    if (!advancePitchSlide()) {
      return;
    }
    // The first driver step is already audible at the note tick, leaving
    // steps - 1 timeline ticks between that pitch and the destination.
    if (steps == 1) {
      track.pitchSlideAutomation.interrupt(out);
      emitPitchBendRange(akaoSnesPitchBendRangeCents(track.pitchBase, track.currentPitch, kDefaultPitchBendRangeCents));
      emitPitchBendForCurrentPitch(out);
      return;
    }
    track.pitchSlideAutomation = out.pitchSlide(track.pitchSlideNote, pitchSlideKey(track.currentPitch),
                                                pitchSlideKey(track.pitchSlideFinalPitch), steps - 1);
    track.pitchSlideAutomation.preferPitchBend();
    samplePitchSlide();
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
    emitPitchBendForCurrentPitch(track.pitchEnvelopeAutomation.output(out));
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
      const double velocity = kNoteVelocity / 127.0;
      const u8 note = static_cast<u8>((track.octave * 12) + noteIndex);
      beginNotePitch(note, !track.percussion);
      if (!track.slur && !track.legato) {
        beginVibratoForNote();
        beginTremoloForNote();
      }
      if (track.percussion) {
        out.note(kAkaoSnesDrumKeyBias + noteIndex - track.transpose, velocity, duration);
      } else {
        const PerformanceNoteId pitchNote =
            out.note((track.octave * 12) + noteIndex + track.transpose, velocity, duration);
        track.pitchSlideNote = pitchNote;
        beginPendingPitchSlide();
      }
      track.lastTieableNoteTick = vm.tick() + length;
      return Effects::wait(length);
    }

    if (noteIndex == akaoSnesStatusNoteIndexTie(context.version)) {
      if (track.lastTieableNoteTick && *track.lastTieableNoteTick >= vm.tick()) {
        track.pitchSlideNote = out.note(0.0, 1.0, duration, true);
        track.lastTieableNoteTick = vm.tick() + length;
      }
      beginPendingPitchSlide();
      return Effects::wait(length);
    }

    track.lastTieableNoteTick.reset();
    return Effects::wait(length);
  }

  [[nodiscard]] LfoPerformanceContext vibratoLfoContext() const {
    if (context.version != AKAOSNES_V3) {
      return {};
    }
    return LfoPerformanceContext{
        .waveform = LfoWaveform::Square,
        .initialPhaseCycles = (track.vibrato.depth & 0x40) == 0 ? 0.5 : 0.0,
        .pitchRangeSemitones = v3VibratoPitchRangeSemitones(track.vibrato.depth),
        .steppedDepthAttackSteps = track.vibrato.delay == 0 ? 0u : 4u,
        .sampleImmediatelyOnNote = true,
    };
  }

  void emitVibratoDepth(PerformanceEmitter output, double semitones, bool force = false) {
    track.vibrato.depthState.emitPhysicalDepth(
        semitones, [&](double value) { output.vibratoDepth(value, vibratoLfoContext()); }, force);
  }

  void emitTremoloDepth(PerformanceEmitter output, double decibels, bool force = false) {
    track.tremolo.depthState.emitPhysicalDepth(decibels, [&](double value) { output.tremoloDepth(value); }, force);
  }

  void setLfoOutputDepth(PerformanceEmitter output, LfoTarget target, double depth, bool force = false) {
    if (target == LfoTarget::Vibrato) {
      emitVibratoDepth(output, depth, force);
    } else {
      emitTremoloDepth(output, depth, force);
    }
  }

  void clearLfoRateAndDelay(LfoTarget target) {
    if (target == LfoTarget::Vibrato) {
      out.vibratoRate(0.0);
      out.vibratoDelay(0, 0);
    } else {
      out.tremoloRate(0.0);
      out.tremoloDelay(0, 0);
    }
  }

  void syncLfoRateAndDelay(LfoTarget target) {
    const bool isVibrato = target == LfoTarget::Vibrato;
    LfoState& lfo = isVibrato ? track.vibrato : track.tremolo;
    if (!isLfoActive(context.version, lfo.rate, lfo.depth)) {
      return;
    }
    if (isVibrato) {
      track.configureVibratoFade(context.version);
    } else {
      track.configureTremoloFade(context.version);
    }
    const u8 timer = akaoSnesTimer0Frequency(context.version, context.minorVersion);
    const double rateHertz = lfoRateHz(context.version, lfo.rate, lfo.depth, timer);
    const u32 delayTicks = lfoDelayTicks(context.version, lfo.delay);
    if (isVibrato) {
      out.vibratoRate(rateHertz, vibratoLfoContext());
      out.vibratoDelayTicks(delayTicks);
    } else {
      out.tremoloRate(rateHertz);
      out.tremoloDelayTicks(delayTicks);
    }
  }

  void setLfo(LfoTarget target, u8 delay, u8 rate, u8 depth) {
    const bool isVibrato = target == LfoTarget::Vibrato;
    const bool active =
        (isVibrato || akaoSnesExportsTremolo(context.version)) && isLfoActive(context.version, rate, depth);
    LfoState& lfo = isVibrato ? track.vibrato : track.tremolo;
    lfo.depthState.interruptFadeAutomationAt(vm.tick());
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
    double physicalDepth = 0.0;
    if (active) {
      physicalDepth = isVibrato ? vibratoDepthSemitones(context.version, rate, depth)
                                : tremoloDepthDecibels(context.version, rate, depth, delay);
    }
    if (isVibrato && context.version == AKAOSNES_V4 && active) {
      const u32 delayTicks = lfoDelayTicks(context.version, track.vibrato.delay);
      const s32 initialDepth = track.vibrato.targetDepthFixed() / 4;
      if (track.vibrato.depthState.restartFade(delayTicks, initialDepth)) {
        physicalDepth = delayTicks == 0 ? track.vibratoFadeDepthSemitones(context.version, initialDepth) : 0.0;
      }
    }
    if (active && lfo.depthState.fadeConfigured()) {
      lfo.depthState.bindFade(out.noteEnvelope(
          isVibrato ? PerformanceAutomationTarget::VibratoDepth : PerformanceAutomationTarget::TremoloDepth,
          isVibrato ? vibratoDepthSemitones(context.version, rate, depth)
                    : tremoloDepthDecibels(context.version, rate, depth, delay),
          lfo.depthState.fadeDurationTicks(), lfoDelayTicks(context.version, delay)));
    }
    setLfoOutputDepth(lfo.depthState.fadeOutput(out), target, physicalDepth, true);
    if (active) {
      syncLfoRateAndDelay(target);
      if (vm.tick() == 0 && initialTempo && beforeInitialTempoTrack && initialTempo->tempo != track.tempo) {
        track.tempo = initialTempo->tempo;
        track.sharedTempoApplied = true;
        if (isVibrato) {
          track.configureVibratoFade(context.version);
        } else {
          track.configureTremoloFade(context.version);
        }
      }
    } else {
      clearLfoRateAndDelay(target);
    }
  }

  void clearLfo(LfoTarget target) {
    LfoState& lfo = target == LfoTarget::Vibrato ? track.vibrato : track.tremolo;
    lfo.depth = 0;
    lfo.depthState.interruptFadeAutomationAt(vm.tick());
    lfo.depthState.resetDepth(0);
    setLfoOutputDepth(lfo.depthState.fadeOutput(out), target, 0, true);
  }

  void beginVibratoForNote() {
    if (context.version == AKAOSNES_V2 || !isLfoActive(context.version, track.vibrato.rate, track.vibrato.depth)) {
      return;
    }
    const u32 delay = lfoDelayTicks(context.version, track.vibrato.delay);
    const s32 initialDepth = context.version == AKAOSNES_V4 ? track.vibrato.targetDepthFixed() / 4 : 0;
    if (!track.vibrato.depthState.restartFade(delay, initialDepth)) {
      return;
    }
    emitVibratoDepth(track.vibrato.depthState.fadeOutput(out),
                     delay == 0 ? track.vibratoFadeDepthSemitones(context.version, initialDepth) : 0.0, true);
  }

  void beginTremoloForNote() {
    if (context.version != AKAOSNES_V3 || !track.tremolo.depthState.restartFade(track.tremolo.delay)) {
      return;
    }
    emitTremoloDepth(track.tremolo.depthState.fadeOutput(out), 0, true);
  }

  void updateVibratoFade() {
    if (context.version == AKAOSNES_V2) {
      return;
    }
    const auto fadeTick = track.vibrato.depthState.tickFade();
    if (fadeTick.shouldApply()) {
      emitVibratoDepth(track.vibrato.depthState.fadeOutput(out),
                       track.vibratoFadeDepthSemitones(context.version, track.vibrato.currentDepthFixed()));
    }
  }

  void updateTremoloFade() {
    if (context.version != AKAOSNES_V3) {
      return;
    }
    const auto fadeTick = track.tremolo.depthState.tickFade();
    if (fadeTick.shouldApply()) {
      emitTremoloDepth(track.tremolo.depthState.fadeOutput(out),
                       track.tremoloFadeDepthDecibels(context.version, track.tremolo.currentDepthFixed()));
    }
  }

  [[nodiscard]] u8 normalizedTempo(u8 rawTempo) const { return normalizeTempoValue(context.minorVersion, rawTempo); }

  void applyTempo(PerformanceEmitter output, u8 tempo) {
    track.tempo = tempo;
    program.observeTempo(track.trackNumber, vm.tick(), tempo);
    if (const auto initial = program.initialTempo(); initial && tempo == initial->tempo) {
      track.sharedTempoApplied = true;
    }
    output.tempo(tempoMicrosecondsPerQuarter(context.version, context.minorVersion, tempo));
    track.configureVibratoFade(context.version);
    track.configureTremoloFade(context.version);
  }

  void tempoChange(u8 rawTempo) {
    const u8 tempo = normalizedTempo(rawTempo);
    track.tempoState.setCurrentAt(vm.tick(), tempo);
    applyTempo(out, tempo);
  }

  void syncSharedTempoAtTick() {
    const std::optional<u8> sharedTempo = program.tempoAt(vm.tick());
    if (!sharedTempo || (track.sharedTempoApplied && track.tempo == *sharedTempo)) {
      return;
    }
    track.tempo = *sharedTempo;
    track.sharedTempoApplied = true;
    track.configureVibratoFade(context.version);
    track.configureTremoloFade(context.version);
  }

  Effects loopEnd() {
    const u8 slot = (track.loopLevel == 0 ? static_cast<u8>(track.loops.size()) : track.loopLevel) - 1;
    LoopFrame& frame = track.loops[slot];
    if (context.version == AKAOSNES_V4) {
      ++frame.incrementCount;
    }
    if (frame.totalPlays == 0) {
      return vm.declaredLoop(frame.start);
    }
    Effects effects = vm.countedRepeatUntil(slot, frame.totalPlays, frame.start);
    if (!effects.flowOverride) {
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
    return vm.finiteBranch(destination);
  }

  void tick() {
    syncSharedTempoAtTick();
    static_cast<void>(
        track.volume.tickRaw([&](s32 value) { emitVolume(track.volume.output(out), static_cast<u8>(value)); }));
    static_cast<void>(track.pan.tickRaw([&](s32 value) { emitPan(track.pan.output(out), static_cast<u8>(value)); }));
    static_cast<void>(
        track.tempoState.tickRaw([&](s32 value) { applyTempo(track.tempoState.output(out), static_cast<u8>(value)); }));
    if (terminalPitchWaitBoundary()) {
      return;
    }
    updateVibratoFade();
    updateTremoloFade();
    const bool pitchSlideAdvanced = advancePitchSlide();
    updatePitchEnvelope();
    if (pitchSlideAdvanced) {
      // The driver updates the envelope after the slide when both are active,
      // so the retained sample must reflect that final pitch for this tick.
      samplePitchSlide();
    }
  }
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
      return event.invoke<&Playback::volume>(event.u8("volume", SemanticOperandRole::Level));
    }
    case EventType::VolumeFade: {
      auto event = cursor.command("Volume Fade", SequenceSemantic::Level);
      const u16 length = profile.version == AKAOSNES_V1 ? event.u16le("length") : event.u8("length");
      const u8 target = event.u8("volume", SemanticOperandRole::Level);
      return length == 0 ? event.invoke<&Playback::volume>(target)
                         : event.invoke(
                               [](Playback& playback, u16 ticks, u8 volume) {
                                 static_cast<void>(playback.track.volume.begin(
                                     playback.out.fade(PerformanceAutomationTarget::Level, channelLevel(volume), ticks),
                                     SequenceFixedPointMotion<s32>::toRawTarget(volume, ticks)));
                               },
                               length, target);
    }
    case EventType::Pan: {
      auto event = cursor.command("Pan", SequenceSemantic::Pan);
      return event.invoke<&Playback::pan>(event.u8("pan", SemanticOperandRole::Pan));
    }
    case EventType::PanFade: {
      auto event = cursor.command("Pan Fade", SequenceSemantic::Pan);
      const u16 length = profile.version == AKAOSNES_V1 ? event.u16le("length") : event.u8("length");
      const u8 target = event.u8("pan", SemanticOperandRole::Pan);
      return length == 0
                 ? event.invoke<&Playback::pan>(target)
                 : event.invoke(
                       [](Playback& playback, u16 ticks, u8 rawPan) {
                         const u8 pan = static_cast<u8>(rawPan << (playback.track.pan8Bit ? 0 : 1));
                         const double rightGain = rightGainFromPan(pan);
                         static_cast<void>(playback.track.pan.begin(
                             playback.out.fade(PerformanceAutomationTarget::Pan, (rightGain * 2.0) - 1.0, ticks),
                             SequenceFixedPointMotion<s32>::toRawTarget(pan, ticks)));
                       },
                       length, target);
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
              playback.track.pitchEnvelopeAutomation.clear();
              return;
            }
            auto& envelope = playback.track.pitchEnvelope;
            envelope.enabled = true;
            envelope.semitones = pitch;
            envelope.delay = wait;
            envelope.length = duration;
            envelope.progressStep = akaoSnesPitchEnvelopeProgressStep(playback.context.version, duration);
            playback.track.pitchEnvelopeAutomation = playback.out.noteEnvelope(
                PerformanceAutomationTarget::Pitch, static_cast<double>(pitch), duration, wait);
          },
          semitones, delay, length);
    }
    case EventType::PitchEnvelopeOff: {
      auto event = cursor.command("Pitch Envelope Off", SequenceSemantic::Pitch);
      event.derived("pitch_envelope_off", true, SemanticOperandRole::State);
      return event.invoke([](Playback& playback) {
        playback.track.pitchEnvelope = {};
        playback.track.pitchEnvelopeAutomation.clear();
      });
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

    case EventType::SlurOn:
      return cursor.command("Slur On", SequenceSemantic::State).set<&TrackState::slur>(true);
    case EventType::SlurOff:
      return cursor.command("Slur Off", SequenceSemantic::State).set<&TrackState::slur>(false);
    case EventType::LegatoOn:
      return cursor.command("Legato On", SequenceSemantic::State).set<&TrackState::legato>(true);
    case EventType::LegatoOff:
      return cursor.command("Legato Off", SequenceSemantic::State).set<&TrackState::legato>(false);
    case EventType::PanLfoOff:
      return cursor.sourceOnly("Pan LFO Off");
    case EventType::NoiseOn:
      return cursor.sourceOnly("Noise On");
    case EventType::NoiseOff:
      return cursor.sourceOnly("Noise Off");
    case EventType::PitchModOn:
      return cursor.sourceOnly("Pitch Modulation On");
    case EventType::PitchModOff:
      return cursor.sourceOnly("Pitch Modulation Off");
    case EventType::EchoOn:
      return cursor.sourceOnly("Echo On");
    case EventType::EchoOff:
      return cursor.sourceOnly("Echo Off");
    case EventType::AdsrDefault:
      if (usesDynamicAdsr(profile)) {
        return cursor.command("ADSR Default", SequenceSemantic::Envelope)
            .restoreEnvelope(EnvelopeFields::All, VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks);
      }
      return cursor.sourceOnly("ADSR Default");
    case EventType::IncCpuSharedCounter:
      return cursor.sourceOnly("Increment CPU Shared Counter");
    case EventType::ZeroCpuSharedCounter:
      return cursor.sourceOnly("Zero CPU Shared Counter");
    case EventType::IgnoreMasterVolume:
      return cursor.sourceOnly("Ignore Master Volume");
    case EventType::IgnoreMasterVolumeBroken:
      return cursor.sourceOnly("Ignore Master Volume (Broken)");
    case EventType::LoopRestart:
      return cursor.sourceOnly("Loop Restart");

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
      return event.invoke<&Playback::programChange>(program);
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
      const std::string_view label = type == EventType::AdsrAr   ? "ADSR Attack Rate"
                                     : type == EventType::AdsrDr ? "ADSR Decay Rate"
                                     : type == EventType::AdsrSl ? "ADSR Sustain Level"
                                                                 : "ADSR Sustain Rate";
      if (usesDynamicAdsr(profile) && (type == EventType::AdsrAr || type == EventType::AdsrSr)) {
        auto event = cursor.command(label, SequenceSemantic::Envelope);
        const u8 value = event.u8("value", SourceValueDisplay::Hex);
        if (type == EventType::AdsrAr) {
          const u8 rate = event.derived("dsp_attack_rate", static_cast<u8>(value & 0x0f));
          return event.emitEnvelopeField<EnvelopeFields::Attack>(
              snesDspAdsrAttackSeconds(rate), VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks);
        }
        const u8 rate = event.derived("dsp_sustain_rate", static_cast<u8>(value & 0x1f));
        return event.emitEnvelopeField<EnvelopeFields::SecondDecay>(
            snesDspAdsrSustainSeconds(rate), VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks);
      }
      auto event = cursor.sourceOnly(label);
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
      const u8 raw = event.u8("raw");
      const u8 tempo = normalizeTempoValue(profile.minorVersion, raw);
      event.derived("tempo",
                    tempoBeatsPerMinute(tempoMicrosecondsPerQuarter(profile.version, profile.minorVersion, tempo)),
                    SourceValueDisplay::BeatsPerMinute);
      return event.invoke<&Playback::tempoChange>(raw);
    }
    case EventType::TempoFade: {
      auto event = cursor.command("Tempo Fade", SequenceSemantic::Tempo);
      const u16 length = profile.version == AKAOSNES_V1 ? event.u16le("length") : event.u8("length");
      const u8 target = event.u8("raw");
      const u8 normalizedTarget = normalizeTempoValue(profile.minorVersion, target);
      event.derived(
          "target_tempo",
          tempoBeatsPerMinute(tempoMicrosecondsPerQuarter(profile.version, profile.minorVersion, normalizedTarget)),
          SourceValueDisplay::BeatsPerMinute);
      return length == 0
                 ? event.invoke<&Playback::tempoChange>(target)
                 : event.invoke(
                       [](Playback& playback, u16 ticks, u8 rawTempo) {
                         playback.track.tempoState.setCurrentRaw(playback.track.tempo);
                         const u8 tempo = playback.normalizedTempo(rawTempo);
                         static_cast<void>(playback.track.tempoState.begin(
                             playback.out.fade(PerformanceAutomationTarget::Tempo,
                                               static_cast<double>(tempoMicrosecondsPerQuarter(
                                                   playback.context.version, playback.context.minorVersion, tempo)),
                                               ticks),
                             SequenceFixedPointMotion<s32>::toRawTarget(tempo, ticks)));
                       },
                       length, target);
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
      return event.invoke<&Playback::loopBreak>(count, destination).mayBranchTo(destination).runtimeControlFlow();
    }
    case EventType::Goto: {
      auto event = cursor.command("Jump", SequenceSemantic::Jump);
      return event.loopCandidate(relocated(event, SemanticOperandRole::LoopTarget));
    }

    case EventType::EchoFeedbackFade:
    case EventType::EchoFirFade:
    case EventType::EchoFeedback:
    case EventType::EchoFir: {
      const bool fade = type == EventType::EchoFeedbackFade || type == EventType::EchoFirFade;
      const std::string_view label = type == EventType::EchoFeedbackFade ? "Echo Feedback Fade"
                                     : type == EventType::EchoFirFade    ? "Echo FIR Fade"
                                     : type == EventType::EchoFeedback   ? "Echo Feedback"
                                                                         : "Echo FIR";
      auto event = cursor.sourceOnly(label);
      event.u8(fade ? "length" : "value");
      if (fade) {
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
      return event.discoverTarget(destination);
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
          },
      .prepass = SemanticPrepassMode::ScheduledPlayback,
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
      .headerSize = akaoSnesSequenceHeaderSize(layout.version, layout.minorVersion),
  };

  if (layout.version == AKAOSNES_V3) {
    info.romRelocBase = reader.has(layout.sequenceHeaderAddress, 2) ? reader.le16(layout.sequenceHeaderAddress) : 0;
    if (layout.minorVersion != AKAOSNES_V3_FFMQ) {
      info.trackPointerOffset += 2;
    }
    const u32 endPointerOffset = info.trackPointerOffset + kAkaoSnesMaxTracks * 2;
    info.sequenceEnd = reader.has(endPointerOffset, 2)
                           ? relocatedAddress(reader.le16(endPointerOffset), info.romRelocBase, info.apuRelocBase)
                           : kAkaoSnesAramSize;
  } else if (layout.version == AKAOSNES_V4) {
    info.romRelocBase = reader.has(layout.sequenceHeaderAddress, 2) ? reader.le16(layout.sequenceHeaderAddress) : 0;
    const u32 endPointerOffset = layout.sequenceHeaderAddress + 2;
    info.sequenceEnd = reader.has(endPointerOffset, 2)
                           ? relocatedAddress(reader.le16(endPointerOffset), info.romRelocBase, info.apuRelocBase)
                           : kAkaoSnesAramSize;
    info.trackPointerOffset += 4;
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
  const TrackDecodeScope tracks{
      .reader = reader,
      .bytecodeEnd = options.bytecodeEnd,
      .maxCommands = 16384,
      .sequenceAsset = options.sequenceAsset,
      .parentAnnotation = options.parentAnnotation,
      .sourceMap = options.sourceMap,
  };
  return tracks.reachable(options.sourceTrackNumber, options.startAddress, [&](u32 offset) {
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
  program.behavior.initialTempoMicrosecondsPerQuarter =
      tempoMicrosecondsPerQuarter(profile.version, profile.minorVersion, kDefaultTempo);

  return program;
}

}  // namespace vgmtrans::formats::akao_snes
