/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/CapcomSnes/CapcomSnes.h"

#include "value/base/LevelScale.h"
#include "value/sequence/BytecodeDecode.h"
#include "value/sequence/CommandSourceMap.h"
#include "value/sequence/SemanticCommand.h"
#include "value/sequence/SequenceVm.h"

#include <fmt/format.h>

#include <algorithm>
#include <any>
#include <array>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vgmtrans::formats::capcom_snes {

using namespace core;

namespace {

constexpr u8 kNoteOctaveMask = 0x07;
constexpr u8 kNoteOctaveUpMask = 0x08;
constexpr u8 kNoteDottedMask = 0x10;
constexpr u8 kNoteTripletMask = 0x20;
constexpr u8 kNoteSlurredMask = 0x40;

[[nodiscard]] constexpr u32 profileFor(CapcomSnesEngineVersion version) {
  return static_cast<u32>(version);
}

[[nodiscard]] CapcomSnesEngineVersion versionFrom(const SequenceProgram& program) {
  switch (static_cast<CapcomSnesEngineVersion>(program.config.profile)) {
    case CapcomSnesEngineVersion::v1BgmInList:
    case CapcomSnesEngineVersion::v2BgmUsuallyAtFixedLocation:
    case CapcomSnesEngineVersion::v3BgmFixedLocation:
      return static_cast<CapcomSnesEngineVersion>(program.config.profile);
    case CapcomSnesEngineVersion::none:
      // Directly constructed programs historically implied the newest driver.
      return CapcomSnesEngineVersion::v3BgmFixedLocation;
  }
  return CapcomSnesEngineVersion::v3BgmFixedLocation;
}

namespace math {

constexpr std::array<u8, 17> kVolumeCurve{0x00, 0x0c, 0x19, 0x26, 0x33, 0x40, 0x4c, 0x59, 0x66,
                                          0x73, 0x80, 0x8c, 0x99, 0xb3, 0xcc, 0xe6, 0xff};
constexpr std::array<u8, 22> kPanCurve{0x00, 0x01, 0x03, 0x07, 0x0d, 0x15, 0x1e, 0x29, 0x34, 0x42, 0x51,
                                       0x5e, 0x67, 0x6e, 0x73, 0x77, 0x7a, 0x7c, 0x7d, 0x7e, 0x7f, 0x7f};
constexpr double kPiOverTwo = 1.57079632679489661923;
constexpr double kLfoStepHertz = 1000.0 / 16384.0;
constexpr double kVibratoBaseHertz = kLfoStepHertz;
constexpr double kVibratoMaxHertz = 255.0 * kLfoStepHertz;
constexpr double kTremoloMuteFloorCentibels = 960.0;
constexpr double kTremoloHalfDepthCentibels = 484.0;

struct Pan {
  double position = 0.0;
  double gain = 1.0;
};

[[nodiscard]] int interpolate(const auto& table, int index, int fraction) {
  const int lower = table[index];
  const int upper = table[index + 1];
  return lower + (((upper - lower) * fraction) >> 8);
}

[[nodiscard]] Pan panFromBalance(double sourceLeft, double sourceRight) {
  if (sourceLeft == 0.0 && sourceRight == 0.0) {
    return Pan{.gain = 0.0};
  }
  const double angle = std::atan2(sourceRight, sourceLeft);
  return Pan{
      .position = std::clamp((angle / kPiOverTwo) * 2.0 - 1.0, -1.0, 1.0),
      .gain = (sourceLeft + sourceRight) / (std::cos(angle) + std::sin(angle)),
  };
}

[[nodiscard]] double volumeGain(CapcomSnesEngineVersion version, u8 rawVolume) {
  if (version == CapcomSnesEngineVersion::v1BgmInList) {
    return rawVolume / 255.0;
  }
  if (rawVolume >= 0x80) {
    return 1.0;
  }
  const int index = rawVolume >> 3;
  const int fraction = ((rawVolume & 0x07) << 5) | 0x1f;
  return static_cast<double>(interpolate(kVolumeCurve, index, fraction)) / 255.0;
}

[[nodiscard]] double tuningCents(s8 tuning) {
  return static_cast<double>(tuning) * (100.0 / 256.0);
}

[[nodiscard]] double portamentoMillisecondsPerCent(u8 rawTime) {
  const u8 step = static_cast<u8>((rawTime << 1) & 0xff);
  const double centsPerUpdate = step * (100.0 / 256.0);
  return centsPerUpdate == 0.0 ? 0.0 : (0.016 / centsPerUpdate) * 1000.0;
}

[[nodiscard]] u32 baseNoteTicks(u32 rawDuration) {
  return rawDuration == 0 || rawDuration > 7 ? 0 : 192u >> (7u - rawDuration);
}

[[nodiscard]] u32 tempoMicrosecondsPerQuarter(u32 rawTempo) {
  return rawTempo == 0 ? 60000000 : static_cast<u32>(std::round(kCapcomSnesPpqn * (125 * 0x40) * 2 * 256.0 / rawTempo));
}

[[nodiscard]] Pan pan(CapcomSnesEngineVersion version, u8 rawPan) {
  const auto biasedPan = static_cast<u8>(rawPan + 0x80);
  if (version == CapcomSnesEngineVersion::v1BgmInList) {
    const double position = biasedPan == 255 ? 1.0 : biasedPan / 256.0;
    return panFromBalance(1.0 - position, position);
  }

  const u16 rightPosition = static_cast<u16>(biasedPan) * 20;
  const u16 leftPosition = 0x1400 - rightPosition;
  const double left = interpolate(kPanCurve, leftPosition >> 8, leftPosition & 0xff) / 128.0;
  const double right = interpolate(kPanCurve, rightPosition >> 8, rightPosition & 0xff) / 128.0;
  return panFromBalance(left, right);
}

[[nodiscard]] double normalizedDepth(u8 value) {
  return static_cast<double>(value) / 127.0;
}

[[nodiscard]] double tremoloDepth(CapcomSnesEngineVersion version, u8 rawDepth) {
  int trough = 0;
  int peak = 250;
  if (version == CapcomSnesEngineVersion::v1BgmInList) {
    const int depth = rawDepth & 0x7f;
    trough = depth == 0 ? 255 : 255 - ((2 * depth * 255) >> 8);
    peak = 255;
  } else if (rawDepth == 0) {
    trough = 250;
  } else if (rawDepth >= 127) {
    trough = 0;
  } else {
    const int inversePosition = 0x7e81 - rawDepth * 255;
    const int curvePosition = inversePosition >> 3;
    trough = interpolate(kVolumeCurve, curvePosition >> 8, curvePosition & 0xff);
  }

  double depthCentibels = kTremoloMuteFloorCentibels;
  if (trough > 0) {
    depthCentibels =
        std::clamp(200.0 * std::log10(peak / static_cast<double>(trough)), 0.0, kTremoloMuteFloorCentibels);
  }
  return std::clamp(depthCentibels / (2.0 * kTremoloHalfDepthCentibels), 0.0, 1.0);
}

[[nodiscard]] double lfoRate(u8 rawRate) {
  if (rawRate == 0) {
    return 0.0;
  }
  const auto cents = [](double hertz) { return 1200.0 * std::log2(hertz / 440.0) + 6900.0; };
  const double position = (cents(rawRate * kLfoStepHertz) - cents(kVibratoBaseHertz)) /
                          (cents(kVibratoMaxHertz) - cents(kVibratoBaseHertz));
  return std::clamp(position, 0.0, 1.0);
}

}  // namespace math

// Mutable state that the original driver carries from one command to the next.
// Value conversion belongs in decode; timing and note-to-note behavior stay
// here because they depend on execution history.
struct TrackState {
  CapcomSnesEngineVersion version = CapcomSnesEngineVersion::v3BgmFixedLocation;
  u32 durationRate = 0;
  s32 transpose = 0;
  u32 noteOctave = 0;
  bool noteDotted = false;
  bool noteTriplet = false;
  bool noteSlurred = false;
  bool noteOctaveUp = false;
  u8 modulationRate = 0;
  double vibratoDepth = 0.0;
  double tremoloDepth = 0.0;
  double portamentoMillisecondsPerCent = 0.0;
  u16 lastPortamentoTime = 0;
  std::optional<s32> lastSourceKey;
  bool lastNoteSlurred = false;
  bool didRest = false;

