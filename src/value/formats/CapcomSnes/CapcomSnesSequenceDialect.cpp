/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/CapcomSnes/CapcomSnesSequenceDialect.h"

#include "formats/CapcomSnes/CapcomSnesDriverMath.h"
#include "value/core/SequenceVm.h"
#include "value/formats/CapcomSnes/CapcomSnesValueLayout.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>

namespace vgmtrans::formats::capcom_snes {

using namespace core;

namespace {

constexpr u8 kNoteOctaveMask = 0x07;
constexpr u8 kNoteOctaveUpMask = 0x08;
constexpr u8 kNoteDottedMask = 0x10;
constexpr u8 kNoteTripletMask = 0x20;
constexpr u8 kNoteSlurredMask = 0x40;

struct Context {
  CapcomSnesEngineVersion version = CapcomSnesEngineVersion::v3BgmFixedLocation;
};

struct TrackState {
  u32 durationRate = 0xff;
  s32 transpose = 0;
  u32 noteOctave = 0;
  bool noteDotted = false;
  bool noteTriplet = false;
  bool noteSlurred = false;
  bool noteOctaveUp = false;
  u8 modulationRate = 0;
  u8 vibratoDepth = 0;
  u8 tremoloDepth = 0;
  double portamentoMillisecondsPerCent = 0.0;
  u16 lastPortamentoTime = 0;
  std::optional<s32> lastSourceKey;
  bool lastNoteSlurred = false;
  bool didRest = false;
};

[[nodiscard]] double tuningSemitones(s8 tuning) {
  return static_cast<double>(tuning) / 256.0;
}

[[nodiscard]] double tuningCents(s8 tuning) {
  return tuningSemitones(tuning) * 100.0;
}

[[nodiscard]] double portamentoMillisecondsPerCent(u8 rawTime) {
  const u8 step = static_cast<u8>((rawTime << 1) & 0xff);
  const double centsPerUpdate = step * (100.0 / 256.0);
  // The Capcom voice/portamento update runs every other 8 ms timer tick.
  return centsPerUpdate == 0.0 ? 0.0 : (0.016 / centsPerUpdate) * 1000.0;
}

[[nodiscard]] u32 baseNoteTicks(u32 rawDuration) {
  if (rawDuration == 0 || rawDuration > 7) {
    return 0;
  }
  return 192u >> (7u - rawDuration);
}

[[nodiscard]] u32 noteTicks(u32 rawDuration, TrackState& state) {
  u32 length = baseNoteTicks(rawDuration);
  if (state.noteDotted) {
    // Dotted is consumed by the next note/rest; triplet mode persists until toggled.
    length = (length % 2 == 0 && length < 0x80) ? length + (length / 2) : 0;
    state.noteDotted = false;
  } else if (state.noteTriplet) {
    length = length * 2 / 3;
  }
  return length;
}

[[nodiscard]] u32 soundingTicks(u32 length, const TrackState& state) {
  u32 duration = length * state.durationRate;
  if (state.noteSlurred || duration == 0) {
    duration = length << 8;
  }
  duration = (duration + 0x80) >> 8;
  return duration == 0 ? 1 : duration;
}

[[nodiscard]] s32 driverSourceKey(u32 keyIndex, const TrackState& state) {
  return static_cast<s32>(keyIndex) - 1 + static_cast<s32>(state.noteOctave * 12) +
         (state.noteOctaveUp ? 24 : 0);
}

[[nodiscard]] double performedKey(s32 key, const TrackState& state) {
  return static_cast<double>(key + state.transpose);
}

[[nodiscard]] u32 tempoMicrosecondsPerQuarter(u32 rawTempo) {
  if (rawTempo == 0) {
    return 60000000;
  }
  // Capcom tempo is derived from the SNES timer update rate, not stored as BPM.
  return static_cast<u32>(std::round(kCapcomSnesPpqn * (125 * 0x40) * 2 * 256.0 / rawTempo));
}

[[nodiscard]] double volumeGain(CapcomSnesEngineVersion version, u8 rawVolume) {
  return version == CapcomSnesEngineVersion::v1BgmInList ? ::capcom_snes::calculateVolumeV1(rawVolume)
                                                         : ::capcom_snes::calculateVolumeV2(rawVolume);
}

[[nodiscard]] ::capcom_snes::PanConversionResult panConversion(CapcomSnesEngineVersion version, u8 rawPan) {
  const auto biasedPan = static_cast<u8>(rawPan + 0x80);
  return version == CapcomSnesEngineVersion::v1BgmInList ? ::capcom_snes::linear8BitPanToMidi(biasedPan)
                                                         : ::capcom_snes::calculatePanV2(biasedPan);
}

[[nodiscard]] double stereoPosition(const ::capcom_snes::PanConversionResult& converted) {
  // Store the pan on the same 0..127 lattice the MIDI renderer uses so legacy
  // Capcom pan values survive the neutral stereo-position hop without shifting
  // left-side values down by one.
  return std::clamp((static_cast<double>(converted.midiPan) / 127.0) * 2.0 - 1.0, -1.0, 1.0);
}

void emitLegatoChangeIfNeeded(bool wasSlurred, const TrackState& state, Emit& out) {
  if (state.noteSlurred == wasSlurred) {
    return;
  }
  out.legatoPedal(LegatoPedalPerformanceEvent{
      .enabled = state.noteSlurred,
  });
}

void applyNoteAttributes(u8 attributes, TrackState& state, Emit* out = nullptr) {
  const bool wasSlurred = state.noteSlurred;
  // The driver ORs octave bits instead of replacing them. Preserve that quirk until
  // parity proves a specific version behaves differently.
  state.noteOctave |= attributes & kNoteOctaveMask;
  state.noteDotted = state.noteDotted || ((attributes & kNoteDottedMask) != 0);
  state.noteOctaveUp = (attributes & kNoteOctaveUpMask) != 0;
  state.noteTriplet = (attributes & kNoteTripletMask) != 0;
  state.noteSlurred = (attributes & kNoteSlurredMask) != 0;
  if (out != nullptr) {
    emitLegatoChangeIfNeeded(wasSlurred, state, *out);
  }
}

void emitModulationDepths(const TrackState& state, Emit& out, bool enabled) {
  if (state.vibratoDepth != 0) {
    out.modulation(ModulationPerformanceEvent{
        .target = ModulationPerformanceTarget::VibratoDepth,
        .amount = enabled ? static_cast<double>(state.vibratoDepth) / 127.0 : 0.0,
    });
  }
  if (state.tremoloDepth != 0) {
    out.modulation(ModulationPerformanceEvent{
        .target = ModulationPerformanceTarget::TremoloDepth,
        .amount = enabled ? static_cast<double>(state.tremoloDepth) / 127.0 : 0.0,
    });
  }
}

struct Rest {
  u8 rawDuration = 0;

