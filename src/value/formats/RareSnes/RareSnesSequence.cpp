/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/RareSnes/RareSnes.h"

#include "value/sequence/CommandSourceMap.h"
#include "value/sequence/CompiledCommandDialect.h"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace vgmtrans::formats::rare_snes {

using namespace core;

namespace {

constexpr u32 kMaxTrackCommands = 32768;
constexpr double kTimerQuantumSeconds = 0.000125;
constexpr u32 kMonoOutputState = 1u << 16;

enum class Kind : u8 {
  Invalid,
  End,
  Program,
  Volume,
  Jump,
  Call,
  Return,
  DefaultDurationOn,
  DefaultDurationOff,
  PitchSlide,
  PitchSlideOff,
  Tempo,
  TempoAdd,
  Vibrato,
  VibratoOff,
  Adsr,
  MasterVolumeStereo,
  Tuning,
  Transpose,
  TransposeAdd,
  EchoParameters,
  EchoOn,
  EchoOff,
  EchoFir,
  NoiseClock,
  NoiseOn,
  NoiseOff,
  AltNote1,
  AltNote2,
  PitchSlidePingPong,
  ProgramVolume,
  FadeOut,
  Timer,
  LongDurationOn,
  LongDurationOff,
  SavePreset,
  LoadPreset,
  ConditionalJump,
  SetCondition,
  Tremolo,
  TremoloOff,
  CenterVolume,
  CallOnce,
  ResetAdsr,
  VoiceParametersShort,
  EchoDelay,
  VolumePresets,
  VoiceParameters,
  MasterVolumeScalar,
  AllLfoOff,
  BoundedVolumeMotion,
  Nop2,
  Nop4,
  DriverReset,
  // Battlemaniacs branch.
  BtmJump,
  BtmInstrument,
  BtmPitchEnvelope,
  BtmVolume,
  BtmAdsrKeyoff,
  BtmCpuPort,
  BtmMasterVolume,
  BtmGlobalTranspose,
  BtmGainKeyoff,
  BtmEchoOff,
  BtmEchoParameters,
  BtmFixedPan,
  BtmSavePreset,
  BtmLoadPreset,
  BtmMasterFade,
  BtmDspFlags,
};

struct DecodeState {
  bool longDuration = false;
  bool defaultDuration = false;
};

struct Preset {
  s8 left = 0;
  s8 right = 0;
  u8 adsr1 = 0;
  u8 adsr2 = 0;
  u8 keyoff = 0;
  Address source;
};

struct PitchEffect {
  bool enabled = false;
  u8 delay = 0;
  u8 interval = 1;
  u8 steps = 0;
  s8 delta = 0;
  u8 invertedSteps = 0;
  u8 pitchUnit = 1;
};

struct Vibrato {
  bool enabled = false;
  u8 period = 0;
  u8 interval = 1;
  s8 delta = 0;
  u8 delay = 0;
  u8 pitchUnit = 1;
};

struct Tremolo {
  bool enabled = false;
  u8 period = 0;
  u8 interval = 1;
  s8 delta = 0;
  u8 delay = 0;
};

struct CallFrame {
  u8 repeatSlot = 0;
  Address destination;
};

struct BattlemaniacsPercussion {
  PatchRecipe patch;
  s8 baseNote = 0;
  u8 keyoff = 0;
  s8 left = 0x7f;
  s8 right = 0x7f;
  u8 dspFlags = 0;
};

[[nodiscard]] Kind commonKind(u8 opcode) {
  switch (opcode) {
    case 0x00:
      return Kind::End;
    case 0x01:
      return Kind::Program;
    case 0x02:
      return Kind::Volume;
    case 0x03:
      return Kind::Jump;
    case 0x04:
      return Kind::Call;
    case 0x05:
      return Kind::Return;
    case 0x06:
      return Kind::DefaultDurationOn;
    case 0x07:
      return Kind::DefaultDurationOff;
    case 0x08:
    case 0x09:
      return Kind::PitchSlide;
    case 0x0a:
      return Kind::PitchSlideOff;
    case 0x0b:
      return Kind::Tempo;
    case 0x0c:
      return Kind::TempoAdd;
    case 0x0d:
    case 0x0f:
      return Kind::Vibrato;
    case 0x0e:
      return Kind::VibratoOff;
    case 0x10:
      return Kind::Adsr;
    case 0x11:
      return Kind::MasterVolumeStereo;
    case 0x12:
      return Kind::Tuning;
    case 0x13:
      return Kind::Transpose;
    case 0x14:
      return Kind::TransposeAdd;
    case 0x15:
      return Kind::EchoParameters;
    case 0x16:
      return Kind::EchoOn;
    case 0x17:
      return Kind::EchoOff;
    case 0x18:
      return Kind::EchoFir;
    case 0x19:
      return Kind::NoiseClock;
    case 0x1a:
      return Kind::NoiseOn;
    case 0x1b:
      return Kind::NoiseOff;
    case 0x1c:
      return Kind::AltNote1;
    case 0x1d:
      return Kind::AltNote2;
    case 0x26:
    case 0x27:
      return Kind::PitchSlidePingPong;
    case 0x2b:
      return Kind::LongDurationOn;
    case 0x2c:
      return Kind::LongDurationOff;
    default:
      return Kind::Invalid;
  }
}

[[nodiscard]] Kind kind(Profile profile, u8 opcode) {
  if (profile == Profile::Battlemaniacs) {
    if (opcode == 0x00) {
      return Kind::End;
    }
    if (opcode == 0x01) {
      return Kind::BtmJump;
    }
    if (opcode == 0x02) {
      return Kind::PitchSlideOff;
    }
    if (opcode == 0x09) {
      return Kind::VibratoOff;
    }
    if (opcode == 0x03) {
      return Kind::BtmInstrument;
    }
    if (opcode == 0x04) {
      return Kind::Call;
    }
    if (opcode == 0x05) {
      return Kind::Return;
    }
    if (opcode == 0x06) {
      return Kind::DefaultDurationOn;
    }
    if (opcode == 0x07) {
      return Kind::DefaultDurationOff;
    }
    if (opcode == 0x08 || opcode == 0x17) {
      return Kind::Vibrato;
    }
    if (opcode >= 0x0a && opcode <= 0x0b) {
      return Kind::BtmPitchEnvelope;
    }
    if (opcode == 0x0c) {
      return Kind::BtmVolume;
    }
    if (opcode == 0x0d) {
      return Kind::BtmAdsrKeyoff;
    }
    if (opcode == 0x0e) {
      return Kind::BtmCpuPort;
    }
    if (opcode == 0x0f) {
      return Kind::BtmMasterVolume;
    }
    if (opcode == 0x10) {
      return Kind::Tempo;
    }
    if (opcode == 0x11) {
      return Kind::TempoAdd;
    }
    if (opcode == 0x12) {
      return Kind::Transpose;
    }
    if (opcode == 0x13) {
      return Kind::TransposeAdd;
    }
    if (opcode == 0x14) {
      return Kind::BtmGlobalTranspose;
    }
    if (opcode == 0x15) {
      return Kind::BtmGainKeyoff;
    }
    if (opcode == 0x16) {
      return Kind::Tuning;
    }
    if (opcode >= 0x18 && opcode <= 0x19) {
      return Kind::BtmPitchEnvelope;
    }
    if (opcode == 0x1f) {
      return Kind::BtmEchoOff;
    }
    if (opcode == 0x20) {
      return Kind::NoiseClock;
    }
    if (opcode == 0x21) {
      return Kind::BtmEchoParameters;
    }
    if (opcode == 0x22) {
      return Kind::EchoFir;
    }
    if (opcode >= 0x23 && opcode <= 0x29) {
      return Kind::BtmFixedPan;
    }
    if (opcode >= 0x2a && opcode <= 0x2d) {
      return Kind::BtmSavePreset;
    }
    if (opcode >= 0x2e && opcode <= 0x31) {
      return Kind::BtmLoadPreset;
    }
    if (opcode == 0x32) {
      return Kind::BtmMasterFade;
    }
    if (opcode == 0x33) {
      return Kind::BtmDspFlags;
    }
    return Kind::Invalid;
  }

  Kind result = commonKind(opcode);
  switch (profile) {
    case Profile::BattletoadsDoubleDragon:
      if (opcode == 0x15) {
        result = Kind::Nop4;
      }
      if (opcode >= 0x1c && opcode <= 0x20) {
        result = Kind::SavePreset;
      }
      if (opcode >= 0x21 && opcode <= 0x25) {
        result = Kind::LoadPreset;
      }
      if (opcode == 0x28) {
        result = Kind::ProgramVolume;
      }
      if (opcode == 0x29) {
        result = Kind::FadeOut;
      }
      if (opcode == 0x2a) {
        result = Kind::Timer;
      }
      break;
    case Profile::DonkeyKongCountry:
      if (opcode >= 0x1c && opcode <= 0x20) {
        result = Kind::SavePreset;
      }
      if (opcode >= 0x21 && opcode <= 0x25) {
        result = Kind::LoadPreset;
      }
      if (opcode == 0x28) {
        result = Kind::ProgramVolume;
      }
      if (opcode == 0x29) {
        result = Kind::FadeOut;
      }
      if (opcode == 0x2a) {
        result = Kind::Timer;
      }
      if (opcode == 0x2d) {
        result = Kind::ConditionalJump;
      }
      if (opcode == 0x2e) {
        result = Kind::SetCondition;
      }
      if (opcode == 0x2f) {
        result = Kind::Tremolo;
      }
      if (opcode == 0x30) {
        result = Kind::TremoloOff;
      }
      break;
    case Profile::KillerInstinctBeta:
      if (opcode >= 0x1e && opcode <= 0x23) {
        result = Kind::Invalid;
      }
      if (opcode == 0x24) {
        result = Kind::AllLfoOff;
      }
      if (opcode == 0x25) {
        result = Kind::BoundedVolumeMotion;
      }
      if (opcode == 0x28) {
        result = Kind::ProgramVolume;
      }
      if (opcode == 0x29) {
        result = Kind::FadeOut;
      }
      if (opcode == 0x2a) {
        result = Kind::Timer;
      }
      if (opcode == 0x2d) {
        result = Kind::ConditionalJump;
      }
      if (opcode == 0x2e) {
        result = Kind::SetCondition;
      }
      if (opcode == 0x2f) {
        result = Kind::Tremolo;
      }
      if (opcode == 0x30) {
        result = Kind::TremoloOff;
      }
      break;
    case Profile::WinningRun:
      if (opcode >= 0x19 && opcode <= 0x1b) {
        result = Kind::Invalid;
      }
      if (opcode == 0x20) {
        result = Kind::MasterVolumeScalar;
      }
      if (opcode == 0x21) {
        result = Kind::CenterVolume;
      }
      if (opcode == 0x22) {
        result = Kind::Nop2;
      }
      if (opcode == 0x23) {
        result = Kind::CallOnce;
      }
      if (opcode == 0x24) {
        result = Kind::AllLfoOff;
      }
      if (opcode == 0x25) {
        result = Kind::BoundedVolumeMotion;
      }
      if (opcode == 0x28) {
        result = Kind::ProgramVolume;
      }
      if (opcode == 0x29) {
        result = Kind::FadeOut;
      }
      if (opcode == 0x2a) {
        result = Kind::Timer;
      }
      if (opcode == 0x2f) {
        result = Kind::Tremolo;
      }
      if (opcode == 0x30) {
        result = Kind::TremoloOff;
      }
      if (opcode == 0x31) {
        result = Kind::DriverReset;
      }
      break;
    case Profile::KillerInstinct:
      if (opcode == 0x0c || opcode == 0x0d || opcode == 0x11 || opcode == 0x15 || opcode == 0x18 ||
          (opcode >= 0x19 && opcode <= 0x1d)) {
        result = Kind::Invalid;
      }
      if (opcode == 0x1e) {
        result = Kind::CenterVolume;
      }
      if (opcode == 0x1f) {
        result = Kind::CallOnce;
      }
      if (opcode == 0x20) {
        result = Kind::ResetAdsr;
      }
      if (opcode == 0x21) {
        result = Kind::ResetAdsr;
      }
      if (opcode == 0x22) {
        result = Kind::VoiceParametersShort;
      }
      if (opcode == 0x23) {
        result = Kind::EchoDelay;
      }
      break;
    case Profile::DonkeyKongCountry2:
      if (opcode == 0x11) {
        result = Kind::Invalid;
      }
      if (opcode == 0x1e) {
        result = Kind::VolumePresets;
      }
      if (opcode == 0x1f) {
        result = Kind::EchoDelay;
      }
      if (opcode == 0x20 || opcode == 0x31) {
        result = Kind::LoadPreset;
      }
      if (opcode == 0x21) {
        result = Kind::CallOnce;
      }
      if (opcode == 0x22) {
        result = Kind::VoiceParameters;
      }
      if (opcode == 0x23) {
        result = Kind::CenterVolume;
      }
      if (opcode == 0x24) {
        result = Kind::MasterVolumeScalar;
      }
      if (opcode == 0x30 || opcode == 0x32) {
        result = Kind::EchoOff;
      }
      break;
    case Profile::Unknown:
    case Profile::Battlemaniacs:
      break;
  }
  return result;
}

[[nodiscard]] u32 tempoMicrosecondsPerQuarter(u8 tempo, u8 timer) {
  if (tempo == 0 || timer == 0) {
    return 60'000'000;
  }
  return static_cast<u32>(std::lround(kPpqn * (125.0 * timer) * 256.0 / tempo));
}

[[nodiscard]] double signedGain(s8 value) {
  return std::min(std::abs(static_cast<int>(value)) / 128.0, 1.0);
}

[[nodiscard]] u32 timelineTicks(u32 timerUpdates, u8 tempo) {
  return std::max<u32>(1, static_cast<u32>(std::lround(timerUpdates * (tempo / 256.0))));
}

[[nodiscard]] double pitchSemitones(u16 base, s32 delta) {
  if (base == 0) {
    return 0.0;
  }
  const double destination = std::max<double>(1.0, static_cast<double>(base) + delta);
  return 12.0 * std::log2(destination / base);
}

[[nodiscard]] u16 approximatePitch(double key, s8 tuning) {
  const double pitch = 4096.0 * std::exp2((key - 72.0) / 12.0) * ((1024.0 + tuning) / 1024.0);
  return static_cast<u16>(std::clamp(std::lround(pitch), 1l, 65535l));
}

struct ProgramState {
  explicit ProgramState(const SequenceProgram& program)
      : selected(static_cast<Profile>(program.config.profile)),
        initialTempo(static_cast<u8>(program.config.driverState)),
        initialTimer(static_cast<u8>(program.config.driverState >> 8)), sourcePrograms(program.sourceProgramMap) {
    for (const TrackProgram& track : program.tracks) {
      for (const SourceCommand& command : track.commands) {
        sourceRanges.emplace(command.address.value, command.range);
        if (command.semantic == SequenceSemantic::Jump && !command.flow.additionalTargets.empty()) {
          conditionalDestinations.emplace(command.address.value, command.flow.additionalTargets);
        }
      }
    }
    resetRuntime();
  }

