/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/NamcoSnes/NamcoSnes.h"

#include "value/sequence/CommandSourceMap.h"
#include "value/sequence/CompiledCommandRuntime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace vgmtrans::formats::namco_snes {

using namespace core;

namespace {

constexpr std::string_view kFormatId = "namco-snes";
constexpr PitchBendLayerId kPitchTableBendLayer{1};
namespace math {

[[nodiscard]] constexpr u8 voiceBit(u32 track) { return static_cast<u8>(0x80u >> track); }

[[nodiscard]] u8 dspVoiceMask(u8 sequenceMask) {
  u8 result = 0;
  for (u8 voice = 0; voice < 8; ++voice) {
    if ((sequenceMask & voiceBit(voice)) != 0) {
      result |= static_cast<u8>(1u << voice);
    }
  }
  return result;
}

[[nodiscard]] constexpr u32 waitTicks(u8 delta, u8 multiplier) {
  return static_cast<u32>(delta) * multiplier;
}

[[nodiscard]] double tuningCents(u8 fraction) { return fraction * (100.0 / 256.0); }

[[nodiscard]] constexpr double signedDspGain(s8 value) { return value / 128.0; }

[[nodiscard]] double modulationScale(Version version, u8 depth) {
  if (version != Version::BlueCrystalRod && depth == 0) {
    return 1.0;
  }
  return depth / 256.0;
}

[[nodiscard]] u16 nextPortamentoPitch(u16 current, u16 target, u8 speed) {
  // The driver subtracts the 8.8 pitches, retains the signed coarse byte,
  // then moves (|coarse distance| + 1) * speed / 2 fractional units.
  const bool borrow = static_cast<u8>(target) < static_cast<u8>(current);
  const int coarseDistance =
      static_cast<s8>(static_cast<u8>((target >> 8) - (current >> 8) - (borrow ? 1 : 0)));
  const u16 step = static_cast<u16>((std::abs(coarseDistance) + 1) * speed) >> 1;
  if (coarseDistance < 0) {
    const u16 next = static_cast<u16>(current - step);
    return next < target ? target : next;
  }
  const u16 next = static_cast<u16>(current + step);
  return next >= target ? target : next;
}

[[nodiscard]] std::vector<u16> portamentoCurve(u16 current, u16 target, u8 speed) {
  // Performance transitions need their duration up front, so predict it with
  // the same one-tick operation used by live playback.
  std::vector<u16> curve{current};
  for (u32 tick = 0; current != target && tick < std::numeric_limits<u16>::max(); ++tick) {
    const u16 next = nextPortamentoPitch(current, target, speed);
    // Speed $01 can genuinely stall within the final semitone.
    if (next == current) {
      break;
    }
    curve.push_back(next);
    current = next;
  }
  return curve;
}

}  // namespace math

struct MaskedValues {
  u8 mask = 0;
  std::array<u8, kTrackCount> values{};
};

enum Parameter : u8 {
  kSrcn,
  kVolume,
  kBalance,
  kGate,
  kPitchTable,
  kTranspose,
  kFineTuning,
  kPortamento,
  kPitchRate,
  kPitchDepth,
  kEnvelope,
  kParameterCount,
};

struct ParameterCommand {
  std::string_view name;
  SequenceSemantic semantic;
  SemanticOperandRole role;
};

using ControlValues = std::array<u8, kParameterCount>;
constexpr ControlValues kDefaultControls{0, 0x88, 0x88, 0, 0, 0, 0, 0, 0, 0, 0};

constexpr std::array<ParameterCommand, kParameterCount> kParameterCommands{{
    {"Instrument", SequenceSemantic::Program, SemanticOperandRole::Instrument},
    {"Voice Volume", SequenceSemantic::Level, SemanticOperandRole::Level},
    {"Stereo Balance", SequenceSemantic::Pan, SemanticOperandRole::Pan},
    {"Gate Time", SequenceSemantic::State, SemanticOperandRole::Duration},
    {"Pitch Modulation Table", SequenceSemantic::Modulation, SemanticOperandRole::Modulation},
    {"Transpose", SequenceSemantic::Pitch, SemanticOperandRole::Pitch},
    {"Fine Tuning", SequenceSemantic::Pitch, SemanticOperandRole::Pitch},
    {"Portamento Speed", SequenceSemantic::Portamento, SemanticOperandRole::Duration},
    {"Pitch Table Rate", SequenceSemantic::Modulation, SemanticOperandRole::Modulation},
    {"Pitch Table Depth", SequenceSemantic::Modulation, SemanticOperandRole::Modulation},
    {"Envelope Preset", SequenceSemantic::Envelope, SemanticOperandRole::Value},
}};

struct DriverData {
  RetainedSource source;
  Layout layout;
};

struct ProgramState {
  explicit ProgramState(const DriverData& data) : data(data) {}

