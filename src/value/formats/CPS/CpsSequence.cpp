/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/CPS/Cps.h"

#include "value/formats/CPS/CpsTables.h"
#include "value/sequence/CommandSourceMap.h"
#include "value/sequence/CompiledCommandDialect.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <string>

namespace vgmtrans::formats::cps {

using namespace core;

namespace {

constexpr u32 kMaxTrackCommands = 262144;

enum class SynthKind : u8 {
  Ym2151,
  OkiM6295,
  QSound,
};

struct StereoGains {
  double left = 1.0;
  double right = 1.0;
};

[[nodiscard]] StereoGains qsoundPanGains(u8 position) {
  if (position <= 16) {
    return StereoGains{.left = 1.0, .right = position / 16.0};
  }
  return StereoGains{.left = (32 - std::min<u8>(position, 32)) / 16.0, .right = 1.0};
}

[[nodiscard]] StereoGains earlyPanGains(u8 raw, CpsVersion version) {
  if (!isCps1(version)) {
    return qsoundPanGains(std::min<u8>(raw, 32));
  }
  const double right = std::min<u8>(raw, 32) / 32.0;
  return StereoGains{.left = 1.0 - right, .right = right};
}

[[nodiscard]] StereoGains latePanGains(u8 raw, CpsVersion version) {
  if (!isCps3(version)) {
    const u8 position = raw == 0x7f ? 32 : static_cast<u8>(raw >> 2);
    return qsoundPanGains(position);
  }
  if (raw <= 0x3f) {
    return StereoGains{.left = 1.0, .right = raw / 64.0};
  }
  return StereoGains{.left = (0x7f - raw) / 64.0, .right = 1.0};
}

[[nodiscard]] u32 tempoFromDriverTicks(u16 ticksPerIteration, double driverRate) {
  if (ticksPerIteration == 0 || driverRate <= 0.0) {
    return 60'000'000;
  }
  const double iterationsPerQuarter = (kCpsPpqn * 256.0) / ticksPerIteration;
  return static_cast<u32>(
      std::clamp<double>(std::lround(iterationsPerQuarter / driverRate * 1'000'000.0), 1.0, 60'000'000.0));
}

[[nodiscard]] SynthKind trackSynth(CpsVersion version, u32 sourceTrackNumber) {
  if (!isCps1(version)) {
    return SynthKind::QSound;
  }
  if (version == CpsVersion::Cps1V100 || sourceTrackNumber < 8) {
    return SynthKind::Ym2151;
  }
  return SynthKind::OkiM6295;
}

struct ProgramState {
  explicit ProgramState(const SequenceProgram& program)
      : version(static_cast<CpsVersion>(program.config.profile)),
        masterVolume(static_cast<u8>(program.config.driverState & 0xff)) {}

  CpsVersion version = CpsVersion::Unknown;
  u8 masterVolume = 127;
  u32 tempoMicrosecondsPerQuarter = 500000;
};

struct TrackState {
  TrackState(const SequenceProgram& program, const TrackProgram& trackProgram)
      : version(static_cast<CpsVersion>(program.config.profile)),
        synth(trackSynth(version, trackProgram.sourceTrackNumber)), trackStart(trackProgram.startAddress),
        noteDuration(version == CpsVersion::Cps1V100 ? 0 : 0xff) {}

  CpsVersion version = CpsVersion::Unknown;
  SynthKind synth = SynthKind::QSound;
  Address trackStart;
  u8 noteDuration = 0;
  u8 noteState = 0;
  u8 bank = 0;
  s32 transpose = 0;
  s32 transposeAdjustment = 0;
  s32 patchTranspose = 0;
  s32 cps1V1Transpose = 0;
  u8 shortenCount = 0;
  bool extendNext = false;
  u8 tieCount = 0;
  bool held = false;
  std::optional<double> previousKey;
  PerformanceNoteId previousNote;
  u16 portamentoRate = 0;
  u8 lateExpression = 0x40;
  s16 lateVolumeBaseAdjustment = 0;
  s16 lateVolumeAdjustment = 0;
  bool conditional = false;
  bool resetLfoOnNote = false;
  u8 lfoRate = 0;
};

struct Playback {
  TrackState& track;
  PerformanceEmitter& out;
  VmApi& vm;
  ProgramState& program;

  [[nodiscard]] LfoPerformanceContext lfoContext() const {
    return LfoPerformanceContext{
        .frequencyHz = cpsDriverRateHertz(track.version) * tables::lfoRate[track.lfoRate] / 262144.0,
        .waveform = LfoWaveform::Triangle,
        .initialPhaseCycles = 0.0,
        .sampleImmediatelyOnNote = true,
        .phaseRunsAtZeroDepth = !isCps3(track.version),
    };
  }

  void emitLfoRate() {
    const auto context = lfoContext();
    const double hertz = context.frequencyHz.value_or(0.0);
    out.vibratoRate(hertz, context);
    out.tremoloRate(hertz, context);
  }

  void setVibrato(u8 raw) {
    out.vibratoDepth(tables::vibratoDepth[raw & 0x7f] / 256.0, lfoContext());
    emitLfoRate();
  }

  void setTremolo(u8 raw) {
    out.tremoloLinearGainDepth(tables::tremoloDepth[raw & 0x7f] / 65536.0, lfoContext());
    emitLfoRate();
  }

  void setLfoRate(u8 raw) {
    track.lfoRate = raw & 0x7f;
    emitLfoRate();
  }

  void setResetLfo(u8 raw) { track.resetLfoOnNote = raw != 0; }

  [[nodiscard]] double ticksPerSecond() const {
    return kCpsPpqn * 1'000'000.0 / std::max<u32>(1, program.tempoMicrosecondsPerQuarter);
  }