  void resetRuntime() {
    tempo = initialTempo;
    timer = initialTimer;
    masterLeft = masterRight = 0x7f;
    globalTranspose = 0;
    condition = 0;
    echoMask = 0;
    echoLeft = echoRight = 0;
    echoFeedback = 0;
    echoDelay = 0;
    percussion = {};
  }

  [[nodiscard]] u8 srcn(u8 sourceProgram) const {
    if (sourceProgram < sourcePrograms.size()) {
      return static_cast<u8>(sourcePrograms[sourceProgram].key);
    }
    return sourceProgram;
  }

  [[nodiscard]] SourceRange source(Address address) const {
    const auto found = sourceRanges.find(address.value);
    return found == sourceRanges.end() ? SourceRange{} : found->second;
  }

  [[nodiscard]] u32 patch(PatchRecipe candidate) {
    const auto samePatch = [&](const PatchRecipe& recipe) {
      return recipe.sourceProgram == candidate.sourceProgram && recipe.srcn == candidate.srcn &&
             recipe.tuning == candidate.tuning && recipe.adsr1 == candidate.adsr1 && recipe.adsr2 == candidate.adsr2 &&
             recipe.gain == candidate.gain;
    };
    const auto found = std::ranges::find_if(recipes.patches, samePatch);
    if (found != recipes.patches.end()) {
      return found->key;
    }
    if (!collecting) {
      return 0;
    }
    candidate.key = static_cast<u32>(recipes.patches.size());
    recipes.patches.push_back(std::move(candidate));
    return recipes.patches.back().key;
  }

  void finishPrepass() {
    collecting = false;
    resetRuntime();
  }

  Profile selected;
  u8 initialTempo = 0;
  u8 initialTimer = 0;
  std::vector<InstrumentIdentity> sourcePrograms;
  std::map<u32, SourceRange> sourceRanges;
  std::map<u32, std::vector<Address>> conditionalDestinations;
  u8 tempo = 0;
  u8 timer = 0;
  s8 masterLeft = 0x7f;
  s8 masterRight = 0x7f;
  s8 globalTranspose = 0;
  u8 condition = 0;
  u8 echoMask = 0;
  s8 echoLeft = 0;
  s8 echoRight = 0;
  s8 echoFeedback = 0;
  u8 echoDelay = 0;
  std::array<std::optional<BattlemaniacsPercussion>, 8> percussion;
  SequenceRecipes recipes;
  bool collecting = true;
};

struct TrackState {
  TrackState(const SequenceProgram& program, const TrackProgram& track)
      : profile(static_cast<Profile>(program.config.profile)), trackNumber(track.sourceTrackNumber),
        monoOutput(profile == Profile::BattletoadsDoubleDragon ||
                   (program.config.driverState & kMonoOutputState) != 0) {
    if (profile == Profile::WinningRun) {
      adsr1 = 0x8f;
      adsr2 = 0xe0;
    } else if (profile == Profile::KillerInstinct || profile == Profile::DonkeyKongCountry2) {
      adsr1 = 0x8e;
      adsr2 = 0xe0;
    }
    if (profile == Profile::KillerInstinct) {
      left = right = 0x40;
    }
  }

  Profile profile = Profile::Unknown;
  u32 trackNumber = 0;
  bool monoOutput = false;
  u16 defaultDuration = 0;
  bool longDuration = false;
  u8 sourceProgram = 0;
  s8 left = 0x7f;
  s8 right = 0x7f;
  s8 transpose = 0;
  s8 tuning = 0;
  u8 adsr1 = 0;
  u8 adsr2 = 0;
  u8 gain = 0x7f;
  u8 keyoff = 0;
  u8 altNote1 = 0x81;
  u8 altNote2 = 0x81;
  std::array<Preset, 5> presets{};
  Address patchSource;
  PitchEffect pitch;
  Vibrato vibrato;
  Tremolo tremolo;
  std::vector<CallFrame> calls;
  PerformanceNoteId lastNote;
  std::optional<double> lastKey;
  u16 lastPitch = 0x1000;
};

struct Playback {
  TrackState& track;
  PerformanceEmitter& out;
  VmApi& vm;
  ProgramState& program;

