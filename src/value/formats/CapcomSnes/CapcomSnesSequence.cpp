/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/CapcomSnes/CapcomSnes.h"

#include "value/base/LevelScale.h"
#include "value/sequence/BytecodeDecode.h"
#include "value/sequence/CommandSourceMap.h"
#include "value/sequence/CompilerCursor.h"
#include "value/sequence/SequenceVm.h"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>
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
};

namespace math {

constexpr std::array<u8, 17> kVolumeCurve{0x00, 0x0c, 0x19, 0x26, 0x33, 0x40, 0x4c, 0x59, 0x66,
                                          0x73, 0x80, 0x8c, 0x99, 0xb3, 0xcc, 0xe6, 0xff};
constexpr std::array<u8, 22> kPanCurve{0x00, 0x01, 0x03, 0x07, 0x0d, 0x15, 0x1e, 0x29, 0x34, 0x42, 0x51,
                                       0x5e, 0x67, 0x6e, 0x73, 0x77, 0x7a, 0x7c, 0x7d, 0x7e, 0x7f, 0x7f};
constexpr double kVibratoBaseHertz = kCapcomSnesLfoStepHertz;
constexpr double kVibratoMaxHertz = 255.0 * kCapcomSnesLfoStepHertz;
constexpr double kTremoloMuteFloorCentibels = 960.0;

struct StereoBalance {
  double leftGain = 1.0;
  double rightGain = 1.0;
};

[[nodiscard]] int interpolate(const auto& table, int index, int fraction) {
  const int lower = table[index];
  const int upper = table[index + 1];
  return lower + (((upper - lower) * fraction) >> 8);
}

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

[[nodiscard]] double tuningCents(s8 tuning) {
  return static_cast<double>(tuning) * (100.0 / 256.0);
}

[[nodiscard]] double portamentoMillisecondsPerCent(u8 rawTime) {
  const u8 step = static_cast<u8>((rawTime << 1) & 0xff);
  const double centsPerUpdate = step * (100.0 / 256.0);
  return centsPerUpdate == 0.0 ? 0.0 : (0.016 / centsPerUpdate) * 1000.0;
}

[[nodiscard]] u32 baseNoteTicks(u8 rawDuration) {
  return rawDuration == 0 || rawDuration > 7 ? 0 : 192u >> (7u - rawDuration);
}

[[nodiscard]] u32 tempoMicrosecondsPerQuarter(u16 rawTempo) {
  return rawTempo == 0 ? 60000000 : static_cast<u32>(std::round(kCapcomSnesPpqn * (125 * 0x40) * 2 * 256.0 / rawTempo));
}

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

[[nodiscard]] double normalizedDepth(u8 value) {
  return static_cast<double>(value) / 127.0;
}

[[nodiscard]] double vibratoDepthSemitones(u8 rawDepth) {
  // The driver applies 128 depth steps across a +/- one-octave pitch range.
  return static_cast<double>(rawDepth) * 12.0 / 128.0;
}

[[nodiscard]] double vibratoRateHertz(u8 rawRate) {
  return rawRate * kCapcomSnesLfoStepHertz;
}

[[nodiscard]] double tremoloDepth(CapcomSnesEngineVersion version, u8 rawDepth) {
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
  // The driver rounds against 128 steps before clamping to a 7-bit controller.
  // Preserve that quantization so the neutral amount lowers back to the same value.
  const int midiValue =
      static_cast<int>(std::floor(depthCentibels * 128.0 / (2.0 * kCapcomSnesTremoloHalfDepthCentibels) + 0.5));
  return std::clamp(midiValue, 0, 127) / 127.0;
}

[[nodiscard]] double lfoRate(u8 rawRate) {
  if (rawRate == 0) {
    return 0.0;
  }
  const auto cents = [](double hertz) { return 1200.0 * std::log2(hertz / 440.0) + 6900.0; };
  const double position = (cents(rawRate * kCapcomSnesLfoStepHertz) - cents(kVibratoBaseHertz)) /
                          (cents(kVibratoMaxHertz) - cents(kVibratoBaseHertz));
  return std::clamp(position, 0.0, 1.0);
}

}  // namespace math

