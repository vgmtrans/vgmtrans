/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/NDS/Nds.h"

#include "value/base/LevelScale.h"
#include "value/sequence/BytecodeDecode.h"
#include "value/sequence/CommandSourceMap.h"
#include "value/sequence/CompiledCommandDialect.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace vgmtrans::formats::nds {

using namespace core;

namespace {

constexpr u32 kMaxTrackCommands = 262144;
constexpr u32 kSseqDataOffsetField = 0x18;
constexpr u32 kSseqHeaderSize = 0x1c;
constexpr double kPitchUnitsPerSemitone = 64.0;
constexpr double kDriverSweepsPerSecond = 192.0;
constexpr double kDriverTempoBase = 240.0;
constexpr double kLfoPhaseStepsPerCycle = 512.0;
constexpr double kNitroSinePeak = 127.0;

struct PendingBlock {
  u32 offset = 0;
  bool callTarget = false;
};

struct ProgramState {
  u16 tempoBpm = 120;
};

struct LfoState {
  u8 target = 0;
  u8 speed = 16;
  u8 depth = 0;
  u8 range = 1;
  u16 delay = 0;
  bool emitted = false;
};

// Only registers that persist from one executed source command to the next
// belong here. Source bounds and relative-address bases are decode concerns.
struct TrackState {
  explicit TrackState(const TrackProgram& program)
      : usesModulation(trackUsesSemantic(program, SequenceSemantic::Modulation)) {}

  bool usesModulation = false;
  bool noteWait = false;
  bool tie = false;
  bool portamento = false;
  bool tiedChannelAutoSweep = true;
  s32 transpose = 0;
  u8 pitchBendRangeSemitones = 2;
  u8 portamentoKey = 60;
  u8 portamentoTime = 0;
  s16 sweepPitch = 0;
  std::optional<PerformanceNoteId> tiedNote;
  LfoState lfo;
};

// Only driver behavior that depends on runtime track history needs a method.
// Ordinary commands compile directly to VM actions in decodeCommand below.
struct Playback {
  TrackState& track;
  PerformanceEmitter& out;
  VmApi& vm;
  ProgramState& program;

  void tie(bool enabled) {
    track.tie = enabled;
    track.tiedNote.reset();
    track.tiedChannelAutoSweep = true;
  }

  void portamentoControl(u8 sourceKey) {
    track.portamentoKey = static_cast<u8>(static_cast<s32>(sourceKey) + track.transpose);
    track.portamento = true;
  }

  void tempo(u16 bpm) {
    program.tempoBpm = bpm;
    out.tempo(static_cast<u32>(std::round(60000000.0 / bpm)));
  }

  void emitPitchSlide(PerformanceNoteId note, double startKey, double targetKey, PitchSlideTiming timing) {
    auto slide = out.pitchSlide(note, startKey, targetKey, timing);
    if (track.portamento) {
      slide.preferPortamento();
    } else {
      slide.preferPitchBend();
    }
  }

  [[nodiscard]] double lfoFrequencyHz() const {
    return track.lfo.speed * kDriverSweepsPerSecond / kLfoPhaseStepsPerCycle;
  }

  [[nodiscard]] u32 lfoDelayTicks() const {
    return static_cast<u32>(std::llround(track.lfo.delay * static_cast<double>(program.tempoBpm) / kDriverTempoBase));
  }

  [[nodiscard]] double lfoDelayMilliseconds() const { return track.lfo.delay * 1000.0 / kDriverSweepsPerSecond; }

  [[nodiscard]] LfoPerformanceContext lfoContext() const {
    return LfoPerformanceContext{
        .frequencyHz = lfoFrequencyHz(),
        .delayTicks = lfoDelayTicks(),
        .delayMilliseconds = lfoDelayMilliseconds(),
        .waveform = LfoWaveform::Sine,
        .phaseRunsAtZeroDepth = true,
    };
  }

