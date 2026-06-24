/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/KonamiSnes/KonamiSnesSequence.h"

#include "value/base/LevelScale.h"
#include "value/sequence/BytecodeDecode.h"
#include "value/sequence/SequenceCursorDialect.h"
#include "value/sequence/SequenceMotion.h"

#include <fmt/format.h>

#include <algorithm>
#include <any>
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

enum class EventType {
  Unknown0,
  Unknown1,
  Unknown2,
  Unknown3,
  Unknown4,
  Unknown5,
  Note,
  PercussionOn,
  PercussionOff,
  Gain,
  InstantTuning,
  Rest,
  Tie,
  Pan,
  Vibrato,
  RandomPitch,
  ProgramChange,
  LoopStart,
  LoopEnd,
  LoopStart2,
  LoopEnd2,
  Tempo,
  TempoFadeV1,
  TempoFadeV2,
  TransposeAbs,
  Adsr1,
  Adsr2,
  Volume,
  VolumeFadeV1,
  VolumeFadeV2,
  Portamento,
  PitchEnvelopeV1,
  PitchEnvelopeV2,
  Tuning,
  PitchSlideV1,
  PitchSlideV2,
  PitchSlideV3,
  Echo,
  EchoParam,
  VoltaStart,
  VoltaEnd,
  PanFadeV1,
  PanFadeV2,
  VibratoFade,
  AdsrGain,
  ProgramChangeVolume,
  ConditionalJumpV1,
  LinearPitchEnvelopeV2,
  Goto,
  Call,
  End,
  Unsupported,
};

constexpr std::array<u8, 21> kPanVolumeLeftV1{
    0x00, 0x05, 0x0c, 0x14, 0x1e, 0x28, 0x32, 0x3c, 0x46, 0x50, 0x59,
    0x62, 0x69, 0x6f, 0x74, 0x78, 0x7b, 0x7d, 0x7e, 0x7e, 0x7f};
constexpr std::array<u8, 21> kPanVolumeRightV1{
    0x7f, 0x7e, 0x7e, 0x7d, 0x7b, 0x78, 0x74, 0x6f, 0x69, 0x62, 0x59,
    0x50, 0x46, 0x3c, 0x32, 0x28, 0x1e, 0x14, 0x0c, 0x05, 0x00};
constexpr std::array<u8, 21> kPanVolumeLeftV2{
    0x00, 0x0a, 0x18, 0x28, 0x3c, 0x50, 0x64, 0x78, 0x8c, 0xa0, 0xb2,
    0xc4, 0xd2, 0xde, 0xe8, 0xf0, 0xf6, 0xfa, 0xfc, 0xfc, 0xfe};
constexpr std::array<u8, 21> kPanVolumeRightV2{
    0xfe, 0xfc, 0xfc, 0xfa, 0xf6, 0xf0, 0xe8, 0xde, 0xd2, 0xc4, 0xb2,
    0xa0, 0x8c, 0x78, 0x64, 0x50, 0x3c, 0x28, 0x18, 0x0a, 0x00};
constexpr std::array<u8, 42> kPanTable{
    0x00, 0x04, 0x08, 0x0e, 0x14, 0x1a, 0x20, 0x28, 0x30, 0x38, 0x40, 0x48, 0x50, 0x5a,
    0x64, 0x6e, 0x78, 0x82, 0x8c, 0x96, 0xa0, 0xa8, 0xb0, 0xb8, 0xc0, 0xc8, 0xd0, 0xd6,
    0xdc, 0xe0, 0xe4, 0xe8, 0xec, 0xf0, 0xf4, 0xf6, 0xf8, 0xfa, 0xfc, 0xfe, 0xfe, 0xfe};
constexpr std::array<u8, 128> kVolumeTable{
    0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02, 0x02,
    0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x04, 0x04,
    0x04, 0x04, 0x04, 0x04, 0x05, 0x05, 0x05, 0x05, 0x06, 0x06, 0x07, 0x07, 0x07, 0x07, 0x08, 0x08,
    0x09, 0x09, 0x0a, 0x0a, 0x0a, 0x0a, 0x0b, 0x0b, 0x0c, 0x0c, 0x0d, 0x0d, 0x0e, 0x0f, 0x10, 0x10,
    0x11, 0x12, 0x13, 0x14, 0x15, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1b, 0x1c, 0x1d, 0x1e, 0x20, 0x22,
    0x23, 0x24, 0x26, 0x28, 0x2a, 0x2c, 0x2d, 0x2f, 0x31, 0x33, 0x35, 0x38, 0x3a, 0x3d, 0x40, 0x43,
    0x46, 0x49, 0x4c, 0x4f, 0x52, 0x56, 0x5a, 0x5e, 0x62, 0x66, 0x6b, 0x6f, 0x73, 0x77, 0x7b, 0x7f};

struct Context {
  KonamiSnesVersion version = KONAMISNES_NONE;
};

