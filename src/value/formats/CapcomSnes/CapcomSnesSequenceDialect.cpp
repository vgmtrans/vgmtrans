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

// Keep the DSL limited to one-line state effects where the macro name carries
// the whole source-driver meaning.
#define CAPCOM_TOGGLE(Type, Member)                                         \
  struct Type : NoOperands<Type> {                                          \
    void execute(Runtime& rt) const { rt.state.Member = !rt.state.Member; } \
  }

#define CAPCOM_SET_TRUE(Type, Member)                           \
  struct Type : NoOperands<Type> {                              \
    void execute(Runtime& rt) const { rt.state.Member = true; } \
  }

#define CAPCOM_U8_STATE(Type, Operand, Member)                 \
  struct Type : U8Operand<Type> {                              \
    static constexpr std::string_view operandName = Operand;   \
    void execute(Runtime& rt) const { rt.state.Member = raw; } \
  }

#define CAPCOM_S8_STATE(Type, Operand, Member)                 \
  struct Type : S8Operand<Type> {                              \
    static constexpr std::string_view operandName = Operand;   \
    void execute(Runtime& rt) const { rt.state.Member = raw; } \
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
  static constexpr std::string_view operandName = "raw";

  void execute(Runtime& rt) const { rt.state.applyAttributes(raw, &rt.out); }
};

CAPCOM_U8_STATE(Octave, "octave", noteOctave);
CAPCOM_TOGGLE(ToggleTriplet, noteTriplet);

struct ToggleSlur : NoOperands<ToggleSlur> {
  void execute(Runtime& rt) const { rt.state.toggleSlur(rt.out); }
};

CAPCOM_SET_TRUE(DottedNote, noteDotted);
CAPCOM_TOGGLE(ToggleOctaveUp, noteOctaveUp);

struct GlobalTranspose {
  s8 raw = 0;

  static GlobalTranspose parse(CommandReader& in) { return GlobalTranspose{.raw = in.s8("semitones")}; }

  void execute(Runtime& rt) const { rt.out.globalTranspose(raw); }
};

CAPCOM_S8_STATE(Transpose, "semitones", transpose);

struct Tuning : S8Operand<Tuning> {
  static constexpr std::string_view operandName = "tuning";

  void describe(CommandInfo& out) const { out.field("cents", tuningCents(raw)); }

  void execute(Runtime& rt) const { rt.out.tuning(tuningCents(raw)); }
};

struct PortamentoTime : U8Operand<PortamentoTime> {
  static constexpr std::string_view operandName = "time";

  void execute(Runtime& rt) const {
    // The driver stores portamento as speed; the next note converts it to a
    // distance-dependent time using the previous source key.
    rt.state.portamentoMillisecondsPerCent = portamentoMillisecondsPerCent(raw);
  }
};

struct Tempo : Be16Operand<Tempo> {
  static constexpr std::string_view operandName = "raw";

  void describe(CommandInfo& out) const { out.field("microseconds_per_quarter", tempoMicrosecondsPerQuarter(raw)); }

  void execute(Runtime& rt) const { rt.out.tempo(tempoMicrosecondsPerQuarter(raw)); }
};

CAPCOM_U8_STATE(DurationRate, "rate", durationRate);

struct RepeatUntil {
  u8 slot = 0;
  u8 count = 0;
  Address destination;

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
  static constexpr std::string_view operandName = "raw";

  void describe(CommandInfo& out, const Context& context) const {
    out.field("linear_gain", volumeGain(context.version, raw));
  }

  void execute(Runtime& rt) const { rt.out.level(volumeGain(rt.context.version, raw), LevelResolution::FourteenBit); }
};

struct Program : U8Operand<Program> {
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

  static Jump parse(CommandReader& in) { return Jump{.destination = in.be16Address("destination")}; }

  Effects execute(Runtime& rt) const { return rt.jump(destination); }
};

struct End : NoOperands<End> {
  Effects execute(Runtime& rt) const { return rt.end(); }
};

struct Pan : U8Operand<Pan> {
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
  static constexpr std::string_view operandName = "raw";

  void describe(CommandInfo& out, const Context& context) const {
    out.field("linear_gain", volumeGain(context.version, raw));
  }

  void execute(Runtime& rt) const { rt.out.masterLevel(volumeGain(rt.context.version, raw)); }
};

struct Lfo {
  u8 type = 0;
  u8 value = 0;

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

  static EchoParam parse(CommandReader& in) {
    return EchoParam{
        .argument = in.u8("argument"),
        .preset = in.u8("preset"),
    };
  }
};

struct EchoOnOff {
  u8 raw = 0;

  static EchoOnOff parse(CommandReader& in) {
    EchoOnOff result{.raw = in.u8("raw")};
    in.derived("enabled", static_cast<u64>(result.raw & 1));
    return result;
  }

  void execute(Runtime& rt) const { rt.out.reverb((raw & 1) != 0 ? 40.0 / 127.0 : 0.0); }
};

struct ReleaseRate : U8Operand<ReleaseRate> {
  static constexpr std::string_view operandName = "raw";

  void describe(CommandInfo& out) const { out.field("gain", static_cast<u8>(raw | 0xa0)); }
};

struct Nop : NoOperands<Nop> {};