  void emitLfoRate(ModulationPerformanceTarget target) {
    const double hertz = lfoFrequencyHz();
    auto context = lfoContext();
    switch (target) {
      case ModulationPerformanceTarget::VibratoRate:
        out.vibratoRate(hertz, std::move(context));
        break;
      case ModulationPerformanceTarget::TremoloRate:
        out.tremoloRate(hertz, std::move(context));
        break;
      case ModulationPerformanceTarget::PanRate:
        out.panLfoRate(hertz, std::move(context));
        break;
      case ModulationPerformanceTarget::VibratoDepth:
      case ModulationPerformanceTarget::TremoloDepth:
      case ModulationPerformanceTarget::PanDepth:
        break;
    }
  }

  void emitLfoRates() {
    emitLfoRate(ModulationPerformanceTarget::VibratoRate);
    emitLfoRate(ModulationPerformanceTarget::TremoloRate);
    emitLfoRate(ModulationPerformanceTarget::PanRate);
  }

  void emitLfoDelayControls() {
    const double seconds = track.lfo.delay / kDriverSweepsPerSecond;
    out.vibratoDelayPhysical(lfoDelayTicks(), seconds * 1000.0);
    out.tremoloDelayPhysical(lfoDelayTicks(), seconds * 1000.0);
  }

  void emitLfoDepth(u8 target, u8 depth) {
    const double scaledDepth = static_cast<double>(depth) * track.lfo.range;
    switch (target) {
      case 0: {
        const double physicalDepth = kNitroSinePeak * scaledDepth / 16384.0;
        out.vibratoDepth(physicalDepth, lfoContext());
        break;
      }
      case 1: {
        const double physicalDepth = kNitroSinePeak * scaledDepth * 60.0 / 163840.0;
        out.tremoloDepth(physicalDepth, lfoContext());
        break;
      }
      case 2: {
        const double physicalDepth = scaledDepth / 128.0;
        out.panLfoDepth(physicalDepth, lfoContext());
        break;
      }
      default:
        break;
    }
  }

  void emitAllLfoDepths() {
    emitLfoDepth(0, track.lfo.target == 0 ? track.lfo.depth : 0);
    emitLfoDepth(1, track.lfo.target == 1 ? track.lfo.depth : 0);
    emitLfoDepth(2, track.lfo.target == 2 ? track.lfo.depth : 0);
  }

  bool initializeLfo() {
    if (!track.usesModulation) {
      return false;
    }
    if (track.lfo.emitted) {
      return false;
    }
    track.lfo.emitted = true;
    emitLfoDelayControls();
    emitLfoRates();
    emitAllLfoDepths();
    return true;
  }

  void modulationDepth(u8 depth) {
    track.lfo.depth = depth;
    if (!initializeLfo()) {
      emitLfoDepth(track.lfo.target, depth);
    }
  }

  void modulationSpeed(u8 speed) {
    track.lfo.speed = speed;
    if (!initializeLfo()) {
      emitLfoRates();
    }
  }

  void modulationTarget(u8 target) {
    const u8 previous = track.lfo.target;
    track.lfo.target = target;
    if (!initializeLfo()) {
      emitLfoDepth(previous, 0);
      emitLfoDepth(target, track.lfo.depth);
    }
  }

  void modulationRange(u8 range) {
    track.lfo.range = range;
    if (!initializeLfo()) {
      emitLfoDepth(track.lfo.target, track.lfo.depth);
    }
  }

  void modulationDelay(u16 delay) {
    track.lfo.delay = delay;
    if (!initializeLfo()) {
      emitLfoDelayControls();
      emitLfoRates();
    }
  }