  DriverData data;
  ReverbPerformanceEvent echo{.voiceMask = 0};
  bool echoEnabled = false;
};

enum class VoiceSource : u8 {
  Melodic,
  Percussion,
  Noise,
};

struct VoiceInstrument {
  VoiceSource source = VoiceSource::Melodic;
  u8 srcn = 0;

  friend bool operator==(const VoiceInstrument&, const VoiceInstrument&) noexcept = default;
};

struct PercussionTrigger {
  u8 key;
  u8 srcn;
  u8 envelope;
  u8 volume;
  u8 balance;
};

struct TrackState {
  explicit TrackState(const TrackProgram& source) : number(source.sourceTrackNumber) {}

  void resetPhysicalVoice() {
    triggerDelay = 0;
    pendingNote.reset();
    triggerTicks = 0;
    gateTicks = 0;
    driverPitch.reset();
    pitchTable = 0;
    pitchPosition = 0;
    pitchPhase = 0;
    pitchBend.reset();
    portamentoTarget.reset();
  }

  void activate() {
    commandControls[kVolume] = 0x88;
    commandControls[kBalance] = 0x88;
    for (const Parameter parameter :
         {kGate, kPitchTable, kTranspose, kFineTuning, kPortamento, kEnvelope}) {
      commandControls[parameter] = 0;
    }
    resetPhysicalVoice();
  }

  [[nodiscard]] bool selected(u8 mask) const { return (mask & math::voiceBit(number)) != 0; }
  [[nodiscard]] bool active() const { return selected(activeMask); }

  u32 number;
  u8 delta = 1;
  u8 multiplier = 1;
  u8 activeMask = 0;
  // The driver copies its command bank ($0360) to the live voice bank ($0200)
  // on every trigger. Commands alone do not alter a sounding voice.
  ControlValues commandControls = kDefaultControls;
  ControlValues liveControls = kDefaultControls;
  u8 triggerDelay = 0;
  bool slur = false;

  std::optional<u8> pendingNote;
  u16 triggerTicks = 0;
  u16 gateTicks = 0;
  // Driver pitch is an 8.8 note value. It survives exported note-off events so
  // release tails and later attacks resume from the pitch the SPC voice retained.
  PerformanceNoteId activeNote;
  std::optional<u16> driverPitch;
  // A target exists only while the per-tick driver slide is still active.
  std::optional<u16> portamentoTarget;

  u16 pitchTable = 0;
  u8 pitchPosition = 0;
  u8 pitchPhase = 0;

  VoiceInstrument emittedInstrument;
  std::optional<std::pair<u8, u8>> emittedMix;
  std::optional<double> pitchBend;
};

struct Playback {
  TrackState& track;
  PerformanceEmitter& out;
  VmApi& vm;
  ProgramState& program;

  [[nodiscard]] ByteReader reader() const { return program.data.source.reader(); }
  [[nodiscard]] const Layout& layout() const { return program.data.layout; }

  void endNote(u64 tick) {
    if (track.activeNote.valid()) {
      static_cast<void>(out.setNoteEnd(track.activeNote, tick));
    }
    track.activeNote = {};
  }

