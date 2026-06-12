/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/CapcomSnes/CapcomSnesSequenceDialect.h"

#include "formats/CapcomSnes/CapcomSnesDriverMath.h"
#include "value/core/BytecodeSequenceDecoder.h"
#include "value/core/SequenceVm.h"
#include "value/formats/CapcomSnes/CapcomSnesValueLayout.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace vgmtrans::formats::capcom_snes {

using namespace core;

namespace {

#define CAPCOM_KIND(Suffix, DisplayName)                          \
  static constexpr std::string_view kind = "capcom-snes." Suffix; \
  static constexpr std::string_view name = DisplayName

#define CAPCOM_COMMAND(Op, Suffix, DisplayName) \
  static constexpr u8 opcode = Op;              \
  CAPCOM_KIND(Suffix, DisplayName)

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

using Runtime = CommandRuntime<TrackState, Context>;

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
  return static_cast<s32>(keyIndex) - 1 + static_cast<s32>(state.noteOctave * 12) + (state.noteOctaveUp ? 24 : 0);
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
  out.legatoPedal(state.noteSlurred);
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
    out.modulation(ModulationPerformanceTarget::VibratoDepth,
                   enabled ? static_cast<double>(state.vibratoDepth) / 127.0 : 0.0);
  }
  if (state.tremoloDepth != 0) {
    out.modulation(ModulationPerformanceTarget::TremoloDepth,
                   enabled ? static_cast<double>(state.tremoloDepth) / 127.0 : 0.0);
  }
}

struct Rest {
  u8 rawDuration = 0;

  CAPCOM_KIND("rest", "Rest");

  static Rest parse(CommandReader& in) {
    const auto duration = static_cast<u8>(in.opcode() >> 5);
    in.derived("duration_index", static_cast<u64>(duration));
    return Rest{.rawDuration = duration};
  }

  Effects execute(Runtime& rt) const {
    const u32 length = noteTicks(rawDuration, rt.state);
    rt.state.didRest = true;
    return rt.wait(length);
  }
};

struct Note {
  u8 keyIndex = 0;
  u8 rawDuration = 0;

  CAPCOM_KIND("note", "Note");

  static Note parse(CommandReader& in) {
    const auto keyIndex = static_cast<u8>(in.opcode() & 0x1f);
    const auto duration = static_cast<u8>(in.opcode() >> 5);
    in.derived("key_index", static_cast<u64>(keyIndex));
    in.derived("duration_index", static_cast<u64>(duration));
    return Note{
        .keyIndex = keyIndex,
        .rawDuration = duration,
    };
  }

  Effects execute(Runtime& rt) const {
    auto& state = rt.state;
    const u32 length = noteTicks(rawDuration, state);
    const s32 key = driverSourceKey(keyIndex, state);
    const u32 duration = soundingTicks(length, state);

    if (state.lastNoteSlurred && state.lastSourceKey && key == *state.lastSourceKey && !state.didRest) {
      // The Capcom driver treats repeated keys after a slurred note as a tie.
      // Legacy VGMTrans extends the previous MIDI note even if this note has
      // already cleared the slur bit, so the state must look at the previous note.
      rt.out.note(performedKey(key, state), 1.0, duration, true);
      state.lastNoteSlurred = state.noteSlurred;
      return rt.wait(length);
    }

    if (state.portamentoMillisecondsPerCent > 0.0 && state.lastSourceKey) {
      const auto keyDistance = static_cast<u32>(std::abs(key - *state.lastSourceKey));
      const auto portamentoTime = static_cast<u16>(keyDistance * 100 * state.portamentoMillisecondsPerCent);
      if (portamentoTime != state.lastPortamentoTime) {
        rt.out.portamento(static_cast<double>(portamentoTime),
                          static_cast<double>(*state.lastSourceKey + state.transpose));
        state.lastPortamentoTime = portamentoTime;
      } else {
        rt.out.portamentoControl(static_cast<double>(*state.lastSourceKey + state.transpose));
      }
    }
    // Slur is modeled as a one-tick overlap into the next source note.
    rt.out.note(performedKey(key, state), 1.0, duration + (state.noteSlurred ? 1u : 0u));
    state.lastSourceKey = key;
    state.didRest = false;
    state.lastNoteSlurred = state.noteSlurred;
    return rt.wait(length);
  }
};

struct NoteAttributes : U8Operand<NoteAttributes> {
  CAPCOM_COMMAND(0x04, "note-attributes", "Note Attributes");
  static constexpr std::string_view operandName = "raw";

