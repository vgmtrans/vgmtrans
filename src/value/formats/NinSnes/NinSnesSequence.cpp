/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/NinSnes/NinSnes.h"

#include "value/sequence/BytecodeDecode.h"
#include "value/sequence/CommandSourceMap.h"
#include "value/sequence/CompilerCursor.h"
#include "value/sequence/SequenceMotion.h"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace vgmtrans::formats::nin_snes {

using namespace core;

namespace {

constexpr u32 kMaxTrackCommands = 32768;
constexpr u8 kMelodicKeyCorrection = 24;
constexpr u8 kIntelliDrumSlots = 16;
constexpr u8 kDefaultTempo = 0x20;

[[nodiscard]] constexpr u32 drumInstrumentKey(u8 program) {
  return (0x7fu << 7) | program;
}

namespace math {

constexpr std::array<u8, 16> kVolumeEarlier{
    0x08, 0x12, 0x1b, 0x24, 0x2c, 0x35, 0x3e, 0x47, 0x51, 0x5a, 0x62, 0x6b, 0x7d, 0x8f, 0xa1, 0xb3,
};
constexpr std::array<u8, 8> kDurationEarlier{0x33, 0x66, 0x80, 0x99, 0xb3, 0xcc, 0xe6, 0xff};
constexpr std::array<u8, 16> kVolumeStandard{
    0x19, 0x33, 0x4c, 0x66, 0x72, 0x7f, 0x8c, 0x99, 0xa5, 0xb2, 0xbf, 0xcc, 0xd8, 0xe5, 0xf2, 0xfc,
};
constexpr std::array<u8, 8> kDurationStandard{0x33, 0x66, 0x7f, 0x99, 0xb2, 0xcc, 0xe5, 0xfc};
constexpr std::array<u8, 16> kVolumeIntelli{
    0x19, 0x32, 0x4c, 0x65, 0x72, 0x7f, 0x8c, 0x98, 0xa5, 0xb2, 0xbf, 0xcb, 0xd8, 0xe5, 0xf2, 0xfc,
};
constexpr std::array<u8, 8> kDurationIntelli{0x32, 0x65, 0x7f, 0x98, 0xb2, 0xcb, 0xe5, 0xfc};
constexpr std::array<u8, 21> kPan{
    0x00, 0x01, 0x03, 0x07, 0x0d, 0x15, 0x1e, 0x29, 0x34, 0x42, 0x51,
    0x5e, 0x67, 0x6e, 0x73, 0x77, 0x7a, 0x7c, 0x7d, 0x7e, 0x7f,
};
constexpr std::array<u8, 64> kIntelliFe3{
    0x00, 0x0c, 0x19, 0x26, 0x33, 0x3f, 0x4c, 0x59, 0x66, 0x72, 0x75, 0x77, 0x70, 0x7c, 0x7f, 0x82,
    0x84, 0x87, 0x89, 0x8c, 0x8e, 0x91, 0x93, 0x96, 0x99, 0x9b, 0x9e, 0xa0, 0xa3, 0xa5, 0xa8, 0xaa,
    0xad, 0xaf, 0xb2, 0xb5, 0xb7, 0xba, 0xbc, 0xbf, 0xc1, 0xc4, 0xc6, 0xc9, 0xcc, 0xce, 0xd1, 0xd3,
    0xd6, 0xd8, 0xdb, 0xdd, 0xe0, 0xe2, 0xe5, 0xe8, 0xea, 0xed, 0xef, 0xf2, 0xf4, 0xf7, 0xf9, 0xfc,
};
constexpr std::array<u8, 64> kIntelliFe4{
    0x19, 0x26, 0x33, 0x3f, 0x4c, 0x59, 0x66, 0x6d, 0x70, 0x72, 0x75, 0x77, 0x70, 0x7c, 0x7f, 0x82,
    0x84, 0x87, 0x89, 0x8c, 0x8e, 0x91, 0x93, 0x96, 0x99, 0x9b, 0x9e, 0xa0, 0xa3, 0xa5, 0xa8, 0xaa,
    0xad, 0xaf, 0xb2, 0xb5, 0xb7, 0xba, 0xbc, 0xbf, 0xc1, 0xc4, 0xc6, 0xc9, 0xcc, 0xce, 0xd1, 0xd3,
    0xd6, 0xd8, 0xdb, 0xdd, 0xe0, 0xe2, 0xe5, 0xe8, 0xea, 0xed, 0xef, 0xf2, 0xf4, 0xf7, 0xf9, 0xfc,
};

[[nodiscard]] constexpr double vibratoDepthCents(u8 depth) {
  if (depth <= 0xf0) {
    return ((0xffu * depth) >> 8) * (100.0 / 256.0);
  }
  return (0xffu * (depth & 0x0fu)) * (100.0 / 256.0);
}

[[nodiscard]] double tremoloDepthDecibels(BaseProfile base, u8 depth) {
  int trough = 255;
  if (base == BaseProfile::Earlier) {
    // The earlier driver applies two rounded 8-bit multiplies before
    // subtracting the tremolo attenuation from the per-note velocity.
    const int attenuation = (255 * depth) >> 8;
    trough -= (255 * attenuation) >> 8;
  } else {
    // Later N-SPC drivers compensate the falling half of the triangle so its
    // deepest point subtracts the raw depth directly.
    trough = std::max(1, 255 - static_cast<int>(depth));
  }

  // N-SPC squares the combined volume after tremolo. A subtractive bipolar
  // output spans 2D dB, so D is 20*log10(peak/trough).
  return 20.0 * std::log10(255.0 / trough);
}

[[nodiscard]] constexpr u32 tempoMicrosecondsPerQuarter(u8 tempo) {
  return tempo == 0 ? 60'000'000 : static_cast<u32>(std::lround(24'576'000.0 / tempo));
}

// Convert the driver's 8-bit level control to linear gain using its square law.
// Renderers handle destination encoding and quantization.
[[nodiscard]] constexpr double levelGain(u8 raw) {
  const double normalized = raw / 255.0;
  return normalized * normalized;
}

struct PanGains {
  double left = 1.0;
  double right = 1.0;
};

[[nodiscard]] u8 panTableValue(std::span<const u8> table, u16 pan) {
  if (table.empty()) {
    return 0;
  }
  u8 index = static_cast<u8>(pan >> 8);
  u8 fraction = static_cast<u8>(pan);
  const u8 maximum = static_cast<u8>(table.size() - 1);
  if (index > maximum) {
    index = maximum;
    fraction = 0;
  }
  const u8 current = table[index];
  const u8 next = index < maximum ? table[index + 1] : current;
  return static_cast<u8>(current + (((next - current) * fraction) >> 8));
}

[[nodiscard]] PanGains panGains(const Profile& selected, std::span<const u8> table, u8 rawPan) {
  if (selected.pan == PanModel::ToseLinear) {
    if (rawPan <= 10) {
      return PanGains{
          .left = (255 - 25 * (10 - rawPan)) / 256.0,
          .right = 1.0,
      };
    }
    return PanGains{
        .left = 1.0,
        .right = (255 - 25 * (rawPan - 10)) / 256.0,
    };
  }

  const u8 index = std::min<u8>(rawPan & 0x1f, static_cast<u8>(table.size() - 1));
  const u16 pan = static_cast<u16>(index) << 8;
  const u16 maximum = static_cast<u16>(table.size() - 1) << 8;
  PanGains gains{
      .left = panTableValue(table, pan) / 128.0,
      .right = panTableValue(table, maximum - pan) / 128.0,
  };
  if (selected.pan == PanModel::HalTable) {
    std::swap(gains.left, gains.right);
  }
  return gains;
}

[[nodiscard]] double stereoPosition(PanGains gains) {
  if (gains.left == 0.0 && gains.right == 0.0) {
    return 0.0;
  }
  constexpr double kPiOverTwo = 1.57079632679489661923;
  return std::clamp((std::atan2(gains.right, gains.left) / kPiOverTwo) * 2.0 - 1.0, -1.0, 1.0);
}

}  // namespace math

enum class EventType : u8 {
  Unknown0,
  Unknown1,
  Unknown2,
  Unknown3,
  Unknown4,
  Nop,
  Nop1,
  End,
  NoteParameter,
  LemmingsNoteParameter,
  IntelliNoteParameter,
  Note,
  Tie,
  Rest,
  Percussion,
  Program,
  Call,
  Pan,
  PanFade,
  VibratoOn,
  VibratoOff,
  MasterVolume,
  MasterVolumeFade,
  Tempo,
  TempoFade,
  GlobalTranspose,
  Transpose,
  TremoloOn,
  TremoloOff,
  Volume,
  VolumeFade,
  VibratoFade,
  PitchEnvelopeTo,
  PitchEnvelopeFrom,
  PitchEnvelopeOff,
  Tuning,
  EchoOn,
  EchoOff,
  EchoParameter,
  EchoVolumeFade,
  PitchSlide,
  PercussionBase,
  Rd2ProgramAndAdsr,
  KonamiLoopStart,
  KonamiLoopEnd,
  KonamiAdsrGain,
  QuintetTuning,
  QuintetAdsr,
  IntelliEchoOn,
  IntelliEchoOff,
  IntelliLegatoOn,
  IntelliLegatoOff,
  IntelliConditionalJump,
  IntelliJump,
  IntelliFe3F5,
  IntelliWritePort,
  IntelliFe3F9,
  IntelliDefineVoice,
  IntelliLoadVoice,
  IntelliAdsr,
  IntelliGainDurationRate,
  IntelliGainDuration,
  IntelliGain,
  IntelliCustomPercussion,
  IntelliTaSubevent,
  IntelliFe4Subevent,
};

struct Status {
  u8 noteMin = 0x80;
  u8 noteMax = 0xc7;
  u8 percussionMin = 0xca;
  u8 percussionMax = 0xdf;
};

struct Definition {
  Status status;
  std::array<EventType, 256> events{};
  std::vector<u8> volume;
  std::vector<u8> duration;
  std::vector<u8> intelliDurationVolume;
};

template <size_t Size>
void useDefault(std::vector<u8>& destination, const std::array<u8, Size>& source) {
  if (destination.empty()) {
    destination.assign(source.begin(), source.end());
  }
}

void loadStandardCommands(std::array<EventType, 256>& events, u8 first) {
  constexpr std::array<EventType, 27> commands{
      EventType::Program,
      EventType::Pan,
      EventType::PanFade,
      EventType::VibratoOn,
      EventType::VibratoOff,
      EventType::MasterVolume,
      EventType::MasterVolumeFade,
      EventType::Tempo,
      EventType::TempoFade,
      EventType::GlobalTranspose,
      EventType::Transpose,
      EventType::TremoloOn,
      EventType::TremoloOff,
      EventType::Volume,
      EventType::VolumeFade,
      EventType::Call,
      EventType::VibratoFade,
      EventType::PitchEnvelopeTo,
      EventType::PitchEnvelopeFrom,
      EventType::PitchEnvelopeOff,
      EventType::Tuning,
      EventType::EchoOn,
      EventType::EchoOff,
      EventType::EchoParameter,
      EventType::EchoVolumeFade,
      EventType::PitchSlide,
      EventType::PercussionBase,
  };
  for (u8 index = 0; index < commands.size(); ++index) {
    events[static_cast<u8>(first + index)] = commands[index];
  }
}

[[nodiscard]] Definition makeDefinition(const Layout& layout) {
  const Profile& selected = profile(layout.profile);
  Definition definition;
  definition.events.fill(EventType::Unknown0);
  definition.volume = layout.volumeTable;
  definition.duration = layout.durationRateTable;

  if (selected.base == BaseProfile::Earlier) {
    definition.status = Status{.noteMin = 0x80, .noteMax = 0xc5, .percussionMin = 0xd0, .percussionMax = 0xd9};
  }
  definition.events[0] = EventType::End;
  for (u16 opcode = 1; opcode < definition.status.noteMin; ++opcode) {
    definition.events[opcode] = EventType::NoteParameter;
  }
  for (u16 opcode = definition.status.noteMin; opcode <= definition.status.noteMax; ++opcode) {
    definition.events[opcode] = EventType::Note;
  }
  definition.events[definition.status.noteMax + 1] = EventType::Tie;
  definition.events[definition.status.noteMax + 2] = EventType::Rest;
  for (u16 opcode = definition.status.percussionMin; opcode <= definition.status.percussionMax; ++opcode) {
    definition.events[opcode] = EventType::Percussion;
  }

  if (selected.base == BaseProfile::Earlier) {
    constexpr std::array<EventType, 25> earlier{
        EventType::Program,
        EventType::Pan,
        EventType::PanFade,
        EventType::PitchSlide,
        EventType::VibratoOn,
        EventType::VibratoOff,
        EventType::MasterVolume,
        EventType::MasterVolumeFade,
        EventType::Tempo,
        EventType::TempoFade,
        EventType::GlobalTranspose,
        EventType::TremoloOn,
        EventType::TremoloOff,
        EventType::Volume,
        EventType::VolumeFade,
        EventType::Call,
        EventType::VibratoFade,
        EventType::PitchEnvelopeTo,
        EventType::PitchEnvelopeFrom,
        EventType::Unknown0,
        EventType::Tuning,
        EventType::EchoOn,
        EventType::EchoOff,
        EventType::EchoParameter,
        EventType::EchoVolumeFade,
    };
    for (u8 index = 0; index < earlier.size(); ++index) {
      definition.events[0xda + index] = earlier[index];
    }
    useDefault(definition.volume, math::kVolumeEarlier);
    useDefault(definition.duration, math::kDurationEarlier);
  } else if (selected.intelli == IntelliMode::Fe3) {
    for (u16 opcode = 1; opcode < definition.status.noteMin; ++opcode) {
      definition.events[opcode] = EventType::IntelliNoteParameter;
    }
    loadStandardCommands(definition.events, 0xd6);
    definition.events[0xf1] = EventType::IntelliEchoOn;
    definition.events[0xf2] = EventType::IntelliEchoOff;
    definition.events[0xf3] = EventType::IntelliLegatoOn;
    definition.events[0xf4] = EventType::IntelliLegatoOff;
    definition.events[0xf5] = EventType::IntelliFe3F5;
    definition.events[0xf6] = EventType::IntelliWritePort;
    definition.events[0xf7] = EventType::IntelliConditionalJump;
    definition.events[0xf8] = EventType::IntelliJump;
    definition.events[0xf9] = EventType::IntelliFe3F9;
    definition.events[0xfa] = EventType::IntelliDefineVoice;
    definition.events[0xfb] = EventType::IntelliLoadVoice;
    definition.events[0xfc] = EventType::IntelliAdsr;
    definition.events[0xfd] = EventType::IntelliGainDurationRate;
    useDefault(definition.volume, math::kVolumeIntelli);
    useDefault(definition.duration, math::kDurationIntelli);
    useDefault(definition.intelliDurationVolume, math::kIntelliFe3);
  } else if (selected.intelli == IntelliMode::Ta || selected.intelli == IntelliMode::Fe4) {
    if (selected.intelli == IntelliMode::Fe4) {
      for (u16 opcode = 1; opcode < definition.status.noteMin; ++opcode) {
        definition.events[opcode] = EventType::IntelliNoteParameter;
      }
    }
    loadStandardCommands(definition.events, 0xda);
    definition.events[0xf5] = EventType::IntelliEchoOn;
    definition.events[0xf6] = EventType::IntelliEchoOff;
    definition.events[0xf7] = selected.intelli == IntelliMode::Ta ? EventType::IntelliAdsr : EventType::IntelliGain;
    definition.events[0xf8] =
        selected.intelli == IntelliMode::Ta ? EventType::IntelliGainDurationRate : EventType::IntelliGain;
    definition.events[0xf9] =
        selected.intelli == IntelliMode::Ta ? EventType::IntelliGainDuration : EventType::Unknown0;
    definition.events[0xfa] = EventType::IntelliDefineVoice;
    definition.events[0xfb] = EventType::IntelliLoadVoice;
    definition.events[0xfc] = EventType::IntelliCustomPercussion;
    definition.events[0xfd] =
        selected.intelli == IntelliMode::Ta ? EventType::IntelliTaSubevent : EventType::IntelliFe4Subevent;
    if (selected.intelli == IntelliMode::Ta) {
      useDefault(definition.volume, math::kVolumeIntelli);
      useDefault(definition.duration, math::kDurationIntelli);
    } else {
      useDefault(definition.intelliDurationVolume, math::kIntelliFe4);
    }
  } else {
    loadStandardCommands(definition.events, 0xe0);
    useDefault(definition.volume, math::kVolumeStandard);
    useDefault(definition.duration, math::kDurationStandard);
  }

  switch (selected.id) {
    case ProfileId::Rd1:
      definition.events[0xfb] = EventType::Unknown2;
      definition.events[0xfc] = EventType::Unknown0;
      definition.events[0xfd] = EventType::Unknown0;
      definition.events[0xfe] = EventType::Unknown0;
      break;
    case ProfileId::Rd2:
      definition.events[0xfb] = EventType::Rd2ProgramAndAdsr;
      definition.events[0xfd] = EventType::Program;
      break;
    case ProfileId::Konami:
      definition.events[0xe4] = EventType::Unknown2;
      definition.events[0xe5] = EventType::KonamiLoopStart;
      definition.events[0xe6] = EventType::KonamiLoopEnd;
      definition.events[0xe8] = EventType::Nop;
      definition.events[0xe9] = EventType::Nop;
      definition.events[0xf5] = EventType::Unknown0;
      definition.events[0xf6] = EventType::Unknown0;
      definition.events[0xf7] = EventType::Unknown0;
      definition.events[0xf8] = EventType::Unknown0;
      definition.events[0xfb] = EventType::KonamiAdsrGain;
      definition.events[0xfc] = EventType::Nop;
      definition.events[0xfd] = EventType::Nop;
      definition.events[0xfe] = EventType::Nop;
      break;
    case ProfileId::Lemmings:
      for (u16 opcode = 1; opcode < definition.status.noteMin; ++opcode) {
        definition.events[opcode] = EventType::LemmingsNoteParameter;
      }
      definition.events[0xe5] = EventType::Unknown1;
      definition.events[0xe6] = EventType::Unknown2;
      definition.events[0xfb] = EventType::Nop1;
      definition.events[0xfc] = EventType::Unknown0;
      definition.events[0xfd] = EventType::Unknown0;
      definition.events[0xfe] = EventType::Unknown0;
      break;
    case ProfileId::QuintetIog:
    case ProfileId::QuintetTs:
      definition.events[0xf4] = EventType::QuintetTuning;
      definition.events[0xff] = EventType::QuintetAdsr;
      break;
    default:
      break;
  }
  return definition;
}

struct VoiceRecord {
  u8 instrument = 0;
  u8 volume = 0;
  u8 pan = 0;
  u8 tuningTranspose = 0;
};

struct PercussionEntry {
  u8 patch = 0;
  u8 note = 0;
  u8 pan = 0;
};

struct VibratoConfig {
  u8 delay = 0;
  u8 rate = 0;
  u8 depth = 0;
  u8 fade = 0;

