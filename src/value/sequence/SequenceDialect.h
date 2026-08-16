/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/sequence/SequenceProgram.h"

#include <string>

namespace vgmtrans::core {

struct SequenceDialect {
  std::string commandDetailKindPrefix;
  Timebase timebase;
  SequenceProgramBehavior behavior;

  // Start a program with this dialect's timebase and playback behavior. The
  // parser attaches its complete, program-specific runtime before publishing.
  [[nodiscard]] SequenceProgram makeProgram() const {
    return SequenceProgram{
        .timebase = timebase,
        .behavior = behavior,
    };
  }
};

}  // namespace vgmtrans::core