  [[nodiscard]] u32 consumeNoteTicks(u8 rawDuration) {
    u32 length = math::baseNoteTicks(rawDuration);
    if (noteDotted) {
      length = (length % 2 == 0 && length < 0x80) ? length + (length / 2) : 0;
      noteDotted = false;
    } else if (noteTriplet) {
      length = length * 2 / 3;
    }
    return length;
  }

  [[nodiscard]] u32 soundingTicks(u32 length) const {
    u32 duration = length * durationRate;
    if (noteSlurred || duration == 0) {
      duration = length << 8;
    }
    duration = (duration + 0x80) >> 8;
    return duration == 0 ? 1 : duration;
  }

  [[nodiscard]] s32 sourceKey(u8 keyIndex) const {
    return static_cast<s32>(keyIndex) - 1 + static_cast<s32>(noteOctave * 12) + (noteOctaveUp ? 24 : 0);
  }

  void applyAttributes(u8 attributes, PerformanceEmitter& out) {
    const bool wasSlurred = noteSlurred;
    noteOctave |= attributes & kNoteOctaveMask;
    noteDotted = noteDotted || ((attributes & kNoteDottedMask) != 0);
    noteOctaveUp = (attributes & kNoteOctaveUpMask) != 0;
    noteTriplet = (attributes & kNoteTripletMask) != 0;
    noteSlurred = (attributes & kNoteSlurredMask) != 0;
    if (noteSlurred != wasSlurred) {
      out.legatoPedal(noteSlurred);
    }
  }