  void emitPortamento(PerformanceNoteId note, double key) {
    if (!track.held || !track.previousKey || !track.previousNote.valid() ||
        std::abs(*track.previousKey - key) < 0.0001) {
      return;
    }
    if (track.portamentoRate == 0) {
      out.pitchSlide(note, *track.previousKey, key, PitchSlideTiming::fromTicks(0))
          .continueFrom(track.previousNote)
          .preferPortamento();
      return;
    }
    const double semitonesPerSecond = track.portamentoRate * 2.0 / 256.0 * cpsDriverRateHertz(track.version);
    const double seconds = std::abs(*track.previousKey - key) / semitonesPerSecond;
    const u32 timelineTicks = std::max<u32>(1, static_cast<u32>(std::ceil(seconds * ticksPerSecond())));
    out.pitchSlide(note, *track.previousKey, key, PitchSlideTiming::fixedRate(timelineTicks, semitonesPerSecond))
        .continueFrom(track.previousNote)
        .preferPortamento();
  }

  void emitLateHeldTransition(PerformanceNoteId note, double key) {
    if (track.portamentoRate != 0) {
      emitPortamento(note, key);
      return;
    }
    out.pitchSlide(note, *track.previousKey, key, PitchSlideTiming::fromTicks(0))
        .continueFrom(track.previousNote)
        .preferPitchBend();
  }

  void emitInstrument(u32 key) {
    if (track.synth == SynthKind::Ym2151) {
      out.instrument(InstrumentIdentity{.domain = std::string(kCps1Ym2151Domain), .key = key});
    } else if (track.synth == SynthKind::OkiM6295) {
      out.instrument(InstrumentIdentity{.domain = std::string(kCps1OkiDomain), .key = key});
    } else {
      out.instrument(InstrumentIdentity{.domain = std::string(kCpsQSoundDomain), .key = key});
    }
  }

  void programChange(u8 raw, s8 patchTranspose) {
    if (isCps1(track.version)) {
      emitInstrument(raw & 0x7f);
      track.patchTranspose = track.synth == SynthKind::Ym2151 ? patchTranspose : 0;
      return;
    }
    const u32 addressBank = track.bank * 2 + raw / 128;
    emitInstrument(addressBank * 128 + raw % 128);
  }

  void lateProgramChange(u8 raw) { emitInstrument((track.bank * 2) * 128 + (raw & 0x7f)); }

  void bankCommand(u8 raw) {
    if (track.version < CpsVersion::Cps2V116) {
      emitInstrument((2 + raw / 128) * 128 + raw % 128);
    } else {
      track.bank = raw;
    }
  }

  void tempo(u32 microsecondsPerQuarter) {
    program.tempoMicrosecondsPerQuarter = microsecondsPerQuarter;
    out.tempo(microsecondsPerQuarter);
  }

  void masterVolume(u8 raw) {
    program.masterVolume = raw;
    out.masterLevel(raw / 127.0);
  }

  [[nodiscard]] u32 earlyDelta(u8 status) {
    u32 table = 0;
    if ((track.noteState & 0x30) == 0) {
      table = 1;
    } else if ((track.noteState & 0x10) == 0) {
      table = 0;
    } else {
      track.noteState &= ~0x10;
      table = 2;
    }
    return tables::delta[table][((status >> 5) & 7) - 1];
  }

  [[nodiscard]] Effects earlyNote(u8 status) {
    const u32 delta = earlyDelta(status);
    const u8 note = status & 0x1f;
    if (note == 0) {
      return Effects::wait(delta);
    }

    double key;
    if (track.synth == SynthKind::OkiM6295) {
      if (track.version == CpsVersion::Cps1V500 || track.version == CpsVersion::Cps1V502) {
        const u32 programNumber = note + tables::octave[track.noteState & 0x0f] - 1;
        emitInstrument(programNumber);
      }
      key = 60.0;
    } else {
      key = note + tables::octave[track.noteState & 0x0f] - 1 + track.transpose;
      if (isCps1(track.version)) {
        key += 12 + track.patchTranspose;
      }
    }
    key = std::clamp(key, 0.0, 127.0);

    const bool tied = (track.noteState & 0x40) != 0;
    u32 duration = std::max<u32>(1, (delta * track.noteDuration) >> 8);
    if (isCps1(track.version)) {
      duration = std::max<u32>(1, duration - 1);
    }
    if (tied) {
      duration = delta;
    }
    const bool extends = track.held && track.previousKey && std::abs(*track.previousKey - key) < 0.0001;
    const bool restart = track.resetLfoOnNote && !extends;
    const auto played = out.note(NotePerformanceEvent{
        .key = key,
        .linearVelocity = program.masterVolume / 127.0,
        .durationTicks = duration,
        .extendsPrevious = extends,
        .restartsLfoPhase = restart,
        .restartsVibratoLfoPhase = restart,
        .restartsTremoloLfoPhase = restart,
    });
    emitPortamento(played, key);
    track.previousKey = key;
    track.previousNote = played;
    track.held = tied;
    return Effects::wait(delta);
  }

  [[nodiscard]] Effects cps1V1Note(u8 status) {
    int shift = static_cast<int>((status >> 5) & 7) - 2;
    u32 delta = 3u << std::max(0, shift);
    if (track.shortenCount != 0) {
      delta = delta * 2 / 3;
      --track.shortenCount;
    }
    if (track.extendNext) {
      delta += delta / 2;
      track.extendNext = false;
    }
    const u8 note = status & 0x1f;
    if (note == 0) {
      track.held = false;
      return Effects::wait(delta);
    }
    double key = note + track.cps1V1Transpose + track.patchTranspose;
    while (key > 96) {
      key -= 12;
    }
    while (key < 0) {
      key += 12;
    }
    key += 12;
    key = std::clamp(key, 0.0, 127.0);
    const bool tied = track.tieCount > 1;
    if (track.tieCount != 0) {
      --track.tieCount;
    }
    const u32 duration = tied ? delta : std::max<u32>(1, (delta * track.noteDuration) >> 8);
    const bool extends = track.held && track.previousKey && std::abs(*track.previousKey - key) < 0.0001;
    const auto played = out.note(NotePerformanceEvent{
        .key = key,
        .linearVelocity = program.masterVolume / 127.0,
        .durationTicks = duration,
        .extendsPrevious = extends,
    });
    track.previousKey = key;
    track.previousNote = played;
    track.held = tied;
    return Effects::wait(delta);
  }

  void v1TieCount(u8 count) { track.tieCount = static_cast<u8>(count + 1); }
  void v1Shorten(u8 count) { track.shortenCount = count; }