  [[nodiscard]] bool active() const { return rate != 0 && depth != 0; }
};

[[nodiscard]] LfoPerformanceContext tremoloLfoContext() {
  return LfoPerformanceContext{
      .waveform = LfoWaveform::Triangle,
      // N-SPC starts at nominal gain and initially moves toward attenuation.
      .initialPhaseCycles = 0.25,
      .tremoloGainMode = TremoloGainMode::NoBoost,
  };
}

[[nodiscard]] double echoSend(u8 volumeLeft, u8 volumeRight) {
  // EVOL is signed because negative values invert the echo signal. MIDI has no
  // polarity control for its reverb send, so preserve the larger wet magnitude.
  const int left = std::abs(static_cast<int>(static_cast<s8>(volumeLeft)));
  const int right = std::abs(static_cast<int>(static_cast<s8>(volumeRight)));
  return std::min(std::max(left, right) / 127.0, 1.0);
}

struct ProgramState {
  struct EchoChange {
    u64 tick = 0;
    u8 mask = 0;
    double send = 0.0;
  };

  explicit ProgramState(const SequenceProgram& program)
      : selected(profile(static_cast<ProfileId>(program.config.profile))) {
    for (u32 encoded = 0; encoded < basePrograms.size(); ++encoded) {
      basePrograms[encoded] =
          encoded < program.sourceProgramMap.size() ? program.sourceProgramMap[encoded].key : encoded;
    }
    for (const auto& track : program.tracks) {
      for (const auto& command : track.commands) {
        sourceRanges.emplace(command.address.value, command.range);
      }
    }
    resetRuntime();
  }

  void resetRuntime() {
    tempo = kDefaultTempo;
    globalTranspose = 0;
    percussionBase = 0;
    customNoteParameters = false;
    intelliFlags = 0;
    voiceTable.clear();
    percussionTable = {};
    programs = basePrograms;
    nextOverrideProgram = 0x80;
    tempoFade.reset(kDefaultTempo);
    tempoFade.clearAutomation();
    tempoFadeTrack.reset();
    masterVolume = 0xff;
    masterFade.reset(masterVolume);
    masterFadeTrack.reset();
  }

  void setEcho(u64 tick, u8 mask, double send) {
    if (collecting) {
      echoChanges.push_back(EchoChange{.tick = tick, .mask = mask, .send = send});
    }
  }

  void finalizePerformance(PerformanceSequence& performance) const {
    u64 nextSequence = 0;
    for (const PerformanceTrack& track : performance.tracks) {
      for (const PerformanceEvent& event : track.events) {
        nextSequence = std::max(nextSequence, performanceEventHeader(event).sequence + 1);
      }
    }

    // EON is one DSP register write whose mask affects every voice. Materialize
    // its eight channel results after independent track execution, including
    // channels that were inactive when the source command ran.
    for (PerformanceTrack& track : performance.tracks) {
      for (const EchoChange& change : echoChanges) {
        track.events.emplace_back(ReverbPerformanceEvent{
            .header =
                PerformanceEventHeader{
                    .track = track.id,
                    .tick = change.tick,
                    .sequence = nextSequence++,
                },
            .send = (change.mask & (1u << track.sourceTrackNumber)) != 0 ? change.send : 0.0,
        });
      }
    }
  }

  [[nodiscard]] u32 resolveProgram(u8 encoded, u8 percussionMinimum, u8* logical = nullptr) const {
    u8 index = encoded;
    if (selected.programs != ProgramResolver::Direct && encoded >= 0x80) {
      index = static_cast<u8>((encoded - percussionMinimum) + percussionBase);
    }
    if (logical != nullptr) {
      // Quintet applies its base/lookup before exposing the logical instrument
      // number. Keep that distinction from the encoded table index: percussion
      // key assignment depends on the resolved logical number, while the
      // program map still needs the encoded index.
      *logical =
          (selected.programs == ProgramResolver::QuintetActRBase || selected.programs == ProgramResolver::QuintetLookup)
              ? static_cast<u8>(basePrograms[index])
              : index;
    }
    return programs[index];
  }