  void execute(Runtime& rt) const { applyNoteAttributes(raw, rt.state, &rt.out); }
};

struct Octave : U8Operand<Octave> {
  CAPCOM_COMMAND(0x09, "octave", "Octave");
  static constexpr std::string_view operandName = "octave";

  void execute(Runtime& rt) const { rt.state.noteOctave = raw; }
};

struct ToggleTriplet : NoOperands<ToggleTriplet> {
  CAPCOM_COMMAND(0x00, "toggle-triplet", "Toggle Triplet");

  void execute(Runtime& rt) const { rt.state.noteTriplet = !rt.state.noteTriplet; }
};

struct ToggleSlur : NoOperands<ToggleSlur> {
  CAPCOM_COMMAND(0x01, "toggle-slur", "Toggle Slur");

  void execute(Runtime& rt) const {
    const bool wasSlurred = rt.state.noteSlurred;
    rt.state.noteSlurred = !rt.state.noteSlurred;
    emitLegatoChangeIfNeeded(wasSlurred, rt.state, rt.out);
  }
};

struct DottedNote : NoOperands<DottedNote> {
  CAPCOM_COMMAND(0x02, "dotted-note", "Dotted Note");

  void execute(Runtime& rt) const { rt.state.noteDotted = true; }
};

struct ToggleOctaveUp : NoOperands<ToggleOctaveUp> {
  CAPCOM_COMMAND(0x03, "toggle-octave-up", "Toggle Octave Up");

  void execute(Runtime& rt) const { rt.state.noteOctaveUp = !rt.state.noteOctaveUp; }
};

struct GlobalTranspose {
  s8 raw = 0;

  CAPCOM_COMMAND(0x0a, "global-transpose", "Global Transpose");

  static GlobalTranspose parse(CommandReader& in) { return GlobalTranspose{.raw = in.s8("semitones")}; }

  void execute(Runtime& rt) const { rt.out.globalTranspose(raw); }
};

struct Transpose {
  s8 raw = 0;

  CAPCOM_COMMAND(0x0b, "transpose", "Transpose");

  static Transpose parse(CommandReader& in) { return Transpose{.raw = in.s8("semitones")}; }

  void execute(Runtime& rt) const { rt.state.transpose = raw; }
};

struct Tuning : S8Operand<Tuning> {
  CAPCOM_COMMAND(0x0c, "tuning", "Tuning");
  static constexpr std::string_view operandName = "tuning";

  void describe(CommandInfo& out) const {
    out.field("raw", raw);
    out.field("cents", tuningCents(raw));
  }

  void execute(Runtime& rt) const { rt.out.tuning(tuningCents(raw)); }
};

struct PortamentoTime : U8Operand<PortamentoTime> {
  CAPCOM_COMMAND(0x0d, "portamento-time", "Portamento Time");
  static constexpr std::string_view operandName = "time";

  void execute(Runtime& rt) const {
    // The driver stores portamento as speed; the next note converts it to a
    // distance-dependent time using the previous source key.
    rt.state.portamentoMillisecondsPerCent = portamentoMillisecondsPerCent(raw);
  }
};

struct Tempo : Be16Operand<Tempo> {
  CAPCOM_COMMAND(0x05, "tempo", "Tempo");
  static constexpr std::string_view operandName = "raw";

  void describe(CommandInfo& out) const {
    out.field("raw", raw);
    out.field("microseconds_per_quarter", tempoMicrosecondsPerQuarter(raw));
  }

  void execute(Runtime& rt) const { rt.out.tempo(tempoMicrosecondsPerQuarter(raw)); }
};

struct DurationRate : U8Operand<DurationRate> {
  CAPCOM_COMMAND(0x06, "duration-rate", "Duration Rate");
  static constexpr std::string_view operandName = "rate";

  void execute(Runtime& rt) const { rt.state.durationRate = raw; }
};

struct RepeatUntil {
  u8 slot = 0;
  u8 count = 0;
  Address destination;

  CAPCOM_KIND("repeat-until", "Repeat Until");

  static RepeatUntil parse(CommandReader& in) {
    const auto slot = static_cast<u8>(in.opcode() - 0x0e);
    in.derived("slot", static_cast<u64>(slot + 1));
    return RepeatUntil{
        .slot = slot,
        .count = in.u8("count"),
        .destination = in.be16Address("destination"),
    };
  }

  Effects execute(Runtime& rt) const {
    if (count == 0) {
      return rt.jump(destination);
    }

    // Capcom stores the number of replays. The VM helper receives total plays.
    return Effects{.step = rt.vm.repeatUntil(slot, static_cast<u32>(count) + 1, destination)};
  }
};