  static constexpr std::string_view kind = "capcom-snes.rest";
  static constexpr std::string_view name = "Rest";

  static Rest parse(CommandReader& in) {
    return Rest{.rawDuration = static_cast<u8>(in.opcode() >> 5)};
  }

  void describe(CommandInfo& out) const {
    out.field("duration_index", static_cast<u64>(rawDuration));
  }

  Effects execute(TrackState& state, Emit&, VmApi&, const Context&) const {
    const u32 length = noteTicks(rawDuration, state);
    state.didRest = true;
    return Effects::wait(length);
  }
};

struct Note {
  u8 keyIndex = 0;
  u8 rawDuration = 0;

  static constexpr std::string_view kind = "capcom-snes.note";
  static constexpr std::string_view name = "Note";

  static Note parse(CommandReader& in) {
    return Note{
        .keyIndex = static_cast<u8>(in.opcode() & 0x1f),
        .rawDuration = static_cast<u8>(in.opcode() >> 5),
    };
  }

  void describe(CommandInfo& out) const {
    out.field("key_index", static_cast<u64>(keyIndex));
    out.field("duration_index", static_cast<u64>(rawDuration));
  }

  Effects execute(TrackState& state, Emit& out, VmApi&, const Context&) const {
    const u32 length = noteTicks(rawDuration, state);
    const s32 key = driverSourceKey(keyIndex, state);
    const u32 duration = soundingTicks(length, state);

    if (state.lastNoteSlurred && state.lastSourceKey && key == *state.lastSourceKey && !state.didRest) {
      // The Capcom driver treats repeated keys after a slurred note as a tie.
      // Legacy VGMTrans extends the previous MIDI note even if this note has
      // already cleared the slur bit, so the state must look at the previous note.
      out.note(NotePerformanceEvent{
          .key = performedKey(key, state),
          .velocity = 1.0,
          .durationTicks = duration,
          .extendsPrevious = true,
      });
      state.lastNoteSlurred = state.noteSlurred;
      return Effects::wait(length);
    }

    if (state.portamentoMillisecondsPerCent > 0.0 && state.lastSourceKey) {
      const auto keyDistance = static_cast<u32>(std::abs(key - *state.lastSourceKey));
      const auto portamentoTime = static_cast<u16>(keyDistance * 100 * state.portamentoMillisecondsPerCent);
      if (portamentoTime != state.lastPortamentoTime) {
        out.portamento(PortamentoPerformanceEvent{
            .timeMilliseconds = static_cast<double>(portamentoTime),
            .previousKey = static_cast<double>(*state.lastSourceKey + state.transpose),
        });
        state.lastPortamentoTime = portamentoTime;
      } else {
        out.portamentoControl(PortamentoControlPerformanceEvent{
            .previousKey = static_cast<double>(*state.lastSourceKey + state.transpose),
        });
      }
    }
    out.note(NotePerformanceEvent{
        .key = performedKey(key, state),
        .velocity = 1.0,
        // Slur is modeled as a one-tick overlap into the next source note.
        .durationTicks = duration + (state.noteSlurred ? 1u : 0u),
    });
    state.lastSourceKey = key;
    state.didRest = false;
    state.lastNoteSlurred = state.noteSlurred;
    return Effects::wait(length);
  }
};

struct NoteAttributes {
  u8 raw = 0;