  [[nodiscard]] Effects note(u8 sourceKey, u8 velocity, u32 duration) {
    static_cast<void>(initializeLfo());
    const s32 key = std::clamp<s32>(static_cast<s32>(sourceKey) + track.transpose, 0, 127);
    const bool continuesTiedVoice = track.tie && track.tiedNote.has_value();
    const PerformanceNoteId note =
        out.note(static_cast<double>(key), LevelScale::linearFromMidi7(velocity), duration, continuesTiedVoice);

    s32 sweep = track.sweepPitch;
    if (track.portamento) {
      sweep += (static_cast<s32>(track.portamentoKey) - key) * static_cast<s32>(kPitchUnitsPerSemitone);
    }
    sweep &= 0xffff;
    if (sweep >= 0x8000) {
      sweep -= 0x10000;
    }
    const double startKey = static_cast<double>(key) + sweep / kPitchUnitsPerSemitone;

    std::optional<PitchSlideTiming> timing;
    if (sweep != 0) {
      if (track.portamentoTime == 0) {
        if (duration != 0) {
          timing = PitchSlideTiming::fromTicks(duration);
        }
      } else {
        // Nitro squares CF, scales it by pitch distance, and counts the
        // resulting length on its 192 Hz sound-thread clock.
        const u32 sweepLength = static_cast<u32>(
            (static_cast<u64>(track.portamentoTime) * track.portamentoTime * std::abs(static_cast<s64>(sweep))) >> 11);
        if (sweepLength != 0) {
          // A reused tied channel retains note-tick timing after a zero CF;
          // changing CF alone does not restore its automatic sweep clock.
          if (continuesTiedVoice && !track.tiedChannelAutoSweep) {
            timing = PitchSlideTiming::fromTicks(sweepLength);
          } else {
            const double timelineTicks = sweepLength * static_cast<double>(program.tempoBpm) / kDriverTempoBase;
            timing = PitchSlideTiming::fixedDuration(std::max<u32>(1, static_cast<u32>(std::llround(timelineTicks))),
                                                     sweepLength * 1000.0 / kDriverSweepsPerSecond);
          }
        }
      }
    }

    if (timing) {
      emitPitchSlide(note, startKey, static_cast<double>(key), *timing);
    } else if (continuesTiedVoice && track.portamentoKey != key) {
      emitPitchSlide(note, static_cast<double>(track.portamentoKey), static_cast<double>(key),
                     PitchSlideTiming::fromTicks(0));
    }

    if (track.tie) {
      track.tiedNote = note;
      if (!continuesTiedVoice) {
        track.tiedChannelAutoSweep = true;
      }
      if (track.portamentoTime == 0) {
        track.tiedChannelAutoSweep = false;
      }
    }
    track.portamentoKey = static_cast<u8>(key);
    return track.noteWait ? Effects::wait(duration) : Effects{};
  }

  void pitchBend(s8 encoded) { out.pitchBend((encoded / 128.0) * track.pitchBendRangeSemitones); }
};

using NdsCompilerCursor = CompilerCursor<TrackState, Playback>;

// NDS-specific decode state stays beside the shared track-discovery service.
// Relative addresses and malformed-range policy are SSEQ semantics, not generic
// bytecode-walker configuration.
struct SequenceDecodeContext {
  TrackDecodeScope tracks;
  NdsSequenceRange range;
  std::vector<Diagnostic>* diagnostics = nullptr;