struct RepeatBreak {
  u8 slot = 0;
  u8 attributes = 0;
  Address destination;

  CAPCOM_KIND("repeat-break", "Repeat Break");

  static RepeatBreak parse(CommandReader& in) {
    const auto slot = static_cast<u8>(in.opcode() - 0x12);
    in.derived("slot", static_cast<u64>(slot + 1));
    return RepeatBreak{
        .slot = slot,
        .attributes = in.u8("attributes"),
        .destination = in.be16Address("destination"),
    };
  }

  Effects execute(Runtime& rt) const {
    const Step step = rt.vm.repeatBreak(slot, destination);
    if (step.kind == Step::Kind::Jump) {
      applyNoteAttributes(attributes, rt.state, &rt.out);
    }
    return Effects{.step = step};
  }
};

struct Volume : U8Operand<Volume> {
  CAPCOM_COMMAND(0x07, "volume", "Volume");
  static constexpr std::string_view operandName = "raw";

  void describe(CommandInfo& out, const Context& context) const {
    out.field("raw", raw);
    out.field("linear_gain", volumeGain(context.version, raw));
  }

  void execute(Runtime& rt) const { rt.out.level(volumeGain(rt.context.version, raw), LevelResolution::FourteenBit); }
};

struct Program : U8Operand<Program> {
  CAPCOM_COMMAND(0x08, "program", "Program");
  static constexpr std::string_view operandName = "program";

  [[nodiscard]] u32 bank() const { return raw >> 7; }
  [[nodiscard]] u32 program() const { return raw & 0x7f; }

  void describe(CommandInfo& out) const {
    out.field("bank", bank());
    out.field("program", program());
  }

  void execute(Runtime& rt) const { rt.out.instrument(bank(), program(), true); }
};

struct Jump {
  Address destination;

  CAPCOM_COMMAND(0x16, "jump", "Jump");

  static Jump parse(CommandReader& in) { return Jump{.destination = in.be16Address("destination")}; }

  Effects execute(Runtime& rt) const { return rt.jump(destination); }
};

struct End : NoOperands<End> {
  CAPCOM_COMMAND(0x17, "end", "End");

  Effects execute(Runtime& rt) const { return rt.end(); }
};

struct Pan : U8Operand<Pan> {
  CAPCOM_COMMAND(0x18, "pan", "Pan");
  static constexpr std::string_view operandName = "raw";

  [[nodiscard]] ::capcom_snes::PanConversionResult conversion(const Context& context) const {
    return panConversion(context.version, raw);
  }

  void describe(CommandInfo& out, const Context& context) const {
    const auto pan = conversion(context);
    out.field("raw", raw);
    out.field("stereo_position", stereoPosition(pan));
    out.field("linear_gain", pan.volumeScale);
  }

  void execute(Runtime& rt) const {
    const auto pan = conversion(rt.context);
    rt.out.pan(stereoPosition(pan), pan.volumeScale);
  }
};

struct MasterVolume : U8Operand<MasterVolume> {
  CAPCOM_COMMAND(0x19, "master-volume", "Master Volume");
  static constexpr std::string_view operandName = "raw";

  void describe(CommandInfo& out, const Context& context) const {
    out.field("raw", raw);
    out.field("linear_gain", volumeGain(context.version, raw));
  }

  void execute(Runtime& rt) const { rt.out.masterLevel(volumeGain(rt.context.version, raw)); }
};

struct Lfo {
  u8 type = 0;
  u8 value = 0;

  CAPCOM_COMMAND(0x1a, "lfo", "LFO");

  static Lfo parse(CommandReader& in) {
    return Lfo{
        .type = in.u8("type"),
        .value = in.u8("value"),
    };
  }

  void describe(CommandInfo& out) const {
    out.field("type", type);
    out.field("value", value);
  }