  static constexpr std::string_view kind = "capcom-snes.note-attributes";
  static constexpr std::string_view name = "Note Attributes";

  static NoteAttributes parse(CommandReader& in) {
    return NoteAttributes{.raw = in.u8("attributes")};
  }

  void describe(CommandInfo& out) const {
    out.field("raw", static_cast<u64>(raw));
  }

  Effects execute(TrackState& state, Emit& out, VmApi&, const Context&) const {
    applyNoteAttributes(raw, state, &out);
    return Effects::none();
  }
};

struct Octave {
  u8 raw = 0;

  static constexpr std::string_view kind = "capcom-snes.octave";
  static constexpr std::string_view name = "Octave";

  static Octave parse(CommandReader& in) {
    return Octave{.raw = in.u8("octave")};
  }

  void describe(CommandInfo& out) const {
    out.field("octave", static_cast<u64>(raw));
  }

  Effects execute(TrackState& state, Emit&, VmApi&, const Context&) const {
    state.noteOctave = raw;
    return Effects::none();
  }
};

struct ToggleTriplet {
  static constexpr std::string_view kind = "capcom-snes.toggle-triplet";
  static constexpr std::string_view name = "Toggle Triplet";

  static ToggleTriplet parse(CommandReader&) {
    return ToggleTriplet{};
  }

  Effects execute(TrackState& state, Emit&, VmApi&, const Context&) const {
    state.noteTriplet = !state.noteTriplet;
    return Effects::none();
  }
};

struct ToggleSlur {
  static constexpr std::string_view kind = "capcom-snes.toggle-slur";
  static constexpr std::string_view name = "Toggle Slur";

  static ToggleSlur parse(CommandReader&) {
    return ToggleSlur{};
  }

  Effects execute(TrackState& state, Emit& out, VmApi&, const Context&) const {
    const bool wasSlurred = state.noteSlurred;
    state.noteSlurred = !state.noteSlurred;
    emitLegatoChangeIfNeeded(wasSlurred, state, out);
    return Effects::none();
  }
};

struct DottedNote {
  static constexpr std::string_view kind = "capcom-snes.dotted-note";
  static constexpr std::string_view name = "Dotted Note";

  static DottedNote parse(CommandReader&) {
    return DottedNote{};
  }

  Effects execute(TrackState& state, Emit&, VmApi&, const Context&) const {
    state.noteDotted = true;
    return Effects::none();
  }
};

struct ToggleOctaveUp {
  static constexpr std::string_view kind = "capcom-snes.toggle-octave-up";
  static constexpr std::string_view name = "Toggle Octave Up";

  static ToggleOctaveUp parse(CommandReader&) {
    return ToggleOctaveUp{};
  }

  Effects execute(TrackState& state, Emit&, VmApi&, const Context&) const {
    state.noteOctaveUp = !state.noteOctaveUp;
    return Effects::none();
  }
};

struct GlobalTranspose {
  s8 semitones = 0;

  static constexpr std::string_view kind = "capcom-snes.global-transpose";
  static constexpr std::string_view name = "Global Transpose";

  static GlobalTranspose parse(CommandReader& in) {
    return GlobalTranspose{.semitones = in.s8("semitones")};
  }

  void describe(CommandInfo& out) const {
    out.field("semitones", static_cast<s64>(semitones));
  }

  Effects execute(TrackState&, Emit& out, VmApi&, const Context&) const {
    out.globalTranspose(GlobalTransposePerformanceEvent{
        .semitones = semitones,
    });
    return Effects::none();
  }
};

struct Transpose {
  s8 semitones = 0;

  static constexpr std::string_view kind = "capcom-snes.transpose";
  static constexpr std::string_view name = "Transpose";

  static Transpose parse(CommandReader& in) {
    return Transpose{.semitones = in.s8("semitones")};
  }

  void describe(CommandInfo& out) const {
    out.field("semitones", static_cast<s64>(semitones));
  }

  Effects execute(TrackState& state, Emit&, VmApi&, const Context&) const {
    state.transpose = semitones;
    return Effects::none();
  }
};

struct Tuning {
  s8 raw = 0;

