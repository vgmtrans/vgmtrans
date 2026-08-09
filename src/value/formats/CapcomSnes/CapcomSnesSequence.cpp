/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/CapcomSnes/CapcomSnes.h"

#include "value/sequence/BytecodeDecode.h"
#include "value/sequence/CommandSourceMap.h"
#include "value/sequence/CompiledCommandDialect.h"
#include "value/sequence/SequenceVm.h"
#include "value/synth/SnesDsp.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>

namespace vgmtrans::formats::capcom_snes {

using namespace core;

namespace {

constexpr u8 kNoteOctaveMask = 0x07;
constexpr u8 kNoteOctaveUpMask = 0x08;
constexpr u8 kNoteDottedMask = 0x10;
constexpr u8 kNoteTripletMask = 0x20;
constexpr u8 kNoteSlurredMask = 0x40;

enum class LfoParameter : u8 {
  VibratoDepth = 0,
  TremoloDepth = 1,
  Rate = 2,
  ResetPhaseOnNote = 3,
};

namespace math {

constexpr std::array<u8, 17> kVolumeCurve{0x00, 0x0c, 0x19, 0x26, 0x33, 0x40, 0x4c, 0x59, 0x66,
                                          0x73, 0x80, 0x8c, 0x99, 0xb3, 0xcc, 0xe6, 0xff};
constexpr std::array<u8, 22> kPanCurve{0x00, 0x01, 0x03, 0x07, 0x0d, 0x15, 0x1e, 0x29, 0x34, 0x42, 0x51,
                                       0x5e, 0x67, 0x6e, 0x73, 0x77, 0x7a, 0x7c, 0x7d, 0x7e, 0x7f, 0x7f};
constexpr double kTremoloMuteFloorCentibels = 960.0;

struct StereoBalance {
  double leftGain = 1.0;
  double rightGain = 1.0;
};

// Blends between two neighboring values in one of the driver's lookup tables.
[[nodiscard]] int interpolate(const auto& table, int index, int fraction) {
  const int lower = table[index];
  const int upper = table[index + 1];
  return lower + (((upper - lower) * fraction) >> 8);
}

// Converts Capcom's volume byte into the level used by the shared playback code.
[[nodiscard]] double volumeGain(CapcomSnesEngineVersion version, u8 rawVolume) {
  if (version == CapcomSnesEngineVersion::v1BgmInList) {
    return rawVolume / 255.0;
  }
  if (rawVolume >= 0x80) {
    return 1.0;
  }
  const int index = rawVolume >> 3;
  const int fraction = ((rawVolume & 0x07) << 5) | 0x1f;
  return static_cast<double>(interpolate(kVolumeCurve, index, fraction)) / 255.0;
}

// Converts Capcom's portamento speed into travel time for one semitone.
[[nodiscard]] double portamentoMillisecondsPerSemitone(u8 rawTime) {
  const u8 step = static_cast<u8>((rawTime << 1) & 0xff);
  const double centsPerUpdate = step * (100.0 / 256.0);
  return centsPerUpdate == 0.0 ? 0.0 : (16.0 * 100.0) / centsPerUpdate;
}

// Converts the duration bits packed into a note opcode into sequence ticks.
[[nodiscard]] u32 baseNoteTicks(u8 rawDuration) {
  return rawDuration == 0 || rawDuration > 7 ? 0 : 192u >> (7u - rawDuration);
}

// Converts Capcom's tempo value into the standard duration of one quarter note.
[[nodiscard]] u32 tempoMicrosecondsPerQuarter(u16 rawTempo) {
  return rawTempo == 0 ? 60000000 : static_cast<u32>(std::round(kCapcomSnesPpqn * (125 * 0x40) * 2 * 256.0 / rawTempo));
}

// Converts Capcom's pan byte into separate left and right channel levels.
[[nodiscard]] StereoBalance stereoBalance(CapcomSnesEngineVersion version, u8 rawPan) {
  const auto biasedPan = static_cast<u8>(rawPan + 0x80);
  if (version == CapcomSnesEngineVersion::v1BgmInList) {
    const double position = biasedPan == 255 ? 1.0 : biasedPan / 256.0;
    return StereoBalance{.leftGain = 1.0 - position, .rightGain = position};
  }

  const u16 rightPosition = static_cast<u16>(biasedPan) * 20;
  const u16 leftPosition = 0x1400 - rightPosition;
  const double left = interpolate(kPanCurve, leftPosition >> 8, leftPosition & 0xff) / 128.0;
  const double right = interpolate(kPanCurve, rightPosition >> 8, rightPosition & 0xff) / 128.0;
  return StereoBalance{.leftGain = left, .rightGain = right};
}

[[nodiscard]] double tremoloDepthDecibels(CapcomSnesEngineVersion version, u8 rawDepth) {
  int trough = 0;
  int peak = 250;
  if (version == CapcomSnesEngineVersion::v1BgmInList) {
    const int depth = rawDepth & 0x7f;
    trough = depth == 0 ? 255 : 255 - ((2 * depth * 255) >> 8);
    peak = 255;
  } else if (rawDepth == 0) {
    trough = 250;
  } else if (rawDepth >= 127) {
    trough = 0;
  } else {
    const int inversePosition = 0x7e81 - rawDepth * 255;
    const int curvePosition = inversePosition >> 3;
    trough = interpolate(kVolumeCurve, curvePosition >> 8, curvePosition & 0xff);
  }

  double depthCentibels = kTremoloMuteFloorCentibels;
  if (trough > 0) {
    depthCentibels =
        std::clamp(200.0 * std::log10(peak / static_cast<double>(trough)), 0.0, kTremoloMuteFloorCentibels);
  }
  // Shared synth and event-simulation LFOs are bipolar. NoBoost contributes
  // matching center attenuation, so their physical depth is half the driver's
  // peak-to-trough attenuation.
  return depthCentibels / 20.0;
}

}  // namespace math

[[nodiscard]] LfoPerformanceContext vibratoLfoContext() {
  return LfoPerformanceContext{
      .waveform = LfoWaveform::Triangle,
      .initialPhaseCycles = 0.0,
      .phaseRunsAtZeroDepth = true,
  };
}

[[nodiscard]] LfoPerformanceContext tremoloLfoContext() {
  return LfoPerformanceContext{
      .waveform = LfoWaveform::Triangle,
      // Capcom folds the shared phase so tremolo starts at its deepest
      // attenuation and then rises toward nominal gain.
      .initialPhaseCycles = 0.75,
      .phaseRunsAtZeroDepth = true,
      .tremoloGainMode = TremoloGainMode::NoBoost,
  };
}

struct ProgramState {
  u32 tempoMicrosecondsPerQuarter = 500000;
};

// Only registers that persist from one executed command to the next belong in
// track state. Source bounds and engine-version conversions are decode concerns.
struct TrackState {
  TrackState() = default;

