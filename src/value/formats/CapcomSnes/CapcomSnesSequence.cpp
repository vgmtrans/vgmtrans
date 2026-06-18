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

// Opcode implementations.
template <class Runtime>
void emitLinearVolume(Runtime& rt, u8 raw) {
  // Capcom volume is a linear amplitude gain. MIDI rendering applies the
  // square-root MIDI controller curve later.
  rt.level(LevelScale::linearFromLinear(math::volumeGain(rt.context.version, raw)), LevelPrecisionHint::FourteenBit);
}

template <class Runtime>
void emitLinearMasterVolume(Runtime& rt, u8 raw) {
  rt.masterLevel(LevelScale::linearFromLinear(math::volumeGain(rt.context.version, raw)));
}

template <class Runtime>
void emitPan(Runtime& rt, u8 raw) {
  const auto pan = math::panConversion(rt.context.version, raw);
  rt.pan(math::stereoPosition(pan), LevelScale::linearFromLinear(pan.volumeScale));
}

template <class Runtime>
void applyAttributes(Runtime& rt, u8 raw) {
  if constexpr (requires { rt.out; }) {
    rt.state.applyAttributes(raw, &rt.out);
  } else {
    rt.state.applyAttributes(raw);
  }
}

template <class Runtime>
void toggleSlur(Runtime& rt) {
  if constexpr (requires { rt.out; }) {
    rt.state.toggleSlur(rt.out);
  } else {
    rt.state.noteSlurred = !rt.state.noteSlurred;
  }
}

template <class Runtime>
void renderWarning(Runtime& rt, std::string message) {
  if constexpr (requires { rt.vm.diagnostic(Diagnostic{}); }) {
    rt.vm.diagnostic(Diagnostic{
        .severity = Severity::Warning,
        .message = std::move(message),
    });
  }
}

struct CapcomCursorReader {
  template <class Runtime>
  static CommandFlow read(VmCommandCursor& cmd, Runtime& rt) {
    const u8 opcode = cmd.opcode();
    if (opcode >= 0x20) {
      return (opcode & 0x1f) == 0 ? rest(cmd, rt, opcode) : note(cmd, rt, opcode);
    }

    switch (opcode) {
      case 0x00:
        return toggleTriplet(cmd, rt);
      case 0x01:
        return slur(cmd, rt);
      case 0x02:
        return dotted(cmd, rt);
      case 0x03:
        return toggleOctaveUp(cmd, rt);
      case 0x04:
        return noteAttributes(cmd, rt);
      case 0x05:
        return tempo(cmd, rt);
      case 0x06:
        return durationRate(cmd, rt);
      case 0x07:
        return volume(cmd, rt);
      case 0x08:
        return program(cmd, rt);
      case 0x09:
        return octave(cmd, rt);
      case 0x0a:
        return globalTranspose(cmd, rt);
      case 0x0b:
        return transpose(cmd, rt);
      case 0x0c:
        return tuning(cmd, rt);
      case 0x0d:
        return portamentoTime(cmd, rt);
      case 0x0e:
      case 0x0f:
      case 0x10:
      case 0x11:
        return repeatUntil(cmd, rt, opcode);
      case 0x12:
      case 0x13:
      case 0x14:
      case 0x15:
        return repeatBreak(cmd, rt, opcode);
      case 0x16:
        return jump(cmd);
      case 0x17:
        return cmd.name("End").kind("end").semantic(SequenceSemantic::End).end();
      case 0x18:
        return pan(cmd, rt);
      case 0x19:
        return masterVolume(cmd, rt);
      case 0x1a:
        return lfo(cmd, rt);
      case 0x1b:
        return echoParam(cmd);
      case 0x1c:
        return echoOnOff(cmd, rt);
      case 0x1d:
        return releaseRate(cmd);
      case 0x1e:
      case 0x1f:
        return rt.context.version == CapcomSnesEngineVersion::v1BgmInList ? unknownOneByte(cmd) : nop(cmd);
      default:
        return unknown(cmd, rt);
    }
  }

private:
  template <class Runtime>
  static CommandFlow rest(VmCommandCursor& cmd, Runtime& rt, u8 opcode) {
    cmd.name("Rest").semantic(SequenceSemantic::Rest);
    const auto rawDuration = static_cast<u8>(opcode >> 5);
    cmd.derived("duration_index", static_cast<u64>(rawDuration));
    const u32 length = rt.state.consumeNoteTicks(rawDuration);
    rt.state.didRest = true;
    return cmd.wait(length);
  }

