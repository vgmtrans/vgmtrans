/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/Akao/AkaoSequenceDecoder.h"

#include "value/base/LevelScale.h"
#include "value/formats/Akao/AkaoVersion.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fmt/format.h>
#include <optional>

namespace vgmtrans::formats::akao {

using namespace core;

namespace {

constexpr u8 kNoteVelocity = 127;
constexpr u16 kDeltaTimeTable[] = {192, 96, 48, 24, 12, 6, 3, 32, 16, 8, 4};

struct AkaoContext {
  AkaoProfile profile;
  AkaoSequenceAnalysis* analysis = nullptr;
};

struct AkaoRepeatStack {
  u8 layer = 0;
  std::array<Address, 4> begin{};

  void start(Address address) {
    layer = static_cast<u8>((layer + 1) & 3);
    begin[layer] = address;
  }

  [[nodiscard]] Address current() const { return begin[layer]; }

  void finishFallthrough() { layer = static_cast<u8>((layer - 1) & 3); }
};

struct AkaoTrackState {
  u8 octave = 4;
  s8 transpose = 0;
  s8 tuning = 0;
  bool slur = false;
  bool legato = false;
  bool portamento = false;
  bool drum = false;
  bool useOneTimeDuration = false;
  u8 oneTimeDuration = 0;
  u16 lastDeltaTime = 0;
  u16 fixedDuration = 0;
  u16 volume = 127;
  u16 expression = 127;
  u16 pan = 64;
  AkaoRepeatStack repeats;
  u32 microsecondsPerQuarter = 500000;
  std::optional<u8> previousKey;
};

[[nodiscard]] u8 percentPanToMidi(double percent) {
  u8 midiPan = static_cast<u8>(std::round(percent * 126.0));
  if (midiPan != 0) {
    ++midiPan;
  }
  return midiPan;
}

[[nodiscard]] u8 linearPan7ToMidi(u8 rawPan) {
  if (rawPan == 127) {
    ++rawPan;
  }
  const double percent = rawPan / 128.0;
  if (percent == 0.0) {
    return 0;
  }
  if (percent == 0.5) {
    return 64;
  }
  if (percent == 1.0) {
    return 127;
  }
  constexpr double halfPi = 1.57079632679489661923;
  return percentPanToMidi(std::atan2(percent, 1.0 - percent) / halfPi);
}

[[nodiscard]] double stereoPositionFromMidiPan(u8 midiPan) {
  return (midiPan / 127.0) * 2.0 - 1.0;
}

[[nodiscard]] u32 akaoMidiBank(u32 bankMsb) {
  return bankMsb << 7;
}

[[nodiscard]] u16 akaoZeroAs256(u8 rawValue) {
  return rawValue == 0 ? 256 : rawValue;
}

[[nodiscard]] double akaoLinearControllerGain(u8 value) {
  return LevelScale::linearFromLinear(value / 127.0);
}

[[nodiscard]] double akaoMillisecondsPerTick(u32 microsecondsPerQuarter) {
  return microsecondsPerQuarter / 1000.0 / static_cast<double>(kAkaoPpqn);
}

[[nodiscard]] double akaoTuningScale(s8 tuning) {
  const int divisor = tuning >= 0 ? 128 : 256;
  return tuning / static_cast<double>(divisor);
}

[[nodiscard]] double akaoTuningCents(s8 tuning) {
  return (akaoTuningScale(tuning) / std::log(2.0)) * 1200.0;
}

[[nodiscard]] double akaoTuningPitchBendSemitones(s8 tuning) {
  constexpr double legacyPitchBendRangeSemitones = 12.0;
  const double percent = akaoTuningScale(tuning) / std::log(2.0);
  const auto bend = static_cast<s16>(std::clamp(static_cast<int>(percent * 8192.0), -8192, 8191));
  return (bend / 8192.0) * legacyPitchBendRangeSemitones;
}

template <class Runtime>
void emitAkaoTimeSignature(Runtime& rt, VmCommandCursor& cmd) {
  // Legacy Akao stores the metronome click interval first and the numerator second.
  // The denominator is derived from the sequence PPQN to match old VGMTrans output.
  const u8 ticksPerBeat = cmd.u8("ticks_per_beat");
  const u8 beatsPerMeasure = cmd.u8("beats_per_measure");
  if (ticksPerBeat == 0 || beatsPerMeasure == 0) {
    return;
  }
  const u8 denominator = static_cast<u8>((kAkaoPpqn * 4) / ticksPerBeat);
  cmd.derived("numerator", beatsPerMeasure)
      .derived("denominator", denominator)
      .derived("midi_clocks_per_metronome_click", ticksPerBeat);
  rt.timeSignature(beatsPerMeasure, denominator, ticksPerBeat);
}

template <class Runtime, class Quantize, class Emit>
void emitAkaoControllerSlide(Runtime& rt, u16& previous, u16 target, u16 duration, Quantize quantize, Emit emit) {
  if (duration == 0) {
    previous = target;
    return;
  }

  u64 startTick = 0;
  if constexpr (requires { rt.vm.tick(); }) {
    startTick = rt.vm.tick();
  }

  const double increment =
      static_cast<double>(static_cast<int>(target) - static_cast<int>(previous)) / static_cast<double>(duration);
  int lastWritten = -1;
  for (u16 i = 0; i < duration; ++i) {
    int value = static_cast<int>(std::round(previous + (increment * (i + 1))));
    value = std::clamp(value, 0, 127);
    const int comparable = quantize(static_cast<u8>(value));
    if (comparable == lastWritten) {
      continue;
    }
    emit(rt, startTick + i, static_cast<u8>(value));
    lastWritten = comparable;
  }
  previous = target;
}

template <class Runtime>
[[nodiscard]] CommandFlow preserve(Runtime&, VmCommandCursor& cmd, std::string_view name, u32 operands,
                                   std::string_view kind = {}) {
  return cmd.preserve(name, operands, kind);
}

template <class Runtime>
struct AkaoRuntime {
  Runtime& rt;