  explicit TrackState(const SequenceProgram& program)
      : resetLfoPhaseOnNote(static_cast<CapcomSnesEngineVersion>(program.config.profile) !=
                            CapcomSnesEngineVersion::v1BgmInList) {}

  u8 durationRate256ths = 0;
  s8 transposeSemitones = 0;
  u8 noteOctave = 0;
  bool noteDotted = false;
  bool noteTriplet = false;
  bool noteSlurred = false;
  bool noteOctaveUp = false;
  bool resetLfoPhaseOnNote = true;
  double portamentoMillisecondsPerSemitone = 0.0;
  std::optional<double> lastPortamentoMilliseconds;
  std::optional<s32> lastSourceKey;
  PerformanceNoteId lastNote;
  bool lastNoteSlurred = false;
  bool didRest = false;
};

// Stateful driver behavior lives here only when it cannot be expressed as an
// obvious set/emit/VM operation in the command switch below.
struct Playback {
  TrackState& track;
  PerformanceEmitter& out;
  VmApi& vm;
  ProgramState& program;

  // Applies the packed note flags and emits a pedal change when slur mode changes.
  void applyAttributes(u8 attributes) {
    const bool wasSlurred = track.noteSlurred;
    // The driver merges the low octave bits into the current octave instead
    // of replacing it. Preserve that unusual behavior for source parity.
    track.noteOctave |= attributes & kNoteOctaveMask;
    track.noteDotted = track.noteDotted || ((attributes & kNoteDottedMask) != 0);
    track.noteOctaveUp = (attributes & kNoteOctaveUpMask) != 0;
    track.noteTriplet = (attributes & kNoteTripletMask) != 0;
    track.noteSlurred = (attributes & kNoteSlurredMask) != 0;
    if (track.noteSlurred != wasSlurred) {
      out.legatoPedal(track.noteSlurred);
    }
  }

