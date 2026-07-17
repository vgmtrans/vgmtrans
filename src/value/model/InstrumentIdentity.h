/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/CoreTypes.h"

#include <string>

namespace vgmtrans::core {

// Stable source-domain identity for selecting an instrument. Formats emit this
// instead of pre-encoding a MIDI/SF2 bank and program; exporters resolve it
// against the instrument values attached to the collection.
struct InstrumentIdentity {
  std::string domain;
  u32 key = 0;

  [[nodiscard]] bool valid() const noexcept { return !domain.empty(); }
  friend bool operator==(const InstrumentIdentity&, const InstrumentIdentity&) noexcept = default;
};

}  // namespace vgmtrans::core