  template <class Runtime>
  static CommandFlow note(VmCommandCursor& cmd, Runtime& rt, u8 opcode) {
    cmd.name("Note").semantic(SequenceSemantic::Note);
    const auto keyIndex = static_cast<u8>(opcode & 0x1f);
    const auto rawDuration = static_cast<u8>(opcode >> 5);
    cmd.derived("key_index", static_cast<u64>(keyIndex))
        .derived("duration_index", static_cast<u64>(rawDuration));

    auto& state = rt.state;
    const u32 length = state.consumeNoteTicks(rawDuration);
    const s32 key = state.sourceKey(keyIndex);
    const u32 duration = state.soundingTicks(length);
    if (state.extendsPreviousSlurredNote(key)) {
      // Repeating a key after a slurred note extends the previous note, even if
      // this command has already cleared the current slur bit.
      rt.note(state.performedKey(key), 1.0, duration, true);
      state.finishExtendedNote();
      return cmd.wait(length);
    }

    if constexpr (requires { rt.out; }) {
      state.emitPortamentoIfNeeded(key, rt.out);
    }
    // Slur is modeled as a one-tick overlap into the next source note.
    rt.note(state.performedKey(key), 1.0, duration + (state.noteSlurred ? 1u : 0u));
    state.finishNote(key);
    return cmd.wait(length);
  }

  template <class Runtime>
  static CommandFlow noteAttributes(VmCommandCursor& cmd, Runtime& rt) {
    cmd.name("Note Attributes").semantic(SequenceSemantic::State);
    const auto raw = cmd.u8("raw");
    if (raw) {
      applyAttributes(rt, raw.value);
    }
    return cmd.next();
  }

  template <class Runtime>
  static CommandFlow octave(VmCommandCursor& cmd, Runtime& rt) {
    cmd.name("Octave").semantic(SequenceSemantic::State);
    const auto value = cmd.u8("octave");
    if (value) {
      rt.state.noteOctave = value.value;
    }
    return cmd.next();
  }

  template <class Runtime>
  static CommandFlow toggleTriplet(VmCommandCursor& cmd, Runtime& rt) {
    cmd.name("Toggle Triplet").semantic(SequenceSemantic::State);
    rt.state.noteTriplet = !rt.state.noteTriplet;
    return cmd.next();
  }

  template <class Runtime>
  static CommandFlow slur(VmCommandCursor& cmd, Runtime& rt) {
    cmd.name("Toggle Slur").semantic(SequenceSemantic::State);
    toggleSlur(rt);
    return cmd.next();
  }

  template <class Runtime>
  static CommandFlow dotted(VmCommandCursor& cmd, Runtime& rt) {
    cmd.name("Dotted Note").semantic(SequenceSemantic::State);
    rt.state.noteDotted = true;
    return cmd.next();
  }

  template <class Runtime>
  static CommandFlow toggleOctaveUp(VmCommandCursor& cmd, Runtime& rt) {
    cmd.name("Toggle Octave Up").semantic(SequenceSemantic::State);
    rt.state.noteOctaveUp = !rt.state.noteOctaveUp;
    return cmd.next();
  }

  template <class Runtime>
  static CommandFlow globalTranspose(VmCommandCursor& cmd, Runtime& rt) {
    cmd.name("Global Transpose").semantic(SequenceSemantic::Pitch);
    const auto semitones = cmd.s8("semitones");
    if (semitones) {
      rt.globalTranspose(semitones.value);
    }
    return cmd.next();
  }

  template <class Runtime>
  static CommandFlow transpose(VmCommandCursor& cmd, Runtime& rt) {
    cmd.name("Transpose").semantic(SequenceSemantic::Pitch);
    const auto semitones = cmd.s8("semitones");
    if (semitones) {
      rt.state.transpose = semitones.value;
    }
    return cmd.next();
  }