  void execute(Runtime& rt) const {
    auto& state = rt.state;
    switch (type) {
      case 0:
        state.vibratoDepth = value & 0x7f;
        rt.out.modulation(ModulationPerformanceTarget::VibratoDepth,
                          state.modulationRate != 0 ? static_cast<double>(state.vibratoDepth) / 127.0 : 0.0);
        break;

      case 1:
        state.tremoloDepth =
            ::capcom_snes::tremoloDepthToMidiValue(value, rt.context.version == CapcomSnesEngineVersion::v1BgmInList);
        rt.out.modulation(ModulationPerformanceTarget::TremoloDepth,
                          state.modulationRate != 0 ? static_cast<double>(state.tremoloDepth) / 127.0 : 0.0);
        break;

      case 2: {
        const bool wasEnabled = state.modulationRate != 0;
        state.modulationRate = value;
        const bool isEnabled = state.modulationRate != 0;
        if (!isEnabled && wasEnabled) {
          emitModulationDepths(state, rt.out, false);
        } else if (isEnabled && !wasEnabled) {
          emitModulationDepths(state, rt.out, true);
        }

        const double rate = static_cast<double>(::capcom_snes::lfoRateByteToMidiValue(value)) / 127.0;
        rt.out.modulation(ModulationPerformanceTarget::VibratoRate, rate);
        rt.out.modulation(ModulationPerformanceTarget::TremoloRate, rate);
        break;
      }

      default:
        // Type 3 is the driver's reset-LFO-phase flag. SF2/DLS always reset phase
        // on note activation, so there is no target-neutral performance event yet.
        break;
    }
  }
};

struct EchoParam {
  u8 argument = 0;
  u8 preset = 0;

  CAPCOM_COMMAND(0x1b, "echo-param", "Echo Param");

  static EchoParam parse(CommandReader& in) {
    return EchoParam{
        .argument = in.u8("argument"),
        .preset = in.u8("preset"),
    };
  }

  void describe(CommandInfo& out) const {
    out.field("argument", argument);
    out.field("preset", preset);
  }
};

struct EchoOnOff : U8Operand<EchoOnOff> {
  CAPCOM_COMMAND(0x1c, "echo-on-off", "Echo On/Off");
  static constexpr std::string_view operandName = "enabled";

  void describe(CommandInfo& out) const { out.field("enabled", static_cast<u8>(raw & 1)); }

  void execute(Runtime& rt) const { rt.out.reverb((raw & 1) != 0 ? 40.0 / 127.0 : 0.0); }
};

struct ReleaseRate : U8Operand<ReleaseRate> {
  CAPCOM_COMMAND(0x1d, "release-rate", "Release Rate");
  static constexpr std::string_view operandName = "raw";

  void describe(CommandInfo& out) const {
    out.field("raw", raw);
    out.field("gain", static_cast<u8>(raw | 0xa0));
  }
};

struct Nop : NoOperands<Nop> {
  CAPCOM_KIND("nop", "No Operation");
};

struct UnknownOneByte {
  u8 opcode = 0;
  u8 value = 0;

  CAPCOM_KIND("unknown-one-byte", "Unknown One-Byte Event");

  static UnknownOneByte parse(CommandReader& in) {
    return UnknownOneByte{
        .opcode = in.opcode(),
        .value = in.u8("value"),
    };
  }

  void describe(CommandInfo& out) const {
    out.field("opcode", opcode);
    out.field("value", value);
  }
};

struct UnknownOpcode {
  u8 opcode = 0;

  CAPCOM_KIND("unknown", "Unknown Opcode");

  static UnknownOpcode parse(CommandReader& in) { return UnknownOpcode{.opcode = in.opcode()}; }

  void describe(CommandInfo& out) const { out.field("opcode", opcode); }

  Effects execute(Runtime& rt) const {
    rt.vm.diagnostic(Diagnostic{
        .severity = Severity::Warning,
        .message = "Unknown Capcom SNES sequence opcode",
    });
    return rt.end();
  }
};

#define CAPCOM_COMMAND_TYPES                                                                                        \
  Rest, Note, ToggleTriplet, ToggleSlur, DottedNote, ToggleOctaveUp, NoteAttributes, Octave, GlobalTranspose,       \
      Transpose, Tuning, PortamentoTime, Tempo, DurationRate, Volume, Program, RepeatUntil, RepeatBreak, Jump, End, \
      Pan, MasterVolume, Lfo, EchoParam, EchoOnOff, ReleaseRate, Nop, UnknownOneByte, UnknownOpcode

struct AppendCommandResult {
  bool ok = false;
  u32 nextOffset = 0;
};

template <class Command>
void appendFixedCommand(TrackProgramBuilder& builder, const SequenceDialect& dialect, ByteReader reader,
                        u32 beginOffset, u32 size) {
  const auto decoded = recordSizedBytecodeCommand<Command>(dialect, reader, beginOffset, beginOffset + size);
  appendDecodedBytecodeCommand(builder, decoded, beginOffset);
}