  [[nodiscard]] const AkaoProfile& profile() const { return rt.context.profile; }
  [[nodiscard]] AkaoSequenceAnalysis* analysis() const { return rt.context.analysis; }

  void useIndividualArt(u32 art) {
    if (auto* out = analysis()) {
      out->usesIndividualArts = true;
      if (art != 0) {
        out->individualArtIds.insert(art);
      }
    }
  }

  void customInstrumentTable(Address table) {
    if (auto* out = analysis()) {
      out->customInstrumentOffsets.insert(table.value);
    }
  }

  void drumInstrumentTable(Address table) {
    if (auto* out = analysis()) {
      out->drumInstrumentOffsets.insert(table.value);
    }
  }

  [[nodiscard]] Address relativeAddress(VmCommandCursor& cmd, std::string_view name) const {
    const u32 operandOffset = cmd.absolutePosition();
    const s16 relative = cmd.s16le(name);
    const Address destination{profile().relativeDestination(operandOffset, relative)};
    cmd.derived(fmt::format("{}_absolute", name), destination.value, SourceValueDisplay::Address);
    return destination;
  }

  [[nodiscard]] CommandFlow jump(VmCommandCursor& cmd, Address destination) const {
    return destination.value <= cmd.address().value ? cmd.loopCandidate(destination) : cmd.jump(destination);
  }

  [[nodiscard]] CommandFlow conditionalBranch(VmCommandCursor& cmd, Address destination) const {
    return cmd.conditionalBranch(destination);
  }

  [[nodiscard]] CommandFlow artProgram(VmCommandCursor& cmd, std::string_view name) {
    cmd.name(name, SequenceSemantic::Program);
    const u8 art = cmd.u8("articulation");
    cmd.derived("bank", 0).derived("program", art).instrumentRef(0, art);
    useIndividualArt(art);
    rt.instrument(akaoMidiBank(0), art, true);
    return cmd.next();
  }

  [[nodiscard]] CommandFlow keySplitProgram(VmCommandCursor& cmd) {
    cmd.name("Program Change (Key-Split Instrument)", SequenceSemantic::Program);
    const u8 program = cmd.u8("program");
    cmd.derived("bank", 1).derived("program", program).instrumentRef(1, program);
    rt.instrument(akaoMidiBank(1), program, true);
    return cmd.next();
  }