  template <class Runtime>
  static CommandFlow tuning(VmCommandCursor& cmd, Runtime& rt) {
    cmd.name("Tuning").semantic(SequenceSemantic::Pitch);
    const auto raw = cmd.s8("tuning");
    if (raw) {
      cmd.detail("cents", math::tuningCents(raw.value));
      rt.tuning(math::tuningCents(raw.value));
    }
    return cmd.next();
  }

  template <class Runtime>
  static CommandFlow portamentoTime(VmCommandCursor& cmd, Runtime& rt) {
    cmd.name("Portamento Time").semantic(SequenceSemantic::Portamento);
    const auto raw = cmd.u8("time");
    if (raw) {
      // The driver stores portamento as speed; the next note turns it into time
      // using the distance from the previous source key.
      rt.state.portamentoMillisecondsPerCent = math::portamentoMillisecondsPerCent(raw.value);
    }
    return cmd.next();
  }

  template <class Runtime>
  static CommandFlow tempo(VmCommandCursor& cmd, Runtime& rt) {
    cmd.name("Tempo").semantic(SequenceSemantic::Tempo);
    const auto raw = cmd.u16be("raw");
    if (raw) {
      const u32 microseconds = math::tempoMicrosecondsPerQuarter(raw.value);
      cmd.detail("microseconds_per_quarter", static_cast<u64>(microseconds));
      rt.tempo(microseconds);
    }
    return cmd.next();
  }

  template <class Runtime>
  static CommandFlow durationRate(VmCommandCursor& cmd, Runtime& rt) {
    cmd.name("Duration Rate").semantic(SequenceSemantic::State);
    const auto rate = cmd.u8("rate");
    if (rate) {
      rt.state.durationRate = rate.value;
    }
    return cmd.next();
  }

  template <class Runtime>
  static CommandFlow repeatUntil(VmCommandCursor& cmd, Runtime& rt, u8 opcode) {
    cmd.name("Repeat Until").semantic(SequenceSemantic::Repeat);
    const auto slot = static_cast<u8>(opcode - 0x0e);
    cmd.derived("slot", static_cast<u64>(slot + 1));
    const auto count = cmd.u8("count");
    const auto destination = cmd.address16be("destination");
    if (count.value == 0) {
      return cmd.declaredLoop(destination.value);
    }

    // Capcom stores the number of replays. The VM helper receives total plays.
    return rt.countedRepeatUntil(cmd, slot, static_cast<u32>(count.value) + 1, destination.value);
  }

  template <class Runtime>
  static CommandFlow repeatBreak(VmCommandCursor& cmd, Runtime& rt, u8 opcode) {
    cmd.name("Repeat Break").semantic(SequenceSemantic::RepeatBreak);
    const auto slot = static_cast<u8>(opcode - 0x12);
    cmd.derived("slot", static_cast<u64>(slot + 1));
    const auto attributes = cmd.u8("attributes");
    const auto destination = cmd.address16be("destination");
    const RepeatBreakFlow branch = rt.countedRepeatBreak(cmd, slot, destination.value);
    if (attributes && branch.taken()) {
      applyAttributes(rt, attributes.value);
    }
    return branch;
  }

  template <class Runtime>
  static CommandFlow volume(VmCommandCursor& cmd, Runtime& rt) {
    cmd.name("Volume").semantic(SequenceSemantic::Level);
    const auto raw = cmd.u8("raw");
    if (raw) {
      cmd.detail("linear_gain", math::volumeGain(rt.context.version, raw.value));
      emitLinearVolume(rt, raw.value);
    }
    return cmd.next();
  }

  template <class Runtime>
  static CommandFlow program(VmCommandCursor& cmd, Runtime& rt) {
    cmd.name("Program").semantic(SequenceSemantic::Program);
    const auto raw = cmd.u8("raw");
    const u32 bank = raw.value >> 7;
    const u32 program = raw.value & 0x7f;
    cmd.derived("bank", static_cast<u64>(bank)).derived("program", static_cast<u64>(program));
    if (raw) {
      rt.instrument(bank, program, true);
    }
    return cmd.next();
  }