  void selectInstrument(VoiceInstrument instrument) {
    if (track.emittedInstrument == instrument) {
      return;
    }
    track.emittedInstrument = instrument;
    switch (instrument.source) {
      case VoiceSource::Melodic:
        out.instrument(InstrumentIdentity{.domain = std::string(kInstrumentDomain), .key = instrument.srcn},
                       InstrumentEnvelopeMode::PreserveDynamicOverride);
        break;
      case VoiceSource::Percussion:
        out.instrument(127, 0, InstrumentEnvelopeMode::PreserveDynamicOverride);
        break;
      case VoiceSource::Noise:
        out.instrument(InstrumentIdentity{.domain = std::string(kInstrumentDomain), .key = kNoiseInstrumentKey},
                       InstrumentEnvelopeMode::PreserveDynamicOverride);
        break;
    }
  }

  void delta(u8 value) { track.delta = value; }
  void multiplier(u8 value) { track.multiplier = value; }

  void activeVoices(u8 mask) {
    const bool wasActive = track.active();
    const bool isActive = track.selected(mask);
    if (wasActive && !isActive) {
      endNote(vm.tick());
      track.resetPhysicalVoice();
    } else if (!wasActive && isActive) {
      track.activate();
    }
    track.activeMask = mask;
    if (track.number == 0 && program.echoEnabled) {
      program.echo.voiceMask = math::dspVoiceMask(mask);
      out.reverb(program.echo);
    }
  }

  void control(u8 index, MaskedValues values) {
    if (index < track.commandControls.size() && track.selected(values.mask)) {
      track.commandControls[index] = values.values[track.number];
    }
  }

  void noteDelay(MaskedValues values) {
    if (track.selected(values.mask)) {
      track.triggerDelay = values.values[track.number];
    }
  }

  void slur(u8 mask) { track.slur = track.selected(mask); }

  void emitMix() {
    const u8 volume = track.liveControls[kVolume];
    const u8 balance = track.liveControls[kBalance];
    const std::pair mix{volume, balance};
    if (track.emittedMix == mix) {
      return;
    }
    track.emittedMix = mix;
    out.level(volume / 256.0, ValueQuantization{.levels = 256});
    const double left = (balance >> 4) / 16.0;
    const double right = (balance & 0x0f) / 16.0;
    if (layout().mono) {
      const double mono = (left + right) / 2.0;
      out.stereoBalance(mono, mono);
    } else {
      out.stereoBalance(left, right);
    }
  }

  void emitPitchBend(double semitones) {
    if (!track.pitchBend || std::abs(*track.pitchBend - semitones) > 0.0001) {
      out.pitchBend(semitones, kPitchTableBendLayer);
      track.pitchBend = semitones;
    }
  }

  [[nodiscard]] u8 resolvePitchPosition(u16 table, u8 position, u8 fallback) const {
    // F0 holds the preceding point. Every other F1-FF marker jumps to the
    // following byte offset, which permits both periodic vibrato and one-shot curves.
    for (u32 hops = 0; hops < 16; ++hops) {
      if (!reader().has(table + position, 1)) {
        return fallback;
      }
      const u8 value = reader().u8At(table + position);
      if (value < 0xf0) {
        return position;
      }
      if (value == 0xf0 || !reader().has(table + position + 1u, 1)) {
        return fallback;
      }
      position = reader().u8At(table + position + 1u);
    }
    return fallback;
  }

  [[nodiscard]] double modulationValue() const {
    if (track.pitchTable == 0 || !reader().has(track.pitchTable + track.pitchPosition, 1)) {
      return 0.0;
    }
    const u8 nextPosition = resolvePitchPosition(track.pitchTable, static_cast<u8>(track.pitchPosition + 1),
                                                 track.pitchPosition);
    const double current = static_cast<int>(reader().u8At(track.pitchTable + track.pitchPosition)) - 0x64;
    const double next = static_cast<int>(reader().u8At(track.pitchTable + nextPosition)) - 0x64;
    const double interpolated = current + (next - current) * (track.pitchPhase / 256.0);
    return interpolated * math::modulationScale(layout().version, track.liveControls[kPitchDepth]);
  }

