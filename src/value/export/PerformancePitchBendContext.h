/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/export/PerformanceInstrumentSelection.h"

#include <algorithm>
#include <optional>
#include <span>

namespace vgmtrans::core {

// The ranges which give a normalized source wheel its musical meaning.
// Consumers may follow events chronologically or retain snapshots for later queries.
class PerformancePitchBendContext {
 public:
  explicit PerformancePitchBendContext(
      std::span<const SoundBankAsset* const> soundBanks = {}) noexcept {
    selectInstrument(InstrumentPerformanceEvent{}, soundBanks);
  }

  // Returns true only when the effective pitch context changes.
  [[nodiscard]] bool apply(const PerformanceEvent& event,
                           std::span<const SoundBankAsset* const> soundBanks) noexcept {
    const auto previous = *this;
    if (const auto* range = std::get_if<PitchBendRangePerformanceEvent>(&event)) {
      sourceRangeCents_ = range->cents;
    } else if (const auto* selection = std::get_if<InstrumentPerformanceEvent>(&event)) {
      selectInstrument(*selection, soundBanks);
    } else if (const auto* note = std::get_if<NotePerformanceEvent>(&event);
               note != nullptr && !note->extendsPrevious && note->instrumentAddress) {
      const auto address = *note->instrumentAddress;
      selectInstrument(InstrumentPerformanceEvent{.bank = address.bank, .program = address.program}, soundBanks);
    }
    return *this != previous;
  }

  void setSourceRangeCents(u16 cents) noexcept { sourceRangeCents_ = cents; }
  void setInstrumentRangeCents(std::optional<u16> cents) noexcept { instrumentRangeCents_ = cents; }

  [[nodiscard]] u16 sourceRangeCents() const noexcept { return sourceRangeCents_; }
  [[nodiscard]] std::optional<u16> instrumentRangeCents() const noexcept { return instrumentRangeCents_; }
  [[nodiscard]] u16 availableRangeCents() const noexcept {
    return std::max(sourceRangeCents_, instrumentRangeCents_.value_or(static_cast<u16>(0)));
  }
  [[nodiscard]] double semitones(const PitchBendPerformanceEvent& bend) const noexcept {
    return effectivePitchBendSemitones(bend, sourceRangeCents_, instrumentRangeCents_);
  }

  friend bool operator==(const PerformancePitchBendContext&, const PerformancePitchBendContext&) noexcept = default;

 private:
  void selectInstrument(const InstrumentPerformanceEvent& selection,
                        std::span<const SoundBankAsset* const> soundBanks) noexcept {
    const auto* instrument = findPerformanceInstrument(selection, soundBanks);
    instrumentRangeCents_ = instrument == nullptr ? std::nullopt : instrument->pitchBendRangeCents;
  }

  u16 sourceRangeCents_ = 200;
  std::optional<u16> instrumentRangeCents_;
};

}  // namespace vgmtrans::core
