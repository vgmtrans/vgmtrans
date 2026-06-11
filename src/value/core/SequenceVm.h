/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/core/PerformanceModel.h"
#include "value/core/SequenceDialect.h"

#include <map>
#include <vector>

namespace vgmtrans::core {

struct VmTrackRuntime;

class Emit {
public:
  Emit(PerformanceTrack& track, CommandId sourceCommand, u64 tick);

  void note(NotePerformanceEvent event);
  void tempo(TempoPerformanceEvent event);
  void instrument(InstrumentPerformanceEvent event);
  void level(LevelPerformanceEvent event);
  void pan(PanPerformanceEvent event);
  void masterLevel(MasterLevelPerformanceEvent event);
  void reverb(ReverbPerformanceEvent event);
  void tuning(TuningPerformanceEvent event);
  void globalTranspose(GlobalTransposePerformanceEvent event);
  void portamento(PortamentoPerformanceEvent event);
  void portamentoControl(PortamentoControlPerformanceEvent event);
  void legatoPedal(LegatoPedalPerformanceEvent event);
  void modulation(ModulationPerformanceEvent event);
  void marker(MarkerPerformanceEvent event);

private:
  [[nodiscard]] PerformanceEventHeader header() const;

  PerformanceTrack& track_;
  CommandId sourceCommand_;
  u64 tick_ = 0;
};

class VmApi {
public:
  [[nodiscard]] Step next() const noexcept;
  [[nodiscard]] Step end() const noexcept;
  [[nodiscard]] Step jump(Address destination) const noexcept;
  [[nodiscard]] Step call(Address destination) const noexcept;
  [[nodiscard]] Step return_() const noexcept;

  // Common repeat handling stays in the VM so loop policy and dry-run logic can
  // eventually reason about repeat commands without each format reimplementing it.
  [[nodiscard]] Step repeatUntil(u8 slot, u32 count, Address destination);
  [[nodiscard]] Step repeatBreak(u8 slot, Address destination);

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

class SequenceVm {
public:
  explicit SequenceVm(LoopPolicy loopPolicy = LoopPolicy::Default);

  [[nodiscard]] PerformanceSequence render(const SequenceProgram& program, const SequenceDialect& dialect) const;

private:
  [[nodiscard]] SequenceProgramBehavior resolvedBehavior(const SequenceProgram& program,
                                                        const SequenceDialect& dialect) const;

  LoopPolicy loopPolicy_ = LoopPolicy::Default;
};

}  // namespace vgmtrans::core
