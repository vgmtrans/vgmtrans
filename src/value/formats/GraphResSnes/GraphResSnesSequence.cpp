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
constexpr u8 kRestKey = 7;
constexpr u8 kInvalidNoteKey = 15;
constexpr double kPitchComparisonTolerance = 0.000001;

[[nodiscard]] constexpr bool isSilentKey(u8 key) { return key == kRestKey || key == kInvalidNoteKey; }

namespace math {

// Note lengths use one byte, where zero means 256 rather than no time at all.
[[nodiscard]] constexpr u32 ticks(u8 encoded) { return encoded == 0 ? 256 : encoded; }

// Timer 0 advances at 8 kHz, and each timer overflow advances the sequence by
// one tick.
[[nodiscard]] constexpr u32 tempoMicrosecondsPerQuarter(u8 timerTarget) {
  return kPpqn * 125u * (timerTarget == 0 ? 256u : timerTarget);
}

// Reproduce the driver's 8-bit duration and key-off counters. Ties suppress
// key-off entirely; untied notes test the threshold after each decrement.
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

[[nodiscard]] constexpr double gain(u8 value) { return value / 128.0; }

// If all eight echo-filter values match one of the driver's four built-in
// filters, return its number. Custom filter values have no preset number.
[[nodiscard]] std::optional<u8> firPreset(const std::array<s8, 8>& coefficients) {
  const auto found = std::ranges::find(kFirPresets, coefficients);
  return found == kFirPresets.end() ? std::nullopt
                                    : std::optional<u8>{static_cast<u8>(found - kFirPresets.begin())};
}

}  // namespace math

[[nodiscard]] SequenceProgramConfig sequenceConfig(u8 timerTarget) {
  return SequenceProgramConfig{
      .commandKindPrefix = "graph-res-snes",
      .timebase = Timebase{.ppqn = kPpqn},
      .behavior = SequenceProgramBehavior{
          .commandLimit = kCommandLimit,
          .initialSourceInstrument = InstrumentIdentity{.domain = std::string(kInstrumentDomain), .key = 0},
          .initialLevel = math::gain(15),
          .initialMasterLevel = math::gain(127),
          .initialStereoBalance = StereoBalance{.leftGain = 1.0, .rightGain = 1.0},
          .initialPitchBendRangeSemitones = 12,
          .initialTempoMicrosecondsPerQuarter = math::tempoMicrosecondsPerQuarter(timerTarget),
      },
  };
}

struct RuntimeConfig {
  RetainedSource source;
  Layout layout;
};

struct ProgramState {
  explicit ProgramState(const RuntimeConfig& config) : dsp(config.layout.dsp) {}

  DspState dsp;
  u16 fadeAccumulator = 0;
  u8 fadeRate = 0;
  bool emittedInitialEcho = false;
  std::optional<u64> lastGlobalTick;
};

struct PitchEnvelopeState {
  u8 index = 0;
  u8 offset = 0;
  u8 counter = 0;
};

struct EnvelopeSettings {
  u8 adsr1 = kDefaultAdsr1;
  u8 adsr2 = kDefaultAdsr2;

  friend bool operator==(const EnvelopeSettings&, const EnvelopeSettings&) = default;
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
  u8 defaultLength = 0;
  u8 durationRate = 8;
  EnvelopeSettings pendingEnvelope;
  EnvelopeSettings appliedEnvelope;
  s16 pitchOffset = 0;
  bool noise = false;
  bool continuesPrevious = false;
  u8 rawNote = kRestKey;
  u32 remaining = 0;
  u64 activeUntil = 0;
  PitchEnvelopeState pitchEnvelope;
  std::array<RepeatFrame, 4> repeats{};
  u8 repeatDepth = static_cast<u8>(repeats.size());
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

  // The fade keeps fractional progress in its low byte. Subtract its whole
  // part from a volume, and mute once the result leaves the valid 0-127 range.
  [[nodiscard]] u8 faded(u8 value) const {
    const int result = static_cast<int>(value) - static_cast<int>(program.fadeAccumulator >> 8);
    return result >= 0 && result < 0x80 ? static_cast<u8>(result) : 0;
  }