  [[nodiscard]] u32 registerOverride(u8 logical, u8 srcn, u8 adsr1, u8 adsr2, u8 gain, u8 pitchHigh, u8 pitchLow,
                                     Address sourceAddress) {
    const u32 program = nextOverrideProgram++;
    programs[logical] = program;
    if (collecting) {
      recipes.overrides.push_back(InstrumentOverride{
          .program = program,
          .srcn = srcn,
          .adsr1 = adsr1,
          .adsr2 = adsr2,
          .gain = gain,
          .pitchHigh = pitchHigh,
          .pitchLow = pitchLow,
          .source = sourceRanges.contains(sourceAddress.value) ? sourceRanges.at(sourceAddress.value) : SourceRange{},
      });
    }
    return program;
  }

  void rememberStandardDrum(u8 logicalProgram, u32 sourceProgram, u8 key, s8 transpose) {
    if (!collecting) {
      return;
    }
    standardDrums[logicalProgram] = DrumSlot{
        .key = key,
        .sourceProgram = sourceProgram,
        .sourceNote = 0x3c,
        .globalTranspose = transpose,
    };
  }

  [[nodiscard]] DrumKit currentIntelliDrumKit(u8 percussionMinimum) const {
    DrumKit kit{
        .pitchModel = DrumPitchModel::IntelliPlayedNote,
    };
    kit.slots.reserve(kIntelliDrumSlots);
    const bool useCustom = (intelliFlags & 0x40) != 0;
    for (u8 slot = 0; slot < kIntelliDrumSlots; ++slot) {
      u8 patch = static_cast<u8>(percussionMinimum + slot);
      u8 note = 0xa4;
      if (useCustom) {
        patch = percussionTable[slot].patch & 0xbf;
        note = percussionTable[slot].note;
      }
      kit.slots.push_back(DrumSlot{
          .key = static_cast<u8>(0x24 + slot),
          .sourceProgram = resolveProgram(patch, percussionMinimum),
          .sourceNote = note,
      });
    }
    return kit;
  }

  [[nodiscard]] u8 ensureIntelliDrumKit(u8 percussionMinimum) {
    DrumKit candidate = currentIntelliDrumKit(percussionMinimum);
    const auto found = std::ranges::find_if(recipes.drumKits, [&](const DrumKit& kit) {
      return kit.pitchModel == candidate.pitchModel && kit.slots == candidate.slots;
    });
    if (found != recipes.drumKits.end()) {
      return found->program;
    }
    if (!collecting || recipes.drumKits.size() >= 0x80) {
      return recipes.drumKits.empty() ? 0 : recipes.drumKits.back().program;
    }
    candidate.program = static_cast<u8>(recipes.drumKits.size());
    recipes.drumKits.push_back(std::move(candidate));
    return recipes.drumKits.back().program;
  }

  void finishPrepass() {
    if (!standardDrums.empty()) {
      DrumKit kit{
          .program = 0,
          .pitchModel = DrumPitchModel::StandardMapping,
      };
      for (const auto& [_, slot] : standardDrums) {
        kit.slots.push_back(slot);
      }
      recipes.drumKits.push_back(std::move(kit));
    }
    collecting = false;
    resetRuntime();
  }

  const Profile& selected;
  std::array<u32, 256> basePrograms{};
  std::array<u32, 256> programs{};
  std::map<u64, SourceRange> sourceRanges;
  u8 tempo = kDefaultTempo;
  s8 globalTranspose = 0;
  u8 percussionBase = 0;
  bool customNoteParameters = false;
  u8 intelliFlags = 0;
  std::vector<VoiceRecord> voiceTable;
  std::array<PercussionEntry, kIntelliDrumSlots> percussionTable{};
  u32 nextOverrideProgram = 0x80;
  std::vector<EchoChange> echoChanges;
  std::map<u8, DrumSlot> standardDrums;
  PerformanceBoundMotion<SequenceFixedPointAutomation<s32>> tempoFade;
  std::optional<u32> tempoFadeTrack;
  u8 masterVolume = 0xff;
  PerformanceBoundMotion<SequenceFixedPointAutomation<s32>> masterFade;
  std::optional<u32> masterFadeTrack;
  SequenceRecipes recipes;
  bool collecting = true;
};

struct PitchEnvelope {
  enum class Mode : u8 { None, To, From };
  Mode mode = Mode::None;
  u8 delay = 0;
  u8 length = 0;
  s8 semitones = 0;
};

struct PitchState {
  static constexpr u16 kDefaultRangeCents = 200;

  void reset() {
    baseValid = false;
    base = 0;
    motion.reset();
    transition.clear();
    transitionNoteKey = 0.0;
    rangeCents = kDefaultRangeCents;
    bend = 0;
  }

  bool baseValid = false;
  s32 base = 0;
  SequenceLinearMotion<s32> motion;
  PitchSlideBinding transition;
  double transitionNoteKey = 0.0;
  u16 rangeCents = kDefaultRangeCents;
  std::optional<s16> bend = 0;
};

struct TrackState {
  TrackState(const SequenceProgram&, const TrackProgram& track) : trackNumber(track.sourceTrackNumber) {
    volumeFade.reset(0xff);
    panFade.reset(10);
    vibratoDepth.reset(0);
    pitch.reset();
  }

  void beginSection() {
    inPattern = false;
    patternRemaining = 0;
    lastWasPercussion = false;
    melodicProgram = 0;
    currentLogicalProgram.reset();
    legato = false;
    // Volume, pan, pitch, and modulation state carry across section boundaries.
  }

  u32 trackNumber = 0;
  u8 noteLength = 1;
  u8 durationRate = 0xfc;
  u8 velocity = 0xfc;
  s8 transpose = 0;
  bool legato = false;
  bool inPattern = false;
  u8 patternRemaining = 0;
  Address patternStart;
  Address konamiLoopStart;
  u8 konamiLoopVolumeDelta = 0;
  s16 konamiLoopPitchDelta = 0;
  VibratoConfig vibrato;
  PitchEnvelope pitchEnvelope;
  PitchState pitch;
  u32 melodicProgram = 0;
  std::optional<u8> currentLogicalProgram;
  bool lastWasPercussion = false;
  u8 percussionProgram = 0;
  PerformanceNoteId lastNote;
  std::optional<double> lastKey;
  PerformanceBoundMotion<SequenceFixedPointAutomation<s32>> volumeFade;
  PerformanceBoundMotion<SequenceFixedPointAutomation<s32>> panFade;
  PerformanceBoundMotion<SequenceAutomatedValue<s32>> vibratoDepth;
  double lastVibratoDepthSemitones = 0.0;
};

struct Playback {
  TrackState& track;
  PerformanceEmitter& out;
  VmApi& vm;
  ProgramState& program;

  [[nodiscard]] u8 soundingDuration() const {
    if (track.legato) {
      return track.noteLength;
    }
    const u8 scaled = static_cast<u8>((track.noteLength * track.durationRate) >> 8);
    const u8 maximum = static_cast<u8>(track.noteLength - 2);
    return std::min(std::max<u8>(scaled, 1), maximum);
  }

  void standardParameters(u8 duration, bool hasPacked, u8 durationValue, u8 velocityValue) {
    track.noteLength = duration;
    if (hasPacked) {
      track.durationRate = durationValue;
      track.velocity = program.selected.id == ProfileId::Konami
                           ? static_cast<u8>(velocityValue + track.konamiLoopVolumeDelta)
                           : velocityValue;
    }
  }

  void lemmingsParameters(u8 duration, bool hasDuration, u8 durationValue, bool hasVelocity, u8 velocityValue) {
    track.noteLength = duration;
    if (hasDuration) {
      track.durationRate = durationValue;
    }
    if (hasVelocity) {
      track.velocity = velocityValue;
    }
  }

  void intelliParameter(u8 raw, u8 resolved) {
    if (raw < 0x40) {
      track.durationRate = resolved;
    } else {
      track.velocity = resolved;
    }
  }

  void fe3CustomParameter(u8 raw, u8 resolved) {
    if (program.customNoteParameters) {
      intelliParameter(raw, resolved);
    }
  }

  void fe3StandardParameter(bool present, u8 durationRate, u8 velocity) {
    if (present && !program.customNoteParameters) {
      track.durationRate = durationRate;
      track.velocity = velocity;
    }
  }

  [[nodiscard]] Effects fe3ParameterFlow(Address standardDestination, Address customDestination) {
    return Effects{.step = vm.jump(program.customNoteParameters ? customDestination : standardDestination)};
  }

  void switchToMelodicProgram() {
    if (track.lastWasPercussion) {
      out.instrument(InstrumentIdentity{.domain = std::string(kInstrumentDomain), .key = track.melodicProgram});
      track.lastWasPercussion = false;
    }
  }

  void melodicProgram(u8 encoded, u8 percussionMinimum) {
    u8 logical = 0;
    track.melodicProgram = program.resolveProgram(encoded, percussionMinimum, &logical);
    track.currentLogicalProgram = logical;
    if (!track.lastWasPercussion) {
      out.instrument(InstrumentIdentity{.domain = std::string(kInstrumentDomain), .key = track.melodicProgram});
    }
  }

  void switchToDrumProgram(u8 drumProgram) {
    if (!track.lastWasPercussion || track.percussionProgram != drumProgram) {
      out.instrument(InstrumentIdentity{
          .domain = std::string(kInstrumentDomain),
          .key = drumInstrumentKey(drumProgram),
      });
      track.percussionProgram = drumProgram;
    }
    track.lastWasPercussion = true;
  }

  void emitPitchBend(s16 bend) {
    if (track.pitch.bend && *track.pitch.bend == bend) {
      return;
    }
    track.pitch.bend = bend;
    out.pitchBend((static_cast<double>(bend) / 8192.0) * (track.pitch.rangeCents / 100.0));
  }

  [[nodiscard]] s16 currentPitchBend() const {
    if (!track.pitch.baseValid) {
      return 0;
    }
    const double cents = (track.pitch.motion.current() - track.pitch.base) * (100.0 / 256.0);
    return static_cast<s16>(
        std::clamp<s32>(static_cast<s32>(std::lround((cents / track.pitch.rangeCents) * 8192.0)), -8192, 8191));
  }

  void applyCurrentPitchBend() { emitPitchBend(currentPitchBend()); }

  // Pitch values use 256 units per semitone. Apply their offset from the
  // original note to the emitted key, which already includes mapping and transpose.
  [[nodiscard]] double pitchKey(s32 pitch, double noteKey) const {
    return noteKey + static_cast<double>(pitch - track.pitch.base) / 256.0;
  }

  void setPitchBendRange(u16 cents) {
    if (cents == 0 || cents == track.pitch.rangeCents) {
      return;
    }
    track.pitch.rangeCents = cents;
    out.pitchBendRange(PitchBendRangePerformanceEvent{.cents = cents});
    if (track.pitch.baseValid) {
      applyCurrentPitchBend();
    }
  }

  void resetPitchForNote() {
    track.pitch.transition.interrupt(out);
    track.pitch.motion.clear();
    track.pitch.baseValid = false;
    if (track.pitch.rangeCents == PitchState::kDefaultRangeCents && track.pitch.bend == 0) {
      return;
    }
    if (track.pitchEnvelope.mode != PitchEnvelope::Mode::None && track.pitchEnvelope.length != 0) {
      emitPitchBend(0);
      return;
    }
    setPitchBendRange(PitchState::kDefaultRangeCents);
    emitPitchBend(0);
  }

