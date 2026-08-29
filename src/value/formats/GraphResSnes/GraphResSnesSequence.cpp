/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/GraphResSnes/GraphResSnes.h"

#include "value/sequence/CommandSourceMap.h"
#include "value/sequence/CompiledCommandRuntime.h"
#include "value/synth/SnesDsp.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <set>
#include <string>
#include <utility>

namespace vgmtrans::formats::graph_res_snes {

using namespace core;

namespace {

constexpr std::array<u8, 16> kNoteNumbers{
    0x0c, 0x0e, 0x10, 0x11, 0x13, 0x15, 0x17, 0x6f,
    0x0d, 0x0f, 0x10, 0x12, 0x14, 0x16, 0x17, 0x6f,
};
constexpr std::array<std::array<s8, 8>, 4> kFirPresets{{
    {{0x7f, 0, 0, 0, 0, 0, 0, 0}},
    {{0x58, -0x41, -0x25, -0x10, -2, 7, 0x0c, 0x0c}},
    {{0x0c, 0x21, 0x2b, 0x2b, 0x13, -2, -0x0d, -7}},
    {{0x34, 0x33, 0, -0x27, -0x1b, 1, -4, -0x15}},
}};

namespace math {

[[nodiscard]] constexpr u32 ticks(u8 encoded) { return encoded == 0 ? 256 : encoded; }

[[nodiscard]] constexpr u32 tempoMicrosecondsPerQuarter(u8 timerTarget) {
  // Timer 0 advances at 8 kHz. One sequence tick is one timer overflow.
  return kPpqn * 125u * (timerTarget == 0 ? 256u : timerTarget);
}

[[nodiscard]] u32 soundingTicks(u32 length, u8 rate, bool tiesNext) {
  if (tiesNext) {
    return length;
  }
  const u8 encodedLength = static_cast<u8>(length);
  const u8 duration = static_cast<u8>(static_cast<u16>(encodedLength) * rate / 8u);
  u8 keyOffCounter = static_cast<u8>(encodedLength - duration);
  if (keyOffCounter == 0) {
    keyOffCounter = 1;
  }
  u8 counter = encodedLength;
  for (u32 elapsed = 1; elapsed <= length; ++elapsed) {
    --counter;
    if (counter <= keyOffCounter) {
      return elapsed;
    }
  }
  return length;
}

[[nodiscard]] constexpr double signedGain(s8 value) { return value / 128.0; }

[[nodiscard]] std::optional<u8> firPreset(const std::array<s8, 8>& coefficients) {
  const auto found = std::ranges::find(kFirPresets, coefficients);
  return found == kFirPresets.end() ? std::nullopt
                                    : std::optional<u8>{static_cast<u8>(found - kFirPresets.begin())};
}

}  // namespace math

struct RuntimeConfig {
  RetainedSource source;
  Layout layout;
};

struct ProgramState {
  explicit ProgramState(const RuntimeConfig& config) : dsp(config.layout.dsp) {}

  DspState dsp;
  u16 fade = 0;
  u8 fadeRate = 0;
  bool initialized = false;
  std::optional<u64> lastGlobalTick;
};

struct PitchEnvelopeState {
  u8 index = 0;
  u8 offset = 0;
  u8 counter = 0;
};

struct RepeatFrame {
  u8 remaining = 0;
  Address exit;
  bool initialized = false;
};

struct TrackState {
  TrackState(const TrackProgram& sourceTrack, const RuntimeConfig& config)
      : data(config.source.reader()), layout(config.layout), trackNumber(sourceTrack.sourceTrackNumber) {}

  ByteReader data;
  Layout layout;
  u32 trackNumber = 0;
  u8 volume = 0x0f;
  s8 pan = 0;
  u8 octave = 4;
  u8 transpose = 0;
  u8 program = 0;
  u8 defaultLength = 0;
  u8 durationRate = 8;
  u8 adsr1 = 0x8f;
  u8 adsr2 = 0xe0;
  u8 appliedAdsr1 = 0x8f;
  u8 appliedAdsr2 = 0xe0;
  s16 pitchOffset = 0;
  bool noise = false;
  bool continuesPrevious = false;
  u8 rawNote = 7;
  u32 remaining = 0;
  u64 activeUntil = 0;
  PitchEnvelopeState pitchEnvelope;
  std::array<RepeatFrame, 4> repeats{};
  u8 repeatDepth = 4;
  RepeatFrame unstackedRepeat;
  PerformanceNoteId lastNote;
  std::optional<double> lastKey;
  std::optional<double> lastPitchBend;
};

struct Playback {
  TrackState& track;
  PerformanceEmitter& out;
  VmApi& vm;
  ProgramState& program;