  static constexpr std::string_view kind = "capcom-snes.tuning";
  static constexpr std::string_view name = "Tuning";

  static Tuning parse(CommandReader& in) {
    return Tuning{.raw = in.s8("tuning")};
  }

  void describe(CommandInfo& out) const {
    out.field("raw", static_cast<s64>(raw));
    out.field("cents", std::to_string(tuningCents(raw)));
  }

  Effects execute(TrackState&, Emit& out, VmApi&, const Context&) const {
    out.tuning(TuningPerformanceEvent{
        .cents = tuningCents(raw),
    });
    return Effects::none();
  }
};

struct PortamentoTime {
  u8 raw = 0;

  static constexpr std::string_view kind = "capcom-snes.portamento-time";
  static constexpr std::string_view name = "Portamento Time";

  static PortamentoTime parse(CommandReader& in) {
    return PortamentoTime{.raw = in.u8("time")};
  }

  void describe(CommandInfo& out) const {
    out.field("raw", static_cast<u64>(raw));
  }

  Effects execute(TrackState& state, Emit&, VmApi&, const Context&) const {
    // The driver stores portamento as speed; the next note converts it to a
    // distance-dependent time using the previous source key.
    state.portamentoMillisecondsPerCent = portamentoMillisecondsPerCent(raw);
    return Effects::none();
  }
};

struct Tempo {
  u16 raw = 0;

  static constexpr std::string_view kind = "capcom-snes.tempo";
  static constexpr std::string_view name = "Tempo";

  static Tempo parse(CommandReader& in) {
    return Tempo{.raw = in.be16("tempo")};
  }

  void describe(CommandInfo& out) const {
    out.field("raw", static_cast<u64>(raw));
    out.field("microseconds_per_quarter", static_cast<u64>(tempoMicrosecondsPerQuarter(raw)));
  }

  Effects execute(TrackState&, Emit& out, VmApi&, const Context&) const {
    out.tempo(TempoPerformanceEvent{
        .microsecondsPerQuarter = tempoMicrosecondsPerQuarter(raw),
    });
    return Effects::none();
  }
};

struct DurationRate {
  u8 raw = 0;

  static constexpr std::string_view kind = "capcom-snes.duration-rate";
  static constexpr std::string_view name = "Duration Rate";

  static DurationRate parse(CommandReader& in) {
    return DurationRate{.raw = in.u8("rate")};
  }

  void describe(CommandInfo& out) const {
    out.field("raw", static_cast<u64>(raw));
  }

  Effects execute(TrackState& state, Emit&, VmApi&, const Context&) const {
    state.durationRate = raw;
    return Effects::none();
  }
};

struct RepeatUntil {
  u8 slot = 0;
  u8 count = 0;
  Address destination;

  static constexpr std::string_view kind = "capcom-snes.repeat-until";
  static constexpr std::string_view name = "Repeat Until";

  static RepeatUntil parse(CommandReader& in) {
    return RepeatUntil{
        .slot = static_cast<u8>(in.opcode() - 0x0e),
        .count = in.u8("count"),
        .destination = in.be16Address("destination"),
    };
  }

  void describe(CommandInfo& out) const {
    out.field("slot", static_cast<u64>(slot + 1));
    out.field("count", static_cast<u64>(count));
    out.field("destination", destination);
  }

  Effects execute(TrackState&, Emit&, VmApi& vm, const Context&) const {
    if (count == 0) {
      return Effects{.step = vm.jump(destination)};
    }

    // Capcom stores the number of replays. The VM helper receives total plays.
    return Effects{.step = vm.repeatUntil(slot, static_cast<u32>(count) + 1, destination)};
  }
};

struct RepeatBreak {
  u8 slot = 0;
  u8 attributes = 0;
  Address destination;

  static constexpr std::string_view kind = "capcom-snes.repeat-break";
  static constexpr std::string_view name = "Repeat Break";

  static RepeatBreak parse(CommandReader& in) {
    return RepeatBreak{
        .slot = static_cast<u8>(in.opcode() - 0x12),
        .attributes = in.u8("attributes"),
        .destination = in.be16Address("destination"),
    };
  }

  void describe(CommandInfo& out) const {
    out.field("slot", static_cast<u64>(slot + 1));
    out.field("attributes", static_cast<u64>(attributes));
    out.field("destination", destination);
  }

  Effects execute(TrackState& state, Emit& out, VmApi& vm, const Context&) const {
    const Step step = vm.repeatBreak(slot, destination);
    if (step.kind == Step::Kind::Jump) {
      applyNoteAttributes(attributes, state, &out);
    }
    return Effects{.step = step};
  }
};

struct Volume {
  u8 raw = 0;

  static constexpr std::string_view kind = "capcom-snes.volume";
  static constexpr std::string_view name = "Volume";