  void emitMaster() { out.masterLevel(math::gain(faded(program.dsp.masterVolume))); }

  // Echo is heard only when it is enabled and at least one voice uses it. Keep
  // the saved echo settings even while the audible echo amount is zero.
  void emitEcho() {
    const double gain = math::gain(faded(program.dsp.echoVolume));
    const bool enabled = (program.dsp.flags & 0x20) == 0 && program.dsp.echoVoices != 0;
    out.reverb(ReverbPerformanceEvent{
        .voiceMask = program.dsp.echoVoices,
        .send = enabled ? gain : 0.0,
        .leftGain = gain,
        .rightGain = gain,
        .delayMilliseconds = program.dsp.echoDelay * 16.0,
        .feedback = math::signedGain(program.dsp.echoFeedback),
        .filterIndex = math::firPreset(program.dsp.fir),
    });
  }

  // Some echo settings come from the driver's starting state rather than a
  // sequence command. Report them once before the first command runs.
  void beforeCommand() {
    if (!program.emittedInitialEcho) {
      program.emittedInitialEcho = true;
      emitEcho();
    }
  }

  // The pan table contains a left/right pair for each distance from center. A
  // negative pan index uses the same pair with left and right swapped.
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

  // Our output stores volume and pan separately. Use the louder side as the
  // volume, then express both sides relative to it to preserve their balance.
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