  [[nodiscard]] CommandFlow sourceArtReference(VmCommandCursor& cmd, std::string_view name, u32 extraBytes = 0) {
    cmd.name(name, SequenceSemantic::Program).sourceOnly();
    const u8 art = cmd.u8("articulation");
    cmd.derived("bank", 0).derived("program", art).instrumentRef(0, art);
    useIndividualArt(art);
    if (extraBytes > 0) {
      static_cast<void>(cmd.rawBytes("bytes", extraBytes));
    }
    return cmd.next();
  }

  [[nodiscard]] CommandFlow customInstrumentTableCommand(VmCommandCursor& cmd) {
    cmd.name("Program Change (Key-Split Instrument)", SequenceSemantic::Program).sourceOnly();
    const Address table = relativeAddress(cmd, "relative");
    cmd.target(table, SourceLinkRole::JumpTarget);
    customInstrumentTable(table);
    return cmd.next();
  }

  [[nodiscard]] CommandFlow drumKitOn(VmCommandCursor& cmd, std::optional<Address> table = std::nullopt) {
    cmd.name("Drum Kit On", SequenceSemantic::Program);
    if (table) {
      cmd.target(*table, SourceLinkRole::JumpTarget);
      drumInstrumentTable(*table);
    }
    rt.instrument(akaoMidiBank(127), 127, true);
    rt.state.drum = true;
    return cmd.next();
  }

  [[nodiscard]] CommandFlow drumKitOff(VmCommandCursor& cmd) {
    cmd.name("Drum Kit Off", SequenceSemantic::Program);
    rt.state.drum = false;
    return cmd.next();
  }

  [[nodiscard]] CommandFlow tempo(VmCommandCursor& cmd) {
    cmd.name("Tempo", SequenceSemantic::Tempo);
    const u16 raw = cmd.u16le("tempo");
    rt.state.microsecondsPerQuarter = profile().tempoMicrosPerQuarter(raw);
    rt.tempo(rt.state.microsecondsPerQuarter);
    return cmd.next();
  }

  [[nodiscard]] CommandFlow setVolume(VmCommandCursor& cmd) {
    cmd.name("Volume", SequenceSemantic::Level);
    rt.state.volume = cmd.u8("volume");
    rt.level(akaoLinearControllerGain(static_cast<u8>(rt.state.volume)));
    return cmd.next();
  }

  [[nodiscard]] CommandFlow fadeVolume(VmCommandCursor& cmd) {
    cmd.name("Volume Fade", SequenceSemantic::Level);
    const u16 duration = akaoZeroAs256(cmd.u8("duration"));
    const u8 target = cmd.u8("target_volume");
    cmd.derived("duration_ticks", duration);
    emitAkaoControllerSlide(
        rt, rt.state.volume, target, duration,
        [](u8 value) { return LevelScale::midi7FromLinear(akaoLinearControllerGain(value)); },
        [](auto& runtime, u64 tick, u8 value) { runtime.levelAt(tick, akaoLinearControllerGain(value)); });
    return cmd.next();
  }

  [[nodiscard]] CommandFlow setExpression(VmCommandCursor& cmd) {
    cmd.name("Expression", SequenceSemantic::Level);
    rt.state.expression = cmd.u8("expression");
    rt.expression(akaoLinearControllerGain(static_cast<u8>(rt.state.expression)));
    return cmd.next();
  }

  [[nodiscard]] CommandFlow fadeExpression(VmCommandCursor& cmd) {
    cmd.name("Expression Fade", SequenceSemantic::Level);
    const u16 duration = akaoZeroAs256(cmd.u8("duration"));
    const u8 target = cmd.u8("target_expression");
    cmd.derived("duration_ticks", duration);
    emitAkaoControllerSlide(
        rt, rt.state.expression, target, duration,
        [](u8 value) { return LevelScale::midi7FromLinear(akaoLinearControllerGain(value)); },
        [](auto& runtime, u64 tick, u8 value) { runtime.expressionAt(tick, akaoLinearControllerGain(value)); });
    return cmd.next();
  }