  [[nodiscard]] bool selectPitchTable(bool restart) {
    const u8 index = track.liveControls[kPitchTable];
    if (index == 0) {
      track.pitchTable = 0;
      return false;
    }
    const u16 pointers = layout().pitchPointerTable(reader());
    // The driver doubles the index with ASL A, discarding its high bit.
    const u32 entry = pointers + static_cast<u8>(index << 1);
    if (!reader().has(entry, 2) || !reader().has(reader().le16(entry), 1)) {
      track.pitchTable = 0;
      return false;
    }
    track.pitchTable = reader().le16(entry);
    if (restart) {
      track.pitchPosition = resolvePitchPosition(track.pitchTable, 0, 0);
      track.pitchPhase = 0;
    } else if (!reader().has(track.pitchTable + track.pitchPosition, 1)) {
      track.pitchPosition = resolvePitchPosition(track.pitchTable, 0, 0);
    }
    return true;
  }

  void beginPitchModulation() {
    emitPitchBend(selectPitchTable(true) ? modulationValue() : 0.0);
  }

  void advancePitchModulation() {
    if (!selectPitchTable(false)) {
      emitPitchBend(0.0);
      return;
    }
    const u16 phase = static_cast<u16>(track.pitchPhase) + track.liveControls[kPitchRate];
    if (phase > 0xff) {
      track.pitchPhase = track.liveControls[kPitchRate];
      track.pitchPosition = resolvePitchPosition(track.pitchTable, static_cast<u8>(track.pitchPosition + 1),
                                                 track.pitchPosition);
    } else {
      track.pitchPhase = static_cast<u8>(phase);
    }
    emitPitchBend(modulationValue());
  }

  void beginAttack(VoiceInstrument instrument) {
    endNote(vm.tick());
    selectInstrument(instrument);
    out.replaceEnvelope(driverEnvelope(reader(), layout(), track.liveControls[kEnvelope]),
                        VoiceEnvelopeScope::FutureAttacks);
  }

  [[nodiscard]] u16 targetPitch(u8 sourceNote) const {
    const u8 coarse = static_cast<u8>(sourceNote + static_cast<s8>(track.liveControls[kTranspose]));
    // $0120+x is the coarse note and $0100+x is its fractional byte.
    return static_cast<u16>((coarse << 8) | track.liveControls[kFineTuning]);
  }

  void advancePortamento() {
    if (!track.driverPitch || !track.portamentoTarget || track.liveControls[kPortamento] == 0) {
      return;
    }
    *track.driverPitch = math::nextPortamentoPitch(*track.driverPitch, *track.portamentoTarget,
                                                   track.liveControls[kPortamento]);
    if (*track.driverPitch == *track.portamentoTarget) {
      track.portamentoTarget.reset();
    }
  }

  void emitPortamentoCurve(u16 target, double outputKey, PerformanceNoteId previous, bool continues) {
    const std::vector curve =
        math::portamentoCurve(*track.driverPitch, target, track.liveControls[kPortamento]);
    const auto key = [&](u16 pitch) {
      return outputKey + (static_cast<s32>(pitch) - static_cast<s32>(target)) / 256.0;
    };
    auto slide = out.pitchSlide(track.activeNote, key(curve.front()), key(curve.back()),
                                static_cast<u32>(curve.size() - 1));
    if (continues) {
      slide.continueFrom(previous);
    }
    for (u32 tick = 0; tick < static_cast<u32>(curve.size()); ++tick) {
      slide.sample(out.at(vm.tick() + tick), key(curve[tick]));
    }
    // Native MIDI portamento can match total time but not this stepped curve.
    // An explicit export override remains available when compatibility matters more.
    slide.preferPitchBend();
  }

  void beginPortamento(u16 target, double outputKey, PerformanceNoteId previous, bool continues) {
    if (track.liveControls[kPortamento] == 0 || !track.driverPitch) {
      track.driverPitch = target;
      track.portamentoTarget.reset();
      return;
    }

    track.portamentoTarget = target;
    // Note dispatch advances once before writing pitch, so the pre-step value
    // is not part of the new note's audible curve.
    advancePortamento();
    if (track.activeNote.valid() && *track.driverPitch != target) {
      emitPortamentoCurve(target, outputKey, previous, continues);
    }
  }

  void latchPercussion(const PercussionTrigger& percussion) {
    track.liveControls[kSrcn] = percussion.srcn;
    track.liveControls[kEnvelope] = percussion.envelope;
    track.liveControls[kVolume] = percussion.volume;
    track.liveControls[kBalance] = percussion.balance;
  }

