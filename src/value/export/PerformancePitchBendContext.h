/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/export/ResolvedPerformance.h"

#include <optional>

namespace vgmtrans::core {

// The ranges which give a normalized source wheel its musical meaning.
// Consumers may follow events chronologically or retain snapshots for later queries.
class PerformancePitchBendContext {
 public:
  PerformancePitchBendContext() = default;
  explicit PerformancePitchBendContext(const ResolvedPerformance& performance) {
    selectInstrument(performance.initialInstrument());
  }

  // Apply source controls. The planner explicitly selects each source voice's
  // first attack; a raw note's extendsPrevious flag only describes its anchor.
  void apply(const PerformanceEvent& event, const ResolvedPerformance& performance) {
    if (const auto* range = std::get_if<PitchBendRangePerformanceEvent>(&event)) {
      sourceRangeCents_ = range->cents;
    } else if (const auto* selection = std::get_if<InstrumentPerformanceEvent>(&event)) {
      selectInstrument(performance.selectionFor(*selection).instrument);
    }
  }

  void setSourceRangeCents(u16 cents) noexcept { sourceRangeCents_ = cents; }
  void setInstrumentRangeCents(std::optional<u16> cents) noexcept { instrumentRangeCents_ = cents; }

  [[nodiscard]] u16 rangeCents() const noexcept {
    return instrumentRangeCents_.value_or(sourceRangeCents_);
  }
  [[nodiscard]] double semitones(const PitchBendPerformanceEvent& bend) const noexcept {
    return effectivePitchBendSemitones(bend, sourceRangeCents_, instrumentRangeCents_);
  }

  friend bool operator==(const PerformancePitchBendContext&, const PerformancePitchBendContext&) noexcept = default;

  void selectInstrument(const Instrument* instrument) {
    instrumentRangeCents_ = instrument == nullptr ? std::nullopt : instrument->pitchBendRangeCents;
  }

 private:
  u16 sourceRangeCents_ = 200;
  std::optional<u16> instrumentRangeCents_;
};

}  // namespace vgmtrans::core