template <class Command>
AppendCommandResult appendCommand(TrackProgramBuilder& builder, const SequenceDialect& dialect, ByteReader reader,
                                  u32 beginOffset) {
  if (!reader.has(beginOffset, 1)) {
    return AppendCommandResult{.ok = false, .nextOffset = beginOffset};
  }

  const auto parsed = parseBytecodeCommand<Command>(dialect, reader, beginOffset, static_cast<u32>(reader.size()));
  if (!parsed) {
    appendFixedCommand<UnknownOpcode>(builder, dialect, reader, beginOffset, 1);
    return AppendCommandResult{.ok = false, .nextOffset = beginOffset + 1};
  }

  appendDecodedBytecodeCommand(builder, parsed->decoded, beginOffset);
  return AppendCommandResult{.ok = true, .nextOffset = static_cast<u32>(parsed->decoded.range.endOffset())};
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
      .commands<CAPCOM_COMMAND_TYPES>();
}

void registerCapcomSnesSequenceDialects(SequenceDialectRegistry& registry) {
  registry.add(capcomSnesSequenceDialect(CapcomSnesEngineVersion::none));
  registry.add(capcomSnesSequenceDialect(CapcomSnesEngineVersion::v1BgmInList));
  registry.add(capcomSnesSequenceDialect(CapcomSnesEngineVersion::v2BgmUsuallyAtFixedLocation));
  registry.add(capcomSnesSequenceDialect(CapcomSnesEngineVersion::v3BgmFixedLocation));
}

TrackProgram decodeCapcomSnesSourceTrack(ByteReader reader, const SequenceDialect& dialect, u32 sourceTrackNumber,
                                         u32 startAddress) {
#define CAPCOM_EMIT(Type)                                                            \
  do {                                                                               \
    const auto decoded = appendCommand<Type>(builder, dialect, reader, beginOffset); \
    if (!decoded.ok) {                                                               \
      return track;                                                                  \
    }                                                                                \
    offset = decoded.nextOffset;                                                     \
  } while (false)
#define CAPCOM_CASE(Type) \
  case Type::opcode:      \
    CAPCOM_EMIT(Type);    \
    break

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
        CAPCOM_EMIT(Rest);
      } else {
        CAPCOM_EMIT(Note);
      }
      continue;
    }

    switch (opcode) {
      CAPCOM_CASE(ToggleTriplet);
      CAPCOM_CASE(ToggleSlur);
      CAPCOM_CASE(DottedNote);
      CAPCOM_CASE(ToggleOctaveUp);
      CAPCOM_CASE(NoteAttributes);
      CAPCOM_CASE(Tempo);
      CAPCOM_CASE(DurationRate);
      CAPCOM_CASE(Volume);
      CAPCOM_CASE(Program);
      CAPCOM_CASE(Octave);
      CAPCOM_CASE(GlobalTranspose);
      CAPCOM_CASE(Transpose);
      CAPCOM_CASE(Tuning);
      CAPCOM_CASE(PortamentoTime);

      case 0x0e:
      case 0x0f:
      case 0x10:
      case 0x11:
        CAPCOM_EMIT(RepeatUntil);
        break;

      case 0x12:
      case 0x13:
      case 0x14:
      case 0x15:
        CAPCOM_EMIT(RepeatBreak);
        break;

      case Jump::opcode:
        if (const auto decoded = appendCommand<Jump>(builder, dialect, reader, beginOffset); !decoded.ok) {
          return track;
        }
        offset = reader.be16(beginOffset + 1);
        break;

      case End::opcode:
        CAPCOM_EMIT(End);
        return track;

      case Pan::opcode:
        CAPCOM_EMIT(Pan);
        break;

      case MasterVolume::opcode:
        CAPCOM_EMIT(MasterVolume);
        break;

      case Lfo::opcode:
        CAPCOM_EMIT(Lfo);
        break;

      case EchoParam::opcode:
        CAPCOM_EMIT(EchoParam);
        break;

      case EchoOnOff::opcode:
        CAPCOM_EMIT(EchoOnOff);
        break;

      case ReleaseRate::opcode:
        CAPCOM_EMIT(ReleaseRate);
        break;

      case 0x1e:
      case 0x1f:
        if (dialect.id.value == "capcom-snes:v1") {
          CAPCOM_EMIT(UnknownOneByte);
        } else {
          CAPCOM_EMIT(Nop);
        }
        break;

      default:
        appendFixedCommand<UnknownOpcode>(builder, dialect, reader, beginOffset, 1);
        return track;
    }
  }

#undef CAPCOM_EMIT
#undef CAPCOM_CASE
  return track;
}

#undef CAPCOM_COMMAND_TYPES
#undef CAPCOM_COMMAND
#undef CAPCOM_KIND

}  // namespace vgmtrans::formats::capcom_snes