  [[nodiscard]] u8 faded(u8 value) const {
    const int result = static_cast<int>(value) - static_cast<int>(program.fade >> 8);
    return result >= 0 && result < 0x80 ? static_cast<u8>(result) : 0;
  }

  void emitMaster() {
    out.masterLevel(std::max(faded(program.dsp.masterLeft), faded(program.dsp.masterRight)) / 128.0);
  }

  void emitEcho() {
    const u8 left = faded(program.dsp.echoLeft);
    const u8 right = faded(program.dsp.echoRight);
    const bool enabled = (program.dsp.flags & 0x20) == 0 && program.dsp.echoVoices != 0;
    out.reverb(ReverbPerformanceEvent{
        .voiceMask = program.dsp.echoVoices,
        .send = enabled ? std::max(left, right) / 128.0 : 0.0,
        .leftGain = left / 128.0,
        .rightGain = right / 128.0,
        .delayMilliseconds = program.dsp.echoDelay * 16.0,
        .feedback = math::signedGain(program.dsp.echoFeedback),
        .filterIndex = math::firPreset(program.dsp.fir),
    });
  }

  void beforeCommand() {
    if (!program.initialized) {
      program.initialized = true;
      emitEcho();
    }
  }

  [[nodiscard]] std::pair<double, double> channelGains() const {
    const int signedPan = track.pan;
    const u8 offset = static_cast<u8>(std::abs(signedPan) * 2);
    const u16 address = static_cast<u16>(track.layout.panTableAddress + offset);
    const u8 firstBalance = track.data.u8At(address);
    const u8 secondBalance = track.data.u8At(static_cast<u16>(address + 1));
    const auto apply = [volume = track.volume](u8 balance) {
      return math::signedGain(static_cast<s8>(static_cast<u16>(volume) * balance / 100u));
    };
    const double first = apply(firstBalance);
    const double second = apply(secondBalance);
    return signedPan < 0 ? std::pair{second, first} : std::pair{first, second};
  }

  void emitMix() {
    const auto [left, right] = channelGains();
    const double level = std::max(std::abs(left), std::abs(right));
    out.level(level, ValueQuantization{.levels = 256});
    out.stereoBalance(level == 0.0 ? 1.0 : left / level, level == 0.0 ? 1.0 : right / level);
  }

  [[nodiscard]] std::optional<u16> pitchEnvelopeAddress() const {
    if (track.pitchEnvelope.index >= track.layout.pitchEnvelopeCount) {
      return std::nullopt;
    }
    return track.data.le16(track.layout.pitchEnvelopeListAddress + track.pitchEnvelope.index * 2u);
  }

  [[nodiscard]] s16 pitchEnvelopeDelta(bool advance) {
    const auto table = pitchEnvelopeAddress();
    if (!table) {
      return 0;
    }
    auto& envelope = track.pitchEnvelope;
    if (advance && envelope.counter == 0) {
      envelope.offset = static_cast<u8>(envelope.offset + 4);
      for (u32 redirects = 0; redirects < 64; ++redirects) {
        const u16 record = static_cast<u16>(*table + envelope.offset);
        if (track.data.u8At(record) != 0xfe) {
          envelope.counter = track.data.u8At(record);
          break;
        }
        envelope.offset = static_cast<u8>(envelope.offset + track.data.u8At(static_cast<u16>(record + 2)));
      }
    }
    if (advance && envelope.counter != 0xff && envelope.counter != 0) {
      --envelope.counter;
    }
    const u16 record = static_cast<u16>(*table + envelope.offset);
    return static_cast<s16>(track.data.u8At(static_cast<u16>(record + 2)) |
                            (track.data.u8At(static_cast<u16>(record + 3)) << 8));
  }

