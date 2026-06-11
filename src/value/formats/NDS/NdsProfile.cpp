/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/NDS/NdsProfile.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>

namespace vgmtrans::formats::nds {

using namespace core;

namespace {

constexpr auto kPitchBend = "nds-pitch-bend";
constexpr auto kPitchBendRange = "nds-pitch-bend-range";
constexpr auto kNotewait = "nds-notewait";
constexpr auto kPortamentoEnable = "nds-portamento-enable";
constexpr auto kExpression = "nds-expression";

[[nodiscard]] u8 byteAt(const DriverSpecificCommand& command, size_t index, u8 fallback = 0) {
  return index < command.bytes.size() ? command.bytes[index] : fallback;
}

}  // namespace

void NdsProfile::beginTrack(const CommandSequence&, const CommandTrack&, MidiTrackState& state,
                            std::vector<MidiEvent>&) const {
  state.noteWait = false;
}

MidiNoteTiming NdsProfile::noteTiming(const NoteCommand& command, MidiTrackState& state) const {
  const auto key = std::clamp<s32>(static_cast<s32>(command.key) + state.transpose + state.globalTranspose, 0, 127);
  return MidiNoteTiming{
      .key = static_cast<u8>(key),
      .velocity = static_cast<u8>(command.rawVelocity),
      .soundingTicks = command.rawDuration,
      .advanceTicks = state.noteWait ? command.rawDuration : 0,
  };
}

std::vector<MidiEvent> NdsProfile::interpretTempo(
    const TempoCommand& command,
    const MidiTrackState& state) const {
  if (command.rawValue == 0) {
    return {};
  }

  return {Tempo{
      .tick = state.tick,
      .microsecondsPerQuarter = static_cast<u32>(std::round(60000000.0 / command.rawValue)),
  }};
}

std::vector<MidiEvent> NdsProfile::interpretDriverSpecific(
    const DriverSpecificCommand& command,
    MidiTrackState& state) const {
  if (command.name == kPitchBend) {
    return {PitchBend{
        .tick = state.tick,
        .channel = state.channel,
        .value = static_cast<s16>(static_cast<s8>(byteAt(command, 0)) * 64),
    }};
  }
  if (command.name == kPitchBendRange) {
    return {PitchBendRange{
        .tick = state.tick,
        .channel = state.channel,
        .semitones = byteAt(command, 0),
    }};
  }
  if (command.name == kNotewait) {
    state.noteWait = byteAt(command, 0) != 0;
    return {};
  }
  if (command.name == kPortamentoEnable) {
    return {PortamentoEnable{
        .tick = state.tick,
        .channel = state.channel,
        .enabled = byteAt(command, 0) != 0,
    }};
  }
  if (command.name == kExpression) {
    return {Expression{
        .tick = state.tick,
        .channel = state.channel,
        .value = static_cast<u8>(std::min<u32>(byteAt(command, 0), 127)),
    }};
  }

  return {};
}

void registerNdsProfile(MidiSequenceProfileRegistry& registry) {
  registry.add(kNdsProfileName, [] { return std::make_unique<NdsProfile>(); });
}

}  // namespace vgmtrans::formats::nds