  static Volume parse(CommandReader& in) {
    return Volume{.raw = in.u8("volume")};
  }

  void describe(CommandInfo& out, const Context& context) const {
    out.field("raw", static_cast<u64>(raw));
    out.field("linear_gain", std::to_string(volumeGain(context.version, raw)));
  }

  Effects execute(TrackState&, Emit& out, VmApi&, const Context& context) const {
    out.level(LevelPerformanceEvent{
        .linearGain = volumeGain(context.version, raw),
        .resolution = LevelResolution::FourteenBit,
    });
    return Effects::none();
  }
};

struct Program {
  u8 raw = 0;

  static constexpr std::string_view kind = "capcom-snes.program";
  static constexpr std::string_view name = "Program";

  static Program parse(CommandReader& in) {
    return Program{.raw = in.u8("program")};
  }

  void describe(CommandInfo& out) const {
    out.field("bank", static_cast<u64>(raw >> 7));
    out.field("program", static_cast<u64>(raw & 0x7f));
  }

  Effects execute(TrackState&, Emit& out, VmApi&, const Context&) const {
    out.instrument(InstrumentPerformanceEvent{
        .bank = static_cast<u32>(raw >> 7),
        .program = static_cast<u32>(raw & 0x7f),
        .forceBankSelect = true,
    });
    return Effects::none();
  }
};

struct Jump {
  Address destination;

  static constexpr std::string_view kind = "capcom-snes.jump";
  static constexpr std::string_view name = "Jump";

  static Jump parse(CommandReader& in) {
    return Jump{.destination = in.be16Address("destination")};
  }

  void describe(CommandInfo& out) const {
    out.field("destination", destination);
  }

  Effects execute(TrackState&, Emit&, VmApi& vm, const Context&) const {
    return Effects{.step = vm.jump(destination)};
  }
};

struct End {
  static constexpr std::string_view kind = "capcom-snes.end";
  static constexpr std::string_view name = "End";

  static End parse(CommandReader&) {
    return End{};
  }

  Effects execute(TrackState&, Emit&, VmApi& vm, const Context&) const {
    return Effects{.step = vm.end()};
  }
};

struct Pan {
  u8 raw = 0;

  static constexpr std::string_view kind = "capcom-snes.pan";
  static constexpr std::string_view name = "Pan";

  static Pan parse(CommandReader& in) {
    return Pan{.raw = in.u8("pan")};
  }

  void describe(CommandInfo& out, const Context& context) const {
    const auto pan = panConversion(context.version, raw);
    out.field("raw", static_cast<u64>(raw));
    out.field("stereo_position", std::to_string(stereoPosition(pan)));
    out.field("linear_gain", std::to_string(pan.volumeScale));
  }

  Effects execute(TrackState&, Emit& out, VmApi&, const Context& context) const {
    const auto pan = panConversion(context.version, raw);
    out.pan(PanPerformanceEvent{
        .stereoPosition = stereoPosition(pan),
        .linearGain = pan.volumeScale,
    });
    return Effects::none();
  }
};

struct MasterVolume {
  u8 raw = 0;

  static constexpr std::string_view kind = "capcom-snes.master-volume";
  static constexpr std::string_view name = "Master Volume";

  static MasterVolume parse(CommandReader& in) {
    return MasterVolume{.raw = in.u8("volume")};
  }

  void describe(CommandInfo& out, const Context& context) const {
    out.field("raw", static_cast<u64>(raw));
    out.field("linear_gain", std::to_string(volumeGain(context.version, raw)));
  }

  Effects execute(TrackState&, Emit& out, VmApi&, const Context& context) const {
    out.masterLevel(MasterLevelPerformanceEvent{
        .linearGain = volumeGain(context.version, raw),
    });
    return Effects::none();
  }
};

struct Lfo {
  u8 type = 0;
  u8 value = 0;

  static constexpr std::string_view kind = "capcom-snes.lfo";
  static constexpr std::string_view name = "LFO";

  static Lfo parse(CommandReader& in) {
    return Lfo{
        .type = in.u8("type"),
        .value = in.u8("value"),
    };
  }

  void describe(CommandInfo& out) const {
    out.field("type", static_cast<u64>(type));
    out.field("value", static_cast<u64>(value));
  }