  static CommandFlow jump(VmCommandCursor& cmd) {
    cmd.name("Jump").semantic(SequenceSemantic::Jump);
    const auto destination = cmd.address16be("destination");
    return cmd.loopCandidate(destination.value);
  }

  template <class Runtime>
  static CommandFlow pan(VmCommandCursor& cmd, Runtime& rt) {
    cmd.name("Pan").semantic(SequenceSemantic::Pan);
    const auto raw = cmd.u8("raw");
    if (raw) {
      const auto converted = math::panConversion(rt.context.version, raw.value);
      cmd.detail("stereo_position", math::stereoPosition(converted))
          .detail("linear_gain", converted.volumeScale);
      emitPan(rt, raw.value);
    }
    return cmd.next();
  }

  template <class Runtime>
  static CommandFlow masterVolume(VmCommandCursor& cmd, Runtime& rt) {
    cmd.name("Master Volume").semantic(SequenceSemantic::Level);
    const auto raw = cmd.u8("raw");
    if (raw) {
      cmd.detail("linear_gain", math::volumeGain(rt.context.version, raw.value));
      emitLinearMasterVolume(rt, raw.value);
    }
    return cmd.next();
  }

  template <class Runtime>
  static CommandFlow lfo(VmCommandCursor& cmd, Runtime& rt) {
    cmd.name("LFO").kind("lfo").semantic(SequenceSemantic::Modulation);
    const auto type = cmd.u8("type");
    const auto value = cmd.u8("value");
    if (!type || !value) {
      return cmd.next();
    }

    auto& state = rt.state;
    switch (type.value) {
      case 0:
        state.vibratoDepth = value.value & 0x7f;
        rt.modulation(ModulationPerformanceTarget::VibratoDepth,
                      state.modulationRate != 0 ? math::midi7Amount(state.vibratoDepth) : 0.0);
        break;

      case 1:
        state.tremoloDepth = math::tremoloDepth(rt.context.version, value.value);
        rt.modulation(ModulationPerformanceTarget::TremoloDepth,
                      state.modulationRate != 0 ? math::midi7Amount(state.tremoloDepth) : 0.0);
        break;

      case 2: {
        const bool wasEnabled = state.modulationRate != 0;
        state.modulationRate = value.value;
        const bool isEnabled = state.modulationRate != 0;
        if constexpr (requires { rt.out; }) {
          if (!isEnabled && wasEnabled) {
            state.emitModulationDepths(rt.out, false);
          } else if (isEnabled && !wasEnabled) {
            state.emitModulationDepths(rt.out, true);
          }
        }

        const double rate = math::lfoRateAmount(value.value);
        rt.modulation(ModulationPerformanceTarget::VibratoRate, rate);
        rt.modulation(ModulationPerformanceTarget::TremoloRate, rate);
        break;
      }

      default:
        // Type 3 is the driver's reset-LFO-phase flag. SF2/DLS already reset phase
        // on note activation, so there is nothing useful to emit yet.
        break;
    }
    return cmd.next();
  }

  static CommandFlow echoParam(VmCommandCursor& cmd) {
    cmd.name("Echo Param").semantic(SequenceSemantic::Meta);
    static_cast<void>(cmd.u8("argument"));
    static_cast<void>(cmd.u8("preset"));
    return cmd.next();
  }

  template <class Runtime>
  static CommandFlow echoOnOff(VmCommandCursor& cmd, Runtime& rt) {
    cmd.name("Echo On/Off").semantic(SequenceSemantic::Meta);
    const auto raw = cmd.u8("raw");
    cmd.derived("enabled", static_cast<u64>(raw.value & 1));
    if (raw) {
      rt.reverb((raw.value & 1) != 0 ? 40.0 / 127.0 : 0.0);
    }
    return cmd.next();
  }

  static CommandFlow releaseRate(VmCommandCursor& cmd) {
    cmd.name("Release Rate").semantic(SequenceSemantic::Meta);
    const auto raw = cmd.u8("raw");
    if (raw) {
      cmd.derived("gain", static_cast<u64>(raw.value | 0xa0));
    }
    return cmd.next();
  }

