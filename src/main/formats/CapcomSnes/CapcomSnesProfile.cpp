/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "formats/CapcomSnes/CapcomSnesProfile.h"

#include "formats/CapcomSnes/CapcomSnesDriverMath.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace vgmtrans::formats::capcom_snes {

using namespace core;

namespace {

constexpr int kPpqn = 48;
constexpr u8 kNoteOctaveMask = 0x07;
constexpr u8 kNoteOctaveUpMask = 0x08;
constexpr u8 kNoteDottedMask = 0x10;
constexpr u8 kNoteTripletMask = 0x20;
constexpr u8 kNoteSlurredMask = 0x40;

[[nodiscard]] u32 capcomLength(u32 rawDuration) {
  if (rawDuration == 0 || rawDuration > 7) {
    return 0;
  }
  return 192u >> (7u - rawDuration);
}

[[nodiscard]] u32 capcomLength(u32 rawDuration, TrackState& state) {
  u32 length = capcomLength(rawDuration);
  if (state.noteDotted) {
    if (length % 2 == 0 && length < 0x80) {
      length += length / 2;
    } else {
      length = 0;
    }
    state.noteDotted = false;
  } else if (state.noteTriplet) {
    length = length * 2 / 3;
  }
  return length;
}

void applyNoteAttributes(u8 attributes, TrackState& state) {
  state.noteOctave |= attributes & kNoteOctaveMask;
  state.noteDotted = state.noteDotted || ((attributes & kNoteDottedMask) != 0);
  state.noteOctaveUp = (attributes & kNoteOctaveUpMask) != 0;
  state.noteTriplet = (attributes & kNoteTripletMask) != 0;
  state.noteSlurred = (attributes & kNoteSlurredMask) != 0;
}

void addLfoDepthEvents(std::vector<PerformanceEvent>& events, const TrackState& state, bool enabled) {
  if (state.vibratoDepth != 0) {
    events.push_back(VibratoDepth{
        .tick = state.tick,
        .channel = state.channel,
        .value = static_cast<u8>(enabled ? state.vibratoDepth : 0),
    });
  }
  if (state.tremoloDepth != 0) {
    events.push_back(TremoloDepth{
        .tick = state.tick,
        .channel = state.channel,
        .value = static_cast<u8>(enabled ? state.tremoloDepth : 0),
    });
  }
}

}  // namespace

CapcomSnesProfile::CapcomSnesProfile(CapcomSnesEngineVersion version) : version_(version) {
}

u32 CapcomSnesProfile::restTicks(const RestCommand& command, TrackState& state) const {
  state.didRest = true;
  return capcomLength(command.rawDuration, state);
}

NoteTiming CapcomSnesProfile::noteTiming(const NoteCommand& command, TrackState& state) const {
  const u32 length = capcomLength(command.rawDuration, state);
  u32 duration = length * state.durationRate;
  if (state.noteSlurred || duration == 0) {
    duration = length << 8;
  }
  duration = (duration + 0x80) >> 8;
  if (duration == 0) {
    duration = 1;
  }

  const s32 sourceKey = static_cast<s32>(command.key) - 1 +
                        static_cast<s32>(state.noteOctave * 12) +
                        (state.noteOctaveUp ? 24 : 0);
  const s32 midiKey = std::clamp<s32>(sourceKey + state.globalTranspose + state.transpose, 0, 127);
  const bool extendsPrevious = state.lastNoteSlurred && sourceKey == state.lastKey && !state.didRest;
  std::vector<PerformanceEvent> beforeEvents;
  if (!extendsPrevious && state.portamentoMillisecondsPerCent > 0.0 && state.lastKey >= 0) {
    const auto keyDistance = static_cast<u32>(std::abs(sourceKey - state.lastKey));
    const auto portamentoTime =
        static_cast<u16>((keyDistance * 100) * state.portamentoMillisecondsPerCent);
    if (portamentoTime != state.lastPortamentoTime) {
      beforeEvents.push_back(PortamentoTime14{
          .tick = state.tick,
          .channel = state.channel,
          .value = portamentoTime,
      });
      state.lastPortamentoTime = portamentoTime;
    }
    beforeEvents.push_back(PortamentoControl{
        .tick = state.tick,
        .channel = state.channel,
        .key = static_cast<u8>(std::clamp<s32>(state.lastKey + state.globalTranspose, 0, 127)),
    });
  }
  if (!extendsPrevious) {
    state.lastKey = sourceKey;
    state.didRest = false;
  }
  state.lastNoteSlurred = state.noteSlurred;
  return NoteTiming{
      .key = static_cast<u8>(midiKey),
      .velocity = 127,
      .soundingTicks = duration + (!extendsPrevious && state.noteSlurred ? 1 : 0),
      .advanceTicks = length,
      .extendsPrevious = extendsPrevious,
      .beforeEvents = std::move(beforeEvents),
  };
}

