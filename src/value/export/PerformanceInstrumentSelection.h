/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/sequence/PerformanceModel.h"
#include "value/synth/SynthModel.h"

#include <algorithm>
#include <span>

namespace vgmtrans::core {

[[nodiscard]] inline const Instrument* findPerformanceInstrument(
    const InstrumentPerformanceEvent& selection,
    std::span<const SoundBankAsset* const> soundBanks) noexcept {
  const InstrumentAddress directAddress{.bank = selection.bank, .program = selection.program};
  for (const auto* soundBank : soundBanks) {
    if (soundBank == nullptr) {
      continue;
    }
    const auto found = std::ranges::find_if(soundBank->instruments, [&](const Instrument& instrument) {
      if (selection.sourceInstrument) {
        return instrument.identity && *instrument.identity == *selection.sourceInstrument;
      }
      return resolveInstrumentAddress(instrument.explicitAddress, instrument.identity) == directAddress;
    });
    if (found != soundBank->instruments.end()) {
      return &*found;
    }
  }
  return nullptr;
}

}  // namespace vgmtrans::core
