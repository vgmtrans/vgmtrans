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

[[nodiscard]] s16 midiPitchBend(double semitones, u16 rangeCents) {
  if (rangeCents == 0) {
    return 0;
  }

  const double normalized = (semitones * 100.0) / static_cast<double>(rangeCents);
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
  u16 pitchBendRangeCents = 200;
  std::optional<u16> lastPitchBendRangeCents;
  u16 sourcePitchBendRangeCents = 200;
  double sourcePitchBendSemitones = 0.0;
  double simulatedVibratoSemitones = 0.0;
  double simulatedVibratoDepthSemitones = 0.0;
  double vibratoFrequencyHz = 0.0;
  u32 vibratoDelayTicks = 0;
  bool vibratoDelayArmed = false;
  u64 vibratoStartTick = 0;
  u64 vibratoCursorTick = 0;
  double vibratoPhaseCycles = 0.0;
  std::optional<s16> lastPitchBendValue;
  u32 microsecondsPerQuarter = 500000;
  double sourceExpressionGain = 1.0;
  double panExpressionGain = 1.0;
  double simulatedTremoloGain = 1.0;
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
  std::ranges::stable_sort(timeSignatures,
                           [](const TimeSignature& lhs, const TimeSignature& rhs) { return lhs.tick < rhs.tick; });
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

[[nodiscard]] u16 requiredPitchBendRangeCents(const RenderTrackState& state) {
  const double possibleSemitones = std::abs(state.sourcePitchBendSemitones) + state.simulatedVibratoDepthSemitones;
  const int cents = std::max<int>(200, static_cast<int>(std::ceil(possibleSemitones * 100.0)));
  return static_cast<u16>(std::min<int>(cents, std::numeric_limits<u16>::max()));
}

void ensurePitchBendRange(MidiTrack& track, RenderTrackState& state, u64 tick, u8 channel, u16 cents) {
  const u16 range = std::max<u16>(200, cents);
  if (state.lastPitchBendRangeCents && *state.lastPitchBendRangeCents == range) {
    state.pitchBendRangeCents = range;
    return;
  }
  track.events.push_back(PitchBendRange{
      .tick = tick,
      .channel = channel,
      .cents = range,
  });
  state.pitchBendRangeCents = range;
  state.lastPitchBendRangeCents = range;
}

void addPitchBend(MidiTrack& track, RenderTrackState& state, u64 tick, u8 channel, s16 value, bool force = false) {
  if (!force && state.lastPitchBendValue && *state.lastPitchBendValue == value) {
    return;
  }
  track.events.push_back(PitchBend{
      .tick = tick,
      .channel = channel,
      .value = value,
  });
  state.lastPitchBendValue = value;
}

void addCombinedPitchBend(MidiTrack& track, RenderTrackState& state, u64 tick, u8 channel, bool force = true) {
  ensurePitchBendRange(track, state, tick, channel,
                       std::max(state.sourcePitchBendRangeCents, requiredPitchBendRangeCents(state)));
  const s16 value =
      midiPitchBend(state.sourcePitchBendSemitones + state.simulatedVibratoSemitones, state.pitchBendRangeCents);
  addPitchBend(track, state, tick, channel, value, force);
}

[[nodiscard]] double tickSeconds(const Timebase& timebase, const RenderTrackState& state) {
  const u16 ppqn = std::max<u16>(timebase.ppqn, 1);
  return (static_cast<double>(state.microsecondsPerQuarter) / 1'000'000.0) / ppqn;
}

[[nodiscard]] double triangleLfo(double phaseCycles) {
  const double phase = phaseCycles - std::floor(phaseCycles);
  if (phase < 0.25) {
    return phase * 4.0;
  }
  if (phase < 0.75) {
    return 2.0 - (phase * 4.0);
  }
  return (phase * 4.0) - 4.0;
}

void flushSimulatedVibrato(MidiTrack& track, RenderTrackState& state, u64 upToTick, u8 channel,
                           const Timebase& timebase) {
  if (state.vibratoCursorTick >= upToTick) {
    return;
  }

  while (state.vibratoCursorTick < upToTick) {
    ++state.vibratoCursorTick;
    if (state.vibratoCursorTick < state.vibratoStartTick || state.simulatedVibratoDepthSemitones <= 0.0 ||
        state.vibratoFrequencyHz <= 0.0) {
      continue;
    }

    state.simulatedVibratoSemitones = state.simulatedVibratoDepthSemitones * triangleLfo(state.vibratoPhaseCycles);
    addCombinedPitchBend(track, state, state.vibratoCursorTick, channel, false);
    state.vibratoPhaseCycles =
        std::fmod(state.vibratoPhaseCycles + state.vibratoFrequencyHz * tickSeconds(timebase, state), 1.0);
  }
}

void setSimulatedVibratoDepth(MidiTrack& track, RenderTrackState& state, u64 tick, u8 channel, double semitones) {
  state.simulatedVibratoDepthSemitones = std::max(0.0, semitones);
  if (state.vibratoDelayArmed) {
    state.vibratoStartTick = tick + (state.simulatedVibratoDepthSemitones > 0.0 ? state.vibratoDelayTicks : 0);
    state.vibratoCursorTick = tick;
    state.vibratoPhaseCycles = 0.0;
    state.vibratoDelayArmed = false;
    if (state.simulatedVibratoSemitones != 0.0 || state.simulatedVibratoDepthSemitones <= 0.0) {
      state.simulatedVibratoSemitones = 0.0;
      addCombinedPitchBend(track, state, tick, channel, false);
    }
    return;
  }

  if (state.simulatedVibratoDepthSemitones <= 0.0) {
    state.simulatedVibratoSemitones = 0.0;
    addCombinedPitchBend(track, state, tick, channel, false);
  }
}

void restartSimulatedVibratoForNote(MidiTrack& track, RenderTrackState& state, u64 tick, u8 channel) {
  if (!state.vibratoDelayArmed && (state.simulatedVibratoDepthSemitones <= 0.0 || state.vibratoFrequencyHz <= 0.0)) {
    return;
  }

  state.vibratoStartTick = tick + state.vibratoDelayTicks;
  state.vibratoCursorTick = tick;
  state.vibratoPhaseCycles = 0.0;
  state.vibratoDelayArmed = false;
  if (state.simulatedVibratoSemitones != 0.0) {
    state.simulatedVibratoSemitones = 0.0;
    addCombinedPitchBend(track, state, tick, channel, false);
  }
}

bool shouldRestartSimulatedVibratoForNote(const PerformanceEvent& event, const RenderTrackState& state) {
  const auto* note = std::get_if<NotePerformanceEvent>(&event);
  return note != nullptr && !note->extendsPrevious &&
         (state.vibratoDelayArmed || (state.simulatedVibratoDepthSemitones > 0.0 && state.vibratoFrequencyHz > 0.0));
}

void addCombinedExpression(MidiTrack& track, RenderTrackState& state, u64 tick, u8 channel,
                           const MidiExportOptions& options) {
  addExpression(track, tick, channel, state.sourceExpressionGain * state.panExpressionGain * state.simulatedTremoloGain,
                LevelPrecisionHint::SevenBit, options);
}

void addMidiEvent(MidiTrack& track, RenderTrackState& state, const PerformanceEvent& event, u8 channel,
                  std::span<const GlobalTransposeChange> globalTransposes, const MidiExportOptions& options,
                  ModulationConversionPolicy modulationConversion) {
  std::visit(
      [&](const auto& typedEvent) {
        using TypedEvent = std::decay_t<decltype(typedEvent)>;
        if constexpr (std::is_same_v<TypedEvent, NotePerformanceEvent>) {
          const u8 key = midiKey(typedEvent.key + globalTransposeAt(globalTransposes, typedEvent.header.tick));
          if (extendPreviousNote(track, state, typedEvent, channel)) {
            return;
          }
          if (modulationConversion == ModulationConversionPolicy::SequenceEventSimulation) {
            restartSimulatedVibratoForNote(track, state, typedEvent.header.tick, channel);
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
          state.microsecondsPerQuarter = typedEvent.microsecondsPerQuarter;
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
          state.sourceExpressionGain = typedEvent.linearGain;
          if (modulationConversion == ModulationConversionPolicy::SequenceEventSimulation) {
            addCombinedExpression(track, state, typedEvent.header.tick, channel, options);
          } else {
            addExpression(track, typedEvent.header.tick, channel, typedEvent.linearGain, typedEvent.precisionHint,
                          options);
          }
        } else if constexpr (std::is_same_v<TypedEvent, PanPerformanceEvent>) {
          track.events.push_back(Pan{
              .tick = typedEvent.header.tick,
              .channel = channel,
              .value = midiPan(typedEvent.stereoPosition),
          });
          if (typedEvent.hasLinearGain) {
            if (modulationConversion == ModulationConversionPolicy::SequenceEventSimulation) {
              state.panExpressionGain = typedEvent.linearGain;
              addCombinedExpression(track, state, typedEvent.header.tick, channel, options);
            } else {
              addExpression(track, typedEvent.header.tick, channel, typedEvent.linearGain, LevelPrecisionHint::SevenBit,
                            options);
            }
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
          if (modulationConversion == ModulationConversionPolicy::SequenceEventSimulation) {
            state.sourcePitchBendSemitones = typedEvent.semitones;
            addCombinedPitchBend(track, state, typedEvent.header.tick, channel, false);
          } else {
            addPitchBend(track, state, typedEvent.header.tick, channel,
                         midiPitchBend(typedEvent.semitones, state.pitchBendRangeCents));
          }
        } else if constexpr (std::is_same_v<TypedEvent, PitchBendRangePerformanceEvent>) {
          if (modulationConversion == ModulationConversionPolicy::SequenceEventSimulation) {
            state.sourcePitchBendRangeCents = std::max<u16>(200, typedEvent.cents);
            ensurePitchBendRange(track, state, typedEvent.header.tick, channel,
                                 std::max(state.sourcePitchBendRangeCents, requiredPitchBendRangeCents(state)));
          } else {
            ensurePitchBendRange(track, state, typedEvent.header.tick, channel, typedEvent.cents);
          }
        } else if constexpr (std::is_same_v<TypedEvent, VibratoDelayPerformanceEvent>) {
          state.vibratoDelayTicks = typedEvent.delayTicks;
          state.vibratoDelayArmed = true;
          if (modulationConversion != ModulationConversionPolicy::SequenceEventSimulation) {
            track.events.push_back(VibratoDelay{
                .tick = typedEvent.header.tick,
                .channel = channel,
                .ticks = typedEvent.midiValue,
            });
          }
        } else if constexpr (std::is_same_v<TypedEvent, TremoloDelayPerformanceEvent>) {
          if (modulationConversion != ModulationConversionPolicy::SequenceEventSimulation) {
            track.events.push_back(TremoloDelay{
                .tick = typedEvent.header.tick,
                .channel = channel,
                .ticks = typedEvent.midiValue,
            });
          }
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
          if (typedEvent.controllerRangeOnly) {
            return;
          }
          const double normalizedAmount = std::clamp(typedEvent.amount, 0.0, 1.0);
          const u8 value = midiNormalized7(normalizedAmount);
          if (modulationConversion == ModulationConversionPolicy::SequenceEventSimulation) {
            switch (typedEvent.target) {
              case ModulationPerformanceTarget::VibratoDepth:
                setSimulatedVibratoDepth(
                    track, state, typedEvent.header.tick, channel,
                    typedEvent.pitchDepthSemitones.value_or(std::clamp(typedEvent.amount, 0.0, 1.0) * 2.0));
                break;
              case ModulationPerformanceTarget::TremoloDepth:
                state.simulatedTremoloGain =
                    std::clamp(1.0 - (std::clamp(typedEvent.amount, 0.0, 1.0) * 0.5), 0.0, 1.0);
                addCombinedExpression(track, state, typedEvent.header.tick, channel, options);
                break;
              case ModulationPerformanceTarget::VibratoRate:
                state.vibratoFrequencyHz = typedEvent.frequencyHz.value_or(state.vibratoFrequencyHz);
                break;
              case ModulationPerformanceTarget::TremoloRate:
                break;
            }
            return;
          }
          switch (typedEvent.target) {
            case ModulationPerformanceTarget::VibratoDepth:
              track.events.push_back(VibratoDepth{
                  .tick = typedEvent.header.tick,
                  .channel = channel,
                  .value = value,
                  .normalizedAmount = normalizedAmount,
              });
              break;
            case ModulationPerformanceTarget::VibratoRate:
              track.events.push_back(VibratoFrequency{
                  .tick = typedEvent.header.tick,
                  .channel = channel,
                  .value = value,
                  .normalizedAmount = normalizedAmount,
              });
              break;
            case ModulationPerformanceTarget::TremoloDepth:
              track.events.push_back(TremoloDepth{
                  .tick = typedEvent.header.tick,
                  .channel = channel,
                  .value = value,
                  .normalizedAmount = normalizedAmount,
              });
              break;
            case ModulationPerformanceTarget::TremoloRate:
              track.events.push_back(TremoloFrequency{
                  .tick = typedEvent.header.tick,
                  .channel = channel,
                  .value = value,
                  .normalizedAmount = normalizedAmount,
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

MidiSequence PerformanceMidiRenderer::render(const PerformanceSequence& performance, MidiExportOptions options,
                                             ModulationConversionPolicy modulationConversion) const {
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
    if (options.writePortMetaEvents) {
      midiTrack.events.push_back(MidiPort{
          .tick = 0,
          .port = midiPortByte(assignment.port),
      });
    }
    for (const auto& event : performanceTrack.events) {
      if (modulationConversion == ModulationConversionPolicy::SequenceEventSimulation) {
        u64 flushTick = performanceEventHeader(event).tick;
        if (shouldRestartSimulatedVibratoForNote(event, renderState) && flushTick != 0) {
          --flushTick;
        }
        flushSimulatedVibrato(midiTrack, renderState, flushTick, assignment.channel, performance.timebase);
      }
      addMidiEvent(midiTrack, renderState, event, assignment.channel, globalTransposes, options, modulationConversion);
    }
    u64 endTick = performanceTrack.endTick;
    if (modulationConversion == ModulationConversionPolicy::SequenceEventSimulation) {
      flushSimulatedVibrato(midiTrack, renderState, endTick, assignment.channel, performance.timebase);
    }
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
