/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/model/MetadataModel.h"

#include <optional>

namespace vgmtrans::core {

enum class CommandTransitionKind {
  Fallthrough,
  Jump,
  Call,
  Return,
  End,
  EndSection,
};

enum class JumpSemantics {
  Normal,
  FiniteBranch,
  LoopCandidate,
  DeclaredLoop,
};

// A command has one decoded default transition. Its runtime body may return
// another transition when mutable driver state selects the actual path.
struct CommandTransition {
  CommandTransitionKind kind = CommandTransitionKind::Fallthrough;
  Address destination;
  JumpSemantics jumpSemantics = JumpSemantics::Normal;

  [[nodiscard]] static constexpr CommandTransition fallthrough() noexcept {
    return CommandTransition{.kind = CommandTransitionKind::Fallthrough};
  }
  [[nodiscard]] static constexpr CommandTransition end() noexcept {
    return CommandTransition{.kind = CommandTransitionKind::End};
  }
  [[nodiscard]] static constexpr CommandTransition endSection() noexcept {
    return CommandTransition{.kind = CommandTransitionKind::EndSection};
  }
  [[nodiscard]] static constexpr CommandTransition jump(
      Address destination, JumpSemantics semantics = JumpSemantics::Normal) noexcept {
    return CommandTransition{
        .kind = CommandTransitionKind::Jump,
        .destination = destination,
        .jumpSemantics = semantics,
    };
  }
  [[nodiscard]] static constexpr CommandTransition call(Address destination) noexcept {
    return CommandTransition{
        .kind = CommandTransitionKind::Call,
        .destination = destination,
    };
  }
  [[nodiscard]] static constexpr CommandTransition return_() noexcept {
    return CommandTransition{.kind = CommandTransitionKind::Return};
  }
};

struct Effects {
  u32 advanceTicks = 0;
  // Empty means "use the command's decoded default transition." An explicit
  // Fallthrough remains distinct when runtime state selects the continuation
  // instead of the decoded default.
  std::optional<CommandTransition> flowOverride;

  [[nodiscard]] static constexpr Effects none() noexcept { return Effects{}; }
  [[nodiscard]] static constexpr Effects wait(u32 ticks) noexcept { return Effects{.advanceTicks = ticks}; }
};

}  // namespace vgmtrans::core
