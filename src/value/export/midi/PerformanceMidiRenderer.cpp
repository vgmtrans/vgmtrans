/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/export/midi/PerformanceMidiRenderer.h"

#include "value/base/LevelScale.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

namespace vgmtrans::core {

namespace {

[[nodiscard]] u8 data7(double value) {
  return static_cast<u8>(std::clamp<int>(static_cast<int>(std::lround(value)), 0, 127));
}

[[nodiscard]] u16 data14(double value) {
  return static_cast<u16>(std::clamp<int>(static_cast<int>(std::lround(value)), 0, 16383));
}

[[nodiscard]] u8 midiKey(double key) {
  return data7(key);
}

[[nodiscard]] u8 midiVelocity(double linearVelocity) {
  return LevelScale::midi7FromLinear(linearVelocity);
}

struct MidiChannelAssignment {
  size_t port = 0;
  u8 channel = 0;
};

[[nodiscard]] MidiChannelAssignment midiChannelAssignment(size_t trackIndex, const MidiExportOptions& options) {
  constexpr size_t channelsPerPort = 16;
  constexpr size_t skippedDrumChannel = 9;
  if (options.skipChannel10) {
    constexpr size_t usableChannelsPerPort = channelsPerPort - 1;
    const size_t port = trackIndex / usableChannelsPerPort;
    const size_t slot = trackIndex % usableChannelsPerPort;
    return MidiChannelAssignment{
        .port = port,
        .channel = static_cast<u8>(slot < skippedDrumChannel ? slot : slot + 1),
    };
  }

  return MidiChannelAssignment{
      .port = trackIndex / channelsPerPort,
      .channel = static_cast<u8>(trackIndex % channelsPerPort),
  };
}

[[nodiscard]] u8 midiPortByte(size_t port) {
  return static_cast<u8>(std::min<size_t>(port, 255));
}

[[nodiscard]] u8 midiPan(double stereoPosition) {
  return data7(((std::clamp(stereoPosition, -1.0, 1.0) + 1.0) / 2.0) * 127.0);
}

[[nodiscard]] u8 midiNormalized7(double amount) {
  return data7(std::clamp(amount, 0.0, 1.0) * 127.0);
}

[[nodiscard]] s16 midiPitchBend(double semitones, u8 rangeSemitones) {
  if (rangeSemitones == 0) {
    return 0;
  }

  const double normalized = semitones / static_cast<double>(rangeSemitones);
  return static_cast<s16>(std::clamp<int>(static_cast<int>(std::lround(normalized * 8192.0)), -8192, 8191));
}

[[nodiscard]] MidiLevelResolution resolveLevelResolution(MidiLevelResolution requested, LevelPrecisionHint hint) {
  if (requested != MidiLevelResolution::Auto) {
    return requested;
  }
  return hint == LevelPrecisionHint::FourteenBit ? MidiLevelResolution::FourteenBit : MidiLevelResolution::SevenBit;
}

[[nodiscard]] bool writeBankSelectLsb(const MidiExportOptions& options) {
  return options.bankSelectStyle == MidiBankSelectStyle::MsbAndLsb;
}

void addExpression(MidiTrack& track, u64 tick, u8 channel, double linearGain, LevelPrecisionHint precisionHint,
                   const MidiExportOptions& options) {
  if (resolveLevelResolution(options.expressionResolution, precisionHint) == MidiLevelResolution::FourteenBit) {
    track.events.push_back(Expression14{
        .tick = tick,
        .channel = channel,
        .value = LevelScale::midi14FromLinear(linearGain),
    });
  } else {
    track.events.push_back(Expression{
        .tick = tick,
        .channel = channel,
        .value = LevelScale::midi7FromLinear(linearGain),
    });
  }
}

struct RenderTrackState {
  std::optional<size_t> lastNoteIndex;
  u8 pitchBendRangeSemitones = 2;
};

struct GlobalTransposeChange {
  u64 tick = 0;
  s32 semitones = 0;
  size_t sequence = 0;
};

[[nodiscard]] std::vector<GlobalTransposeChange> collectGlobalTransposeChanges(const PerformanceSequence& performance) {
  std::vector<GlobalTransposeChange> changes;
  for (const auto& track : performance.tracks) {
    for (const auto& event : track.events) {
      const auto* transpose = std::get_if<GlobalTransposePerformanceEvent>(&event);
      if (transpose == nullptr) {
        continue;
      }
      changes.push_back(GlobalTransposeChange{
          .tick = transpose->header.tick,
          .semitones = transpose->semitones,
          .sequence = changes.size(),
      });
    }
  }
  std::ranges::stable_sort(changes, [](const GlobalTransposeChange& lhs, const GlobalTransposeChange& rhs) {
    return std::tie(lhs.tick, lhs.sequence) < std::tie(rhs.tick, rhs.sequence);
  });
  return changes;
}

[[nodiscard]] s32 globalTransposeAt(std::span<const GlobalTransposeChange> changes, u64 tick) {
  s32 semitones = 0;
  for (const auto& change : changes) {
    if (change.tick > tick) {
      break;
    }
    semitones = change.semitones;
  }
  return semitones;
}

[[nodiscard]] std::vector<TimeSignature> collectGlobalTimeSignatures(const PerformanceSequence& performance) {
  std::vector<TimeSignature> timeSignatures;
  for (const auto& track : performance.tracks) {
    for (const auto& event : track.events) {
      const auto* timeSignature = std::get_if<TimeSignaturePerformanceEvent>(&event);
      if (timeSignature == nullptr) {
        continue;
      }
      timeSignatures.push_back(TimeSignature{
          .tick = timeSignature->header.tick,
          .numerator = timeSignature->numerator,
          .denominator = timeSignature->denominator,
          .clocksPerMetronomeClick = timeSignature->clocksPerMetronomeClick,
      });
    }
  }
  std::ranges::stable_sort(timeSignatures, [](const TimeSignature& lhs, const TimeSignature& rhs) {
    return lhs.tick < rhs.tick;
  });
  return timeSignatures;
}

bool extendPreviousNote(MidiTrack& track, RenderTrackState& state, const NotePerformanceEvent& note, u8 channel) {
  if (!note.extendsPrevious || !state.lastNoteIndex || *state.lastNoteIndex >= track.events.size()) {
    return false;
  }

  auto* previous = std::get_if<NoteDuration>(&track.events[*state.lastNoteIndex]);
  if (previous == nullptr || previous->channel != channel) {
    return false;
  }

  const u64 previousEnd = previous->tick + previous->duration;
  const u64 extensionEnd = note.header.tick + note.durationTicks;
  if (extensionEnd > previousEnd) {
    previous->duration = static_cast<u32>(extensionEnd - previous->tick);
  }
  return true;
}

void addMidiEvent(MidiTrack& track, RenderTrackState& state, const PerformanceEvent& event, u8 channel,
                  std::span<const GlobalTransposeChange> globalTransposes, const MidiExportOptions& options) {
  std::visit(
      [&](const auto& typedEvent) {
        using TypedEvent = std::decay_t<decltype(typedEvent)>;
        if constexpr (std::is_same_v<TypedEvent, NotePerformanceEvent>) {
          const u8 key = midiKey(typedEvent.key + globalTransposeAt(globalTransposes, typedEvent.header.tick));
          if (extendPreviousNote(track, state, typedEvent, channel)) {
            return;
          }
          state.lastNoteIndex = track.events.size();
          track.events.push_back(NoteDuration{
              .tick = typedEvent.header.tick,
              .channel = channel,
              .key = key,
              .velocity = midiVelocity(typedEvent.linearVelocity),
              .duration = typedEvent.durationTicks,
          });
        } else if constexpr (std::is_same_v<TypedEvent, TempoPerformanceEvent>) {
          track.events.push_back(Tempo{
              .tick = typedEvent.header.tick,
              .microsecondsPerQuarter = typedEvent.microsecondsPerQuarter,
          });
        } else if constexpr (std::is_same_v<TypedEvent, TimeSignaturePerformanceEvent>) {
          // Standard MIDI treats time signatures as global metadata. They are collected
          // once and written to the first MIDI track by PerformanceMidiRenderer::render.
        } else if constexpr (std::is_same_v<TypedEvent, InstrumentPerformanceEvent>) {
          if (typedEvent.bank != 0 || typedEvent.forceBankSelect) {
            track.events.push_back(BankSelect{
                .tick = typedEvent.header.tick,
                .channel = channel,
                .bank = static_cast<u16>(typedEvent.bank),
                .writeLsb = writeBankSelectLsb(options),
            });
          }
          track.events.push_back(ProgramChange{
              .tick = typedEvent.header.tick,
              .channel = channel,
              .program = data7(typedEvent.program),
          });
        } else if constexpr (std::is_same_v<TypedEvent, LevelPerformanceEvent>) {
          if (resolveLevelResolution(options.volumeResolution, typedEvent.precisionHint) ==
              MidiLevelResolution::FourteenBit) {
            track.events.push_back(Volume14{
                .tick = typedEvent.header.tick,
                .channel = channel,
                .value = LevelScale::midi14FromLinear(typedEvent.linearGain),
            });
          } else {
            track.events.push_back(Volume{
                .tick = typedEvent.header.tick,
                .channel = channel,
                .value = LevelScale::midi7FromLinear(typedEvent.linearGain),
            });
          }
        } else if constexpr (std::is_same_v<TypedEvent, ExpressionPerformanceEvent>) {
          addExpression(track, typedEvent.header.tick, channel, typedEvent.linearGain, typedEvent.precisionHint,
                        options);
        } else if constexpr (std::is_same_v<TypedEvent, PanPerformanceEvent>) {
          track.events.push_back(Pan{
              .tick = typedEvent.header.tick,
              .channel = channel,
              .value = midiPan(typedEvent.stereoPosition),
          });
          if (typedEvent.hasLinearGain) {
            addExpression(track, typedEvent.header.tick, channel, typedEvent.linearGain, LevelPrecisionHint::SevenBit,
                          options);
          }
        } else if constexpr (std::is_same_v<TypedEvent, MasterLevelPerformanceEvent>) {
          track.events.push_back(MasterVolume{
              .tick = typedEvent.header.tick,
              .value = LevelScale::midi14FromLinear(typedEvent.linearGain),
          });
        } else if constexpr (std::is_same_v<TypedEvent, ReverbPerformanceEvent>) {
          track.events.push_back(Reverb{
              .tick = typedEvent.header.tick,
              .channel = channel,
              .value = midiNormalized7(typedEvent.send),
          });
        } else if constexpr (std::is_same_v<TypedEvent, MonoModePerformanceEvent>) {
          track.events.push_back(MonoMode{
              .tick = typedEvent.header.tick,
              .channel = channel,
              .channels = typedEvent.channels,
          });
        } else if constexpr (std::is_same_v<TypedEvent, TuningPerformanceEvent>) {
          track.events.push_back(FineTune{
              .tick = typedEvent.header.tick,
              .channel = channel,
              .cents = typedEvent.cents,
          });
        } else if constexpr (std::is_same_v<TypedEvent, GlobalTransposePerformanceEvent>) {
          // Global transpose changes how later notes and portamento controls are written. It does not
          // become a MIDI event itself.
        } else if constexpr (std::is_same_v<TypedEvent, PitchBendPerformanceEvent>) {
          track.events.push_back(PitchBend{
              .tick = typedEvent.header.tick,
              .channel = channel,
              .value = midiPitchBend(typedEvent.semitones, state.pitchBendRangeSemitones),
          });
        } else if constexpr (std::is_same_v<TypedEvent, PitchBendRangePerformanceEvent>) {
          track.events.push_back(PitchBendRange{
              .tick = typedEvent.header.tick,
              .channel = channel,
              .semitones = typedEvent.semitones,
          });
          state.pitchBendRangeSemitones = typedEvent.semitones;
        } else if constexpr (std::is_same_v<TypedEvent, PortamentoPerformanceEvent>) {
          const double previousKey =
              typedEvent.previousKey + globalTransposeAt(globalTransposes, typedEvent.header.tick);
          track.events.push_back(PortamentoTime14{
              .tick = typedEvent.header.tick,
              .channel = channel,
              .value = data14(typedEvent.timeMilliseconds),
          });
          track.events.push_back(PortamentoControl{
              .tick = typedEvent.header.tick,
              .channel = channel,
              .key = midiKey(previousKey),
          });
        } else if constexpr (std::is_same_v<TypedEvent, PortamentoEnablePerformanceEvent>) {
          track.events.push_back(PortamentoEnable{
              .tick = typedEvent.header.tick,
              .channel = channel,
              .enabled = typedEvent.enabled,
          });
        } else if constexpr (std::is_same_v<TypedEvent, PortamentoTimePerformanceEvent>) {
          track.events.push_back(PortamentoTime{
              .tick = typedEvent.header.tick,
              .channel = channel,
              .value = data7(typedEvent.timeMilliseconds),
          });
        } else if constexpr (std::is_same_v<TypedEvent, PortamentoControlPerformanceEvent>) {
          const double previousKey =
              typedEvent.previousKey + globalTransposeAt(globalTransposes, typedEvent.header.tick);
          track.events.push_back(PortamentoControl{
              .tick = typedEvent.header.tick,
              .channel = channel,
              .key = midiKey(previousKey),
          });
        } else if constexpr (std::is_same_v<TypedEvent, LegatoPedalPerformanceEvent>) {
          track.events.push_back(LegatoPedal{
              .tick = typedEvent.header.tick,
              .channel = channel,
              .enabled = typedEvent.enabled,
          });
        } else if constexpr (std::is_same_v<TypedEvent, ModulationPerformanceEvent>) {
          const u8 value = midiNormalized7(typedEvent.amount);
          switch (typedEvent.target) {
            case ModulationPerformanceTarget::VibratoDepth:
              track.events.push_back(VibratoDepth{
                  .tick = typedEvent.header.tick,
                  .channel = channel,
                  .value = value,
              });
              break;
            case ModulationPerformanceTarget::VibratoRate:
              track.events.push_back(VibratoFrequency{
                  .tick = typedEvent.header.tick,
                  .channel = channel,
                  .value = value,
              });
              break;
            case ModulationPerformanceTarget::TremoloDepth:
              track.events.push_back(TremoloDepth{
                  .tick = typedEvent.header.tick,
                  .channel = channel,
                  .value = value,
              });
              break;
            case ModulationPerformanceTarget::TremoloRate:
              track.events.push_back(TremoloFrequency{
                  .tick = typedEvent.header.tick,
                  .channel = channel,
                  .value = value,
              });
              break;
          }
        } else if constexpr (std::is_same_v<TypedEvent, MarkerPerformanceEvent>) {
          track.events.push_back(Marker{
              .tick = typedEvent.header.tick,
              .text = typedEvent.text,
          });
        }
      },
      event);
}

}  // namespace

MidiSequence PerformanceMidiRenderer::render(const PerformanceSequence& performance, MidiExportOptions options) const {
  MidiSequence sequence{
      .timebase = performance.timebase,
      .diagnostics = performance.diagnostics,
  };
  sequence.tracks.reserve(performance.tracks.size());
  const std::vector<GlobalTransposeChange> globalTransposes = collectGlobalTransposeChanges(performance);
  const std::vector<TimeSignature> globalTimeSignatures = collectGlobalTimeSignatures(performance);

  for (size_t trackIndex = 0; trackIndex < performance.tracks.size(); ++trackIndex) {
    const auto& performanceTrack = performance.tracks[trackIndex];
    MidiTrack midiTrack{
        .name = "Track " + std::to_string(performanceTrack.sourceTrackNumber),
    };
    RenderTrackState renderState;
    const auto assignment = midiChannelAssignment(trackIndex, options);
    if (assignment.port > 255) {
      sequence.diagnostics.push_back(Diagnostic{
          .severity = Severity::Warning,
          .message = "MIDI port number exceeded the Standard MIDI File port meta-event range",
      });
    }
    midiTrack.events.push_back(MidiPort{
        .tick = 0,
        .port = midiPortByte(assignment.port),
    });
    for (const auto& event : performanceTrack.events) {
      addMidiEvent(midiTrack, renderState, event, assignment.channel, globalTransposes, options);
    }
    u64 endTick = performanceTrack.endTick;
    if (trackIndex == 0) {
      midiTrack.events.insert(midiTrack.events.end(), globalTimeSignatures.begin(), globalTimeSignatures.end());
      for (const auto& timeSignature : globalTimeSignatures) {
        endTick = std::max(endTick, timeSignature.tick);
      }
    }
    midiTrack.events.push_back(EndOfTrack{
        .tick = endTick,
    });
    sequence.tracks.push_back(std::move(midiTrack));
  }

  return sequence;
}

}  // namespace vgmtrans::core