[[nodiscard]] EventType eventType(KonamiSnesVersion version, u8 opcode) {
  if (opcode <= 0x5f || opcode >= 0x80) {
    if (opcode <= 0x5f || opcode <= 0xdf) {
      return EventType::Note;
    }
  }

  if ((version == KONAMISNES_V5 || version == KONAMISNES_V6) && opcode >= 0x70 && opcode <= 0x7f) {
    return EventType::InstantTuning;
  }

  if (opcode == 0x60) {
    return EventType::PercussionOn;
  }
  if (opcode == 0x61) {
    return EventType::PercussionOff;
  }
  if (opcode == 0x62 && version != KONAMISNES_V1) {
    return EventType::Gain;
  }
  if (opcode < 0xe0) {
    if (version == KONAMISNES_V1 && (opcode == 0x62 || opcode == 0x63)) {
      return EventType::Unknown1;
    }
    if (version == KONAMISNES_V1 && opcode == 0x64) {
      return EventType::Unknown2;
    }
    return EventType::Unknown0;
  }

  switch (opcode) {
    case 0xe0:
      return EventType::Rest;
    case 0xe1:
      return EventType::Tie;
    case 0xe2:
      return EventType::ProgramChange;
    case 0xe3:
      return EventType::Pan;
    case 0xe4:
      return EventType::Vibrato;
    case 0xe5:
      return EventType::RandomPitch;
    case 0xe6:
      return EventType::LoopStart;
    case 0xe7:
      return EventType::LoopEnd;
    case 0xe8:
      return EventType::LoopStart2;
    case 0xe9:
      return EventType::LoopEnd2;
    case 0xea:
      return EventType::Tempo;
    case 0xeb:
      return (version == KONAMISNES_V5 || version == KONAMISNES_V6) ? EventType::TempoFadeV2
                                                                    : EventType::TempoFadeV1;
    case 0xec:
      return EventType::TransposeAbs;
    case 0xed:
      return (version == KONAMISNES_V5 || version == KONAMISNES_V6) ? EventType::Adsr1 : EventType::Unknown3;
    case 0xee:
      return EventType::Volume;
    case 0xef:
      return (version == KONAMISNES_V5 || version == KONAMISNES_V6) ? EventType::VolumeFadeV2
                                                                    : EventType::VolumeFadeV1;
    case 0xf0:
      return EventType::Portamento;
    case 0xf1:
      return (version == KONAMISNES_V5 || version == KONAMISNES_V6) ? EventType::PitchEnvelopeV2
                                                                    : EventType::PitchEnvelopeV1;
    case 0xf2:
      return EventType::Tuning;
    case 0xf3:
      if (version == KONAMISNES_V1) {
        return EventType::PitchSlideV1;
      }
      if (version == KONAMISNES_V5 || version == KONAMISNES_V6) {
        return EventType::PitchSlideV3;
      }
      return EventType::PitchSlideV2;
    case 0xf4:
      return EventType::Echo;
    case 0xf5:
      return EventType::EchoParam;
    case 0xf6:
      return EventType::VoltaStart;
    case 0xf7:
      return EventType::VoltaEnd;
    case 0xf8:
      return (version == KONAMISNES_V5 || version == KONAMISNES_V6) ? EventType::PanFadeV2 : EventType::PanFadeV1;
    case 0xf9:
      return EventType::VibratoFade;
    case 0xfa:
      return version >= KONAMISNES_V4 ? EventType::AdsrGain : EventType::Unknown3;
    case 0xfb:
      return version >= KONAMISNES_V4 ? EventType::Adsr2 : EventType::Unknown1;
    case 0xfc:
      if (version == KONAMISNES_V1) {
        return EventType::ConditionalJumpV1;
      }
      if (version == KONAMISNES_V2) {
        return EventType::LinearPitchEnvelopeV2;
      }
      if (version >= KONAMISNES_V4) {
        return EventType::ProgramChangeVolume;
      }
      return EventType::Unknown2;
    case 0xfd:
      return EventType::Goto;
    case 0xfe:
      return EventType::Call;
    case 0xff:
      return EventType::End;
    default:
      return EventType::Unsupported;
  }
}

[[nodiscard]] bool isPitchSlide(EventType type) {
  return type == EventType::PitchSlideV1 || type == EventType::PitchSlideV2 || type == EventType::PitchSlideV3;
}

[[nodiscard]] u32 tempoMicrosecondsPerQuarter(KonamiSnesVersion version, u8 tempo) {
  if (tempo == 0) {
    return 60000000;
  }
  return static_cast<u32>(std::lround(kKonamiSnesPpqn * (125.0 * timerFrequency(version)) * 256.0 / tempo));
}

[[nodiscard]] double tuningSemitones(s8 tuning) {
  return tuning * 4.0 / 256.0;
}

[[nodiscard]] double linearGainFromRawVolume(u8 volume) {
  return std::clamp(static_cast<double>(volume) / 255.0, 0.0, 1.0);
}

[[nodiscard]] double stereoPositionFromPan(KonamiSnesVersion version, u8 pan) {
  u8 left = 0;
  u8 right = 0;
  if (version == KONAMISNES_V1) {
    pan = std::min<u8>(pan, 20);
    left = kPanVolumeLeftV1[pan];
    right = kPanVolumeRightV1[pan];
  } else if (version == KONAMISNES_V2) {
    pan = std::min<u8>(pan, 20);
    left = kPanVolumeLeftV2[pan];
    right = kPanVolumeRightV2[pan];
  } else {
    pan = std::min<u8>(pan, 40);
    left = kPanTable[40 - pan];
    right = kPanTable[pan];
  }
  const double total = static_cast<double>(left) + static_cast<double>(right);
  if (total <= 0.0) {
    return 0.0;
  }
  return std::clamp((static_cast<double>(right) / total) * 2.0 - 1.0, -1.0, 1.0);
}

[[nodiscard]] u8 clampPan(KonamiSnesVersion version, u8 pan) {
  return std::min(pan, version <= KONAMISNES_V2 ? u8{20} : u8{40});
}

struct ControllerFade {
  SequenceFixedPointMotionPlan<s32> motion;
};

struct PitchSlide {
  EventType type = EventType::PitchSlideV1;
  u8 delay = 0;
  u8 length = 0;
  u8 targetNote = 0;
  double targetSemitones = 0.0;
  double deltaSemitones = 0.0;
};

class LfoState {
public:
  void reset() {
    delay_ = 0;
    rate_ = 0;
    depth_ = 0;
    currentDepth_.reset(0);
    reusableFadeTicks_ = 0;
  }

  void configure(u8 delay, u8 rate, u8 depth) {
    delay_ = delay;
    rate_ = rate;
    depth_ = depth;
    currentDepth_.setCurrent(static_cast<s32>(depth) << 8);
    reusableFadeTicks_ = 0;
  }

  void setReusableFade(u8 ticks) {
    reusableFadeTicks_ = ticks;
  }

