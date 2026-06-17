/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/CapcomSnes/CapcomSnesSequence.h"

#include "formats/CapcomSnes/CapcomSnesDriverMath.h"
#include "value/base/LevelScale.h"
#include "value/sequence/SequenceVm.h"
#include "value/sequence/bytecode/BytecodeMap.h"
#include "value/sequence/bytecode/BytecodeWalkers.h"
#include "value/sequence/bytecode/SequenceCommandHelpers.h"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace vgmtrans::formats::capcom_snes {

using namespace core;

namespace {

constexpr u8 kNoteOctaveMask = 0x07;
constexpr u8 kNoteOctaveUpMask = 0x08;
constexpr u8 kNoteDottedMask = 0x10;
constexpr u8 kNoteTripletMask = 0x20;
constexpr u8 kNoteSlurredMask = 0x40;

// Driver-local state used while executing decoded source commands.
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
  void applyAttributes(u8 attributes, PerformanceEmitter* out = nullptr);
  void toggleSlur(PerformanceEmitter& out);
  void emitPortamentoIfNeeded(s32 key, PerformanceEmitter& out);
  void emitModulationDepths(PerformanceEmitter& out, bool enabled) const;

  u32 durationRate = 0;
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

// Small driver formulas kept local to the Capcom sequence reader.
namespace math {

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
  // Store pan on the same 0..127 steps the MIDI renderer uses so Capcom center
  // and edge positions survive conversion without shifting left.
  return std::clamp((static_cast<double>(converted.midiPan) / 127.0) * 2.0 - 1.0, -1.0, 1.0);
}

[[nodiscard]] u8 tremoloDepth(CapcomSnesEngineVersion version, u8 rawDepth) {
  return ::capcom_snes::tremoloDepthToMidiValue(rawDepth, version == CapcomSnesEngineVersion::v1BgmInList);
}

[[nodiscard]] double midi7Amount(u8 value) {
  return static_cast<double>(value) / 127.0;
}

[[nodiscard]] double lfoRateAmount(u8 rawRate) {
  return midi7Amount(::capcom_snes::lfoRateByteToMidiValue(rawRate));
}

}  // namespace math