  void emitModulationDepths(PerformanceEmitter& out, bool enabled) const {
    if (vibratoDepth != 0) {
      out.modulation(ModulationPerformanceTarget::VibratoDepth, enabled ? vibratoDepth : 0.0);
    }
    if (tremoloDepth != 0) {
      out.modulation(ModulationPerformanceTarget::TremoloDepth, enabled ? tremoloDepth : 0.0);
    }
  }
};

using Args = SemanticCommandArgs;

// Playback gathers the mutable driver state and the two shared VM interfaces in
// one concrete object. The generic std::any casts happen once at the dialect
// boundary; individual commands never need to know about type erasure.
struct Playback {
  TrackState& track;
  PerformanceEmitter& out;
  VmApi& vm;

  [[nodiscard]] Effects rest(u8 durationIndex) {
    const u32 length = track.consumeNoteTicks(durationIndex);
    track.didRest = true;
    return Effects::wait(length);
  }

  [[nodiscard]] Effects note(u8 durationIndex, u8 keyIndex) {
    const u32 length = track.consumeNoteTicks(durationIndex);
    const s32 key = track.sourceKey(keyIndex);
    const u32 duration = track.soundingTicks(length);

    if (track.lastNoteSlurred && track.lastSourceKey && key == *track.lastSourceKey && !track.didRest) {
      out.note(static_cast<double>(key + track.transpose), 1.0, duration, true);
      track.lastNoteSlurred = track.noteSlurred;
      return Effects::wait(length);
    }

    if (track.portamentoMillisecondsPerCent > 0.0 && track.lastSourceKey) {
      const auto distance = static_cast<u32>(std::abs(key - *track.lastSourceKey));
      const auto portamentoTime = static_cast<u16>(distance * 100 * track.portamentoMillisecondsPerCent);
      if (portamentoTime != track.lastPortamentoTime) {
        out.portamento(static_cast<double>(portamentoTime),
                       static_cast<double>(*track.lastSourceKey + track.transpose));
        track.lastPortamentoTime = portamentoTime;
      } else {
        out.portamentoControl(static_cast<double>(*track.lastSourceKey + track.transpose));
      }
    }

    out.note(static_cast<double>(key + track.transpose), 1.0, duration + (track.noteSlurred ? 1u : 0u));
    track.lastSourceKey = key;
    track.didRest = false;
    track.lastNoteSlurred = track.noteSlurred;
    return Effects::wait(length);
  }
};

// The shared decoder owns generic operand/source plumbing. Capcom adds only the
// detected driver version because a few source conversions depend on it.
class Decode : public SemanticCommandDecoder {
public:
  Decode(ByteReader reader, u32 begin, u32 end, CapcomSnesEngineVersion version, std::vector<Diagnostic>* diagnostics)
      : SemanticCommandDecoder(reader, begin, end, diagnostics), version_(version) {}

