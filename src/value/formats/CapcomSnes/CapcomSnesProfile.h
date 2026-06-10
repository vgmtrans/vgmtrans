/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/core/MidiSequenceBuilder.h"

#include <string_view>

namespace vgmtrans::formats::capcom_snes {

enum class CapcomSnesEngineVersion : u8 {
  none,
  v1BgmInList,
  v2BgmUsuallyAtFixedLocation,
  v3BgmFixedLocation,
};

[[nodiscard]] std::string_view capcomSnesProfileName(CapcomSnesEngineVersion version);

class CapcomSnesProfile final : public core::MidiSequenceProfile {
 public:
  explicit CapcomSnesProfile(CapcomSnesEngineVersion version = CapcomSnesEngineVersion::v3BgmFixedLocation);

  [[nodiscard]] u32 restTicks(const core::RestCommand& command, core::MidiTrackState& state) const override;
  [[nodiscard]] core::MidiNoteTiming noteTiming(
      const core::NoteCommand& command,
      core::MidiTrackState& state) const override;
  [[nodiscard]] std::vector<core::MidiEvent> interpretNoteState(
      const core::NoteStateCommand& command,
      core::MidiTrackState& state) const override;
  void applyDuration(const core::DurationCommand& command, core::MidiTrackState& state) const override;

  [[nodiscard]] std::vector<core::MidiEvent> interpretTempo(
      const core::TempoCommand& command,
      const core::MidiTrackState& state) const override;
  [[nodiscard]] std::vector<core::MidiEvent> interpretVolume(
      const core::VolumeCommand& command,
      const core::MidiTrackState& state) const override;
  [[nodiscard]] std::vector<core::MidiEvent> interpretProgram(
      const core::ProgramCommand& command,
      const core::MidiTrackState& state) const override;
  [[nodiscard]] std::vector<core::MidiEvent> interpretPan(
      const core::PanCommand& command,
      const core::MidiTrackState& state) const override;
  [[nodiscard]] std::vector<core::MidiEvent> interpretMasterVolume(
      const core::MasterVolumeCommand& command,
      const core::MidiTrackState& state) const override;
  [[nodiscard]] std::vector<core::MidiEvent> interpretReverb(
      const core::ReverbCommand& command,
      const core::MidiTrackState& state) const override;
  [[nodiscard]] std::vector<core::MidiEvent> interpretTuning(
      const core::TuningCommand& command,
      const core::MidiTrackState& state) const override;
  [[nodiscard]] std::vector<core::MidiEvent> interpretPortamento(
      const core::PortamentoCommand& command,
      core::MidiTrackState& state) const override;
  [[nodiscard]] std::vector<core::MidiEvent> interpretVibrato(
      const core::VibratoCommand& command,
      core::MidiTrackState& state) const override;
  [[nodiscard]] std::vector<core::MidiEvent> interpretTremolo(
      const core::TremoloCommand& command,
      core::MidiTrackState& state) const override;
  [[nodiscard]] std::vector<core::MidiEvent> interpretModulationRate(
      const core::ModulationRateCommand& command,
      core::MidiTrackState& state) const override;
  [[nodiscard]] std::vector<core::MidiEvent> interpretRepeatBreak(
      const core::RepeatBreakCommand& command,
      core::MidiTrackState& state) const override;

 private:
  CapcomSnesEngineVersion version_;
};

void registerCapcomSnesProfile(core::MidiSequenceProfileRegistry& registry);

}  // namespace vgmtrans::formats::capcom_snes
