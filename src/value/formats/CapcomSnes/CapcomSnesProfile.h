/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/core/PerformanceLowerer.h"

#include <string_view>

namespace vgmtrans::formats::capcom_snes {

enum class CapcomSnesEngineVersion : u8 {
  none,
  v1BgmInList,
  v2BgmUsuallyAtFixedLocation,
  v3BgmFixedLocation,
};

[[nodiscard]] std::string_view capcomSnesProfileName(CapcomSnesEngineVersion version);

class CapcomSnesProfile final : public core::SequencerProfile {
 public:
  explicit CapcomSnesProfile(CapcomSnesEngineVersion version = CapcomSnesEngineVersion::v3BgmFixedLocation);

  [[nodiscard]] u32 restTicks(const core::RestCommand& command, core::TrackState& state) const override;
  [[nodiscard]] core::NoteTiming noteTiming(
      const core::NoteCommand& command,
      core::TrackState& state) const override;
  [[nodiscard]] std::vector<core::TimelineEvent> lowerNoteState(
      const core::NoteStateCommand& command,
      core::TrackState& state) const override;
  void applyDuration(const core::DurationCommand& command, core::TrackState& state) const override;

  [[nodiscard]] std::vector<core::TimelineEvent> lowerTempo(
      const core::TempoCommand& command,
      const core::TrackState& state) const override;
  [[nodiscard]] std::vector<core::TimelineEvent> lowerVolume(
      const core::VolumeCommand& command,
      const core::TrackState& state) const override;
  [[nodiscard]] std::vector<core::TimelineEvent> lowerProgram(
      const core::ProgramCommand& command,
      const core::TrackState& state) const override;
  [[nodiscard]] std::vector<core::TimelineEvent> lowerPan(
      const core::PanCommand& command,
      const core::TrackState& state) const override;
  [[nodiscard]] std::vector<core::TimelineEvent> lowerMasterVolume(
      const core::MasterVolumeCommand& command,
      const core::TrackState& state) const override;
  [[nodiscard]] std::vector<core::TimelineEvent> lowerReverb(
      const core::ReverbCommand& command,
      const core::TrackState& state) const override;
  [[nodiscard]] std::vector<core::TimelineEvent> lowerTuning(
      const core::TuningCommand& command,
      const core::TrackState& state) const override;
  [[nodiscard]] std::vector<core::TimelineEvent> lowerPortamento(
      const core::PortamentoCommand& command,
      core::TrackState& state) const override;
  [[nodiscard]] std::vector<core::TimelineEvent> lowerLfo(
      const core::LfoCommand& command,
      core::TrackState& state) const override;
  [[nodiscard]] std::vector<core::TimelineEvent> lowerRepeatBreak(
      const core::RepeatBreakCommand& command,
      core::TrackState& state) const override;

 private:
  CapcomSnesEngineVersion version_;
};

void registerCapcomSnesProfile(core::SequencerProfileRegistry& registry);

}  // namespace vgmtrans::formats::capcom_snes