  [[nodiscard]] CommandFlow setPan(VmCommandCursor& cmd) {
    cmd.name("Pan", SequenceSemantic::Pan);
    rt.state.pan = cmd.u8("pan");
    rt.pan(stereoPositionFromMidiPan(linearPan7ToMidi(static_cast<u8>(rt.state.pan))));
    return cmd.next();
  }

  [[nodiscard]] CommandFlow fadePan(VmCommandCursor& cmd) {
    cmd.name("Pan Fade", SequenceSemantic::Pan);
    const u16 duration = akaoZeroAs256(cmd.u8("duration"));
    const u8 target = cmd.u8("target_pan");
    cmd.derived("duration_ticks", duration);
    emitAkaoControllerSlide(
        rt, rt.state.pan, target, duration, [](u8 value) { return value; },
        [](auto& runtime, u64 tick, u8 value) { runtime.panAt(tick, stereoPositionFromMidiPan(value)); });
    return cmd.next();
  }

  [[nodiscard]] CommandFlow repeatStart(VmCommandCursor& cmd) {
    cmd.name("Repeat Start", SequenceSemantic::Repeat);
    rt.state.repeats.start(cmd.addressAtCursor());
    return cmd.next();
  }

  [[nodiscard]] CommandFlow repeatUntil(VmCommandCursor& cmd) {
    cmd.name("Repeat Until", SequenceSemantic::Repeat);
    const u16 count = akaoZeroAs256(cmd.u8("count"));
    const u8 slot = rt.state.repeats.layer;
    const Address target = rt.state.repeats.current();
    cmd.derived("count", count).target(target, SourceLinkRole::JumpTarget);
    CommandFlow flow = rt.countedRepeatUntil(cmd, slot, count, target);
    if (cmd.phase() == CommandPhase::Decode ||
        (flow.resolvedEffects && flow.resolvedEffects->step.kind == StepKind::Next)) {
      rt.state.repeats.finishFallthrough();
    }
    return flow;
  }

  [[nodiscard]] CommandFlow repeatAgain(VmCommandCursor& cmd) {
    cmd.name("Repeat Again", SequenceSemantic::Repeat);
    const Address target = rt.state.repeats.current();
    cmd.target(target, SourceLinkRole::JumpTarget);
    return cmd.loopCandidate(target);
  }