  void resetPitchEnvelope() {
    track.pitchEnvelope.offset = 0;
    const auto table = pitchEnvelopeAddress();
    track.pitchEnvelope.counter = table ? track.data.u8At(*table) : 0;
  }

  struct Pitch {
    double key = 0.0;
    double bend = 0.0;
  };

  [[nodiscard]] Pitch tonalPitch(bool advanceEnvelope) {
    const u8 noteNumber = static_cast<u8>(kNoteNumbers[track.rawNote] + track.transpose);
    const u16 tableAddress = static_cast<u16>(track.layout.pitchTableAddress + static_cast<u8>(noteNumber * 2u));
    const u16 base = track.data.le16(tableAddress);
    const u16 shifted = static_cast<u16>(base + pitchEnvelopeDelta(advanceEnvelope) + track.pitchOffset);
    const u8 shift = track.octave < 5 ? static_cast<u8>(5 - track.octave) : 0;
    const u16 reference = static_cast<u16>(base >> shift);
    const u16 physical = static_cast<u16>(shifted >> shift);
    if (reference == 0 || physical == 0) {
      return {};
    }
    return Pitch{
        .key = 57.0 + 12.0 * std::log2(reference / 4096.0),
        .bend = 12.0 * std::log2(physical / static_cast<double>(reference)),
    };
  }

  void emitPitch(bool advanceEnvelope) {
    if (track.noise || track.rawNote == 7 || track.rawNote == 15) {
      return;
    }
    const Pitch pitch = tonalPitch(advanceEnvelope);
    if (pitch.key == 0.0) {
      return;
    }
    if (!track.lastPitchBend || std::abs(*track.lastPitchBend - pitch.bend) > 0.000001) {
      out.pitchBend(pitch.bend);
      track.lastPitchBend = pitch.bend;
    }
  }

  void tickGlobal() {
    if (program.lastGlobalTick && *program.lastGlobalTick == vm.tick()) {
      return;
    }
    program.lastGlobalTick = vm.tick();
    const u16 next = static_cast<u16>(program.fade + (program.fadeRate << 4));
    if ((next & 0x8000) == 0 && next != program.fade) {
      const u8 oldInteger = static_cast<u8>(program.fade >> 8);
      program.fade = next;
      if (oldInteger != static_cast<u8>(program.fade >> 8)) {
        emitMaster();
        emitEcho();
      }
    }
  }

  void tick() {
    tickGlobal();
    if (track.remaining == 0 || --track.remaining == 0 || track.noise || track.rawNote == 7 || track.rawNote == 15) {
      return;
    }
    static_cast<void>(pitchEnvelopeDelta(true));
    if (track.lastNote.valid() && vm.tick() < track.activeUntil) {
      emitPitch(false);
    }
  }

  [[nodiscard]] Effects note(u8 key, u8 encodedLength, bool hasLength, bool tiesNext) {
    const u32 length = math::ticks(hasLength ? encodedLength : track.defaultLength);
    const bool continues = track.continuesPrevious && track.lastNote.valid();
    track.rawNote = key;

    if (track.appliedAdsr1 != track.adsr1 || track.appliedAdsr2 != track.adsr2) {
      track.appliedAdsr1 = track.adsr1;
      track.appliedAdsr2 = track.adsr2;
      out.replaceEnvelope(driverEnvelope(track.appliedAdsr1, track.appliedAdsr2),
                          VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks);
    }

    if (key == 7 || key == 15) {
      track.lastNote = {};
      track.lastKey.reset();
      track.lastPitchBend.reset();
    } else {
      double noteKey = 0.0;
      double bend = 0.0;
      if (track.noise) {
        noteKey = static_cast<u8>(kNoteNumbers[key] + track.transpose) & 0x1f;
        program.dsp.flags = static_cast<u8>(noteKey) | 0x20;
        emitEcho();
      } else {
        const Pitch pitch = tonalPitch(true);
        noteKey = pitch.key;
        bend = pitch.bend;
      }
      const u32 sounding = math::soundingTicks(length, track.durationRate, tiesNext);
      track.activeUntil = vm.tick() + sounding;
      NotePerformanceEvent event{
          .key = noteKey,
          .linearVelocity = 1.0,
          .durationTicks = sounding,
          .restartsEnvelope = !continues,
          .restartsLfoPhase = !continues,
      };
      if (continues && track.lastKey && std::abs(*track.lastKey - noteKey) < 0.000001) {
        event.extendsPrevious = true;
        track.lastNote = out.note(std::move(event));
      } else if (continues) {
        track.lastNote = out.continueVoice(track.lastNote, std::move(event));
      } else {
        track.lastNote = out.note(std::move(event));
      }
      track.lastKey = noteKey;
      out.pitchBend(bend);
      track.lastPitchBend = bend;
    }

    if (!continues) {
      resetPitchEnvelope();
    }
    track.continuesPrevious = tiesNext;
    track.remaining = length;
    return Effects::wait(length);
  }