  // A pitch-change pattern is made of four-byte steps. When one step expires,
  // move to the next; a step beginning with $FE jumps to another step instead.
  void advancePitchEnvelope() {
    const auto table = pitchEnvelopeAddress();
    if (!table) {
      return;
    }
    auto& envelope = track.pitchEnvelope;
    if (envelope.counter == 0) {
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
    if (envelope.counter != 0xff && envelope.counter != 0) {
      --envelope.counter;
    }
  }

  // Bytes 2-3 of the current step are a signed 16-bit amount added directly to
  // the DSP pitch before octave scaling; positive raises it and negative lowers it.
  [[nodiscard]] s16 pitchEnvelopeDelta() const {
    const auto table = pitchEnvelopeAddress();
    if (!table) {
      return 0;
    }
    const auto& envelope = track.pitchEnvelope;
    const u16 record = static_cast<u16>(*table + envelope.offset);
    return static_cast<s16>(track.data.u8At(static_cast<u16>(record + 2)) |
                            (track.data.u8At(static_cast<u16>(record + 3)) << 8));
  }

  // Starting a new note returns the pitch-change pattern to its first step.
  // A tied note keeps the pattern at its current position.
  void resetPitchEnvelope() {
    track.pitchEnvelope.offset = 0;
    const auto table = pitchEnvelopeAddress();
    track.pitchEnvelope.counter = table ? track.data.u8At(*table) : 0;
  }

  struct Pitch {
    double key = 0.0;
    double bend = 0.0;
  };

  // The tuning table chooses the note. The pitch-change pattern and pitch-offset
  // command then move it up or down, which we report as pitch bend.
  [[nodiscard]] Pitch tonalPitch() const {
    const u8 noteNumber = static_cast<u8>(kNoteNumbers[track.rawNote] + track.transpose);
    const u16 tableAddress = static_cast<u16>(track.layout.pitchTableAddress + static_cast<u8>(noteNumber * 2u));
    const u16 base = track.data.le16(tableAddress);
    const u16 shifted = static_cast<u16>(base + pitchEnvelopeDelta() + track.pitchOffset);
    const u8 shift = track.octave < 5 ? static_cast<u8>(5 - track.octave) : 0;
    const u16 reference = static_cast<u16>(base >> shift);
    const u16 physical = static_cast<u16>(shifted >> shift);
    if (reference == 0 || physical == 0) {
      return {};
    }
    return Pitch{
        .key = kUnityKey + 12.0 * std::log2(reference / 4096.0),
        .bend = 12.0 * std::log2(physical / static_cast<double>(reference)),
    };
  }

  // The game writes pitch every tick. Our output needs a new event only when
  // the audible pitch bend has actually changed.
  void emitPitch() {
    if (track.noise || isSilentKey(track.rawNote)) {
      return;
    }
    const Pitch pitch = tonalPitch();
    if (pitch.key == 0.0) {
      return;
    }
    if (!track.lastPitchBend ||
        std::abs(*track.lastPitchBend - pitch.bend) > kPitchComparisonTolerance) {
      out.pitchBend(pitch.bend);
      track.lastPitchBend = pitch.bend;
    }
  }

  // Fade progress is shared by the whole song, but this function is reached
  // once per track. Update the fade only once for each moment in the song.
  void tickGlobal() {
    const u64 currentTick = vm.tick();
    if (program.lastGlobalTick && *program.lastGlobalTick == currentTick) {
      return;
    }
    program.lastGlobalTick = currentTick;
    const u16 next = static_cast<u16>(program.fadeAccumulator + (program.fadeRate << 4));
    if ((next & 0x8000) == 0 && next != program.fadeAccumulator) {
      const u8 oldInteger = static_cast<u8>(program.fadeAccumulator >> 8);
      program.fadeAccumulator = next;
      if (oldInteger != static_cast<u8>(program.fadeAccumulator >> 8)) {
        emitMaster();
        emitEcho();
      }
    }
  }

  // While a normal note is still sounding, move its pitch-change pattern
  // forward and report any new bend. Rests and noise do not use this pattern.
  void tick() {
    tickGlobal();
    if (track.remaining == 0 || --track.remaining == 0 || track.noise || isSilentKey(track.rawNote)) {
      return;
    }
    advancePitchEnvelope();
    if (track.lastNote.valid() && vm.tick() < track.activeUntil) {
      emitPitch();
    }
  }

  // F7 saves a new volume shape but does not apply it right away. When the next
  // note is read, apply it to sounds already playing and to notes started later.
  void applyPendingEnvelope() {
    if (track.appliedEnvelope == track.pendingEnvelope) {
      return;
    }
    track.appliedEnvelope = track.pendingEnvelope;
    out.replaceEnvelope(driverEnvelope(track.appliedEnvelope.adsr1, track.appliedEnvelope.adsr2),
                        VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks);
  }

  // A tie keeps the same sound playing instead of starting it again. Noise uses
  // a noise frequency in place of a note, and a new normal note restarts its
  // pitch-change pattern after calculating the note's starting pitch.
  [[nodiscard]] Effects note(u8 key, u8 encodedLength, bool hasLength, bool tiesNext) {
    const u32 length = math::ticks(hasLength ? encodedLength : track.defaultLength);
    const bool continues = track.continuesPrevious && track.lastNote.valid();
    track.rawNote = key;

    applyPendingEnvelope();

    if (isSilentKey(key)) {
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
        advancePitchEnvelope();
        const Pitch pitch = tonalPitch();
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
      if (continues && track.lastKey &&
          std::abs(*track.lastKey - noteKey) < kPitchComparisonTolerance) {
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
    program.dsp.masterVolume = value;
    emitMaster();
  }

  void echoVolume(u8 value) {
    program.dsp.echoVolume = value;
    emitEcho();
  }

  void fadeRate(u8 value) { program.fadeRate = value; }
  void noiseToggle() { track.noise = !track.noise; }

  void adsr(u8 adsr2, u8 adsr1) {
    track.pendingEnvelope = EnvelopeSettings{.adsr1 = adsr1, .adsr2 = adsr2};
  }

  void instrument(u8 value) {
    out.instrument(InstrumentIdentity{.domain = std::string(kInstrumentDomain), .key = value},
                   InstrumentEnvelopeMode::PreserveDynamicOverride);
  }

  void pitchOffset(s16 value) {
    track.pitchOffset = value;
    emitPitch();
  }

  void pitchEnvelope(u8 index) { track.pitchEnvelope.index = index; }

  // ED writes directly to the sound chip instead of saving a setting for the
  // next note. Some registers change the song's echo; others change the sound
  // already playing on this track.
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
    switch (reg & 0x0f) {
      case 5:
        track.appliedEnvelope.adsr1 = value;
        out.replaceEnvelope(driverEnvelope(value, track.appliedEnvelope.adsr2), VoiceEnvelopeScope::ActiveVoices);
        break;
      case 6:
        track.appliedEnvelope.adsr2 = value;
        out.replaceEnvelope(driverEnvelope(track.appliedEnvelope.adsr1, value), VoiceEnvelopeScope::ActiveVoices);
        break;
      case 7:
        track.appliedEnvelope = EnvelopeSettings{.adsr1 = 0, .adsr2 = 0};
        out.replaceEnvelope(snesDspEnvelope(0, 0, value), VoiceEnvelopeScope::ActiveVoices);
        break;
      default:
        break;
    }
  }

  // Up to four loops may be nested. EA opens a loop, and EB closes it and makes
  // that nesting slot available again.
  void loopStart() {
    if (track.repeatDepth == 0) {
      return;
    }
    --track.repeatDepth;
    track.repeats[track.repeatDepth] = {};
  }

  // EB and EE count repeats the same way. A count of zero means repeat forever;
  // other counts are remembered when the loop is first reached.
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

  // E9 leaves the loop only on its final pass. Leaving also frees the current
  // nesting slot, just as reaching the normal loop end would.
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

// Jump commands store a signed distance from the command itself, not a full
// address. Addresses wrap around at the end of the sound processor's memory.
[[nodiscard]] Address relativeTarget(Cursor::Event& event, u32 begin, SemanticOperandRole role) {
  const auto encoded = event.rawU16le("relative_destination", SourceValueDisplay::SignedDecimal);
  return event.resolved(
      "destination", encoded,
      [begin](u16 offset) { return Address{static_cast<u16>(begin + offset)}; }, SourceValueDisplay::Address, role);
}

// Read one command and describe what it does. FE appears after a note, but it
// changes that note into a tie, so note decoding also looks one byte ahead.
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
    auto event = cursor.command(key == kRestKey ? "Rest" : (key == kInvalidNoteKey ? "Invalid Note" : "Note"),
                                key == kRestKey ? SequenceSemantic::Rest : SequenceSemantic::Note);
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
      const Address destination = relativeTarget(event, begin, SemanticOperandRole::RepeatTarget);
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
      const Address destination = relativeTarget(event, begin, SemanticOperandRole::RepeatTarget);
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
      const Address destination = relativeTarget(event, begin, SemanticOperandRole::CallTarget);
      return event.call(destination);
    }
    case 0xfa: {
      auto event = cursor.command("Jump", SequenceSemantic::Jump);
      const Address destination = relativeTarget(event, begin, SemanticOperandRole::JumpTarget);
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

TrackProgram decodeSourceTrack(ByteReader reader, const Layout& layout, u32 trackNumber, u32 startAddress,
                               std::set<u8>* programs, std::vector<Diagnostic>* diagnostics) {
  const TrackDecodeScope tracks{.reader = reader, .maxCommands = kCommandLimit};
  return tracks.decode(trackNumber, startAddress,
                       [&](u32 offset) { return decodeCommand(reader, offset, layout, diagnostics, programs); });
}

// Collect the sample numbers used by the sequence while reading its commands.
// Keep the original sound memory because playback still needs its lookup tables.
SequenceParse decodeSequence(RetainedSource source, const Layout& layout, AssetId sequenceId,
                             SourceMapBuilder* sourceMap, std::vector<Diagnostic>* diagnostics) {
  const ByteReader reader = source.reader();
  const SourceRange header = reader.range(layout.sequenceHeaderAddress, kTrackCount * 3u);
  std::set<u8> programs{0};
  const SequenceProgramConfig config = sequenceConfig(layout.timerTarget);
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