struct UnknownOneByte {
  u8 opcode = 0;
  u8 value = 0;

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

template <class Registrar>
[[nodiscard]] BytecodeDispatchTable capcomBytecodeMap(Registrar& registrar, CapcomSnesEngineVersion version) {
  BytecodeMapBuilder<TrackState, Context> map{"capcom-snes", registrar};

  for (u16 opcode = 0x20; opcode <= 0xff; ++opcode) {
    if ((opcode & 0x1f) == 0) {
      map.op<Rest>(static_cast<u8>(opcode), "Rest");
    } else {
      map.op<Note>(static_cast<u8>(opcode), "Note");
    }
  }

  map.op<0x00, ToggleTriplet>("Toggle Triplet");
  map.op<0x01, ToggleSlur>("Toggle Slur");
  map.op<0x02, DottedNote>("Dotted Note");
  map.op<0x03, ToggleOctaveUp>("Toggle Octave Up");
  map.op<0x04, NoteAttributes>("Note Attributes");
  map.op<0x05, Tempo>("Tempo");
  map.op<0x06, DurationRate>("Duration Rate");
  map.op<0x07, Volume>("Volume");
  map.op<0x08, Program>("Program");
  map.op<0x09, Octave>("Octave");
  map.op<0x0a, GlobalTranspose>("Global Transpose");
  map.op<0x0b, Transpose>("Transpose");
  map.op<0x0c, Tuning>("Tuning");
  map.op<0x0d, PortamentoTime>("Portamento Time");
  map.range<0x0e, 0x11, RepeatUntil>("Repeat Until");
  map.range<0x12, 0x15, RepeatBreak>("Repeat Break");
  map.jump<0x16, Jump, &Jump::destination>("Jump");
  map.terminal<0x17, End>("End");
  map.op<0x18, Pan>("Pan");
  map.op<0x19, MasterVolume>("Master Volume");
  map.op<0x1a, Lfo>("LFO");
  map.op<0x1b, EchoParam>("Echo Param");
  map.op<0x1c, EchoOnOff>("Echo On/Off");
  map.op<0x1d, ReleaseRate>("Release Rate");

  if (version == CapcomSnesEngineVersion::v1BgmInList) {
    map.op<0x1e, UnknownOneByte>("Unknown One-Byte Event", suffix("unknown-one-byte"));
    map.op<0x1f, UnknownOneByte>("Unknown One-Byte Event", suffix("unknown-one-byte"));
  } else {
    map.op<0x1e, Nop>("No Operation", suffix("nop"));
    map.op<0x1f, Nop>("No Operation", suffix("nop"));
  }

  map.unknown<UnknownOpcode>("Unknown Opcode", suffix("unknown"));
  return map.finish();
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

[[nodiscard]] CapcomSnesEngineVersion versionForDialect(const SequenceDialect& dialect) {
  if (dialect.id.value == "capcom-snes:v1") {
    return CapcomSnesEngineVersion::v1BgmInList;
  }
  if (dialect.id.value == "capcom-snes:v2") {
    return CapcomSnesEngineVersion::v2BgmUsuallyAtFixedLocation;
  }
  if (dialect.id.value == "capcom-snes:v3") {
    return CapcomSnesEngineVersion::v3BgmFixedLocation;
  }
  return CapcomSnesEngineVersion::none;
}

}  // namespace

SequenceDialect capcomSnesSequenceDialect(CapcomSnesEngineVersion version) {
  SequenceDialectBuilder<TrackState, Context> builder{dialectId(version), Context{.version = version}};
  builder.timebase(Timebase{.ppqn = kCapcomSnesPpqn})
      .defaultBehavior(SequenceProgramBehavior{
          .defaultLoopPolicy = LoopPolicy::PlayOnce,
          .initialReverbSend = 0.0,
          .initialMonoModeChannels = 0,
          .stopAllTracksAtFirstLoop = true,
      });
  static_cast<void>(capcomBytecodeMap(builder, version));
  return builder.finish();
}

void registerCapcomSnesSequenceDialects(SequenceDialectRegistry& registry) {
  registry.add(capcomSnesSequenceDialect(CapcomSnesEngineVersion::none));
  registry.add(capcomSnesSequenceDialect(CapcomSnesEngineVersion::v1BgmInList));
  registry.add(capcomSnesSequenceDialect(CapcomSnesEngineVersion::v2BgmUsuallyAtFixedLocation));
  registry.add(capcomSnesSequenceDialect(CapcomSnesEngineVersion::v3BgmFixedLocation));
}

TrackProgram decodeCapcomSnesSourceTrack(ByteReader reader, const SequenceDialect& dialect, u32 sourceTrackNumber,
                                         u32 startAddress) {
  const BytecodeDispatchTable bytecode = capcomBytecodeMap(dialect, versionForDialect(dialect));
  return decodeLinearBytecodeTrack(reader, sourceTrackNumber, startAddress,
                                   LinearBytecodeDecodePolicy{.maxCommands = 4096},
                                   [&](u32 offset) { return bytecode.decode(reader, offset); });
}

#undef CAPCOM_S8_STATE
#undef CAPCOM_U8_STATE
#undef CAPCOM_SET_TRUE
#undef CAPCOM_TOGGLE

}  // namespace vgmtrans::formats::capcom_snes
