/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/core/Model.h"

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace vgmtrans::core {

struct TrackState {
  u32 trackIndex = 0;
  u8 channel = 0;
  u64 tick = 0;
  u32 durationRate = 0;
  s32 transpose = 0;
  s32 globalTranspose = 0;
  u8 lfoRate = 0;
  u8 vibratoDepth = 0;
  u8 tremoloDepth = 0;
  double portamentoMillisecondsPerCent = 0.0;
  u16 lastPortamentoTime = 0;
  u32 noteOctave = 0;
  bool noteDotted = false;
  bool noteTriplet = false;
  bool noteSlurred = false;
  bool noteOctaveUp = false;
  bool lastNoteSlurred = false;
  bool didRest = false;
  s32 lastKey = -1;
  std::array<u32, 4> repeatCounters{};
};

struct NoteTiming {
  u8 key = 0;
  u8 velocity = 127;
  u32 soundingTicks = 0;
  u32 advanceTicks = 0;
  bool extendsPrevious = false;
  std::vector<Event> beforeEvents;
};

class SequencerProfile {
 public:
  virtual ~SequencerProfile() = default;

  virtual void beginTrack(const CommandSequence& commandSequence, const CommandTrack& track,
                          TrackState& state, std::vector<Event>& events) const;
  [[nodiscard]] virtual u32 restTicks(const RestCommand& command, TrackState& state) const;
  [[nodiscard]] virtual NoteTiming noteTiming(const NoteCommand& command, TrackState& state) const;
  [[nodiscard]] virtual std::vector<Event> interpretNoteState(
      const NoteStateCommand& command,
      TrackState& state) const;
  virtual void applyDuration(const DurationCommand& command, TrackState& state) const;
  virtual void applyTranspose(const TransposeCommand& command, TrackState& state) const;

  [[nodiscard]] virtual std::vector<Event> interpretTempo(
      const TempoCommand& command, const TrackState& state) const;
  [[nodiscard]] virtual std::vector<Event> interpretProgram(
      const ProgramCommand& command, const TrackState& state) const;
  [[nodiscard]] virtual std::vector<Event> interpretVolume(
      const VolumeCommand& command, const TrackState& state) const;
  [[nodiscard]] virtual std::vector<Event> interpretPan(
      const PanCommand& command, const TrackState& state) const;
  [[nodiscard]] virtual std::vector<Event> interpretMasterVolume(
      const MasterVolumeCommand& command, const TrackState& state) const;
  [[nodiscard]] virtual std::vector<Event> interpretReverb(
      const ReverbCommand& command, const TrackState& state) const;
  [[nodiscard]] virtual std::vector<Event> interpretTuning(
      const TuningCommand& command, const TrackState& state) const;
  [[nodiscard]] virtual std::vector<Event> interpretPortamento(
      const PortamentoCommand& command, TrackState& state) const;
  [[nodiscard]] virtual std::vector<Event> interpretLfo(
      const LfoCommand& command, TrackState& state) const;
  [[nodiscard]] virtual std::vector<Event> interpretEnvelope(
      const EnvelopeCommand& command, const TrackState& state) const;
  [[nodiscard]] virtual std::vector<Event> interpretDriverSpecific(
      const DriverSpecificCommand& command, TrackState& state) const;
  [[nodiscard]] virtual std::vector<Event> interpretRepeatBreak(
      const RepeatBreakCommand& command, TrackState& state) const;
};

class EventSequenceBuilder {
 public:
  [[nodiscard]] EventSequence build(
      const CommandSequence& commandSequence,
      const SequencerProfile& profile,
      LoopPolicy loopPolicy) const;
};

class SequencerProfileRegistry {
 public:
  using Factory = std::function<std::unique_ptr<SequencerProfile>()>;

  void add(std::string format, Factory factory);
  [[nodiscard]] std::unique_ptr<SequencerProfile> create(std::string_view format) const;
  [[nodiscard]] bool contains(std::string_view format) const;

 private:
  std::unordered_map<std::string, Factory> factories_;
};

}  // namespace vgmtrans::core
