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

// Keep the DSL limited to one-line state effects where the macro name carries
// the whole source-driver meaning.
#define CAPCOM_TOGGLE(Type, Op, Suffix, DisplayName, Member)                \
  struct Type : NoOperands<Type> {                                          \
    CAPCOM_COMMAND(Op, Suffix, DisplayName);                                \
    void execute(Runtime& rt) const { rt.state.Member = !rt.state.Member; } \
  }

#define CAPCOM_SET_TRUE(Type, Op, Suffix, DisplayName, Member)  \
  struct Type : NoOperands<Type> {                              \
    CAPCOM_COMMAND(Op, Suffix, DisplayName);                    \
    void execute(Runtime& rt) const { rt.state.Member = true; } \
  }

#define CAPCOM_U8_STATE(Type, Op, Suffix, DisplayName, Operand, Member) \
  struct Type : U8Operand<Type> {                                       \
    CAPCOM_COMMAND(Op, Suffix, DisplayName);                            \
    static constexpr std::string_view operandName = Operand;            \
    void execute(Runtime& rt) const { rt.state.Member = raw; }          \
  }

#define CAPCOM_S8_STATE(Type, Op, Suffix, DisplayName, Operand, Member) \
  struct Type : S8Operand<Type> {                                       \
    CAPCOM_COMMAND(Op, Suffix, DisplayName);                            \
    static constexpr std::string_view operandName = Operand;            \
    void execute(Runtime& rt) const { rt.state.Member = raw; }          \
  }

constexpr u8 kNoteOctaveMask = 0x07;
constexpr u8 kNoteOctaveUpMask = 0x08;
constexpr u8 kNoteDottedMask = 0x10;
constexpr u8 kNoteTripletMask = 0x20;
constexpr u8 kNoteSlurredMask = 0x40;

struct Context {
  CapcomSnesEngineVersion version = CapcomSnesEngineVersion::v3BgmFixedLocation;
};