  void volumePreset(u8 index) { volume(track.data.u8At(track.layout.volumeTableAddress + (index & 0x0f))); }

  void volume(u8 value) {
    track.volume = value;
    emitMix();
  }

  void octave(u8 value) { track.octave = value & 7; }
  void octaveAdd(s8 delta) { track.octave = static_cast<u8>(track.octave + delta) & 7; }
  void transpose(s8 value) { track.transpose = static_cast<u8>(value); }
  void durationRate(u8 value) { track.durationRate = value; }
  void defaultLength(u8 value) { track.defaultLength = value; }

  void pan(s8 value) {
    track.pan = value;
    emitMix();
  }

  void masterVolume(u8 value) {
    program.dsp.masterLeft = value;
    program.dsp.masterRight = value;
    emitMaster();
  }

  void echoVolume(u8 value) {
    program.dsp.echoLeft = value;
    program.dsp.echoRight = value;
    emitEcho();
  }

  void fadeRate(u8 value) { program.fadeRate = value; }
  void noiseToggle() { track.noise = !track.noise; }

  void adsr(u8 adsr2, u8 adsr1) {
    track.adsr1 = adsr1;
    track.adsr2 = adsr2;
  }

  void instrument(u8 value) {
    track.program = value;
    out.instrument(InstrumentIdentity{.domain = std::string(kInstrumentDomain), .key = value},
                   InstrumentEnvelopeMode::PreserveDynamicOverride);
  }

  void pitchOffset(s16 value) {
    track.pitchOffset = value;
    emitPitch(false);
  }

  void pitchEnvelope(u8 index) { track.pitchEnvelope.index = index; }

  void dspWrite(u8 reg, u8 value) {
    bool echoChanged = false;
    switch (reg) {
      case 0x0d:
        program.dsp.echoFeedback = static_cast<s8>(value);
        echoChanged = true;
        break;
      case 0x4d:
        program.dsp.echoVoices = value;
        echoChanged = true;
        break;
      case 0x6c:
        program.dsp.flags = value;
        echoChanged = true;
        break;
      case 0x7d:
        program.dsp.echoDelay = value & 0x0f;
        echoChanged = true;
        break;
      default:
        if ((reg & 0x0f) == 0x0f && (reg >> 4) < program.dsp.fir.size()) {
          program.dsp.fir[reg >> 4] = static_cast<s8>(value);
          echoChanged = true;
        }
        break;
    }
    if (echoChanged) {
      emitEcho();
    }

    if ((reg >> 4) != track.trackNumber) {
      return;
    }
    if ((reg & 0x0f) == 5) {
      track.appliedAdsr1 = value;
      out.replaceEnvelope(driverEnvelope(value, track.appliedAdsr2), VoiceEnvelopeScope::ActiveVoices);
    } else if ((reg & 0x0f) == 6) {
      track.appliedAdsr2 = value;
      out.replaceEnvelope(driverEnvelope(track.appliedAdsr1, value), VoiceEnvelopeScope::ActiveVoices);
    } else if ((reg & 0x0f) == 7) {
      track.appliedAdsr1 = 0;
      track.appliedAdsr2 = 0;
      out.replaceEnvelope(snesDspEnvelope(0, 0, value), VoiceEnvelopeScope::ActiveVoices);
    }
  }

  void loopStart() {
    if (track.repeatDepth == 0) {
      return;
    }
    --track.repeatDepth;
    track.repeats[track.repeatDepth] = {};
  }