  // Start a delayed pitch change for direct channel-pitch-bend output. It moves
  // from the current pitch to the target over the requested length, expanding
  // the bend range when necessary to avoid clipping.
  void beginPitchBendMotion(u8 delay, u8 length, s32 target) {
    track.pitch.motion.clear();
    if (!track.pitch.baseValid || length == 0) {
      setPitchBendRange(PitchState::kDefaultRangeCents);
      return;
    }

    const s32 current = track.pitch.motion.current();
    const double largestDeviation = std::max(std::abs(static_cast<double>(current - track.pitch.base)),
                                             std::abs(static_cast<double>(target - track.pitch.base)));
    const u16 range =
        std::max<u16>(PitchState::kDefaultRangeCents, static_cast<u16>(std::ceil(largestDeviation * (100.0 / 256.0))));
    setPitchBendRange(range);
    static_cast<void>(track.pitch.motion.begin(SequenceMotionPlan<s32>::targetOverTicks(target, length, delay)));
    applyCurrentPitchBend();
  }

  void pitchSlide(u8 delay, u8 length, u8 targetNote) {
    const s32 target = static_cast<s32>(targetNote & 0x7f) * 256;

    // A new F9 stops any F9 slide still in progress.
    track.pitch.transition.interrupt(out);

    // Portamento needs a nonzero length and a note to slide. Use pitch bends otherwise.
    if (!track.pitch.baseValid || length == 0 || !track.lastNote.valid() || !track.lastKey) {
      beginPitchBendMotion(delay, length, target);
      return;
    }

    // Stop the previous calculation without resetting its current pitch, so
    // the replacement starts from the value already reached.
    track.pitch.motion.clear();
    const s32 current = track.pitch.motion.current();
    static_cast<void>(track.pitch.motion.begin(SequenceMotionPlan<s32>::targetOverTicks(target, length, delay)));

    // Calculate intermediate pitches with N-SPC integer math.
    // advancePitchMotion() records each value on the slide created below.
    track.pitch.transitionNoteKey = *track.lastKey;
    track.pitch.transition =
        out.at(vm.tick() + delay)
            .pitchSlide(track.lastNote, pitchKey(current, *track.lastKey), pitchKey(target, *track.lastKey), length);
  }

  void beginNotePitch(u8 rawNote) {
    resetPitchForNote();
    track.pitch.baseValid = true;
    track.pitch.base = static_cast<s32>(rawNote & 0x7f) * 256;
    track.pitch.motion.setCurrent(track.pitch.base);

    if (track.pitchEnvelope.mode != PitchEnvelope::Mode::None && track.pitchEnvelope.length != 0) {
      const s32 offset = static_cast<s32>(track.pitchEnvelope.semitones) * 256;
      s32 target = track.pitch.base;
      if (track.pitchEnvelope.mode == PitchEnvelope::Mode::To) {
        target += offset;
      } else {
        track.pitch.motion.setCurrent(track.pitch.base - offset);
      }
      beginPitchBendMotion(track.pitchEnvelope.delay, track.pitchEnvelope.length, target);
    }
    beginNoteVibrato();
  }

  [[nodiscard]] bool pitchMotionIdle() const { return !track.pitch.motion.active(); }

  void advancePitchMotion() {
    const auto tick = track.pitch.motion.tick();
    if (tick.status == SequenceMotionStatus::Inactive || tick.status == SequenceMotionStatus::Delayed) {
      return;
    }
    if (track.pitch.transition.valid()) {
      track.pitch.transition.sample(out, pitchKey(tick.current, track.pitch.transitionNoteKey));
      track.pitch.bend.reset();
      if (tick.status == SequenceMotionStatus::Finished) {
        track.pitch.transition.clear();
      }
    } else {
      applyCurrentPitchBend();
    }
  }

  [[nodiscard]] Effects note(u8 noteIndex) {
    switchToMelodicProgram();
    const double key =
        kMelodicKeyCorrection + noteIndex + track.transpose + static_cast<double>(track.konamiLoopPitchDelta) / 256.0;
    beginNotePitch(noteIndex);
    track.lastNote = out.note(key, math::levelGain(track.velocity), soundingDuration() + (track.legato ? 1u : 0u));
    track.lastKey = key;
    return Effects::wait(track.noteLength);
  }

  [[nodiscard]] Effects percussion(u8 slot, u8 percussionMinimum, bool intelli) {
    const u8 duration = soundingDuration();
    if (intelli) {
      const bool custom = (program.intelliFlags & 0x40) != 0;
      const PercussionEntry entry = program.percussionTable[slot];
      const u8 patch = custom ? static_cast<u8>(entry.patch & 0xbf) : static_cast<u8>(percussionMinimum + slot);
      if (custom && entry.pan < 0x80) {
        const auto gains = math::panGains(program.selected, panTable, entry.pan);
        out.stereoBalance(gains.left, gains.right);
      }
      if (custom) {
        out.reverb((entry.patch & 0x40) != 0 ? 40.0 / 127.0 : 0.0);
      }
      static_cast<void>(program.resolveProgram(patch, percussionMinimum));
      const u8 kit = program.ensureIntelliDrumKit(percussionMinimum);
      switchToDrumProgram(kit);
      const double key = 0x24 + slot - program.globalTranspose;
      beginNotePitch(static_cast<u8>(0x24 + slot - program.globalTranspose));
      track.lastNote = out.note(key, math::levelGain(track.velocity), duration);
      track.lastKey = key;
    } else {
      u8 logical = 0;
      const u32 sourceProgram =
          program.resolveProgram(static_cast<u8>(slot + program.percussionBase), percussionMinimum, &logical);
      const u8 key = static_cast<u8>(0x24 + logical - program.percussionBase);
      program.rememberStandardDrum(logical, sourceProgram, key, program.globalTranspose);
      switchToDrumProgram(0);
      const double outputKey = key - program.globalTranspose + static_cast<double>(track.konamiLoopPitchDelta) / 256.0;
      beginNotePitch(static_cast<u8>(key - program.globalTranspose));
      track.lastNote = out.note(outputKey, math::levelGain(track.velocity), duration);
      track.lastKey = outputKey;
    }
    return Effects::wait(track.noteLength);
  }

  [[nodiscard]] Effects tie() {
    if (track.lastKey) {
      track.lastNote = out.note(*track.lastKey, math::levelGain(track.velocity), soundingDuration(), true);
    }
    return Effects::wait(track.noteLength);
  }

  [[nodiscard]] Effects rest() {
    // A rest ends the preceding note chain; a later tie cannot reach back
    // across that silence.
    track.lastNote = {};
    track.lastKey.reset();
    return Effects::wait(track.noteLength);
  }

  void beginPattern(u8 times, Address destination) {
    track.inPattern = true;
    track.patternRemaining = times;
    track.patternStart = destination;
  }

  [[nodiscard]] Effects endOrReturn() {
    if (!track.inPattern) {
      return Effects{.step = vm.endSection()};
    }
    if (track.patternRemaining > 1) {
      --track.patternRemaining;
      return Effects{.step = vm.jump(track.patternStart)};
    }
    track.inPattern = false;
    track.patternRemaining = 0;
    return Effects{.step = vm.return_()};
  }

  void emitPan(PerformanceEmitter output, u8 value) const {
    const auto gains = math::panGains(program.selected, panTable, value);
    output.stereoBalance(gains.left, gains.right);
  }

  void pan(u8 value) {
    track.panFade.setCurrentRaw(value);
    emitPan(out, value);
  }

  void panFade(u8 length, u8 value) {
    if (length == 0) {
      pan(value);
      return;
    }
    // Interpolate the source pan index before applying its non-linear table.
    const auto gains = math::panGains(program.selected, panTable, value);
    static_cast<void>(
        track.panFade.begin(out.fade(PerformanceAutomationTarget::Pan, math::stereoPosition(gains), length),
                            SequenceFixedPointMotion<s32>::toRawTarget(value, length)));
  }

  void vibratoOn(u8 delay, u8 rate, u8 depth) {
    track.vibrato = VibratoConfig{.delay = delay, .rate = rate, .depth = depth};
    track.vibratoDepth.setCurrent(depth);
    track.vibratoDepth.clearAutomation();
    emitConfiguredVibrato();
  }

  void vibratoOff() {
    track.vibrato = {};
    track.vibratoDepth.setCurrent(0);
    track.vibratoDepth.clearAutomation();
    emitConfiguredVibrato();
  }

  void vibratoFade(u8 length) { track.vibrato.fade = length; }

  void emitVibratoDepth(u8 rawDepth, PerformanceEmitter output) {
    const double depthSemitones = math::vibratoDepthCents(rawDepth) / 100.0;
    if (std::abs(depthSemitones - track.lastVibratoDepthSemitones) < 0.000001) {
      return;
    }
    track.lastVibratoDepthSemitones = depthSemitones;
    output.vibratoDepth(depthSemitones);
  }

  void emitVibratoRateAndDelay() {
    const bool active = track.vibrato.active();
    if (active) {
      out.vibratoRateCyclesPerTick(static_cast<double>(track.vibrato.rate) / 256.0);
      out.vibratoDelayTicks(track.vibrato.delay);
    } else {
      out.vibratoRate(0.0);
      out.vibratoDelay(0, 0);
    }
  }

  void emitConfiguredVibrato() {
    emitVibratoDepth(track.vibrato.active() ? track.vibrato.depth : 0, out);
    emitVibratoRateAndDelay();
  }

  void beginNoteVibrato() {
    if (!track.vibrato.active() || track.vibrato.fade == 0) {
      return;
    }
    track.vibratoDepth.setCurrent(0);
    const s32 target = track.vibrato.depth;
    track.vibratoDepth.begin(
        out.noteEnvelope(PerformanceAutomationTarget::VibratoDepth,
                         math::vibratoDepthCents(track.vibrato.depth) / 100.0, track.vibrato.fade, track.vibrato.delay),
        SequenceMotionPlan<s32>::targetOverTicksWithStep(target, target / static_cast<s32>(track.vibrato.fade),
                                                         track.vibrato.fade, track.vibrato.delay));
    emitVibratoDepth(0, track.vibratoDepth.output(out));
  }

  void tremoloOn(u8 delay, u8 rate, u8 depth) {
    const LfoPerformanceContext context = tremoloLfoContext();
    out.tremoloDepth(math::tremoloDepthDecibels(program.selected.base, depth), context);
    out.tremoloRateCyclesPerTick(static_cast<double>(rate) / 256.0, context);
    out.tremoloDelayTicks(delay);
  }

  void tremoloOff() { out.tremoloDepth(0.0, tremoloLfoContext()); }

  void tempo(u8 value) {
    program.tempoFade.setCurrentRaw(value);
    program.tempoFade.clearAutomation();
    program.tempoFadeTrack.reset();
    program.tempo = value;
    out.tempo(math::tempoMicrosecondsPerQuarter(value));
  }

  void tempoFade(u8 length, u8 value) {
    if (length == 0) {
      tempo(value);
      return;
    }
    program.tempoFade.setCurrentRaw(program.tempo);
    program.tempoFade.begin(out.fade(PerformanceAutomationTarget::Tempo,
                                     static_cast<double>(math::tempoMicrosecondsPerQuarter(value)), length),
                            SequenceFixedPointMotion<s32>::toRawTarget(value, length));
    program.tempoFadeTrack = track.trackNumber;
    advanceTempoFade();
  }

