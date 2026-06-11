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

namespace {

[[nodiscard]] bool profileComplete(const MidiSequenceProfile& profile) {
  return profile.beginTrack != nullptr && profile.restTicks != nullptr && profile.noteTiming != nullptr &&
         profile.interpretNoteState != nullptr && profile.applyDuration != nullptr &&
         profile.applyTranspose != nullptr && profile.interpretTempo != nullptr &&
         profile.interpretProgram != nullptr && profile.interpretVolume != nullptr && profile.interpretPan != nullptr &&
         profile.interpretMasterVolume != nullptr && profile.interpretReverb != nullptr &&
         profile.interpretTuning != nullptr && profile.interpretPortamento != nullptr &&
         profile.interpretVibrato != nullptr && profile.interpretTremolo != nullptr &&
         profile.interpretModulationRate != nullptr && profile.interpretEnvelope != nullptr &&
         profile.interpretDriverSpecific != nullptr && profile.interpretRepeatBreak != nullptr;
}

}  // namespace

void defaultBeginTrack(const CommandSequence&, const CommandTrack&, MidiTrackState&, std::vector<MidiEvent>&) {
}

u32 defaultRestTicks(const RestCommand& command, MidiTrackState&) {
  return command.rawDuration;
}

std::vector<MidiEvent> defaultInterpretNoteState(const NoteStateCommand&, MidiTrackState&) {
  return {};
}

MidiNoteTiming defaultNoteTiming(const NoteCommand& command, MidiTrackState& state) {
  const auto key = std::clamp<s32>(static_cast<s32>(command.key) + state.transpose + state.globalTranspose, 0, 127);
  const auto ticks = command.rawDuration;
  return MidiNoteTiming{
      .key = static_cast<u8>(key),
      .velocity =
          command.rawVelocity == 0 ? static_cast<u8>(127) : static_cast<u8>(std::min<u32>(command.rawVelocity, 127)),
      .soundingTicks = ticks,
      .advanceTicks = ticks,
  };
}

void defaultApplyDuration(const DurationCommand& command, MidiTrackState& state) {
  state.durationRate = command.rawValue;
}

void defaultApplyTranspose(const TransposeCommand& command, MidiTrackState& state) {
  state.transpose = command.rawSemitones;
}

std::vector<MidiEvent> defaultInterpretTempo(const TempoCommand& command, const MidiTrackState& state) {
  return {Tempo{
      .tick = state.tick,
      .microsecondsPerQuarter = command.rawValue == 0 ? 500000 : command.rawValue,
  }};
}

std::vector<MidiEvent> defaultInterpretProgram(const ProgramCommand& command, const MidiTrackState& state) {
  return {ProgramChange{
      .tick = state.tick,
      .channel = state.channel,
      .program = static_cast<u8>(std::min<u32>(command.rawProgram, 127)),
  }};
}

std::vector<MidiEvent> defaultInterpretVolume(const VolumeCommand& command, const MidiTrackState& state) {
  return {Volume{
      .tick = state.tick,
      .channel = state.channel,
      .value = static_cast<u8>(std::min<u32>(command.rawValue, 127)),
  }};
}

std::vector<MidiEvent> defaultInterpretPan(const PanCommand& command, const MidiTrackState& state) {
  return {Pan{
      .tick = state.tick,
      .channel = state.channel,
      .value = static_cast<u8>(std::min<u32>(command.rawValue, 127)),
  }};
}

std::vector<MidiEvent> defaultInterpretMasterVolume(const MasterVolumeCommand& command, const MidiTrackState& state) {
  return {MasterVolume{
      .tick = state.tick,
      .value = static_cast<u16>(std::min<u32>(command.rawValue, 0x3fff)),
  }};
}

std::vector<MidiEvent> defaultInterpretReverb(const ReverbCommand& command, const MidiTrackState& state) {
  return {Reverb{
      .tick = state.tick,
      .channel = state.channel,
      .value = static_cast<u8>(std::min<u32>(command.rawValue, 127)),
  }};
}

std::vector<MidiEvent> defaultInterpretTuning(const TuningCommand& command, const MidiTrackState& state) {
  return {FineTune{
      .tick = state.tick,
      .channel = state.channel,
      .cents = static_cast<double>(std::clamp<s32>(command.rawValue, -8192, 8191)),
  }};
}

std::vector<MidiEvent> defaultInterpretPortamento(const PortamentoCommand& command, MidiTrackState& state) {
  return {PortamentoTime{
      .tick = state.tick,
      .channel = state.channel,
      .value = static_cast<u8>(std::min<u32>(command.rawTime, 127)),
  }};
}

std::vector<MidiEvent> defaultInterpretVibrato(const VibratoCommand& command, MidiTrackState& state) {
  return {VibratoDepth{
      .tick = state.tick,
      .channel = state.channel,
      .value = static_cast<u8>(std::min<u32>(command.rawDepth, 127)),
  }};
}

std::vector<MidiEvent> defaultInterpretTremolo(const TremoloCommand& command, MidiTrackState& state) {
  return {TremoloDepth{
      .tick = state.tick,
      .channel = state.channel,
      .value = static_cast<u8>(std::min<u32>(command.rawDepth, 127)),
  }};
}

std::vector<MidiEvent> defaultInterpretModulationRate(const ModulationRateCommand& command, MidiTrackState& state) {
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

std::vector<MidiEvent> defaultInterpretEnvelope(const EnvelopeCommand&, const MidiTrackState&) {
  return {};
}

std::vector<MidiEvent> defaultInterpretDriverSpecific(const DriverSpecificCommand&, MidiTrackState&) {
  return {};
}

std::vector<MidiEvent> defaultInterpretRepeatBreak(const RepeatBreakCommand&, MidiTrackState&) {
  return {};
}

void MidiSequenceProfileRegistry::add(std::string format, MidiSequenceProfile profile) {
  if (format.empty()) {
    throw std::invalid_argument("Cannot register a MidiSequenceProfile with an empty format name");
  }
  if (!profileComplete(profile)) {
    throw std::invalid_argument("Cannot register an incomplete MidiSequenceProfile");
  }
  profiles_[std::move(format)] = profile;
}

const MidiSequenceProfile* MidiSequenceProfileRegistry::find(std::string_view format) const {
  const auto found = profiles_.find(std::string(format));
  if (found == profiles_.end()) {
    return nullptr;
  }
  return &found->second;
}

bool MidiSequenceProfileRegistry::contains(std::string_view format) const {
  return profiles_.contains(std::string(format));
}

}  // namespace vgmtrans::core