  [[nodiscard]] std::pair<s8, s8> audibleVolumes(s8 left, s8 right) const {
    if (!track.monoOutput) {
      return {left, right};
    }

    int center = 0;
    if (track.profile == Profile::Battlemaniacs) {
      center = (std::abs(static_cast<int>(left)) + std::abs(static_cast<int>(right))) / 2;
    } else if (track.profile == Profile::DonkeyKongCountry2) {
      center = std::abs(static_cast<int>(left)) / 2 + std::abs(static_cast<int>(right)) / 2;
    } else {
      const u16 sum = static_cast<u8>(left) + static_cast<u8>(right);
      center = static_cast<s8>(static_cast<u8>(sum / 2));
    }
    const s8 mono = static_cast<s8>(center);
    return {mono, mono};
  }

  void volume(s8 left, s8 right) {
    track.left = left;
    track.right = right;
    const auto [audibleLeft, audibleRight] = audibleVolumes(left, right);
    if (track.monoOutput && track.profile != Profile::Battlemaniacs) {
      track.left = audibleLeft;
      track.right = audibleRight;
    }
    out.stereoBalance(signedGain(audibleLeft), signedGain(audibleRight));
  }

  void centerVolume(s8 value) { volume(value, value); }

  void programChange(u8 sourceProgram, Address source) {
    track.sourceProgram = sourceProgram;
    track.patchSource = source;
    if (track.profile == Profile::DonkeyKongCountry || track.profile == Profile::BattletoadsDoubleDragon) {
      track.tuning = 0;
    }
  }

  void programVolume(u8 sourceProgram, s8 left, s8 right, Address source) {
    programChange(sourceProgram, source);
    volume(left, right);
  }

  void adsr(u8 adsr1, u8 adsr2, Address source) {
    track.adsr1 = adsr1;
    track.adsr2 = adsr2;
    track.patchSource = source;
  }

  void tuning(s8 value, Address source) {
    track.tuning = value;
    track.patchSource = source;
  }

  void transpose(s8 value) { track.transpose = value; }
  void transposeAdd(s8 value) { track.transpose = static_cast<s8>(track.transpose + value); }

  void tempo(u8 value) {
    program.tempo = value;
    out.tempo(tempoMicrosecondsPerQuarter(program.tempo, program.timer));
  }

  void tempoAdd(s8 value) { tempo(static_cast<u8>(program.tempo + value)); }

  void timer(u8 value) {
    program.timer = value;
    out.tempo(tempoMicrosecondsPerQuarter(program.tempo, program.timer));
  }

  [[nodiscard]] LfoPerformanceContext lfoContext(u8 delay) const {
    const u32 delayUpdates = delay;
    return LfoPerformanceContext{
        .delayTicks =
            delayUpdates == 0 ? std::optional<u32>{0} : std::optional<u32>{timelineTicks(delayUpdates, program.tempo)},
        .delayMilliseconds = delayUpdates * program.timer * kTimerQuantumSeconds * 1000.0,
        .shape = LfoShape{.waveform = LfoWaveform::Triangle},
        .initialPhaseCycles = 0.0,
        .sampleImmediatelyOnNote = false,
        .tremoloGainMode = TremoloGainMode::BipolarAroundNominal,
    };
  }

  void emitVibrato() {
    if (!track.vibrato.enabled || track.vibrato.period == 0) {
      out.vibratoDepth(0.0);
      out.vibratoRate(0.0);
      out.vibratoDelayPhysical(0, 0.0);
      return;
    }
    const u32 interval = std::max<u32>(track.vibrato.interval, 1);
    const u32 cycleUpdates = std::max<u32>(1, track.vibrato.period) * interval * 2;
    const s32 excursion = std::abs(static_cast<int>(track.vibrato.delta)) * std::max<u32>(1, track.vibrato.period / 2) *
                          track.vibrato.pitchUnit;
    LfoPerformanceContext context = lfoContext(track.vibrato.delay);
    const double upward = pitchSemitones(track.lastPitch, excursion);
    const double downward = pitchSemitones(track.lastPitch, -excursion);
    context.pitchRangeSemitones = ModulationRange{.minimum = downward, .maximum = upward};
    out.vibratoDepth(std::max(std::abs(upward), std::abs(downward)), context);
    out.vibratoRate(1.0 / (cycleUpdates * program.timer * kTimerQuantumSeconds), context);
  }

  // Configuration is inert until the driver initializes the live counters for
  // a note. emitInstrumentAndModulation() publishes the resulting note LFO.
  void vibrato(u8 period, u8 interval, s8 delta, u8 delay, u8 pitchUnit) {
    track.vibrato = Vibrato{
        .enabled = period != 0,
        .period = period,
        .interval = std::max<u8>(interval, 1),
        .delta = delta,
        .delay = delay,
        .pitchUnit = pitchUnit,
    };
  }

  void vibratoOff() { track.vibrato = {}; }

  void emitTremolo() {
    if (!track.tremolo.enabled || track.tremolo.period == 0) {
      out.tremoloLinearGainDepth(0.0);
      out.tremoloRate(0.0);
      out.tremoloDelayPhysical(0, 0.0);
      return;
    }
    const u32 interval = std::max<u32>(track.tremolo.interval, 1);
    const u32 cycleUpdates = std::max<u32>(1, track.tremolo.period) * interval * 2;
    const double base = std::max({signedGain(track.left), signedGain(track.right), 1.0 / 128.0});
    const double excursion = std::min(1.0, std::abs(static_cast<int>(track.tremolo.delta)) *
                                               std::max<u32>(1, track.tremolo.period / 2) / 128.0 / base);
    LfoPerformanceContext context = lfoContext(track.tremolo.delay);
    out.tremoloLinearGainDepth(excursion, context);
    out.tremoloRate(1.0 / (cycleUpdates * program.timer * kTimerQuantumSeconds), context);
  }

  void tremolo(u8 period, u8 interval, s8 delta, u8 delay) {
    track.tremolo = Tremolo{
        .enabled = period != 0,
        .period = period,
        .interval = std::max<u8>(interval, 1),
        .delta = delta,
        .delay = delay,
    };
  }

  void tremoloOff() { track.tremolo = {}; }

  void allLfoOff() {
    track.pitch = {};
    vibratoOff();
    tremoloOff();
  }

  void configurePitch(bool inverted, u8 delay, u8 interval, u8 steps, s8 delta, u8 invertedSteps, u8 pitchUnit) {
    track.pitch = PitchEffect{
        .enabled = steps != 0,
        .delay = delay,
        .interval = std::max<u8>(interval, 1),
        .steps = steps,
        .delta = static_cast<s8>(inverted ? -delta : delta),
        .invertedSteps = invertedSteps,
        .pitchUnit = pitchUnit,
    };
  }

  void pitchOff() { track.pitch = {}; }

  [[nodiscard]] PatchRecipe currentPatch() const {
    return PatchRecipe{
        .sourceProgram = track.sourceProgram,
        .srcn = program.srcn(track.sourceProgram),
        .tuning = track.tuning,
        .adsr1 = track.adsr1,
        .adsr2 = track.adsr2,
        .gain = track.gain,
        .source = program.source(track.patchSource),
    };
  }

  void emitInstrumentAndModulation(PatchRecipe patch, double key) {
    const u32 identity = program.patch(std::move(patch));
    out.instrument(InstrumentIdentity{.domain = std::string(kInstrumentDomain), .key = identity});
    track.lastPitch = approximatePitch(key, track.tuning);
    emitVibrato();
    emitTremolo();
  }

  void beginPitchEffect(double key) {
    if (!track.pitch.enabled || track.pitch.steps == 0 || !track.lastNote.valid()) {
      return;
    }
    const u32 delayUpdates = track.pitch.delay;
    const u32 motionUpdates = static_cast<u32>(track.pitch.interval) * track.pitch.steps;
    const u32 delayTicks = delayUpdates == 0 ? 0 : timelineTicks(delayUpdates, program.tempo);
    const u32 durationTicks = timelineTicks(std::max<u32>(motionUpdates, 1), program.tempo);
    const double milliseconds = motionUpdates * program.timer * kTimerQuantumSeconds * 1000.0;
    const u32 invertedSteps = std::min<u32>(track.pitch.invertedSteps, track.pitch.steps);
    const s32 totalOffset = static_cast<s32>(track.pitch.delta) *
                            (static_cast<s32>(track.pitch.steps) - static_cast<s32>(invertedSteps * 2)) *
                            track.pitch.pitchUnit;
    auto output = out.at(vm.tick() + delayTicks);
    const double target = key + pitchSemitones(track.lastPitch, totalOffset);
    auto binding =
        output.pitchSlide(track.lastNote, key, target, PitchSlideTiming::fixedDuration(durationTicks, milliseconds));
    s32 offset = 0;
    const s32 delta = static_cast<s32>(track.pitch.delta) * track.pitch.pitchUnit;
    for (u32 step = 1; step <= track.pitch.steps; ++step) {
      offset += step <= invertedSteps ? -delta : delta;
      const u32 stepUpdates = static_cast<u32>(track.pitch.interval) * step;
      const u32 stepTicks = std::min(timelineTicks(stepUpdates, program.tempo), durationTicks);
      binding.sample(out.at(vm.tick() + delayTicks + stepTicks), key + pitchSemitones(track.lastPitch, offset));
    }
  }

