/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/sequence/PerformanceModel.h"
#include "value/sequence/SequenceDialect.h"

namespace vgmtrans::core {

namespace detail {
class RepeatState;
struct VmApiAccess;
struct VmTrackRuntime;
}  // namespace detail

struct BranchResult {
  bool taken = false;
  Effects effects;
};

class RepeatCounter {
public:
  [[nodiscard]] bool active() const;
  [[nodiscard]] bool firstVisit() const;
  [[nodiscard]] u32 remainingPlays() const;
  void start(u32 totalPlays);
  [[nodiscard]] bool consumeReplay();
  void finish();

private:
  friend class VmApi;

  RepeatCounter(detail::RepeatState& state, u8 slot) noexcept;

  detail::RepeatState* state_ = nullptr;
  u8 slot_ = 0;
};

// Commands call PerformanceEmitter to add notes, tempo changes, controller changes, and markers.
// PerformanceEmitter fills in the current tick and source command automatically.
class PerformanceEmitter {
public:
  PerformanceEmitter(PerformanceTrack& track, CommandId sourceCommand, SourceAnnotationId sourceAnnotation, u64 tick);

  void note(NotePerformanceEvent event);
  void note(double key, double linearVelocity, u32 durationTicks, bool extendsPrevious = false);
  void tempo(TempoPerformanceEvent event);
  void tempo(u32 microsecondsPerQuarter);
  void instrument(InstrumentPerformanceEvent event);
  void instrument(u32 bank, u32 program, bool forceBankSelect = false);
  void level(LevelPerformanceEvent event);
  void level(double linearGain, LevelPrecisionHint precisionHint = LevelPrecisionHint::SevenBit);
  void expression(ExpressionPerformanceEvent event);
  void expression(double linearGain, LevelPrecisionHint precisionHint = LevelPrecisionHint::SevenBit);
  void pan(PanPerformanceEvent event);
  void pan(double stereoPosition);
  void pan(double stereoPosition, double linearGain);
  void masterLevel(MasterLevelPerformanceEvent event);
  void masterLevel(double linearGain);
  void reverb(ReverbPerformanceEvent event);
  void reverb(double send);
  void tuning(TuningPerformanceEvent event);
  void tuning(double cents);
  void globalTranspose(GlobalTransposePerformanceEvent event);
  void globalTranspose(s32 semitones);
  void pitchBend(PitchBendPerformanceEvent event);
  void pitchBend(double semitones);
  void pitchBendRange(PitchBendRangePerformanceEvent event);
  void pitchBendRange(u8 semitones);
  void portamento(PortamentoPerformanceEvent event);
  void portamento(double timeMilliseconds, double previousKey);
  void portamentoEnable(PortamentoEnablePerformanceEvent event);
  void portamentoEnable(bool enabled);
  void portamentoTime(PortamentoTimePerformanceEvent event);
  void portamentoTime(double timeMilliseconds);
  void portamentoControl(PortamentoControlPerformanceEvent event);
  void portamentoControl(double previousKey);
  void legatoPedal(LegatoPedalPerformanceEvent event);
  void legatoPedal(bool enabled);
  void modulation(ModulationPerformanceEvent event);
  void modulation(ModulationPerformanceTarget target, double amount);
  void marker(MarkerPerformanceEvent event);
  void appendEvents(std::vector<PerformanceEvent> events);

private:
  [[nodiscard]] PerformanceEventHeader header() const;