  // Applies a repeat break's note flags only when that break is actually taken.
  [[nodiscard]] Effects repeatBreak(u8 slot, u8 attributes, Address destination) {
    const auto branch = vm.countedRepeatBreak(slot, destination);
    if (branch.taken) {
      applyAttributes(attributes);
    }
    return branch.effects;
  }

  // Advances through a rest and prevents the next note from extending the last one.
  [[nodiscard]] Effects rest(u8 durationIndex) {
    const u32 length = consumeNoteTicks(durationIndex);
    track.didRest = true;
    return Effects::wait(length);
  }

  void tempo(u32 microsecondsPerQuarter) {
    program.tempoMicrosecondsPerQuarter = microsecondsPerQuarter;
    out.tempo(microsecondsPerQuarter);
  }

  void vibratoDepth(double semitones) { out.vibratoDepth(semitones, vibratoLfoContext()); }

  void tremoloDepth(double decibels) { out.tremoloDepth(decibels, tremoloLfoContext()); }

  void lfoRates(double vibratoHertz, double tremoloHertz) {
    out.vibratoRate(vibratoHertz, vibratoLfoContext());
    out.tremoloRate(tremoloHertz, tremoloLfoContext());
  }

  // Calculates and emits one note, including slurs and portamento from the last note.
  [[nodiscard]] Effects note(u8 durationIndex, u8 keyIndex) {
    const u32 length = consumeNoteTicks(durationIndex);
    const s32 octave = static_cast<s32>(track.noteOctave) + (track.noteOctaveUp ? 2 : 0);
    const s32 key = static_cast<s32>(keyIndex) - 1 + octave * 12;
    const u32 duration = soundingTicks(length);
    const double outputKey = static_cast<double>(key + track.transposeSemitones);

    const bool continuesVoice = track.lastNoteSlurred && !track.didRest && track.lastNote.valid();
    const bool repeatsSourcePitch = continuesVoice && track.lastSourceKey && key == *track.lastSourceKey;
    NotePerformanceEvent event{
        .key = outputKey,
        .linearVelocity = 1.0,
        .durationTicks = duration + (track.noteSlurred && !repeatsSourcePitch ? 1u : 0u),
        // The driver decides whether this note is a new key-on from the
        // preceding note's slur bit. The current note's slur bit controls
        // whether the following note will be tied to this one.
        .restartsLfoPhase = track.resetLfoPhaseOnNote && !track.lastNoteSlurred,
    };

    if (repeatsSourcePitch) {
      // Repeating the target extends both the sounding voice and any glide
      // still approaching that target.
      event.note = track.lastNote;
      event.extendsPrevious = true;
      track.lastNote = out.note(std::move(event));
    } else {
      const PerformanceNoteId note = out.note(std::move(event));
      emitPitchSlideTo(note, key);
      track.lastNote = note;
    }

    track.lastSourceKey = key;
    track.didRest = false;
    track.lastNoteSlurred = track.noteSlurred;
    return Effects::wait(length);
  }

private:
  // Calculates one note's length and consumes the one-shot dotted-note flag.
  [[nodiscard]] u32 consumeNoteTicks(u8 rawDuration) {
    u32 length = math::baseNoteTicks(rawDuration);
    if (track.noteDotted) {
      // Dotted is a one-shot flag; triplet remains active until changed.
      length = (length % 2 == 0 && length < 0x80) ? length + (length / 2) : 0;
      track.noteDotted = false;
    } else if (track.noteTriplet) {
      length = length * 2 / 3;
    }
    return length;
  }