  void rest() {
    emitMix();
    if (track.driverPitch) {
      advancePitchModulation();
    }
    endNote(vm.tick());
  }

  void startNote(u8 sourceNote, std::optional<PercussionTrigger> percussion = {}) {
    const u8 srcn = track.liveControls[kSrcn];
    const s8 transpose = static_cast<s8>(track.liveControls[kTranspose]);
    const u8 coarse = static_cast<u8>(sourceNote + transpose);
    const double fine = math::tuningCents(track.liveControls[kFineTuning]);
    const double outputKey = percussion ? percussion->key : std::min<u8>(coarse, 127);
    // The drum region maps its sequence key to the table's source note. Only
    // live pitch controls remain to be applied by the sequence.
    const double tuning = percussion ? transpose * 100.0 + fine : fine;
    const u16 target = targetPitch(sourceNote);

    const PerformanceNoteId previous = track.activeNote;
    const bool continues = track.slur && previous.valid();
    if (!track.slur) {
      beginAttack(percussion ? VoiceInstrument{.source = VoiceSource::Percussion}
                             : VoiceInstrument{.source = VoiceSource::Melodic, .srcn = srcn});
      beginPitchModulation();
    }
    emitMix();
    if (track.slur && !previous.valid()) {
      beginPortamento(target, outputKey, previous, false);
      advancePitchModulation();
      return;
    }
    out.tuning(tuning);

    NotePerformanceEvent event{
        .key = outputKey,
        .linearVelocity = 1.0,
        .durationTicks = std::numeric_limits<u32>::max(),
        .restartsEnvelope = !continues,
        .restartsLfoPhase = !continues,
    };
    track.activeNote = continues ? out.continueVoice(previous, std::move(event)) : out.note(std::move(event));
    beginPortamento(target, outputKey, previous, continues);
    if (continues) {
      advancePitchModulation();
    }
  }

  void startNoise(u8 raw, std::optional<PercussionTrigger> percussion = {}) {
    const PerformanceNoteId previous = track.activeNote;
    const bool continues = track.slur && previous.valid();
    if (!track.slur) {
      beginAttack(percussion ? VoiceInstrument{.source = VoiceSource::Percussion}
                             : VoiceInstrument{.source = VoiceSource::Noise});
    }
    emitMix();
    if (track.slur && !previous.valid()) {
      track.driverPitch = static_cast<u16>((raw & 0x1f) << 8);
      track.portamentoTarget.reset();
      return;
    }
    out.tuning(0.0);
    const double key = percussion ? percussion->key : std::min<int>(35 + (raw & 0x1f), 127);
    NotePerformanceEvent event{.key = key,
                               .linearVelocity = 1.0,
                               .durationTicks = std::numeric_limits<u32>::max(),
                               .restartsEnvelope = !continues,
                               .restartsLfoPhase = !continues};
    track.activeNote = continues ? out.continueVoice(previous, std::move(event)) : out.note(std::move(event));
    track.driverPitch = static_cast<u16>((raw & 0x1f) << 8);
    track.portamentoTarget.reset();
  }

  void trigger(u8 raw) {
    track.pendingNote.reset();
    track.triggerTicks = 0;
    if (!track.active()) {
      return;
    }
    track.liveControls = track.commandControls;
    track.gateTicks = 0;
    if (raw == kRest) {
      rest();
      return;
    }
    if (raw < kRest) {
      startNote(raw);
      return;
    }
    if (raw < 0x80) {
      startNoise(raw);
      return;
    }

    const u8 index = raw & 0x7f;
    const u32 row = layout().percussionTable(reader()) + index * 5u;
    if (!reader().has(row, 5)) {
      return;
    }
    const PercussionTrigger percussion{.key = index,
                                       .srcn = reader().u8At(row),
                                       .envelope = reader().u8At(row + 1),
                                       .volume = reader().u8At(row + 2),
                                       .balance = reader().u8At(row + 3)};
    const u8 note = reader().u8At(row + 4);
    latchPercussion(percussion);
    if (note < kRest) {
      startNote(note, percussion);
    } else if (note == kRest) {
      rest();
    } else if (note < 0x80) {
      startNoise(note, percussion);
    }
  }

