/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/core/MidiModel.h"
#include "value/core/SequenceModel.h"

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace vgmtrans::core {

struct MidiTrackState {
  u32 trackIndex = 0;
  u8 channel = 0;
  u64 tick = 0;
  u32 durationRate = 0;
  s32 transpose = 0;
  s32 globalTranspose = 0;
  u8 modulationRate = 0;
  u8 vibratoDepth = 0;
  u8 tremoloDepth = 0;
  double portamentoMillisecondsPerCent = 0.0;
  u16 lastPortamentoTime = 0;
  u32 noteOctave = 0;
  bool noteDotted = false;
  bool noteWait = true;
  bool noteTriplet = false;
  bool noteSlurred = false;
  bool noteOctaveUp = false;
  bool lastNoteSlurred = false;
  bool didRest = false;
  s32 lastKey = -1;
  std::array<u32, 4> repeatCounters{};
};

struct MidiNoteTiming {
  u8 key = 0;
  u8 velocity = 127;
  u32 soundingTicks = 0;
  u32 advanceTicks = 0;
  bool extendsPrevious = false;
  std::vector<MidiEvent> beforeEvents;
};

class MidiSequenceProfile {
 public:
  virtual ~MidiSequenceProfile() = default;

  virtual void beginTrack(const CommandSequence& commandSequence, const CommandTrack& track,
                          MidiTrackState& state, std::vector<MidiEvent>& events) const;
  [[nodiscard]] virtual u32 restTicks(const RestCommand& command, MidiTrackState& state) const;
  [[nodiscard]] virtual MidiNoteTiming noteTiming(const NoteCommand& command, MidiTrackState& state) const;
  [[nodiscard]] virtual std::vector<MidiEvent> interpretNoteState(
      const NoteStateCommand& command,
      MidiTrackState& state) const;
  virtual void applyDuration(const DurationCommand& command, MidiTrackState& state) const;
  virtual void applyTranspose(const TransposeCommand& command, MidiTrackState& state) const;

  [[nodiscard]] virtual std::vector<MidiEvent> interpretTempo(
      const TempoCommand& command, const MidiTrackState& state) const;
  [[nodiscard]] virtual std::vector<MidiEvent> interpretProgram(
      const ProgramCommand& command, const MidiTrackState& state) const;
  [[nodiscard]] virtual std::vector<MidiEvent> interpretVolume(
      const VolumeCommand& command, const MidiTrackState& state) const;
  [[nodiscard]] virtual std::vector<MidiEvent> interpretPan(
      const PanCommand& command, const MidiTrackState& state) const;
  [[nodiscard]] virtual std::vector<MidiEvent> interpretMasterVolume(
      const MasterVolumeCommand& command, const MidiTrackState& state) const;
  [[nodiscard]] virtual std::vector<MidiEvent> interpretReverb(
      const ReverbCommand& command, const MidiTrackState& state) const;
  [[nodiscard]] virtual std::vector<MidiEvent> interpretTuning(
      const TuningCommand& command, const MidiTrackState& state) const;
  [[nodiscard]] virtual std::vector<MidiEvent> interpretPortamento(
      const PortamentoCommand& command, MidiTrackState& state) const;
  [[nodiscard]] virtual std::vector<MidiEvent> interpretVibrato(
      const VibratoCommand& command, MidiTrackState& state) const;
  [[nodiscard]] virtual std::vector<MidiEvent> interpretTremolo(
      const TremoloCommand& command, MidiTrackState& state) const;
  [[nodiscard]] virtual std::vector<MidiEvent> interpretModulationRate(
      const ModulationRateCommand& command, MidiTrackState& state) const;
  [[nodiscard]] virtual std::vector<MidiEvent> interpretEnvelope(
      const EnvelopeCommand& command, const MidiTrackState& state) const;
  [[nodiscard]] virtual std::vector<MidiEvent> interpretDriverSpecific(
      const DriverSpecificCommand& command, MidiTrackState& state) const;
  [[nodiscard]] virtual std::vector<MidiEvent> interpretRepeatBreak(
      const RepeatBreakCommand& command, MidiTrackState& state) const;
};

class MidiSequenceProfileRegistry {
 public:
  using Factory = std::function<std::unique_ptr<MidiSequenceProfile>()>;

  void add(std::string format, Factory factory);
  [[nodiscard]] std::unique_ptr<MidiSequenceProfile> create(std::string_view format) const;
  [[nodiscard]] bool contains(std::string_view format) const;

 private:
  std::unordered_map<std::string, Factory> factories_;
};

}  // namespace vgmtrans::core