  Effects execute(TrackState& state, Emit& out, VmApi&, const Context& context) const {
    switch (type) {
      case 0:
        state.vibratoDepth = value & 0x7f;
        out.modulation(ModulationPerformanceEvent{
            .target = ModulationPerformanceTarget::VibratoDepth,
            .amount = state.modulationRate != 0 ? static_cast<double>(state.vibratoDepth) / 127.0 : 0.0,
        });
        break;

      case 1:
        state.tremoloDepth =
            ::capcom_snes::tremoloDepthToMidiValue(value, context.version == CapcomSnesEngineVersion::v1BgmInList);
        out.modulation(ModulationPerformanceEvent{
            .target = ModulationPerformanceTarget::TremoloDepth,
            .amount = state.modulationRate != 0 ? static_cast<double>(state.tremoloDepth) / 127.0 : 0.0,
        });
        break;

      case 2: {
        const bool wasEnabled = state.modulationRate != 0;
        state.modulationRate = value;
        const bool isEnabled = state.modulationRate != 0;
        if (!isEnabled && wasEnabled) {
          emitModulationDepths(state, out, false);
        } else if (isEnabled && !wasEnabled) {
          emitModulationDepths(state, out, true);
        }

        const double rate = static_cast<double>(::capcom_snes::lfoRateByteToMidiValue(value)) / 127.0;
        out.modulation(ModulationPerformanceEvent{
            .target = ModulationPerformanceTarget::VibratoRate,
            .amount = rate,
        });
        out.modulation(ModulationPerformanceEvent{
            .target = ModulationPerformanceTarget::TremoloRate,
            .amount = rate,
        });
        break;
      }

      default:
        // Type 3 is the driver's reset-LFO-phase flag. SF2/DLS always reset phase
        // on note activation, so there is no target-neutral performance event yet.
        break;
    }
    return Effects::none();
  }
};

struct EchoParam {
  u8 argument = 0;
  u8 preset = 0;

  static constexpr std::string_view kind = "capcom-snes.echo-param";
  static constexpr std::string_view name = "Echo Param";

  static EchoParam parse(CommandReader& in) {
    return EchoParam{
        .argument = in.u8("argument"),
        .preset = in.u8("preset"),
    };
  }

  void describe(CommandInfo& out) const {
    out.field("argument", static_cast<u64>(argument));
    out.field("preset", static_cast<u64>(preset));
  }

  Effects execute(TrackState&, Emit&, VmApi&, const Context&) const {
    // Reverb/echo is preserved as source state until the performance model has
    // target-neutral reverb events.
    return Effects::none();
  }
};

struct EchoOnOff {
  u8 raw = 0;

  static constexpr std::string_view kind = "capcom-snes.echo-on-off";
  static constexpr std::string_view name = "Echo On/Off";

  static EchoOnOff parse(CommandReader& in) {
    return EchoOnOff{.raw = in.u8("enabled")};
  }

  void describe(CommandInfo& out) const {
    out.field("enabled", static_cast<u64>(raw & 1));
  }

  Effects execute(TrackState&, Emit& out, VmApi&, const Context&) const {
    out.reverb(ReverbPerformanceEvent{
        .send = (raw & 1) != 0 ? 40.0 / 127.0 : 0.0,
    });
    return Effects::none();
  }
};

struct ReleaseRate {
  u8 raw = 0;

  static constexpr std::string_view kind = "capcom-snes.release-rate";
  static constexpr std::string_view name = "Release Rate";

  static ReleaseRate parse(CommandReader& in) {
    return ReleaseRate{.raw = in.u8("rate")};
  }

  void describe(CommandInfo& out) const {
    out.field("raw", static_cast<u64>(raw));
    out.field("gain", static_cast<u64>(raw | 0xa0));
  }

  Effects execute(TrackState&, Emit&, VmApi&, const Context&) const {
    return Effects::none();
  }
};

struct Nop {
  static constexpr std::string_view kind = "capcom-snes.nop";
  static constexpr std::string_view name = "No Operation";

  static Nop parse(CommandReader&) {
    return Nop{};
  }

  Effects execute(TrackState&, Emit&, VmApi&, const Context&) const {
    return Effects::none();
  }
};

struct UnknownOneByte {
  u8 opcode = 0;
  u8 value = 0;

  static constexpr std::string_view kind = "capcom-snes.unknown-one-byte";
  static constexpr std::string_view name = "Unknown One-Byte Event";

  static UnknownOneByte parse(CommandReader& in) {
    return UnknownOneByte{
        .opcode = in.opcode(),
        .value = in.u8("value"),
    };
  }

  void describe(CommandInfo& out) const {
    out.field("opcode", static_cast<u64>(opcode));
    out.field("value", static_cast<u64>(value));
  }

  Effects execute(TrackState&, Emit&, VmApi&, const Context&) const {
    // V1 maps opcodes $1E/$1F to unknown one-byte events and then keeps reading.
    return Effects::none();
  }
};

struct UnknownOpcode {
  u8 opcode = 0;

  static constexpr std::string_view kind = "capcom-snes.unknown";
  static constexpr std::string_view name = "Unknown Opcode";

  static UnknownOpcode parse(CommandReader& in) {
    return UnknownOpcode{.opcode = in.opcode()};
  }

  void describe(CommandInfo& out) const {
    out.field("opcode", static_cast<u64>(opcode));
  }