  [[nodiscard]] CommandFlow branchWithCondition(VmCommandCursor& cmd, std::string_view name,
                                                std::string_view conditionName) {
    cmd.name(name, SequenceSemantic::Jump);
    const u8 condition = cmd.u8(conditionName);
    const Address destination = relativeAddress(cmd, "relative");
    cmd.detail(conditionName, condition);
    return conditionalBranch(cmd, destination);
  }
};

template <class Runtime>
AkaoRuntime(Runtime&) -> AkaoRuntime<Runtime>;

template <class Runtime>
[[nodiscard]] CommandFlow readSubEvent(Runtime& rt, VmCommandCursor& cmd, u8 sub) {
  AkaoRuntime akao{rt};
  const AkaoProfile& profile = akao.profile();
  switch (sub) {
    case 0x00:
      return akao.tempo(cmd);
    case 0x04:
      if (profile.version3OrLater()) {
        return akao.drumKitOn(cmd);
      }
      return akao.drumKitOn(cmd, akao.relativeAddress(cmd, "relative"));
    case 0x05:
      return akao.drumKitOff(cmd);
    case 0x06: {
      cmd.name("Jump");
      return akao.jump(cmd, akao.relativeAddress(cmd, "relative"));
    }
    case 0x07:
      return akao.branchWithCondition(cmd, "CPU Conditional Jump", "condition");
    case 0x08:
      return akao.branchWithCondition(cmd, "Loop Branch", "count");
    case 0x09:
      return akao.branchWithCondition(cmd, "Loop Break", "count");
    case 0x0a:
      return akao.artProgram(cmd, "Program Change w/o Attack");
    case 0x0e: {
      if (profile.version32()) {
        cmd.name("Play Pattern");
        const Address destination = akao.relativeAddress(cmd, "relative");
        return cmd.call(destination);
      }
      return preserve(rt, cmd, "Unknown FE 0E", profile.subOperandBytes(sub), "unknown-fe-0e");
    }
    case 0x0f:
      if (profile.version32()) {
        cmd.name("End Pattern");
        return cmd.ret();
      }
      return preserve(rt, cmd, "Unknown FE 0F", profile.subOperandBytes(sub), "unknown-fe-0f");
    case 0x12:
      return akao.fadeVolume(cmd);
    case 0x14:
      if (profile.version3OrLater()) {
        return akao.keySplitProgram(cmd);
      }
      return akao.customInstrumentTableCommand(cmd);
    case 0x15:
      cmd.name("Time Signature");
      emitAkaoTimeSignature(rt, cmd);
      return cmd.next();
    default:
      return preserve(rt, cmd, fmt::format("Sub Event {:02X}", sub), profile.subOperandBytes(sub), "sub-event");
  }
}

struct AkaoCommandReader {
  template <class Runtime>
  static CommandFlow read(Runtime& rt, VmCommandCursor& cmd) {
    AkaoRuntime akao{rt};
    const AkaoProfile& profile = akao.profile();
    const u8 status = cmd.opcode();
    if (profile.isNoteOpcode(status)) {
      const bool noteWithLength = profile.noteHasInlineDuration(status);
      const u8 noteByte = noteWithLength ? static_cast<u8>((status - 0xf0) * 11) : status;
      const bool rest = noteByte >= 0x8f;
      const bool tie = !rest && noteByte >= 0x83;
      u32 delta = kDeltaTimeTable[noteByte % 11];
      if (noteWithLength) {
        delta = cmd.u8("duration");
      }
      if (rt.state.useOneTimeDuration) {
        delta = rt.state.oneTimeDuration;
        rt.state.useOneTimeDuration = false;
      }
      if (rt.state.fixedDuration != 0) {
        delta = rt.state.fixedDuration;
      }
      if (delta == 0) {
        delta = kDeltaTimeTable[noteByte % 11];
      }
      rt.state.lastDeltaTime = static_cast<u16>(delta);
      u32 sounding = delta;
      if (!profile.version3OrLater() && !rt.state.slur && !rt.state.legato) {
        sounding = delta > 2 ? delta - 2 : 0;
      }

      if (rest) {
        cmd.name("Rest", SequenceSemantic::Rest);
        return cmd.wait(delta);
      }
      if (tie) {
        cmd.name("Tie", SequenceSemantic::Note);
        if (rt.state.previousKey) {
          rt.note(*rt.state.previousKey, LevelScale::linearFromMidi7(kNoteVelocity), std::max<u32>(1, sounding), true);
        }
        return cmd.wait(delta);
      }

      const u8 relativeKey = noteByte / 11;
      const u8 sourceKey = rt.state.drum && !profile.version3OrLater()
                               ? static_cast<u8>(24 + relativeKey)
                               : static_cast<u8>(rt.state.octave * 12 + relativeKey);
      const u8 key = static_cast<u8>(std::clamp<int>(static_cast<int>(sourceKey) + rt.state.transpose, 0, 127));
      cmd.name("Note", SequenceSemantic::Note).derived("key", key, SourceValueDisplay::MidiNote);
      if (rt.state.portamento && rt.state.previousKey) {
        rt.portamentoControl(*rt.state.previousKey);
      }
      rt.note(key, LevelScale::linearFromMidi7(kNoteVelocity), std::max<u32>(1, sounding));
      rt.state.previousKey = key;
      return cmd.wait(delta);
    }

    if (status >= 0x9a && status <= 0x9f) {
      cmd.unsupported("Undefined Akao event");
      return cmd.stop();
    }

    if (profile.isSubEventPrefix(status)) {
      const u8 sub = cmd.u8("sub_event");
      return readSubEvent(rt, cmd, sub);
    }

    switch (status) {
      case 0xa0:
        cmd.name("End", SequenceSemantic::End, CommandPlaybackStatus::StopsPlayback);
        return cmd.end();
      case 0xa1:
        return akao.artProgram(cmd, "Program");
      case 0xa2:
        cmd.name("Next Note Length");
        rt.state.oneTimeDuration = cmd.u8("duration");
        rt.state.useOneTimeDuration = true;
        return cmd.next();
      case 0xa3:
        return akao.setVolume(cmd);
      case 0xa5:
        cmd.name("Octave");
        rt.state.octave = cmd.u8("octave");
        return cmd.next();
      case 0xa6:
        cmd.name("Octave Up");
        ++rt.state.octave;
        return cmd.next();
      case 0xa7:
        cmd.name("Octave Down");
        if (rt.state.octave > 0) {
          --rt.state.octave;
        }
        return cmd.next();
      case 0xa8:
        return akao.setExpression(cmd);
      case 0xa9:
        return akao.fadeExpression(cmd);
      case 0xaa:
        return akao.setPan(cmd);
      case 0xab:
        return akao.fadePan(cmd);
      case 0xc0:
        cmd.name("Transpose");
        rt.state.transpose = cmd.s8("semitones");
        return cmd.next();
      case 0xc1: {
        cmd.name("Transpose (Relative)");
        const s8 relative = cmd.s8("semitones");
        rt.state.transpose =
            static_cast<s8>(std::clamp<int>(static_cast<int>(rt.state.transpose) + relative, -128, 127));
        cmd.derived("transpose", rt.state.transpose);
        return cmd.next();
      }
      case 0xc2:
        cmd.name("Reverb On").sourceOnly();
        return cmd.next();
      case 0xc3:
        cmd.name("Reverb Off").sourceOnly();
        return cmd.next();
      case 0xc8:
        return akao.repeatStart(cmd);
      case 0xc9:
        return akao.repeatUntil(cmd);
      case 0xca:
        return akao.repeatAgain(cmd);
      case 0xcc:
        cmd.name("Slur On");
        rt.state.slur = true;
        return cmd.next();
      case 0xcd:
        cmd.name("Slur Off");
        rt.state.slur = false;
        return cmd.next();
      case 0xd0:
        cmd.name("Legato On");
        rt.state.legato = true;
        return cmd.next();
      case 0xd1:
        cmd.name("Legato Off");
        rt.state.legato = false;
        return cmd.next();
      case 0xd8: {
        cmd.name("Tuning", SequenceSemantic::Pitch);
        rt.state.tuning = cmd.s8("tuning");
        rt.pitchBend(akaoTuningPitchBendSemitones(rt.state.tuning));
        return cmd.next();
      }
      case 0xd9: {
        cmd.name("Tuning (Relative)", SequenceSemantic::Pitch);
        const s8 relative = cmd.s8("tuning");
        rt.state.tuning = static_cast<s8>(std::clamp<int>(static_cast<int>(rt.state.tuning) + relative, -128, 127));
        const double cents = akaoTuningCents(rt.state.tuning);
        cmd.derived("cents", cents);
        rt.tuning(cents);
        return cmd.next();
      }
      case 0xda: {
        cmd.name("Portamento On", SequenceSemantic::Portamento);
        const u8 rawSpeed = cmd.u8("speed");
        const u16 speed = rawSpeed == 0 ? 256 : rawSpeed;
        cmd.derived("ticks", speed);
        rt.portamentoTime(akaoMillisecondsPerTick(rt.state.microsecondsPerQuarter) * speed);
        rt.portamentoEnable(true);
        rt.state.portamento = true;
        return cmd.next();
      }
      case 0xdb:
        cmd.name("Portamento Off", SequenceSemantic::Portamento);
        rt.portamentoEnable(false);
        rt.state.portamento = false;
        return cmd.next();
      case 0xdc: {
        cmd.name("Fixed Note Length");
        const s8 relativeLength = cmd.s8("relative_length");
        rt.state.fixedDuration =
            static_cast<u16>(std::clamp<int>(static_cast<int>(rt.state.lastDeltaTime) + relativeLength, 1, 255));
        cmd.derived("duration", rt.state.fixedDuration);
        return cmd.next();
      }
      case 0xe8:
        if (profile.legacyFamily()) {
          return akao.tempo(cmd);
        }
        break;
      case 0xea:
        if (profile.legacyFamily()) {
          cmd.name("Reverb Depth").sourceOnly();
          const u16 depth = cmd.u16le("depth");
          cmd.detail("depth", depth);
          return cmd.next();
        }
        break;
      case 0xec:
        if (profile.legacyFamily()) {
          return akao.drumKitOn(cmd, akao.relativeAddress(cmd, "relative"));
        }
        break;
      case 0xed:
        if (profile.legacyFamily()) {
          return akao.drumKitOff(cmd);
        }
        break;
      case 0xee:
        if (profile.legacyFamily()) {
          cmd.name("Jump");
          return akao.jump(cmd, akao.relativeAddress(cmd, "relative"));
        }
        break;
      case 0xef:
        if (profile.legacyFamily()) {
          return akao.branchWithCondition(cmd, "CPU Conditional Jump", "condition");
        }
        break;
      case 0xf0:
        if (profile.legacyFamily()) {
          return akao.branchWithCondition(cmd, "Loop Branch", "count");
        }
        break;
      case 0xf1:
        if (profile.legacyFamily()) {
          return akao.branchWithCondition(cmd, "Loop Break", "count");
        }
        break;
      case 0xf2:
        if (profile.legacyFamily()) {
          return akao.artProgram(cmd, "Program Change w/o Attack");
        }
        break;
      case 0xf4:
        if (profile.version == AkaoPs1Version::Version1_0) {
          return akao.sourceArtReference(cmd, "Individual Art Event", 1);
        }
        break;
      case 0xfd:
        if (profile.legacyFamily()) {
          cmd.name("Time Signature");
          emitAkaoTimeSignature(rt, cmd);
          return cmd.next();
        }
        break;
      case 0xfc:
        if (profile.version == AkaoPs1Version::Version1_1) {
          return akao.customInstrumentTableCommand(cmd);
        }
        break;
      default:
        break;
    }

    return preserve(rt, cmd, "Akao Event", profile.directOperandBytes(status), "event");
  }
};

}  // namespace

namespace {

SequenceDialect makeAkaoDialect(AkaoPs1Version version, AkaoSequenceAnalysis* analysis) {
  return makeCursorDialect<AkaoTrackState, AkaoContext, AkaoCommandReader>(CursorDialectSpec<AkaoContext>{
      .id = dialectId(version),
      .commandKindPrefix = dialectId(version),
      .timebase = Timebase{.ppqn = kAkaoPpqn},
      .defaultBehavior =
          SequenceProgramBehavior{
              .defaultLoopPolicy = LoopPolicy::Default,
              .commandLimit = kAkaoMaxTrackCommands,
              .initialLevel = 1.0,
              .initialPitchBendRangeSemitones = 12,
          },
      .context = AkaoContext{.profile = akaoProfile(version), .analysis = analysis},
  });
}

}  // namespace

SequenceDialect makeAkaoDialect(AkaoPs1Version version) {
  return makeAkaoDialect(version, nullptr);
}

TrackProgram decodeAkaoTrack(ByteReader reader, const SequenceDialect& dialect, CursorTrackDecodeInput input) {
  return decodeCursorReachableTrack<AkaoTrackState, AkaoContext, AkaoCommandReader>(reader, dialect, input);
}

void analyzeAkaoTrack(ByteReader reader, AkaoSequenceAnalysis& analysis, u32 start) {
  const SequenceDialect dialect = makeAkaoDialect(analysis.header.version, &analysis);
  static_cast<void>(decodeCursorReachableTrack<AkaoTrackState, AkaoContext, AkaoCommandReader>(
      reader, dialect,
      CursorTrackDecodeInput{
          .startOffset = start,
          .bytecodeEnd = analysis.header.offset + analysis.header.length,
          .sequenceOffset = analysis.header.offset,
          .sequenceEnd = analysis.header.offset + analysis.header.length,
          .maxCommands = kAkaoMaxAnalysisCommands,
      }));
}

}  // namespace vgmtrans::formats::akao