u32 TrackState::consumeNoteTicks(u8 rawDuration) {
  u32 length = math::baseNoteTicks(rawDuration);
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

void TrackState::applyAttributes(u8 attributes, PerformanceEmitter* out) {
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

void TrackState::toggleSlur(PerformanceEmitter& out) {
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

void TrackState::emitPortamentoIfNeeded(s32 key, PerformanceEmitter& out) {
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

void TrackState::emitModulationDepths(PerformanceEmitter& out, bool enabled) const {
  if (vibratoDepth != 0) {
    out.modulation(ModulationPerformanceTarget::VibratoDepth, enabled ? math::midi7Amount(vibratoDepth) : 0.0);
  }
  if (tremoloDepth != 0) {
    out.modulation(ModulationPerformanceTarget::TremoloDepth, enabled ? math::midi7Amount(tremoloDepth) : 0.0);
  }
}

using Runtime = CommandRuntime<TrackState, Context>;

// Opcode implementations.
void emitLinearVolume(Runtime& rt, u8 raw) {
  // Capcom volume is a linear amplitude gain. MIDI rendering applies the
  // square-root MIDI controller curve later.
  rt.out.level(LevelScale::linearFromLinear(math::volumeGain(rt.context.version, raw)),
               LevelPrecisionHint::FourteenBit);
}

void emitLinearMasterVolume(Runtime& rt, u8 raw) {
  rt.out.masterLevel(LevelScale::linearFromLinear(math::volumeGain(rt.context.version, raw)));
}

void emitPan(Runtime& rt, u8 raw) {
  const auto pan = math::panConversion(rt.context.version, raw);
  rt.out.pan(math::stereoPosition(pan), LevelScale::linearFromLinear(pan.volumeScale));
}

// Notes and note-state commands.
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

struct Octave : U8StateCommand<Octave, &TrackState::noteOctave> {
  static constexpr std::string_view operandName = "octave";
};

struct ToggleTriplet : ToggleBoolStateCommand<ToggleTriplet, &TrackState::noteTriplet> {};

struct ToggleSlur : NoOperands<ToggleSlur> {
  void execute(Runtime& rt) const { rt.state.toggleSlur(rt.out); }
};

struct DottedNote : SetTrueStateCommand<DottedNote, &TrackState::noteDotted> {};

struct ToggleOctaveUp : ToggleBoolStateCommand<ToggleOctaveUp, &TrackState::noteOctaveUp> {};

// Pitch, tuning, and timing commands.
struct GlobalTranspose {
  s8 raw = 0;

  static GlobalTranspose parse(CommandReader& in) { return GlobalTranspose{.raw = in.s8("semitones")}; }

  void execute(Runtime& rt) const { rt.out.globalTranspose(raw); }
};

struct Transpose : S8StateCommand<Transpose, &TrackState::transpose> {
  static constexpr std::string_view operandName = "semitones";
};

struct Tuning : S8Operand<Tuning> {
  static constexpr std::string_view operandName = "tuning";

  void describe(CommandInfo& out) const { out.field("cents", math::tuningCents(raw)); }

  void execute(Runtime& rt) const { rt.out.tuning(math::tuningCents(raw)); }
};

struct PortamentoTime : U8Operand<PortamentoTime> {
  static constexpr std::string_view operandName = "time";

  void execute(Runtime& rt) const {
    // The driver stores portamento as speed; the next note converts it to a
    // distance-dependent time using the previous source key.
    rt.state.portamentoMillisecondsPerCent = math::portamentoMillisecondsPerCent(raw);
  }
};

struct Tempo : Be16Operand<Tempo> {
  static constexpr std::string_view operandName = "raw";

  void describe(CommandInfo& out) const {
    out.field("microseconds_per_quarter", math::tempoMicrosecondsPerQuarter(raw));
  }

  void execute(Runtime& rt) const { rt.out.tempo(math::tempoMicrosecondsPerQuarter(raw)); }
};

struct DurationRate : U8StateCommand<DurationRate, &TrackState::durationRate> {
  static constexpr std::string_view operandName = "rate";
};

// Control flow.
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
      return rt.declaredLoop(destination);
    }

    // Capcom stores the number of replays. The VM helper receives total plays.
    return rt.vm.countedRepeatUntil(slot, static_cast<u32>(count) + 1, destination);
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
    const BranchResult branch = rt.vm.countedRepeatBreak(slot, destination);
    if (branch.taken) {
      rt.state.applyAttributes(attributes, &rt.out);
    }
    return branch.effects;
  }
};

// Program and mixer controls.
struct Volume : U8Operand<Volume> {
  static constexpr std::string_view operandName = "raw";

  void describe(CommandInfo& out, const Context& context) const {
    out.field("linear_gain", math::volumeGain(context.version, raw));
  }

  void execute(Runtime& rt) const { emitLinearVolume(rt, raw); }
};

struct Program : U8Operand<Program> {
  static constexpr std::string_view operandName = "raw";

  [[nodiscard]] u32 bank() const { return raw >> 7; }
  [[nodiscard]] u32 program() const { return raw & 0x7f; }

  void describe(CommandInfo& out) const {
    out.field("bank", bank());
    out.field("program", program());
  }

  void references(CommandReferences& out) const { out.instrument(bank(), program()); }

  void execute(Runtime& rt) const { rt.out.instrument(bank(), program(), true); }
};

struct Jump : Be16AddressOperand<Jump> {
  Address destination;

  Effects execute(Runtime& rt) const { return rt.loopCandidate(destination); }
};

struct End : NoOperands<End> {
  Effects execute(Runtime& rt) const { return rt.end(); }
};

struct Pan : U8Operand<Pan> {
  static constexpr std::string_view operandName = "raw";

  [[nodiscard]] ::capcom_snes::PanConversionResult conversion(const Context& context) const {
    return math::panConversion(context.version, raw);
  }

  void describe(CommandInfo& out, const Context& context) const {
    const auto pan = conversion(context);
    out.field("stereo_position", math::stereoPosition(pan));
    out.field("linear_gain", pan.volumeScale);
  }

  void execute(Runtime& rt) const { emitPan(rt, raw); }
};

struct MasterVolume : U8Operand<MasterVolume> {
  static constexpr std::string_view operandName = "raw";

  void describe(CommandInfo& out, const Context& context) const {
    out.field("linear_gain", math::volumeGain(context.version, raw));
  }

  void execute(Runtime& rt) const { emitLinearMasterVolume(rt, raw); }
};

// LFO and effects commands.
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
                          state.modulationRate != 0 ? math::midi7Amount(state.vibratoDepth) : 0.0);
        break;

      case 1:
        state.tremoloDepth = math::tremoloDepth(rt.context.version, value);
        rt.out.modulation(ModulationPerformanceTarget::TremoloDepth,
                          state.modulationRate != 0 ? math::midi7Amount(state.tremoloDepth) : 0.0);
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

        const double rate = math::lfoRateAmount(value);
        rt.out.modulation(ModulationPerformanceTarget::VibratoRate, rate);
        rt.out.modulation(ModulationPerformanceTarget::TremoloRate, rate);
        break;
      }

      default:
        // Type 3 is the driver's reset-LFO-phase flag. SF2/DLS already reset phase
        // on note activation, so there is nothing useful to emit yet.
        break;
    }
  }
};