  void volume(u8 value) {
    track.volumeFade.setCurrentRaw(value);
    out.level(math::levelGain(value), ValueQuantization{.levels = 256});
  }

  void volumeFade(u8 length, u8 value) {
    if (length == 0) {
      volume(value);
      return;
    }
    static_cast<void>(
        track.volumeFade.begin(out.fade(PerformanceAutomationTarget::Level, math::levelGain(value), length),
                               SequenceFixedPointMotion<s32>::toRawTarget(value, length)));
  }

  void masterVolume(u8 value) {
    program.masterVolume = value;
    program.masterFade.setCurrentRaw(value);
    program.masterFadeTrack.reset();
    out.masterLevel(math::levelGain(value));
  }

  void masterVolumeFade(u8 length, u8 value) {
    if (length == 0) {
      masterVolume(value);
      return;
    }
    program.masterFade.setCurrentRaw(program.masterVolume);
    static_cast<void>(
        program.masterFade.begin(out.fade(PerformanceAutomationTarget::MasterLevel, math::levelGain(value), length),
                                 SequenceFixedPointMotion<s32>::toRawTarget(value, length)));
    program.masterFadeTrack = track.trackNumber;
  }

  void advanceTempoFade() {
    static_cast<void>(program.tempoFade.tickRaw([&](s32 raw) {
      const u8 value = static_cast<u8>(std::clamp<s32>(raw, 0, 0xff));
      program.tempo = value;
      program.tempoFade.output(out).tempo(math::tempoMicrosecondsPerQuarter(value));
    }));
    if (!program.tempoFade.active()) {
      program.tempoFadeTrack.reset();
    }
  }

  void advanceVibratoFade() {
    const auto tick = track.vibratoDepth.tick();
    if (!tick.shouldApply()) {
      return;
    }
    const u8 value = static_cast<u8>(std::clamp<s32>(tick.current, 0, track.vibrato.depth));
    track.vibratoDepth.setCurrentPreservingMotion(value);
    emitVibratoDepth(value, track.vibratoDepth.output(out));
  }

  void advancePanFade() {
    static_cast<void>(track.panFade.tickRaw(
        [&](s32 value) { emitPan(track.panFade.output(out), static_cast<u8>(std::clamp<s32>(value, 0, 0xff))); }));
  }

  void advanceVolumeFade() {
    static_cast<void>(track.volumeFade.tickRaw([&](s32 value) {
      track.volumeFade.output(out).level(math::levelGain(static_cast<u8>(std::clamp<s32>(value, 0, 0xff))),
                                         ValueQuantization{.levels = 256});
    }));
  }

  void advanceMasterFade() {
    static_cast<void>(program.masterFade.tickRaw([&](s32 value) {
      program.masterVolume = static_cast<u8>(std::clamp<s32>(value, 0, 0xff));
      program.masterFade.output(out).masterLevel(math::levelGain(program.masterVolume));
    }));
    if (!program.masterFade.active()) {
      program.masterFadeTrack.reset();
    }
  }

  void tick() {
    advanceVolumeFade();
    advancePanFade();
    advanceVibratoFade();
    advancePitchMotion();
    if (program.tempoFadeTrack == track.trackNumber) {
      advanceTempoFade();
    }
    if (program.masterFadeTrack == track.trackNumber) {
      advanceMasterFade();
    }
  }

  void globalTranspose(s8 semitones) {
    program.globalTranspose = semitones;
    out.globalTranspose(semitones);
  }

  void echo(u8 channels, u8 volumeLeft, u8 volumeRight) {
    program.setEcho(vm.tick(), channels, echoSend(volumeLeft, volumeRight));
  }

  void echoOff() { program.setEcho(vm.tick(), 0, 0.0); }

  void percussionBase(u8 base) {
    program.percussionBase = base;
    if (program.selected.intelli == IntelliMode::Ta) {
      program.intelliFlags &= static_cast<u8>(~0x40);
    }
  }

  void pitchEnvelope(PitchEnvelope::Mode mode, u8 delay, u8 length, s8 semitones) {
    track.pitchEnvelope = PitchEnvelope{mode, delay, length, semitones};
  }

  void pitchEnvelopeOff() { track.pitchEnvelope = {}; }

  [[nodiscard]] Effects konamiLoop(u8 times, s8 volumeDelta, s8 pitchDelta, Address destination) {
    RepeatCounter counter = vm.repeatCounter(0);
    if (counter.firstVisit()) {
      counter.start(times == 0 ? 256 : times);
    }

    if (counter.consumeReplay()) {
      // The driver accumulates these operands only for another pass. Volume is
      // an eight-bit add applied when the packed note parameters are read;
      // pitch uses signed 16-bit units with 1/256 semitone resolution.
      track.konamiLoopVolumeDelta = static_cast<u8>(track.konamiLoopVolumeDelta + static_cast<u8>(volumeDelta));
      const u16 pitchBits =
          static_cast<u16>(track.konamiLoopPitchDelta) + static_cast<u16>(static_cast<s16>(pitchDelta) * 16);
      track.konamiLoopPitchDelta =
          pitchBits < 0x8000 ? static_cast<s16>(pitchBits) : static_cast<s16>(static_cast<s32>(pitchBits) - 0x10000);
      return Effects{.step = vm.jump(destination)};
    }

    counter.finish();
    track.konamiLoopVolumeDelta = 0;
    track.konamiLoopPitchDelta = 0;
    return Effects{.step = vm.next()};
  }

  void defineVoiceTable(u8 size) { program.voiceTable.assign(size, VoiceRecord{}); }

  void defineVoice(u8 index, u8 instrument, u8 volume, u8 pan, u8 tuningTranspose) {
    if (index < program.voiceTable.size()) {
      program.voiceTable[index] = VoiceRecord{
          .instrument = instrument,
          .volume = volume,
          .pan = pan,
          .tuningTranspose = tuningTranspose,
      };
    }
  }

  void overwriteInstrument(u8 logical, u8 srcn, u8 adsr1, u8 adsr2, u8 gain, u8 pitchHigh, u8 pitchLow,
                           Address sourceAddress) {
    const u32 newProgram =
        program.registerOverride(logical, srcn, adsr1, adsr2, gain, pitchHigh, pitchLow, sourceAddress);
    if (track.currentLogicalProgram == logical) {
      track.melodicProgram = newProgram;
      if (!track.lastWasPercussion) {
        out.instrument(InstrumentIdentity{.domain = std::string(kInstrumentDomain), .key = newProgram});
      }
    }
  }

  void loadVoice(u8 index, u8 percussionMinimum, IntelliMode mode) {
    // The declared table is the only typed data boundary; bytes belonging to
    // following commands are not silently reinterpreted as voice records.
    if (index >= program.voiceTable.size()) {
      return;
    }
    const VoiceRecord& record = program.voiceTable[index];
    volume(record.volume);
    const u8 panValue = mode == IntelliMode::Fe3 ? record.pan : record.pan & 0x1f;
    pan(panValue);

    double tuningCents = 0.0;
    s8 transpose = track.transpose;
    if (mode == IntelliMode::Fe3) {
      constexpr std::array<s8, 7> transposes{-24, -12, -1, 0, 1, 12, 24};
      const u8 tuning = record.tuningTranspose & 0x0f;
      const u8 transposeIndex = (record.tuningTranspose >> 4) & 7;
      if (tuning != 0) {
        tuningCents = ((tuning - 1) * 5 / 256.0) * 100.0;
      }
      if (transposeIndex != 0) {
        transpose = transposes[transposeIndex - 1];
      }
    } else {
      tuningCents = (((record.pan >> 5) & 7) * 5 / 256.0) * 100.0;
      transpose = static_cast<s8>(record.tuningTranspose);
    }
    track.transpose = transpose;
    out.tuning(tuningCents);
    melodicProgram(record.instrument, percussionMinimum);
  }

  void clearPercussionTable() { program.percussionTable = {}; }

  void percussionEntry(u8 slot, u8 patch, u8 note, u8 pan) {
    if (slot < program.percussionTable.size()) {
      program.percussionTable[slot] = PercussionEntry{patch, note, pan};
    }
  }

  void enableCustomPercussion() { program.intelliFlags |= 0x40; }

  void intelliFlags(u8 mask, bool enabled) {
    if (enabled) {
      program.intelliFlags |= mask;
    } else {
      program.intelliFlags &= static_cast<u8>(~mask);
    }
  }

  void fe3Flags(u8 param) {
    if (param < 0xf0) {
      return;
    }
    const bool enabled = (param & 8) == 0;
    switch (param & 7) {
      case 0:
        intelliFlags(0x40, enabled);
        break;
      case 7:
        program.customNoteParameters = enabled;
        break;
      default:
        break;
    }
  }

  std::span<const u8> panTable = math::kPan;
};

using Cursor = CompilerCursor<TrackState, Playback>;

struct DecodeContext {
  ByteReader reader;
  const Layout& layout;
  const Profile& selected;
  const Definition& definition;
  std::vector<Diagnostic>* diagnostics = nullptr;

