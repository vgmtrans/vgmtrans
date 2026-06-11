/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/core/MidiSequenceProfile.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

namespace vgmtrans::core {

void MidiSequenceProfile::beginTrack(
    const CommandSequence&,
    const CommandTrack&,
    MidiTrackState&,
    std::vector<MidiEvent>&) const {
}

u32 MidiSequenceProfile::restTicks(const RestCommand& command, MidiTrackState&) const {
  return command.rawDuration;
}

std::vector<MidiEvent> MidiSequenceProfile::interpretNoteState(
    const NoteStateCommand&,
    MidiTrackState&) const {
  return {};
}

MidiNoteTiming MidiSequenceProfile::noteTiming(const NoteCommand& command, MidiTrackState& state) const {
  const auto key = std::clamp<s32>(static_cast<s32>(command.key) + state.transpose + state.globalTranspose, 0, 127);
  const auto ticks = command.rawDuration;
  return MidiNoteTiming{
      .key = static_cast<u8>(key),
      .velocity = command.rawVelocity == 0 ? static_cast<u8>(127)
                                           : static_cast<u8>(std::min<u32>(command.rawVelocity, 127)),
      .soundingTicks = ticks,
      .advanceTicks = ticks,
  };
}

void MidiSequenceProfile::applyDuration(const DurationCommand& command, MidiTrackState& state) const {
  state.durationRate = command.rawValue;
}

void MidiSequenceProfile::applyTranspose(const TransposeCommand& command, MidiTrackState& state) const {
  state.transpose = command.rawSemitones;
}

std::vector<MidiEvent> MidiSequenceProfile::interpretTempo(
    const TempoCommand& command,
    const MidiTrackState& state) const {
  return {Tempo{
      .tick = state.tick,
      .microsecondsPerQuarter = command.rawValue == 0 ? 500000 : command.rawValue,
  }};
}

std::vector<MidiEvent> MidiSequenceProfile::interpretProgram(
    const ProgramCommand& command,
    const MidiTrackState& state) const {
  return {ProgramChange{
      .tick = state.tick,
      .channel = state.channel,
      .program = static_cast<u8>(std::min<u32>(command.rawProgram, 127)),
  }};
}

std::vector<MidiEvent> MidiSequenceProfile::interpretVolume(
    const VolumeCommand& command,
    const MidiTrackState& state) const {
  return {Volume{
      .tick = state.tick,
      .channel = state.channel,
      .value = static_cast<u8>(std::min<u32>(command.rawValue, 127)),
  }};
}

std::vector<MidiEvent> MidiSequenceProfile::interpretPan(
    const PanCommand& command,
    const MidiTrackState& state) const {
  return {Pan{
      .tick = state.tick,
      .channel = state.channel,
      .value = static_cast<u8>(std::min<u32>(command.rawValue, 127)),
  }};
}

std::vector<MidiEvent> MidiSequenceProfile::interpretMasterVolume(
    const MasterVolumeCommand& command,
    const MidiTrackState& state) const {
  return {MasterVolume{
      .tick = state.tick,
      .value = static_cast<u16>(std::min<u32>(command.rawValue, 0x3fff)),
  }};
}

std::vector<MidiEvent> MidiSequenceProfile::interpretReverb(
    const ReverbCommand& command,
    const MidiTrackState& state) const {
  return {Reverb{
      .tick = state.tick,
      .channel = state.channel,
      .value = static_cast<u8>(std::min<u32>(command.rawValue, 127)),
  }};
}

std::vector<MidiEvent> MidiSequenceProfile::interpretTuning(
    const TuningCommand& command,
    const MidiTrackState& state) const {
  return {FineTune{
      .tick = state.tick,
      .channel = state.channel,
      .cents = static_cast<double>(std::clamp<s32>(command.rawValue, -8192, 8191)),
  }};
}

std::vector<MidiEvent> MidiSequenceProfile::interpretPortamento(
    const PortamentoCommand& command,
    MidiTrackState& state) const {
  return {PortamentoTime{
      .tick = state.tick,
      .channel = state.channel,
      .value = static_cast<u8>(std::min<u32>(command.rawTime, 127)),
  }};
}

std::vector<MidiEvent> MidiSequenceProfile::interpretVibrato(
    const VibratoCommand& command,
    MidiTrackState& state) const {
  return {VibratoDepth{
      .tick = state.tick,
      .channel = state.channel,
      .value = static_cast<u8>(std::min<u32>(command.rawDepth, 127)),
  }};
}

std::vector<MidiEvent> MidiSequenceProfile::interpretTremolo(
    const TremoloCommand& command,
    MidiTrackState& state) const {
  return {TremoloDepth{
      .tick = state.tick,
      .channel = state.channel,
      .value = static_cast<u8>(std::min<u32>(command.rawDepth, 127)),
  }};
}

std::vector<MidiEvent> MidiSequenceProfile::interpretModulationRate(
    const ModulationRateCommand& command,
    MidiTrackState& state) const {
  const auto value = static_cast<u8>(std::min<u32>(command.rawRate, 127));
  return {
      VibratoFrequency{
          .tick = state.tick,
          .channel = state.channel,
          .value = value,
      },
      TremoloFrequency{
          .tick = state.tick,
          .channel = state.channel,
          .value = value,
      },
  };
}

std::vector<MidiEvent> MidiSequenceProfile::interpretEnvelope(
    const EnvelopeCommand&,
    const MidiTrackState&) const {
  return {};
}

std::vector<MidiEvent> MidiSequenceProfile::interpretDriverSpecific(
    const DriverSpecificCommand&,
    MidiTrackState&) const {
  return {};
}

std::vector<MidiEvent> MidiSequenceProfile::interpretRepeatBreak(
    const RepeatBreakCommand&,
    MidiTrackState&) const {
  return {};
}

void MidiSequenceProfileRegistry::add(std::string format, Factory factory) {
  if (format.empty()) {
    throw std::invalid_argument("Cannot register a MidiSequenceProfile with an empty format name");
  }
  if (!factory) {
    throw std::invalid_argument("Cannot register an empty MidiSequenceProfile factory");
  }
  factories_[std::move(format)] = std::move(factory);
}

std::unique_ptr<MidiSequenceProfile> MidiSequenceProfileRegistry::create(std::string_view format) const {
  const auto found = factories_.find(std::string(format));
  if (found == factories_.end()) {
    return nullptr;
  }
  return found->second();
}

bool MidiSequenceProfileRegistry::contains(std::string_view format) const {
  return factories_.contains(std::string(format));
}

}  // namespace vgmtrans::core