  void earlyVolume(u8 raw) {
    if (track.synth == SynthKind::OkiM6295) {
      constexpr std::array<u8, 16> attenuation{32, 22, 16, 11, 8, 6, 4, 3, 2, 0, 0, 0, 0, 0, 0, 0};
      const u8 index = static_cast<u8>(8 - raw) & 0x0f;
      out.level(attenuation[index] / 32.0, LevelPrecisionHint::SevenBit);
    } else if (isCps1(track.version)) {
      out.level(std::min<u8>(raw, 127) / 127.0, LevelPrecisionHint::SevenBit);
    } else {
      out.level(tables::earlyVolume[raw & 0x7f] / 8191.0, LevelPrecisionHint::FourteenBit);
    }
  }

  void lateVolume(u8 raw) {
    const double level = isCps3(track.version) ? raw / 128.0 : tables::earlyVolume[raw & 0x7f] / 8191.0;
    out.level(level, LevelPrecisionHint::FourteenBit);
  }

  void emitLateExpression() {
    const double expression = isCps3(track.version)
                                  ? (track.lateExpression == 0 ? 0.0 : (track.lateExpression + 1) / 128.0)
                                  : tables::qsoundExpression[track.lateExpression & 0x7f] / 512.0;
    const s32 combinedAdjustment = track.lateVolumeBaseAdjustment + track.lateVolumeAdjustment;
    const double adjustment = isCps3(track.version) ? cpsVolumeAdjustmentGain(combinedAdjustment)
                                                    : (std::clamp<s32>(combinedAdjustment, -64, 63) + 64) / 64.0;
    out.expression(expression * adjustment, LevelPrecisionHint::FourteenBit);
  }

  void lateExpression(u8 raw) {
    track.lateExpression = raw;
    emitLateExpression();
  }

  void setVolumeAdjustment(s8 raw) {
    track.lateVolumeBaseAdjustment = raw;
    if (isCps3(track.version)) {
      track.lateVolumeAdjustment = 0;
    }
    emitLateExpression();
  }

  void addVolumeAdjustment(s8 raw) {
    if (isCps3(track.version)) {
      track.lateVolumeBaseAdjustment = static_cast<s16>(track.lateVolumeBaseAdjustment + raw);
    } else {
      track.lateVolumeAdjustment = static_cast<s16>(std::clamp<s32>(track.lateVolumeAdjustment + raw, -64, 63));
    }
    emitLateExpression();
  }

  void setTranspose(s8 raw) {
    track.transpose = raw;
    if (isCps3(track.version)) {
      track.transposeAdjustment = 0;
    }
  }

  void addTranspose(s8 raw) {
    if (isCps3(track.version)) {
      track.transpose += raw;
    } else {
      track.transposeAdjustment = std::clamp<s32>(track.transposeAdjustment + raw, -64, 63);
    }
  }

  [[nodiscard]] Effects lateWait(u32 ticks) { return Effects::wait(ticks); }

  [[nodiscard]] Effects lateNote(u8 velocity, u8 encodedKey, u32 duration) {
    const bool hold = (encodedKey & 0x80) != 0;
    const bool continuesPreviousVoice = track.held && track.previousKey && track.previousNote.valid();
    if (continuesPreviousVoice) {
      static_cast<void>(out.setPreviousNoteEnd(vm.tick()));
    }
    const double key =
        std::clamp<double>((encodedKey & 0x7f) + track.transpose + track.transposeAdjustment, 0.0, 127.0);
    const bool extendsPrevious = continuesPreviousVoice && std::abs(*track.previousKey - key) < 0.0001;
    const bool restart = track.resetLfoOnNote && !continuesPreviousVoice;
    const auto note = out.note(NotePerformanceEvent{
        .key = key,
        .linearVelocity = std::min(velocity * 2, 127) / 127.0,
        .durationTicks = std::max<u32>(1, duration),
        .extendsPrevious = extendsPrevious,
        .restartsLfoPhase = restart,
        .restartsVibratoLfoPhase = restart,
        .restartsTremoloLfoPhase = restart,
    });
    if (continuesPreviousVoice && !extendsPrevious) {
      emitLateHeldTransition(note, key);
    }
    track.previousKey = key;
    track.previousNote = note;
    track.held = hold;
    return {};
  }

  void latePortamento(u8 raw) {
    track.portamentoRate = raw == 0 ? 0 : static_cast<u16>(raw) + (isCps3(track.version) ? 1 : 0);
  }

  [[nodiscard]] Effects repeatBreak(u8 slot, Address destination) {
    return vm.countedRepeatBreak(slot, destination).effects;
  }

  [[nodiscard]] Effects conditionalStartRepeat() {
    if (!track.conditional) {
      track.conditional = true;
      return Effects::overrideWith(vm.jump(track.trackStart));
    }
    return {};
  }

  [[nodiscard]] Effects conditionalEnd() { return track.conditional ? Effects::overrideWith(vm.end()) : Effects{}; }

  [[nodiscard]] Effects branchIfFirst(Address destination) {
    if (!track.conditional) {
      track.conditional = true;
      return Effects::overrideWith(vm.finiteBranch(destination));
    }
    return {};
  }

  [[nodiscard]] Effects branchIfRepeated(Address destination) {
    return track.conditional ? Effects::overrideWith(vm.finiteBranch(destination)) : Effects{};
  }