  [[nodiscard]] Effects note(u8 encoded, u16 duration) {
    if (encoded == 0x80) {
      track.lastNote = {};
      track.lastKey.reset();
      return Effects::wait(duration);
    }
    u8 note = encoded;
    if (track.profile != Profile::DonkeyKongCountry && track.profile != Profile::BattletoadsDoubleDragon &&
        track.profile != Profile::Battlemaniacs) {
      if (note == 0xe1) {
        note = track.altNote2;
      } else if (note >= 0xe0) {
        note = track.altNote1;
      }
    }
    const double key = static_cast<double>(note - 0x81 + 36) + track.transpose + program.globalTranspose;
    emitInstrumentAndModulation(currentPatch(), key);
    const u32 sounding = track.keyoff == 0 ? duration : std::min<u32>(duration, track.keyoff);
    track.lastNote = out.note(key, 1.0, sounding);
    track.lastKey = key;
    beginPitchEffect(key);
    return Effects::wait(duration);
  }

  [[nodiscard]] Effects battlemaniacsPercussion(u8 encoded, u16 duration) {
    const u8 slot = static_cast<u8>((encoded >> 4) & 7);
    const s8 offset = static_cast<s8>((encoded & 0x0f) - 8);
    const auto& percussion = program.percussion[slot];
    if (!percussion) {
      return Effects::wait(duration);
    }
    const double key = percussion->baseNote + offset + 35.0;
    // Percussion voices skip NON, so their second flag bit is EON. Melodic
    // voices use PMON/NON/EON in bits 0/1/2.
    echoChannel((percussion->dspFlags & 0x02) != 0);
    volume(percussion->left, percussion->right);
    emitInstrumentAndModulation(percussion->patch, key);
    const u32 sounding = percussion->keyoff == 0 ? duration : std::min<u32>(duration, percussion->keyoff);
    track.lastNote = out.note(key, 1.0, sounding);
    track.lastKey = key;
    beginPitchEffect(key);
    return Effects::wait(duration);
  }

  void call(u8 times, Address destination) {
    const u8 repeatSlot = static_cast<u8>(track.calls.size());
    vm.repeatCounter(repeatSlot).start(times == 0 ? 256u : static_cast<u32>(times));
    track.calls.push_back(CallFrame{
        .repeatSlot = repeatSlot,
        .destination = destination,
    });
  }

  [[nodiscard]] Effects return_() {
    if (track.calls.empty()) {
      return vm.end();
    }
    CallFrame& frame = track.calls.back();
    RepeatCounter counter = vm.repeatCounter(frame.repeatSlot);
    if (counter.consumeReplay()) {
      return vm.finiteBranch(frame.destination);
    }
    counter.finish();
    track.calls.pop_back();
    return vm.return_();
  }

  [[nodiscard]] Effects conditional(Address command) {
    const auto found = program.conditionalDestinations.find(command.value);
    if (found == program.conditionalDestinations.end() || found->second.empty()) {
      return vm.end();
    }
    const size_t index = std::min<size_t>(program.condition, found->second.size() - 1);
    return vm.finiteBranch(found->second[index]);
  }

  void savePreset(u8 slot, s8 left, s8 right, u8 adsr1, u8 adsr2, u8 keyoff, Address source) {
    if (slot < track.presets.size()) {
      track.presets[slot] = Preset{
          .left = left,
          .right = right,
          .adsr1 = adsr1,
          .adsr2 = adsr2,
          .keyoff = keyoff,
          .source = source,
      };
    }
  }

  void loadPreset(u8 slot, bool volumeOnly) {
    if (slot >= track.presets.size()) {
      return;
    }
    const Preset& preset = track.presets[slot];
    volume(preset.left, preset.right);
    if (!volumeOnly) {
      track.adsr1 = preset.adsr1;
      track.adsr2 = preset.adsr2;
      track.keyoff = preset.keyoff;
      track.patchSource = preset.source;
    }
  }

  void volumePresets(s8 left1, s8 right1, s8 left2, s8 right2) {
    track.presets[0].left = left1;
    track.presets[0].right = right1;
    track.presets[1].left = left2;
    track.presets[1].right = right2;
  }

  void resetAdsr(bool hard, Address source) {
    track.adsr1 = hard ? 0x8f : 0x8e;
    track.adsr2 = 0xe0;
    track.patchSource = source;
  }

  void voiceShort(u8 sourceProgram, s8 transposeValue, s8 tuningValue, Address source) {
    programChange(sourceProgram, source);
    track.transpose = transposeValue;
    track.tuning = tuningValue;
  }

  void voice(u8 sourceProgram, s8 transposeValue, s8 tuningValue, s8 left, s8 right, u8 adsr1, u8 adsr2,
             Address source) {
    voiceShort(sourceProgram, transposeValue, tuningValue, source);
    volume(left, right);
    adsr(adsr1, adsr2, source);
  }

  void masterStereo(s8 left, s8 right) {
    const auto [audibleLeft, audibleRight] = audibleVolumes(left, right);
    program.masterLeft = audibleLeft;
    program.masterRight = audibleRight;
    out.masterLevel(std::max(signedGain(audibleLeft), signedGain(audibleRight)));
  }

  void masterScalar(u8 value) { out.masterLevel(value / 100.0); }

  void echoParameters(s8 feedback, s8 left, s8 right, std::optional<u8> delay = std::nullopt) {
    program.echoFeedback = feedback;
    program.echoLeft = left;
    program.echoRight = right;
    if (delay) {
      program.echoDelay = *delay;
    }
    out.reverb(ReverbPerformanceEvent{
        .voiceMask = program.echoMask,
        .send = std::max(signedGain(left), signedGain(right)),
        .leftGain = static_cast<double>(left) / 128.0,
        .rightGain = static_cast<double>(right) / 128.0,
        .delayMilliseconds = program.echoDelay * 16.0,
        .feedback = static_cast<double>(feedback) / 128.0,
    });
  }

  void echoChannel(bool enabled) {
    const u8 bit = track.trackNumber < 8 ? static_cast<u8>(1u << track.trackNumber) : 0;
    if (enabled) {
      program.echoMask |= bit;
    } else {
      program.echoMask &= static_cast<u8>(~bit);
    }
    echoParameters(program.echoFeedback, program.echoLeft, program.echoRight);
  }

  void echoAllOff() {
    program.echoMask = 0;
    echoParameters(0, 0, 0, u8{0});
  }

  void echoDelay(u8 encoded) {
    program.echoDelay = static_cast<u8>((encoded >> 1) & 0x0f);
    echoParameters(program.echoFeedback, program.echoLeft, program.echoRight);
  }

  void fadeOut(u8 amount) {
    if (amount == 0) {
      return;
    }
    const u32 updates = static_cast<u32>(127 * 256 / amount);
    static_cast<void>(out.fade(PerformanceAutomationTarget::MasterLevel, 0.0, timelineTicks(updates, program.tempo)));
  }

  void boundedVolumeMotion(u8 flags, u8 interval, u8 delta, u8 delay, s8 minimum, s8 maximum) {
    const s8 targetLeft = (flags & 0x01) != 0 ? maximum : ((flags & 0x02) != 0 ? minimum : track.left);
    const s8 targetRight = (flags & 0x10) != 0 ? maximum : ((flags & 0x20) != 0 ? minimum : track.right);
    const int distance = std::max(std::abs(static_cast<int>(targetLeft) - track.left),
                                  std::abs(static_cast<int>(targetRight) - track.right));
    const u32 updates = delta == 0 ? 0 : static_cast<u32>((distance + delta - 1) / delta) * std::max<u8>(interval, 1);
    const u32 duration = updates == 0 ? 0 : timelineTicks(updates, program.tempo);
    const u32 delayTicks = delay == 0 ? 0 : timelineTicks(delay, program.tempo);
    const double leftGain = signedGain(targetLeft);
    const double rightGain = signedGain(targetRight);
    const double total = leftGain + rightGain;
    const double pan = total == 0.0 ? 0.0 : std::clamp((rightGain - leftGain) / total, -1.0, 1.0);
    static_cast<void>(out.fade(PerformanceAutomationTarget::Pan, pan, duration, delayTicks));
    static_cast<void>(
        out.fade(PerformanceAutomationTarget::Level, std::max(leftGain, rightGain), duration, delayTicks));
    track.left = targetLeft;
    track.right = targetRight;
  }

  void btmInstrument(u8 slotOrSrcn, u8 srcn, u8 adsr1, u8 adsr2, u8 gain, s8 left, s8 right, u8 dspFlags, u8 keyoff,
                     s8 tuningOrBase, Address source) {
    PatchRecipe patch{
        .sourceProgram = slotOrSrcn,
        .srcn = srcn,
        .tuning = track.trackNumber == 5 ? s8{0} : tuningOrBase,
        .adsr1 = adsr1,
        .adsr2 = adsr2,
        .gain = gain,
        .source = program.source(source),
    };
    if (track.trackNumber == 5) {
      program.percussion[slotOrSrcn & 7] = BattlemaniacsPercussion{
          .patch = patch,
          .baseNote = tuningOrBase,
          .keyoff = keyoff,
          .left = left,
          .right = right,
          .dspFlags = dspFlags,
      };
      return;
    }
    track.sourceProgram = slotOrSrcn;
    track.tuning = tuningOrBase;
    track.adsr1 = adsr1;
    track.adsr2 = adsr2;
    track.gain = gain;
    track.keyoff = keyoff;
    track.patchSource = source;
    volume(left, right);
    echoChannel((dspFlags & 0x04) != 0);
  }

  void btmAdsrKeyoff(u8 adsr1, u8 adsr2, u8 keyoff, Address source) {
    adsr(adsr1, adsr2, source);
    track.keyoff = keyoff;
  }

  void btmPercussionVolume(u8 slot, s8 left, s8 right) {
    auto& percussion = program.percussion[slot & 7];
    if (!percussion) {
      return;
    }
    percussion->left = left;
    percussion->right = right;
  }