void CapcomSnesProfile::applyDuration(const DurationCommand& command, TrackState& state) const {
  state.durationRate = command.rawValue;
}

std::vector<PerformanceEvent> CapcomSnesProfile::lowerTempo(
    const TempoCommand& command,
    const TrackState& state) const {
  const u32 microsecondsPerQuarter = command.rawValue == 0
                                         ? 60000000
                                         : static_cast<u32>(std::round(kPpqn * (125 * 0x40) * 2 * 256.0 /
                                                                       command.rawValue));
  return {Tempo{
      .tick = state.tick,
      .microsecondsPerQuarter = microsecondsPerQuarter,
  }};
}

std::vector<PerformanceEvent> CapcomSnesProfile::lowerVolume(
    const VolumeCommand& command,
    const TrackState& state) const {
  if (version_ == CapcomSnesEngineVersion::v1BgmInList) {
    return {Volume{
        .tick = state.tick,
        .channel = state.channel,
        .value = static_cast<u8>(std::min<u32>(command.rawValue >> 1, 127)),
    }};
  }

  return {Volume14{
      .tick = state.tick,
      .channel = state.channel,
      .value = ::capcom_snes::percentAmpTo14BitMidi(
          ::capcom_snes::calculateVolumeV2(static_cast<u8>(command.rawValue))),
  }};
}

std::vector<PerformanceEvent> CapcomSnesProfile::lowerProgram(
    const ProgramCommand& command,
    const TrackState& state) const {
  return {
      BankSelect{
          .tick = state.tick,
          .channel = state.channel,
          .bank = static_cast<u16>(command.rawProgram >> 7),
          .writeLsb = false,
      },
      ProgramChange{
          .tick = state.tick,
          .channel = state.channel,
          .program = static_cast<u8>(command.rawProgram & 0x7f),
      },
  };
}

std::vector<PerformanceEvent> CapcomSnesProfile::lowerPan(
    const PanCommand& command,
    const TrackState& state) const {
  const auto biasedPan = static_cast<u8>(command.rawValue + 0x80);
  ::capcom_snes::PanConversionResult pan;
  if (version_ == CapcomSnesEngineVersion::v1BgmInList) {
    pan = ::capcom_snes::linear8BitPanToMidi(biasedPan);
  } else {
    pan = ::capcom_snes::calculatePanV2(biasedPan);
  }

  return {
      Pan{
          .tick = state.tick,
          .channel = state.channel,
          .value = pan.midiPan,
      },
      Expression{
          .tick = state.tick,
          .channel = state.channel,
          .value = ::capcom_snes::percentAmpTo7BitMidi(pan.volumeScale),
      },
  };
}

std::vector<PerformanceEvent> CapcomSnesProfile::lowerMasterVolume(
    const MasterVolumeCommand& command,
    const TrackState& state) const {
  if (version_ == CapcomSnesEngineVersion::v1BgmInList) {
    return {MasterVolume{
        .tick = state.tick,
        .value = static_cast<u16>(std::min<u32>((command.rawValue >> 1) * 129, 0x3fff)),
    }};
  }

  return {MasterVolume{
      .tick = state.tick,
      .value = ::capcom_snes::percentAmpTo14BitMidi(
          ::capcom_snes::calculateVolumeV2(static_cast<u8>(command.rawValue))),
  }};
}

std::vector<PerformanceEvent> CapcomSnesProfile::lowerReverb(
    const ReverbCommand& command,
    const TrackState& state) const {
  return {Reverb{
      .tick = state.tick,
      .channel = state.channel,
      .value = static_cast<u8>((command.rawValue & 1) != 0 ? 40 : 0),
  }};
}

std::vector<PerformanceEvent> CapcomSnesProfile::lowerTuning(
    const TuningCommand& command,
    const TrackState& state) const {
  return {FineTune{
      .tick = state.tick,
      .channel = state.channel,
      .cents = command.rawValue * 100.0 / 256.0,
  }};
}

