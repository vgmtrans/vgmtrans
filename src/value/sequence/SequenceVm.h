/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/sequence/PerformanceModel.h"
#include "value/sequence/SequenceDialect.h"

#include <map>
#include <vector>

namespace vgmtrans::core {

struct VmTrackRuntime;

struct BranchResult {
  bool taken = false;
  Effects effects;
};

// Commands call PerformanceEmitter to add notes, tempo changes, controller changes, and markers.
// PerformanceEmitter fills in the current tick and source command automatically.
class PerformanceEmitter {
public:
  PerformanceEmitter(PerformanceTrack& track, CommandId sourceCommand, u64 tick);

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
  void pan(double stereoPosition, double linearGain = 1.0);
  void masterLevel(MasterLevelPerformanceEvent event);
  void masterLevel(double linearGain);
  void reverb(ReverbPerformanceEvent event);
  void reverb(double send);
  void tuning(TuningPerformanceEvent event);
  void tuning(double cents);
  void globalTranspose(GlobalTransposePerformanceEvent event);
  void globalTranspose(s32 semitones);
  void pitchBend(PitchBendPerformanceEvent event);
  void pitchBend(s16 value);
  void pitchBendRange(PitchBendRangePerformanceEvent event);
  void pitchBendRange(u8 semitones);
  void portamento(PortamentoPerformanceEvent event);
  void portamento(double timeMilliseconds, double previousKey);
  void portamentoEnable(PortamentoEnablePerformanceEvent event);
  void portamentoEnable(bool enabled);
  void portamentoTime(PortamentoTimePerformanceEvent event);
  void portamentoTime(u8 value);
  void portamentoControl(PortamentoControlPerformanceEvent event);
  void portamentoControl(double previousKey);
  void legatoPedal(LegatoPedalPerformanceEvent event);
  void legatoPedal(bool enabled);
  void modulation(ModulationPerformanceEvent event);
  void modulation(ModulationPerformanceTarget target, double amount);
  void marker(MarkerPerformanceEvent event);

private:
  [[nodiscard]] PerformanceEventHeader header() const;

  PerformanceTrack& track_;
  CommandId sourceCommand_;
  u64 tick_ = 0;
};

// Commands use VmApi for playback flow that is shared across formats: next, end,
// jump, call, return, repeat handling, diagnostics, and current tick.
class VmApi {
public:
  [[nodiscard]] Step next() const noexcept;
  [[nodiscard]] Step end() const noexcept;
  [[nodiscard]] Step jump(Address destination) const noexcept;
  [[nodiscard]] Step jumpOrLoopForever(Address destination) const noexcept;
  [[nodiscard]] Step loopForever(Address destination) const noexcept;
  [[nodiscard]] Step call(Address destination) const noexcept;
  [[nodiscard]] Step return_() const noexcept;

  // Formats provide the repeat slot, count, and target; the VM owns the counters
  // and applies loop policy consistently.
  [[nodiscard]] Step repeatUntil(u8 slot, u32 count, Address destination);
  [[nodiscard]] Step repeatBreak(u8 slot, Address destination);
  [[nodiscard]] Effects repeatUntilEffect(u8 slot, u32 count, Address destination);
  [[nodiscard]] BranchResult repeatBreakBranch(u8 slot, Address destination);

  [[nodiscard]] u64 tick() const noexcept;
  void diagnostic(Diagnostic diagnostic);

private:
  friend class SequenceVm;

  VmApi(VmTrackRuntime& runtime, PerformanceSequence& sequence, SourceRange commandRange, u32 currentIndex);

  VmTrackRuntime& runtime_;
  PerformanceSequence& sequence_;
  SourceRange commandRange_;
  u32 currentIndex_ = 0;
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