  void btmPercussionAdsrKeyoff(u8 slot, u8 adsr1, u8 adsr2, u8 keyoff, Address source) {
    auto& percussion = program.percussion[slot & 7];
    if (!percussion) {
      return;
    }
    percussion->patch.adsr1 = adsr1;
    percussion->patch.adsr2 = adsr2;
    percussion->patch.source = program.source(source);
    percussion->keyoff = keyoff;
  }

  void btmGainKeyoff(u8 gain, u8 keyoff, Address source) {
    track.adsr1 = 0;
    track.gain = gain;
    track.keyoff = keyoff;
    track.patchSource = source;
  }

  void btmPercussionGainKeyoff(u8 slot, u8 gain, u8 keyoff, Address source) {
    auto& percussion = program.percussion[slot & 7];
    if (!percussion) {
      return;
    }
    percussion->patch.adsr1 = 0;
    percussion->patch.gain = gain;
    percussion->patch.source = program.source(source);
    percussion->keyoff = keyoff;
  }

  void btmDspFlags(u8 slot, u8 flags) {
    if (track.trackNumber == 5) {
      auto& percussion = program.percussion[slot & 7];
      if (percussion) {
        percussion->dspFlags = flags;
      }
      return;
    }
    echoChannel((flags & 0x04) != 0);
  }

  void globalTranspose(s8 value) { program.globalTranspose = value; }

  void fixedPan(u8 index) {
    index = std::min<u8>(index, 6);
    const u8 steps = index <= 3 ? static_cast<u8>(3 - index) : static_cast<u8>(7 - index);
    const int total = std::min(127, std::abs(static_cast<int>(track.left)) + std::abs(static_cast<int>(track.right)));
    static constexpr std::array<int, 4> rounding{0, 3, 2, 1};
    const int quiet = std::min(total, (total >> (steps + 1)) + rounding[steps]);
    const int loud = total - quiet;
    const bool panRight = index > 3;
    const int leftMagnitude = panRight ? quiet : loud;
    const int rightMagnitude = panRight ? loud : quiet;
    const auto signedLike = [](int magnitude, s8 source) {
      return static_cast<s8>(source < 0 ? -magnitude : magnitude);
    };
    volume(signedLike(leftMagnitude, track.left), signedLike(rightMagnitude, track.right));
  }

