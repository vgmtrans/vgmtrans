/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "base/Types.h"
#include "value/base/CoreTypes.h"

#include <string>

namespace vgmtrans::core {

// Common metadata for sequences, instrument sets, sample collections, and misc
// assets. Exporters use this for names and source ranges instead of rediscovering them.
struct AssetMetadata {
  AssetId id;
  std::string format;
  std::string name;
  SourceRange range;
};

// Address is the canonical sequence bytecode/source offset used by TrackProgram
// and SequenceVm. Formats must translate raw driver pointers into this address
// space before handing them to VM flow helpers such as jump, call, or target.
struct Address {
  u64 value = 0;
};

struct Timebase {
  u32 ppqn = 48;
};

// How export should treat source loops. The parsed sequence still keeps the
// original loop commands for formats that can represent them directly.
enum class LoopPolicy {
  Default,
  PlayOnce,
  Preserve,
};

}  // namespace vgmtrans::core
