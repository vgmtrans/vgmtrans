/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/base/CoreTypes.h"

#include <compare>
#include <optional>
#include <string>
#include <variant>

namespace vgmtrans::core {

// Stable source-domain identity for selecting an instrument. Formats emit this
// instead of pre-encoding a MIDI/SF2 bank and program; exporters resolve it
// against the instrument values attached to the collection.
struct InstrumentIdentity {
  std::string domain;
  u32 key = 0;

  [[nodiscard]] bool valid() const noexcept { return !domain.empty(); }
  friend auto operator<=>(const InstrumentIdentity&, const InstrumentIdentity&) noexcept = default;
};

struct InstrumentAddress {
  u32 bank = 0;
  u32 program = 0;

  friend auto operator<=>(const InstrumentAddress&, const InstrumentAddress&) noexcept = default;
};

// Index into the bank copies retained by one ResolvedPerformance. Appending
// variants does not invalidate handles; bank/instrument order must remain stable.
struct InstrumentHandle {
  u32 bank = 0;
  u32 instrument = 0;
  friend auto operator<=>(const InstrumentHandle&, const InstrumentHandle&) = default;
};

// Formats emit addresses or native identities. Export preparation replaces
// those with handles, retaining addresses only for external/unavailable presets.
using InstrumentSelection = std::variant<InstrumentAddress, InstrumentIdentity, InstrumentHandle>;

// Export addresses are explicit policy when a format needs a particular bank;
// otherwise a source identity receives a stable sequential 128-program address.
// These are source-address preferences. Resolved output consumers use the shared
// InstrumentAddressPlan, which may relocate a conflicting or generated preset.
[[nodiscard]] inline InstrumentAddress resolveInstrumentAddress(const InstrumentIdentity& identity) noexcept {
  return InstrumentAddress{.bank = identity.key >> 7, .program = identity.key & 0x7f};
}

[[nodiscard]] inline InstrumentAddress resolveInstrumentAddress(const InstrumentSelection& selection) {
  if (const auto* identity = std::get_if<InstrumentIdentity>(&selection)) {
    return resolveInstrumentAddress(*identity);
  }
  return std::get<InstrumentAddress>(selection);
}

[[nodiscard]] inline InstrumentAddress resolveInstrumentAddress(
    const std::optional<InstrumentAddress>& explicitAddress,
    const std::optional<InstrumentIdentity>& identity) noexcept {
  if (explicitAddress) {
    return *explicitAddress;
  }
  return identity ? resolveInstrumentAddress(*identity) : InstrumentAddress{};
}

}  // namespace vgmtrans::core