  void btmMasterFade(s8 leftStep, s8 rightStep, u8 interval, u8 steps) {
    if (track.monoOutput) {
      return;
    }
    const auto applySteps = [steps](s8 value, s8 delta) {
      int result = value;
      for (u32 step = 0; step < steps; ++step) {
        result = std::clamp(result + static_cast<int>(delta), -128, 127);
      }
      return static_cast<s8>(result);
    };
    program.masterLeft = applySteps(program.masterLeft, leftStep);
    program.masterRight = applySteps(program.masterRight, rightStep);
    const double target = std::max(signedGain(program.masterLeft), signedGain(program.masterRight));
    const u32 duration = static_cast<u32>(interval + 1) * steps;
    static_cast<void>(out.fade(PerformanceAutomationTarget::MasterLevel, target, duration));
  }
};

using Cursor = CompilerCursor<TrackState, Playback>;

[[nodiscard]] DecodedBytecodeCommand decodeNote(Cursor& cursor, Profile profile, DecodeState& state, u32 trackNumber) {
  auto event = cursor.command(cursor.opcode() == 0x80 ? "Rest" : "Note",
                              cursor.opcode() == 0x80 ? SequenceSemantic::Rest : SequenceSemantic::Note,
                              CommandPlaybackStatus::AffectsPlayback, cursor.opcode() == 0x80 ? "rest" : "note");
  const u8 encoded =
      event.opcodeValue("key", cursor.opcode(), SourceValueDisplay::MidiNote, SemanticOperandRole::NoteKey);
  u16 duration = 0;
  if (state.defaultDuration) {
    duration = event.derived("duration", u16{0}, SemanticOperandRole::Duration);
  } else if (state.longDuration) {
    duration = event.u16be("duration", SourceValueDisplay::Default, SemanticOperandRole::Duration);
  } else {
    duration = event.u8("duration", SemanticOperandRole::Duration);
  }
  if (state.defaultDuration) {
    if (profile == Profile::Battlemaniacs && trackNumber == 5) {
      return event.invoke(
          [](Playback& playback, u8 note) {
            return playback.battlemaniacsPercussion(note, playback.track.defaultDuration);
          },
          encoded);
    }
    return event.invoke([](Playback& playback, u8 note) { return playback.note(note, playback.track.defaultDuration); },
                        encoded);
  }
  if (profile == Profile::Battlemaniacs && trackNumber == 5) {
    return event.invoke<&Playback::battlemaniacsPercussion>(encoded, duration);
  }
  return event.invoke<&Playback::note>(encoded, duration);
}

[[nodiscard]] std::vector<Address> conditionalDestinations(ByteReader reader, u32 position, u32 floor) {
  std::vector<Address> result;
  for (u32 index = 0; index < 16 && reader.has(position + index * 2, 2); ++index) {
    const u16 address = reader.le16(position + index * 2);
    if (address < floor || !reader.has(address, 1)) {
      break;
    }
    result.push_back(Address{address});
  }
  return result;
}

[[nodiscard]] DecodedBytecodeCommand decodeCommand(ByteReader reader, Profile profile, u32 trackNumber, u32 begin,
                                                   u32 sequenceDataFloor, DecodeState& state,
                                                   std::vector<Diagnostic>* diagnostics) {
  Cursor cursor(reader, begin, kAramSize, "rare-snes", diagnostics);
  if (!cursor.hasOpcode()) {
    return cursor.truncated();
  }
  const u8 opcode = cursor.opcode();
  if (opcode >= 0x80) {
    return decodeNote(cursor, profile, state, trackNumber);
  }

  const Kind selected = kind(profile, opcode);
  const Address source{begin};
  switch (selected) {
    case Kind::End:
      return cursor.command("End", SequenceSemantic::End).end();
    case Kind::Program: {
      auto event = cursor.command("Program", SequenceSemantic::Program);
      const u8 value = event.u8("program", SemanticOperandRole::InstrumentProgram);
      return event.invoke<&Playback::programChange>(value, source);
    }
    case Kind::Volume:
    case Kind::BtmVolume: {
      auto event = cursor.command("Volume L/R", SequenceSemantic::Level);
      if (selected == Kind::BtmVolume && trackNumber == 5) {
        const u8 slot = event.u8("percussion_slot", SemanticOperandRole::InstrumentProgram);
        const s8 left = event.s8("left", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Level);
        const s8 right = event.s8("right", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Level);
        return event.invoke<&Playback::btmPercussionVolume>(slot, left, right);
      }
      return event.invoke<&Playback::volume>(
          event.s8("left", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Level),
          event.s8("right", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Level));
    }
    case Kind::CenterVolume: {
      auto event = cursor.command("Centered Volume", SequenceSemantic::Level);
      return event.invoke<&Playback::centerVolume>(
          event.s8("volume", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Level));
    }
    case Kind::Jump:
    case Kind::BtmJump: {
      auto event = cursor.command("Jump", SequenceSemantic::Jump);
      return event.loopCandidate(event.addressLe("destination", SemanticOperandRole::LoopTarget));
    }
    case Kind::Call:
    case Kind::CallOnce: {
      auto event = cursor.command(selected == Kind::Call ? "Pattern Repeat" : "Pattern Play", SequenceSemantic::Call);
      const u8 times = selected == Kind::Call ? event.u8("times", SemanticOperandRole::Count) : u8{1};
      const Address destination = event.addressLe("destination", SemanticOperandRole::CallTarget);
      return event.invoke<&Playback::call>(times, destination).call(destination);
    }
    case Kind::Return: {
      auto event = cursor.command("Pattern Return", SequenceSemantic::Return);
      event.invoke<&Playback::return_>();
      return event.discoverReturn();
    }
    case Kind::DefaultDurationOn: {
      auto event = cursor.command("Default Duration On", SequenceSemantic::State);
      const u16 duration = state.longDuration
                               ? event.u16be("duration", SourceValueDisplay::Default, SemanticOperandRole::Duration)
                               : event.u8("duration", SemanticOperandRole::Duration);
      state.defaultDuration = duration != 0;
      return event.set<&TrackState::defaultDuration>(duration);
    }
    case Kind::DefaultDurationOff:
      state.defaultDuration = false;
      return cursor.command("Default Duration Off", SequenceSemantic::State).set<&TrackState::defaultDuration>(u16{0});
    case Kind::LongDurationOn:
      state.longDuration = true;
      return cursor.command("16-Bit Durations On", SequenceSemantic::State).set<&TrackState::longDuration>(true);
    case Kind::LongDurationOff:
      state.longDuration = false;
      return cursor.command("16-Bit Durations Off", SequenceSemantic::State).set<&TrackState::longDuration>(false);
    case Kind::PitchSlide:
    case Kind::PitchSlidePingPong: {
      const bool shortEnvelope = selected == Kind::PitchSlidePingPong;
      auto event =
          cursor.command(!shortEnvelope ? "Pitch Envelope" : "Ping-Pong Pitch Envelope", SequenceSemantic::Pitch);
      const u8 delay = event.u8("delay", SemanticOperandRole::Duration);
      const u8 interval = event.u8("interval", SemanticOperandRole::Duration);
      const u8 encodedSteps = event.u8(shortEnvelope ? "half_cycle_steps" : "steps", SemanticOperandRole::Count);
      const s8 delta = event.s8("delta", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Pitch);
      const u8 steps = shortEnvelope ? static_cast<u8>(encodedSteps * 2) : encodedSteps;
      const u8 invertedSteps = shortEnvelope ? encodedSteps : event.u8("inverted_steps", SemanticOperandRole::Count);
      return event.invoke<&Playback::configurePitch>(opcode == 0x09 || opcode == 0x26, delay, interval, steps, delta,
                                                     invertedSteps, u8{1});
    }
    case Kind::PitchSlideOff:
      return cursor.command("Pitch Envelope Off", SequenceSemantic::Pitch).invoke<&Playback::pitchOff>();
    case Kind::Tempo: {
      auto event = cursor.command("Tempo", SequenceSemantic::Tempo);
      return event.invoke<&Playback::tempo>(event.u8("tempo"));
    }
    case Kind::TempoAdd: {
      auto event = cursor.command("Relative Tempo", SequenceSemantic::Tempo);
      return event.invoke<&Playback::tempoAdd>(event.s8("delta"));
    }
    case Kind::Vibrato: {
      auto event = cursor.command("Vibrato", SequenceSemantic::Modulation);
      const u8 period = event.u8("period", SemanticOperandRole::Modulation);
      const u8 interval = event.u8("interval", SemanticOperandRole::Duration);
      const s8 delta = event.s8("delta", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Pitch);
      const u8 delay = opcode == 0x0d || opcode == 0x08 ? 0 : event.u8("delay", SemanticOperandRole::Duration);
      const bool battlemaniacs = profile == Profile::Battlemaniacs;
      return event.invoke<&Playback::vibrato>(period, static_cast<u8>(battlemaniacs ? interval + 1 : interval), delta,
                                              delay, static_cast<u8>(battlemaniacs ? 8 : 1));
    }
    case Kind::VibratoOff:
      return cursor.command("Vibrato Off", SequenceSemantic::Modulation).invoke<&Playback::vibratoOff>();
    case Kind::Adsr: {
      auto event = cursor.command("ADSR", SequenceSemantic::State);
      const u8 adsr1 = event.u8("adsr1", SourceValueDisplay::Hex);
      const u8 adsr2 = event.u8("adsr2", SourceValueDisplay::Hex);
      return event.invoke<&Playback::adsr>(adsr1, adsr2, source);
    }
    case Kind::MasterVolumeStereo:
    case Kind::BtmMasterVolume: {
      auto event = cursor.command("Master Volume L/R", SequenceSemantic::Level);
      return event.invoke<&Playback::masterStereo>(
          event.s8("left", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Level),
          event.s8("right", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Level));
    }
    case Kind::MasterVolumeScalar: {
      auto event = cursor.command("Master Volume", SequenceSemantic::Level);
      return event.invoke<&Playback::masterScalar>(event.u8("percent", SemanticOperandRole::Level));
    }
    case Kind::Tuning: {
      if (profile == Profile::Battlemaniacs && trackNumber == 5) {
        auto event = cursor.noOp("Percussion Fine Tuning NOP", "percussion-tuning-nop");
        static_cast<void>(event.rawBytes("reserved", 1));
        return event.ignore();
      }
      auto event = cursor.command("Fine Tuning", SequenceSemantic::Pitch);
      return event.invoke<&Playback::tuning>(
          event.s8("tuning", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Pitch), source);
    }
    case Kind::Transpose: {
      auto event = cursor.command("Transpose", SequenceSemantic::Pitch);
      return event.invoke<&Playback::transpose>(
          event.s8("semitones", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Pitch));
    }
    case Kind::TransposeAdd: {
      auto event = cursor.command("Relative Transpose", SequenceSemantic::Pitch);
      return event.invoke<&Playback::transposeAdd>(
          event.s8("semitones", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Pitch));
    }
    case Kind::EchoParameters: {
      auto event = cursor.command("Echo Parameters", SequenceSemantic::State);
      const s8 feedback = event.s8("feedback", SourceValueDisplay::SignedDecimal);
      const s8 left = event.s8("left", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Level);
      const s8 right = event.s8("right", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Level);
      return event.invoke([](Playback& playback, s8 fb, s8 l, s8 r) { playback.echoParameters(fb, l, r); }, feedback,
                          left, right);
    }
    case Kind::EchoOn:
      return cursor.command("Echo On", SequenceSemantic::State).invoke<&Playback::echoChannel>(true);
    case Kind::EchoOff:
      return cursor.command("Echo Off", SequenceSemantic::State).invoke<&Playback::echoChannel>(false);
    case Kind::BtmEchoOff:
      return cursor.command("Echo All Off", SequenceSemantic::State).invoke<&Playback::echoAllOff>();
    case Kind::EchoFir: {
      auto event = cursor.sourceOnly("Echo FIR", "echo-fir");
      static_cast<void>(event.rawBytes("coefficients", 8));
      return event.ignore();
    }
    case Kind::NoiseClock: {
      auto event = cursor.sourceOnly(profile == Profile::Battlemaniacs ? "Noise Clock / Echo Writes" : "Noise Clock",
                                     "noise-clock");
      event.u8("clock", SourceValueDisplay::Hex, SemanticOperandRole::State);
      return event.ignore();
    }
    case Kind::NoiseOn:
      return cursor.sourceOnly("Noise On", "noise-on").ignore();
    case Kind::NoiseOff:
      return cursor.sourceOnly("Noise Off", "noise-off").ignore();
    case Kind::AltNote1:
    case Kind::AltNote2: {
      auto event =
          cursor.command(selected == Kind::AltNote1 ? "Alternate Note 1" : "Alternate Note 2", SequenceSemantic::State);
      const u8 note = event.u8("note", SourceValueDisplay::MidiNote, SemanticOperandRole::NoteKey);
      return selected == Kind::AltNote1 ? event.set<&TrackState::altNote1>(note)
                                        : event.set<&TrackState::altNote2>(note);
    }
    case Kind::ProgramVolume: {
      auto event = cursor.command("Program And Volume", SequenceSemantic::Program);
      const u8 programValue = event.u8("program", SemanticOperandRole::InstrumentProgram);
      const s8 left = event.s8("left", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Level);
      const s8 right = event.s8("right", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Level);
      return event.invoke<&Playback::programVolume>(programValue, left, right, source);
    }
    case Kind::FadeOut: {
      auto event = cursor.command("Fade Out", SequenceSemantic::Level);
      return event.invoke<&Playback::fadeOut>(event.u8("step", SemanticOperandRole::Level));
    }
    case Kind::Timer: {
      auto event = cursor.command("Timer 0 Frequency", SequenceSemantic::Tempo);
      return event.invoke<&Playback::timer>(event.u8("frequency"));
    }
    case Kind::SavePreset:
    case Kind::BtmSavePreset: {
      const u8 base = selected == Kind::BtmSavePreset ? 0x2a : 0x1c;
      const u8 slot = static_cast<u8>(opcode - base);
      auto event = cursor.command("Save Volume/Envelope Preset", SequenceSemantic::State);
      const s8 left = event.s8("left", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Level);
      const s8 right = event.s8("right", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Level);
      const u8 adsr1 = event.u8("adsr1", SourceValueDisplay::Hex);
      const u8 adsr2 = event.u8("adsr2", SourceValueDisplay::Hex);
      const u8 keyoff = selected == Kind::BtmSavePreset ? event.u8("keyoff", SemanticOperandRole::Duration) : 0;
      return event.invoke<&Playback::savePreset>(slot, left, right, adsr1, adsr2, keyoff, source);
    }
    case Kind::LoadPreset:
    case Kind::BtmLoadPreset: {
      u8 slot = 0;
      bool volumeOnly = false;
      if (selected == Kind::BtmLoadPreset) {
        slot = static_cast<u8>(opcode - 0x2e);
      } else if (profile == Profile::DonkeyKongCountry2) {
        slot = opcode == 0x31 ? 1 : 0;
        volumeOnly = true;
      } else {
        slot = static_cast<u8>(opcode - 0x21);
      }
      return cursor.command("Load Volume/Envelope Preset", SequenceSemantic::State)
          .invoke<&Playback::loadPreset>(slot, volumeOnly);
    }
    case Kind::ConditionalJump: {
      auto event = cursor.command("Conditional Jump", SequenceSemantic::Jump);
      const auto destinations = conditionalDestinations(reader, begin + 1, sequenceDataFloor);
      for (u32 index = 0; index < destinations.size(); ++index) {
        const Address destination =
            event.addressLe(fmt::format("destination_{}", index), SemanticOperandRole::JumpTarget);
        event.mayBranchTo(destination);
      }
      event.invoke<&Playback::conditional>(source);
      return event.requireRuntimeControlFlow();
    }
    case Kind::SetCondition: {
      auto event = cursor.command("Set Conditional Index", SequenceSemantic::State);
      return event.invoke([](Playback& playback, u8 value) { playback.program.condition = value; },
                          event.u8("index", SemanticOperandRole::State));
    }
    case Kind::Tremolo: {
      auto event = cursor.command("Tremolo", SequenceSemantic::Modulation);
      const u8 period = event.u8("period", SemanticOperandRole::Modulation);
      const u8 interval = event.u8("interval", SemanticOperandRole::Duration);
      const s8 delta = event.s8("delta", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Level);
      const u8 delay = event.u8("delay", SemanticOperandRole::Duration);
      return event.invoke<&Playback::tremolo>(period, interval, delta, delay);
    }
    case Kind::TremoloOff:
      return cursor.command("Tremolo Off", SequenceSemantic::Modulation).invoke<&Playback::tremoloOff>();
    case Kind::AllLfoOff:
      return cursor.command("All Pitch/Volume LFOs Off", SequenceSemantic::Modulation).invoke<&Playback::allLfoOff>();
    case Kind::ResetAdsr:
      return cursor.command(opcode == 0x20 ? "Reset ADSR" : "Reset ADSR Soft", SequenceSemantic::State)
          .invoke<&Playback::resetAdsr>(opcode == 0x20, source);
    case Kind::VoiceParametersShort: {
      auto event = cursor.command("Voice Parameters", SequenceSemantic::Program);
      const u8 programValue = event.u8("program", SemanticOperandRole::InstrumentProgram);
      const s8 transposeValue = event.s8("transpose", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Pitch);
      const s8 tuningValue = event.s8("tuning", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Pitch);
      return event.invoke<&Playback::voiceShort>(programValue, transposeValue, tuningValue, source);
    }
    case Kind::VoiceParameters: {
      auto event = cursor.command("Voice Parameters", SequenceSemantic::Program);
      const u8 programValue = event.u8("program", SemanticOperandRole::InstrumentProgram);
      const s8 transposeValue = event.s8("transpose", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Pitch);
      const s8 tuningValue = event.s8("tuning", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Pitch);
      const s8 left = event.s8("left", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Level);
      const s8 right = event.s8("right", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Level);
      const u8 adsr1 = event.u8("adsr1", SourceValueDisplay::Hex);
      const u8 adsr2 = event.u8("adsr2", SourceValueDisplay::Hex);
      return event.invoke<&Playback::voice>(programValue, transposeValue, tuningValue, left, right, adsr1, adsr2,
                                            source);
    }
    case Kind::EchoDelay: {
      auto event = cursor.command("Echo Delay", SequenceSemantic::State);
      return event.invoke<&Playback::echoDelay>(event.u8("encoded_delay"));
    }
    case Kind::VolumePresets: {
      auto event = cursor.command("Volume Presets", SequenceSemantic::State);
      return event.invoke<&Playback::volumePresets>(
          event.s8("left_1", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Level),
          event.s8("right_1", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Level),
          event.s8("left_2", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Level),
          event.s8("right_2", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Level));
    }
    case Kind::BoundedVolumeMotion: {
      auto event = cursor.command("Bounded Stereo Volume Motion", SequenceSemantic::Level);
      const u8 flags = event.u8("direction_flags", SourceValueDisplay::Hex);
      const u8 interval = event.u8("interval", SemanticOperandRole::Duration);
      const u8 delta = event.u8("delta", SemanticOperandRole::Level);
      const u8 delay = event.u8("delay", SemanticOperandRole::Duration);
      const s8 minimum = event.s8("minimum", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Level);
      const s8 maximum = event.s8("maximum", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Level);
      return event.invoke<&Playback::boundedVolumeMotion>(flags, interval, delta, delay, minimum, maximum);
    }
    case Kind::Nop2: {
      auto event = cursor.noOp("NOP", "nop");
      static_cast<void>(event.rawBytes("reserved", 2));
      return event.ignore();
    }
    case Kind::Nop4: {
      auto event = cursor.noOp("NOP", "nop");
      static_cast<void>(event.rawBytes("reserved", 4));
      return event.ignore();
    }
    case Kind::DriverReset:
      return cursor.sourceOnly("Driver Reset", "driver-reset").end();
    case Kind::BtmInstrument: {
      auto event = cursor.command("Instrument Setup", SequenceSemantic::Program);
      if (trackNumber == 5) {
        const u8 slot = event.u8("percussion_slot", SemanticOperandRole::InstrumentProgram);
        const u8 srcn = event.u8("srcn", SemanticOperandRole::Instrument);
        const u8 adsr1 = event.u8("adsr1", SourceValueDisplay::Hex);
        const u8 adsr2 = event.u8("adsr2", SourceValueDisplay::Hex);
        const u8 gain = event.u8("gain", SourceValueDisplay::Hex);
        const s8 left = event.s8("left", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Level);
        const s8 right = event.s8("right", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Level);
        const u8 flags = event.u8("dsp_flags", SourceValueDisplay::Hex);
        const u8 keyoff = event.u8("keyoff", SemanticOperandRole::Duration);
        const s8 base = event.s8("base_note", SourceValueDisplay::MidiNote, SemanticOperandRole::NoteKey);
        return event.invoke<&Playback::btmInstrument>(slot, srcn, adsr1, adsr2, gain, left, right, flags, keyoff, base,
                                                      source);
      }
      const u8 srcn = event.u8("srcn", SemanticOperandRole::Instrument);
      const u8 adsr1 = event.u8("adsr1", SourceValueDisplay::Hex);
      const u8 adsr2 = event.u8("adsr2", SourceValueDisplay::Hex);
      const u8 gain = event.u8("gain", SourceValueDisplay::Hex);
      const s8 left = event.s8("left", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Level);
      const s8 right = event.s8("right", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Level);
      const u8 flags = event.u8("dsp_flags", SourceValueDisplay::Hex);
      const u8 keyoff = event.u8("keyoff", SemanticOperandRole::Duration);
      const s8 tuningValue = event.s8("tuning", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Pitch);
      return event.invoke<&Playback::btmInstrument>(srcn, srcn, adsr1, adsr2, gain, left, right, flags, keyoff,
                                                    tuningValue, source);
    }
    case Kind::BtmPitchEnvelope: {
      auto event = cursor.command("Pitch Envelope", SequenceSemantic::Pitch);
      const u8 delay = event.u8("delay", SemanticOperandRole::Duration);
      const u8 interval = static_cast<u8>(event.u8("interval", SemanticOperandRole::Duration) + 1);
      const u8 encodedSteps = event.u8("steps", SemanticOperandRole::Count);
      const s8 delta = event.s8("delta", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Pitch);
      const bool pingPong = opcode == 0x18 || opcode == 0x19;
      const u8 steps = pingPong ? static_cast<u8>(encodedSteps & 0xfe) : encodedSteps;
      const u8 invertedSteps =
          pingPong ? static_cast<u8>(encodedSteps / 2) : event.u8("inverted_steps", SemanticOperandRole::Count);
      return event.invoke<&Playback::configurePitch>(opcode == 0x0b || opcode == 0x18, delay, interval, steps, delta,
                                                     invertedSteps, u8{8});
    }
    case Kind::BtmAdsrKeyoff: {
      auto event = cursor.command("ADSR / Key-Off", SequenceSemantic::State);
      if (trackNumber == 5) {
        const u8 slot = event.u8("percussion_slot", SemanticOperandRole::InstrumentProgram);
        const u8 adsr1 = event.u8("adsr1", SourceValueDisplay::Hex);
        const u8 adsr2 = event.u8("adsr2", SourceValueDisplay::Hex);
        const u8 keyoff = event.u8("keyoff", SemanticOperandRole::Duration);
        return event.invoke<&Playback::btmPercussionAdsrKeyoff>(slot, adsr1, adsr2, keyoff, source);
      }
      const u8 adsr1 = event.u8("adsr1", SourceValueDisplay::Hex);
      const u8 adsr2 = event.u8("adsr2", SourceValueDisplay::Hex);
      const u8 keyoff = event.u8("keyoff", SemanticOperandRole::Duration);
      return event.invoke<&Playback::btmAdsrKeyoff>(adsr1, adsr2, keyoff, source);
    }
    case Kind::BtmCpuPort: {
      auto event = cursor.sourceOnly("Write SNES Port", "write-port");
      event.u8("value", SourceValueDisplay::Hex);
      return event.ignore();
    }
    case Kind::BtmGlobalTranspose: {
      auto event = cursor.command("Global Transpose", SequenceSemantic::Pitch);
      return event.invoke<&Playback::globalTranspose>(
          event.s8("semitones", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Pitch));
    }
    case Kind::BtmGainKeyoff: {
      auto event = cursor.command("GAIN / Key-Off", SequenceSemantic::State);
      if (trackNumber == 5) {
        const u8 slot = event.u8("percussion_slot", SemanticOperandRole::InstrumentProgram);
        const u8 gain = event.u8("gain", SourceValueDisplay::Hex);
        const u8 keyoff = event.u8("keyoff", SemanticOperandRole::Duration);
        return event.invoke<&Playback::btmPercussionGainKeyoff>(slot, gain, keyoff, source);
      }
      const u8 gain = event.u8("gain", SourceValueDisplay::Hex);
      const u8 keyoff = event.u8("keyoff", SemanticOperandRole::Duration);
      return event.invoke<&Playback::btmGainKeyoff>(gain, keyoff, source);
    }
    case Kind::BtmEchoParameters: {
      auto event = cursor.command("Echo Parameters", SequenceSemantic::State);
      const u8 delay = event.u8("delay");
      const s8 feedback = event.s8("feedback", SourceValueDisplay::SignedDecimal);
      const s8 left = event.s8("left", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Level);
      const s8 right = event.s8("right", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Level);
      return event.invoke([](Playback& playback, s8 fb, s8 l, s8 r, u8 d) { playback.echoParameters(fb, l, r, d); },
                          feedback, left, right, delay);
    }
    case Kind::BtmFixedPan:
      return cursor.command("Fixed Pan", SequenceSemantic::Pan)
          .invoke<&Playback::fixedPan>(static_cast<u8>(opcode - 0x23));
    case Kind::BtmMasterFade: {
      auto event = cursor.command("Master Volume Fade", SequenceSemantic::Level);
      const s8 left = event.s8("left_step", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Level);
      const s8 right = event.s8("right_step", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Level);
      const u8 interval = event.u8("interval", SemanticOperandRole::Duration);
      const u8 steps = event.u8("steps", SemanticOperandRole::Count);
      return event.invoke<&Playback::btmMasterFade>(left, right, interval, steps);
    }
    case Kind::BtmDspFlags: {
      auto event = cursor.command("PMON/NON/EON Flags", SequenceSemantic::State);
      u8 slot = 0;
      if (trackNumber == 5) {
        slot = event.u8("percussion_slot", SemanticOperandRole::InstrumentProgram);
      }
      const u8 flags = event.u8("flags", SourceValueDisplay::Hex);
      return event.invoke<&Playback::btmDspFlags>(slot, flags);
    }
    case Kind::Invalid:
      return cursor.unsupported("Invalid Opcode", "invalid").stop();
  }
  return cursor.unsupported("Invalid Opcode", "invalid").stop();
}

[[nodiscard]] TrackProgram decodeTrack(ByteReader reader, Profile profile, u32 trackNumber, u32 startAddress,
                                       u32 sequenceDataFloor, std::optional<AssetId> sequence,
                                       std::optional<SourceAnnotationId> parent, SourceMapBuilder* sourceMap,
                                       std::vector<Diagnostic>* diagnostics) {
  TrackDecodeScope scope{
      .reader = reader,
      .bytecodeEnd = kAramSize,
      .maxCommands = kMaxTrackCommands,
      .sequenceAsset = sequence,
      .parentAnnotation = parent,
      .sourceMap = sourceMap,
  };
  auto session = scope.begin(trackNumber, startAddress);

  struct DiscoveryPoint {
    u32 offset = 0;
    DecodeState state;
    std::vector<u32> returns;

    bool operator<(const DiscoveryPoint& rhs) const {
      return std::tie(offset, state.longDuration, state.defaultDuration, returns) <
             std::tie(rhs.offset, rhs.state.longDuration, rhs.state.defaultDuration, rhs.returns);
    }
  };

  std::vector<DiscoveryPoint> pending{{.offset = startAddress}};
  std::set<DiscoveryPoint> visited;
  std::map<u32, DecodedBytecodeCommand> commands;
  while (!pending.empty() && visited.size() < kMaxTrackCommands) {
    DiscoveryPoint point = std::move(pending.back());
    pending.pop_back();
    if (!reader.has(point.offset, 1) || point.offset >= kAramSize || !visited.insert(point).second) {
      continue;
    }

    DecodeState nextState = point.state;
    DecodedBytecodeCommand decoded =
        decodeCommand(reader, profile, trackNumber, point.offset, sequenceDataFloor, nextState, nullptr);
    const Kind selected = kind(profile, decoded.opcode);
    const auto [existing, inserted] = commands.try_emplace(point.offset, decoded);
    if (!inserted && existing->second.encodedSize != decoded.encodedSize) {
      if (diagnostics != nullptr) {
        diagnostics->push_back(Diagnostic{
            .severity = Severity::Warning,
            .message =
                fmt::format("RareSnes command ${:04X} is reached with incompatible duration modes", point.offset),
            .range = decoded.range,
        });
      }
      continue;
    }

    const auto queue = [&](Address address, DecodeState state, std::vector<u32> returns) {
      if (address.value < kAramSize && reader.has(address.value, 1)) {
        pending.push_back(DiscoveryPoint{
            .offset = static_cast<u32>(address.value),
            .state = state,
            .returns = std::move(returns),
        });
      }
    };

    if (decoded.opcode >= 0x80) {
      queue(decoded.flow.continuation, nextState, std::move(point.returns));
      continue;
    }

    switch (selected) {
      case Kind::Call:
      case Kind::CallOnce:
        if (const auto destination = decoded.flow.defaultDestination()) {
          point.returns.push_back(static_cast<u32>(decoded.flow.continuation.value));
          queue(*destination, nextState, std::move(point.returns));
        }
        break;
      case Kind::Return:
        if (!point.returns.empty()) {
          const Address continuation{point.returns.back()};
          point.returns.pop_back();
          queue(continuation, nextState, std::move(point.returns));
        }
        break;
      case Kind::Jump:
      case Kind::BtmJump:
        if (const auto destination = decoded.flow.defaultDestination()) {
          queue(*destination, nextState, std::move(point.returns));
        }
        break;
      case Kind::ConditionalJump:
        for (const Address destination : decoded.flow.additionalTargets) {
          queue(destination, nextState, point.returns);
        }
        break;
      case Kind::End:
      case Kind::DriverReset:
      case Kind::Invalid:
        break;
      default:
        queue(decoded.flow.continuation, nextState, std::move(point.returns));
        break;
    }
  }

  for (auto& [offset, command] : commands) {
    if (command.presentation.semantic == SequenceSemantic::Unsupported && diagnostics != nullptr) {
      diagnostics->push_back(Diagnostic{
          .severity = Severity::Warning,
          .message = fmt::format("RareSnes unsupported opcode ${:02X} at ${:04X}", command.opcode, offset),
          .range = command.range,
      });
    }
    session.append(std::move(command), offset);
  }
  return session.finish();
}

[[nodiscard]] SequenceRecipes projectRecipes(const ProgramState& state) {
  return state.recipes;
}

[[nodiscard]] SequenceDialect makeDialect() {
  return makeCompiledDialect<TrackState, Playback, ProgramState>(SequenceDialect{
      .id = DialectId{.value = "rare-snes"},
      .commandDetailKindPrefix = "rare-snes",
      .timebase = Timebase{.ppqn = kPpqn},
      .defaultBehavior =
          SequenceProgramBehavior{
              .defaultLoopPolicy = LoopPolicy::PlayOnce,
              .initialLevel = 127.0 / 128.0,
              .initialReverbSend = 0.0,
              .initialPitchBendRangeSemitones = 12,
              .initialTempoMicrosecondsPerQuarter = tempoMicrosecondsPerQuarter(0x20, 0x64),
          },
      .preferredPitchTransitionRendering = PitchTransitionRenderingHint::PitchBend,
      .prepass = SemanticPrepassMode::ScheduledPlayback,
  });
}

}  // namespace