// Only registers that persist from one executed command to the next belong in
// track state. Source bounds and engine-version conversions are decode concerns.
struct TrackState {
  u8 durationRate256ths = 0;
  s8 transposeSemitones = 0;
  u8 noteOctave = 0;
  bool noteDotted = false;
  bool noteTriplet = false;
  bool noteSlurred = false;
  bool noteOctaveUp = false;
  bool modulationEnabled = false;
  double vibratoAmount = 0.0;
  double vibratoDepthSemitones = 0.0;
  double tremoloAmount = 0.0;
  double portamentoMillisecondsPerCent = 0.0;
  u16 lastPortamentoMilliseconds = 0;
  std::optional<s32> lastSourceKey;
  bool lastNoteSlurred = false;
  bool didRest = false;
};

// Stateful driver behavior lives here only when it cannot be expressed as an
// obvious set/emit/VM operation in the command switch below.
struct Playback {
  TrackState& track;
  PerformanceEmitter& out;
  VmApi& vm;

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

  void setModulationEnabled(bool enabled) {
    if (enabled == track.modulationEnabled) {
      return;
    }
    track.modulationEnabled = enabled;
    if (track.vibratoAmount != 0.0 || track.vibratoDepthSemitones != 0.0) {
      out.modulation(ModulationPerformanceEvent{
          .target = ModulationPerformanceTarget::VibratoDepth,
          .amount = enabled ? track.vibratoAmount : 0.0,
          .pitchDepthSemitones = enabled ? track.vibratoDepthSemitones : 0.0,
      });
    }
    if (track.tremoloAmount != 0.0) {
      out.modulation(ModulationPerformanceTarget::TremoloDepth, enabled ? track.tremoloAmount : 0.0);
    }
  }

  [[nodiscard]] Effects repeatBreak(u8 slot, u8 attributes, Address destination) {
    const auto branch = vm.countedRepeatBreak(slot, destination);
    if (branch.taken) {
      applyAttributes(attributes);
    }
    return branch.effects;
  }

  [[nodiscard]] Effects rest(u8 durationIndex) {
    const u32 length = consumeNoteTicks(durationIndex);
    track.didRest = true;
    return Effects::wait(length);
  }

  [[nodiscard]] Effects note(u8 durationIndex, u8 keyIndex) {
    const u32 length = consumeNoteTicks(durationIndex);
    const s32 key = sourceKey(keyIndex);
    const u32 duration = soundingTicks(length);

    // Consecutive slurred notes at the same source pitch extend the existing
    // note instead of retriggering it.
    if (track.lastNoteSlurred && track.lastSourceKey && key == *track.lastSourceKey && !track.didRest) {
      out.note(static_cast<double>(key + track.transposeSemitones), 1.0, duration, true);
      rememberNote(key);
      return Effects::wait(length);
    }

    emitPortamentoTo(key);
    out.note(static_cast<double>(key + track.transposeSemitones), 1.0, duration + (track.noteSlurred ? 1u : 0u));
    rememberNote(key);
    return Effects::wait(length);
  }

private:
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

  [[nodiscard]] u32 soundingTicks(u32 length) const {
    u32 duration = length * track.durationRate256ths;
    if (track.noteSlurred || duration == 0) {
      duration = length << 8;
    }
    duration = (duration + 0x80) >> 8;
    return duration == 0 ? 1 : duration;
  }

  [[nodiscard]] s32 sourceKey(u8 keyIndex) const {
    return static_cast<s32>(keyIndex) - 1 + static_cast<s32>(track.noteOctave * 12) + (track.noteOctaveUp ? 24 : 0);
  }

  void emitPortamentoTo(s32 key) {
    if (track.portamentoMillisecondsPerCent <= 0.0 || !track.lastSourceKey) {
      return;
    }

    const auto distance = static_cast<u32>(std::abs(key - *track.lastSourceKey));
    const auto portamentoTime = static_cast<u16>(distance * 100 * track.portamentoMillisecondsPerCent);
    const double previousKey = static_cast<double>(*track.lastSourceKey + track.transposeSemitones);
    if (portamentoTime != track.lastPortamentoMilliseconds) {
      out.portamento(static_cast<double>(portamentoTime), previousKey);
      track.lastPortamentoMilliseconds = portamentoTime;
    } else {
      out.portamentoControl(previousKey);
    }
  }

