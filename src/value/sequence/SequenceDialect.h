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
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace vgmtrans::core {

class PerformanceEmitter;
class VmApi;

enum class StepKind {
  Next,
  End,
  Jump,
  Call,
  Return,
};

enum class JumpSemantics {
  Normal,
  FiniteBranch,
  FiniteRepeat,
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

using ExecuteSourceCommand = Effects (*)(const SourceCommand&, const TrackProgram&, std::any& trackState,
                                         PerformanceEmitter& out, VmApi& vm, const std::any& context);
using CreateTrackState = std::any (*)(const SequenceProgram&, const TrackProgram&, const std::any& context);
using CommandTypeToken = const void*;

namespace detail {

template <class Command>
[[nodiscard]] CommandTypeToken commandTypeToken();

}  // namespace detail

struct CommandHandler {
  CommandHandlerId id;
  CommandTypeToken typeToken = nullptr;
  ExecuteSourceCommand execute = nullptr;
};

struct SequenceDialect {
  DialectId id;
  std::string commandDetailKindPrefix;
  Timebase timebase;
  SequenceProgramBehavior defaultBehavior;
  CreateTrackState createTrackState = nullptr;
  std::vector<CommandHandler> handlers;
  std::any context;

  [[nodiscard]] const CommandHandler* handler(CommandHandlerId id) const;
  [[nodiscard]] const CommandHandler* handlerForType(CommandTypeToken typeToken) const;
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

namespace detail {

template <class Command>
[[nodiscard]] CommandTypeToken commandTypeToken() {
  static const int token = 0;
  return &token;
}

template <class TrackState, class Context>
std::any createTrackState(const SequenceProgram& program, const TrackProgram& track, const std::any& context) {
  if constexpr (std::constructible_from<TrackState, const SequenceProgram&, const TrackProgram&, const Context&>) {
    return TrackState{program, track, std::any_cast<const Context&>(context)};
  } else if constexpr (std::constructible_from<TrackState, const SequenceProgram&, const TrackProgram&>) {
    return TrackState{program, track};
  } else {
    return TrackState{};
  }
}

}  // namespace detail

}  // namespace vgmtrans::core
