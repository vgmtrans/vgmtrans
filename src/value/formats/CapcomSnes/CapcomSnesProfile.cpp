/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/CapcomSnes/CapcomSnesProfile.h"

#include "formats/CapcomSnes/CapcomSnesDriverMath.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <string_view>

namespace vgmtrans::formats::capcom_snes {

using namespace core;

namespace {

constexpr int kPpqn = 48;
constexpr u8 kNoteOctaveMask = 0x07;
constexpr u8 kNoteOctaveUpMask = 0x08;
constexpr u8 kNoteDottedMask = 0x10;
constexpr u8 kNoteTripletMask = 0x20;
constexpr u8 kNoteSlurredMask = 0x40;

constexpr std::string_view kDefaultProfileName = "CapcomSnes";
constexpr std::string_view kV1ProfileName = "CapcomSnes:v1";
constexpr std::string_view kV2ProfileName = "CapcomSnes:v2";
constexpr std::string_view kV3ProfileName = "CapcomSnes:v3";

struct PanControllerValues {
  u8 pan = 64;
  u8 expression = 127;
};

[[nodiscard]] u32 baseNoteTicks(u32 rawDuration) {
  if (rawDuration == 0 || rawDuration > 7) {
    return 0;
  }
  return 192u >> (7u - rawDuration);
}

[[nodiscard]] u32 noteTicks(u32 rawDuration, MidiTrackState& state) {
  u32 length = baseNoteTicks(rawDuration);
  if (state.noteDotted) {
    // Dotted applies once, while triplet mode persists until toggled by command.
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

[[nodiscard]] u32 soundingTicks(u32 length, const MidiTrackState& state) {
  u32 duration = length * state.durationRate;
  if (state.noteSlurred || duration == 0) {
    duration = length << 8;
  }
  duration = (duration + 0x80) >> 8;
  return duration == 0 ? 1 : duration;
}

[[nodiscard]] s32 sourceKey(const NoteCommand& command, const MidiTrackState& state) {
  // Keep source pitch separate so portamento distance ignores active transpose like the driver.
  return static_cast<s32>(command.key) - 1 +
         static_cast<s32>(state.noteOctave * 12) +
         (state.noteOctaveUp ? 24 : 0);
}

[[nodiscard]] u8 midiKey(s32 key, const MidiTrackState& state) {
  return static_cast<u8>(std::clamp<s32>(key + state.globalTranspose + state.transpose, 0, 127));
}

void applyNoteAttributes(u8 attributes, MidiTrackState& state) {
  state.noteOctave |= attributes & kNoteOctaveMask;
  state.noteDotted = state.noteDotted || ((attributes & kNoteDottedMask) != 0);
  state.noteOctaveUp = (attributes & kNoteOctaveUpMask) != 0;
  state.noteTriplet = (attributes & kNoteTripletMask) != 0;
  state.noteSlurred = (attributes & kNoteSlurredMask) != 0;
}

void addModulationDepthEvents(std::vector<MidiEvent>& events, const MidiTrackState& state, bool enabled) {
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

[[nodiscard]] u32 tempoMicrosecondsPerQuarter(u32 rawTempo) {
  if (rawTempo == 0) {
    return 60000000;
  }
  // Capcom's timer math is driver-rate based, not a direct BPM value.
  return static_cast<u32>(std::round(kPpqn * (125 * 0x40) * 2 * 256.0 / rawTempo));
}

[[nodiscard]] u16 volume14(CapcomSnesEngineVersion version, u8 rawValue) {
  // Convert through the amplitude curve before quantizing to MIDI resolution.
  const double volume = version == CapcomSnesEngineVersion::v1BgmInList
                            ? ::capcom_snes::calculateVolumeV1(rawValue)
                            : ::capcom_snes::calculateVolumeV2(rawValue);
  return ::capcom_snes::percentAmpTo14BitMidi(volume);
}

[[nodiscard]] PanControllerValues panControllerValues(CapcomSnesEngineVersion version, u32 rawValue) {
  const auto biasedPan = static_cast<u8>(rawValue + 0x80);
  const auto pan = version == CapcomSnesEngineVersion::v1BgmInList
                       ? ::capcom_snes::linear8BitPanToMidi(biasedPan)
                       : ::capcom_snes::calculatePanV2(biasedPan);
  return PanControllerValues{
      .pan = pan.midiPan,
      .expression = ::capcom_snes::percentAmpTo7BitMidi(pan.volumeScale),
  };
}

[[nodiscard]] double tuningCents(s32 rawValue) {
  return rawValue * 100.0 / 256.0;
}

[[nodiscard]] double portamentoMillisecondsPerCent(u32 rawTime) {
  const u8 step = static_cast<u8>((rawTime << 1) & 0xff);
  const double centsPerUpdate = step * (100.0 / 256.0);
  return centsPerUpdate == 0.0 ? 0.0 : (0.016 / centsPerUpdate) * 1000.0;
}

}  // namespace

std::string_view capcomSnesProfileName(CapcomSnesEngineVersion version) {
  // Export lookup uses these keys to preserve version-specific pan/volume math.
  switch (version) {
    case CapcomSnesEngineVersion::v1BgmInList:
      return kV1ProfileName;
    case CapcomSnesEngineVersion::v2BgmUsuallyAtFixedLocation:
      return kV2ProfileName;
    case CapcomSnesEngineVersion::v3BgmFixedLocation:
      return kV3ProfileName;
    case CapcomSnesEngineVersion::none:
      return kDefaultProfileName;
  }
  return kDefaultProfileName;
}

CapcomSnesProfile::CapcomSnesProfile(CapcomSnesEngineVersion version) : version_(version) {
}

u32 CapcomSnesProfile::restTicks(const RestCommand& command, MidiTrackState& state) const {
  state.didRest = true;
  return noteTicks(command.rawDuration, state);
}

MidiNoteTiming CapcomSnesProfile::noteTiming(const NoteCommand& command, MidiTrackState& state) const {
  const u32 length = noteTicks(command.rawDuration, state);
  const u32 duration = soundingTicks(length, state);
  const s32 key = sourceKey(command, state);
  const bool extendsPrevious = state.lastNoteSlurred && key == state.lastKey && !state.didRest;
  std::vector<MidiEvent> beforeEvents;
  if (!extendsPrevious && state.portamentoMillisecondsPerCent > 0.0 && state.lastKey >= 0) {
    const auto keyDistance = static_cast<u32>(std::abs(key - state.lastKey));
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
    state.lastKey = key;
    state.didRest = false;
  }
  state.lastNoteSlurred = state.noteSlurred;
  return MidiNoteTiming{
      .key = midiKey(key, state),
      .velocity = 127,
      .soundingTicks = duration + (!extendsPrevious && state.noteSlurred ? 1 : 0),
      .advanceTicks = length,
      .extendsPrevious = extendsPrevious,
      .beforeEvents = std::move(beforeEvents),
  };
}

std::vector<MidiEvent> CapcomSnesProfile::interpretNoteState(
    const NoteStateCommand& command,
    MidiTrackState& state) const {
  std::vector<MidiEvent> events;
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

  switch (command.action) {
    case NoteStateAction::ToggleTriplet:
      state.noteTriplet = !state.noteTriplet;
      break;
    case NoteStateAction::ToggleSlur:
      setSlur(!state.noteSlurred);
      break;
    case NoteStateAction::EnableDotted:
      state.noteDotted = true;
      break;
    case NoteStateAction::ToggleOctaveUp:
      state.noteOctaveUp = !state.noteOctaveUp;
      break;
    case NoteStateAction::Attributes: {
      const bool wasSlurred = state.noteSlurred;
      applyNoteAttributes(static_cast<u8>(command.rawValue), state);
      if (state.noteSlurred != wasSlurred) {
        events.push_back(LegatoPedal{
            .tick = state.tick,
            .channel = state.channel,
            .enabled = state.noteSlurred,
        });
      }
      break;
    }
    case NoteStateAction::Octave:
      state.noteOctave = command.rawValue & kNoteOctaveMask;
      break;
  }

  return events;
}

void CapcomSnesProfile::applyDuration(const DurationCommand& command, MidiTrackState& state) const {
  state.durationRate = command.rawValue;
}

std::vector<MidiEvent> CapcomSnesProfile::interpretTempo(
    const TempoCommand& command,
    const MidiTrackState& state) const {
  return {Tempo{
      .tick = state.tick,
      .microsecondsPerQuarter = tempoMicrosecondsPerQuarter(command.rawValue),
  }};
}

std::vector<MidiEvent> CapcomSnesProfile::interpretVolume(
    const VolumeCommand& command,
    const MidiTrackState& state) const {
  return {Volume14{
      .tick = state.tick,
      .channel = state.channel,
      .value = volume14(version_, static_cast<u8>(command.rawValue)),
  }};
}

std::vector<MidiEvent> CapcomSnesProfile::interpretProgram(
    const ProgramCommand& command,
    const MidiTrackState& state) const {
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

std::vector<MidiEvent> CapcomSnesProfile::interpretPan(
    const PanCommand& command,
    const MidiTrackState& state) const {
  const auto panValues = panControllerValues(version_, command.rawValue);

  return {
      Pan{
          .tick = state.tick,
          .channel = state.channel,
          .value = panValues.pan,
      },
      Expression{
          .tick = state.tick,
          .channel = state.channel,
          .value = panValues.expression,
      },
  };
}

std::vector<MidiEvent> CapcomSnesProfile::interpretMasterVolume(
    const MasterVolumeCommand& command,
    const MidiTrackState& state) const {
  return {MasterVolume{
      .tick = state.tick,
      .value = volume14(version_, static_cast<u8>(command.rawValue)),
  }};
}

std::vector<MidiEvent> CapcomSnesProfile::interpretReverb(
    const ReverbCommand& command,
    const MidiTrackState& state) const {
  return {Reverb{
      .tick = state.tick,
      .channel = state.channel,
      .value = static_cast<u8>((command.rawValue & 1) != 0 ? 40 : 0),
  }};
}

std::vector<MidiEvent> CapcomSnesProfile::interpretTuning(
    const TuningCommand& command,
    const MidiTrackState& state) const {
  return {FineTune{
      .tick = state.tick,
      .channel = state.channel,
      .cents = tuningCents(command.rawValue),
  }};
}

std::vector<MidiEvent> CapcomSnesProfile::interpretPortamento(
    const PortamentoCommand& command,
    MidiTrackState& state) const {
  state.portamentoMillisecondsPerCent = portamentoMillisecondsPerCent(command.rawTime);
  return {};
}

std::vector<MidiEvent> CapcomSnesProfile::interpretVibrato(
    const VibratoCommand& command,
    MidiTrackState& state) const {
  state.vibratoDepth = static_cast<u8>(command.rawDepth & 0x7f);
  return {VibratoDepth{
      .tick = state.tick,
      .channel = state.channel,
      .value = static_cast<u8>(state.modulationRate != 0 ? state.vibratoDepth : 0),
  }};
}

std::vector<MidiEvent> CapcomSnesProfile::interpretTremolo(
    const TremoloCommand& command,
    MidiTrackState& state) const {
  state.tremoloDepth = ::capcom_snes::tremoloDepthToMidiValue(
      static_cast<int>(command.rawDepth), version_ == CapcomSnesEngineVersion::v1BgmInList);
  return {TremoloDepth{
      .tick = state.tick,
      .channel = state.channel,
      .value = static_cast<u8>(state.modulationRate != 0 ? state.tremoloDepth : 0),
  }};
}

std::vector<MidiEvent> CapcomSnesProfile::interpretModulationRate(
    const ModulationRateCommand& command,
    MidiTrackState& state) const {
  std::vector<MidiEvent> events;
  const bool wasEnabled = state.modulationRate != 0;
  state.modulationRate = static_cast<u8>(command.rawRate);
  const bool isEnabled = state.modulationRate != 0;
  // Depth commands latch silently until a nonzero modulation rate enables output.
  if (!isEnabled && wasEnabled) {
    addModulationDepthEvents(events, state, false);
  } else if (isEnabled && !wasEnabled) {
    addModulationDepthEvents(events, state, true);
  }

  const u8 rate = ::capcom_snes::lfoRateByteToMidiValue(static_cast<u8>(command.rawRate));
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

std::vector<MidiEvent> CapcomSnesProfile::interpretRepeatBreak(
    const RepeatBreakCommand& command,
    MidiTrackState& state) const {
  std::vector<MidiEvent> events;
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

void registerCapcomSnesProfile(MidiSequenceProfileRegistry& registry) {
  registry.add(std::string(kDefaultProfileName), [] {
    return std::make_unique<CapcomSnesProfile>(CapcomSnesEngineVersion::v3BgmFixedLocation);
  });
  registry.add(std::string(kV1ProfileName), [] {
    return std::make_unique<CapcomSnesProfile>(CapcomSnesEngineVersion::v1BgmInList);
  });
  registry.add(std::string(kV2ProfileName), [] {
    return std::make_unique<CapcomSnesProfile>(CapcomSnesEngineVersion::v2BgmUsuallyAtFixedLocation);
  });
  registry.add(std::string(kV3ProfileName), [] {
    return std::make_unique<CapcomSnesProfile>(CapcomSnesEngineVersion::v3BgmFixedLocation);
  });
}

}  // namespace vgmtrans::formats::capcom_snes
