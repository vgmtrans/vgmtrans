/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "core/PerformanceLowerer.h"

namespace vgmtrans::formats::capcom_snes {

enum class CapcomSnesEngineVersion : u8 {
  v1BgmInList,
  v2BgmUsuallyAtFixedLocation,
  v3BgmFixedLocation,
};

class CapcomSnesProfile final : public core::SequencerProfile {
 public:
  explicit CapcomSnesProfile(CapcomSnesEngineVersion version = CapcomSnesEngineVersion::v3BgmFixedLocation);

  [[nodiscard]] u32 restTicks(const core::RestCommand& command, core::TrackState& state) const override;
  [[nodiscard]] core::NoteTiming noteTiming(
      const core::NoteCommand& command,
      core::TrackState& state) const override;
  void applyDuration(const core::DurationCommand& command, core::TrackState& state) const override;

  [[nodiscard]] std::vector<core::PerformanceEvent> lowerTempo(
      const core::TempoCommand& command,
      const core::TrackState& state) const override;
  [[nodiscard]] std::vector<core::PerformanceEvent> lowerVolume(
      const core::VolumeCommand& command,
      const core::TrackState& state) const override;
  [[nodiscard]] std::vector<core::PerformanceEvent> lowerProgram(
      const core::ProgramCommand& command,
      const core::TrackState& state) const override;
  [[nodiscard]] std::vector<core::PerformanceEvent> lowerPan(
      const core::PanCommand& command,
      const core::TrackState& state) const override;
  [[nodiscard]] std::vector<core::PerformanceEvent> lowerMasterVolume(
      const core::MasterVolumeCommand& command,
      const core::TrackState& state) const override;
  [[nodiscard]] std::vector<core::PerformanceEvent> lowerReverb(
      const core::ReverbCommand& command,
      const core::TrackState& state) const override;
  [[nodiscard]] std::vector<core::PerformanceEvent> lowerTuning(
      const core::TuningCommand& command,
      const core::TrackState& state) const override;
  [[nodiscard]] std::vector<core::PerformanceEvent> lowerLfo(
      const core::LfoCommand& command,
      const core::TrackState& state) const override;
  [[nodiscard]] std::vector<core::PerformanceEvent> lowerDriverSpecific(
      const core::DriverSpecificCommand& command,
      core::TrackState& state) const override;

 private:
  CapcomSnesEngineVersion version_;
};

void registerCapcomSnesProfile(core::SequencerProfileRegistry& registry);

}  // namespace vgmtrans::formats::capcom_snes