  [[nodiscard]] const ByteReader& reader() const noexcept { return tracks.reader; }
  [[nodiscard]] u32 dataBase() const noexcept { return range.offset + kSseqHeaderSize; }
};

// Reads a three-byte relative address and records both that value and the final
// destination it points to.
[[nodiscard]] Address targetAddress(const SequenceDecodeContext& context, NdsCompilerCursor::Event& event,
                                    SemanticOperandRole role) {
  const u32 relative = event.u24le("relative", SourceValueDisplay::Address);
  const Address destination{context.dataBase() + relative};
  return event.derived("destination", destination, SourceValueDisplay::Address,
                       destination.value < context.range.sequenceEnd ? role : SemanticOperandRole::Address);
}

// One source opcode is read and compiled in one block. Event operations append
// shared actions or typed Playback behavior in written order; there is no
// second opcode profile or execution switch.
[[nodiscard]] DecodedBytecodeCommand decodeCommand(const SequenceDecodeContext& context, u32 begin) {
  NdsCompilerCursor cursor(context.reader(), begin, context.range.sequenceEnd, "nds", context.diagnostics);
  if (!cursor.hasOpcode()) {
    return cursor.truncated();
  }

  if (cursor.opcode() <= 0x7f) {
    auto event = cursor.command("Note", SequenceSemantic::Note);
    const u8 key =
        event.opcodeValue("key", cursor.opcode(), SourceValueDisplay::MidiNote, SemanticOperandRole::NoteKey);
    const u8 velocity = event.u8("velocity", SemanticOperandRole::Level);
    const u32 duration = event.varLen("duration", SemanticOperandRole::Duration);
    return event.invoke<&Playback::note>(key, velocity, duration);
  }

  switch (cursor.opcode()) {
    case 0x80: {
      auto event = cursor.command("Rest", SequenceSemantic::Rest);
      return event.wait(event.varLen("duration", SemanticOperandRole::Duration));
    }
    case 0x81: {
      auto event = cursor.command("Program", SequenceSemantic::Program);
      const u32 raw = event.varLen("raw");
      const u32 bank = event.derived("bank", raw >> 7, SemanticOperandRole::InstrumentBank);
      const u32 program = event.derived("program", raw & 0x7f, SemanticOperandRole::InstrumentProgram);
      return event.emitInstrument(bank, program);
    }
    case 0x93: {
      auto event = cursor.sourceOnly("Open Track");
      event.u8("track");
      static_cast<void>(targetAddress(context, event, SemanticOperandRole::Address));
      return event.ignore();
    }
    case 0x94:
    case 0x95: {
      const bool isCall = cursor.opcode() == 0x95;
      auto event = cursor.command(isCall ? "Call" : "Jump", isCall ? SequenceSemantic::Call : SequenceSemantic::Jump);
      const Address destination =
          targetAddress(context, event, isCall ? SemanticOperandRole::CallTarget : SemanticOperandRole::JumpTarget);
      if (!event.ok()) {
        return event.stop();
      }
      if (destination.value >= context.range.sequenceEnd) {
        event.warning(isCall ? "Call target outside sequence data" : "Jump target outside sequence data");
        return event.stop();
      }
      return isCall ? event.call(destination) : event.jump(destination);
    }
    case 0x96: {
      auto event = cursor.unsupported("Unsupported Command");
      event.warning("Unsupported NDS SSEQ command stopped playback");
      return event.stop();
    }
    case 0xa0:
      return cursor.ignored("Cmd with Random Value", 5, "random-value");
    case 0xa1:
      return cursor.ignored("Cmd with Variable", 2, "variable-command");
    case 0xa2:
      return cursor.ignored("If", 0);
    case 0xb0:
      return cursor.ignored("Set Variable", 3);
    case 0xb1:
      return cursor.ignored("Add Variable", 3);
    case 0xb2:
      return cursor.ignored("Sub Variable", 3);
    case 0xb3:
      return cursor.ignored("Mul Variable", 3);
    case 0xb4:
      return cursor.ignored("Div Variable", 3);
    case 0xb5:
      return cursor.ignored("Shift Variable", 3);
    case 0xb6:
      return cursor.ignored("Rand Variable", 3);
    case 0xb8:
      return cursor.ignored("If Variable ==", 3, "if-variable-equal");
    case 0xb9:
      return cursor.ignored("If Variable >=", 3, "if-variable-greater-equal");
    case 0xba:
      return cursor.ignored("If Variable >", 3, "if-variable-greater");
    case 0xbb:
      return cursor.ignored("If Variable <=", 3, "if-variable-less-equal");
    case 0xbc:
      return cursor.ignored("If Variable <", 3, "if-variable-less");
    case 0xbd:
      return cursor.ignored("If Variable !=", 3, "if-variable-not-equal");
    case 0xc0: {
      auto event = cursor.command("Pan", SequenceSemantic::Pan);
      const double position = std::clamp((event.u8("pan") / 63.5) - 1.0, -1.0, 1.0);
      return event.emitPan(position);
    }
    case 0xc1: {
      auto event = cursor.command("Volume", SequenceSemantic::Level);
      return event.emitLevel(LevelScale::linearFromMidi7(event.u8("volume")));
    }
    case 0xc2:
      return cursor.ignored("Master Volume", 1);
    case 0xc3: {
      auto event = cursor.command("Transpose", SequenceSemantic::State);
      return event.set<&TrackState::transpose>(event.s8("semitones"));
    }
    case 0xc4: {
      auto event = cursor.command("Pitch Bend", SequenceSemantic::Pitch);
      return event.invoke<&Playback::pitchBend>(event.s8("bend"));
    }
    case 0xc5: {
      auto event = cursor.command("Pitch Bend Range", SequenceSemantic::Pitch);
      const u8 semitones = event.u8("semitones");
      return event.set<&TrackState::pitchBendRangeSemitones>(semitones).emitPitchBendRange(semitones);
    }
    case 0xc6:
      return cursor.ignored("Priority", 1);
    case 0xc7: {
      auto event = cursor.command("Note Wait", SequenceSemantic::State);
      return event.set<&TrackState::noteWait>(event.u8("enabled") != 0);
    }
    case 0xc8: {
      auto event = cursor.command("Tie", SequenceSemantic::State);
      return event.invoke<&Playback::tie>(event.u8("enabled") != 0);
    }
    case 0xc9: {
      auto event = cursor.command("Portamento Control", SequenceSemantic::Portamento);
      return event.invoke<&Playback::portamentoControl>(
          event.u8("key", SourceValueDisplay::MidiNote, SemanticOperandRole::NoteKey));
    }
    case 0xca: {
      auto event = cursor.command("Modulation Depth", SequenceSemantic::Modulation);
      return event.invoke<&Playback::modulationDepth>(event.u8("depth"));
    }
    case 0xcb: {
      auto event = cursor.command("Modulation Speed", SequenceSemantic::Modulation);
      return event.invoke<&Playback::modulationSpeed>(event.u8("speed"));
    }
    case 0xcc: {
      auto event = cursor.command("Modulation Type", SequenceSemantic::Modulation);
      return event.invoke<&Playback::modulationTarget>(event.u8("type"));
    }
    case 0xcd: {
      auto event = cursor.command("Modulation Range", SequenceSemantic::Modulation);
      return event.invoke<&Playback::modulationRange>(event.u8("range"));
    }
    case 0xce: {
      auto event = cursor.command("Portamento", SequenceSemantic::Portamento);
      return event.set<&TrackState::portamento>(event.u8("enabled") != 0);
    }
    case 0xcf: {
      auto event = cursor.command("Portamento Time", SequenceSemantic::Portamento);
      return event.set<&TrackState::portamentoTime>(event.u8("time"));
    }
    case 0xd0:
      return cursor.ignored("Attack Rate", 1);
    case 0xd1:
      return cursor.ignored("Decay Rate", 1);
    case 0xd2:
      return cursor.ignored("Sustain Level", 1);
    case 0xd3:
      return cursor.ignored("Release Rate", 1);
    case 0xd4:
      return cursor.ignored("Loop Start", 1);
    case 0xd5: {
      auto event = cursor.command("Expression", SequenceSemantic::Level);
      return event.emitExpression(LevelScale::linearFromMidi7(event.u8("expression")));
    }
    case 0xd6:
      return cursor.ignored("Print Variable", 1);
    case 0xe0: {
      auto event = cursor.command("Modulation Delay", SequenceSemantic::Modulation);
      return event.invoke<&Playback::modulationDelay>(event.u16le("delay"));
    }
    case 0xe1: {
      auto event = cursor.command("Tempo", SequenceSemantic::Tempo);
      const u16 bpm = event.u16le("tempo", SourceValueDisplay::BeatsPerMinute);
      return bpm == 0 ? event.ignore() : event.invoke<&Playback::tempo>(bpm);
    }
    case 0xe3: {
      auto event = cursor.command("Sweep Pitch", SequenceSemantic::Pitch);
      return event.set<&TrackState::sweepPitch>(
          event.s16le("pitch", SourceValueDisplay::SignedDecimal, SemanticOperandRole::Pitch));
    }
    case 0xfc:
      return cursor.ignored("Loop End", 0);
    case 0xfd:
      return cursor.command("Return", SequenceSemantic::Return).return_();
    case 0xfe: {
      auto event = cursor.sourceOnly("Allocate Track");
      event.u16le("track_mask");
      return event.ignore();
    }
    case 0xff:
      return cursor.command("End", SequenceSemantic::End).end();
    default: {
      auto event = cursor.unsupported("Unknown Opcode", "unknown");
      event.warning("Unknown NDS SSEQ opcode stopped playback");
      return event.stop();
    }
  }
}

// Creates the stop command used when malformed data overlaps the first byte of
// a real subroutine.
[[nodiscard]] DecodedBytecodeCommand terminalRecoveryCommand(const SequenceDecodeContext& context, u32 offset) {
  return DecodedBytecodeCommand{
      .range = context.reader().range(offset, 1),
      .opcode = context.reader().u8At(offset),
      .encodedSize = 1,
      .flow = CommandFlow::end(Address{offset + 1}),
      .presentation =
          DecodedCommandPresentation{
              .label = "Recovery Stop",
              .localKind = "recovery-stop",
              .detailKind = "nds.recovery-stop",
              .semantic = SequenceSemantic::Unsupported,
              .playback = CommandPlaybackStatus::StopsPlayback,
          },
  };
}

// Malformed SDAT FAT ranges can overlap a real call target by one byte. This
// exceptional walker keeps the normal compiler cursor, adding only
// the overlap stop needed to avoid swallowing the subroutine's first byte.
[[nodiscard]] TrackProgram decodeMalformedSdatRangeTrack(const SequenceDecodeContext& context, u32 trackIndex,
                                                         u32 startOffset) {
  auto track = context.tracks.begin(trackIndex, startOffset);
  std::set<u32> decodedOffsets;
  std::set<u32> callTargetOffsets;
  std::vector<PendingBlock> pendingBlocks{{.offset = startOffset}};
  u32 decodedCommands = 0;

  while (!pendingBlocks.empty()) {
    const PendingBlock block = pendingBlocks.back();
    pendingBlocks.pop_back();
    u32 offset = block.offset;

    while (hasBytecodeBytes(context.reader(), offset, 1, context.tracks.bytecodeEnd) &&
           decodedCommands++ < context.tracks.maxCommands) {
      const u32 begin = offset;
      if (!decodedOffsets.insert(begin).second) {
        break;
      }

      auto decoded = decodeCommand(context, begin);
      if (!block.callTarget) {
        const auto overlap = std::ranges::find_if(
            callTargetOffsets, [&](u32 target) { return begin < target && target < decoded.range.endOffset(); });
        if (overlap != callTargetOffsets.end()) {
          track.append(terminalRecoveryCommand(context, begin), begin);
          break;
        }
      }

      if (decoded.flow.unconditionalJump()) {
        const u32 destination = decoded.flow.defaultDestination()->value;
        track.append(std::move(decoded), begin);
        if (decodedOffsets.contains(destination)) {
          break;
        }
        offset = destination;
        continue;
      }

      if (decoded.flow.callTarget()) {
        const u32 destination = decoded.flow.defaultDestination()->value;
        if (!decodedOffsets.contains(destination) && callTargetOffsets.insert(destination).second) {
          pendingBlocks.push_back(PendingBlock{.offset = destination, .callTarget = true});
        }
      }

      const auto next = decoded.flow.discoveryContinuation();
      track.append(std::move(decoded), begin);
      if (!next) {
        break;
      }
      offset = next->value;
    }
  }

  return track.finish();
}

// Chooses the normal track reader or the special recovery path used by a few
// malformed SDAT files.
[[nodiscard]] TrackProgram decodeTrack(const SequenceDecodeContext& context, u32 trackIndex, u32 startOffset) {
  if (context.range.recoverMalformedSdatRange) {
    return decodeMalformedSdatRangeTrack(context, trackIndex, startOffset);
  }
  return context.tracks.reachable(trackIndex, startOffset, [&](u32 offset) { return decodeCommand(context, offset); });
}

// Reads the opening track setup and returns the start of the main track followed
// by any additional tracks it opens.
[[nodiscard]] std::vector<u32> readTrackStarts(const SequenceDecodeContext& context) {
  std::vector<u32> secondaryTracks;
  u32 offset = context.dataBase();
  if (!hasBytecodeBytes(context.reader(), offset, 1, context.range.sequenceEnd)) {
    return {offset};
  }

  if (context.reader().u8At(offset) != 0xfe ||
      !hasBytecodeBytes(context.reader(), offset, 3, context.range.sequenceEnd)) {
    return {offset};
  }
  offset += 3;
  if (!hasBytecodeBytes(context.reader(), offset, 1, context.range.sequenceEnd)) {
    return {offset};
  }

  if (context.reader().u8At(offset) == 0x80) {
    RecordReader delay{context.reader(), offset, context.range.sequenceEnd};
    static_cast<void>(delay.u8("opcode"));
    static_cast<void>(delay.varLen("duration"));
    if (!delay.ok()) {
      return {offset};
    }
    offset = delay.position();
  }

  while (hasBytecodeBytes(context.reader(), offset, 5, context.range.sequenceEnd) &&
         context.reader().u8At(offset) == 0x93) {
    const u32 relative = context.reader().u8At(offset + 2) | (context.reader().u8At(offset + 3) << 8) |
                         (context.reader().u8At(offset + 4) << 16);
    const u32 destination = context.dataBase() + relative;
    if (destination < context.range.sequenceEnd) {
      secondaryTracks.push_back(destination);
    }
    offset += 5;
  }

  std::vector<u32> starts{offset};
  starts.insert(starts.end(), secondaryTracks.begin(), secondaryTracks.end());
  return starts;
}

// Decodes every track found in the sequence's opening setup.
[[nodiscard]] std::vector<TrackProgram> decodeTracks(const SequenceDecodeContext& context) {
  const std::vector<u32> starts = readTrackStarts(context);
  std::vector<TrackProgram> tracks;
  tracks.reserve(starts.size());
  for (u32 trackIndex = 0; trackIndex < starts.size(); ++trackIndex) {
    tracks.push_back(decodeTrack(context, trackIndex, starts[trackIndex]));
  }
  return tracks;
}

}  // namespace

