/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/model/MetadataModel.h"

#include <optional>

namespace vgmtrans::core {

enum class RuntimeTransitionKind {
  Fallthrough,
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

// A RuntimeTransition is selected while executing one command. Most commands
// do not produce one: SequenceVm applies their static CommandFlow instead.
struct RuntimeTransition {
  RuntimeTransitionKind kind = RuntimeTransitionKind::Fallthrough;
  Address destination;
  JumpSemantics jumpSemantics = JumpSemantics::Normal;

  [[nodiscard]] static constexpr RuntimeTransition fallthrough() noexcept {
    return RuntimeTransition{.kind = RuntimeTransitionKind::Fallthrough};
  }
  [[nodiscard]] static constexpr RuntimeTransition end() noexcept {
    return RuntimeTransition{.kind = RuntimeTransitionKind::End};
  }
  [[nodiscard]] static constexpr RuntimeTransition endSection() noexcept {
    return RuntimeTransition{.kind = RuntimeTransitionKind::EndSection};
  }
  [[nodiscard]] static constexpr RuntimeTransition jump(Address destination,
                                                        JumpSemantics semantics = JumpSemantics::Normal) noexcept {
    return RuntimeTransition{
        .kind = RuntimeTransitionKind::Jump,
        .destination = destination,
        .jumpSemantics = semantics,
    };
  }
  [[nodiscard]] static constexpr RuntimeTransition call(Address destination) noexcept {
    return RuntimeTransition{
        .kind = RuntimeTransitionKind::Call,
        .destination = destination,
    };
  }
  [[nodiscard]] static constexpr RuntimeTransition return_() noexcept {
    return RuntimeTransition{.kind = RuntimeTransitionKind::Return};
  }
};

struct Effects {
  u32 advanceTicks = 0;
  // Empty means "use the command's static transition." An explicit
  // Fallthrough remains distinct when runtime state selects the continuation
  // instead of the static default.
  std::optional<RuntimeTransition> flowOverride;

  [[nodiscard]] static constexpr Effects none() noexcept { return Effects{}; }
  [[nodiscard]] static constexpr Effects wait(u32 ticks) noexcept { return Effects{.advanceTicks = ticks}; }
};

}  // namespace vgmtrans::core
