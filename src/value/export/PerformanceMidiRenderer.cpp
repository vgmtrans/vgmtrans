/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/export/PerformanceMidiRenderer.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
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

[[nodiscard]] u16 midiAmplitude14(double linearGain) {
  return data14(std::sqrt(std::clamp(linearGain, 0.0, 1.0)) * 16383.0);
}

[[nodiscard]] u8 midiKey(double key) {
  return data7(key);
}

[[nodiscard]] u8 midiVelocity(double velocity) {
  return data7(velocity * 127.0);
}

[[nodiscard]] u8 midiChannel(size_t trackIndex) {
  const size_t channel = trackIndex >= 9 ? trackIndex + 1 : trackIndex;
  return static_cast<u8>(channel % 16);
}

[[nodiscard]] u8 midiPan(double stereoPosition) {
  return data7(((std::clamp(stereoPosition, -1.0, 1.0) + 1.0) / 2.0) * 127.0);
}

[[nodiscard]] u8 midiNormalized7(double amount) {
  return data7(std::clamp(amount, 0.0, 1.0) * 127.0);
}

struct RenderTrackState {
  std::optional<size_t> lastNoteIndex;
};

bool extendPreviousNote(MidiTrack& track, RenderTrackState& state, const NotePerformanceEvent& note, u8 channel,
                        u8 key) {
  if (!note.extendsPrevious || !state.lastNoteIndex || *state.lastNoteIndex >= track.events.size()) {
    return false;
  }

  auto* previous = std::get_if<NoteDuration>(&track.events[*state.lastNoteIndex]);
  if (previous == nullptr || previous->channel != channel || previous->key != key) {
    return false;
  }

  const u64 previousEnd = previous->tick + previous->duration;
  const u64 extensionEnd = note.header.tick + note.durationTicks;
  if (extensionEnd > previousEnd) {
    previous->duration = static_cast<u32>(extensionEnd - previous->tick);
  }
  return true;
}

void addMidiEvent(MidiTrack& track, RenderTrackState& state, const PerformanceEvent& event, u8 channel) {
  std::visit(
      [&](const auto& typedEvent) {
        using TypedEvent = std::decay_t<decltype(typedEvent)>;
        if constexpr (std::is_same_v<TypedEvent, NotePerformanceEvent>) {
          const u8 key = midiKey(typedEvent.key);
          if (extendPreviousNote(track, state, typedEvent, channel, key)) {
            return;
          }
          state.lastNoteIndex = track.events.size();
          track.events.push_back(NoteDuration{
              .tick = typedEvent.header.tick,
              .channel = channel,
              .key = key,
              .velocity = midiVelocity(typedEvent.velocity),
              .duration = typedEvent.durationTicks,
          });
        } else if constexpr (std::is_same_v<TypedEvent, TempoPerformanceEvent>) {
          track.events.push_back(Tempo{
              .tick = typedEvent.header.tick,
              .microsecondsPerQuarter = typedEvent.microsecondsPerQuarter,
          });
        } else if constexpr (std::is_same_v<TypedEvent, InstrumentPerformanceEvent>) {
          if (typedEvent.bank != 0) {
            track.events.push_back(BankSelect{
                .tick = typedEvent.header.tick,
                .channel = channel,
                .bank = static_cast<u16>(typedEvent.bank),
                .writeLsb = false,
            });
          }
          track.events.push_back(ProgramChange{
              .tick = typedEvent.header.tick,
              .channel = channel,
              .program = data7(typedEvent.program),
          });
        } else if constexpr (std::is_same_v<TypedEvent, LevelPerformanceEvent>) {
          track.events.push_back(Volume{
              .tick = typedEvent.header.tick,
              .channel = channel,
              .value = data7(typedEvent.linearGain * 127.0),
          });
        } else if constexpr (std::is_same_v<TypedEvent, PanPerformanceEvent>) {
          track.events.push_back(Pan{
              .tick = typedEvent.header.tick,
              .channel = channel,
              .value = midiPan(typedEvent.stereoPosition),
          });
          if (typedEvent.linearGain != 1.0) {
            track.events.push_back(Expression{
                .tick = typedEvent.header.tick,
                .channel = channel,
                .value = data7(std::sqrt(std::clamp(typedEvent.linearGain, 0.0, 1.0)) * 127.0),
            });
          }
        } else if constexpr (std::is_same_v<TypedEvent, MasterLevelPerformanceEvent>) {
          track.events.push_back(MasterVolume{
              .tick = typedEvent.header.tick,
              .value = midiAmplitude14(typedEvent.linearGain),
          });
        } else if constexpr (std::is_same_v<TypedEvent, ReverbPerformanceEvent>) {
          track.events.push_back(Reverb{
              .tick = typedEvent.header.tick,
              .channel = channel,
              .value = midiNormalized7(typedEvent.send),
          });
        } else if constexpr (std::is_same_v<TypedEvent, TuningPerformanceEvent>) {
          track.events.push_back(FineTune{
              .tick = typedEvent.header.tick,
              .channel = channel,
              .cents = typedEvent.cents,
          });
        } else if constexpr (std::is_same_v<TypedEvent, PortamentoPerformanceEvent>) {
          track.events.push_back(PortamentoTime14{
              .tick = typedEvent.header.tick,
              .channel = channel,
              .value = data14(typedEvent.timeMilliseconds),
          });
          track.events.push_back(PortamentoControl{
              .tick = typedEvent.header.tick,
              .channel = channel,
              .key = midiKey(typedEvent.previousKey),
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

MidiSequence PerformanceMidiRenderer::render(const PerformanceSequence& performance) const {
  MidiSequence sequence{
      .timebase = performance.timebase,
      .diagnostics = performance.diagnostics,
  };
  sequence.tracks.reserve(performance.tracks.size());

  for (size_t trackIndex = 0; trackIndex < performance.tracks.size(); ++trackIndex) {
    const auto& performanceTrack = performance.tracks[trackIndex];
    MidiTrack midiTrack{
        .name = "Track " + std::to_string(performanceTrack.sourceTrackNumber),
    };
    RenderTrackState renderState;
    const u8 channel = midiChannel(trackIndex);
    for (const auto& event : performanceTrack.events) {
      addMidiEvent(midiTrack, renderState, event, channel);
    }
    midiTrack.events.push_back(EndOfTrack{
        .tick = performanceTrack.endTick,
    });
    sequence.tracks.push_back(std::move(midiTrack));
  }

  return sequence;
}

}  // namespace vgmtrans::core