  static CommandFlow nop(VmCommandCursor& cmd) {
    return cmd.name("No Operation").kind("nop").semantic(SequenceSemantic::Meta).next();
  }

  static CommandFlow unknownOneByte(VmCommandCursor& cmd) {
    cmd.name("Unknown One-Byte Event").kind("unknown-one-byte").semantic(SequenceSemantic::Meta);
    cmd.derived("opcode", static_cast<u64>(cmd.opcode()), SourceValueDisplay::Hex);
    static_cast<void>(cmd.u8("value"));
    return cmd.next();
  }

  template <class Runtime>
  static CommandFlow unknown(VmCommandCursor& cmd, Runtime& rt) {
    cmd.name("Unknown Opcode").kind("unknown").semantic(SequenceSemantic::Unsupported)
        .derived("opcode", static_cast<u64>(cmd.opcode()), SourceValueDisplay::Hex)
        .unsupported("Unknown Capcom SNES sequence opcode");
    renderWarning(rt, "Unknown Capcom SNES sequence opcode");
    return cmd.end();
  }
};

using CapcomCursorCommand = CursorBytecodeCommand<TrackState, Context, CapcomCursorReader>;

// Source opcode table. This should stay compact enough to read like the
// driver's dispatch map.
template <class Registrar>
[[nodiscard]] BytecodeDispatchTable capcomBytecodeMap(Registrar& registrar, CapcomSnesEngineVersion version) {
  BytecodeMapBuilder<TrackState, Context> map{"capcom-snes", registrar};

  for (u16 opcode = 0x20; opcode <= 0xff; ++opcode) {
    if ((opcode & 0x1f) == 0) {
      map.cursorOp<CapcomCursorCommand>(static_cast<u8>(opcode), commandMeta("rest", "Rest"));
    } else {
      map.cursorOp<CapcomCursorCommand>(static_cast<u8>(opcode), commandMeta("note", "Note"));
    }
  }

  map.cursorOp<0x00, CapcomCursorCommand>(commandMeta("toggle-triplet", "Toggle Triplet"));
  map.cursorOp<0x01, CapcomCursorCommand>(commandMeta("toggle-slur", "Toggle Slur"));
  map.cursorOp<0x02, CapcomCursorCommand>(commandMeta("dotted-note", "Dotted Note"));
  map.cursorOp<0x03, CapcomCursorCommand>(commandMeta("toggle-octave-up", "Toggle Octave Up"));
  map.cursorOp<0x04, CapcomCursorCommand>(commandMeta("note-attributes", "Note Attributes"));
  map.cursorOp<0x05, CapcomCursorCommand>(commandMeta("tempo", "Tempo"));
  map.cursorOp<0x06, CapcomCursorCommand>(commandMeta("duration-rate", "Duration Rate"));
  map.cursorOp<0x07, CapcomCursorCommand>(commandMeta("volume", "Volume"));
  map.cursorOp<0x08, CapcomCursorCommand>(commandMeta("program", "Program"));
  map.cursorOp<0x09, CapcomCursorCommand>(commandMeta("octave", "Octave"));
  map.cursorOp<0x0a, CapcomCursorCommand>(commandMeta("global-transpose", "Global Transpose"));
  map.cursorOp<0x0b, CapcomCursorCommand>(commandMeta("transpose", "Transpose"));
  map.cursorOp<0x0c, CapcomCursorCommand>(commandMeta("tuning", "Tuning"));
  map.cursorOp<0x0d, CapcomCursorCommand>(commandMeta("portamento-time", "Portamento Time"));
  map.cursorRange<0x0e, 0x11, CapcomCursorCommand>(
      commandMeta("repeat-until", "Repeat Until"),
      BytecodeCommandOptions{.playbackStatus = CommandPlaybackStatus::AffectsControlFlow});
  map.cursorRange<0x12, 0x15, CapcomCursorCommand>(
      commandMeta("repeat-break", "Repeat Break"),
      BytecodeCommandOptions{.playbackStatus = CommandPlaybackStatus::AffectsControlFlow});
  map.cursorOp<0x16, CapcomCursorCommand>(
      commandMeta("jump", "Jump"),
      BytecodeCommandOptions{.playbackStatus = CommandPlaybackStatus::AffectsControlFlow});
  map.cursorOp<0x17, CapcomCursorCommand>(
      commandMeta("end", "End"), BytecodeCommandOptions{.playbackStatus = CommandPlaybackStatus::StopsPlayback});
  map.cursorOp<0x18, CapcomCursorCommand>(commandMeta("pan", "Pan"));
  map.cursorOp<0x19, CapcomCursorCommand>(commandMeta("master-volume", "Master Volume"));
  map.cursorOp<0x1a, CapcomCursorCommand>(commandMeta("lfo", "LFO"));
  map.cursorOp<0x1b, CapcomCursorCommand>(
      commandMeta("echo-param", "Echo Param"),
      BytecodeCommandOptions{.playbackStatus = CommandPlaybackStatus::SourceOnly});
  map.cursorOp<0x1c, CapcomCursorCommand>(commandMeta("echo-on-off", "Echo On/Off"));
  map.cursorOp<0x1d, CapcomCursorCommand>(
      commandMeta("release-rate", "Release Rate"),
      BytecodeCommandOptions{.playbackStatus = CommandPlaybackStatus::SourceOnly});

  if (version == CapcomSnesEngineVersion::v1BgmInList) {
    map.cursorOp<0x1e, CapcomCursorCommand>(
        commandMeta("unknown-one-byte", "Unknown One-Byte Event"),
        BytecodeCommandOptions{.playbackStatus = CommandPlaybackStatus::SourceOnly});
    map.cursorOp<0x1f, CapcomCursorCommand>(
        commandMeta("unknown-one-byte", "Unknown One-Byte Event"),
        BytecodeCommandOptions{.playbackStatus = CommandPlaybackStatus::SourceOnly});
  } else {
    map.cursorOp<0x1e, CapcomCursorCommand>(
        commandMeta("nop", "No Operation"), BytecodeCommandOptions{.playbackStatus = CommandPlaybackStatus::NoOp});
    map.cursorOp<0x1f, CapcomCursorCommand>(
        commandMeta("nop", "No Operation"), BytecodeCommandOptions{.playbackStatus = CommandPlaybackStatus::NoOp});
  }

  map.cursorTruncated<CapcomCursorCommand>(
      commandMeta("truncated", "Truncated Command"),
      BytecodeCommandOptions{.playbackStatus = CommandPlaybackStatus::Unsupported});
  map.cursorUnknown<CapcomCursorCommand>(
      commandMeta("unknown", "Unknown Opcode"),
      BytecodeCommandOptions{.playbackStatus = CommandPlaybackStatus::Unsupported});
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
                                         u32 sourceTrackNumber, u32 startAddress, SourceMapBuilder* sourceMap,
                                         std::vector<Diagnostic>* diagnostics) {
  const BytecodeDispatchTable& bytecode = descriptor.bytecode;
  return decodeLinearBytecodeTrack(reader, sourceTrackNumber, startAddress,
                                   LinearBytecodeDecodePolicy{.maxCommands = 4096},
                                   [&](u32 offset) {
                                     return bytecode.decode(reader, offset,
                                                            BytecodeDecodeContext{
                                                                .bytecodeEnd = static_cast<u32>(reader.size()),
                                                                .dialectContext = &descriptor.dialect.context,
                                                                .sourceMap = sourceMap,
                                                                .diagnostics = diagnostics,
                                                            });
                                   });
}

SequenceProgramAsset parseCapcomSnesSequence(const ScanInput& input, const CapcomSnesLayout& layout, AssetId sequenceId,
                                             std::optional<ScanInstrumentSetRef> instrumentSet,
                                             std::string_view displayName, SourceMapBuilder* sourceMap,
                                             std::vector<Diagnostic>* diagnostics) {
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
  const std::optional<AssetId> instrumentSetId =
      instrumentSet ? std::optional<AssetId>{instrumentSet->id} : std::nullopt;

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
                                             static_cast<u32>(kCapcomSnesMaxTracks - 1 - trackIndex), trackAddress,
                                             sourceMap, diagnostics);

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