  [[nodiscard]] Effects repeat(RepeatFrame& frame, u8 count, Address destination, Address exit) {
    if (!frame.initialized) {
      frame = RepeatFrame{.remaining = count, .exit = exit, .initialized = true};
    }
    if (count == 0) {
      return vm.loopCandidate(destination);
    }
    if (--frame.remaining != 0) {
      return vm.finiteBranch(destination);
    }
    frame = {};
    return vm.fallthrough();
  }

  [[nodiscard]] Effects loopEnd(u8 count, Address destination, Address exit) {
    if (track.repeatDepth >= track.repeats.size()) {
      return vm.fallthrough();
    }
    Effects effects = repeat(track.repeats[track.repeatDepth], count, destination, exit);
    if (!track.repeats[track.repeatDepth].initialized) {
      ++track.repeatDepth;
    }
    return effects;
  }

  [[nodiscard]] Effects loopBreak() {
    if (track.repeatDepth >= track.repeats.size()) {
      return vm.fallthrough();
    }
    RepeatFrame& frame = track.repeats[track.repeatDepth];
    if (!frame.initialized || frame.remaining != 1) {
      return vm.fallthrough();
    }
    const Address exit = frame.exit;
    frame = {};
    ++track.repeatDepth;
    return vm.finiteBranch(exit);
  }