  [[nodiscard]] Effects note(MaskedValues notes) {
    if (track.selected(notes.mask)) {
      track.pendingNote = notes.values[track.number];
      track.triggerTicks = 0;
      if (track.triggerDelay == 0) {
        trigger(*track.pendingNote);
      }
    }
    return Effects::wait(math::waitTicks(track.delta, track.multiplier));
  }

  [[nodiscard]] Effects wait() const { return Effects::wait(math::waitTicks(track.delta, track.multiplier)); }

  void masterVolume(u8 value) {
    if (track.number == 0) {
      out.masterLevel(value / 256.0);
    }
  }

  void echoDelay(u8 value) {
    if (track.number == 0) {
      program.echo.delayMilliseconds = (value & 0x0f) * 16.0;
      out.reverb(program.echo);
    }
  }

  void echoEnabled(u8 value) {
    if (track.number == 0) {
      program.echoEnabled = value != 0;
      program.echo.voiceMask = program.echoEnabled ? math::dspVoiceMask(track.activeMask) : 0;
      out.reverb(program.echo);
    }
  }

  void echoFeedback(s8 value) {
    if (track.number == 0) {
      program.echo.feedback = math::signedDspGain(value);
      out.reverb(program.echo);
    }
  }

  void echoFilter(u8 value) {
    if (track.number == 0) {
      program.echo.filterIndex = value;
      out.reverb(program.echo);
    }
  }

  void echoVolume(s8 value) {
    if (track.number == 0) {
      const double gain = math::signedDspGain(value);
      program.echo.leftGain = gain;
      program.echo.rightGain = gain;
      program.echo.send = std::abs(gain);
      out.reverb(program.echo);
    }
  }

  [[nodiscard]] Effects repeatUntil(u8 slot, u8 count, Address destination) {
    // The SPC700 counter is eight-bit, so encoded zero completes on its 256th visit.
    return vm.countedRepeatUntil(slot, count == 0 ? 256 : count, destination);
  }

  [[nodiscard]] Effects repeatBreak(u8 slot, u8 count, Address destination) {
    RepeatCounter counter = vm.repeatCounter(slot);
    if (counter.firstVisit()) {
      counter.start(count == 0 ? 256 : count);
    }
    if (counter.consumeReplay()) {
      return vm.fallthrough();
    }
    counter.finish();
    return vm.finiteBranch(destination);
  }

  [[nodiscard]] Effects returnOrEnd() {
    if (vm.inSubroutine()) {
      return vm.return_();
    }
    endNote(vm.tick());
    return vm.end();
  }

