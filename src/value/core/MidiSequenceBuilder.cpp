/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/core/MidiSequenceBuilder.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace vgmtrans::core {

namespace {

constexpr size_t kMaxExecutedCommandsPerTrack = 262144;

template <typename T>
void appendEvents(std::vector<MidiEvent>& destination, std::vector<T> events) {
  destination.insert(destination.end(),
                     std::make_move_iterator(events.begin()),
                     std::make_move_iterator(events.end()));
}

void purgeEndedPendingNotes(
    const std::vector<MidiEvent>& events,
    std::vector<size_t>& pendingNoteIndexes,
    u64 tick) {
  std::erase_if(pendingNoteIndexes, [&](size_t index) {
    if (index >= events.size()) {
      return true;
    }
    const auto* note = std::get_if<NoteDuration>(&events[index]);
    return note == nullptr || note->tick + note->duration <= tick;
  });
}

void extendPendingNotes(
    std::vector<MidiEvent>& events,
    const std::vector<size_t>& pendingNoteIndexes,
    u64 endTick) {
  for (const size_t index : pendingNoteIndexes) {
    if (index >= events.size()) {
      continue;
    }
    auto* note = std::get_if<NoteDuration>(&events[index]);
    if (note != nullptr && endTick > note->tick) {
      note->duration = static_cast<u32>(std::max<u64>(note->duration, endTick - note->tick));
    }
  }
}

[[nodiscard]] Diagnostic warning(std::string message, SourceRange range) {
  return Diagnostic{
      .severity = Severity::Warning,
      .message = std::move(message),
      .range = range,
  };
}

[[nodiscard]] std::unordered_map<u64, size_t> commandIndexByOffset(const CommandTrack& track) {
  std::unordered_map<u64, size_t> indexes;
  for (size_t i = 0; i < track.commands.size(); ++i) {
    const auto range = commandRange(track.commands[i]);
    if (range.valid()) {
      indexes.emplace(range.offset, i);
    }
  }
  return indexes;
}

[[nodiscard]] std::optional<size_t> destinationIndex(
    const std::unordered_map<u64, size_t>& indexes,
    Address destination) {
  const auto found = indexes.find(destination.value);
  if (found == indexes.end()) {
    return std::nullopt;
  }
  return found->second;
}

void rememberExecutedCommand(const Command& command, std::unordered_set<u64>& offsets) {
  if (std::holds_alternative<LoopBoundaryCommand>(command)) {
    return;
  }

  const auto range = commandRange(command);
  if (range.valid()) {
    offsets.insert(range.offset);
  }
}

[[nodiscard]] u8 midiChannelForTrack(size_t trackIndex, const CommandSequence& commandSequence) {
  constexpr size_t kChannelsPerBank = 16;
  constexpr size_t kSkippedChannel = 9;
  if (!commandSequence.behavior.skipChannel10) {
    return static_cast<u8>(trackIndex % kChannelsPerBank);
  }

  constexpr size_t kUsableChannelsPerBank = kChannelsPerBank - 1;
  const size_t slot = trackIndex % kUsableChannelsPerBank;
  return static_cast<u8>(slot < kSkippedChannel ? slot : slot + 1);
}

[[nodiscard]] std::optional<u64> firstLoopTick(
    const CommandSequence& commandSequence,
    const CommandTrack& track,
    const MidiSequenceProfile& profile,
    u8 channel) {
  // Dry-run the track state to find the first musical loop without emitting events.
  const auto indexes = commandIndexByOffset(track);
  MidiTrackState state{
      .trackIndex = track.sourceTrackNumber,
      .channel = channel,
      .globalTranspose = commandSequence.behavior.initialGlobalTranspose,
  };
  size_t pc = 0;
  size_t executedCommands = 0;
  std::unordered_set<u64> executedOffsets;
  std::vector<size_t> returnStack;

  while (pc < track.commands.size() && executedCommands++ < kMaxExecutedCommandsPerTrack) {
    const auto& command = track.commands[pc];
    bool incrementPc = true;
    bool ended = false;
    std::optional<u64> loopTick;

    std::visit(
        [&](const auto& typedCommand) {
          using TypedCommand = std::decay_t<decltype(typedCommand)>;
          if constexpr (std::is_same_v<TypedCommand, NoteCommand>) {
            state.tick += profile.noteTiming(typedCommand, state).advanceTicks;
          } else if constexpr (std::is_same_v<TypedCommand, RestCommand>) {
            state.tick += profile.restTicks(typedCommand, state);
          } else if constexpr (std::is_same_v<TypedCommand, NoteStateCommand>) {
            static_cast<void>(profile.interpretNoteState(typedCommand, state));
          } else if constexpr (std::is_same_v<TypedCommand, DurationCommand>) {
            profile.applyDuration(typedCommand, state);
          } else if constexpr (std::is_same_v<TypedCommand, TransposeCommand>) {
            profile.applyTranspose(typedCommand, state);
          } else if constexpr (std::is_same_v<TypedCommand, GlobalTransposeCommand>) {
            state.globalTranspose = typedCommand.rawSemitones;
          } else if constexpr (std::is_same_v<TypedCommand, PortamentoCommand>) {
            static_cast<void>(profile.interpretPortamento(typedCommand, state));
          } else if constexpr (std::is_same_v<TypedCommand, VibratoCommand>) {
            static_cast<void>(profile.interpretVibrato(typedCommand, state));
          } else if constexpr (std::is_same_v<TypedCommand, TremoloCommand>) {
            static_cast<void>(profile.interpretTremolo(typedCommand, state));
          } else if constexpr (std::is_same_v<TypedCommand, ModulationRateCommand>) {
            static_cast<void>(profile.interpretModulationRate(typedCommand, state));
          } else if constexpr (std::is_same_v<TypedCommand, DriverSpecificCommand>) {
            static_cast<void>(profile.interpretDriverSpecific(typedCommand, state));
          } else if constexpr (std::is_same_v<TypedCommand, RepeatCommand>) {
            if (typedCommand.slot >= state.repeatCounters.size()) {
              ended = true;
              return;
            }
            auto& counter = state.repeatCounters[typedCommand.slot];
            if (typedCommand.count == 0 && counter == 0) {
              loopTick = state.tick;
              ended = true;
              return;
            }
            if (counter == 0) {
              counter = typedCommand.count;
              if (const auto target = destinationIndex(indexes, typedCommand.destination)) {
                pc = *target;
                incrementPc = false;
              } else {
                ended = true;
              }
            } else {
              --counter;
              if (counter != 0) {
                if (const auto target = destinationIndex(indexes, typedCommand.destination)) {
                  pc = *target;
                  incrementPc = false;
                } else {
                  ended = true;
                }
              }
            }
          } else if constexpr (std::is_same_v<TypedCommand, RepeatBreakCommand>) {
            if (typedCommand.slot >= state.repeatCounters.size()) {
              ended = true;
              return;
            }
            auto& counter = state.repeatCounters[typedCommand.slot];
            if (counter == 1) {
              counter = 0;
              static_cast<void>(profile.interpretRepeatBreak(typedCommand, state));
              if (const auto target = destinationIndex(indexes, typedCommand.destination)) {
                pc = *target;
                incrementPc = false;
              } else {
                ended = true;
              }
            }
          } else if constexpr (std::is_same_v<TypedCommand, JumpCommand>) {
            const bool destinationWasExecuted = executedOffsets.contains(typedCommand.destination.value);
            rememberExecutedCommand(command, executedOffsets);
            if (const auto target = destinationIndex(indexes, typedCommand.destination)) {
              if (destinationWasExecuted) {
                loopTick = state.tick;
                ended = true;
                return;
              }
              pc = *target;
              incrementPc = false;
            } else {
              ended = true;
            }
          } else if constexpr (std::is_same_v<TypedCommand, CallCommand>) {
            const auto returnTarget = destinationIndex(indexes, typedCommand.returnAddress);
            const auto callTarget = destinationIndex(indexes, typedCommand.destination);
            if (!returnTarget || !callTarget) {
              ended = true;
              return;
            }
            returnStack.push_back(*returnTarget);
            pc = *callTarget;
            incrementPc = false;
          } else if constexpr (std::is_same_v<TypedCommand, ReturnCommand>) {
            if (returnStack.empty()) {
              ended = true;
              return;
            }
            pc = returnStack.back();
            returnStack.pop_back();
            incrementPc = false;
          } else if constexpr (std::is_same_v<TypedCommand, LoopBoundaryCommand>) {
            if (const auto target = destinationIndex(indexes, typedCommand.destination);
                target.has_value() && *target < pc) {
              pc = *target;
              incrementPc = false;
            } else {
              loopTick = state.tick;
              ended = true;
            }
          } else if constexpr (std::is_same_v<TypedCommand, EndCommand>) {
            ended = true;
          }
        },
        command);

    if (loopTick.has_value()) {
      return loopTick;
    }
    if (ended) {
      return std::nullopt;
    }
    if (incrementPc) {
      ++pc;
    }
    if (incrementPc || !std::holds_alternative<JumpCommand>(command)) {
      rememberExecutedCommand(command, executedOffsets);
    }
  }

  return std::nullopt;
}

[[nodiscard]] u64 trackStopTick(
    const CommandSequence& commandSequence,
    const CommandTrack& track,
    const MidiSequenceProfile& profile,
    u8 channel) {
  const auto indexes = commandIndexByOffset(track);
  MidiTrackState state{
      .trackIndex = track.sourceTrackNumber,
      .channel = channel,
      .globalTranspose = commandSequence.behavior.initialGlobalTranspose,
  };
  size_t pc = 0;
  size_t executedCommands = 0;
  std::unordered_set<u64> executedOffsets;
  std::vector<size_t> returnStack;

  while (pc < track.commands.size() && executedCommands++ < kMaxExecutedCommandsPerTrack) {
    const auto& command = track.commands[pc];
    bool incrementPc = true;
    bool ended = false;

    std::visit(
        [&](const auto& typedCommand) {
          using TypedCommand = std::decay_t<decltype(typedCommand)>;
          if constexpr (std::is_same_v<TypedCommand, NoteCommand>) {
            state.tick += profile.noteTiming(typedCommand, state).advanceTicks;
          } else if constexpr (std::is_same_v<TypedCommand, RestCommand>) {
            state.tick += profile.restTicks(typedCommand, state);
          } else if constexpr (std::is_same_v<TypedCommand, NoteStateCommand>) {
            static_cast<void>(profile.interpretNoteState(typedCommand, state));
          } else if constexpr (std::is_same_v<TypedCommand, DurationCommand>) {
            profile.applyDuration(typedCommand, state);
          } else if constexpr (std::is_same_v<TypedCommand, TransposeCommand>) {
            profile.applyTranspose(typedCommand, state);
          } else if constexpr (std::is_same_v<TypedCommand, GlobalTransposeCommand>) {
            state.globalTranspose = typedCommand.rawSemitones;
          } else if constexpr (std::is_same_v<TypedCommand, PortamentoCommand>) {
            static_cast<void>(profile.interpretPortamento(typedCommand, state));
          } else if constexpr (std::is_same_v<TypedCommand, VibratoCommand>) {
            static_cast<void>(profile.interpretVibrato(typedCommand, state));
          } else if constexpr (std::is_same_v<TypedCommand, TremoloCommand>) {
            static_cast<void>(profile.interpretTremolo(typedCommand, state));
          } else if constexpr (std::is_same_v<TypedCommand, ModulationRateCommand>) {
            static_cast<void>(profile.interpretModulationRate(typedCommand, state));
          } else if constexpr (std::is_same_v<TypedCommand, DriverSpecificCommand>) {
            static_cast<void>(profile.interpretDriverSpecific(typedCommand, state));
          } else if constexpr (std::is_same_v<TypedCommand, RepeatCommand>) {
            if (typedCommand.slot >= state.repeatCounters.size()) {
              ended = true;
              return;
            }
            auto& counter = state.repeatCounters[typedCommand.slot];
            if (typedCommand.count == 0 && counter == 0) {
              ended = true;
              return;
            }
            if (counter == 0) {
              counter = typedCommand.count;
              if (const auto target = destinationIndex(indexes, typedCommand.destination)) {
                pc = *target;
                incrementPc = false;
              } else {
                ended = true;
              }
            } else {
              --counter;
              if (counter != 0) {
                if (const auto target = destinationIndex(indexes, typedCommand.destination)) {
                  pc = *target;
                  incrementPc = false;
                } else {
                  ended = true;
                }
              }
            }
          } else if constexpr (std::is_same_v<TypedCommand, RepeatBreakCommand>) {
            if (typedCommand.slot >= state.repeatCounters.size()) {
              ended = true;
              return;
            }
            auto& counter = state.repeatCounters[typedCommand.slot];
            if (counter == 1) {
              counter = 0;
              static_cast<void>(profile.interpretRepeatBreak(typedCommand, state));
              if (const auto target = destinationIndex(indexes, typedCommand.destination)) {
                pc = *target;
                incrementPc = false;
              } else {
                ended = true;
              }
            }
          } else if constexpr (std::is_same_v<TypedCommand, JumpCommand>) {
            const bool destinationWasExecuted = executedOffsets.contains(typedCommand.destination.value);
            rememberExecutedCommand(command, executedOffsets);
            if (const auto target = destinationIndex(indexes, typedCommand.destination)) {
              if (destinationWasExecuted) {
                ended = true;
                return;
              }
              pc = *target;
              incrementPc = false;
            } else {
              ended = true;
            }
          } else if constexpr (std::is_same_v<TypedCommand, CallCommand>) {
            const auto returnTarget = destinationIndex(indexes, typedCommand.returnAddress);
            const auto callTarget = destinationIndex(indexes, typedCommand.destination);
            if (!returnTarget || !callTarget) {
              ended = true;
              return;
            }
            returnStack.push_back(*returnTarget);
            pc = *callTarget;
            incrementPc = false;
          } else if constexpr (std::is_same_v<TypedCommand, ReturnCommand>) {
            if (returnStack.empty()) {
              ended = true;
              return;
            }
            pc = returnStack.back();
            returnStack.pop_back();
            incrementPc = false;
          } else if constexpr (std::is_same_v<TypedCommand, LoopBoundaryCommand>) {
            if (const auto target = destinationIndex(indexes, typedCommand.destination);
                target.has_value() && *target < pc) {
              pc = *target;
              incrementPc = false;
            } else {
              ended = true;
            }
          } else if constexpr (std::is_same_v<TypedCommand, EndCommand>) {
            ended = true;
          }
        },
        command);

    if (ended) {
      return state.tick;
    }
    if (incrementPc) {
      ++pc;
    }
    if (incrementPc || !std::holds_alternative<JumpCommand>(command)) {
      rememberExecutedCommand(command, executedOffsets);
    }
  }

  return state.tick;
}

// Immediate commands apply at the current tick without advancing time or changing track position.
template <typename T>
inline constexpr bool kImmediateCommand =
    std::is_same_v<T, NoteStateCommand> ||
    std::is_same_v<T, DurationCommand> ||
    std::is_same_v<T, TransposeCommand> ||
    std::is_same_v<T, GlobalTransposeCommand> ||
    std::is_same_v<T, TempoCommand> ||
    std::is_same_v<T, ProgramCommand> ||
    std::is_same_v<T, VolumeCommand> ||
    std::is_same_v<T, PanCommand> ||
    std::is_same_v<T, MasterVolumeCommand> ||
    std::is_same_v<T, ReverbCommand> ||
    std::is_same_v<T, TuningCommand> ||
    std::is_same_v<T, PortamentoCommand> ||
    std::is_same_v<T, VibratoCommand> ||
    std::is_same_v<T, TremoloCommand> ||
    std::is_same_v<T, ModulationRateCommand> ||
    std::is_same_v<T, EnvelopeCommand> ||
    std::is_same_v<T, DriverSpecificCommand>;

template <typename T>
[[nodiscard]] std::vector<MidiEvent> interpretImmediateCommand(
    const T& command,
    const MidiSequenceProfile& profile,
    MidiTrackState& state) {
  if constexpr (std::is_same_v<T, NoteStateCommand>) {
    return profile.interpretNoteState(command, state);
  } else if constexpr (std::is_same_v<T, DurationCommand>) {
    profile.applyDuration(command, state);
    return {};
  } else if constexpr (std::is_same_v<T, TransposeCommand>) {
    profile.applyTranspose(command, state);
    return {};
  } else if constexpr (std::is_same_v<T, GlobalTransposeCommand>) {
    state.globalTranspose = command.rawSemitones;
    return {};
  } else if constexpr (std::is_same_v<T, TempoCommand>) {
    return profile.interpretTempo(command, state);
  } else if constexpr (std::is_same_v<T, ProgramCommand>) {
    return profile.interpretProgram(command, state);
  } else if constexpr (std::is_same_v<T, VolumeCommand>) {
    return profile.interpretVolume(command, state);
  } else if constexpr (std::is_same_v<T, PanCommand>) {
    return profile.interpretPan(command, state);
  } else if constexpr (std::is_same_v<T, MasterVolumeCommand>) {
    return profile.interpretMasterVolume(command, state);
  } else if constexpr (std::is_same_v<T, ReverbCommand>) {
    return profile.interpretReverb(command, state);
  } else if constexpr (std::is_same_v<T, TuningCommand>) {
    return profile.interpretTuning(command, state);
  } else if constexpr (std::is_same_v<T, PortamentoCommand>) {
    return profile.interpretPortamento(command, state);
  } else if constexpr (std::is_same_v<T, VibratoCommand>) {
    return profile.interpretVibrato(command, state);
  } else if constexpr (std::is_same_v<T, TremoloCommand>) {
    return profile.interpretTremolo(command, state);
  } else if constexpr (std::is_same_v<T, ModulationRateCommand>) {
    return profile.interpretModulationRate(command, state);
  } else if constexpr (std::is_same_v<T, EnvelopeCommand>) {
    return profile.interpretEnvelope(command, state);
  } else if constexpr (std::is_same_v<T, DriverSpecificCommand>) {
    return profile.interpretDriverSpecific(command, state);
  } else {
    static_assert(kImmediateCommand<T>, "unhandled immediate command type");
  }
}

}  // namespace

MidiSequence MidiSequenceBuilder::build(
    const CommandSequence& commandSequence,
    const MidiSequenceProfile& profile,
    LoopPolicy loopPolicy) const {
  if (loopPolicy == LoopPolicy::Default) {
    loopPolicy = commandSequence.behavior.defaultLoopPolicy;
  }
  if (loopPolicy == LoopPolicy::Default) {
    loopPolicy = LoopPolicy::PlayOnce;
  }

  MidiSequence result{
      .timebase = commandSequence.timebase,
  };

  std::optional<u64> playOnceStopTick;
  std::vector<std::optional<u64>> firstLoopTicks(commandSequence.tracks.size());
  u64 globalStopTick = 0;
  for (size_t trackIndex = 0; trackIndex < commandSequence.tracks.size(); ++trackIndex) {
    globalStopTick = std::max(globalStopTick,
                              trackStopTick(commandSequence,
                                            commandSequence.tracks[trackIndex],
                                            profile,
                                            midiChannelForTrack(trackIndex, commandSequence)));
  }
  if (commandSequence.behavior.maxPlaybackTicks.has_value() &&
      globalStopTick > *commandSequence.behavior.maxPlaybackTicks) {
    globalStopTick = *commandSequence.behavior.maxPlaybackTicks;
  }
  if (loopPolicy == LoopPolicy::PlayOnce) {
    // All tracks stop at the latest first-loop tick so short tracks do not truncate the song.
    for (size_t trackIndex = 0; trackIndex < commandSequence.tracks.size(); ++trackIndex) {
      const auto loopTick = firstLoopTick(commandSequence,
                                         commandSequence.tracks[trackIndex],
                                         profile,
                                         midiChannelForTrack(trackIndex, commandSequence));
      firstLoopTicks[trackIndex] = loopTick;
      if (loopTick.has_value() && (!playOnceStopTick.has_value() || *loopTick > *playOnceStopTick)) {
        playOnceStopTick = loopTick;
      }
    }
  }

  for (size_t trackIndex = 0; trackIndex < commandSequence.tracks.size(); ++trackIndex) {
    const auto& track = commandSequence.tracks[trackIndex];
    MidiTrackState state{
        .trackIndex = static_cast<u32>(trackIndex),
        .channel = midiChannelForTrack(trackIndex, commandSequence),
        .globalTranspose = commandSequence.behavior.initialGlobalTranspose,
    };
    MidiTrack midiTrack{
        .name = "Track " + std::to_string(track.sourceTrackNumber),
    };

    if (commandSequence.behavior.writeInitialMonoMode) {
      midiTrack.events.push_back(MonoMode{
          .tick = 0,
          .channel = state.channel,
          .channels = 0,
      });
    }
    if (commandSequence.behavior.writeInitialReverb) {
      midiTrack.events.push_back(Reverb{
          .tick = 0,
          .channel = state.channel,
          .value = commandSequence.behavior.initialReverb,
      });
    }
    profile.beginTrack(commandSequence, track, state, midiTrack.events);

    if (commandSequence.behavior.suppressEventsWhenPlaybackTicksZero && globalStopTick == 0) {
      if (midiTrack.events.empty() || !std::holds_alternative<EndOfTrack>(midiTrack.events.back())) {
        midiTrack.events.push_back(EndOfTrack{.tick = state.tick});
      }
      result.tracks.push_back(std::move(midiTrack));
      continue;
    }

    const auto indexes = commandIndexByOffset(track);
    size_t pc = 0;
    size_t executedCommands = 0;
    bool ended = false;
    std::optional<u64> playbackStopTick;
    if (globalStopTick > 0) {
      playbackStopTick = globalStopTick;
    }
    std::vector<size_t> pendingNoteIndexes;
    std::unordered_set<u64> executedOffsets;
    std::vector<size_t> returnStack;
    while (pc < track.commands.size() && executedCommands++ < kMaxExecutedCommandsPerTrack) {
      if (playbackStopTick.has_value() && state.tick >= *playbackStopTick) {
        ended = true;
        break;
      }

      const auto& command = track.commands[pc];
      bool incrementPc = true;

      std::visit(
          [&](const auto& typedCommand) {
            using TypedCommand = std::decay_t<decltype(typedCommand)>;
            if constexpr (std::is_same_v<TypedCommand, NoteCommand>) {
              auto timing = profile.noteTiming(typedCommand, state);
              u32 soundingTicks = timing.soundingTicks;
              if (commandSequence.behavior.truncateSustainedNotesAtLoopBoundary &&
                  playbackStopTick.has_value() && soundingTicks > timing.advanceTicks + 1) {
                const u64 stopEndTick = *playbackStopTick + 1;
                if (state.tick < stopEndTick && state.tick + soundingTicks > stopEndTick) {
                  soundingTicks = static_cast<u32>(stopEndTick - state.tick);
                }
              }
              appendEvents(midiTrack.events, std::move(timing.beforeEvents));
              if (timing.extendsPrevious) {
                // Slurred notes extend the existing note event instead of starting a new note.
                extendPendingNotes(midiTrack.events, pendingNoteIndexes, state.tick + soundingTicks);
              } else {
                purgeEndedPendingNotes(midiTrack.events, pendingNoteIndexes, state.tick);
                const size_t noteIndex = midiTrack.events.size();
                midiTrack.events.push_back(NoteDuration{
                    .tick = state.tick,
                    .channel = state.channel,
                    .key = timing.key,
                    .velocity = timing.velocity,
                    .duration = soundingTicks,
                });
                pendingNoteIndexes.push_back(noteIndex);
              }
              state.tick += timing.advanceTicks;
            } else if constexpr (std::is_same_v<TypedCommand, RestCommand>) {
              state.tick += profile.restTicks(typedCommand, state);
            } else if constexpr (kImmediateCommand<TypedCommand>) {
              appendEvents(midiTrack.events, interpretImmediateCommand(typedCommand, profile, state));
            } else if constexpr (std::is_same_v<TypedCommand, RepeatCommand>) {
              if (typedCommand.slot >= state.repeatCounters.size()) {
                result.diagnostics.push_back(warning("Repeat command uses an unsupported repeat slot",
                                                     typedCommand.range));
                return;
              }
              auto& counter = state.repeatCounters[typedCommand.slot];
              if (typedCommand.count == 0 && counter == 0) {
                midiTrack.events.push_back(Marker{.tick = state.tick, .text = "Loop"});
                ended = true;
                return;
              }

              if (counter == 0) {
                counter = typedCommand.count;
                if (const auto target = destinationIndex(indexes, typedCommand.destination)) {
                  pc = *target;
                  incrementPc = false;
                } else {
                  result.diagnostics.push_back(warning("Repeat destination was not decoded",
                                                       typedCommand.range));
                }
              } else {
                --counter;
                if (counter != 0) {
                  if (const auto target = destinationIndex(indexes, typedCommand.destination)) {
                    pc = *target;
                    incrementPc = false;
                  } else {
                    result.diagnostics.push_back(warning("Repeat destination was not decoded",
                                                         typedCommand.range));
                  }
                }
              }
            } else if constexpr (std::is_same_v<TypedCommand, RepeatBreakCommand>) {
              if (typedCommand.slot >= state.repeatCounters.size()) {
                result.diagnostics.push_back(warning("Repeat break command uses an unsupported repeat slot",
                                                     typedCommand.range));
                return;
              }
              auto& counter = state.repeatCounters[typedCommand.slot];
              if (counter == 1) {
                counter = 0;
                appendEvents(midiTrack.events, profile.interpretRepeatBreak(typedCommand, state));
                if (const auto target = destinationIndex(indexes, typedCommand.destination)) {
                  pc = *target;
                  incrementPc = false;
                } else {
                  result.diagnostics.push_back(warning("Repeat break destination was not decoded",
                                                       typedCommand.range));
                }
              }
            } else if constexpr (std::is_same_v<TypedCommand, JumpCommand>) {
              const bool destinationWasExecuted = executedOffsets.contains(typedCommand.destination.value);
              rememberExecutedCommand(command, executedOffsets);
              if (const auto target = destinationIndex(indexes, typedCommand.destination)) {
                if (loopPolicy == LoopPolicy::PlayOnce && destinationWasExecuted) {
                  // A backward jump to an executed command marks loop playback, not another pass forever.
                  if (state.tick == 0 || !playbackStopTick.has_value() || state.tick >= *playbackStopTick) {
                    ended = true;
                    return;
                  }
                }
                if (loopPolicy != LoopPolicy::PlayOnce) {
                  midiTrack.events.push_back(Marker{.tick = state.tick, .text = "Jump"});
                }
                pc = *target;
                incrementPc = false;
              } else {
                result.diagnostics.push_back(warning("Jump destination was not decoded", typedCommand.range));
                ended = true;
              }
            } else if constexpr (std::is_same_v<TypedCommand, CallCommand>) {
              const auto returnTarget = destinationIndex(indexes, typedCommand.returnAddress);
              const auto callTarget = destinationIndex(indexes, typedCommand.destination);
              if (!returnTarget) {
                result.diagnostics.push_back(warning("Call return address was not decoded", typedCommand.range));
                ended = true;
                return;
              }
              if (!callTarget) {
                result.diagnostics.push_back(warning("Call destination was not decoded", typedCommand.range));
                ended = true;
                return;
              }
              returnStack.push_back(*returnTarget);
              pc = *callTarget;
              incrementPc = false;
            } else if constexpr (std::is_same_v<TypedCommand, ReturnCommand>) {
              if (returnStack.empty()) {
                ended = true;
                return;
              }
              pc = returnStack.back();
              returnStack.pop_back();
              incrementPc = false;
            } else if constexpr (std::is_same_v<TypedCommand, LoopBoundaryCommand>) {
              if (loopPolicy == LoopPolicy::PlayOnce) {
                if (const auto target = destinationIndex(indexes, typedCommand.destination);
                    target.has_value() && playbackStopTick.has_value() && state.tick < *playbackStopTick &&
                    *target < pc) {
                  pc = *target;
                  incrementPc = false;
                  return;
                }
              }
              if (loopPolicy != LoopPolicy::PlayOnce) {
                midiTrack.events.push_back(Marker{.tick = state.tick, .text = "Loop"});
              }
              ended = true;
            } else if constexpr (std::is_same_v<TypedCommand, EndCommand>) {
              midiTrack.events.push_back(EndOfTrack{.tick = state.tick});
              ended = true;
            } else if constexpr (std::is_same_v<TypedCommand, UnknownCommand>) {
              result.diagnostics.push_back(warning("Unknown sequencer command " +
                                                       std::to_string(typedCommand.opcode) +
                                                       " was skipped at source offset " +
                                                       std::to_string(typedCommand.range.offset),
                                                   typedCommand.range));
            }
          },
          command);

      if (playbackStopTick.has_value() && state.tick >= *playbackStopTick) {
        ended = true;
      }
      if (ended) {
        break;
      }
      if (incrementPc) {
        ++pc;
      }
      if (incrementPc || !std::holds_alternative<JumpCommand>(command)) {
        rememberExecutedCommand(command, executedOffsets);
      }
    }

    if (pc < track.commands.size() && executedCommands > kMaxExecutedCommandsPerTrack) {
      const auto range = track.commands.empty() ? SourceRange{} : commandRange(track.commands.back());
      result.diagnostics.push_back(warning("MIDI sequence building stopped after too many executed commands", range));
    }
    if (midiTrack.events.empty() || !std::holds_alternative<EndOfTrack>(midiTrack.events.back())) {
      midiTrack.events.push_back(EndOfTrack{.tick = state.tick});
    }
    result.tracks.push_back(std::move(midiTrack));
  }

  return result;
}

}  // namespace vgmtrans::core