  [[nodiscard]] Effects unstackedLoop(u8 count, Address destination, Address exit) {
    return repeat(track.unstackedRepeat, count, destination, exit);
  }
};

using Cursor = CompilerCursor<TrackState, Playback>;

[[nodiscard]] Address relativeAddress(u32 begin, u16 offset) {
  return Address{static_cast<u16>(begin + offset)};
}

[[nodiscard]] DecodedBytecodeCommand decodeCommand(ByteReader reader, u32 begin, const Layout& layout,
                                                   std::vector<Diagnostic>* diagnostics,
                                                   std::set<u8>* programs) {
  Cursor cursor(reader, begin, "graph-res-snes", diagnostics);
  if (!cursor.hasOpcode()) {
    return cursor.truncated();
  }
  const u8 opcode = cursor.opcode();
  if (opcode < 0x80) {
    const u8 key = opcode & 0x0f;
    auto event = cursor.command(key == 7 ? "Rest" : (key == 15 ? "Invalid Note" : "Note"),
                                key == 7 ? SequenceSemantic::Rest : SequenceSemantic::Note);
    event.opcodeValue("key", key, SourceValueDisplay::MidiNote, SemanticOperandRole::NoteKey);
    const bool hasLength = (opcode & 0x10) != 0;
    const u8 length = hasLength ? event.u8("length", SemanticOperandRole::Duration) : 0;
    const bool tiesNext = reader.has(begin + 1u + (hasLength ? 1u : 0u), 1) &&
                          reader.u8At(begin + 1u + (hasLength ? 1u : 0u)) == 0xfe;
    event.derived("ties_next", tiesNext, SemanticOperandRole::State);
    return event.invoke<&Playback::note>(key, length, hasLength, tiesNext);
  }
  if (opcode < 0x90) {
    auto event = cursor.command("Volume Preset", SequenceSemantic::Level);
    const u8 index = event.opcodeValue("preset", static_cast<u8>(opcode & 0x0f));
    return event.invoke<&Playback::volumePreset>(index);
  }
  if (opcode < 0xa0) {
    auto event = cursor.command("Octave", SequenceSemantic::Pitch);
    const u8 octave = event.opcodeValue("octave", static_cast<u8>(opcode & 7));
    return event.invoke<&Playback::octave>(octave);
  }
  if (opcode < 0xe0) {
    return cursor.unsupported("Invalid Command").stop();
  }

  switch (opcode) {
    case 0xe4: {
      auto event = cursor.command("Transpose", SequenceSemantic::Pitch);
      return event.invoke<&Playback::transpose>(event.s8("semitones", SemanticOperandRole::Pitch));
    }
    case 0xe5: {
      auto event = cursor.command("Master Volume", SequenceSemantic::Level);
      return event.invoke<&Playback::masterVolume>(event.u8("volume", SemanticOperandRole::Level));
    }
    case 0xe6: {
      auto event = cursor.command("Echo Volume", SequenceSemantic::Level);
      return event.invoke<&Playback::echoVolume>(event.u8("volume", SemanticOperandRole::Level));
    }
    case 0xe7:
      return cursor.command("Octave Down", SequenceSemantic::Pitch).invoke<&Playback::octaveAdd>(-1);
    case 0xe8:
      return cursor.command("Octave Up", SequenceSemantic::Pitch).invoke<&Playback::octaveAdd>(1);
    case 0xe9:
      return cursor.command("Loop Break", SequenceSemantic::RepeatBreak).invokeFlow<&Playback::loopBreak>();
    case 0xea:
      return cursor.command("Loop Start", SequenceSemantic::Repeat).invoke<&Playback::loopStart>();
    case 0xeb: {
      auto event = cursor.command("Loop End", SequenceSemantic::Repeat);
      const u8 count = event.u8("count", SemanticOperandRole::Count);
      const auto encoded = event.rawU16le("relative_destination", SourceValueDisplay::SignedDecimal);
      const Address destination = event.resolved(
          "destination", encoded, [begin](u16 value) { return relativeAddress(begin, value); },
          SourceValueDisplay::Address, SemanticOperandRole::RepeatTarget);
      const Address exit{static_cast<u16>(begin + 4)};
      return event.invokeFlow<&Playback::loopEnd>(count, destination, exit).mayBranchTo(destination);
    }
    case 0xec: {
      auto event = cursor.command("Duration Rate", SequenceSemantic::State);
      return event.invoke<&Playback::durationRate>(event.u8("eighths", SemanticOperandRole::Duration));
    }
    case 0xed: {
      auto event = cursor.command("DSP Write", SequenceSemantic::State);
      const u8 reg = event.u8("register", SourceValueDisplay::Hex);
      return event.invoke<&Playback::dspWrite>(reg, event.u8("value", SourceValueDisplay::Hex));
    }
    case 0xee: {
      auto event = cursor.command("Unstacked Loop", SequenceSemantic::Repeat);
      const u8 count = event.u8("count", SemanticOperandRole::Count);
      const auto encoded = event.rawU16le("relative_destination", SourceValueDisplay::SignedDecimal);
      const Address destination = event.resolved(
          "destination", encoded, [begin](u16 value) { return relativeAddress(begin, value); },
          SourceValueDisplay::Address, SemanticOperandRole::RepeatTarget);
      const Address exit{static_cast<u16>(begin + 4)};
      return event.invokeFlow<&Playback::unstackedLoop>(count, destination, exit).mayBranchTo(destination);
    }
    case 0xef: {
      auto event = cursor.command("Pitch Offset", SequenceSemantic::Pitch);
      return event.invoke<&Playback::pitchOffset>(
          event.s16le("offset", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Pitch));
    }
    case 0xf0:
      return cursor.command("Toggle Noise", SequenceSemantic::State).invoke<&Playback::noiseToggle>();
    case 0xf1: {
      auto event = cursor.command("Volume", SequenceSemantic::Level);
      return event.invoke<&Playback::volume>(event.u8("volume", SemanticOperandRole::Level));
    }
    case 0xf3: {
      auto event = cursor.command("Master / Echo Fade Rate", SequenceSemantic::Level);
      return event.invoke<&Playback::fadeRate>(event.u8("rate"));
    }
    case 0xf4: {
      auto event = cursor.command("Pan", SequenceSemantic::Pan);
      return event.invoke<&Playback::pan>(event.s8("pan", SemanticOperandRole::Pan));
    }
    case 0xf7: {
      auto event = cursor.command("ADSR", SequenceSemantic::Envelope);
      const u8 adsr2 = event.u8("adsr2", SourceValueDisplay::Hex);
      return event.invoke<&Playback::adsr>(adsr2, event.u8("adsr1", SourceValueDisplay::Hex));
    }
    case 0xf8:
      return cursor.command("Return", SequenceSemantic::Return).return_();
    case 0xf9: {
      auto event = cursor.command("Call", SequenceSemantic::Call);
      const auto encoded = event.rawU16le("relative_destination", SourceValueDisplay::SignedDecimal);
      const Address destination = event.resolved(
          "destination", encoded, [begin](u16 value) { return relativeAddress(begin, value); },
          SourceValueDisplay::Address, SemanticOperandRole::CallTarget);
      return event.call(destination);
    }
    case 0xfa: {
      auto event = cursor.command("Jump", SequenceSemantic::Jump);
      const auto encoded = event.rawU16le("relative_destination", SourceValueDisplay::SignedDecimal);
      const Address destination = event.resolved(
          "destination", encoded, [begin](u16 value) { return relativeAddress(begin, value); },
          SourceValueDisplay::Address, SemanticOperandRole::JumpTarget);
      return event.loopCandidate(destination);
    }
    case 0xfb: {
      auto event = cursor.command("Pitch Envelope / Vibrato", SequenceSemantic::Modulation);
      const u8 index = event.u8("program");
      if (index >= layout.pitchEnvelopeCount) {
        event.warning("Pitch-envelope index lies outside the discovered driver table");
      }
      return event.invoke<&Playback::pitchEnvelope>(index);
    }
    case 0xfc: {
      auto event = cursor.command("Program Change", SequenceSemantic::Program);
      const u8 program = event.u8("srcn", SemanticOperandRole::InstrumentProgram);
      if (programs != nullptr) {
        programs->insert(program);
      }
      return event.invoke<&Playback::instrument>(program);
    }
    case 0xfd: {
      auto event = cursor.command("Default Note Length", SequenceSemantic::State);
      return event.invoke<&Playback::defaultLength>(event.u8("length", SemanticOperandRole::Duration));
    }
    case 0xfe:
      return cursor.sourceOnly("Tie Marker", "tie-marker");
    case 0xff:
      return cursor.command("End", SequenceSemantic::End).end();
    default:
      return cursor.unsupported("Invalid Command").stop();
  }
}

}  // namespace

const SequenceProgramConfig& sequenceConfig() {
  static const SequenceProgramConfig config{
      .commandKindPrefix = "graph-res-snes",
      .timebase = Timebase{.ppqn = kPpqn},
      .behavior = SequenceProgramBehavior{
          .commandLimit = kCommandLimit,
          .initialSourceInstrument = InstrumentIdentity{.domain = std::string(kInstrumentDomain), .key = 0},
          .initialLevel = 15.0 / 128.0,
          .initialMasterLevel = 127.0 / 128.0,
          .initialStereoBalance = StereoBalance{.leftGain = 1.0, .rightGain = 1.0},
          .initialPitchBendRangeSemitones = 12,
          .initialTempoMicrosecondsPerQuarter = math::tempoMicrosecondsPerQuarter(0x85),
      },
  };
  return config;
}

TrackProgram decodeSourceTrack(ByteReader reader, const Layout& layout, u32 trackNumber, u32 startAddress,
                               std::set<u8>* programs, std::vector<Diagnostic>* diagnostics) {
  const TrackDecodeScope tracks{.reader = reader, .maxCommands = kCommandLimit};
  return tracks.decode(trackNumber, startAddress,
                       [&](u32 offset) { return decodeCommand(reader, offset, layout, diagnostics, programs); });
}

SequenceParse decodeSequence(RetainedSource source, const Layout& layout, AssetId sequenceId,
                             SourceMapBuilder* sourceMap, std::vector<Diagnostic>* diagnostics) {
  const ByteReader reader = source.reader();
  const SourceRange header = reader.range(layout.sequenceHeaderAddress, kTrackCount * 3u);
  std::set<u8> programs{0};
  SequenceProgramConfig config = sequenceConfig();
  config.behavior.initialTempoMicrosecondsPerQuarter = math::tempoMicrosecondsPerQuarter(layout.timerTarget);
  SequenceDecodeSession sequence{reader, config, sequenceId, header, sourceMap, kCommandLimit, kAramSize};
  for (const TrackHeader& track : layout.tracks) {
    sequence.addTrack(
        track.index, track.range, track.startAddress,
        [&](u32 offset) { return decodeCommand(reader, offset, layout, diagnostics, &programs); }, track.startAddress);
  }
  SequenceProgram program = sequence.finish(
      makeCompiledRuntime<Cursor, ProgramState>(RuntimeConfig{.source = std::move(source), .layout = layout}));
  return SequenceParse{.program = std::move(program), .programs = std::move(programs), .headerRange = header};
}

}  // namespace vgmtrans::formats::graph_res_snes