// Source-only and diagnostic commands.
struct EchoParam : SourceOnlyCommand {
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

struct ReleaseRate : SourceOnlyCommand, U8Operand<ReleaseRate> {
  static constexpr std::string_view operandName = "raw";

  void describe(CommandInfo& out) const { out.field("gain", static_cast<u8>(raw | 0xa0)); }
};

struct Nop : NoOpCommand, NoOperands<Nop> {};

struct UnknownOneByte : SourceOnlyCommand {
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

// Source opcode table. This should stay compact enough to read like the
// driver's dispatch map.
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

[[nodiscard]] CapcomSnesSequenceDescriptor makeCapcomSnesSequenceDescriptor(CapcomSnesEngineVersion version) {
  SequenceDialectBuilder<TrackState, Context> builder{dialectId(version), Context{.version = version}};
  builder.timebase(Timebase{.ppqn = kCapcomSnesPpqn})
      .defaultBehavior(SequenceProgramBehavior{
          .defaultLoopPolicy = LoopPolicy::PlayOnce,
          .initialReverbSend = 0.0,
          .initialMonoModeChannels = 0,
      });
  auto bytecode = capcomBytecodeMap(builder, version);
  return CapcomSnesSequenceDescriptor{
      .dialect = builder.finish(),
      .bytecode = std::move(bytecode),
  };
}

}  // namespace

const CapcomSnesSequenceDescriptor& capcomSnesSequenceDescriptor(CapcomSnesEngineVersion version) {
  static const CapcomSnesSequenceDescriptor none = makeCapcomSnesSequenceDescriptor(CapcomSnesEngineVersion::none);
  static const CapcomSnesSequenceDescriptor v1 = makeCapcomSnesSequenceDescriptor(CapcomSnesEngineVersion::v1BgmInList);
  static const CapcomSnesSequenceDescriptor v2 =
      makeCapcomSnesSequenceDescriptor(CapcomSnesEngineVersion::v2BgmUsuallyAtFixedLocation);
  static const CapcomSnesSequenceDescriptor v3 =
      makeCapcomSnesSequenceDescriptor(CapcomSnesEngineVersion::v3BgmFixedLocation);

  switch (version) {
    case CapcomSnesEngineVersion::v1BgmInList:
      return v1;
    case CapcomSnesEngineVersion::v2BgmUsuallyAtFixedLocation:
      return v2;
    case CapcomSnesEngineVersion::v3BgmFixedLocation:
      return v3;
    case CapcomSnesEngineVersion::none:
      return none;
  }
  return none;
}

SequenceDialect capcomSnesSequenceDialect(CapcomSnesEngineVersion version) {
  return capcomSnesSequenceDescriptor(version).dialect;
}

void registerCapcomSnesSequenceDialects(SequenceDialectRegistry& registry) {
  registry.add(capcomSnesSequenceDescriptor(CapcomSnesEngineVersion::none).dialect);
  registry.add(capcomSnesSequenceDescriptor(CapcomSnesEngineVersion::v1BgmInList).dialect);
  registry.add(capcomSnesSequenceDescriptor(CapcomSnesEngineVersion::v2BgmUsuallyAtFixedLocation).dialect);
  registry.add(capcomSnesSequenceDescriptor(CapcomSnesEngineVersion::v3BgmFixedLocation).dialect);
}

TrackProgram decodeCapcomSnesSourceTrack(ByteReader reader, const CapcomSnesSequenceDescriptor& descriptor,
                                         u32 sourceTrackNumber, u32 startAddress) {
  const BytecodeDispatchTable& bytecode = descriptor.bytecode;
  return decodeLinearBytecodeTrack(reader, sourceTrackNumber, startAddress,
                                   LinearBytecodeDecodePolicy{.maxCommands = 4096},
                                   [&](u32 offset) { return bytecode.decode(reader, offset); });
}

SequenceProgramAsset parseCapcomSnesSequence(const ScanInput& input, const CapcomSnesLayout& layout, AssetId sequenceId,
                                             std::optional<AssetId> instrumentSetId, std::string_view displayName) {
  const u32 headerSize = (layout.priorityInHeader ? 1 : 0) + kCapcomSnesMaxTracks * 2;
  ItemTree items;
  ItemTreeBuilder itemBuilder(items, input.ids);
  const auto root = itemBuilder.add(std::nullopt, ItemKind::Sequence, "capcom-snes.sequence-header", "Sequence Header",
                                    input.reader.range(layout.sequenceHeaderAddress, headerSize));

  const CapcomSnesSequenceDescriptor& descriptor = capcomSnesSequenceDescriptor(layout.version);
  const SequenceDialect& dialect = descriptor.dialect;
  SequenceProgram program{
      .dialect = dialect.id,
      .timebase = dialect.timebase,
      .behavior = dialect.defaultBehavior,
  };

  const u32 pointerBase = layout.sequenceHeaderAddress + (layout.priorityInHeader ? 1 : 0);
  // Capcom stores track pointers in reverse channel order. Reorder them here so source
  // track numbers match the driver's playback order.
  for (int trackIndex = static_cast<int>(kCapcomSnesMaxTracks) - 1; trackIndex >= 0; --trackIndex) {
    const auto pointerOffset = pointerBase + static_cast<u32>(trackIndex) * 2;
    const u16 trackAddress = input.reader.be16(pointerOffset);
    if (trackAddress == 0) {
      continue;
    }

    const auto trackItem =
        itemBuilder.add(root, ItemKind::Track, "capcom-snes.track-pointer", "Track Pointer",
                        input.reader.range(pointerOffset, 2), fmt::format("Track starts at ${:04X}", trackAddress));
    auto track = decodeCapcomSnesSourceTrack(input.reader, descriptor,
                                             static_cast<u32>(kCapcomSnesMaxTracks - 1 - trackIndex), trackAddress);

    addSourceCommandItemsAndInstrumentReferences(itemBuilder, trackItem, program, dialect, track, instrumentSetId);

    program.tracks.push_back(std::move(track));
  }

  return SequenceProgramAsset{
      .metadata =
          AssetMetadata{
              .id = sequenceId,
              .format = "CapcomSnes",
              .name = std::string(displayName),
              .range = input.reader.range(layout.sequenceHeaderAddress, headerSize),
              .items = std::move(items),
          },
      .program = std::move(program),
  };
}

}  // namespace vgmtrans::formats::capcom_snes
