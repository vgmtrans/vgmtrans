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
  [[nodiscard]] std::vector<core::Event> lowerNoteState(
      const core::NoteStateCommand& command,
      core::TrackState& state) const override;
  void applyDuration(const core::DurationCommand& command, core::TrackState& state) const override;

  [[nodiscard]] std::vector<core::Event> lowerTempo(
      const core::TempoCommand& command,
      const core::TrackState& state) const override;
  [[nodiscard]] std::vector<core::Event> lowerVolume(
      const core::VolumeCommand& command,
      const core::TrackState& state) const override;
  [[nodiscard]] std::vector<core::Event> lowerProgram(
      const core::ProgramCommand& command,
      const core::TrackState& state) const override;
  [[nodiscard]] std::vector<core::Event> lowerPan(
      const core::PanCommand& command,
      const core::TrackState& state) const override;
  [[nodiscard]] std::vector<core::Event> lowerMasterVolume(
      const core::MasterVolumeCommand& command,
      const core::TrackState& state) const override;
  [[nodiscard]] std::vector<core::Event> lowerReverb(
      const core::ReverbCommand& command,
      const core::TrackState& state) const override;
  [[nodiscard]] std::vector<core::Event> lowerTuning(
      const core::TuningCommand& command,
      const core::TrackState& state) const override;
  [[nodiscard]] std::vector<core::Event> lowerPortamento(
      const core::PortamentoCommand& command,
      core::TrackState& state) const override;
  [[nodiscard]] std::vector<core::Event> lowerLfo(
      const core::LfoCommand& command,
      core::TrackState& state) const override;
  [[nodiscard]] std::vector<core::Event> lowerRepeatBreak(
      const core::RepeatBreakCommand& command,
      core::TrackState& state) const override;

 private:
  CapcomSnesEngineVersion version_;
};

void registerCapcomSnesProfile(core::SequencerProfileRegistry& registry);

}  // namespace vgmtrans::formats::capcom_snes