  void rememberNote(s32 key) {
    track.lastSourceKey = key;
    track.didRest = false;
    track.lastNoteSlurred = track.noteSlurred;
  }
};

using CapcomCursor = CompilerCursor<TrackState, Playback>;
using CapcomCompiledDialect = CompiledCommandDialect<TrackState, Playback>;

// One source opcode is read and compiled in one local block. Simple commands
// show their complete behavior inline; only history-dependent driver behavior
// calls the nearby Playback methods above.
[[nodiscard]] DecodedBytecodeCommand decodeCommand(ByteReader reader, u32 begin, u32 end,
                                                   CapcomSnesEngineVersion version,
                                                   std::vector<Diagnostic>* diagnostics) {
  CapcomCursor cursor(reader, begin, end, "capcom-snes", diagnostics);
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
      const u32 tempo =
          event.resolved("microseconds_per_quarter", event.rawU16be("raw"), math::tempoMicrosecondsPerQuarter);
      return event.emitTempo(tempo);
    }
    case 0x06: {
      auto event = cursor.command("Duration Rate", SequenceSemantic::State);
      return event.set<&TrackState::durationRate256ths>(event.u8("rate"));
    }
    case 0x07: {
      auto event = cursor.command("Volume", SequenceSemantic::Level);
      const auto raw = event.rawU8("raw");
      const double gain = event.resolvedValue("linear_gain", raw, math::volumeGain(version, raw.value));
      return event.emitLevel(LevelScale::linearFromLinear(gain), ValueQuantization{.levels = 256});
    }
    case 0x08: {
      auto event = cursor.command("Instrument", SequenceSemantic::Instrument);
      const u8 instrument = event.u8("instrument", SemanticOperandRole::Instrument);
      return event.emitInstrument(kCapcomSnesInstrumentDomain, instrument);
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
      const double cents = event.resolved("cents", event.rawS8("tuning"), math::tuningCents, SourceValueDisplay::Cents);
      return event.emitTuning(cents);
    }
    case 0x0d: {
      auto event = cursor.command("Portamento Time", SequenceSemantic::Portamento);
      const double millisecondsPerCent =
          event.resolved("milliseconds_per_cent", event.rawU8("time"), math::portamentoMillisecondsPerCent);
      return event.set<&TrackState::portamentoMillisecondsPerCent>(millisecondsPerCent);
    }
    case 0x0e:
    case 0x0f:
    case 0x10:
    case 0x11: {
      auto event = cursor.command("Repeat Until", SequenceSemantic::Repeat, CommandPlaybackStatus::AffectsControlFlow);
      // Four opcodes select independent counters. A nonzero source count is
      // one less than the total VM visit count; zero declares a loop.
      const u8 slot = event.derived("slot", static_cast<u8>(cursor.opcode() - 0x0e + 1));
      const u8 count = event.u8("count");
      const Address destination = event.address("destination", SemanticOperandRole::RepeatTarget);
      return count == 0 ? event.declaredLoop(destination, SemanticOperandRole::RepeatTarget)
                        : event.repeatUntil(slot - 1, count + 1, destination);
    }
    case 0x12:
    case 0x13:
    case 0x14:
    case 0x15: {
      auto event =
          cursor.command("Repeat Break", SequenceSemantic::RepeatBreak, CommandPlaybackStatus::AffectsControlFlow);
      const u8 slot = event.derived("slot", static_cast<u8>(cursor.opcode() - 0x12 + 1));
      const u8 attributes = event.u8("attributes", SourceValueDisplay::Hex);
      const Address destination = event.address("destination", SemanticOperandRole::RepeatTarget);
      event.mayBranchTo(destination, SemanticOperandRole::RepeatTarget);
      return event.invoke<&Playback::repeatBreak>(slot - 1, attributes, destination);
    }
    case 0x16: {
      auto event = cursor.command("Jump", SequenceSemantic::Jump, CommandPlaybackStatus::AffectsControlFlow);
      const Address destination = event.address("destination", SemanticOperandRole::JumpTarget);
      return event.loopCandidate(destination, SemanticOperandRole::JumpTarget);
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
      return event.emitMasterLevel(LevelScale::linearFromLinear(gain));
    }
    case 0x1a: {
      auto event = cursor.command("LFO", SequenceSemantic::Modulation);
      switch (static_cast<LfoParameter>(event.u8("type"))) {
        case LfoParameter::VibratoDepth: {
          const auto raw = event.rawU8("value", SourceValueDisplay::Hex);
          const u8 depth = raw.value & 0x7f;
          const double amount = event.resolvedValue("amount", raw, math::normalizedDepth(depth));
          const double semitones = event.derived("pitch_depth_semitones", math::vibratoDepthSemitones(depth));
          const auto modulationEnabled = event.state<&TrackState::modulationEnabled>();
          event.set<&TrackState::vibratoAmount>(amount);
          event.set<&TrackState::vibratoDepthSemitones>(semitones);
          return event.emitModulation(ModulationPerformanceTarget::VibratoDepth,
                                      event.select(modulationEnabled, amount, 0.0),
                                      event.select(modulationEnabled, semitones, 0.0));
        }
        case LfoParameter::TremoloDepth: {
          const auto raw = event.rawU8("value", SourceValueDisplay::Hex);
          const double amount = event.resolvedValue("amount", raw, math::tremoloDepth(version, raw.value));
          const auto modulationEnabled = event.state<&TrackState::modulationEnabled>();
          event.set<&TrackState::tremoloAmount>(amount);
          return event.emitModulation(ModulationPerformanceTarget::TremoloDepth,
                                      event.select(modulationEnabled, amount, 0.0));
        }
        case LfoParameter::Rate: {
          // One rate gates both remembered depths. Zero disables modulation
          // without forgetting either configured depth.
          const auto raw = event.rawU8("value", SourceValueDisplay::Hex);
          const bool enabled = event.resolvedValue("enabled", raw, raw.value != 0, SourceValueDisplay::Boolean);
          const double amount = event.derived("amount", math::lfoRate(raw.value));
          const double hertz = event.derived("frequency_hz", math::vibratoRateHertz(raw.value));
          return event.invoke<&Playback::setModulationEnabled>(enabled)
              .emitVibratoRate(amount, hertz)
              .emitModulation(ModulationPerformanceTarget::TremoloRate, amount);
        }
        default:
          static_cast<void>(event.u8("value", SourceValueDisplay::Hex));
          return event.ignore();
      }
    }
    case 0x1b: {
      auto event = cursor.sourceOnly("Echo Param");
      static_cast<void>(event.u8("argument", SourceValueDisplay::Hex));
      static_cast<void>(event.u8("preset", SourceValueDisplay::Hex));
      return event.ignore();
    }
    case 0x1c: {
      auto event = cursor.command("Echo On/Off", SequenceSemantic::Meta);
      const auto raw = event.rawU8("raw");
      const bool enabled = event.resolvedValue("enabled", raw, (raw.value & 1) != 0, SourceValueDisplay::Boolean);
      return event.emitReverb(enabled ? 40.0 / 127.0 : 0.0);
    }
    case 0x1d: {
      auto event = cursor.sourceOnly("Release Rate");
      const auto raw = event.rawU8("raw");
      static_cast<void>(event.resolvedValue("gain", raw, static_cast<u32>(raw.value | 0xa0), SourceValueDisplay::Hex));
      return event.ignore();
    }
    case 0x1e:
    case 0x1f:
      if (version == CapcomSnesEngineVersion::v1BgmInList) {
        auto event = cursor.sourceOnly("Unknown One-Byte Event", "unknown-one-byte");
        static_cast<void>(event.u8("value", SourceValueDisplay::Hex));
        return event.ignore();
      }
      return cursor.noOp("No Operation", "nop");
    default:
      return cursor.unsupported("Unsupported").stop();
  }
}