const SequenceDialect& sequenceDialect() {
  static const SequenceDialect dialect = makeDialect();
  return dialect;
}

TrackProgram decodeSourceTrack(ByteReader reader, Profile profile, u32 trackNumber, u32 startAddress,
                               u32 sequenceDataFloor, std::vector<Diagnostic>* diagnostics) {
  return decodeTrack(reader, profile, trackNumber, startAddress, sequenceDataFloor, std::nullopt, std::nullopt, nullptr,
                     diagnostics);
}

SequenceParse decodeSequence(ByteReader reader, const Layout& layout, AssetId sequenceId, SourceMapBuilder* sourceMap,
                             std::vector<Diagnostic>* diagnostics) {
  const auto& dialect = sequenceDialect();
  const u32 sequenceDataFloor =
      std::max<u32>(layout.sequenceHeaderAddress + static_cast<u32>(layout.trackStarts.size()) * 2 + 2, 1);

  // SequenceDecodeSession owns the standard header/pointer source hierarchy.
  // Stateful Rare durations require the small custom track walker above, so
  // build an equivalent program and project each track through its shared scope.
  SequenceProgram program = dialect.makeProgram(Address{layout.sequenceHeaderAddress});
  program.config = SequenceProgramConfig{
      .profile = static_cast<u32>(layout.profile),
      .driverState = static_cast<u32>(layout.initialTempo) | (static_cast<u32>(layout.initialTimer) << 8) |
                     (layout.monoOutput ? kMonoOutputState : 0),
  };
  program.behavior = dialect.defaultBehavior;
  if (layout.profile == Profile::KillerInstinct) {
    program.behavior.initialLevel = 0.5;
  }
  const double initialChannelGain = layout.profile == Profile::KillerInstinct ? 0.5 : 127.0 / 128.0;
  program.behavior.initialStereoBalance = StereoBalance{initialChannelGain, initialChannelGain};
  program.behavior.initialTempoMicrosecondsPerQuarter =
      tempoMicrosecondsPerQuarter(layout.initialTempo, layout.initialTimer);

  std::optional<SourceAnnotationId> headerParent;
  if (sourceMap != nullptr) {
    auto header = sourceMap->header("RareSnes Sequence Header", layout.sequenceHeaderRange)
                      .kind("rare-snes-sequence-header")
                      .owner(ObjectRefs::asset(sequenceId));
    headerParent = header.id();
    sourceMap->field("Initial Tempo", layout.initialTempoRange, layout.initialTempo)
        .kind("rare-snes-initial-tempo")
        .owner(ObjectRefs::asset(sequenceId))
        .parent(*headerParent);
  }

  for (u32 track = 0; track < layout.trackStarts.size(); ++track) {
    const u16 start = layout.trackStarts[track];
    if (start == 0 || !reader.has(start, 1)) {
      continue;
    }
    if (sourceMap != nullptr && headerParent) {
      const SourceRange pointerRange = reader.range(layout.sequenceHeaderAddress + track * 2, 2);
      sourceMap->pointer(fmt::format("Track {} Pointer", track), pointerRange, SourceTarget{reader.range(start, 1)})
          .kind("rare-snes-track-pointer")
          .field("destination", pointerRange, start, SourceValueDisplay::Address)
          .owner(ObjectRefs::sequenceTrack(sequenceId, track))
          .parent(*headerParent);
    }
    program.tracks.push_back(decodeTrack(reader, layout.profile, track, start, sequenceDataFloor, sequenceId,
                                         headerParent, sourceMap, diagnostics));
  }

  if (layout.instrumentTableAddress) {
    program.sourceProgramMap.reserve(256);
    for (u32 sourceProgram = 0; sourceProgram < 256 && reader.has(*layout.instrumentTableAddress + sourceProgram, 1);
         ++sourceProgram) {
      program.sourceProgramMap.push_back(InstrumentIdentity{
          .domain = "rare-snes.srcn",
          .key = reader.u8At(*layout.instrumentTableAddress + sourceProgram),
      });
    }
  }

  const SequenceRecipes recipes = analyzeCompiledProgram<ProgramState, SequenceRecipes>(
      program, dialect, projectRecipes, SequenceVmOptions{.loopPolicy = LoopPolicy::PlayOnce});
  return SequenceParse{
      .program = std::move(program),
      .recipes = recipes,
  };
}

}  // namespace vgmtrans::formats::rare_snes