const SequenceDialect& ndsSequenceDialect() {
  static const SequenceDialect dialect = makeCompiledDialect<TrackState, Playback, ProgramState>(SequenceDialect{
      .id = DialectId{.value = std::string(kNdsSequenceDialectId)},
      .commandDetailKindPrefix = "nds",
      .timebase = Timebase{.ppqn = 0x30},
      .defaultBehavior =
          SequenceProgramBehavior{
              .defaultLoopPolicy = LoopPolicy::PlayOnce,
              .commandLimit = kMaxTrackCommands,
              .panLaw = PanLaw::EqualPower,
          },
  });
  return dialect;
}

// Creates the sequence program, describes its header, and decodes all tracks
// within the selected file range.
SequenceProgram parseNdsSequenceProgram(ByteReader reader, AssetId id, NdsSequenceRange range,
                                        SourceMapBuilder* sourceMap, std::vector<Diagnostic>* diagnostics) {
  const SequenceDialect& dialect = ndsSequenceDialect();
  const u32 sequenceOffset = range.offset;
  SequenceProgram program = dialect.makeProgram(Address{sequenceOffset + kSseqHeaderSize});

  if (sourceMap != nullptr && reader.has(sequenceOffset, kSseqHeaderSize)) {
    sourceMap->header("SSEQ Header", reader.range(sequenceOffset, kSseqHeaderSize))
        .kind("sseq-header")
        .owner(ObjectRefs::sequence(id))
        .field("data_offset", reader.range(sequenceOffset + kSseqDataOffsetField, 4),
               sequenceOffset + reader.le32(sequenceOffset + kSseqDataOffsetField), SourceValueDisplay::Address);
  }

  const SequenceDecodeContext context{
      .tracks =
          TrackDecodeScope{
              .reader = reader,
              .bytecodeEnd = range.sequenceEnd,
              .maxCommands = kMaxTrackCommands,
              .sequenceAsset = id,
              .sourceMap = sourceMap,
          },
      .range = range,
      .diagnostics = diagnostics,
  };
  program.tracks = decodeTracks(context);

  return program;
}

}  // namespace vgmtrans::formats::nds