  void meta(u8 slot, u8 value) {
    out.marker(MarkerPerformanceEvent{.text = "CPS Meta " + std::to_string(slot) + "=" + std::to_string(value)});
  }
};

using Cursor = CompilerCursor<TrackState, Playback>;

[[nodiscard]] Address relative16(u32 next, u16 raw) {
  return Address{static_cast<u32>(static_cast<s64>(next) + static_cast<s16>(raw))};
}

[[nodiscard]] DecodedBytecodeCommand decodeCps1V1Command(ByteReader reader, u32 offset, u32 programBase,
                                                         const std::vector<s8>& transposes,
                                                         std::vector<Diagnostic>* diagnostics) {
  Cursor cursor(reader, offset, "cps.cps1-v1", diagnostics);
  if (!cursor.hasOpcode()) {
    return cursor.truncated();
  }
  const u8 opcode = cursor.opcode();
  if (opcode >= 0x40) {
    auto event = cursor.command("Note", (opcode & 0x1f) == 0 ? SequenceSemantic::Rest : SequenceSemantic::Note);
    event.opcodeValue("note_index", opcode & 0x1f, SourceValueDisplay::Default, SemanticOperandRole::NoteKey);
    return event.invoke<&Playback::cps1V1Note>(opcode);
  }
  if (opcode >= 0x20) {
    if (opcode >= 0x30) {
      auto event = cursor.command("Shorten Events", SequenceSemantic::State);
      const u8 count =
          event.opcodeValue("count", opcode & 0x0f, SourceValueDisplay::Default, SemanticOperandRole::Count);
      return event.invoke<&Playback::v1Shorten>(count);
    }
    auto event = cursor.command("Tie Notes", SequenceSemantic::State);
    const u8 count = event.opcodeValue("count", opcode & 0x0f, SourceValueDisplay::Default, SemanticOperandRole::Count);
    return event.invoke<&Playback::v1TieCount>(count);
  }

  switch (opcode) {
    case 0x00: {
      auto event = cursor.command("Tempo", SequenceSemantic::Tempo);
      const u8 raw = event.u8("tempo");
      const u8 adjusted = static_cast<u8>(raw + 0x80);
      const u16 encoded = static_cast<u16>(((adjusted >> 3) << 8) | ((adjusted >> 2) << 7));
      const u32 mpq = event.derived("microseconds_per_quarter", static_cast<u32>(encoded) << 7);
      return event.invoke<&Playback::tempo>(mpq);
    }
    case 0x01: {
      auto event = cursor.command("Duration", SequenceSemantic::State);
      return event.set<&TrackState::noteDuration>(event.u8("duration", SemanticOperandRole::Duration));
    }
    case 0x02: {
      auto event = cursor.command("Repeat Break", SequenceSemantic::RepeatBreak);
      const auto stored = event.rawU16le("stored_destination", SourceValueDisplay::Address);
      const Address destination = event.resolvedValue("destination", stored, Address{programBase + stored.value},
                                                      SourceValueDisplay::Address, SemanticOperandRole::RepeatTarget);
      event.mayBranchTo(destination);
      return event.invoke<&Playback::repeatBreak>(0, destination);
    }
    case 0x03:
    case 0x04:
    case 0x05: {
      auto event = cursor.command("Repeat Until", SequenceSemantic::Repeat);
      const u8 count = event.u8("count", SemanticOperandRole::Count);
      const auto stored = event.rawU16le("stored_destination", SourceValueDisplay::Address);
      const Address destination = event.resolvedValue("destination", stored, Address{programBase + stored.value},
                                                      SourceValueDisplay::Address, SemanticOperandRole::RepeatTarget);
      const u8 slot = opcode - 3;
      return count == 0 ? event.declaredLoop(destination)
                        : event.repeatUntil(slot, static_cast<u32>(count) + 1, destination);
    }
    case 0x06: {
      auto event = cursor.command("Dotted Next Event", SequenceSemantic::State);
      return event.set<&TrackState::extendNext>(true);
    }
    case 0x07: {
      auto event = cursor.command("Transpose", SequenceSemantic::Pitch);
      return event.set<&TrackState::cps1V1Transpose>(event.s8("semitones"));
    }
    case 0x08: {
      auto event = cursor.command("Tuning", SequenceSemantic::Pitch);
      const s8 raw = event.s8("tuning");
      const double semitones = raw / 256.0;
      event.derived("cents", semitones * 100.0, SourceValueDisplay::Cents);
      return event.emitPitchBend(semitones);
    }
    case 0x09:
    case 0x0a:
    case 0x0e:
      return cursor.ignored("Driver State", 1);
    case 0x0b:
      return cursor.ignored("Driver State", 2);
    case 0x0c: {
      auto event = cursor.command("Program Change", SequenceSemantic::Program);
      const u8 program = event.u8("program", SemanticOperandRole::Instrument);
      const s8 transpose = program < transposes.size() ? transposes[program] : 0;
      return event.invoke<&Playback::programChange>(program, transpose);
    }
    case 0x0d: {
      auto event = cursor.command("Volume", SequenceSemantic::Level);
      const s8 adjustment = event.s8("adjustment");
      const double gain = std::clamp((127 + adjustment) / 127.0, 0.0, 1.0);
      return event.emitLevel(gain);
    }
    case 0x0f:
      return cursor.command("End", SequenceSemantic::End).end();
    default:
      return cursor.unsupported("Unknown Command").stop();
  }
}

[[nodiscard]] Address earlyRelativeDestination(Cursor::Event& event, SemanticOperandRole role) {
  const u8 first = event.u8("relative_high", SourceValueDisplay::Hex);
  Address destination;
  if ((first & 0x80) == 0) {
    destination = Address{event.nextAddress().value - first};
  } else {
    const u8 low = event.u8("relative_low", SourceValueDisplay::Hex);
    destination = relative16(event.nextAddress().value, static_cast<u16>((first << 8) | low));
  }
  return event.derived("destination", destination, SourceValueDisplay::Address, role);
}

[[nodiscard]] DecodedBytecodeCommand decodeEarlyCommand(ByteReader reader, u32 offset, CpsVersion version,
                                                        u32 programBase, const std::vector<s8>& transposes,
                                                        std::vector<Diagnostic>* diagnostics) {
  Cursor cursor(reader, offset, "cps.early", diagnostics);
  if (!cursor.hasOpcode()) {
    return cursor.truncated();
  }
  const u8 opcode = cursor.opcode();
  if (opcode >= 0x20) {
    auto event = cursor.command("Note", (opcode & 0x1f) == 0 ? SequenceSemantic::Rest : SequenceSemantic::Note);
    event.opcodeValue("note_index", opcode & 0x1f, SourceValueDisplay::Default, SemanticOperandRole::NoteKey);
    return event.invoke<&Playback::earlyNote>(opcode);
  }

  switch (opcode) {
    case 0x00:
      return cursor.command("Toggle Duration Table", SequenceSemantic::State).invoke([](Playback& playback) {
        playback.track.noteState ^= 0x20;
      });
    case 0x01: {
      auto event = cursor.command("Toggle Tie", SequenceSemantic::State);
      return event.invoke([](Playback& playback) { playback.track.noteState ^= 0x40; });
    }
    case 0x02: {
      auto event = cursor.command("Next Duration Table", SequenceSemantic::State);
      return event.invoke([](Playback& playback) { playback.track.noteState |= 0x10; });
    }
    case 0x03: {
      auto event = cursor.command("Toggle Octave Bank", SequenceSemantic::State);
      return event.invoke([](Playback& playback) { playback.track.noteState ^= 8; });
    }
    case 0x04: {
      auto event = cursor.command("Note State", SequenceSemantic::State);
      const u8 state = event.u8("state", SourceValueDisplay::Hex);
      return event.invoke(
          [](Playback& playback, u8 value) {
            playback.track.noteState = static_cast<u8>((playback.track.noteState & 0x97) | value);
          },
          state);
    }
    case 0x05: {
      auto event = cursor.command("Tempo", SequenceSemantic::Tempo);
      u32 mpq;
      if (!isCps1(version) && version >= CpsVersion::Cps2V140) {
        const u8 bpm = event.u8("beats_per_minute");
        mpq = bpm == 0 ? 60'000'000 : 60'000'000 / bpm;
      } else {
        mpq = tempoFromDriverTicks(event.u16be("ticks_per_iteration"), kCps2DriverRateHertz);
      }
      event.derived("microseconds_per_quarter", mpq);
      return event.invoke<&Playback::tempo>(mpq);
    }
    case 0x06: {
      auto event = cursor.command("Duration", SequenceSemantic::State);
      return event.set<&TrackState::noteDuration>(event.u8("duration", SemanticOperandRole::Duration));
    }
    case 0x07: {
      auto event = cursor.command("Volume", SequenceSemantic::Level);
      return event.invoke<&Playback::earlyVolume>(event.u8("volume", SemanticOperandRole::Level));
    }
    case 0x08: {
      auto event = cursor.command("Program Change", SequenceSemantic::Program);
      const u8 raw = event.u8("program", SemanticOperandRole::Instrument);
      const s8 transpose = raw < transposes.size() ? transposes[raw] : 0;
      return event.invoke<&Playback::programChange>(raw, transpose);
    }
    case 0x09: {
      auto event = cursor.command("Octave", SequenceSemantic::State);
      const u8 octave = event.u8("octave");
      return event.invoke(
          [](Playback& playback, u8 value) {
            playback.track.noteState = static_cast<u8>((playback.track.noteState & 0xf8) | value);
          },
          octave);
    }
    case 0x0a: {
      auto event = cursor.command("Global Transpose", SequenceSemantic::Pitch);
      return event.emitGlobalTranspose(event.s8("semitones"));
    }
    case 0x0b: {
      auto event = cursor.command("Transpose", SequenceSemantic::Pitch);
      return event.set<&TrackState::transpose>(event.s8("semitones"));
    }
    case 0x0c: {
      auto event = cursor.command("Pitch Bend", SequenceSemantic::Pitch);
      const s8 raw = event.s8("pitch_bend");
      return event.emitPitchBend(raw / 128.0 * 0.5);
    }
    case 0x0d: {
      auto event = cursor.command("Portamento Rate", SequenceSemantic::Portamento);
      return event.set<&TrackState::portamentoRate>(event.u8("rate"));
    }
    case 0x0e:
    case 0x0f:
    case 0x10:
    case 0x11: {
      auto event = cursor.command("Repeat Until", SequenceSemantic::Repeat);
      const u8 count = event.u8("count", SemanticOperandRole::Count);
      Address destination;
      if (isCps1(version) && version <= CpsVersion::Cps1V425) {
        const auto stored = event.rawU16be("stored_destination", SourceValueDisplay::Address);
        destination = event.resolvedValue("destination", stored, Address{programBase + stored.value},
                                          SourceValueDisplay::Address, SemanticOperandRole::RepeatTarget);
      } else {
        destination = earlyRelativeDestination(event, SemanticOperandRole::RepeatTarget);
      }
      const u8 slot = opcode - 0x0e;
      return count == 0 ? event.declaredLoop(destination)
                        : event.repeatUntil(slot, static_cast<u32>(count) + 1, destination);
    }
    case 0x12:
    case 0x13:
    case 0x14:
    case 0x15: {
      auto event = cursor.command("Repeat Break", SequenceSemantic::RepeatBreak);
      const u8 slot = opcode - 0x12;
      static_cast<void>(event.u8("note_state", SourceValueDisplay::Hex));
      const u16 raw = event.u16be("stored_relative_destination", SourceValueDisplay::Address);
      const Address destination = isCps1(version) && version <= CpsVersion::Cps1V425
                                      ? Address{programBase + raw}
                                      : relative16(event.nextAddress().value, raw);
      event.derived("destination", destination, SourceValueDisplay::Address, SemanticOperandRole::RepeatTarget);
      event.mayBranchTo(destination);
      return event.invoke<&Playback::repeatBreak>(slot, destination);
    }
    case 0x16: {
      auto event = cursor.command("Jump", SequenceSemantic::Jump);
      const u16 raw = event.u16be("stored_destination", SourceValueDisplay::Address);
      const Address destination = isCps1(version) && version <= CpsVersion::Cps1V425
                                      ? Address{programBase + raw}
                                      : relative16(event.nextAddress().value, raw);
      event.derived("destination", destination, SourceValueDisplay::Address, SemanticOperandRole::LoopTarget);
      return event.declaredLoop(destination);
    }
    case 0x17:
      return cursor.command("End", SequenceSemantic::End).end();
    case 0x18: {
      auto event = cursor.command("Pan", SequenceSemantic::Pan);
      const u8 raw = event.u8("pan", SemanticOperandRole::Pan);
      const auto gains = earlyPanGains(raw, version);
      return event.emitStereoBalance(gains.left, gains.right);
    }
    case 0x19:
      return cursor.ignored(isCps1(version) ? "Effect" : "QSound Effect Depth", 1);
    case 0x1a: {
      auto event = cursor.command("Master Volume", SequenceSemantic::Level);
      const u8 raw = event.u8("volume", SemanticOperandRole::Level);
      return isCps1(version) ? event.invoke<&Playback::masterVolume>(raw) : event.ignore();
    }
    case 0x1b: {
      if (isCps1(version)) {
        return cursor.ignored("YM2151 Vibrato Control", 1);
      }
      if (version < CpsVersion::Cps2V171) {
        auto event = cursor.command("Vibrato Depth", SequenceSemantic::Modulation);
        return event.invoke<&Playback::setVibrato>(event.u8("depth", SemanticOperandRole::Modulation));
      }
      auto event = cursor.command("LFO Control", SequenceSemantic::Modulation);
      const u8 target = event.u8("target");
      const u8 value = event.u8("value", SemanticOperandRole::Modulation);
      switch (target) {
        case 0:
          return event.invoke<&Playback::setVibrato>(value);
        case 1:
          return event.invoke<&Playback::setTremolo>(value);
        case 2:
          return event.invoke<&Playback::setLfoRate>(value);
        case 3:
          return event.invoke<&Playback::setResetLfo>(value);
        default:
          return event.ignore();
      }
    }
    case 0x1c: {
      if (isCps1(version)) {
        return cursor.ignored("YM2151 Tremolo Control", 1);
      }
      if (version < CpsVersion::Cps2V171) {
        auto event = cursor.command("Tremolo Depth", SequenceSemantic::Modulation);
        return event.invoke<&Playback::setTremolo>(event.u8("depth", SemanticOperandRole::Modulation));
      }
      return cursor.ignored("LFO Control", 2);
    }
    case 0x1d: {
      if (isCps1(version) || version >= CpsVersion::Cps2V171) {
        return cursor.ignored("LFO Driver State", 1);
      }
      auto event = cursor.command("LFO Rate", SequenceSemantic::Modulation);
      return event.invoke<&Playback::setLfoRate>(event.u8("rate", SemanticOperandRole::Modulation));
    }
    case 0x1e: {
      if (isCps1(version) || version >= CpsVersion::Cps2V171) {
        return cursor.ignored("LFO Driver State", 1);
      }
      auto event = cursor.command("Reset LFO On Note", SequenceSemantic::Modulation);
      return event.invoke<&Playback::setResetLfo>(event.u8("enabled"));
    }
    case 0x1f: {
      if (isCps1(version)) {
        return cursor.ignored("Driver State", 1);
      }
      auto event = cursor.command("Instrument Bank", SequenceSemantic::Program);
      return event.invoke<&Playback::bankCommand>(event.u8("bank", SemanticOperandRole::InstrumentBank));
    }
    default:
      return cursor.ignored("Driver State", 1);
  }
}

[[nodiscard]] DecodedBytecodeCommand decodeLateCommand(ByteReader reader, u32 offset, CpsVersion version,
                                                       std::array<Address, 4>& loopStarts,
                                                       std::vector<Diagnostic>* diagnostics) {
  Cursor cursor(reader, offset, "cps.late", diagnostics);
  if (!cursor.hasOpcode()) {
    return cursor.truncated();
  }
  const u8 opcode = cursor.opcode();
  if (opcode < 0x80) {
    auto event = cursor.command("Wait", SequenceSemantic::Wait);
    u32 ticks = opcode;
    while (event.peekU8() && *event.peekU8() < 0x80) {
      ticks = (ticks << 7) | event.u8("continuation", SemanticOperandRole::Duration);
    }
    event.derived("ticks", ticks, SemanticOperandRole::Duration);
    return event.invoke<&Playback::lateWait>(ticks);
  }
  if (opcode < 0xc0) {
    auto event = cursor.command("Note", SequenceSemantic::Note);
    const u8 velocity =
        event.opcodeValue("velocity", opcode & 0x3f, SourceValueDisplay::Default, SemanticOperandRole::Level);
    const u8 note = event.u8("note", SourceValueDisplay::MidiNote, SemanticOperandRole::NoteKey);
    const u32 duration = event.varLen("duration", SemanticOperandRole::Duration);
    return event.invoke<&Playback::lateNote>(velocity, note, duration);
  }

  switch (opcode) {
    case 0xc0:
      return cursor.noOp("No Operation");
    case 0xc1: {
      auto event = cursor.command("Tempo", SequenceSemantic::Tempo);
      const u16 raw = event.u16be("ticks_per_iteration");
      const u32 mpq = event.derived("microseconds_per_quarter", tempoFromDriverTicks(raw, cpsDriverRateHertz(version)));
      return event.invoke<&Playback::tempo>(mpq);
    }
    case 0xc2: {
      auto event = cursor.command("Instrument Bank", SequenceSemantic::Program);
      const u8 bank = event.u8("bank", SemanticOperandRole::InstrumentBank) & 0x0f;
      return event.set<&TrackState::bank>(bank);
    }
    case 0xc3: {
      auto event = cursor.command("Pitch Bend", SequenceSemantic::Pitch);
      const s8 raw = event.s8("pitch_bend");
      return event.emitPitchBend(raw * 12.0 / 128.0);
    }
    case 0xc4: {
      auto event = cursor.command("Program Change", SequenceSemantic::Program);
      return event.invoke<&Playback::lateProgramChange>(event.u8("program", SemanticOperandRole::Instrument));
    }
    case 0xc5: {
      auto event = cursor.command("Vibrato Depth", SequenceSemantic::Modulation);
      return event.invoke<&Playback::setVibrato>(event.u8("depth", SemanticOperandRole::Modulation));
    }
    case 0xc6: {
      auto event = cursor.command("Volume", SequenceSemantic::Level);
      return event.invoke<&Playback::lateVolume>(event.u8("volume", SemanticOperandRole::Level));
    }
    case 0xc7: {
      auto event = cursor.command("Pan", SequenceSemantic::Pan);
      const u8 raw = event.u8("pan", SemanticOperandRole::Pan);
      const auto gains = latePanGains(raw, version);
      return event.emitStereoBalance(gains.left, gains.right);
    }
    case 0xc8: {
      auto event = cursor.command("Expression", SequenceSemantic::Level);
      return event.invoke<&Playback::lateExpression>(event.u8("expression", SemanticOperandRole::Level));
    }
    case 0xc9: {
      auto event = cursor.command("Portamento Rate", SequenceSemantic::Portamento);
      return event.invoke<&Playback::latePortamento>(event.u8("rate"));
    }
    case 0xca:
      return cursor.command("Conditional Restart", SequenceSemantic::Jump)
          .invoke<&Playback::conditionalStartRepeat>()
          .runtimeControlFlow();
    case 0xcb:
      return cursor.command("Conditional End", SequenceSemantic::End)
          .invoke<&Playback::conditionalEnd>()
          .runtimeControlFlow();
    case 0xcc:
    case 0xcd: {
      auto event = cursor.command(opcode == 0xcc ? "Branch On First Pass" : "Branch On Repeat", SequenceSemantic::Jump);
      const u16 raw = event.u16be("stored_relative_destination", SourceValueDisplay::Address);
      // CPS3's CD handler sign-extends each byte separately; CC uses a normal signed word.
      const s32 displacement = opcode == 0xcd && isCps3(version)
                                   ? static_cast<s8>(raw >> 8) * 256 + static_cast<s8>(raw & 0xff)
                                   : static_cast<s16>(raw);
      const Address destination{static_cast<u32>(event.nextAddress().value + displacement)};
      event.derived("destination", destination, SourceValueDisplay::Address, SemanticOperandRole::JumpTarget);
      event.mayBranchTo(destination);
      return opcode == 0xcc ? event.invoke<&Playback::branchIfFirst>(destination)
                            : event.invoke<&Playback::branchIfRepeated>(destination);
    }
    case 0xce: {
      auto event = cursor.command("Jump", SequenceSemantic::Jump);
      const u16 relative = event.u16be("stored_relative_destination", SourceValueDisplay::Address);
      const Address destination = relative16(event.nextAddress().value, relative);
      event.derived("destination", destination, SourceValueDisplay::Address, SemanticOperandRole::LoopTarget);
      return event.declaredLoop(destination);
    }
    case 0xcf: {
      auto event = cursor.command("Switch Track Cursor", SequenceSemantic::State);
      event.u8("track", SemanticOperandRole::State);
      event.warning("Cross-track cursor switching is not represented by the sequence VM");
      return event.ignore();
    }
    case 0xd0:
    case 0xd1:
    case 0xd2:
    case 0xd3: {
      auto event = cursor.command("Repeat Start", SequenceSemantic::Repeat);
      const u8 slot = opcode - 0xd0;
      loopStarts[slot] = event.nextAddress();
      event.derived("slot", slot + 1);
      return event;
    }
    case 0xd4:
    case 0xd5:
    case 0xd6:
    case 0xd7: {
      auto event = cursor.command("Repeat Until", SequenceSemantic::Repeat);
      const u8 slot = opcode - 0xd4;
      const u8 count = event.u8("count", SemanticOperandRole::Count);
      const Address destination = loopStarts[slot];
      if (!destination.value) {
        event.warning("CPS repeat has no discovered start");
        return event.ignore();
      }
      const Address next = event.nextAddress();
      // CPS3 uses a terminal 127-pass top-level repeat as a practical infinity.
      const bool practicalLoop =
          isCps3(version) && slot == 0 && count == 0x7e && reader.has(next.value, 1) && reader.u8At(next.value) == 0xff;
      event.derived("destination", destination, SourceValueDisplay::Address, SemanticOperandRole::RepeatTarget);
      return count == 0 || practicalLoop ? event.declaredLoop(destination)
                                         : event.repeatUntil(slot, static_cast<u32>(count) + 1, destination);
    }
    case 0xd8:
    case 0xd9:
    case 0xda:
    case 0xdb: {
      auto event = cursor.command("Repeat Break", SequenceSemantic::RepeatBreak);
      const u8 slot = opcode - 0xd8;
      const u16 relative = event.u16be("stored_relative_destination", SourceValueDisplay::Address);
      const s32 displacement = isCps3(version) ? relative : static_cast<s16>(relative);
      const Address destination{static_cast<u32>(event.nextAddress().value + displacement)};
      event.derived("destination", destination, SourceValueDisplay::Address, SemanticOperandRole::RepeatTarget);
      event.mayBranchTo(destination);
      return event.invoke<&Playback::repeatBreak>(slot, destination);
    }
    case 0xdc: {
      auto event = cursor.command("Set Transpose", SequenceSemantic::Pitch);
      return event.invoke<&Playback::setTranspose>(event.s8("semitones"));
    }
    case 0xdd: {
      auto event = cursor.command("Add Transpose", SequenceSemantic::Pitch);
      return event.invoke<&Playback::addTranspose>(event.s8("semitones"));
    }
    case 0xde: {
      auto event = cursor.command("Set Volume Adjustment", SequenceSemantic::Level);
      return event.invoke<&Playback::setVolumeAdjustment>(event.s8("adjustment"));
    }
    case 0xdf: {
      auto event = cursor.command("Add Volume Adjustment", SequenceSemantic::Level);
      return event.invoke<&Playback::addVolumeAdjustment>(event.s8("adjustment"));
    }
    case 0xe0: {
      auto event = cursor.command("Reset LFO On Note", SequenceSemantic::Modulation);
      return event.invoke<&Playback::setResetLfo>(event.u8("enabled"));
    }
    case 0xe1: {
      auto event = cursor.command("LFO Rate", SequenceSemantic::Modulation);
      return event.invoke<&Playback::setLfoRate>(event.u8("rate", SemanticOperandRole::Modulation));
    }
    case 0xe2: {
      auto event = cursor.command("Tremolo Depth", SequenceSemantic::Modulation);
      return event.invoke<&Playback::setTremolo>(event.u8("depth", SemanticOperandRole::Modulation));
    }
    case 0xe3:
      return cursor.ignored("Driver State", 1);
    case 0xe4:
    case 0xe5:
      return cursor.ignored("Driver State", 2);
    case 0xe6:
      return cursor.ignored("Driver State", 1);
    case 0xe7: {
      if (!isCps3(version)) {
        return cursor.unsupported("CPS3 Fine Tune").stop();
      }
      auto event = cursor.command("Fine Tune", SequenceSemantic::Pitch);
      const u8 raw = event.u8("tuning");
      const double cents =
          event.derived("cents", (static_cast<int>(raw) - 64) * 100.0 / 64.0, SourceValueDisplay::Cents);
      return event.emitTuning(cents);
    }
    case 0xe8: {
      if (!isCps3(version)) {
        return cursor.unsupported("CPS3 Meta Event").stop();
      }
      auto event = cursor.command("Meta Event", SequenceSemantic::Meta);
      const u8 slot = event.u8("slot");
      const u8 value = event.u8("value");
      return event.invoke<&Playback::meta>(slot, value);
    }
    case 0xff:
      return cursor.command("End", SequenceSemantic::End).end();
    default:
      return cursor.unsupported("Unknown Command").stop();
  }
}

[[nodiscard]] SequenceDialect makeDialect(std::string_view id, std::string_view prefix, u32 ppqn,
                                          u8 initialPitchBendRange, double initialLevel = 1.0) {
  return makeCompiledDialect<TrackState, Playback, ProgramState>(SequenceDialect{
      .id = DialectId{std::string(id)},
      .commandDetailKindPrefix = std::string(prefix),
      .timebase = Timebase{.ppqn = ppqn},
      .defaultBehavior =
          SequenceProgramBehavior{
              .defaultLoopPolicy = LoopPolicy::PlayOnce,
              .commandLimit = kMaxTrackCommands,
              .panLaw = PanLaw::ConstantSum,
              .initialLevel = initialLevel,
              .initialMonoModeChannels = 16,
              .initialPitchBendRangeSemitones = initialPitchBendRange,
              .initialTempoMicrosecondsPerQuarter = 500000,
          },
      .preferredPitchTransitionRendering = PitchTransitionRenderingHint::Portamento,
  });
}

}  // namespace

const SequenceDialect& cps1V1Dialect() {
  static const SequenceDialect dialect = makeDialect(kCps1V1DialectId, "cps.cps1-v1", 24, 2);
  return dialect;
}

const SequenceDialect& cpsEarlyDialect() {
  static const SequenceDialect dialect = makeDialect(kCpsEarlyDialectId, "cps.early", kCpsPpqn, 2);
  return dialect;
}

const SequenceDialect& cpsLateDialect() {
  static const SequenceDialect dialect = makeDialect(kCpsLateDialectId, "cps.late", kCpsPpqn, 12, 0.0);
  return dialect;
}

SequenceProgram decodeCpsSequence(ByteReader reader, const CpsLayout& layout, const CpsSequenceInfo& sourceSequence,
                                  AssetId sequenceAsset, SourceMapBuilder* sourceMap,
                                  std::vector<Diagnostic>* diagnostics) {
  const bool v1 = layout.version == CpsVersion::Cps1V100;
  const SequenceDialect& dialect =
      v1 ? cps1V1Dialect() : (usesLateSequence(layout.version) ? cpsLateDialect() : cpsEarlyDialect());
  const u32 maxTracks = isCps1(layout.version) ? (v1 ? 8 : 12) : 16;
  const u32 headerSize = 1 + maxTracks * 2;
  const u32 availableHeaderSize = static_cast<u32>(std::min<u64>(headerSize, reader.size() - sourceSequence.offset));
  SequenceDecodeSession sequence(reader, dialect, sequenceAsset,
                                 reader.range(sourceSequence.offset, availableHeaderSize), sourceMap, kMaxTrackCommands,
                                 static_cast<u32>(layout.program.endOffset()));
  if (!reader.has(sourceSequence.offset, headerSize) || (reader.u8At(sourceSequence.offset) & 0x80) != 0) {
    if (diagnostics != nullptr) {
      diagnostics->push_back(Diagnostic{
          .severity = Severity::Warning,
          .message = "CPS sequence header is truncated or marked non-playable",
          .range = reader.range(sourceSequence.offset, std::min<u32>(headerSize, 1)),
      });
    }
    SequenceProgram empty = sequence.finish();
    empty.sourceBaseAddress = Address{sourceSequence.offset};
    empty.config.profile = static_cast<u32>(layout.version);
    empty.config.driverState = layout.masterVolume;
    if (usesLateSequence(layout.version)) {
      empty.behavior.initialExpression = isCps3(layout.version) ? 65.0 / 128.0 : 0.5;
    }
    return empty;
  }

  for (u32 track = 0; track < maxTracks; ++track) {
    const u32 pointer = sourceSequence.offset + 1 + track * 2;
    const u16 encoded = v1 ? reader.le16(pointer) : reader.be16(pointer);
    if (encoded == 0) {
      continue;
    }
    u32 start;
    if (v1 || (isCps1(layout.version) && layout.version <= CpsVersion::Cps1V425)) {
      start = static_cast<u32>(layout.program.offset + encoded);
    } else {
      start = sourceSequence.offset + encoded;
    }
    if (!reader.has(start, 1)) {
      continue;
    }

    if (v1) {
      const auto decode = [&](u32 offset) {
        return decodeCps1V1Command(reader, offset, static_cast<u32>(layout.program.offset),
                                   layout.cps1InstrumentTransposes, diagnostics);
      };
      sequence.addLinearTrack(track, reader.range(pointer, 2), start, decode, encoded);
    } else if (usesLateSequence(layout.version)) {
      std::array<Address, 4> loopStarts;
      const auto decode = [&](u32 offset) {
        return decodeLateCommand(reader, offset, layout.version, loopStarts, diagnostics);
      };
      sequence.addLinearTrack(track, reader.range(pointer, 2), start, decode, encoded);
    } else {
      const auto decode = [&](u32 offset) {
        return decodeEarlyCommand(reader, offset, layout.version, static_cast<u32>(layout.program.offset),
                                  layout.cps1InstrumentTransposes, diagnostics);
      };
      sequence.addLinearTrack(track, reader.range(pointer, 2), start, decode, encoded);
    }
  }
  SequenceProgram program = sequence.finish();
  program.sourceBaseAddress = Address{sourceSequence.offset};
  program.config.profile = static_cast<u32>(layout.version);
  program.config.driverState = layout.masterVolume;
  if (usesLateSequence(layout.version)) {
    program.behavior.initialExpression = isCps3(layout.version) ? 65.0 / 128.0 : 0.5;
  }
  return program;
}

}  // namespace vgmtrans::formats::cps