  void tick() {
    bool triggered = false;
    if (track.pendingNote && ++track.triggerTicks == track.triggerDelay) {
      trigger(*track.pendingNote);
      triggered = true;
    }

    if (!triggered && track.driverPitch) {
      advancePortamento();
      advancePitchModulation();
    }
    if (track.activeNote.valid() && track.liveControls[kGate] != 0 && ++track.gateTicks > track.liveControls[kGate]) {
      endNote(vm.tick());
    }
  }
};

using Cursor = CompilerCursor<TrackState, Playback>;

template <class Event>
[[nodiscard]] MaskedValues maskedValues(Event& event, std::string_view valueName, SemanticOperandRole role,
                                        SourceValueDisplay display = SourceValueDisplay::Default) {
  MaskedValues result;
  result.mask = event.u8("voice_mask", SourceValueDisplay::Hex);
  for (u32 voice = 0; voice < kTrackCount; ++voice) {
    if ((result.mask & math::voiceBit(voice)) == 0) {
      continue;
    }
    const std::string name = std::string(valueName) + "_" + std::to_string(voice);
    result.values[voice] = event.u8(name, display, role);
  }
  return result;
}

struct SequenceReferences {
  std::set<u8> srcns{0};
  std::set<u8> percussion;
  std::set<u8> noiseRates;
};

[[nodiscard]] DecodedBytecodeCommand decodeCommand(ByteReader reader, u32 begin,
                                                   std::vector<Diagnostic>* diagnostics,
                                                   SequenceReferences& references) {
  Cursor cursor(reader, begin, kFormatId, diagnostics);
  if (!cursor.hasOpcode()) {
    return cursor.truncated();
  }

  const u8 opcode = cursor.opcode();
  switch (opcode) {
    case 0x00: {
      auto event = cursor.command("Delta Time", SequenceSemantic::State);
      return event.invoke<&Playback::delta>(event.u8("ticks", SemanticOperandRole::Duration));
    }
    case 0x01: {
      auto event = cursor.command("Active Voices", SequenceSemantic::State);
      return event.invoke<&Playback::activeVoices>(event.u8("mask", SourceValueDisplay::Hex));
    }
    case 0x02: {
      auto event = cursor.command("Call", SequenceSemantic::Call);
      return event.call(event.addressLe("destination", SemanticOperandRole::CallTarget));
    }
    case 0x03:
      return cursor.command("Return / End", SequenceSemantic::End)
          .invokeFlow<&Playback::returnOrEnd>()
          .discoverReturn();
    case 0x04: {
      auto event = cursor.command("Timebase Multiplier", SequenceSemantic::State);
      return event.invoke<&Playback::multiplier>(event.u8("multiplier"));
    }
    case 0x05: {
      auto event = cursor.command("Master Volume", SequenceSemantic::Level);
      return event.invoke<&Playback::masterVolume>(event.u8("volume", SemanticOperandRole::Level));
    }
    case 0x06:
    case 0x0f: {
      auto event = cursor.command(opcode == 0x06 ? "Repeat Until A" : "Repeat Until B", SequenceSemantic::Repeat);
      const u8 count = event.u8("count", SemanticOperandRole::Count);
      const Address destination = event.addressLe("destination", SemanticOperandRole::RepeatTarget);
      const u8 slot = opcode == 0x06 ? 0 : 1;
      event.invokeFlow<&Playback::repeatUntil>(slot, count, destination);
      return event.mayBranchTo(destination);
    }
    case 0x07:
    case 0x10: {
      auto event =
          cursor.command(opcode == 0x07 ? "Repeat Break A" : "Repeat Break B", SequenceSemantic::RepeatBreak);
      const u8 count = event.u8("count", SemanticOperandRole::Count);
      const Address destination = event.addressLe("destination", SemanticOperandRole::RepeatTarget);
      const u8 slot = opcode == 0x07 ? 0 : 1;
      event.invokeFlow<&Playback::repeatBreak>(slot, count, destination);
      return event.mayBranchTo(destination);
    }
    case 0x08: {
      auto event = cursor.command("Jump", SequenceSemantic::Jump);
      return event.jump(event.addressLe("destination", SemanticOperandRole::JumpTarget));
    }
    case 0x09: {
      auto event = cursor.command("Notes", SequenceSemantic::Note);
      const MaskedValues notes = maskedValues(event, "note", SemanticOperandRole::NoteKey, SourceValueDisplay::Hex);
      for (u32 voice = 0; voice < kTrackCount; ++voice) {
        if ((notes.mask & math::voiceBit(voice)) == 0) {
          continue;
        }
        const u8 note = notes.values[voice];
        if (note >= 0x80) {
          references.percussion.insert(note & 0x7f);
        } else if (note > kRest) {
          references.noiseRates.insert(note & 0x1f);
        }
      }
      return event.invoke<&Playback::note>(notes);
    }
    case 0x0a: {
      auto event = cursor.command("Echo Delay", SequenceSemantic::State);
      return event.invoke<&Playback::echoDelay>(event.u8("delay"));
    }
    case 0x0b: {
      auto event = cursor.command("Note Trigger Delay", SequenceSemantic::State);
      return event.invoke<&Playback::noteDelay>(maskedValues(event, "delay", SemanticOperandRole::Duration));
    }
    case 0x0c: {
      auto event = cursor.command("Legato Voice Mask", SequenceSemantic::State);
      return event.invoke<&Playback::slur>(event.u8("mask", SourceValueDisplay::Hex));
    }
    case 0x0d: {
      auto event = cursor.command("Echo Enable", SequenceSemantic::State);
      return event.invoke<&Playback::echoEnabled>(event.u8("enabled"));
    }
    case 0x0e:
      return cursor.command("Wait", SequenceSemantic::Wait).invoke<&Playback::wait>();
    case 0x11: {
      auto event = cursor.command("Echo Feedback", SequenceSemantic::State);
      return event.invoke<&Playback::echoFeedback>(event.s8("feedback"));
    }
    case 0x12: {
      auto event = cursor.command("Echo FIR Preset", SequenceSemantic::State);
      return event.invoke<&Playback::echoFilter>(event.u8("preset"));
    }
    case 0x13: {
      auto event = cursor.command("Echo Volume", SequenceSemantic::Level);
      return event.invoke<&Playback::echoVolume>(event.s8("volume", SemanticOperandRole::Level));
    }
    case 0x14: {
      auto event = cursor.sourceOnly("Echo Start Address", "echo-start-address");
      static_cast<void>(event.u8("esa_high", SourceValueDisplay::Hex));
      return event;
    }
    default:
      break;
  }

  if (opcode >= 0x20 && opcode - 0x20 < kParameterCommands.size()) {
    const u8 index = opcode - 0x20;
    const ParameterCommand& command = kParameterCommands[index];
    auto event = cursor.command(command.name, command.semantic);
    const MaskedValues values = maskedValues(event, "value", command.role);
    if (index == kSrcn) {
      for (u32 voice = 0; voice < kTrackCount; ++voice) {
        if ((values.mask & math::voiceBit(voice)) != 0) {
          references.srcns.insert(values.values[voice]);
        }
      }
    }
    return event.invoke<&Playback::control>(index, values);
  }
  return cursor.unsupported("Invalid Command").stop();
}

}  // namespace

