/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/sequence/PerformanceModel.h"
#include "value/sequence/SequenceProgram.h"

#include <any>
#include <concepts>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace vgmtrans::core {

class PerformanceEmitter;
class VmApi;

enum class StepKind {
  Next,
  End,
  EndSection,
  Jump,
  Call,
  Return,
};

enum class JumpSemantics {
  Normal,
  FiniteBranch,
  LoopCandidate,
  DeclaredLoop,
};

// Step names the primitive control-flow result of a command. JumpSemantics
// annotates jump-like steps when the VM needs loop-policy context.
struct Step {
  StepKind kind = StepKind::Next;
  Address destination;
  JumpSemantics jumpSemantics = JumpSemantics::Normal;

  [[nodiscard]] static constexpr Step next() noexcept { return Step{.kind = StepKind::Next}; }
  [[nodiscard]] static constexpr Step end() noexcept { return Step{.kind = StepKind::End}; }
  [[nodiscard]] static constexpr Step endSection() noexcept { return Step{.kind = StepKind::EndSection}; }
  [[nodiscard]] static constexpr Step jump(Address destination,
                                           JumpSemantics semantics = JumpSemantics::Normal) noexcept {
    return Step{.kind = StepKind::Jump, .destination = destination, .jumpSemantics = semantics};
  }
  [[nodiscard]] static constexpr Step call(Address destination) noexcept {
    return Step{.kind = StepKind::Call, .destination = destination};
  }
  [[nodiscard]] static constexpr Step return_() noexcept { return Step{.kind = StepKind::Return}; }
};

struct Effects {
  u32 advanceTicks = 0;
  Step step = Step::next();

  [[nodiscard]] static constexpr Effects none() noexcept { return Effects{}; }
  [[nodiscard]] static constexpr Effects wait(u32 ticks) noexcept { return Effects{.advanceTicks = ticks}; }
};

// Formats play only command values saved during decoding. They do not receive
// the original source bytes or loosely typed format context.
using CreateProgramState = std::any (*)(const SequenceProgram&);
using CreateTrackState = std::any (*)(const SequenceProgram&, const TrackProgram&);
using ExecuteCommand = Effects (*)(const SourceCommand&, std::any& programState, std::any& trackState,
                                   PerformanceEmitter& out, VmApi& vm);
using TickTrackState = void (*)(const SourceCommand&, std::any& programState, std::any& trackState,
                                PerformanceEmitter& out, VmApi& vm);
using FinishPrepass = void (*)(std::any& programState);
using BeginTrackSection = void (*)(std::any& trackState);
using FinalizePerformance = void (*)(std::any& programState, PerformanceSequence& performance);

enum class SemanticPrepassMode {
  // Render immediately; the format does not need information from later
  // commands before it can emit the first event.
  None,
  // Run a silent first pass in normal time order. Use this when one track can
  // change a song-wide value that another track reads.
  ScheduledPlayback,
  // Visit every decoded command once in source order. Use this to collect
  // limits from blocks that normal control flow might skip.
  DecodedCommands,
};

struct SequenceDialect {
  DialectId id;
  std::string commandDetailKindPrefix;
  Timebase timebase;
  SequenceProgramBehavior defaultBehavior;
  // PreserveFormat uses this default when a transition has no preference of
  // its own; an export request may still override every transition.
  PitchTransitionRenderingHint preferredPitchTransitionRendering = PitchTransitionRenderingHint::Portamento;
  CreateProgramState createProgramState = nullptr;
  CreateTrackState createTrackState = nullptr;
  ExecuteCommand execute = nullptr;
  TickTrackState tick = nullptr;
  FinishPrepass finishPrepass = nullptr;
  BeginTrackSection beginTrackSection = nullptr;
  FinalizePerformance finalizePerformance = nullptr;
  SemanticPrepassMode prepass = SemanticPrepassMode::None;

  // Formats normally want a program with this dialect's identity, timebase,
  // and default VM behavior. Keep that mechanical wiring out of each parser.
  [[nodiscard]] SequenceProgram makeProgram(Address sourceBaseAddress = {}) const {
    return SequenceProgram{
        .dialect = id,
        .timebase = timebase,
        .sourceBaseAddress = sourceBaseAddress,
        .behavior = defaultBehavior,
    };
  }
};

class SequenceDialectRegistry {
public:
  void add(SequenceDialect dialect);
  void seal() noexcept;
  [[nodiscard]] const SequenceDialect* find(std::string_view id) const;
  [[nodiscard]] bool contains(std::string_view id) const;
  [[nodiscard]] bool sealed() const noexcept { return sealed_; }

private:
  std::unordered_map<std::string, SequenceDialect> dialects_;
  bool sealed_ = false;
};

}  // namespace vgmtrans::core