  // Shortens a note by the current duration rate unless it is slurred.
  [[nodiscard]] u32 soundingTicks(u32 length) const {
    u32 duration = length * track.durationRate256ths;
    if (track.noteSlurred || duration == 0) {
      duration = length << 8;
    }
    duration = (duration + 0x80) >> 8;
    return duration == 0 ? 1 : duration;
  }

  [[nodiscard]] u32 pitchSlideTicks(double milliseconds) const {
    const double ticks =
        milliseconds * 1000.0 * kCapcomSnesPpqn / std::max<u32>(program.tempoMicrosecondsPerQuarter, 1);
    return static_cast<u32>(
        std::clamp<double>(std::ceil(ticks), 1.0, static_cast<double>(std::numeric_limits<u32>::max())));
  }

  // Declares the driver's fixed-rate glide from the preceding note target.
  void emitPitchSlideTo(PerformanceNoteId note, s32 key) {
    if (!track.lastSourceKey) {
      return;
    }

    const bool continuesVoice = track.lastNoteSlurred && !track.didRest && track.lastNote.valid();
    if (track.portamentoMillisecondsPerSemitone <= 0.0 && !continuesVoice) {
      return;
    }
    const double startKey = static_cast<double>(*track.lastSourceKey + track.transposeSemitones);
    const double targetKey = static_cast<double>(key + track.transposeSemitones);
    if (std::abs(startKey - targetKey) < 0.000001) {
      return;
    }
    const double milliseconds = std::abs(targetKey - startKey) * track.portamentoMillisecondsPerSemitone;
    const PitchSlideTiming timing = milliseconds <= 0.0
                                        ? PitchSlideTiming::fromTicks(0)
                                        : PitchSlideTiming::fixedDuration(pitchSlideTicks(milliseconds), milliseconds);
    auto slide = out.pitchSlide(note, startKey, targetKey, timing);
    if (continuesVoice) {
      slide.continueFrom(track.lastNote);
    }
    if (track.lastPortamentoMilliseconds && std::abs(*track.lastPortamentoMilliseconds - milliseconds) < 0.000001) {
      slide.useCurrentPortamentoTiming();
    }
    track.lastPortamentoMilliseconds = milliseconds;
  }
};

using CapcomCursor = CompilerCursor<TrackState, Playback>;

// One source opcode is read and compiled in one local block. Simple commands
// show their complete behavior inline; only history-dependent driver behavior
// calls the nearby Playback methods above.
[[nodiscard]] DecodedBytecodeCommand decodeCommand(ByteReader reader, u32 begin, CapcomSnesEngineVersion version,
                                                   std::vector<Diagnostic>* diagnostics) {
  CapcomCursor cursor(reader, begin, "capcom-snes", diagnostics);
  if (!cursor.hasOpcode()) {
    return cursor.truncated();
  }

  // Notes and rests pack duration into the high three opcode bits. A zero key
  // in the low five bits denotes a rest.
  if (cursor.opcode() >= 0x20) {
    const u8 keyIndex = cursor.opcode() & 0x1f;
    if (keyIndex == 0) {
      auto event = cursor.command("Rest", SequenceSemantic::Rest);
      return event.invoke<&Playback::rest>(event.opcodeBits<5, 3>("duration_index"));
    }
    auto event = cursor.command("Note", SequenceSemantic::Note);
    const u8 durationIndex = event.opcodeBits<5, 3>("duration_index");
    event.opcodeValue("key_index", keyIndex);
    return event.invoke<&Playback::note>(durationIndex, keyIndex);
  }

  switch (cursor.opcode()) {
    case 0x00:
      return cursor.command("Toggle Triplet", SequenceSemantic::State).toggle<&TrackState::noteTriplet>();
    case 0x01: {
      auto event = cursor.command("Toggle Slur", SequenceSemantic::State);
      event.toggle<&TrackState::noteSlurred>();
      return event.emitLegatoPedal(event.state<&TrackState::noteSlurred>());
    }
    case 0x02:
      return cursor.command("Dotted Note", SequenceSemantic::State).set<&TrackState::noteDotted>(true);
    case 0x03:
      return cursor.command("Toggle Octave Up", SequenceSemantic::State).toggle<&TrackState::noteOctaveUp>();
    case 0x04: {
      auto event = cursor.command("Note Attributes", SequenceSemantic::State);
      return event.invoke<&Playback::applyAttributes>(event.u8("attributes", SourceValueDisplay::Hex));
    }
    case 0x05: {
      auto event = cursor.command("Tempo", SequenceSemantic::Tempo);
      const auto raw = event.rawU16be("raw");
      const u32 tempo = raw.valid ? math::tempoMicrosecondsPerQuarter(raw.value) : 0;
      static_cast<void>(
          event.resolvedValue("tempo", raw, tempoBeatsPerMinute(tempo), SourceValueDisplay::BeatsPerMinute));
      return event.invoke<&Playback::tempo>(tempo);
    }
    case 0x06: {
      auto event = cursor.command("Duration Rate", SequenceSemantic::State);
      return event.set<&TrackState::durationRate256ths>(event.u8("rate"));
    }
    case 0x07: {
      auto event = cursor.command("Volume", SequenceSemantic::Level);
      const auto raw = event.rawU8("raw");
      const double gain = event.resolvedValue("linear_gain", raw, math::volumeGain(version, raw.value));
      return event.emitLevel(gain, ValueQuantization{.levels = 256});
    }
    case 0x08: {
      auto event = cursor.command("Instrument", SequenceSemantic::Instrument);
      const u8 instrument = event.u8("instrument", SemanticOperandRole::Instrument);
      // The driver loads SRCN/ADSR/GAIN from the instrument table, but keeps
      // its separate per-voice release GAIN byte intact for the next key-off.
      return event.emitInstrument(kCapcomSnesInstrumentDomain, instrument,
                                  InstrumentEnvelopeMode::PreserveDynamicOverride);
    }
    case 0x09: {
      auto event = cursor.command("Octave", SequenceSemantic::State);
      return event.set<&TrackState::noteOctave>(event.u8("octave"));
    }
    case 0x0a: {
      auto event = cursor.command("Global Transpose", SequenceSemantic::Pitch);
      return event.emitGlobalTranspose(event.s8("semitones"));
    }
    case 0x0b: {
      auto event = cursor.command("Transpose", SequenceSemantic::Pitch);
      return event.set<&TrackState::transposeSemitones>(event.s8("semitones"));
    }
    case 0x0c: {
      auto event = cursor.command("Tuning", SequenceSemantic::Pitch);
      const auto tuning = event.rawS8("tuning");
      const double cents =
          event.resolvedValue("cents", tuning, tuning.value * (100.0 / 256.0), SourceValueDisplay::Cents);
      return event.emitTuning(cents);
    }
    case 0x0d: {
      auto event = cursor.command("Portamento Time", SequenceSemantic::Portamento);
      const double millisecondsPerSemitone =
          event.resolved("milliseconds_per_semitone", event.rawU8("time"), math::portamentoMillisecondsPerSemitone);
      return event.set<&TrackState::portamentoMillisecondsPerSemitone>(millisecondsPerSemitone);
    }
    case 0x0e:
    case 0x0f:
    case 0x10:
    case 0x11: {
      auto event = cursor.command("Repeat Until", SequenceSemantic::Repeat);
      // Four opcodes select independent counters. A nonzero source count is
      // one less than the total VM visit count; zero declares a loop.
      const u8 slot = event.derived("slot", static_cast<u8>(cursor.opcode() - 0x0e + 1));
      const u8 count = event.u8("count");
      const Address destination = event.address("destination", SemanticOperandRole::RepeatTarget);
      return count == 0 ? event.declaredLoop(destination) : event.repeatUntil(slot - 1, count + 1, destination);
    }
    case 0x12:
    case 0x13:
    case 0x14:
    case 0x15: {
      auto event = cursor.command("Repeat Break", SequenceSemantic::RepeatBreak);
      const u8 slot = event.derived("slot", static_cast<u8>(cursor.opcode() - 0x12 + 1));
      const u8 attributes = event.u8("attributes", SourceValueDisplay::Hex);
      const Address destination = event.address("destination", SemanticOperandRole::RepeatTarget);
      event.mayBranchTo(destination);
      return event.invoke<&Playback::repeatBreak>(slot - 1, attributes, destination);
    }
    case 0x16: {
      auto event = cursor.command("Jump", SequenceSemantic::Jump);
      const Address destination = event.address("destination", SemanticOperandRole::JumpTarget);
      return event.loopCandidate(destination);
    }
    case 0x17:
      return cursor.command("End", SequenceSemantic::End).end();
    case 0x18: {
      auto event = cursor.command("Pan", SequenceSemantic::Pan);
      const auto raw = event.rawU8("raw");
      const auto balance = math::stereoBalance(version, raw.value);
      const double leftGain = event.resolvedValue("left_gain", raw, balance.leftGain);
      const double rightGain = event.derived("right_gain", balance.rightGain);
      return event.emitStereoBalance(leftGain, rightGain);
    }
    case 0x19: {
      auto event = cursor.command("Master Volume", SequenceSemantic::Level);
      const auto raw = event.rawU8("raw");
      const double gain = event.resolvedValue("linear_gain", raw, math::volumeGain(version, raw.value));
      return event.emitMasterLevel(gain);
    }
    case 0x1a: {
      auto event = cursor.command("LFO", SequenceSemantic::Modulation);
      switch (static_cast<LfoParameter>(event.u8("type"))) {
        case LfoParameter::VibratoDepth: {
          const auto raw = event.rawU8("value", SourceValueDisplay::Hex);
          const u8 depth = raw.value & 0x7f;
          // The driver applies 128 depth steps across a +/- one-octave pitch range.
          const double semitones = event.resolvedValue("pitch_depth_semitones", raw, depth * (12.0 / 128.0));
          return event.invoke<&Playback::vibratoDepth>(semitones);
        }
        case LfoParameter::TremoloDepth: {
          const auto raw = event.rawU8("value", SourceValueDisplay::Hex);
          const double decibels =
              event.resolvedValue("depth_decibels", raw, math::tremoloDepthDecibels(version, raw.value));
          return event.invoke<&Playback::tremoloDepth>(decibels);
        }
        case LfoParameter::Rate: {
          const auto raw = event.rawU8("value", SourceValueDisplay::Hex);
          [[maybe_unused]] const bool phaseAdvancing =
              event.resolvedValue("phase_advancing", raw, raw.value != 0, SourceValueDisplay::Boolean);
          const double hertz = event.derived("frequency_hz", raw.value * kCapcomSnesLfoStepHertz);
          const double tremoloHertz = event.derived("tremolo_frequency_hz", 2.0 * hertz);

          // A zero speed freezes the shared oscillator at its current phase;
          // it does not disable either depth.
          return event.invoke<&Playback::lfoRates>(hertz, tremoloHertz);
        }
        case LfoParameter::ResetPhaseOnNote: {
          const auto raw = event.rawU8("value", SourceValueDisplay::Hex);
          const bool enabled =
              event.resolvedValue("reset_phase_on_note", raw, (raw.value & 1) != 0, SourceValueDisplay::Boolean);
          return event.set<&TrackState::resetLfoPhaseOnNote>(enabled);
        }
        default:
          event.u8("value", SourceValueDisplay::Hex);
          return event.ignore();
      }
    }
    case 0x1b: {
      auto event = cursor.sourceOnly("Echo Param");
      event.u8("argument", SourceValueDisplay::Hex);
      event.u8("preset", SourceValueDisplay::Hex);
      return event.ignore();
    }
    case 0x1c: {
      auto event = cursor.command("Echo On/Off", SequenceSemantic::Meta);
      const auto raw = event.rawU8("raw");
      const bool enabled = event.resolvedValue("enabled", raw, (raw.value & 1) != 0, SourceValueDisplay::Boolean);
      return event.emitReverb(enabled ? 40.0 / 127.0 : 0.0);
    }
    case 0x1d: {
      auto event = cursor.command("Release Rate", SequenceSemantic::Envelope);
      const auto raw = event.rawU8("raw");
      const u8 gain = event.derived("gain", static_cast<u8>(raw.value | 0xa0), SourceValueDisplay::Hex);
      // Normalize the GAIN rate from full ENVX so the same sticky override can
      // be applied to any instrument selected later on this track.
      const double releaseSeconds =
          event.resolvedValue("release_seconds", raw, snesDspGainEnvelopeSeconds(gain, 0x7ff, 0));
      return event.emitEnvelopeField<EnvelopeFields::Release>(releaseSeconds,
                                                              VoiceEnvelopeScope::ActiveVoicesAndFutureAttacks);
    }
    case 0x1e:
    case 0x1f:
      if (version == CapcomSnesEngineVersion::v1BgmInList) {
        return cursor.ignored("Unknown One-Byte Event", 1, "unknown-one-byte");
      }
      return cursor.noOp("No Operation", "nop");
    default:
      return cursor.unsupported("Unsupported").stop();
  }
}

}  // namespace

const SequenceDialect& capcomSnesSequenceDialect() {
  static const SequenceDialect dialect = makeCompiledDialect<TrackState, Playback, ProgramState>(SequenceDialect{
      .id = DialectId{.value = "capcom-snes"},
      .commandDetailKindPrefix = "capcom-snes",
      .timebase = Timebase{.ppqn = kCapcomSnesPpqn},
      .defaultBehavior =
          SequenceProgramBehavior{
              .defaultLoopPolicy = LoopPolicy::PlayOnce,
              .initialReverbSend = 0.0,
              .initialStereoBalance = omitInitialStereoBalance,
              .initialMonoModeChannels = 0,
          },
  });
  return dialect;
}

// Decodes one known track directly for focused tests and callers that already
// know its starting address.
TrackProgram decodeCapcomSnesSourceTrack(ByteReader reader, CapcomSnesEngineVersion version,
                                         CapcomSnesTrackDecodeOptions options) {
  const TrackDecodeScope tracks{
      .reader = reader,
      .sourceMap = options.sourceMap,
  };
  return tracks.linear(options.trackIndex, options.startOffset,
                       [&](u32 offset) { return decodeCommand(reader, offset, version, options.diagnostics); });
}

// Reads the eight track pointers from the discovered song header and decodes
// every track that is present.
SequenceProgram decodeCapcomSnesSequence(ByteReader reader, const CapcomSnesLayout& layout, AssetId sequenceId,
                                         SourceMapBuilder* sourceMap, std::vector<Diagnostic>* diagnostics) {
  SequenceDecodeSession sequence{
      reader, capcomSnesSequenceDialect(), sequenceId, layout.sequenceHeaderRange, sourceMap,
  };
  const auto decode = [&](u32 offset) { return decodeCommand(reader, offset, layout.version, diagnostics); };

  // Capcom stores the pointer slots in reverse track order.
  for (u32 sourceTrackNumber = 0; sourceTrackNumber < kCapcomSnesMaxTracks; ++sourceTrackNumber) {
    const u32 pointerIndex = kCapcomSnesMaxTracks - 1 - sourceTrackNumber;
    const u32 pointerOffset = layout.trackPointerTableAddress + pointerIndex * 2;
    const u16 trackAddress = reader.be16(pointerOffset);
    if (trackAddress == 0) {
      continue;
    }

    sequence.addLinearTrack(sourceTrackNumber, reader.range(pointerOffset, 2), trackAddress, decode);
  }
  SequenceProgram program = sequence.finish();
  program.config.profile = static_cast<u32>(layout.version);
  return program;
}

}  // namespace vgmtrans::formats::capcom_snes