  [[nodiscard]] CapcomSnesEngineVersion version() const noexcept { return version_; }

private:
  CapcomSnesEngineVersion version_;
};

using DecodeFunction = void (*)(Decode&);
using ExecuteFunction = Effects (*)(Args, Playback&);

struct CommandDefinition {
  DecodedCommandPresentation presentation;
  DecodeFunction decode = nullptr;
  ExecuteFunction execute = nullptr;
};

[[nodiscard]] CommandDefinition command(std::string_view label, SequenceSemantic semantic, DecodeFunction decode,
                                        ExecuteFunction execute,
                                        CommandPlaybackStatus playback = CommandPlaybackStatus::AffectsPlayback,
                                        std::string_view localKind = {}) {
  const std::string kind = localKind.empty() ? sourceLocalKind(label) : std::string(localKind);
  return CommandDefinition{
      .presentation =
          DecodedCommandPresentation{
              .label = std::string(label),
              .localKind = kind,
              .detailKind = "capcom-snes." + kind,
              .semantic = semantic,
              .playback = playback,
          },
      .decode = decode,
      .execute = execute,
  };
}

[[nodiscard]] CommandDefinition command(std::string_view label, SequenceSemantic semantic, ExecuteFunction execute,
                                        CommandPlaybackStatus playback = CommandPlaybackStatus::AffectsPlayback,
                                        std::string_view localKind = {}) {
  return command(label, semantic, nullptr, execute, playback, localKind);
}

[[nodiscard]] CommandDefinition unsupportedCommand() {
  return command(
      "Unsupported", SequenceSemantic::Unsupported, [](Decode& d) { d.terminate(); },
      [](Args, Playback& p) { return Effects{.step = p.vm.end()}; }, CommandPlaybackStatus::Unsupported, "unsupported");
}

[[nodiscard]] CommandDefinition truncatedCommand() {
  return command(
      "Truncated Command", SequenceSemantic::Unsupported, [](Decode& d) { d.terminate(); },
      [](Args, Playback& p) { return Effects{.step = p.vm.end()}; }, CommandPlaybackStatus::Unsupported, "truncated");
}

using OpcodeProfile = std::array<CommandDefinition, 0x20>;

// The profile is the implementation, not merely an opcode-name table. For each
// command, source decoding and playback behavior are adjacent. A reader never
// needs to correlate separate metadata, operand, flow, and execution switches.
[[nodiscard]] OpcodeProfile makeBaseProfile() {
  OpcodeProfile profile;
  profile.fill(unsupportedCommand());

  profile[0x00] = command("Toggle Triplet", SequenceSemantic::State, [](Args, Playback& p) {
    p.track.noteTriplet = !p.track.noteTriplet;
    return Effects{};
  });

  profile[0x01] = command("Toggle Slur", SequenceSemantic::State, [](Args, Playback& p) {
    p.track.noteSlurred = !p.track.noteSlurred;
    p.out.legatoPedal(p.track.noteSlurred);
    return Effects{};
  });

  profile[0x02] = command("Dotted Note", SequenceSemantic::State, [](Args, Playback& p) {
    p.track.noteDotted = true;
    return Effects{};
  });

  profile[0x03] = command("Toggle Octave Up", SequenceSemantic::State, [](Args, Playback& p) {
    p.track.noteOctaveUp = !p.track.noteOctaveUp;
    return Effects{};
  });

  profile[0x04] = command(
      "Note Attributes", SequenceSemantic::State, [](Decode& d) { d.u8("attributes", SourceValueDisplay::Hex); },
      [](Args a, Playback& p) {
        p.track.applyAttributes(a.u8(), p.out);
        return Effects{};
      });

  profile[0x05] = command(
      "Tempo", SequenceSemantic::Tempo,
      [](Decode& d) { d.resolved("microseconds_per_quarter", d.rawU16be("raw"), math::tempoMicrosecondsPerQuarter); },
      [](Args a, Playback& p) {
        p.out.tempo(a.u32());
        return Effects{};
      });

  profile[0x06] = command(
      "Duration Rate", SequenceSemantic::State, [](Decode& d) { d.u8("rate"); },
      [](Args a, Playback& p) {
        p.track.durationRate = a.u8();
        return Effects{};
      });

  profile[0x07] = command(
      "Volume", SequenceSemantic::Level,
      [](Decode& d) {
        const auto raw = d.rawU8("raw");
        d.resolvedValue("linear_gain", raw, math::volumeGain(d.version(), raw.value));
      },
      [](Args a, Playback& p) {
        p.out.level(LevelScale::linearFromLinear(a.f64()), ValueQuantization{.levels = 256});
        return Effects{};
      });

  profile[0x08] = command(
      "Instrument", SequenceSemantic::Instrument,
      [](Decode& d) { d.u8("instrument", SemanticOperandRole::Instrument); },
      [](Args a, Playback& p) {
        p.out.instrument(InstrumentIdentity{
            .domain = std::string(kCapcomSnesInstrumentDomain),
            .key = a.u32(),
        });
        return Effects{};
      });

  profile[0x09] = command(
      "Octave", SequenceSemantic::State, [](Decode& d) { d.u8("octave"); },
      [](Args a, Playback& p) {
        p.track.noteOctave = a.u8();
        return Effects{};
      });

  profile[0x0a] = command(
      "Global Transpose", SequenceSemantic::Pitch, [](Decode& d) { d.s8("semitones"); },
      [](Args a, Playback& p) {
        p.out.globalTranspose(a.s8());
        return Effects{};
      });

  profile[0x0b] = command(
      "Transpose", SequenceSemantic::Pitch, [](Decode& d) { d.s8("semitones"); },
      [](Args a, Playback& p) {
        p.track.transpose = a.s8();
        return Effects{};
      });

  profile[0x0c] = command(
      "Tuning", SequenceSemantic::Pitch,
      [](Decode& d) { d.resolved("cents", d.rawS8("tuning"), math::tuningCents, SourceValueDisplay::Cents); },
      [](Args a, Playback& p) {
        p.out.tuning(a.f64());
        return Effects{};
      });

  profile[0x0d] = command(
      "Portamento Time", SequenceSemantic::Portamento,
      [](Decode& d) { d.resolved("milliseconds_per_cent", d.rawU8("time"), math::portamentoMillisecondsPerCent); },
      [](Args a, Playback& p) {
        p.track.portamentoMillisecondsPerCent = a.f64();
        return Effects{};
      });

  const auto repeatUntil = command(
      "Repeat Until", SequenceSemantic::Repeat,
      [](Decode& d) {
        // Four opcodes select four independent repeat counters. A nonzero
        // count is one less than the VM visit count; zero declares a loop.
        d.derived("slot", static_cast<u32>(d.opcode() - 0x0e + 1));
        const u8 count = d.u8("count");
        const Address destination = d.address("destination", SemanticOperandRole::RepeatTarget);
        count == 0 ? d.jumpTo(destination) : d.branchTo(destination);
      },
      [](Args a, Playback& p) {
        const u8 slot = a.u8("slot") - 1;
        const u32 count = a.u32("count");
        const Address destination = a.address("destination");
        return count == 0 ? Effects{.step = p.vm.declaredLoop(destination)}
                          : p.vm.countedRepeatUntil(slot, count + 1, destination);
      },
      CommandPlaybackStatus::AffectsControlFlow);
  for (u8 opcode = 0x0e; opcode <= 0x11; ++opcode) {
    profile[opcode] = repeatUntil;
  }

  const auto repeatBreak = command(
      "Repeat Break", SequenceSemantic::RepeatBreak,
      [](Decode& d) {
        d.derived("slot", static_cast<u32>(d.opcode() - 0x12 + 1));
        d.u8("attributes", SourceValueDisplay::Hex);
        d.branchTo(d.address("destination", SemanticOperandRole::RepeatTarget));
      },
      [](Args a, Playback& p) {
        const auto branch = p.vm.countedRepeatBreak(a.u8("slot") - 1, a.address("destination"));
        if (branch.taken) {
          p.track.applyAttributes(a.u8("attributes"), p.out);
        }
        return branch.effects;
      },
      CommandPlaybackStatus::AffectsControlFlow);
  for (u8 opcode = 0x12; opcode <= 0x15; ++opcode) {
    profile[opcode] = repeatBreak;
  }

  profile[0x16] = command(
      "Jump", SequenceSemantic::Jump,
      [](Decode& d) {
        const Address destination = d.address("destination", SemanticOperandRole::JumpTarget);
        d.jumpTo(destination);
      },
      [](Args a, Playback& p) { return Effects{.step = p.vm.loopCandidate(a.address())}; },
      CommandPlaybackStatus::AffectsControlFlow);

  profile[0x17] = command(
      "End", SequenceSemantic::End, [](Decode& d) { d.terminate(); },
      [](Args, Playback& p) { return Effects{.step = p.vm.end()}; }, CommandPlaybackStatus::StopsPlayback);

  profile[0x18] = command(
      "Pan", SequenceSemantic::Pan,
      [](Decode& d) {
        const auto raw = d.rawU8("raw");
        const auto converted = math::pan(d.version(), raw.value);
        d.resolvedValue("stereo_position", raw, converted.position);
        d.derived("linear_gain", converted.gain);
      },
      [](Args a, Playback& p) {
        p.out.pan(a.f64("stereo_position"), LevelScale::linearFromLinear(a.f64("linear_gain")));
        return Effects{};
      });

  profile[0x19] = command(
      "Master Volume", SequenceSemantic::Level,
      [](Decode& d) {
        const auto raw = d.rawU8("raw");
        d.resolvedValue("linear_gain", raw, math::volumeGain(d.version(), raw.value));
      },
      [](Args a, Playback& p) {
        p.out.masterLevel(LevelScale::linearFromLinear(a.f64()));
        return Effects{};
      });

  profile[0x1a] = command(
      "LFO", SequenceSemantic::Modulation,
      [](Decode& d) {
        d.u8("type");
        d.u8("value", SourceValueDisplay::Hex);
      },
      [](Args a, Playback& p) {
        const u8 type = a.u8("type");
        const u8 value = a.u8("value");
        switch (type) {
          case 0:  // Vibrato depth.
            p.track.vibratoDepth = math::normalizedDepth(value & 0x7f);
            p.out.modulation(ModulationPerformanceTarget::VibratoDepth,
                             p.track.modulationRate != 0 ? p.track.vibratoDepth : 0.0);
            break;
          case 1:  // Tremolo depth.
            p.track.tremoloDepth = math::tremoloDepth(p.track.version, value);
            p.out.modulation(ModulationPerformanceTarget::TremoloDepth,
                             p.track.modulationRate != 0 ? p.track.tremoloDepth : 0.0);
            break;
          case 2: {  // Shared LFO rate; zero also disables both depths.
            const bool wasEnabled = p.track.modulationRate != 0;
            p.track.modulationRate = value;
            const bool enabled = p.track.modulationRate != 0;
            if (enabled != wasEnabled) {
              p.track.emitModulationDepths(p.out, enabled);
            }
            const double rate = math::lfoRate(value);
            p.out.modulation(ModulationPerformanceTarget::VibratoRate, rate);
            p.out.modulation(ModulationPerformanceTarget::TremoloRate, rate);
            break;
          }
          default:
            break;
        }
        return Effects{};
      });

  profile[0x1b] = command(
      "Echo Param", SequenceSemantic::Meta,
      [](Decode& d) {
        d.u8("argument", SourceValueDisplay::Hex);
        d.u8("preset", SourceValueDisplay::Hex);
      },
      nullptr, CommandPlaybackStatus::SourceOnly);

  profile[0x1c] = command(
      "Echo On/Off", SequenceSemantic::Meta,
      [](Decode& d) {
        const auto raw = d.rawU8("raw");
        d.resolvedValue("enabled", raw, (raw.value & 1) != 0, SourceValueDisplay::Boolean);
      },
      [](Args a, Playback& p) {
        p.out.reverb(a.boolean() ? 40.0 / 127.0 : 0.0);
        return Effects{};
      });

  profile[0x1d] = command(
      "Release Rate", SequenceSemantic::Meta,
      [](Decode& d) {
        const auto raw = d.rawU8("raw");
        d.resolvedValue("gain", raw, static_cast<u32>(raw.value | 0xa0), SourceValueDisplay::Hex);
      },
      nullptr, CommandPlaybackStatus::SourceOnly);

  const auto noOperation =
      command("No Operation", SequenceSemantic::Meta, nullptr, nullptr, CommandPlaybackStatus::NoOp, "nop");
  profile[0x1e] = noOperation;
  profile[0x1f] = noOperation;
  return profile;
}

[[nodiscard]] OpcodeProfile makeVersion1Profile() {
  // V1 assigns operands to the two slots that later drivers treat as NOPs.
  auto profile = makeBaseProfile();
  const auto unknownOneByte = command(
      "Unknown One-Byte Event", SequenceSemantic::Meta, [](Decode& d) { d.u8("value", SourceValueDisplay::Hex); },
      nullptr, CommandPlaybackStatus::SourceOnly, "unknown-one-byte");
  profile[0x1e] = unknownOneByte;
  profile[0x1f] = unknownOneByte;
  return profile;
}

[[nodiscard]] const CommandDefinition& noteCommand() {
  // Notes and rests pack duration into the high three opcode bits. A zero key
  // in the low five bits denotes a rest; other values are notes.
  static const CommandDefinition definition = command(
      "Note", SequenceSemantic::Note,
      [](Decode& d) {
        d.opcodeValue("duration_index", static_cast<u32>(d.opcode() >> 5));
        d.opcodeValue("key_index", static_cast<u32>(d.opcode() & 0x1f));
      },
      [](Args a, Playback& p) { return p.note(a.u8("duration_index"), a.u8("key_index")); });
  return definition;
}

[[nodiscard]] const CommandDefinition& restCommand() {
  static const CommandDefinition definition = command(
      "Rest", SequenceSemantic::Rest,
      [](Decode& d) { d.opcodeValue("duration_index", static_cast<u32>(d.opcode() >> 5)); },
      [](Args a, Playback& p) { return p.rest(a.u8()); });
  return definition;
}

[[nodiscard]] const CommandDefinition& definitionFor(CapcomSnesEngineVersion version, u8 opcode) {
  if (opcode >= 0x20) {
    return (opcode & 0x1f) == 0 ? restCommand() : noteCommand();
  }

  static const OpcodeProfile base = makeBaseProfile();
  static const OpcodeProfile version1 = makeVersion1Profile();
  return version == CapcomSnesEngineVersion::v1BgmInList ? version1[opcode] : base[opcode];
}

// These are the only two lifecycle entry points. Decoding and execution both
// select the same profile entry, so adding a command never requires a second
// command-kind switch or a separately synchronized metadata table.
[[nodiscard]] DecodedBytecodeCommand decodeCommand(ByteReader reader, u32 begin, u32 end,
                                                   CapcomSnesEngineVersion version,
                                                   std::vector<Diagnostic>* diagnostics) {
  Decode decode(reader, begin, end, version, diagnostics);
  if (!decode.hasOpcode()) {
    return decode.finish(truncatedCommand().presentation);
  }

  const CommandDefinition& definition = definitionFor(version, decode.opcode());
  if (definition.decode != nullptr) {
    definition.decode(decode);
  }
  if (!decode.ok()) {
    return decode.finish(truncatedCommand().presentation);
  }
  return decode.finish(definition.presentation);
}

[[nodiscard]] std::any createTrackState(const SequenceProgram& program, const TrackProgram&) {
  return TrackState{.version = versionFrom(program)};
}

[[nodiscard]] Effects executeCommand(const SourceCommand& command, std::any&, std::any& trackStateValue,
                                     PerformanceEmitter& out, VmApi& vm) {
  auto& state = std::any_cast<TrackState&>(trackStateValue);
  Playback playback{.track = state, .out = out, .vm = vm};

  // End, unsupported, and truncated commands are all terminal at discovery
  // time, so they need no special runtime command identity.
  if (command.flow.terminal) {
    return Effects{.step = vm.end()};
  }
  const CommandDefinition& definition = definitionFor(state.version, command.opcode);
  return definition.execute != nullptr ? definition.execute(Args{command}, playback) : Effects{};
}

[[nodiscard]] SequenceDialect makeDialect() {
  return SequenceDialect{
      .id = DialectId{.value = "capcom-snes"},
      .commandDetailKindPrefix = "capcom-snes",
      .timebase = Timebase{.ppqn = kCapcomSnesPpqn},
      .defaultBehavior =
          SequenceProgramBehavior{
              .defaultLoopPolicy = LoopPolicy::PlayOnce,
              .initialReverbSend = 0.0,
              .initialMonoModeChannels = 0,
          },
      .createSemanticTrackState = createTrackState,
      .executeSemantic = executeCommand,
  };
}

}  // namespace

