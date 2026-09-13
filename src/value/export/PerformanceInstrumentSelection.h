/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/synth/SynthModel.h"

#include <algorithm>
#include <span>

namespace vgmtrans::core {

[[nodiscard]] inline bool matchesInstrumentSelection(const Instrument& instrument,
                                                     const InstrumentSelection& selection) noexcept {
  if (const auto* identity = std::get_if<InstrumentIdentity>(&selection)) {
    return instrument.identity && *instrument.identity == *identity;
  }
  return resolveInstrumentAddress(instrument.explicitAddress, instrument.identity) ==
         std::get<InstrumentAddress>(selection);
}

[[nodiscard]] inline const Instrument* findPerformanceInstrument(
    const InstrumentSelection& selection, std::span<const SoundBankAsset* const> soundBanks) noexcept {
  for (const auto* soundBank : soundBanks) {
    if (soundBank == nullptr) {
      continue;
    }
    const auto found = std::ranges::find_if(soundBank->instruments, [&](const Instrument& instrument) {
      return matchesInstrumentSelection(instrument, selection);
    });
    if (found != soundBank->instruments.end()) {
      return &*found;
    }
  }
  return nullptr;
}

}  // namespace vgmtrans::core
