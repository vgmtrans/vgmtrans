/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/core/MidiSequenceProfile.h"

namespace vgmtrans::formats::nds {

inline constexpr auto kNdsProfileName = "NDS";

class NdsProfile final : public core::MidiSequenceProfile {
 public:
  void beginTrack(const core::CommandSequence& commandSequence, const core::CommandTrack& track,
                  core::MidiTrackState& state, std::vector<core::MidiEvent>& events) const override;
  [[nodiscard]] core::MidiNoteTiming noteTiming(
      const core::NoteCommand& command,
      core::MidiTrackState& state) const override;
  [[nodiscard]] std::vector<core::MidiEvent> interpretTempo(
      const core::TempoCommand& command,
      const core::MidiTrackState& state) const override;
  [[nodiscard]] std::vector<core::MidiEvent> interpretDriverSpecific(
      const core::DriverSpecificCommand& command,
      core::MidiTrackState& state) const override;
};

void registerNdsProfile(core::MidiSequenceProfileRegistry& registry);

}  // namespace vgmtrans::formats::nds