const SequenceDialect& capcomSnesSequenceDialect() {
  static const SequenceDialect dialect = makeDialect();
  return dialect;
}

TrackProgram decodeCapcomSnesSourceTrack(ByteReader reader, CapcomSnesEngineVersion version, u32 sourceTrackNumber,
                                         u32 startAddress, SourceMapBuilder* sourceMap,
                                         std::vector<Diagnostic>* diagnostics,
                                         std::optional<SourceAnnotationId> parentAnnotation,
                                         std::optional<AssetId> sequenceAsset) {
  const u32 end = static_cast<u32>(reader.size());
  return decodeSemanticLinearTrack(reader,
                                   TrackDecodeInput{
                                       .sequenceAsset = sequenceAsset,
                                       .trackIndex = sourceTrackNumber,
                                       .startOffset = startAddress,
                                       .parentAnnotation = parentAnnotation,
                                       .sourceMap = sourceMap,
                                       .diagnostics = diagnostics,
                                   },
                                   [reader, end, version, diagnostics](u32 offset) {
                                     return decodeCommand(reader, offset, end, version, diagnostics);
                                   });
}

SequenceProgram decodeCapcomSnesSequence(ByteReader reader, const CapcomSnesLayout& layout, AssetId sequenceId,
                                         SourceRange sequenceRange, SourceMapBuilder* sourceMap,
                                         std::vector<Diagnostic>* diagnostics) {
  SourceAnnotationId headerAnnotation;
  if (sourceMap != nullptr) {
    headerAnnotation = sourceMap->header("Sequence Header", sequenceRange)
                           .kind("capcom-snes-sequence-header")
                           .owner(ObjectRefs::sequence(sequenceId))
                           .id();
  }

  const auto& dialect = capcomSnesSequenceDialect();
  SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .config = SequenceProgramConfig{.profile = profileFor(layout.version)},
      .behavior = dialect.defaultBehavior,
  };
  const u32 pointerBase = layout.sequenceHeaderAddress + (layout.priorityInHeader ? 1 : 0);
  for (u32 pointerIndex = kCapcomSnesMaxTracks; pointerIndex-- > 0;) {
    const u32 sourceTrackNumber = kCapcomSnesMaxTracks - 1 - pointerIndex;
    const u32 pointerOffset = pointerBase + pointerIndex * 2;
    const SourceRange pointerRange = reader.range(pointerOffset, 2);
    const u16 trackAddress = reader.be16(pointerOffset);
    if (trackAddress == 0) {
      continue;
    }

    std::optional<SourceAnnotationId> pointerAnnotation;
    if (sourceMap != nullptr) {
      auto annotation = sourceMap->pointer("Track Pointer", pointerRange, SourceTarget{reader.range(trackAddress, 1)})
                            .kind("capcom-snes-track-pointer")
                            .description(fmt::format("Track starts at ${:04X}", trackAddress))
                            .derived("source_track", sourceTrackNumber)
                            .field("destination", pointerRange, trackAddress, SourceValueDisplay::Address);
      if (headerAnnotation.valid()) {
        annotation.parent(headerAnnotation);
      }
      pointerAnnotation = annotation.id();
    }

    program.tracks.push_back(decodeCapcomSnesSourceTrack(reader, layout.version, sourceTrackNumber, trackAddress,
                                                         sourceMap, diagnostics, pointerAnnotation, sequenceId));
  }
  return program;
}

}  // namespace vgmtrans::formats::capcom_snes