[[nodiscard]] TrackProgram decodeTrack(const TrackDecodeScope& tracks, CapcomSnesEngineVersion version, u32 trackIndex,
                                       u32 startOffset, std::vector<Diagnostic>* diagnostics) {
  const u32 end = std::min(static_cast<u32>(tracks.reader.size()), tracks.bytecodeEnd);
  return tracks.linear(trackIndex, startOffset,
                       [&](u32 offset) { return decodeCommand(tracks.reader, offset, end, version, diagnostics); });
}

[[nodiscard]] SequenceDialect makeDialect() {
  return SequenceDialect{
      .id = DialectId{.value = "capcom-snes"},
      .commandDetailKindPrefix = "capcom-snes",
      .timebase = Timebase{.ppqn = kCapcomSnesPpqn},
      .defaultBehavior =
          SequenceProgramBehavior{
              .defaultLoopPolicy = LoopPolicy::PlayOnce,
              .initialReverbSend = 0.0,
              .initialMonoModeChannels = 0,
          },
      .createSemanticTrackState = CapcomCompiledDialect::createTrackState,
      .executeSemantic = CapcomCompiledDialect::execute,
  };
}

}  // namespace

const SequenceDialect& capcomSnesSequenceDialect() {
  static const SequenceDialect dialect = makeDialect();
  return dialect;
}