struct TrackState {
  u32 consumeNoteTicks(u8 rawDuration);
  [[nodiscard]] u32 soundingTicks(u32 length) const;
  [[nodiscard]] s32 sourceKey(u8 keyIndex) const;
  [[nodiscard]] double performedKey(s32 key) const;
  [[nodiscard]] bool extendsPreviousSlurredNote(s32 key) const;
  void finishExtendedNote();
  void finishNote(s32 key);
  void applyAttributes(u8 attributes, Emit* out = nullptr);
  void toggleSlur(Emit& out);
  void emitPortamentoIfNeeded(s32 key, Emit& out);
  void emitModulationDepths(Emit& out, bool enabled) const;

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

u32 TrackState::consumeNoteTicks(u8 rawDuration) {
  u32 length = baseNoteTicks(rawDuration);
  if (noteDotted) {
    // Dotted is consumed by the next note/rest; triplet mode persists until toggled.
    length = (length % 2 == 0 && length < 0x80) ? length + (length / 2) : 0;
    noteDotted = false;
  } else if (noteTriplet) {
    length = length * 2 / 3;
  }
  return length;
}

u32 TrackState::soundingTicks(u32 length) const {
  u32 duration = length * durationRate;
  if (noteSlurred || duration == 0) {
    duration = length << 8;
  }
  duration = (duration + 0x80) >> 8;
  return duration == 0 ? 1 : duration;
}

s32 TrackState::sourceKey(u8 keyIndex) const {
  return static_cast<s32>(keyIndex) - 1 + static_cast<s32>(noteOctave * 12) + (noteOctaveUp ? 24 : 0);
}

double TrackState::performedKey(s32 key) const {
  return static_cast<double>(key + transpose);
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

void TrackState::applyAttributes(u8 attributes, Emit* out) {
  const bool wasSlurred = noteSlurred;
  // The driver ORs octave bits instead of replacing them. Preserve that quirk until
  // parity proves a specific version behaves differently.
  noteOctave |= attributes & kNoteOctaveMask;
  noteDotted = noteDotted || ((attributes & kNoteDottedMask) != 0);
  noteOctaveUp = (attributes & kNoteOctaveUpMask) != 0;
  noteTriplet = (attributes & kNoteTripletMask) != 0;
  noteSlurred = (attributes & kNoteSlurredMask) != 0;
  if (out != nullptr && noteSlurred != wasSlurred) {
    out->legatoPedal(noteSlurred);
  }
}

void TrackState::toggleSlur(Emit& out) {
  const bool wasSlurred = noteSlurred;
  noteSlurred = !noteSlurred;
  if (noteSlurred != wasSlurred) {
    out.legatoPedal(noteSlurred);
  }
}

bool TrackState::extendsPreviousSlurredNote(s32 key) const {
  return lastNoteSlurred && lastSourceKey && key == *lastSourceKey && !didRest;
}

void TrackState::finishExtendedNote() {
  lastNoteSlurred = noteSlurred;
}

void TrackState::finishNote(s32 key) {
  lastSourceKey = key;
  didRest = false;
  lastNoteSlurred = noteSlurred;
}

void TrackState::emitPortamentoIfNeeded(s32 key, Emit& out) {
  if (portamentoMillisecondsPerCent <= 0.0 || !lastSourceKey) {
    return;
  }

  const auto keyDistance = static_cast<u32>(std::abs(key - *lastSourceKey));
  const auto portamentoTime = static_cast<u16>(keyDistance * 100 * portamentoMillisecondsPerCent);
  if (portamentoTime != lastPortamentoTime) {
    out.portamento(static_cast<double>(portamentoTime), static_cast<double>(*lastSourceKey + transpose));
    lastPortamentoTime = portamentoTime;
  } else {
    out.portamentoControl(static_cast<double>(*lastSourceKey + transpose));
  }
}

void TrackState::emitModulationDepths(Emit& out, bool enabled) const {
  if (vibratoDepth != 0) {
    out.modulation(ModulationPerformanceTarget::VibratoDepth,
                   enabled ? static_cast<double>(vibratoDepth) / 127.0 : 0.0);
  }
  if (tremoloDepth != 0) {
    out.modulation(ModulationPerformanceTarget::TremoloDepth,
                   enabled ? static_cast<double>(tremoloDepth) / 127.0 : 0.0);
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
    const u32 length = rt.state.consumeNoteTicks(rawDuration);
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
    const u32 length = state.consumeNoteTicks(rawDuration);
    const s32 key = state.sourceKey(keyIndex);
    const u32 duration = state.soundingTicks(length);

    if (state.extendsPreviousSlurredNote(key)) {
      // The Capcom driver treats repeated keys after a slurred note as a tie.
      // Legacy VGMTrans extends the previous MIDI note even if this note has
      // already cleared the slur bit, so the state must look at the previous note.
      rt.out.note(state.performedKey(key), 1.0, duration, true);
      state.finishExtendedNote();
      return rt.wait(length);
    }

    state.emitPortamentoIfNeeded(key, rt.out);
    // Slur is modeled as a one-tick overlap into the next source note.
    rt.out.note(state.performedKey(key), 1.0, duration + (state.noteSlurred ? 1u : 0u));
    state.finishNote(key);
    return rt.wait(length);
  }
};

struct NoteAttributes : U8Operand<NoteAttributes> {
  CAPCOM_COMMAND(0x04, "note-attributes", "Note Attributes");
  static constexpr std::string_view operandName = "raw";

  void execute(Runtime& rt) const { rt.state.applyAttributes(raw, &rt.out); }
};

CAPCOM_U8_STATE(Octave, 0x09, "octave", "Octave", "octave", noteOctave);
CAPCOM_TOGGLE(ToggleTriplet, 0x00, "toggle-triplet", "Toggle Triplet", noteTriplet);

struct ToggleSlur : NoOperands<ToggleSlur> {
  CAPCOM_COMMAND(0x01, "toggle-slur", "Toggle Slur");

  void execute(Runtime& rt) const { rt.state.toggleSlur(rt.out); }
};

CAPCOM_SET_TRUE(DottedNote, 0x02, "dotted-note", "Dotted Note", noteDotted);
CAPCOM_TOGGLE(ToggleOctaveUp, 0x03, "toggle-octave-up", "Toggle Octave Up", noteOctaveUp);

struct GlobalTranspose {
  s8 raw = 0;

  CAPCOM_COMMAND(0x0a, "global-transpose", "Global Transpose");

  static GlobalTranspose parse(CommandReader& in) { return GlobalTranspose{.raw = in.s8("semitones")}; }

  void execute(Runtime& rt) const { rt.out.globalTranspose(raw); }
};

CAPCOM_S8_STATE(Transpose, 0x0b, "transpose", "Transpose", "semitones", transpose);

struct Tuning : S8Operand<Tuning> {
  CAPCOM_COMMAND(0x0c, "tuning", "Tuning");
  static constexpr std::string_view operandName = "tuning";

  void describe(CommandInfo& out) const { out.field("cents", tuningCents(raw)); }

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

  void describe(CommandInfo& out) const { out.field("microseconds_per_quarter", tempoMicrosecondsPerQuarter(raw)); }

  void execute(Runtime& rt) const { rt.out.tempo(tempoMicrosecondsPerQuarter(raw)); }
};

CAPCOM_U8_STATE(DurationRate, 0x06, "duration-rate", "Duration Rate", "rate", durationRate);

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
    return rt.vm.repeatUntilEffect(slot, static_cast<u32>(count) + 1, destination);
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
    const BranchResult branch = rt.vm.repeatBreakBranch(slot, destination);
    if (branch.taken) {
      rt.state.applyAttributes(attributes, &rt.out);
    }
    return branch.effects;
  }
};