  void beginReusableFade() {
    if (reusableFadeTicks_ == 0) {
      return;
    }
    currentDepth_.setCurrent(0);
    const auto target = static_cast<s32>(depth_) << 8;
    const auto step = reusableFadeTicks_ == 0 ? 0 : target / reusableFadeTicks_;
    static_cast<void>(
        currentDepth_.begin(SequenceMotionPlan<s32>::targetOverTicksWithStep(target, step, reusableFadeTicks_, delay_)));
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

struct ModulationRanges {
  u8 maxDepth = kMinVibratoMaxDepth;
  u16 maxRateFactor = 0;
};

struct TrackState {
  TrackState() = default;
  TrackState(const SequenceProgram& program, const TrackProgram&, const Context& context)
      : maxVibrato(analyzeVibratoRanges(program, context.version)) {
    reset(context.version);
  }

  void reset(KonamiSnesVersion version) {
    noteLength = 0;
    noteDurationRate = 0;
    loopReturnAddress = {};
    loopReturnAddress2 = {};
    percussion = false;
    inSubroutine = false;
    instrument = 0;
    prevNoteKey.reset();
    prevNoteSlurred = false;
    seqTuningCents = 0.0;
    loopVolumeDelta = 0;
    loopPitchDelta = 0;
    loopVolumeDelta2 = 0;
    loopPitchDelta2 = 0;
    tempo = kKonamiSnesDefaultTempo;
    volumeFade.reset(0xff);
    panFade.reset(version <= KONAMISNES_V2 ? 10 : 20);
    tempoFade.reset(kKonamiSnesDefaultTempo);
    vibrato.reset();
    pitchBase.reset();
    pitchSlide.clearMotion();
  }

  static ModulationRanges analyzeVibratoRanges(const SequenceProgram& program, KonamiSnesVersion version) {
    ModulationRanges ranges{
        .maxDepth = kMinVibratoMaxDepth,
        .maxRateFactor = vibrato::minMaxRateFactor(version),
    };
    for (const auto& track : program.tracks) {
      u8 tempo = kKonamiSnesDefaultTempo;
      u8 activeRate = 0;
      u8 activeDepth = 0;
      for (const auto& command : track.commands) {
        const auto bytes = track.bytesFor(command);
        if (bytes.empty()) {
          continue;
        }
        const u8 opcode = bytes[0];
        const EventType type = eventType(version, opcode);
        if (type == EventType::Tempo && bytes.size() >= 2) {
          tempo = bytes[1];
          if (vibrato::isActive(version, activeRate, activeDepth)) {
            ranges.maxRateFactor =
                std::max(ranges.maxRateFactor, vibrato::rateFactor(version, activeRate, tempo));
          }
        } else if (type == EventType::Vibrato && bytes.size() >= 4) {
          activeRate = bytes[2];
          activeDepth = bytes[3];
          if (vibrato::isActive(version, activeRate, activeDepth)) {
            ranges.maxDepth = std::max(ranges.maxDepth, activeDepth);
            ranges.maxRateFactor =
                std::max(ranges.maxRateFactor, vibrato::rateFactor(version, activeRate, tempo));
          }
        }
      }
    }
    return ranges;
  }

  [[nodiscard]] u8 noteDuration(KonamiSnesVersion version, u8 length) const {
    const u8 maxRate = noteDurationRateMax(version);
    if (noteDurationRate == maxRate) {
      return length;
    }
    const u8 duration = version == KONAMISNES_V1 ? static_cast<u8>((length * noteDurationRate) / 100)
                                                 : static_cast<u8>((length * (noteDurationRate << 1)) >> 8);
    return std::max<u8>(duration, 1);
  }

  [[nodiscard]] double noteSemitones(u8 key, bool includeTuning = true) const {
    double semitones = (key & 0x7f) + transpose;
    if (includeTuning) {
      semitones += totalTuningCents() / 100.0;
    }
    return semitones;
  }

  [[nodiscard]] double totalTuningCents() const {
    return seqTuningCents + static_cast<double>(loopPitchDelta + loopPitchDelta2) * (100.0 / 32.0);
  }

  template <class Runtime>
  void applyEffectiveTuning(Runtime& rt) {
    const double cents = totalTuningCents();
    if (std::abs(lastEmittedTuningCents - cents) > 0.001) {
      rt.tuning(cents);
      lastEmittedTuningCents = cents;
    }
  }

  template <class Runtime>
  void resetPitchForNote(u8 key, Runtime& rt) {
    pitchSlide.clearMotion();
    rt.pitchBend(0.0);
    rt.pitchBendRange(2);
    if (percussion) {
      pitchBase.reset();
      return;
    }
    pitchBase = noteSemitones(key, true);
    pitchSlide.setCurrent(*pitchBase);
  }

  template <class Runtime>
  void beginPitchSlide(const PitchSlide& slide, Runtime& rt) {
    if (!pitchBase || slide.length == 0) {
      rt.pitchBendRange(2);
      return;
    }
    const double startDeviation = std::abs(pitchSlide.current() - *pitchBase);
    const double targetDeviation = std::abs(slide.targetSemitones - *pitchBase);
    const u8 range = static_cast<u8>(std::max<int>(2, static_cast<int>(std::ceil(std::max(startDeviation, targetDeviation)))));
    rt.pitchBendRange(range);
    const double step = slide.length == 0 ? 0.0 : (slide.targetSemitones - pitchSlide.current()) / slide.length;
    static_cast<void>(pitchSlide.begin(SequenceMotionPlan<double>::targetOverTicksWithStep(
        slide.targetSemitones, step, slide.length, slide.delay)));
  }

  template <class Runtime>
  void emitVibratoDepth(Runtime& rt, bool force = false) {
    const bool active = vibrato::isActive(rt.context.version, vibrato.rate(), vibrato.depth());
    double amount = 0.0;
    if (active) {
      const double currentCents =
          vibrato::currentDepthCents(rt.context.version, vibrato.depth(), vibrato.currentDepth());
      const double maxCents = vibrato::maxDepthCents(rt.context.version, maxVibrato.maxDepth);
      amount = maxCents <= 0.0 ? 0.0 : currentCents / maxCents;
    }
    amount = std::clamp(amount, 0.0, 1.0);
    if (force || std::abs(amount - lastVibratoDepthAmount) > 0.0001) {
      rt.modulation(ModulationPerformanceTarget::VibratoDepth, amount);
      lastVibratoDepthAmount = amount;
    }
  }

  template <class Runtime>
  void emitVibratoRate(Runtime& rt) {
    const u16 factor = vibrato::rateFactor(rt.context.version, vibrato.rate(), tempo);
    const double amount = maxVibrato.maxRateFactor == 0 ? 0.0 : static_cast<double>(factor) / maxVibrato.maxRateFactor;
    rt.modulation(ModulationPerformanceTarget::VibratoRate, std::clamp(amount, 0.0, 1.0));
  }

  void tickAutomation(PerformanceEmitter& out, KonamiSnesVersion version) {
    static_cast<void>(tempoFade.tickRaw([&](s32 rawTempo) {
      tempo = static_cast<u8>(std::clamp<s32>(rawTempo, 0, 0xff));
      out.tempo(tempoMicrosecondsPerQuarter(version, tempo));
    }));
    static_cast<void>(volumeFade.tickRaw([&](s32 rawVolume) {
      out.level(LevelScale::linearFromLinear(linearGainFromRawVolume(static_cast<u8>(std::clamp<s32>(rawVolume, 0, 0xff)))),
                LevelPrecisionHint::FourteenBit);
    }));
    static_cast<void>(panFade.tickRaw([&](s32 rawPan) {
      out.pan(stereoPositionFromPan(version, clampPan(version, static_cast<u8>(std::clamp<s32>(rawPan, 0, 0xff)))));
    }));
    if (pitchBase && pitchSlide.active()) {
      const auto pitchTick = pitchSlide.tick();
      if (pitchTick.status != SequenceMotionStatus::Inactive && pitchTick.status != SequenceMotionStatus::Delayed) {
        out.pitchBend(pitchSlide.current() - *pitchBase);
      }
    }
    if (vibrato.fadeActive()) {
      const auto fadeTick = vibrato.tickFade();
      if (fadeTick.status != SequenceMotionStatus::Inactive && fadeTick.status != SequenceMotionStatus::Delayed) {
        struct TickRuntime {
          TrackState& state;
          PerformanceEmitter& out;
          const Context& context;
          void modulation(ModulationPerformanceTarget target, double amount) { out.modulation(target, amount); }
        } rt{*this, out, Context{.version = version}};
        emitVibratoDepth(rt);
      }
    }
  }

  u8 noteLength = 0;
  u8 noteDurationRate = 0;
  s32 transpose = 0;
  std::optional<Address> loopReturnAddress;
  std::optional<Address> loopReturnAddress2;
  s16 loopVolumeDelta = 0;
  s16 loopPitchDelta = 0;
  s16 loopVolumeDelta2 = 0;
  s16 loopPitchDelta2 = 0;
  bool percussion = false;
  bool inSubroutine = false;
  u8 instrument = 0;
  std::optional<u8> prevNoteKey;
  bool prevNoteSlurred = false;
  double seqTuningCents = 0.0;
  double lastEmittedTuningCents = std::numeric_limits<double>::quiet_NaN();
  u8 tempo = kKonamiSnesDefaultTempo;
  SequenceFixedPointAutomation<s32> panFade;
  SequenceFixedPointAutomation<s32> volumeFade;
  SequenceFixedPointAutomation<s32> tempoFade;
  LfoState vibrato;
  ModulationRanges maxVibrato{
      .maxDepth = kMinVibratoMaxDepth,
      .maxRateFactor = vibrato::minMaxRateFactor(KONAMISNES_V1),
  };
  std::optional<double> pitchBase;
  SequenceAutomatedValue<double> pitchSlide;
  double lastVibratoDepthAmount = -1.0;
};

template <class Runtime>
void emitVolume(Runtime& rt, u8 rawVolume) {
  rt.state.volumeFade.setCurrentRaw(rawVolume);
  rt.level(LevelScale::linearFromLinear(linearGainFromRawVolume(rawVolume)), LevelPrecisionHint::FourteenBit);
}

template <class Runtime>
void emitPan(Runtime& rt, u8 rawPan) {
  const u8 pan = clampPan(rt.context.version, rawPan);
  rt.state.panFade.setCurrentRaw(pan);
  rt.pan(stereoPositionFromPan(rt.context.version, pan));
}

template <class Runtime>
void applyTuning(Runtime& rt, s8 tuning) {
  rt.state.seqTuningCents = tuningSemitones(tuning) * 100.0;
  rt.state.applyEffectiveTuning(rt);
}

template <class Runtime>
PitchSlide readPitchSlideOperands(Runtime& rt, VmCommandCursor& cmd, EventType type) {
  PitchSlide slide{
      .type = type,
      .delay = cmd.u8("slide_delay"),
      .length = cmd.u8("slide_length"),
      .targetNote = cmd.u8("target_note"),
  };
  slide.targetSemitones = rt.state.noteSemitones(slide.targetNote, type == EventType::PitchSlideV1);
  if (type == EventType::PitchSlideV2) {
    if (slide.length != 0) {
      static_cast<void>(cmd.u8("reserved"));
      const s16 delta = cmd.s16le("delta");
      slide.deltaSemitones = delta / 256.0;
    }
  } else if (type == EventType::PitchSlideV3) {
    const s16 delta = cmd.s16le("delta");
    slide.deltaSemitones = (delta / 256.0) * (256.0 / std::max<u8>(rt.state.tempo, 1));
  } else if (slide.length != 0 && rt.state.pitchBase) {
    slide.deltaSemitones = (slide.targetSemitones - rt.state.pitchSlide.current()) / slide.length;
  }
  cmd.derived("target_semitones", slide.targetSemitones);
  return slide;
}

template <class Runtime>
std::optional<PitchSlide> consumeInlinePitchSlide(Runtime& rt, VmCommandCursor& cmd) {
  const auto opcode = cmd.peekU8();
  if (!opcode) {
    return std::nullopt;
  }
  const EventType type = eventType(rt.context.version, *opcode);
  if (!isPitchSlide(type)) {
    return std::nullopt;
  }
  static_cast<void>(cmd.u8("pitch_slide_opcode"));
  return readPitchSlideOperands(rt, cmd, type);
}

CommandFlow unknownEvent(VmCommandCursor& cmd, u8 argCount) {
  cmd.name("Unknown Event", SequenceSemantic::Unsupported).kind("unknown").sourceOnly();
  cmd.derived("opcode", cmd.opcode(), SourceValueDisplay::Hex);
  for (u8 i = 0; i < argCount; ++i) {
    static_cast<void>(cmd.u8(fmt::format("arg{}", i + 1)));
  }
  return cmd.next();
}

template <class Runtime>
ControllerFade readFade(Runtime& rt, VmCommandCursor& cmd, EventType type) {
  ControllerFade fade;
  switch (type) {
    case EventType::TempoFadeV1:
    case EventType::VolumeFadeV1:
    case EventType::PanFadeV1: {
      const u8 ticks = cmd.u8("length");
      const u8 target = cmd.u8("target");
      const u8 clampedTarget = (type == EventType::PanFadeV1) ? clampPan(rt.context.version, target) : target;
      fade.motion = SequenceFixedPointMotion<s32>::toRawTarget(clampedTarget, ticks);
      break;
    }
    case EventType::TempoFadeV2:
    case EventType::VolumeFadeV2:
    case EventType::PanFadeV2: {
      const u8 target = cmd.u8("target");
      const auto rawStep = static_cast<s32>(static_cast<s8>(cmd.u8("step")) << 4);
      const u8 clampedTarget = (type == EventType::PanFadeV2) ? clampPan(rt.context.version, target) : target;
      fade.motion = SequenceFixedPointMotion<s32>::toRawTargetByFixedStep(clampedTarget, rawStep);
      break;
    }
    default:
      break;
  }
  return fade;
}

struct KonamiSnesCursorReader {
  template <class Runtime>
  static CommandFlow read(Runtime& rt, VmCommandCursor& cmd) {
    const u8 opcode = cmd.opcode();
    const EventType type = eventType(rt.context.version, opcode);
    auto& state = rt.state;

    switch (type) {
      case EventType::Unknown0:
        return unknownEvent(cmd, 0);
      case EventType::Unknown1:
        return unknownEvent(cmd, 1);
      case EventType::Unknown2:
        return unknownEvent(cmd, 2);
      case EventType::Unknown3:
        return unknownEvent(cmd, 3);
      case EventType::Unknown4:
        return unknownEvent(cmd, 4);
      case EventType::Unknown5:
        return unknownEvent(cmd, 5);

      case EventType::Note: {
        cmd.name("Note", SequenceSemantic::Note);
        const u8 key = opcode & 0x7f;
        cmd.derived("key", key);
        u8 length = state.noteLength;
        if ((opcode & 0x80) == 0) {
          length = cmd.u8("length");
          state.noteLength = length;
        }
        u8 velocity = cmd.u8("velocity_or_duration");
        if ((velocity & 0x80) == 0) {
          state.noteDurationRate = std::min(velocity, noteDurationRateMax(rt.context.version));
          velocity = cmd.u8("velocity");
        }
        velocity &= 0x7f;
        if (velocity == 0) {
          velocity = 1;
        }
        velocity = static_cast<u8>(std::clamp<int>(velocity + state.loopVolumeDelta + state.loopVolumeDelta2, 1, 127));
        if (rt.context.version != KONAMISNES_V1) {
          velocity = kVolumeTable[velocity];
        }

        state.applyEffectiveTuning(rt);
        const u8 duration = state.noteDuration(rt.context.version, length);
        const bool tied = state.prevNoteSlurred && state.prevNoteKey && key == *state.prevNoteKey;
        state.resetPitchForNote(key, rt);
        const auto slide = consumeInlinePitchSlide(rt, cmd);
        state.vibrato.beginReusableFade();
        if (state.vibrato.fadeActive()) {
          state.emitVibratoDepth(rt, true);
        }
        if (tied) {
          rt.note(state.noteSemitones(key, false), LevelScale::linearFromLinear(velocity / 127.0), duration, true);
        } else {
          rt.note(state.percussion ? key : state.noteSemitones(key, false),
                  LevelScale::linearFromLinear(velocity / 127.0), duration);
          state.prevNoteKey = key;
        }
        if (slide) {
          state.beginPitchSlide(*slide, rt);
        }
        state.prevNoteSlurred = state.noteDurationRate == noteDurationRateMax(rt.context.version) && !state.percussion;
        return cmd.wait(length);
      }

      case EventType::PercussionOn:
        cmd.name("Percussion On", SequenceSemantic::Program);
        if (!state.percussion) {
          rt.instrument(0x7f, 0, true);
          state.percussion = true;
        }
        return cmd.next();

      case EventType::PercussionOff:
        cmd.name("Percussion Off", SequenceSemantic::Program);
        if (state.percussion) {
          rt.instrument(state.instrument >> 7, state.instrument & 0x7f, true);
          state.percussion = false;
        }
        return cmd.next();

      case EventType::Gain:
        cmd.name("GAIN", SequenceSemantic::Meta).sourceOnly();
        static_cast<void>(cmd.u8("gain_amount"));
        return cmd.next();

      case EventType::InstantTuning: {
        cmd.name("Instant Tuning", SequenceSemantic::Pitch);
        s8 tuning = opcode & 0x0f;
        if (tuning > 8) {
          tuning -= 16;
        }
        cmd.derived("tuning", tuning);
        applyTuning(rt, tuning);
        return cmd.next();
      }

      case EventType::Rest: {
        cmd.name("Rest", SequenceSemantic::Rest);
        state.noteLength = cmd.u8("length");
        const auto slide = consumeInlinePitchSlide(rt, cmd);
        if (slide) {
          state.beginPitchSlide(*slide, rt);
        }
        state.prevNoteSlurred = false;
        return cmd.wait(state.noteLength);
      }

      case EventType::Tie: {
        cmd.name("Tie", SequenceSemantic::Note);
        state.noteLength = cmd.u8("length");
        state.noteDurationRate = std::min<u8>(cmd.u8("duration_rate"), noteDurationRateMax(rt.context.version));
        const u8 duration = state.noteDuration(rt.context.version, state.noteLength);
        if (state.prevNoteSlurred) {
          rt.note(0.0, 1.0, duration, true);
          state.prevNoteSlurred = state.noteDurationRate == noteDurationRateMax(rt.context.version);
        }
        return cmd.wait(state.noteLength);
      }

      case EventType::ProgramChange: {
        cmd.name("Program", SequenceSemantic::Program);
        const u8 program = cmd.u8("program");
        state.instrument = program;
        cmd.derived("bank", program >> 7).derived("program_number", program & 0x7f).instrumentRef(program >> 7, program & 0x7f);
        rt.instrument(program >> 7, program & 0x7f, true);
        emitPan(rt, rt.context.version <= KONAMISNES_V2 ? 10 : 20);
        return cmd.next();
      }

      case EventType::ProgramChangeVolume: {
        cmd.name("Program And Volume", SequenceSemantic::Program);
        const u8 volume = cmd.u8("volume");
        const u8 program = cmd.u8("program");
        state.instrument = program;
        rt.instrument(program >> 7, program & 0x7f, true);
        emitVolume(rt, volume);
        emitPan(rt, rt.context.version <= KONAMISNES_V2 ? 10 : 20);
        return cmd.next();
      }

      case EventType::Pan: {
        cmd.name("Pan", SequenceSemantic::Pan);
        const u8 raw = cmd.u8("pan");
        const bool instrumentPanOff = rt.context.version <= KONAMISNES_V2 ? raw == 0x15 : raw == 0x2a;
        const bool instrumentPanOn = rt.context.version <= KONAMISNES_V2 ? raw == 0x16 : raw == 0x2c;
        cmd.derived("instrument_pan_off", instrumentPanOff).derived("instrument_pan_on", instrumentPanOn);
        if (!instrumentPanOff && !instrumentPanOn) {
          emitPan(rt, raw);
        }
        return cmd.next();
      }

      case EventType::Vibrato: {
        cmd.name("Vibrato", SequenceSemantic::Modulation);
        const u8 arg1 = cmd.u8("delay_or_fade");
        const u8 rate = cmd.u8("rate");
        const u8 depth = cmd.u8("depth");
        const u8 builtInFade = vibrato::inlineFadeLength(rt.context.version, arg1);
        const u8 delay = vibrato::delayFromArg1(rt.context.version, arg1);
        state.vibrato.configure(delay, rate, depth);
        if (builtInFade != 0) {
          state.vibrato.setReusableFade(builtInFade);
        }
        state.emitVibratoDepth(rt, true);
        state.emitVibratoRate(rt);
        return cmd.next();
      }

      case EventType::RandomPitch:
        cmd.name("Random Pitch", SequenceSemantic::Modulation).sourceOnly();
        static_cast<void>(cmd.u8("rate"));
        static_cast<void>(cmd.u16le("pitch_mask"));
        return cmd.next();

      case EventType::LoopStart:
        cmd.name("Loop Start", SequenceSemantic::Loop);
        state.loopReturnAddress = cmd.addressAtCursor();
        return cmd.next();

      case EventType::LoopEnd: {
        cmd.name("Loop End", SequenceSemantic::Repeat);
        const u8 times = cmd.u8("times");
        const s8 volumeDelta = cmd.s8("volume_delta");
        const s8 pitchDelta = cmd.s8("pitch_delta");
        if (!state.loopReturnAddress) {
          return cmd.next();
        }
        if (times == 0) {
          return cmd.declaredLoop(*state.loopReturnAddress);
        }
        const auto flow = rt.countedRepeatUntil(cmd, 0, times, *state.loopReturnAddress);
        if (flow.fallsThrough()) {
          state.loopVolumeDelta = 0;
          state.loopPitchDelta = 0;
        } else {
          state.loopVolumeDelta += volumeDelta;
          state.loopPitchDelta += pitchDelta;
        }
        state.applyEffectiveTuning(rt);
        return flow;
      }

      case EventType::LoopStart2:
        cmd.name("Loop Start #2", SequenceSemantic::Loop);
        state.loopReturnAddress2 = cmd.addressAtCursor();
        return cmd.next();

      case EventType::LoopEnd2: {
        cmd.name("Loop End #2", SequenceSemantic::Repeat);
        const u8 times = cmd.u8("times");
        const s8 volumeDelta = cmd.s8("volume_delta");
        const s8 pitchDelta = cmd.s8("pitch_delta");
        if (!state.loopReturnAddress2) {
          return cmd.next();
        }
        if (times == 0) {
          return cmd.declaredLoop(*state.loopReturnAddress2);
        }
        const auto flow = rt.countedRepeatUntil(cmd, 1, times, *state.loopReturnAddress2);
        if (flow.fallsThrough()) {
          state.loopVolumeDelta2 = 0;
          state.loopPitchDelta2 = 0;
        } else {
          state.loopVolumeDelta2 += volumeDelta;
          state.loopPitchDelta2 += pitchDelta;
        }
        state.applyEffectiveTuning(rt);
        return flow;
      }

      case EventType::Tempo: {
        cmd.name("Tempo", SequenceSemantic::Tempo);
        const u8 tempo = cmd.u8("tempo");
        state.tempo = tempo;
        state.tempoFade.setCurrentRaw(tempo);
        const u32 microseconds = tempoMicrosecondsPerQuarter(rt.context.version, tempo);
        cmd.derived("microseconds_per_quarter", microseconds);
        rt.tempo(microseconds);
        if (vibrato::usesLegacy(rt.context.version)) {
          state.emitVibratoRate(rt);
        }
        return cmd.next();
      }

      case EventType::TempoFadeV1:
      case EventType::TempoFadeV2: {
        cmd.name("Tempo Fade", SequenceSemantic::Tempo);
        const auto fade = readFade(rt, cmd, type);
        static_cast<void>(state.tempoFade.begin(fade.motion));
        return cmd.next();
      }

      case EventType::TransposeAbs:
        cmd.name("Transpose", SequenceSemantic::Pitch);
        state.transpose = cmd.s8("semitones");
        return cmd.next();

      case EventType::Adsr1:
        cmd.name("ADSR(1)", SequenceSemantic::Meta).sourceOnly();
        static_cast<void>(cmd.u8("adsr1"));
        return cmd.next();

      case EventType::Adsr2:
        cmd.name("ADSR(2)", SequenceSemantic::Meta).sourceOnly();
        static_cast<void>(cmd.u8("adsr2"));
        return cmd.next();

      case EventType::Volume: {
        cmd.name("Volume", SequenceSemantic::Level);
        emitVolume(rt, cmd.u8("volume"));
        return cmd.next();
      }

      case EventType::VolumeFadeV1:
      case EventType::VolumeFadeV2: {
        cmd.name("Volume Fade", SequenceSemantic::Level);
        const auto fade = readFade(rt, cmd, type);
        static_cast<void>(state.volumeFade.begin(fade.motion));
        return cmd.next();
      }

      case EventType::Portamento:
        cmd.name("Portamento", SequenceSemantic::Portamento).sourceOnly();
        static_cast<void>(cmd.u8("speed"));
        return cmd.next();

      case EventType::PitchEnvelopeV1:
        cmd.name("Pitch Envelope", SequenceSemantic::Pitch).sourceOnly();
        static_cast<void>(cmd.u8("delay"));
        static_cast<void>(cmd.u8("speed"));
        static_cast<void>(cmd.u8("depth"));
        return cmd.next();

      case EventType::PitchEnvelopeV2:
        cmd.name("Pitch Envelope", SequenceSemantic::Pitch).sourceOnly();
        static_cast<void>(cmd.u8("delay"));
        static_cast<void>(cmd.u8("length"));
        static_cast<void>(cmd.u8("offset"));
        static_cast<void>(cmd.s16le("delta"));
        return cmd.next();

      case EventType::Tuning:
        cmd.name("Tuning", SequenceSemantic::Pitch);
        applyTuning(rt, cmd.s8("tuning"));
        return cmd.next();

      case EventType::PitchSlideV1:
      case EventType::PitchSlideV2:
      case EventType::PitchSlideV3: {
        cmd.name("Pitch Slide", SequenceSemantic::Pitch);
        const auto slide = readPitchSlideOperands(rt, cmd, type);
        state.beginPitchSlide(slide, rt);
        return cmd.next();
      }

      case EventType::Echo:
        cmd.name("Echo", SequenceSemantic::Meta).sourceOnly();
        static_cast<void>(cmd.u8("channels"));
        static_cast<void>(cmd.u8("volume_left"));
        static_cast<void>(cmd.u8("volume_right"));
        return cmd.next();

      case EventType::EchoParam:
        cmd.name("Echo Param", SequenceSemantic::Meta).sourceOnly();
        static_cast<void>(cmd.u8("delay"));
        static_cast<void>(cmd.u8("feedback"));
        static_cast<void>(cmd.u8("arg3"));
        return cmd.next();

      case EventType::VoltaStart:
        cmd.name("Loop With Volta Start", SequenceSemantic::RepeatBreak).sourceOnly();
        return cmd.next();

      case EventType::VoltaEnd:
        cmd.name("Loop With Volta End", SequenceSemantic::RepeatBreak).sourceOnly();
        return cmd.next();

      case EventType::PanFadeV1:
      case EventType::PanFadeV2: {
        cmd.name("Pan Fade", SequenceSemantic::Pan);
        const auto fade = readFade(rt, cmd, type);
        static_cast<void>(state.panFade.begin(fade.motion));
        return cmd.next();
      }

      case EventType::VibratoFade: {
        cmd.name("Vibrato Fade", SequenceSemantic::Modulation);
        state.vibrato.setReusableFade(cmd.u8("length"));
        return cmd.next();
      }

      case EventType::AdsrGain:
        cmd.name("ADSR/Gain", SequenceSemantic::Meta).sourceOnly();
        static_cast<void>(cmd.u8("adsr1"));
        static_cast<void>(cmd.u8("adsr2"));
        static_cast<void>(cmd.u8("gain"));
        return cmd.next();

      case EventType::ConditionalJumpV1: {
        cmd.name("Conditional Jump", SequenceSemantic::Jump);
        const Address destination = cmd.address16le("destination");
        const Address alternate = cmd.address16le("alternate_destination");
        cmd.target(alternate, SourceLinkRole::JumpTarget);
        return cmd.jump(destination);
      }

      case EventType::LinearPitchEnvelopeV2:
        cmd.name("Linear Pitch Envelope", SequenceSemantic::Pitch).sourceOnly();
        static_cast<void>(cmd.u8("delta_fraction"));
        static_cast<void>(cmd.u8("delta_integer"));
        return cmd.next();

      case EventType::Goto:
        cmd.name("Jump", SequenceSemantic::Jump);
        return cmd.loopCandidate(cmd.address16le("destination"));

      case EventType::Call:
        cmd.name("Pattern Play", SequenceSemantic::Call);
        if (cmd.phase() == CommandPhase::Render) {
          state.inSubroutine = true;
        }
        return cmd.call(cmd.address16le("destination"));

      case EventType::End:
        cmd.name("End", SequenceSemantic::End);
        if (cmd.phase() == CommandPhase::Render && state.inSubroutine) {
          state.inSubroutine = false;
          return cmd.ret();
        }
        return cmd.end();

      case EventType::Unsupported:
        cmd.name("Unknown Opcode", SequenceSemantic::Unsupported)
            .kind("unknown")
            .derived("opcode", opcode, SourceValueDisplay::Hex)
            .unsupported("Unknown Konami SNES sequence opcode");
        rt.diagnostic(Severity::Warning, "Unknown Konami SNES sequence opcode");
        return cmd.end();
    }
    return cmd.end();
  }
};

[[nodiscard]] std::string dialectId(KonamiSnesVersion version) {
  return fmt::format("konami-snes:{}", konamiSnesVersionName(version));
}

[[nodiscard]] KonamiSnesSequenceDescriptor makeDescriptor(KonamiSnesVersion version) {
  auto dialect = makeCursorDialect<TrackState, Context, KonamiSnesCursorReader>(CursorDialectSpec<Context>{
      .id = dialectId(version),
      .commandDetailKindPrefix = "konami-snes",
      .timebase = Timebase{.ppqn = kKonamiSnesPpqn},
      .defaultBehavior =
          SequenceProgramBehavior{
              .defaultLoopPolicy = LoopPolicy::PlayOnce,
              .initialLevel = 1.0,
              .initialReverbSend = 0.0,
              .initialPitchBendRangeSemitones = 2,
              .stopAllTracksAtFirstLoop = false,
          },
      .context = Context{.version = version},
  });
  dialect.tick = [](const SourceCommand& command, const TrackProgram& track, std::any& trackState,
                    PerformanceEmitter& out, VmApi& vm, const std::any& context) {
    static_cast<void>(command);
    static_cast<void>(track);
    static_cast<void>(vm);
    auto& state = std::any_cast<TrackState&>(trackState);
    const auto& typedContext = std::any_cast<const Context&>(context);
    state.tickAutomation(out, typedContext.version);
  };
  return KonamiSnesSequenceDescriptor{
      .dialect = std::move(dialect),
  };
}

}  // namespace

const KonamiSnesSequenceDescriptor& konamiSnesSequenceDescriptor(KonamiSnesVersion version) {
  static const KonamiSnesSequenceDescriptor none = makeDescriptor(KONAMISNES_NONE);
  static const KonamiSnesSequenceDescriptor v1 = makeDescriptor(KONAMISNES_V1);
  static const KonamiSnesSequenceDescriptor v2 = makeDescriptor(KONAMISNES_V2);
  static const KonamiSnesSequenceDescriptor v3 = makeDescriptor(KONAMISNES_V3);
  static const KonamiSnesSequenceDescriptor v4 = makeDescriptor(KONAMISNES_V4);
  static const KonamiSnesSequenceDescriptor v5 = makeDescriptor(KONAMISNES_V5);
  static const KonamiSnesSequenceDescriptor v6 = makeDescriptor(KONAMISNES_V6);

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
  registry.add(konamiSnesSequenceDescriptor(KONAMISNES_NONE).dialect);
  registry.add(konamiSnesSequenceDescriptor(KONAMISNES_V1).dialect);
  registry.add(konamiSnesSequenceDescriptor(KONAMISNES_V2).dialect);
  registry.add(konamiSnesSequenceDescriptor(KONAMISNES_V3).dialect);
  registry.add(konamiSnesSequenceDescriptor(KONAMISNES_V4).dialect);
  registry.add(konamiSnesSequenceDescriptor(KONAMISNES_V5).dialect);
  registry.add(konamiSnesSequenceDescriptor(KONAMISNES_V6).dialect);
}

TrackProgram decodeKonamiSnesSourceTrack(ByteReader reader, const KonamiSnesSequenceDescriptor& descriptor,
                                         u32 sourceTrackNumber, u32 startAddress, SourceMapBuilder* sourceMap,
                                         std::vector<Diagnostic>* diagnostics,
                                         std::optional<SourceAnnotationId> parentAnnotation,
                                         std::optional<AssetId> sequenceAsset) {
  return decodeCursorLinearTrack<TrackState, Context, KonamiSnesCursorReader>(
      reader, descriptor.dialect,
      CursorTrackDecodeInput{
          .sequenceAsset = sequenceAsset,
          .trackIndex = sourceTrackNumber,
          .startOffset = startAddress,
          .parentAnnotation = parentAnnotation,
          .sourceMap = sourceMap,
          .diagnostics = diagnostics,
          .maxCommands = 8192,
      });
}

SequenceProgramAsset parseKonamiSnesSequence(const ScanInput& input, const KonamiSnesLayout& layout,
                                             AssetId sequenceId, std::string_view displayName,
                                             SourceMapBuilder* sourceMap, std::vector<Diagnostic>* diagnostics) {
  u32 trackCount = kKonamiSnesMaxTracks;
  for (u32 trackNumber = 0; trackNumber < kKonamiSnesMaxTracks; ++trackNumber) {
    const u32 pointerOffset = layout.sequenceHeaderAddress + trackNumber * 2;
    if (!input.reader.has(pointerOffset, 2)) {
      trackCount = trackNumber;
      break;
    }
    const u16 trackAddress = input.reader.le16(pointerOffset);
    if (trackAddress >= layout.sequenceHeaderAddress &&
        trackAddress - layout.sequenceHeaderAddress < kKonamiSnesMaxTracks * 2) {
      trackCount = (trackAddress - layout.sequenceHeaderAddress) / 2;
      break;
    }
  }

  const SourceRange headerRange = input.reader.range(layout.sequenceHeaderAddress, trackCount * 2);
  SourceAnnotationId headerAnnotation;
  if (sourceMap != nullptr) {
    auto header = sourceMap->header("Sequence Header", headerRange)
                      .kind("konami-snes-sequence-header")
                      .owner(ObjectRefs::sequence(sequenceId))
                      .field("version", headerRange, konamiSnesVersionName(layout.version));
    headerAnnotation = header.id();
  }

  const auto& descriptor = konamiSnesSequenceDescriptor(layout.version);
  SequenceProgram program{
      .dialect = descriptor.dialect.id,
      .timebase = descriptor.dialect.timebase,
      .sourceBaseAddress = Address{layout.sequenceHeaderAddress},
      .behavior = descriptor.dialect.defaultBehavior,
  };

  for (u32 trackNumber = 0; trackNumber < trackCount; ++trackNumber) {
    const u32 pointerOffset = layout.sequenceHeaderAddress + trackNumber * 2;
    const SourceRange pointerRange = input.reader.range(pointerOffset, 2);
    const u16 trackAddress = input.reader.le16(pointerOffset);
    if (trackAddress == 0 || !input.reader.has(trackAddress, 1)) {
      continue;
    }

    std::optional<SourceAnnotationId> trackAnnotation;
    if (sourceMap != nullptr) {
      auto pointer = sourceMap->pointer("Track Pointer", pointerRange, SourceTarget{input.reader.range(trackAddress, 1)})
                         .kind("konami-snes-track-pointer")
                         .description(fmt::format("Track starts at ${:04X}", trackAddress))
                         .derived("source_track", trackNumber)
                         .field("destination", pointerRange, trackAddress, SourceValueDisplay::Address);
      if (headerAnnotation.valid()) {
        pointer.parent(headerAnnotation);
      }
      trackAnnotation = pointer.id();
    }

    auto track = decodeKonamiSnesSourceTrack(input.reader, descriptor, trackNumber, trackAddress, sourceMap,
                                             diagnostics, trackAnnotation, sequenceId);
    program.tracks.push_back(std::move(track));
  }

  return SequenceProgramAsset{
      .metadata =
          AssetMetadata{
              .id = sequenceId,
              .format = "KonamiSnes",
              .name = std::string(displayName),
              .range = headerRange,
          },
      .program = std::move(program),
  };
}

}  // namespace vgmtrans::formats::konami_snes