const SequenceProgramConfig& sequenceConfig() {
  static const SequenceProgramConfig config{
      .commandKindPrefix = std::string(kFormatId),
      .timebase = Timebase{.ppqn = kPpqn},
      .behavior = SequenceProgramBehavior{
          .commandLimit = kCommandLimit,
          .preferredPitchTransitionRendering = PitchTransitionRenderingHint::Portamento,
          .initialSourceInstrument = InstrumentIdentity{.domain = std::string(kInstrumentDomain), .key = 0},
          .initialLevel = 0x88 / 256.0,
          .initialMasterLevel = 1.0,
          .initialReverbSend = 0.0,
          .initialStereoBalance = StereoBalance{.leftGain = 0.5, .rightGain = 0.5},
          .initialMonoModeChannels = 0,
          .initialTempoMicrosecondsPerQuarter = 804000,
      },
  };
  return config;
}

SequenceParse decodeSequence(RetainedSource source, const Layout& layout, AssetId sequenceId,
                             SourceMapBuilder* sourceMap, std::vector<Diagnostic>* diagnostics) {
  const ByteReader reader = source.reader();
  const SourceRange header = reader.range(layout.sequenceReferenceAddress, layout.sequenceReferenceSize);
  SequenceReferences references;
  SequenceDecodeSession sequence{reader, sequenceConfig(), sequenceId, header, sourceMap, kCommandLimit, kAramSize};
  const u32 pointer = layout.sequenceReferenceAddress + layout.sequenceReferenceSize - 2u;
  sequence.addTrack(
      0, reader.range(pointer, 2), layout.sequenceAddress,
      [&](u32 offset) { return decodeCommand(reader, offset, diagnostics, references); },
      layout.sequenceAddress);
  SequenceProgram program =
      sequence.finish(makeCompiledRuntime<Cursor, ProgramState>(DriverData{std::move(source), layout}));
  const TrackProgram stream = program.tracks.front();
  for (u32 voice = 1; voice < kTrackCount; ++voice) {
    TrackProgram copy = stream;
    copy.sourceTrackNumber = voice;
    program.tracks.push_back(std::move(copy));
  }
  return SequenceParse{
      .program = std::move(program),
      .srcns = std::move(references.srcns),
      .percussion = std::move(references.percussion),
      .noiseRates = std::move(references.noiseRates),
      .headerRange = header,
  };
}

}  // namespace vgmtrans::formats::namco_snes