struct Volume : U8Operand<Volume> {
  CAPCOM_COMMAND(0x07, "volume", "Volume");
  static constexpr std::string_view operandName = "raw";

  void describe(CommandInfo& out, const Context& context) const {
    out.field("linear_gain", volumeGain(context.version, raw));
  }

  void execute(Runtime& rt) const { rt.out.level(volumeGain(rt.context.version, raw), LevelResolution::FourteenBit); }
};

struct Program : U8Operand<Program> {
  CAPCOM_COMMAND(0x08, "program", "Program");
  static constexpr std::string_view operandName = "raw";

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
          state.emitModulationDepths(rt.out, false);
        } else if (isEnabled && !wasEnabled) {
          state.emitModulationDepths(rt.out, true);
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
};

struct EchoOnOff {
  u8 raw = 0;

  CAPCOM_COMMAND(0x1c, "echo-on-off", "Echo On/Off");

  static EchoOnOff parse(CommandReader& in) {
    EchoOnOff result{.raw = in.u8("raw")};
    in.derived("enabled", static_cast<u64>(result.raw & 1));
    return result;
  }

  void execute(Runtime& rt) const { rt.out.reverb((raw & 1) != 0 ? 40.0 / 127.0 : 0.0); }
};

struct ReleaseRate : U8Operand<ReleaseRate> {
  CAPCOM_COMMAND(0x1d, "release-rate", "Release Rate");
  static constexpr std::string_view operandName = "raw";

  void describe(CommandInfo& out) const { out.field("gain", static_cast<u8>(raw | 0xa0)); }
};

struct Nop : NoOperands<Nop> {
  CAPCOM_KIND("nop", "No Operation");
};

struct UnknownOneByte {
  u8 opcode = 0;
  u8 value = 0;

  CAPCOM_KIND("unknown-one-byte", "Unknown One-Byte Event");

  static UnknownOneByte parse(CommandReader& in) {
    in.derived("opcode", static_cast<u64>(in.opcode()));
    return UnknownOneByte{
        .opcode = in.opcode(),
        .value = in.u8("value"),
    };
  }
};

struct UnknownOpcode {
  u8 opcode = 0;

  CAPCOM_KIND("unknown", "Unknown Opcode");

  static UnknownOpcode parse(CommandReader& in) {
    in.derived("opcode", static_cast<u64>(in.opcode()));
    return UnknownOpcode{.opcode = in.opcode()};
  }

  Effects execute(Runtime& rt) const {
    rt.vm.diagnostic(Diagnostic{
        .severity = Severity::Warning,
        .message = "Unknown Capcom SNES sequence opcode",
    });
    return rt.end();
  }
};

#define CAPCOM_FALLTHROUGH_COMMANDS(X) \
  X(ToggleTriplet)                     \
  X(ToggleSlur)                        \
  X(DottedNote)                        \
  X(ToggleOctaveUp)                    \
  X(NoteAttributes)                    \
  X(Tempo)                             \
  X(DurationRate)                      \
  X(Volume)                            \
  X(Program)                           \
  X(Octave)                            \
  X(GlobalTranspose)                   \
  X(Transpose)                         \
  X(Tuning)                            \
  X(PortamentoTime)                    \
  X(Pan)                               \
  X(MasterVolume)                      \
  X(Lfo)                               \
  X(EchoParam)                         \
  X(EchoOnOff)                         \
  X(ReleaseRate)