  [[nodiscard]] Address address(u16 raw) const { return Address{layout.resolveAddress(raw)}; }
};

[[nodiscard]] DecodedBytecodeCommand unknownCommand(Cursor& cursor, u8 arguments) {
  auto event = cursor.sourceOnly("Unknown Event", "unknown");
  for (u8 index = 0; index < arguments; ++index) {
    event.u8(fmt::format("arg{}", index + 1), SourceValueDisplay::Hex);
  }
  return event.ignore();
}

[[nodiscard]] DecodedBytecodeCommand decodeNoteParameters(Cursor& cursor, const DecodeContext& context, EventType type,
                                                          u32 begin) {
  auto event = cursor.command("Note Parameters", SequenceSemantic::State);
  const u8 duration =
      event.opcodeValue("duration", cursor.opcode(), SourceValueDisplay::Decimal, SemanticOperandRole::Duration);

  if (type == EventType::LemmingsNoteParameter) {
    bool hasDuration = false;
    bool hasVelocity = false;
    u8 durationRate = 0;
    u8 velocity = 0;
    if (event.peekU8() <= 0x7f) {
      hasDuration = true;
      const u8 raw = event.u8("duration_rate", SemanticOperandRole::Duration);
      durationRate = static_cast<u8>((raw << 1) + (raw >> 1) + (raw & 1));
      event.derived("resolved_duration_rate", durationRate, SemanticOperandRole::Duration);
      if (event.peekU8() <= 0x7f) {
        hasVelocity = true;
        velocity = static_cast<u8>(event.u8("velocity", SemanticOperandRole::Level) << 1);
        event.derived("resolved_velocity", velocity, SemanticOperandRole::Level);
      }
    }
    return event.invoke<&Playback::lemmingsParameters>(duration, hasDuration, durationRate, hasVelocity, velocity);
  }

  if (type == EventType::NoteParameter) {
    bool present = false;
    u8 durationRate = 0;
    u8 velocity = 0;
    if (event.peekU8() <= 0x7f) {
      present = true;
      const u8 packed = event.u8("quantize_velocity", SourceValueDisplay::Hex);
      durationRate = context.definition.duration[(packed >> 4) & 7];
      velocity = context.definition.volume[packed & 15];
      event.derived("duration_rate", durationRate, SemanticOperandRole::Duration);
      event.derived("velocity", velocity, SemanticOperandRole::Level);
    }
    return event.invoke<&Playback::standardParameters>(duration, present, durationRate, velocity);
  }

  event.set<&TrackState::noteLength>(duration);
  std::vector<std::pair<u8, u8>> parameters;
  while (event.peekU8() <= 0x7f && parameters.size() < 0x80) {
    const u8 raw = event.u8(fmt::format("parameter_{}", parameters.size() + 1), SourceValueDisplay::Hex);
    const u8 resolved = context.definition.intelliDurationVolume[raw & 0x3f];
    event.derived(fmt::format("resolved_{}", parameters.size() + 1), resolved,
                  raw < 0x40 ? SemanticOperandRole::Duration : SemanticOperandRole::Level);
    parameters.emplace_back(raw, resolved);
  }

  if (context.selected.intelli != IntelliMode::Fe3) {
    for (const auto& [raw, resolved] : parameters) {
      event.invoke<&Playback::intelliParameter>(raw, resolved);
    }
    return event;
  }

  // FE3 can switch between the ordinary packed byte and its variable-length
  // parameter stream at runtime. Decode both exits and let the semantic action
  // choose one; this is the only overlapping command shape in the driver.
  const bool hasPacked = !parameters.empty();
  const u8 packed = hasPacked ? parameters.front().first : 0;
  const u8 standardDuration = hasPacked ? context.definition.duration[(packed >> 4) & 7] : 0;
  const u8 standardVelocity = hasPacked ? context.definition.volume[packed & 15] : 0;
  event.invoke<&Playback::fe3StandardParameter>(hasPacked, standardDuration, standardVelocity);
  for (const auto& [raw, resolved] : parameters) {
    event.invoke<&Playback::fe3CustomParameter>(raw, resolved);
  }
  const Address standardDestination{begin + 1 + (hasPacked ? 1u : 0u)};
  const Address customDestination = event.nextAddress();
  event.invoke<&Playback::fe3ParameterFlow>(standardDestination, customDestination);
  event.mayBranchTo(standardDestination, SemanticOperandRole::JumpTarget);
  return event.runtimeControlFlow();
}

[[nodiscard]] DecodedBytecodeCommand decodeCommand(const DecodeContext& context, u32 begin) {
  Cursor cursor(context.reader, begin, "nin-snes", context.diagnostics);
  if (!cursor.hasOpcode()) {
    return cursor.truncated();
  }

  const u8 opcode = cursor.opcode();
  const EventType type = context.definition.events[opcode];
  if (type == EventType::NoteParameter || type == EventType::LemmingsNoteParameter ||
      type == EventType::IntelliNoteParameter) {
    return decodeNoteParameters(cursor, context, type, begin);
  }

  switch (type) {
    case EventType::End: {
      auto event = cursor.command("Section End / Pattern Return", SequenceSemantic::End);
      event.invoke<&Playback::endOrReturn>();
      return event.discoverReturn();
    }
    case EventType::Note: {
      auto event = cursor.command("Note", SequenceSemantic::Note);
      const u8 key = event.opcodeValue("key", static_cast<u8>(opcode - context.definition.status.noteMin),
                                       SourceValueDisplay::MidiNote, SemanticOperandRole::NoteKey);
      return event.invoke<&Playback::note>(key);
    }
    case EventType::Tie:
      return cursor.command("Tie", SequenceSemantic::Note).invoke<&Playback::tie>();
    case EventType::Rest:
      return cursor.command("Rest", SequenceSemantic::Rest).invoke<&Playback::rest>();
    case EventType::Percussion: {
      auto event = cursor.command("Percussion Note", SequenceSemantic::Note);
      const u8 slot = event.opcodeValue("slot", static_cast<u8>(opcode - context.definition.status.percussionMin),
                                        SourceValueDisplay::Decimal, SemanticOperandRole::NoteKey);
      const bool intelli =
          (context.selected.intelli == IntelliMode::Ta || context.selected.intelli == IntelliMode::Fe4) &&
          slot < kIntelliDrumSlots;
      return event.invoke<&Playback::percussion>(slot, context.definition.status.percussionMin, intelli);
    }
    case EventType::Program:
    case EventType::Rd2ProgramAndAdsr: {
      auto event =
          cursor.command(type == EventType::Program ? "Program" : "Program And ADSR", SequenceSemantic::Program);
      const u8 program = event.u8("program", SemanticOperandRole::Instrument);
      if (type == EventType::Rd2ProgramAndAdsr) {
        event.u8("adsr1", SourceValueDisplay::Hex);
        event.u8("adsr2", SourceValueDisplay::Hex);
      }
      return event.invoke<&Playback::melodicProgram>(program, context.definition.status.percussionMin);
    }
    case EventType::Call: {
      auto event = cursor.command("Pattern Play", SequenceSemantic::Call);
      const u16 stored = event.u16le("stored_destination", SourceValueDisplay::Address);
      const Address destination = context.address(stored);
      event.derived("destination", destination, SourceValueDisplay::Address, SemanticOperandRole::CallTarget);
      const u8 times = event.u8("times", SemanticOperandRole::Count);
      event.invoke<&Playback::beginPattern>(times, destination);
      return event.call(destination);
    }
    case EventType::Pan: {
      auto event = cursor.command("Pan", SequenceSemantic::Pan);
      return event.invoke<&Playback::pan>(event.u8("pan", SemanticOperandRole::Pan));
    }
    case EventType::PanFade: {
      auto event = cursor.command("Pan Fade", SequenceSemantic::Pan);
      const u8 length = event.u8("length", SemanticOperandRole::Duration);
      const u8 pan = event.u8("pan", SemanticOperandRole::Pan);
      return event.invoke<&Playback::panFade>(length, pan);
    }
    case EventType::VibratoOn: {
      auto event = cursor.command("Vibrato", SequenceSemantic::Modulation);
      const u8 delay = event.u8("delay", SemanticOperandRole::Modulation);
      const u8 rate = event.u8("rate", SemanticOperandRole::Modulation);
      const u8 depth = event.u8("depth", SemanticOperandRole::Modulation);
      return event.invoke<&Playback::vibratoOn>(delay, rate, depth);
    }
    case EventType::VibratoOff:
      return cursor.command("Vibrato Off", SequenceSemantic::Modulation).invoke<&Playback::vibratoOff>();
    case EventType::MasterVolume: {
      auto event = cursor.command("Master Volume", SequenceSemantic::Level);
      return event.invoke<&Playback::masterVolume>(event.u8("volume", SemanticOperandRole::Level));
    }
    case EventType::MasterVolumeFade: {
      auto event = cursor.command("Master Volume Fade", SequenceSemantic::Level);
      const u8 length = event.u8("length", SemanticOperandRole::Duration);
      const u8 volume = event.u8("volume", SemanticOperandRole::Level);
      return event.invoke<&Playback::masterVolumeFade>(length, volume);
    }
    case EventType::Tempo: {
      auto event = cursor.command("Tempo", SequenceSemantic::Tempo);
      return event.invoke<&Playback::tempo>(event.u8("tempo"));
    }
    case EventType::TempoFade: {
      auto event = cursor.command("Tempo Fade", SequenceSemantic::Tempo);
      const u8 length = event.u8("length", SemanticOperandRole::Duration);
      const u8 tempo = event.u8("tempo");
      return event.invoke<&Playback::tempoFade>(length, tempo);
    }
    case EventType::GlobalTranspose: {
      auto event = cursor.command("Global Transpose", SequenceSemantic::Pitch);
      return event.invoke<&Playback::globalTranspose>(
          event.s8("semitones", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Pitch));
    }
    case EventType::Transpose: {
      auto event = cursor.command("Transpose", SequenceSemantic::Pitch);
      return event.set<&TrackState::transpose>(
          event.s8("semitones", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Pitch));
    }
    case EventType::TremoloOn: {
      auto event = cursor.command("Tremolo On", SequenceSemantic::Modulation);
      const u8 delay = event.u8("delay", SemanticOperandRole::Duration);
      const u8 rate = event.u8("rate", SemanticOperandRole::Modulation);
      const u8 depth = event.u8("depth", SemanticOperandRole::Modulation);
      return event.invoke<&Playback::tremoloOn>(delay, rate, depth);
    }
    case EventType::TremoloOff:
      return cursor.command("Tremolo Off", SequenceSemantic::Modulation).invoke<&Playback::tremoloOff>();
    case EventType::Volume: {
      auto event = cursor.command("Volume", SequenceSemantic::Level);
      return event.invoke<&Playback::volume>(event.u8("volume", SemanticOperandRole::Level));
    }
    case EventType::VolumeFade: {
      auto event = cursor.command("Volume Fade", SequenceSemantic::Level);
      const u8 length = event.u8("length", SemanticOperandRole::Duration);
      const u8 volume = event.u8("volume", SemanticOperandRole::Level);
      return event.invoke<&Playback::volumeFade>(length, volume);
    }
    case EventType::VibratoFade: {
      auto event = cursor.command("Vibrato Fade", SequenceSemantic::Modulation);
      return event.invoke<&Playback::vibratoFade>(event.u8("length", SemanticOperandRole::Duration));
    }
    case EventType::PitchEnvelopeTo:
    case EventType::PitchEnvelopeFrom: {
      auto event = cursor.command(type == EventType::PitchEnvelopeTo ? "Pitch Envelope To" : "Pitch Envelope From",
                                  SequenceSemantic::Pitch);
      const u8 delay = event.u8("delay", SemanticOperandRole::Duration);
      const u8 length = event.u8("length", SemanticOperandRole::Duration);
      const s8 semitones = event.s8("semitones", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Pitch);
      return event.invoke<&Playback::pitchEnvelope>(
          type == EventType::PitchEnvelopeTo ? PitchEnvelope::Mode::To : PitchEnvelope::Mode::From, delay, length,
          semitones);
    }
    case EventType::PitchEnvelopeOff:
      return cursor.command("Pitch Envelope Off", SequenceSemantic::Pitch).invoke<&Playback::pitchEnvelopeOff>();
    case EventType::Tuning: {
      auto event = cursor.command("Fine Tuning", SequenceSemantic::Pitch);
      const u8 tuning = event.u8("tuning", SemanticOperandRole::Pitch);
      return event.emitTuning((tuning / 256.0) * 100.0);
    }
    case EventType::EchoOn: {
      auto event = cursor.command("Echo", SequenceSemantic::State);
      const u8 channels = event.u8("channels", SourceValueDisplay::Hex);
      const u8 volumeLeft = event.u8("volume_left");
      const u8 volumeRight = event.u8("volume_right");
      return event.invoke<&Playback::echo>(channels, volumeLeft, volumeRight);
    }
    case EventType::EchoOff:
      return cursor.command("Echo Off", SequenceSemantic::State).invoke<&Playback::echoOff>();
    case EventType::EchoParameter:
    case EventType::EchoVolumeFade: {
      auto event = cursor.sourceOnly(type == EventType::EchoParameter ? "Echo Parameters" : "Echo Volume Fade");
      event.u8(type == EventType::EchoParameter ? "delay" : "length");
      event.u8(type == EventType::EchoParameter ? "feedback" : "volume_left");
      event.u8(type == EventType::EchoParameter ? "fir" : "volume_right");
      return event.ignore();
    }
    case EventType::PitchSlide: {
      auto event = cursor.command("Pitch Slide", SequenceSemantic::Pitch);
      const u8 delay = event.u8("delay", SemanticOperandRole::Duration);
      const u8 length = event.u8("length", SemanticOperandRole::Duration);
      const u8 target = event.u8("target_note", SourceValueDisplay::MidiNote, SemanticOperandRole::NoteKey);
      // During a preceding wait, execute F9 early only after the current pitch change ends.
      return event.invoke<&Playback::pitchSlide>(delay, length, target).duringWaitWhen<&Playback::pitchMotionIdle>();
    }
    case EventType::PercussionBase: {
      auto event = cursor.command("Percussion Base", SequenceSemantic::State);
      return event.invoke<&Playback::percussionBase>(event.u8("program", SemanticOperandRole::InstrumentProgram));
    }
    case EventType::KonamiLoopStart: {
      auto event = cursor.command("Loop Start", SequenceSemantic::Loop);
      return event.set<&TrackState::konamiLoopStart>(event.derived(
          "destination", event.nextAddress(), SourceValueDisplay::Address, SemanticOperandRole::LoopTarget));
    }
    case EventType::KonamiLoopEnd: {
      auto event = cursor.command("Loop End", SequenceSemantic::Repeat);
      const u8 times = event.u8("times", SemanticOperandRole::Count);
      const s8 volumeDelta = event.s8("volume_delta", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Level);
      const s8 pitchDelta = event.s8("pitch_delta", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Pitch);
      event.invoke<&Playback::konamiLoop>(times, volumeDelta, pitchDelta, event.state<&TrackState::konamiLoopStart>());
      return event.runtimeControlFlow();
    }
    case EventType::KonamiAdsrGain: {
      auto event = cursor.sourceOnly("ADSR / GAIN");
      event.u8("adsr1", SourceValueDisplay::Hex);
      event.u8("adsr2", SourceValueDisplay::Hex);
      event.u8("gain", SourceValueDisplay::Hex);
      return event.ignore();
    }
    case EventType::QuintetTuning: {
      auto event = cursor.command("Fine Tuning", SequenceSemantic::Pitch);
      const u8 tuning = event.u8("tuning", SemanticOperandRole::Pitch);
      return event.emitTuning((tuning / 256.0) * 61.8);
    }
    case EventType::QuintetAdsr: {
      auto event = cursor.sourceOnly("ADSR");
      event.u8("adsr1", SourceValueDisplay::Hex);
      event.u8("sustain_rate");
      event.u8("sustain_level");
      return event.ignore();
    }
    case EventType::IntelliEchoOn:
      return cursor.command("Echo On", SequenceSemantic::State).emitReverb(40.0 / 127.0);
    case EventType::IntelliEchoOff:
      return cursor.command("Echo Off", SequenceSemantic::State).emitReverb(0.0);
    case EventType::IntelliLegatoOn:
      return cursor.command("Legato On", SequenceSemantic::State).set<&TrackState::legato>(true).emitLegatoPedal(true);
    case EventType::IntelliLegatoOff:
      return cursor.command("Legato Off", SequenceSemantic::State)
          .set<&TrackState::legato>(false)
          .emitLegatoPedal(false);
    case EventType::IntelliConditionalJump:
    case EventType::IntelliJump: {
      auto event = cursor.command(type == EventType::IntelliJump ? "Short Jump" : "Conditional Short Jump",
                                  SequenceSemantic::Jump);
      const u8 distance = event.u8("distance");
      const Address destination{event.nextAddress().value + distance};
      event.derived("destination", destination, SourceValueDisplay::Address, SemanticOperandRole::JumpTarget);
      // The driver's "conditional" form tests a state that legacy conversion
      // has always treated as taken; retain that established interpretation.
      return event.jump(destination);
    }
    case EventType::IntelliFe3F5: {
      auto event = cursor.command("FE3 Flags / Port Wait", SequenceSemantic::State);
      return event.invoke<&Playback::fe3Flags>(event.u8("parameter", SourceValueDisplay::Hex));
    }
    case EventType::IntelliWritePort: {
      auto event = cursor.sourceOnly("Write APU Port");
      event.u8("value");
      return event.ignore();
    }
    case EventType::IntelliFe3F9: {
      auto event = cursor.sourceOnly("Unknown FE3 Table", "unknown");
      for (u8 index = 0; index < 36; ++index) {
        event.u8(fmt::format("byte_{}", index + 1), SourceValueDisplay::Hex);
      }
      return event.ignore();
    }
    case EventType::IntelliDefineVoice: {
      auto event = cursor.command("Voice Parameter Definition", SequenceSemantic::Program);
      const s8 parameter = event.s8("count_or_instrument", SourceValueDisplay::SignedDecimal);
      if (parameter >= 0) {
        const u8 count = static_cast<u8>(parameter);
        event.invoke<&Playback::defineVoiceTable>(count);
        for (u8 index = 0; index < count; ++index) {
          const u8 instrument = event.u8(fmt::format("instrument_{}", index), SemanticOperandRole::Instrument);
          const u8 volume = event.u8(fmt::format("volume_{}", index), SemanticOperandRole::Level);
          const u8 pan = event.u8(fmt::format("pan_{}", index), SemanticOperandRole::Pan);
          const u8 tuningTranspose = event.u8(fmt::format("tuning_transpose_{}", index), SemanticOperandRole::Pitch);
          event.invoke<&Playback::defineVoice>(index, instrument, volume, pan, tuningTranspose);
        }
        return event;
      }
      if (context.selected.intelli != IntelliMode::Fe3 && context.selected.intelli != IntelliMode::Ta) {
        return event.ignore();
      }
      const u8 logical = static_cast<u8>(parameter) & 0x3f;
      const u8 srcn = event.u8("srcn", SourceValueDisplay::Hex);
      const u8 adsr1 = event.u8("adsr1", SourceValueDisplay::Hex);
      const u8 adsr2 = event.u8("adsr2", SourceValueDisplay::Hex);
      const u8 gain = event.u8("gain", SourceValueDisplay::Hex);
      const u8 pitchHigh = event.u8("pitch_high", SourceValueDisplay::Hex);
      const u8 pitchLow = event.u8("pitch_low", SourceValueDisplay::Hex);
      if (context.selected.intelli == IntelliMode::Ta) {
        return event.invoke<&Playback::overwriteInstrument>(logical, srcn, adsr1, adsr2, gain, pitchHigh, pitchLow,
                                                            Address{begin});
      }
      return event.ignore();
    }
    case EventType::IntelliLoadVoice: {
      auto event = cursor.command("Load Voice Parameters", SequenceSemantic::Program);
      const u8 index = event.u8("index");
      return event.invoke<&Playback::loadVoice>(index, context.definition.status.percussionMin,
                                                context.selected.intelli);
    }
    case EventType::IntelliAdsr: {
      auto event = cursor.sourceOnly("ADSR");
      event.u8("adsr1", SourceValueDisplay::Hex);
      event.u8("adsr2", SourceValueDisplay::Hex);
      return event.ignore();
    }
    case EventType::IntelliGainDurationRate: {
      auto event = cursor.command("GAIN Duration Rate", SequenceSemantic::State);
      const u8 rate = event.u8("duration_rate", SemanticOperandRole::Duration);
      event.u8("gain", SourceValueDisplay::Hex);
      if (context.selected.intelli == IntelliMode::Ta) {
        return event.set<&TrackState::durationRate>(rate);
      }
      return event.ignore();
    }
    case EventType::IntelliGainDuration: {
      auto event = cursor.command("GAIN Duration", SequenceSemantic::State);
      const u8 rate = event.u8("duration_rate", SemanticOperandRole::Duration);
      return context.selected.intelli == IntelliMode::Ta ? event.set<&TrackState::durationRate>(rate) : event.ignore();
    }
    case EventType::IntelliGain: {
      auto event = cursor.sourceOnly("GAIN");
      event.u8("gain", SourceValueDisplay::Hex);
      return event.ignore();
    }
    case EventType::IntelliCustomPercussion: {
      auto event = cursor.command("Custom Percussion Table", SequenceSemantic::State);
      const u8 packedCount = event.u8("packed_count", SourceValueDisplay::Hex);
      const u8 count = static_cast<u8>((packedCount & 0x0f) + 1);
      event.derived("count", count, SemanticOperandRole::Count);
      event.invoke<&Playback::clearPercussionTable>();
      for (u8 slot = 0; slot < count; ++slot) {
        const u8 patch =
            event.u8(fmt::format("patch_{}", slot), SourceValueDisplay::Hex, SemanticOperandRole::Instrument);
        const u8 note =
            event.u8(fmt::format("note_{}", slot), SourceValueDisplay::MidiNote, SemanticOperandRole::NoteKey);
        const u8 pan = event.u8(fmt::format("pan_{}", slot), SemanticOperandRole::Pan);
        if (slot < kIntelliDrumSlots) {
          event.invoke<&Playback::percussionEntry>(slot, patch, note, pan);
        }
      }
      return event.invoke<&Playback::enableCustomPercussion>();
    }
    case EventType::IntelliTaSubevent:
    case EventType::IntelliFe4Subevent: {
      auto event = cursor.command("Intelligent Systems Subevent", SequenceSemantic::State);
      const u8 subtype = event.u8("subtype", SourceValueDisplay::Hex);
      if (type == EventType::IntelliTaSubevent && subtype == 0) {
        event.u16le("request_value", SourceValueDisplay::Hex);
        event.u8("request_type", SourceValueDisplay::Hex);
        return event.ignore();
      }
      if (subtype == 1 || subtype == 2) {
        const u8 mask = event.u8("mask", SourceValueDisplay::Hex);
        return event.invoke<&Playback::intelliFlags>(mask, subtype == 1);
      }
      if (type == EventType::IntelliTaSubevent && subtype == 3) {
        return event.set<&TrackState::legato>(true).emitLegatoPedal(true);
      }
      if (type == EventType::IntelliTaSubevent && subtype == 4) {
        return event.set<&TrackState::legato>(false).emitLegatoPedal(false);
      }
      if (type == EventType::IntelliTaSubevent && subtype == 5) {
        event.u8("global_byte", SourceValueDisplay::Hex);
      }
      return event.ignore();
    }
    case EventType::Nop:
      return cursor.sourceOnly("NOP").ignore();
    case EventType::Nop1: {
      auto event = cursor.sourceOnly("NOP");
      event.u8("argument", SourceValueDisplay::Hex);
      return event.ignore();
    }
    case EventType::Unknown1:
      return unknownCommand(cursor, 1);
    case EventType::Unknown2:
      return unknownCommand(cursor, 2);
    case EventType::Unknown3:
      return unknownCommand(cursor, 3);
    case EventType::Unknown4:
      return unknownCommand(cursor, 4);
    case EventType::Unknown0:
    default:
      return unknownCommand(cursor, 0);
  }
}

struct PlaylistDecode {
  SectionPlaylist playlist;
  std::optional<SourceAnnotationId> annotation;
};

[[nodiscard]] PlaylistDecode decodePlaylist(ByteReader reader, const Layout& layout, AssetId sequenceId,
                                            SourceMapBuilder* sourceMap, std::vector<Diagnostic>* diagnostics) {
  const Profile& selected = profile(layout.profile);
  std::map<u32, PlaylistCommand> commands;
  std::map<u32, SequenceSection> sections;
  std::vector<u32> pending{layout.playlistAddress};

  const auto warn = [&](std::string message, SourceRange range) {
    if (diagnostics != nullptr) {
      diagnostics->push_back(Diagnostic{
          .severity = Severity::Warning,
          .message = std::move(message),
          .range = range.valid() ? std::optional<SourceRange>{range} : std::nullopt,
      });
    }
  };
  const auto queue = [&](u32 address) {
    if (reader.has(address, 2) && !commands.contains(address) && std::ranges::find(pending, address) == pending.end()) {
      pending.push_back(address);
    }
  };
  const auto decodeSection = [&](u16 address) -> std::optional<SequenceSection> {
    if (!reader.has(address, kTrackCount * 2)) {
      warn(fmt::format("NinSnes section ${:04X} did not contain eight track pointers", address),
           reader.range(address, reader.has(address, 1) ? 1 : 0));
      return std::nullopt;
    }
    SequenceSection section{
        .address = Address{address},
        .trackStarts = std::vector<std::optional<Address>>(kTrackCount),
    };
    bool active = false;
    for (u8 track = 0; track < kTrackCount; ++track) {
      const u16 raw = reader.le16(address + track * 2);
      if ((raw & 0xff00) == 0) {
        continue;
      }
      const u16 start = layout.resolveAddress(raw);
      if (!reader.has(start, 1)) {
        warn(fmt::format("NinSnes track pointer ${:04X} was outside ARAM", start),
             reader.range(address + track * 2, 2));
        continue;
      }
      section.trackStarts[track] = Address{start};
      active = true;
    }
    return active ? std::optional<SequenceSection>{std::move(section)} : std::nullopt;
  };

  while (!pending.empty() && commands.size() < 4096) {
    const u32 address = pending.back();
    pending.pop_back();
    if (commands.contains(address) || !reader.has(address, 2)) {
      continue;
    }
    const u16 value = reader.le16(address);
    PlaylistCommand command{
        .address = Address{address},
        .fallthrough = Address{address + 2},
        .range = reader.range(address, 2),
    };
    if (value == 0) {
      command.operation = PlaylistEnd{};
    } else if (value <= 0xff) {
      if (!reader.has(address, 4)) {
        warn("NinSnes playlist repeat was truncated", reader.range(address, 2));
        command.operation = PlaylistEnd{};
      } else {
        const u16 storedDestination = reader.le16(address + 2);
        const u16 destination = layout.resolveAddress(storedDestination);
        command.fallthrough = Address{address + 4};
        command.range = reader.range(address, 4);
        command.operation = PlaylistRepeat{
            .additionalPlays = value,
            .destination = Address{destination},
            .infinite = selected.playlist == PlaylistModel::Tose ? (value == 0 || value == 0xff) : value > 0x80,
        };
        queue(destination);
        queue(address + 4);
      }
    } else {
      const u16 sectionAddress = layout.resolveAddress(value);
      command.operation = PlaylistPlaySection{.section = Address{sectionAddress}};
      if (!sections.contains(sectionAddress)) {
        if (auto section = decodeSection(sectionAddress)) {
          sections.emplace(sectionAddress, std::move(*section));
        }
      }
      queue(address + 2);
    }
    commands.emplace(address, std::move(command));
  }

  PlaylistDecode decoded{
      .playlist =
          SectionPlaylist{
              .startAddress = Address{layout.playlistAddress},
          },
  };
  for (auto& [_, section] : sections) {
    decoded.playlist.sections.push_back(std::move(section));
  }
  for (auto& [_, command] : commands) {
    decoded.playlist.commands.push_back(std::move(command));
  }

  if (sourceMap == nullptr || decoded.playlist.commands.empty()) {
    return decoded;
  }
  const u64 first = decoded.playlist.commands.front().range.offset;
  u64 last = first;
  for (const auto& command : decoded.playlist.commands) {
    last = std::max(last, command.range.endOffset());
  }
  decoded.annotation = sourceMap
                           ->header("Section Playlist",
                                    SourceRange{
                                        .source = reader.source(),
                                        .offset = first,
                                        .size = static_cast<u32>(last - first),
                                    })
                           .kind("nin-snes-playlist")
                           .owner(ObjectRefs::sequence(sequenceId))
                           .id();

  for (auto& command : decoded.playlist.commands) {
    auto annotation = sourceMap
                          ->command(std::holds_alternative<PlaylistPlaySection>(command.operation)
                                        ? "Play Section"
                                        : (std::holds_alternative<PlaylistRepeat>(command.operation) ? "Repeat Playlist"
                                                                                                     : "Playlist End"),
                                    command.range,
                                    std::holds_alternative<PlaylistRepeat>(command.operation) ? SequenceSemantic::Repeat
                                                                                              : SequenceSemantic::Meta)
                          .kind("nin-snes-playlist-command")
                          .parent(*decoded.annotation)
                          .field("value", reader.range(command.range.offset, 2), reader.le16(command.range.offset),
                                 SourceValueDisplay::Hex);
    if (const auto* play = std::get_if<PlaylistPlaySection>(&command.operation)) {
      annotation.derived("section", play->section.value, SourceValueDisplay::Address)
          .link(SourceLinkRole::PointsTo, SourceTarget{reader.range(play->section.value, kTrackCount * 2)});
    } else if (const auto* repeat = std::get_if<PlaylistRepeat>(&command.operation)) {
      annotation
          .field("destination", reader.range(command.range.offset + 2, 2), repeat->destination.value,
                 SourceValueDisplay::Address)
          .derived("additional_plays", repeat->additionalPlays)
          .derived("infinite", repeat->infinite)
          .link(SourceLinkRole::RepeatTarget, SourceTarget{reader.range(repeat->destination.value, 2)});
    }
    static_cast<void>(annotation.id());
  }

  for (const auto& section : decoded.playlist.sections) {
    auto annotation = sourceMap->header("Section", reader.range(section.address.value, kTrackCount * 2))
                          .kind("nin-snes-section")
                          .parent(*decoded.annotation)
                          .owner(ObjectRefs::sequence(sequenceId));
    for (u8 track = 0; track < section.trackStarts.size(); ++track) {
      const SourceRange range = reader.range(section.address.value + track * 2, 2);
      if (section.trackStarts[track]) {
        annotation
            .field(fmt::format("track_{}", track + 1), range, section.trackStarts[track]->value,
                   SourceValueDisplay::Address)
            .link(SourceLinkRole::PointsTo, SourceTarget{reader.range(section.trackStarts[track]->value, 1)});
      } else {
        annotation.field(fmt::format("track_{}", track + 1), range, 0, SourceValueDisplay::Address);
      }
    }
  }
  return decoded;
}

[[nodiscard]] TrackProgram decodeTrack(ByteReader reader, u32 trackNumber, const std::vector<Address>& starts,
                                       const DecodeContext& context, AssetId sequenceId,
                                       std::optional<SourceAnnotationId> parent, SourceMapBuilder* sourceMap) {
  if (starts.empty()) {
    return TrackProgram{
        .id = TrackId{trackNumber},
        .sourceTrackNumber = trackNumber,
    };
  }

  // A channel can begin at a different address in every section. Discover all
  // roots into one immutable program, then the playlist selects the right root
  // each time that section starts.
  std::map<u32, DecodedBytecodeCommand> commands;
  std::vector<u32> pending;
  for (const Address start : starts) {
    pending.push_back(static_cast<u32>(start.value));
  }
  while (!pending.empty() && commands.size() < kMaxTrackCommands) {
    u32 address = pending.back();
    pending.pop_back();
    while (reader.has(address, 1) && !commands.contains(address) && commands.size() < kMaxTrackCommands) {
      DecodedBytecodeCommand command = decodeCommand(context, address);
      for (const Address target : command.flow.staticTargets) {
        if (reader.has(target.value, 1) && !commands.contains(static_cast<u32>(target.value))) {
          pending.push_back(static_cast<u32>(target.value));
        }
      }
      const auto fallthrough = command.flow.fallthrough;
      commands.emplace(address, std::move(command));
      if (!fallthrough) {
        break;
      }
      address = static_cast<u32>(fallthrough->value);
    }
  }

  const TrackDecodeScope scope{
      .reader = reader,
      .maxCommands = kMaxTrackCommands,
      .sequenceAsset = sequenceId,
      .parentAnnotation = parent,
      .sourceMap = sourceMap,
  };
  auto session = scope.begin(trackNumber, static_cast<u32>(starts.front().value));
  for (auto& [address, command] : commands) {
    session.append(std::move(command), address);
  }
  return session.finish();
}

[[nodiscard]] std::vector<InstrumentIdentity> buildProgramMap(ByteReader reader, const Layout& layout) {
  const Profile& selected = profile(layout.profile);
  std::vector<InstrumentIdentity> map(256);
  for (u16 sourceProgram = 0; sourceProgram < map.size(); ++sourceProgram) {
    u8 resolved = static_cast<u8>(sourceProgram);
    if (selected.programs == ProgramResolver::QuintetActRBase) {
      resolved = static_cast<u8>(resolved + layout.quintetBgmInstrumentBase);
    } else if (selected.programs == ProgramResolver::QuintetLookup) {
      const u32 address = layout.quintetInstrumentLookupAddress + resolved;
      if (reader.has(address, 1)) {
        resolved = reader.u8At(address);
      }
    }
    map[sourceProgram] = InstrumentIdentity{
        .domain = std::string(kInstrumentDomain),
        .key = resolved,
    };
  }
  return map;
}

[[nodiscard]] SequenceRecipes projectRecipes(const ProgramState& state) {
  return state.recipes;
}

[[nodiscard]] SequenceDialect makeDialect() {
  return makeCompiledDialect<TrackState, Playback, ProgramState>(SequenceDialect{
      .id = DialectId{.value = "nin-snes"},
      .commandDetailKindPrefix = "nin-snes",
      .timebase = Timebase{.ppqn = kPpqn},
      .defaultBehavior =
          SequenceProgramBehavior{
              .defaultLoopPolicy = LoopPolicy::PlayOnce,
              .panLaw = PanLaw::ConstantSum,
              .initialReverbSend = 0.0,
              .initialPitchBendRangeSemitones = 2,
              .initialTempoMicrosecondsPerQuarter = math::tempoMicrosecondsPerQuarter(kDefaultTempo),
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

SequenceParse decodeSequence(ByteReader reader, const Layout& layout, AssetId sequenceId, SourceMapBuilder* sourceMap,
                             std::vector<Diagnostic>* diagnostics) {
  const Profile& selected = profile(layout.profile);
  const Definition definition = makeDefinition(layout);
  PlaylistDecode playlist = decodePlaylist(reader, layout, sequenceId, sourceMap, diagnostics);

  SequenceProgram program = sequenceDialect().makeProgram(Address{layout.playlistAddress});
  program.config.profile = static_cast<u32>(layout.profile);
  program.sourceProgramMap = buildProgramMap(reader, layout);
  program.sectionPlaylist = std::move(playlist.playlist);
  DecodeContext context{
      .reader = reader,
      .layout = layout,
      .selected = selected,
      .definition = definition,
      .diagnostics = diagnostics,
  };

  program.tracks.reserve(kTrackCount);
  for (u8 track = 0; track < kTrackCount; ++track) {
    std::vector<Address> starts;
    for (const SequenceSection& section : program.sectionPlaylist->sections) {
      if (section.trackStarts[track] && std::ranges::find_if(starts, [&](Address address) {
                                          return address.value == section.trackStarts[track]->value;
                                        }) == starts.end()) {
        starts.push_back(*section.trackStarts[track]);
      }
    }
    program.tracks.push_back(decodeTrack(reader, track, starts, context, sequenceId, playlist.annotation, sourceMap));
  }

  SequenceRecipes recipes =
      analyzeCompiledProgram<ProgramState, SequenceRecipes>(program, sequenceDialect(), projectRecipes);
  return SequenceParse{
      .program = std::move(program),
      .recipes = std::move(recipes),
  };
}

}  // namespace vgmtrans::formats::nin_snes
