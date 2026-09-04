/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/CoreTypes.h"

#include <optional>
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

struct InstrumentAddress {
  u32 bank = 0;
  u32 program = 0;

  friend bool operator==(const InstrumentAddress&, const InstrumentAddress&) noexcept = default;
};

// Export addresses are explicit policy when a format needs a particular bank;
// otherwise a source identity receives a stable sequential 128-program address.
// Every target uses this function so identity and address cannot disagree.
[[nodiscard]] inline InstrumentAddress resolveInstrumentAddress(
    const std::optional<InstrumentAddress>& explicitAddress,
    const std::optional<InstrumentIdentity>& identity) noexcept {
  if (explicitAddress) {
    return *explicitAddress;
  }
  const u32 sequentialKey = identity ? identity->key : 0;
  return InstrumentAddress{
      .bank = sequentialKey >> 7,
      .program = sequentialKey & 0x7f,
  };
}

}  // namespace vgmtrans::core