TrackProgram decodeCapcomSnesSourceTrack(ByteReader reader, CapcomSnesEngineVersion version,
                                         CapcomSnesTrackDecodeOptions options) {
  const TrackDecodeScope tracks{
      .reader = reader,
      .sourceMap = options.sourceMap,
  };
  return decodeTrack(tracks, version, options.trackIndex, options.startOffset, options.diagnostics);
}

SequenceProgram decodeCapcomSnesSequence(ByteReader reader, const CapcomSnesLayout& layout, AssetId sequenceId,
                                         SourceRange sequenceRange, SourceMapBuilder* sourceMap,
                                         std::vector<Diagnostic>* diagnostics) {
  SourceAnnotationId headerAnnotation;
  if (sourceMap != nullptr) {
    headerAnnotation = sourceMap->header("Sequence Header", sequenceRange)
                           .kind("capcom-snes-sequence-header")
                           .owner(ObjectRefs::sequence(sequenceId))
                           .id();
  }

  const auto& dialect = capcomSnesSequenceDialect();
  SequenceProgram program = dialect.makeProgram();
  const TrackDecodeScope tracks{
      .reader = reader,
      .sequenceAsset = sequenceId,
      .sourceMap = sourceMap,
  };
  const u32 pointerBase = layout.sequenceHeaderAddress + (layout.priorityInHeader ? 1 : 0);
  // Capcom stores the pointer slots in reverse track order.
  for (u32 sourceTrackNumber = 0; sourceTrackNumber < kCapcomSnesMaxTracks; ++sourceTrackNumber) {
    const u32 pointerIndex = kCapcomSnesMaxTracks - 1 - sourceTrackNumber;
    const u32 pointerOffset = pointerBase + pointerIndex * 2;
    const SourceRange pointerRange = reader.range(pointerOffset, 2);
    const u16 trackAddress = reader.be16(pointerOffset);
    if (trackAddress == 0) {
      continue;
    }

    std::optional<SourceAnnotationId> pointerAnnotation;
    if (sourceMap != nullptr) {
      auto annotation = sourceMap->pointer("Track Pointer", pointerRange, SourceTarget{reader.range(trackAddress, 1)})
                            .kind("capcom-snes-track-pointer")
                            .description(fmt::format("Track starts at ${:04X}", trackAddress))
                            .derived("source_track", sourceTrackNumber)
                            .field("destination", pointerRange, trackAddress, SourceValueDisplay::Address);
      if (headerAnnotation.valid()) {
        annotation.parent(headerAnnotation);
      }
      pointerAnnotation = annotation.id();
    }

    program.tracks.push_back(decodeTrack(tracks.withParent(pointerAnnotation), layout.version, sourceTrackNumber,
                                         trackAddress, diagnostics));
  }
  return program;
}

}  // namespace vgmtrans::formats::capcom_snes
