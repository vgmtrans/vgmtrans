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
  SequenceProgramBehavior defaultBehavior;
  SequenceRuntime runtime;

  // Formats normally want a program with this dialect's runtime, timebase, and
  // default VM behavior. Keep that mechanical wiring out of each parser.
  [[nodiscard]] SequenceProgram makeProgram(Address sourceBaseAddress = {}) const {
    return SequenceProgram{
        .runtime = runtime,
        .timebase = timebase,
        .sourceBaseAddress = sourceBaseAddress,
        .behavior = defaultBehavior,
    };
  }
};

}  // namespace vgmtrans::core