  Effects execute(TrackState&, Emit&, VmApi& vm, const Context&) const {
    vm.diagnostic(Diagnostic{
        .severity = Severity::Warning,
        .message = "Unknown Capcom SNES sequence opcode",
    });
    return Effects{.step = vm.end()};
  }
};

template <class Command>
void appendCommand(TrackProgramBuilder& builder, const SequenceDialect& dialect, ByteReader reader, u32 beginOffset,
                   u32 size) {
  const auto* handler = dialect.handlerForKind(Command::kind);
  if (handler == nullptr) {
    throw std::logic_error("Capcom SNES sequence command was not registered in its dialect");
  }
  builder.add<Command>(handler->id, handler->kind, Address{beginOffset}, reader.range(beginOffset, size),
                       reader.slice(beginOffset, size));
}

template <class Command>
bool appendCommandIfPresent(TrackProgramBuilder& builder, const SequenceDialect& dialect, ByteReader reader,
                            u32 beginOffset, u32 size) {
  if (!reader.has(beginOffset, size)) {
    appendCommand<UnknownOpcode>(builder, dialect, reader, beginOffset, 1);
    return false;
  }
  appendCommand<Command>(builder, dialect, reader, beginOffset, size);
  return true;
}

[[nodiscard]] std::string dialectId(CapcomSnesEngineVersion version) {
  switch (version) {
    case CapcomSnesEngineVersion::v1BgmInList:
      return "capcom-snes:v1";
    case CapcomSnesEngineVersion::v2BgmUsuallyAtFixedLocation:
      return "capcom-snes:v2";
    case CapcomSnesEngineVersion::v3BgmFixedLocation:
      return "capcom-snes:v3";
    case CapcomSnesEngineVersion::none:
      return "capcom-snes";
  }
  return "capcom-snes";
}

}  // namespace

SequenceDialect capcomSnesSequenceDialect(CapcomSnesEngineVersion version) {
  return SequenceDialectBuilder<TrackState, Context>(dialectId(version), Context{.version = version})
      .timebase(Timebase{.ppqn = kCapcomSnesPpqn})
      .defaultBehavior(SequenceProgramBehavior{
          .defaultLoopPolicy = LoopPolicy::PlayOnce,
          .initialReverbSend = 0.0,
          .initialMonoModeChannels = 0,
          .stopAllTracksAtFirstLoop = true,
      })
      .commands<Rest, Note, ToggleTriplet, ToggleSlur, DottedNote, ToggleOctaveUp, NoteAttributes, Octave,
                GlobalTranspose, Transpose, Tuning, PortamentoTime, Tempo, DurationRate, Volume, Program, RepeatUntil,
                RepeatBreak, Jump, End, Pan, MasterVolume, Lfo, EchoParam, EchoOnOff, ReleaseRate, Nop, UnknownOneByte,
                UnknownOpcode>();
}

void registerCapcomSnesSequenceDialects(SequenceDialectRegistry& registry) {
  registry.add(capcomSnesSequenceDialect(CapcomSnesEngineVersion::none));
  registry.add(capcomSnesSequenceDialect(CapcomSnesEngineVersion::v1BgmInList));
  registry.add(capcomSnesSequenceDialect(CapcomSnesEngineVersion::v2BgmUsuallyAtFixedLocation));
  registry.add(capcomSnesSequenceDialect(CapcomSnesEngineVersion::v3BgmFixedLocation));
}

TrackProgram decodeCapcomSnesSourceTrack(ByteReader reader, const SequenceDialect& dialect, u32 sourceTrackNumber,
                                         u32 startAddress) {
  TrackProgram track{
      .id = TrackId{sourceTrackNumber},
      .sourceTrackNumber = sourceTrackNumber,
      .startAddress = Address{startAddress},
  };
  TrackProgramBuilder builder{track};

  std::set<u32> visitedOffsets;
  u32 offset = startAddress;
  while (reader.has(offset, 1) && track.commands.size() < 4096) {
    if (!visitedOffsets.insert(offset).second) {
      break;
    }

    const u32 beginOffset = offset;
    const u8 opcode = reader.u8At(offset++);
    if (opcode >= 0x20) {
      if ((opcode & 0x1f) == 0) {
        appendCommand<Rest>(builder, dialect, reader, beginOffset, 1);
      } else {
        appendCommand<Note>(builder, dialect, reader, beginOffset, 1);
      }
      continue;
    }

    switch (opcode) {
      case 0x00:
        appendCommand<ToggleTriplet>(builder, dialect, reader, beginOffset, 1);
        break;

      case 0x01:
        appendCommand<ToggleSlur>(builder, dialect, reader, beginOffset, 1);
        break;

      case 0x02:
        appendCommand<DottedNote>(builder, dialect, reader, beginOffset, 1);
        break;

      case 0x03:
        appendCommand<ToggleOctaveUp>(builder, dialect, reader, beginOffset, 1);
        break;

      case 0x04:
        if (!appendCommandIfPresent<NoteAttributes>(builder, dialect, reader, beginOffset, 2)) {
          return track;
        }
        offset = beginOffset + 2;
        break;

      case 0x05:
        if (!appendCommandIfPresent<Tempo>(builder, dialect, reader, beginOffset, 3)) {
          return track;
        }
        offset = beginOffset + 3;
        break;

      case 0x06:
        if (!appendCommandIfPresent<DurationRate>(builder, dialect, reader, beginOffset, 2)) {
          return track;
        }
        offset = beginOffset + 2;
        break;

      case 0x07:
        if (!appendCommandIfPresent<Volume>(builder, dialect, reader, beginOffset, 2)) {
          return track;
        }
        offset = beginOffset + 2;
        break;

      case 0x08:
        if (!appendCommandIfPresent<Program>(builder, dialect, reader, beginOffset, 2)) {
          return track;
        }
        offset = beginOffset + 2;
        break;

      case 0x09:
        if (!appendCommandIfPresent<Octave>(builder, dialect, reader, beginOffset, 2)) {
          return track;
        }
        offset = beginOffset + 2;
        break;

      case 0x0a:
        if (!appendCommandIfPresent<GlobalTranspose>(builder, dialect, reader, beginOffset, 2)) {
          return track;
        }
        offset = beginOffset + 2;
        break;

      case 0x0b:
        if (!appendCommandIfPresent<Transpose>(builder, dialect, reader, beginOffset, 2)) {
          return track;
        }
        offset = beginOffset + 2;
        break;

      case 0x0c:
        if (!appendCommandIfPresent<Tuning>(builder, dialect, reader, beginOffset, 2)) {
          return track;
        }
        offset = beginOffset + 2;
        break;

      case 0x0d:
        if (!appendCommandIfPresent<PortamentoTime>(builder, dialect, reader, beginOffset, 2)) {
          return track;
        }
        offset = beginOffset + 2;
        break;

      case 0x0e:
      case 0x0f:
      case 0x10:
      case 0x11:
        if (!appendCommandIfPresent<RepeatUntil>(builder, dialect, reader, beginOffset, 4)) {
          return track;
        }
        offset = beginOffset + 4;
        break;

      case 0x12:
      case 0x13:
      case 0x14:
      case 0x15:
        if (!appendCommandIfPresent<RepeatBreak>(builder, dialect, reader, beginOffset, 4)) {
          return track;
        }
        offset = beginOffset + 4;
        break;

      case 0x16:
        if (!appendCommandIfPresent<Jump>(builder, dialect, reader, beginOffset, 3)) {
          return track;
        }
        offset = reader.be16(beginOffset + 1);
        break;

      case 0x17:
        appendCommand<End>(builder, dialect, reader, beginOffset, 1);
        return track;

      case 0x18:
        if (!appendCommandIfPresent<Pan>(builder, dialect, reader, beginOffset, 2)) {
          return track;
        }
        offset = beginOffset + 2;
        break;

      case 0x19:
        if (!appendCommandIfPresent<MasterVolume>(builder, dialect, reader, beginOffset, 2)) {
          return track;
        }
        offset = beginOffset + 2;
        break;

      case 0x1a:
        if (!appendCommandIfPresent<Lfo>(builder, dialect, reader, beginOffset, 3)) {
          return track;
        }
        offset = beginOffset + 3;
        break;

      case 0x1b:
        if (!appendCommandIfPresent<EchoParam>(builder, dialect, reader, beginOffset, 3)) {
          return track;
        }
        offset = beginOffset + 3;
        break;

      case 0x1c:
        if (!appendCommandIfPresent<EchoOnOff>(builder, dialect, reader, beginOffset, 2)) {
          return track;
        }
        offset = beginOffset + 2;
        break;

      case 0x1d:
        if (!appendCommandIfPresent<ReleaseRate>(builder, dialect, reader, beginOffset, 2)) {
          return track;
        }
        offset = beginOffset + 2;
        break;

      case 0x1e:
      case 0x1f:
        if (dialect.id.value == "capcom-snes:v1") {
          if (!appendCommandIfPresent<UnknownOneByte>(builder, dialect, reader, beginOffset, 2)) {
            return track;
          }
          offset = beginOffset + 2;
        } else {
          appendCommand<Nop>(builder, dialect, reader, beginOffset, 1);
        }
        break;

      default:
        appendCommand<UnknownOpcode>(builder, dialect, reader, beginOffset, 1);
        return track;
    }
  }

  return track;
}

}  // namespace vgmtrans::formats::capcom_snes