#define CAPCOM_TYPE(Type) Type,
#define CAPCOM_COMMAND_TYPES                                                                                     \
  Rest, Note, CAPCOM_FALLTHROUGH_COMMANDS(CAPCOM_TYPE) RepeatUntil, RepeatBreak, Jump, End, Nop, UnknownOneByte, \
      UnknownOpcode

template <class Command>
[[nodiscard]] DecodedBytecodeCommand capcomFallthroughCommand(const SequenceDialect& dialect, ByteReader reader,
                                                              u32 begin) {
  return recordAutoFallthroughBytecodeCommand<Command, UnknownOpcode>(dialect, reader, begin,
                                                                      static_cast<u32>(reader.size()));
}

[[nodiscard]] DecodedBytecodeCommand capcomJumpCommand(const SequenceDialect& dialect, ByteReader reader, u32 begin) {
  auto parsed = parseBytecodeCommand<Jump>(dialect, reader, begin, static_cast<u32>(reader.size()));
  if (!parsed) {
    return truncatedBytecodeCommand<UnknownOpcode>(dialect, reader, begin, static_cast<u32>(reader.size()));
  }
  auto decoded = std::move(parsed->decoded);
  decoded.flow.staticTargets = {parsed->command.destination};
  return decoded;
}

[[nodiscard]] DecodedBytecodeCommand capcomEndCommand(const SequenceDialect& dialect, ByteReader reader, u32 begin) {
  auto decoded = recordAutoBytecodeCommand<End, UnknownOpcode>(dialect, reader, begin, static_cast<u32>(reader.size()));
  decoded.flow.terminal = true;
  return decoded;
}

[[nodiscard]] DecodedBytecodeCommand capcomUnknownCommand(const SequenceDialect& dialect, ByteReader reader,
                                                          u32 begin) {
  return terminalBytecodeCommand<UnknownOpcode>(dialect, reader, begin, begin + 1);
}

[[nodiscard]] DecodedBytecodeCommand decodeCapcomCommand(ByteReader reader, const SequenceDialect& dialect, u32 begin) {
#define CAPCOM_EMIT(Type) return capcomFallthroughCommand<Type>(dialect, reader, begin);
#define CAPCOM_CASE(Type) \
  case Type::opcode:      \
    CAPCOM_EMIT(Type)

  const u8 opcode = reader.u8At(begin);
  if (opcode >= 0x20) {
    if ((opcode & 0x1f) == 0) {
      CAPCOM_EMIT(Rest);
    }
    CAPCOM_EMIT(Note);
  }

  switch (opcode) {
    CAPCOM_FALLTHROUGH_COMMANDS(CAPCOM_CASE)

    case 0x0e:
    case 0x0f:
    case 0x10:
    case 0x11:
      CAPCOM_EMIT(RepeatUntil);

    case 0x12:
    case 0x13:
    case 0x14:
    case 0x15:
      CAPCOM_EMIT(RepeatBreak);

    case Jump::opcode:
      return capcomJumpCommand(dialect, reader, begin);

    case End::opcode:
      return capcomEndCommand(dialect, reader, begin);

    case 0x1e:
    case 0x1f:
      if (dialect.id.value == "capcom-snes:v1") {
        CAPCOM_EMIT(UnknownOneByte);
      }
      CAPCOM_EMIT(Nop);

    default:
      return capcomUnknownCommand(dialect, reader, begin);
  }

#undef CAPCOM_CASE
#undef CAPCOM_EMIT
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
  return decodeLinearBytecodeTrack(reader, sourceTrackNumber, startAddress,
                                   LinearBytecodeDecodePolicy{.maxCommands = 4096},
                                   [&](u32 offset) { return decodeCapcomCommand(reader, dialect, offset); });
}

#undef CAPCOM_COMMAND_TYPES
#undef CAPCOM_TYPE
#undef CAPCOM_FALLTHROUGH_COMMANDS
#undef CAPCOM_S8_STATE
#undef CAPCOM_U8_STATE
#undef CAPCOM_SET_TRUE
#undef CAPCOM_TOGGLE
#undef CAPCOM_COMMAND
#undef CAPCOM_KIND

}  // namespace vgmtrans::formats::capcom_snes
