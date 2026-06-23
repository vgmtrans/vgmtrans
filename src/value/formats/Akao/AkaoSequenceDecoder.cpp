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
  std::array<u16, 4> completedPlays{};

  void start(Address address) {
    layer = static_cast<u8>((layer + 1) & 3);
    begin[layer] = address;
    completedPlays[layer] = 0;
  }

  [[nodiscard]] Address current() const { return begin[layer]; }
  [[nodiscard]] u16 currentCompletedPlays() const { return completedPlays[layer]; }

  void completeCurrentPlay() { ++completedPlays[layer]; }
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
  double tempoBpm = 120.0;
  u32 microsecondsPerQuarter = 500000;
  std::optional<u8> previousKey;
  std::optional<u8> tieKey;
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

[[nodiscard]] u32 akaoMicrosPerQuarterFromBpm(double bpm) {
  return static_cast<u32>(std::round(60000000.0 / std::max(1.0, bpm)));
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

  const u64 startTick = rt.tick();

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
void recordIndividualArt(Runtime& rt, u32 art) {
  if (auto* out = rt.context.analysis) {
    out->usesIndividualArts = true;
    if (art != 0) {
      out->individualArtIds.insert(art);
    }
  }
}

template <class Runtime>
void recordCustomInstrumentTable(Runtime& rt, Address table) {
  if (auto* out = rt.context.analysis) {
    out->customInstrumentOffsets.insert(table.value);
  }
}

template <class Runtime>
void recordDrumInstrumentTable(Runtime& rt, Address table) {
  if (auto* out = rt.context.analysis) {
    out->drumInstrumentOffsets.insert(table.value);
  }
}

template <class Runtime>
[[nodiscard]] Address readRelativeAddress(Runtime& rt, VmCommandCursor& cmd, std::string_view name) {
  const u32 operandOffset = cmd.absolutePosition();
  const s16 relative = cmd.s16le(name);
  const Address destination{rt.context.profile.relativeDestination(operandOffset, relative)};
  cmd.derived(fmt::format("{}_absolute", name), destination.value, SourceValueDisplay::Address);
  return destination;
}

[[nodiscard]] CommandFlow jumpOrLoop(VmCommandCursor& cmd, Address destination) {
  return destination.value <= cmd.address().value ? cmd.loopCandidate(destination) : cmd.jump(destination);
}

template <class Runtime>
[[nodiscard]] CommandFlow programArt(Runtime& rt, VmCommandCursor& cmd, std::string_view name) {
  cmd.name(name, SequenceSemantic::Program);
  const u8 art = cmd.u8("articulation");
  cmd.derived("bank", 0).derived("program", art).instrumentRef(0, art);
  recordIndividualArt(rt, art);
  rt.instrument(akaoMidiBank(0), art, true);
  return cmd.next();
}

template <class Runtime>
[[nodiscard]] CommandFlow customInstrumentTableCommand(Runtime& rt, VmCommandCursor& cmd) {
  cmd.name("Program Change (Key-Split Instrument)", SequenceSemantic::Program).sourceOnly();
  const Address table = readRelativeAddress(rt, cmd, "relative");
  cmd.target(table, SourceLinkRole::PointsTo);
  recordCustomInstrumentTable(rt, table);
  return cmd.next();
}

template <class Runtime>
[[nodiscard]] CommandFlow drumKitOn(Runtime& rt, VmCommandCursor& cmd, std::optional<Address> table = std::nullopt) {
  cmd.name("Drum Kit On", SequenceSemantic::Program);
  if (table) {
    cmd.target(*table, SourceLinkRole::PointsTo);
    recordDrumInstrumentTable(rt, *table);
  }
  rt.instrument(akaoMidiBank(127), 127, true);
  rt.state.drum = true;
  return cmd.next();
}

template <class Runtime>
[[nodiscard]] CommandFlow drumKitOff(Runtime& rt, VmCommandCursor& cmd) {
  cmd.name("Drum Kit Off", SequenceSemantic::Program);
  rt.state.drum = false;
  return cmd.next();
}

template <class Runtime>
[[nodiscard]] CommandFlow tempo(Runtime& rt, VmCommandCursor& cmd) {
  cmd.name("Tempo", SequenceSemantic::Tempo);
  const u16 raw = cmd.u16le("tempo");
  rt.state.tempoBpm = rt.context.profile.tempoBpm(raw);
  rt.state.microsecondsPerQuarter = rt.context.profile.tempoMicrosPerQuarter(raw);
  rt.tempo(rt.state.microsecondsPerQuarter);
  return cmd.next();
}

template <class Runtime>
[[nodiscard]] CommandFlow tempoFade(Runtime& rt, VmCommandCursor& cmd) {
  cmd.name("Tempo Fade", SequenceSemantic::Tempo);
  const u16 duration = akaoZeroAs256(cmd.u8("duration"));
  const u16 raw = cmd.u16le("tempo");
  const double targetBpm = rt.context.profile.tempoBpm(raw);
  cmd.derived("duration_ticks", duration);

  const u64 startTick = rt.tick();
  const double increment = (targetBpm - rt.state.tempoBpm) / static_cast<double>(duration);
  for (u16 i = 0; i < duration; ++i) {
    const double bpm = rt.state.tempoBpm + (increment * (i + 1));
    rt.tempoAt(startTick + i, akaoMicrosPerQuarterFromBpm(bpm));
  }

  rt.state.tempoBpm = targetBpm;
  rt.state.microsecondsPerQuarter = rt.context.profile.tempoMicrosPerQuarter(raw);
  return cmd.next();
}

template <class Runtime>
[[nodiscard]] CommandFlow repeatStart(Runtime& rt, VmCommandCursor& cmd) {
  cmd.name("Repeat Start", SequenceSemantic::Repeat);
  rt.state.repeats.start(cmd.addressAtCursor());
  return cmd.next();
}

template <class Runtime>
[[nodiscard]] CommandFlow repeatUntil(Runtime& rt, VmCommandCursor& cmd) {
  cmd.name("Repeat Until", SequenceSemantic::Repeat);
  const u16 count = akaoZeroAs256(cmd.u8("count"));
  const u8 slot = rt.state.repeats.layer;
  const Address target = rt.state.repeats.current();
  cmd.derived("count", count);
  rt.state.repeats.completeCurrentPlay();
  RepeatUntilFlow flow = rt.countedRepeatUntil(cmd, slot, count, target);
  if (flow.fallsThrough()) {
    rt.state.repeats.finishFallthrough();
  }
  return flow;
}

template <class Runtime>
[[nodiscard]] CommandFlow repeatBranch(Runtime& rt, VmCommandCursor& cmd) {
  cmd.name("Loop Branch", SequenceSemantic::RepeatBreak);
  const u16 count = akaoZeroAs256(cmd.u8("count"));
  const Address destination = readRelativeAddress(rt, cmd, "relative");
  cmd.derived("count", count);
  return rt.conditionalFiniteBranch(cmd, destination, rt.state.repeats.currentCompletedPlays() + 1 == count);
}

template <class Runtime>
[[nodiscard]] CommandFlow repeatAgain(Runtime& rt, VmCommandCursor& cmd) {
  cmd.name("Repeat Again", SequenceSemantic::Repeat);
  const Address target = rt.state.repeats.current();
  return cmd.loopCandidate(target);
}

template <class Runtime>
[[nodiscard]] CommandFlow branchWithCondition(Runtime& rt, VmCommandCursor& cmd, std::string_view name,
                                              std::string_view conditionName) {
  cmd.name(name, SequenceSemantic::Jump);
  const u8 condition = cmd.u8(conditionName);
  const Address destination = readRelativeAddress(rt, cmd, "relative");
  cmd.detail(conditionName, condition);
  return cmd.conditionalBranch(destination);
}

template <class Runtime>
[[nodiscard]] CommandFlow readSubEvent(Runtime& rt, VmCommandCursor& cmd, u8 sub) {
  const AkaoProfile& profile = rt.context.profile;
  switch (sub) {
    case 0x00:
      return tempo(rt, cmd);
    case 0x01:
      return tempoFade(rt, cmd);
    case 0x04:
      if (profile.version3OrLater()) {
        return drumKitOn(rt, cmd);
      }
      return drumKitOn(rt, cmd, readRelativeAddress(rt, cmd, "relative"));
    case 0x05:
      return drumKitOff(rt, cmd);
    case 0x06: {
      cmd.name("Jump");
      return jumpOrLoop(cmd, readRelativeAddress(rt, cmd, "relative"));
    }
    case 0x07:
      return branchWithCondition(rt, cmd, "CPU Conditional Jump", "condition");
    case 0x08:
      return repeatBranch(rt, cmd);
    case 0x09:
      return branchWithCondition(rt, cmd, "Loop Break", "count");
    case 0x0a:
      return programArt(rt, cmd, "Program Change w/o Attack");
    case 0x0e: {
      if (profile.version32()) {
        cmd.name("Play Pattern");
        const Address destination = readRelativeAddress(rt, cmd, "relative");
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
    case 0x12: {
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
    case 0x14:
      if (profile.version3OrLater()) {
        cmd.name("Program Change (Key-Split Instrument)", SequenceSemantic::Program);
        const u8 program = cmd.u8("program");
        cmd.derived("bank", 1).derived("program", program).instrumentRef(1, program);
        rt.instrument(akaoMidiBank(1), program, true);
        return cmd.next();
      }
      return customInstrumentTableCommand(rt, cmd);
    case 0x15:
      cmd.name("Time Signature");
      emitAkaoTimeSignature(rt, cmd);
      return cmd.next();
    default:
      return preserve(rt, cmd, fmt::format("Sub Event {:02X}", sub), profile.subOperandBytes(sub), "sub-event");
  }
}

struct AkaoCursorReader {
  template <class Runtime>
  static CommandFlow read(Runtime& rt, VmCommandCursor& cmd) {
    const AkaoProfile& profile = rt.context.profile;
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
        rt.state.tieKey.reset();
        return cmd.wait(delta);
      }
      if (tie) {
        cmd.name("Tie", SequenceSemantic::Note);
        if (rt.state.tieKey) {
          rt.note(*rt.state.tieKey, LevelScale::linearFromMidi7(kNoteVelocity), std::max<u32>(1, sounding), true);
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
      rt.state.tieKey = key;
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
        return programArt(rt, cmd, "Program");
      case 0xa2:
        cmd.name("Next Note Length");
        rt.state.oneTimeDuration = cmd.u8("duration");
        rt.state.useOneTimeDuration = true;
        return cmd.next();
      case 0xa3:
        cmd.name("Volume", SequenceSemantic::Level);
        rt.state.volume = cmd.u8("volume");
        rt.level(akaoLinearControllerGain(static_cast<u8>(rt.state.volume)));
        return cmd.next();
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
        cmd.name("Expression", SequenceSemantic::Level);
        rt.state.expression = cmd.u8("expression");
        rt.expression(akaoLinearControllerGain(static_cast<u8>(rt.state.expression)));
        return cmd.next();
      case 0xa9: {
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
      case 0xaa:
        cmd.name("Pan", SequenceSemantic::Pan);
        rt.state.pan = cmd.u8("pan");
        rt.pan(stereoPositionFromMidiPan(linearPan7ToMidi(static_cast<u8>(rt.state.pan))));
        return cmd.next();
      case 0xab: {
        cmd.name("Pan Fade", SequenceSemantic::Pan);
        const u16 duration = akaoZeroAs256(cmd.u8("duration"));
        const u8 target = cmd.u8("target_pan");
        cmd.derived("duration_ticks", duration);
        emitAkaoControllerSlide(
            rt, rt.state.pan, target, duration, [](u8 value) { return value; },
            [](auto& runtime, u64 tick, u8 value) { runtime.panAt(tick, stereoPositionFromMidiPan(value)); });
        return cmd.next();
      }
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
        return repeatStart(rt, cmd);
      case 0xc9:
        return repeatUntil(rt, cmd);
      case 0xca:
        return repeatAgain(rt, cmd);
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
          return tempo(rt, cmd);
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
          return drumKitOn(rt, cmd, readRelativeAddress(rt, cmd, "relative"));
        }
        break;
      case 0xed:
        if (profile.legacyFamily()) {
          return drumKitOff(rt, cmd);
        }
        break;
      case 0xee:
        if (profile.legacyFamily()) {
          cmd.name("Jump");
          return jumpOrLoop(cmd, readRelativeAddress(rt, cmd, "relative"));
        }
        break;
      case 0xef:
        if (profile.legacyFamily()) {
          return branchWithCondition(rt, cmd, "CPU Conditional Jump", "condition");
        }
        break;
      case 0xf0:
        if (profile.legacyFamily()) {
          return repeatBranch(rt, cmd);
        }
        break;
      case 0xf1:
        if (profile.legacyFamily()) {
          return branchWithCondition(rt, cmd, "Loop Break", "count");
        }
        break;
      case 0xf2:
        if (profile.legacyFamily()) {
          return programArt(rt, cmd, "Program Change w/o Attack");
        }
        break;
      case 0xf4:
        if (profile.version == AkaoPs1Version::Version1_0) {
          cmd.name("Overlay Voice On", SequenceSemantic::Program);
          const u8 primaryArt = cmd.u8("primary_articulation");
          const u8 secondaryArt = cmd.u8("secondary_articulation");
          cmd.derived("bank", 0).derived("program", primaryArt).instrumentRef(0, primaryArt);
          recordIndividualArt(rt, primaryArt);
          recordIndividualArt(rt, secondaryArt);
          rt.instrument(akaoMidiBank(0), primaryArt, true);
          return cmd.next();
        }
        break;
      case 0xf5:
        if (profile.version == AkaoPs1Version::Version1_0) {
          cmd.name("Overlay Voice Off").sourceOnly();
          return cmd.next();
        }
        break;
      case 0xf6:
        if (profile.version == AkaoPs1Version::Version1_0) {
          cmd.name("Overlay Volume Balance").sourceOnly();
          cmd.detail("balance", cmd.u8("balance"));
          return cmd.next();
        }
        break;
      case 0xf7:
        if (profile.version == AkaoPs1Version::Version1_0) {
          cmd.name("Overlay Volume Balance Fade").sourceOnly();
          const u16 duration = akaoZeroAs256(cmd.u8("duration"));
          const u8 balance = cmd.u8("balance");
          cmd.derived("duration_ticks", duration).detail("balance", balance);
          return cmd.next();
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
          return customInstrumentTableCommand(rt, cmd);
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
  return makeCursorDialect<AkaoTrackState, AkaoContext, AkaoCursorReader>(CursorDialectSpec<AkaoContext>{
      .id = dialectId(version),
      .commandDetailKindPrefix = dialectId(version),
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
  return decodeCursorReachableTrack<AkaoTrackState, AkaoContext, AkaoCursorReader>(reader, dialect, input);
}

void analyzeAkaoTrack(ByteReader reader, AkaoSequenceAnalysis& analysis, u32 start) {
  const SequenceDialect dialect = makeAkaoDialect(analysis.header.version, &analysis);
  static_cast<void>(decodeCursorReachableTrack<AkaoTrackState, AkaoContext, AkaoCursorReader>(
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
