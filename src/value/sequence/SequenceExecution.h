/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/model/MetadataModel.h"

#include <optional>

namespace vgmtrans::core {

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

// A Step is a transition selected while executing one command. Most commands
// do not produce one: SequenceVm applies their static CommandFlow instead.
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
  // Empty means "use the command's static transition." An explicit Next is
  // retained for the rare case where runtime state selects the continuation
  // instead of a different static default.
  std::optional<Step> flowOverride;

  [[nodiscard]] static constexpr Effects none() noexcept { return Effects{}; }
  [[nodiscard]] static constexpr Effects wait(u32 ticks) noexcept { return Effects{.advanceTicks = ticks}; }
  [[nodiscard]] static constexpr Effects overrideWith(Step step) noexcept { return Effects{.flowOverride = step}; }
};

}  // namespace vgmtrans::core