std::vector<PerformanceEvent> CapcomSnesProfile::lowerPortamento(
    const PortamentoCommand& command,
    TrackState& state) const {
  const u8 step = static_cast<u8>((command.rawTime << 1) & 0xff);
  const double centsPerUpdate = step * (100.0 / 256.0);
  state.portamentoMillisecondsPerCent = centsPerUpdate == 0.0 ? 0.0 : (0.016 / centsPerUpdate) * 1000.0;
  return {};
}

std::vector<PerformanceEvent> CapcomSnesProfile::lowerLfo(
    const LfoCommand& command,
    TrackState& state) const {
  switch (command.rawType) {
    case 0:
      state.vibratoDepth = static_cast<u8>(command.rawAmount & 0x7f);
      return {VibratoDepth{
          .tick = state.tick,
          .channel = state.channel,
          .value = static_cast<u8>(state.lfoRate != 0 ? state.vibratoDepth : 0),
      }};
    case 1:
      state.tremoloDepth = ::capcom_snes::tremoloDepthToMidiValue(
          static_cast<int>(command.rawAmount), version_ == CapcomSnesEngineVersion::v1BgmInList);
      return {TremoloDepth{
          .tick = state.tick,
          .channel = state.channel,
          .value = static_cast<u8>(state.lfoRate != 0 ? state.tremoloDepth : 0),
      }};
    case 2: {
      std::vector<PerformanceEvent> events;
      const bool wasEnabled = state.lfoRate != 0;
      state.lfoRate = command.rawAmount;
      const bool isEnabled = state.lfoRate != 0;
      if (!isEnabled && wasEnabled) {
        addLfoDepthEvents(events, state, false);
      } else if (isEnabled && !wasEnabled) {
        addLfoDepthEvents(events, state, true);
      }

      const u8 rate = ::capcom_snes::lfoRateByteToMidiValue(static_cast<u8>(command.rawAmount));
      events.push_back(VibratoFrequency{
          .tick = state.tick,
          .channel = state.channel,
          .value = rate,
      });
      events.push_back(TremoloFrequency{
          .tick = state.tick,
          .channel = state.channel,
          .value = rate,
      });
      return events;
    }
    default:
      return {};
  }
}

std::vector<PerformanceEvent> CapcomSnesProfile::lowerDriverSpecific(
    const DriverSpecificCommand& command,
    TrackState& state) const {
  std::vector<PerformanceEvent> events;
  auto setSlur = [&](bool enabled) {
    if (state.noteSlurred != enabled) {
      state.noteSlurred = enabled;
      events.push_back(LegatoPedal{
          .tick = state.tick,
          .channel = state.channel,
          .enabled = enabled,
      });
    }
  };

  if (command.name == "Toggle Triplet") {
    state.noteTriplet = !state.noteTriplet;
  } else if (command.name == "Toggle Slur") {
    setSlur(!state.noteSlurred);
  } else if (command.name == "Dotted Note On") {
    state.noteDotted = true;
  } else if (command.name == "Toggle 2-Octave Up") {
    state.noteOctaveUp = !state.noteOctaveUp;
  } else if (command.name == "Note Attributes" && command.bytes.size() >= 2) {
    const bool wasSlurred = state.noteSlurred;
    applyNoteAttributes(command.bytes[1], state);
    if (state.noteSlurred != wasSlurred) {
      events.push_back(LegatoPedal{
          .tick = state.tick,
          .channel = state.channel,
          .enabled = state.noteSlurred,
      });
    }
  } else if (command.name == "Octave" && command.bytes.size() >= 2) {
    state.noteOctave = command.bytes[1] & kNoteOctaveMask;
  }

  return events;
}

std::vector<PerformanceEvent> CapcomSnesProfile::lowerRepeatBreak(
    const RepeatBreakCommand& command,
    TrackState& state) const {
  std::vector<PerformanceEvent> events;
  const bool wasSlurred = state.noteSlurred;
  applyNoteAttributes(command.rawAttributes, state);
  if (state.noteSlurred != wasSlurred) {
    events.push_back(LegatoPedal{
        .tick = state.tick,
        .channel = state.channel,
        .enabled = state.noteSlurred,
    });
  }
  return events;
}

void registerCapcomSnesProfile(SequencerProfileRegistry& registry) {
  registry.add("CapcomSnes", [] {
    return std::make_unique<CapcomSnesProfile>(CapcomSnesEngineVersion::v3BgmFixedLocation);
  });
}

}  // namespace vgmtrans::formats::capcom_snes