  PerformanceTrack& track_;
  CommandId sourceCommand_;
  SourceAnnotationId sourceAnnotation_;
  u64 tick_ = 0;
};

// CommandRuntime is declared with only a forward-declared PerformanceEmitter.
// Define these helpers here, after the emitter's methods are visible.
template <class TrackState, class Context>
void CommandRuntime<TrackState, Context>::note(double key, double linearVelocity, u32 durationTicks,
                                               bool extendsPrevious) {
  out.note(key, linearVelocity, durationTicks, extendsPrevious);
}

template <class TrackState, class Context>
void CommandRuntime<TrackState, Context>::tempo(u32 microsecondsPerQuarter) {
  out.tempo(microsecondsPerQuarter);
}

template <class TrackState, class Context>
void CommandRuntime<TrackState, Context>::instrument(u32 bank, u32 program, bool forceBankSelect) {
  out.instrument(bank, program, forceBankSelect);
}

template <class TrackState, class Context>
void CommandRuntime<TrackState, Context>::level(double linearGain, LevelPrecisionHint precisionHint) {
  out.level(linearGain, precisionHint);
}

template <class TrackState, class Context>
void CommandRuntime<TrackState, Context>::expression(double linearGain, LevelPrecisionHint precisionHint) {
  out.expression(linearGain, precisionHint);
}

template <class TrackState, class Context>
void CommandRuntime<TrackState, Context>::pan(double stereoPosition) {
  out.pan(stereoPosition);
}

template <class TrackState, class Context>
void CommandRuntime<TrackState, Context>::pan(double stereoPosition, double linearGain) {
  out.pan(stereoPosition, linearGain);
}

template <class TrackState, class Context>
void CommandRuntime<TrackState, Context>::masterLevel(double linearGain) {
  out.masterLevel(linearGain);
}

template <class TrackState, class Context>
void CommandRuntime<TrackState, Context>::reverb(double send) {
  out.reverb(send);
}

template <class TrackState, class Context>
void CommandRuntime<TrackState, Context>::tuning(double cents) {
  out.tuning(cents);
}

template <class TrackState, class Context>
void CommandRuntime<TrackState, Context>::globalTranspose(s32 semitones) {
  out.globalTranspose(semitones);
}

template <class TrackState, class Context>
void CommandRuntime<TrackState, Context>::pitchBend(double semitones) {
  out.pitchBend(semitones);
}

template <class TrackState, class Context>
void CommandRuntime<TrackState, Context>::pitchBendRange(u8 semitones) {
  out.pitchBendRange(semitones);
}

template <class TrackState, class Context>
void CommandRuntime<TrackState, Context>::portamentoEnable(bool enabled) {
  out.portamentoEnable(enabled);
}

template <class TrackState, class Context>
void CommandRuntime<TrackState, Context>::portamentoTime(double timeMilliseconds) {
  out.portamentoTime(timeMilliseconds);
}

template <class TrackState, class Context>
void CommandRuntime<TrackState, Context>::modulation(ModulationPerformanceTarget target, double amount) {
  out.modulation(target, amount);
}

// Commands use VmApi for playback flow that is shared across formats: next, end,
// jump, call, return, repeat handling, diagnostics, and current tick.
class VmApi {
public:
  [[nodiscard]] Step next() const noexcept;
  [[nodiscard]] Step end() const noexcept;
  [[nodiscard]] Step jump(Address destination) const noexcept;
  [[nodiscard]] Step finiteBranch(Address destination) const noexcept;
  [[nodiscard]] Step loopCandidate(Address destination) const noexcept;
  [[nodiscard]] Step declaredLoop(Address destination) const noexcept;
  [[nodiscard]] Step call(Address destination) const noexcept;
  [[nodiscard]] Step return_() const noexcept;

  // Formats can manage repeat counters directly when their driver does not fit
  // the counted-repeat helpers below.
  [[nodiscard]] RepeatCounter repeatCounter(u8 slot);

  // Counted-repeat helpers cover drivers where the first encounter counts as
  // one play and a repeat command jumps back to a decoded source block.
  [[nodiscard]] Effects countedRepeatUntil(u8 slot, u32 totalPlays, Address destination);
  [[nodiscard]] BranchResult countedRepeatBreak(u8 slot, Address destination);

  [[nodiscard]] u64 tick() const noexcept;
  void diagnostic(Diagnostic diagnostic);

private:
  friend struct detail::VmApiAccess;

  VmApi(detail::VmTrackRuntime& runtime, PerformanceSequence& sequence, const SourceCommand& command);

  detail::VmTrackRuntime& runtime_;
  PerformanceSequence& sequence_;
  const SourceCommand& command_;
};

struct SequenceVmOptions {
  LoopPolicy loopPolicy = LoopPolicy::Default;
  // Extra runtime loop repeats after the first pass through an infinite loop.
  u32 sequenceLoops = 0;
};

// SequenceVm turns a parsed source-driver program into target-neutral performance events.
// It walks each track, asks the SequenceDialect to execute each SourceCommand, advances time
// from the returned Effects, and handles jumps, calls, repeats, loop policy, and diagnostics.
// MIDI or other exporters consume the resulting PerformanceSequence later.
class SequenceVm {
public:
  SequenceVm() = default;
  explicit SequenceVm(LoopPolicy loopPolicy);
  explicit SequenceVm(SequenceVmOptions options);

  [[nodiscard]] PerformanceSequence render(const SequenceProgram& program, const SequenceDialect& dialect) const;

private:
  [[nodiscard]] SequenceProgramBehavior resolvedBehavior(const SequenceProgram& program,
                                                         const SequenceDialect& dialect) const;

  SequenceVmOptions options_;
};

}  // namespace vgmtrans::core
