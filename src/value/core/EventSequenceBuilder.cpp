/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/core/EventSequenceBuilder.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace vgmtrans::core {

namespace {

constexpr size_t kMaxExecutedCommandsPerTrack = 65536;

template <typename T>
void appendEvents(std::vector<Event>& destination, std::vector<T> events) {
  destination.insert(destination.end(),
                     std::make_move_iterator(events.begin()),
                     std::make_move_iterator(events.end()));
}

void purgeEndedPendingNotes(
    const std::vector<Event>& events,
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
    std::vector<Event>& events,
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

[[nodiscard]] std::optional<u64> firstLoopTick(
    const CommandSequence& commandSequence,
    const CommandTrack& track,
    const SequencerProfile& profile,
    u8 channel) {
  // Dry-run the track state to find the first musical loop without emitting events.
  const auto indexes = commandIndexByOffset(track);
  TrackState state{
      .trackIndex = track.sourceTrackNumber,
      .channel = channel,
      .globalTranspose = commandSequence.behavior.initialGlobalTranspose,
  };
  size_t pc = 0;
  size_t executedCommands = 0;
  std::unordered_set<u64> executedOffsets;

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
          } else if constexpr (std::is_same_v<TypedCommand, LfoCommand>) {
            static_cast<void>(profile.interpretLfo(typedCommand, state));
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

}  // namespace

void SequencerProfile::beginTrack(
    const CommandSequence&,
    const CommandTrack&,
    TrackState&,
    std::vector<Event>&) const {
}

u32 SequencerProfile::restTicks(const RestCommand& command, TrackState&) const {
  return command.rawDuration;
}

std::vector<Event> SequencerProfile::interpretNoteState(
    const NoteStateCommand&,
    TrackState&) const {
  return {};
}

NoteTiming SequencerProfile::noteTiming(const NoteCommand& command, TrackState& state) const {
  const auto key = std::clamp<s32>(static_cast<s32>(command.key) + state.transpose + state.globalTranspose, 0, 127);
  const auto ticks = command.rawDuration;
  return NoteTiming{
      .key = static_cast<u8>(key),
      .velocity = command.rawVelocity == 0 ? static_cast<u8>(127)
                                           : static_cast<u8>(std::min<u32>(command.rawVelocity, 127)),
      .soundingTicks = ticks,
      .advanceTicks = ticks,
  };
}

void SequencerProfile::applyDuration(const DurationCommand& command, TrackState& state) const {
  state.durationRate = command.rawValue;
}

void SequencerProfile::applyTranspose(const TransposeCommand& command, TrackState& state) const {
  state.transpose = command.rawSemitones;
}

std::vector<Event> SequencerProfile::interpretTempo(
    const TempoCommand& command,
    const TrackState& state) const {
  return {Tempo{
      .tick = state.tick,
      .microsecondsPerQuarter = command.rawValue == 0 ? 500000 : command.rawValue,
  }};
}

std::vector<Event> SequencerProfile::interpretProgram(
    const ProgramCommand& command,
    const TrackState& state) const {
  return {ProgramChange{
      .tick = state.tick,
      .channel = state.channel,
      .program = static_cast<u8>(std::min<u32>(command.rawProgram, 127)),
  }};
}

std::vector<Event> SequencerProfile::interpretVolume(
    const VolumeCommand& command,
    const TrackState& state) const {
  return {Volume{
      .tick = state.tick,
      .channel = state.channel,
      .value = static_cast<u8>(std::min<u32>(command.rawValue, 127)),
  }};
}

std::vector<Event> SequencerProfile::interpretPan(
    const PanCommand& command,
    const TrackState& state) const {
  return {Pan{
      .tick = state.tick,
      .channel = state.channel,
      .value = static_cast<u8>(std::min<u32>(command.rawValue, 127)),
  }};
}

std::vector<Event> SequencerProfile::interpretMasterVolume(
    const MasterVolumeCommand& command,
    const TrackState& state) const {
  return {MasterVolume{
      .tick = state.tick,
      .value = static_cast<u16>(std::min<u32>(command.rawValue, 0x3fff)),
  }};
}

std::vector<Event> SequencerProfile::interpretReverb(
    const ReverbCommand& command,
    const TrackState& state) const {
  return {Reverb{
      .tick = state.tick,
      .channel = state.channel,
      .value = static_cast<u8>(std::min<u32>(command.rawValue, 127)),
  }};
}

std::vector<Event> SequencerProfile::interpretTuning(
    const TuningCommand& command,
    const TrackState& state) const {
  return {FineTune{
      .tick = state.tick,
      .channel = state.channel,
      .cents = static_cast<double>(std::clamp<s32>(command.rawValue, -8192, 8191)),
  }};
}

std::vector<Event> SequencerProfile::interpretPortamento(
    const PortamentoCommand& command,
    TrackState& state) const {
  return {PortamentoTime{
      .tick = state.tick,
      .channel = state.channel,
      .value = static_cast<u8>(std::min<u32>(command.rawTime, 127)),
  }};
}

std::vector<Event> SequencerProfile::interpretLfo(
    const LfoCommand& command,
    TrackState& state) const {
  if (command.target == LfoTarget::Pitch) {
    return {VibratoDepth{
        .tick = state.tick,
        .channel = state.channel,
        .value = static_cast<u8>(std::min<u32>(command.rawAmount, 127)),
    }};
  }
  if (command.target == LfoTarget::Volume) {
    return {TremoloDepth{
        .tick = state.tick,
        .channel = state.channel,
        .value = static_cast<u8>(std::min<u32>(command.rawAmount, 127)),
    }};
  }
  return {};
}

std::vector<Event> SequencerProfile::interpretEnvelope(
    const EnvelopeCommand&,
    const TrackState&) const {
  return {};
}

std::vector<Event> SequencerProfile::interpretDriverSpecific(
    const DriverSpecificCommand&,
    TrackState&) const {
  return {};
}

std::vector<Event> SequencerProfile::interpretRepeatBreak(
    const RepeatBreakCommand&,
    TrackState&) const {
  return {};
}

EventSequence EventSequenceBuilder::build(
    const CommandSequence& commandSequence,
    const SequencerProfile& profile,
    LoopPolicy loopPolicy) const {
  if (loopPolicy == LoopPolicy::Default) {
    loopPolicy = commandSequence.behavior.defaultLoopPolicy;
  }
  if (loopPolicy == LoopPolicy::Default) {
    loopPolicy = LoopPolicy::PlayOnce;
  }

  EventSequence result{
      .timebase = commandSequence.timebase,
  };

  std::optional<u64> playOnceStopTick;
  std::vector<std::optional<u64>> firstLoopTicks(commandSequence.tracks.size());
  if (loopPolicy == LoopPolicy::PlayOnce) {
    // All tracks stop at the latest first-loop tick so short tracks do not truncate the song.
    for (size_t trackIndex = 0; trackIndex < commandSequence.tracks.size(); ++trackIndex) {
      const auto loopTick = firstLoopTick(commandSequence,
                                         commandSequence.tracks[trackIndex],
                                         profile,
                                         static_cast<u8>(trackIndex % 16));
      firstLoopTicks[trackIndex] = loopTick;
      if (loopTick.has_value() && (!playOnceStopTick.has_value() || *loopTick > *playOnceStopTick)) {
        playOnceStopTick = loopTick;
      }
    }
  }

  for (size_t trackIndex = 0; trackIndex < commandSequence.tracks.size(); ++trackIndex) {
    const auto& track = commandSequence.tracks[trackIndex];
    TrackState state{
        .trackIndex = static_cast<u32>(trackIndex),
        .channel = static_cast<u8>(trackIndex % 16),
        .globalTranspose = commandSequence.behavior.initialGlobalTranspose,
    };
    EventTrack eventTrack{
        .name = "Track " + std::to_string(track.sourceTrackNumber),
    };

    if (commandSequence.behavior.writeInitialMonoMode) {
      eventTrack.events.push_back(MonoMode{
          .tick = 0,
          .channel = state.channel,
          .channels = 0,
      });
    }
    if (commandSequence.behavior.writeInitialReverb) {
      eventTrack.events.push_back(Reverb{
          .tick = 0,
          .channel = state.channel,
          .value = commandSequence.behavior.initialReverb,
      });
    }
    profile.beginTrack(commandSequence, track, state, eventTrack.events);

    const auto indexes = commandIndexByOffset(track);
    size_t pc = 0;
    size_t executedCommands = 0;
    bool ended = false;
    std::optional<u64> loopPlaybackStopTick;
    if (loopPolicy == LoopPolicy::PlayOnce && playOnceStopTick.has_value() && firstLoopTicks[trackIndex].has_value()) {
      loopPlaybackStopTick = playOnceStopTick;
    }
    std::vector<size_t> pendingNoteIndexes;
    std::unordered_set<u64> executedOffsets;
    while (pc < track.commands.size() && executedCommands++ < kMaxExecutedCommandsPerTrack) {
      if (loopPlaybackStopTick.has_value() && state.tick >= *loopPlaybackStopTick) {
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
              if (loopPlaybackStopTick.has_value() && soundingTicks > timing.advanceTicks + 1) {
                const u64 stopEndTick = *loopPlaybackStopTick + 1;
                if (state.tick < stopEndTick && state.tick + soundingTicks > stopEndTick) {
                  soundingTicks = static_cast<u32>(stopEndTick - state.tick);
                }
              }
              appendEvents(eventTrack.events, std::move(timing.beforeEvents));
              if (timing.extendsPrevious) {
                // Slurred notes extend the existing note event instead of starting a new note.
                extendPendingNotes(eventTrack.events, pendingNoteIndexes, state.tick + soundingTicks);
              } else {
                purgeEndedPendingNotes(eventTrack.events, pendingNoteIndexes, state.tick);
                const size_t noteIndex = eventTrack.events.size();
                eventTrack.events.push_back(NoteDuration{
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
            } else if constexpr (std::is_same_v<TypedCommand, NoteStateCommand>) {
              appendEvents(eventTrack.events, profile.interpretNoteState(typedCommand, state));
            } else if constexpr (std::is_same_v<TypedCommand, DurationCommand>) {
              profile.applyDuration(typedCommand, state);
            } else if constexpr (std::is_same_v<TypedCommand, TransposeCommand>) {
              profile.applyTranspose(typedCommand, state);
            } else if constexpr (std::is_same_v<TypedCommand, GlobalTransposeCommand>) {
              state.globalTranspose = typedCommand.rawSemitones;
            } else if constexpr (std::is_same_v<TypedCommand, TempoCommand>) {
              appendEvents(eventTrack.events, profile.interpretTempo(typedCommand, state));
            } else if constexpr (std::is_same_v<TypedCommand, ProgramCommand>) {
              appendEvents(eventTrack.events, profile.interpretProgram(typedCommand, state));
            } else if constexpr (std::is_same_v<TypedCommand, VolumeCommand>) {
              appendEvents(eventTrack.events, profile.interpretVolume(typedCommand, state));
            } else if constexpr (std::is_same_v<TypedCommand, PanCommand>) {
              appendEvents(eventTrack.events, profile.interpretPan(typedCommand, state));
            } else if constexpr (std::is_same_v<TypedCommand, MasterVolumeCommand>) {
              appendEvents(eventTrack.events, profile.interpretMasterVolume(typedCommand, state));
            } else if constexpr (std::is_same_v<TypedCommand, ReverbCommand>) {
              appendEvents(eventTrack.events, profile.interpretReverb(typedCommand, state));
            } else if constexpr (std::is_same_v<TypedCommand, TuningCommand>) {
              appendEvents(eventTrack.events, profile.interpretTuning(typedCommand, state));
            } else if constexpr (std::is_same_v<TypedCommand, PortamentoCommand>) {
              appendEvents(eventTrack.events, profile.interpretPortamento(typedCommand, state));
            } else if constexpr (std::is_same_v<TypedCommand, LfoCommand>) {
              appendEvents(eventTrack.events, profile.interpretLfo(typedCommand, state));
            } else if constexpr (std::is_same_v<TypedCommand, EnvelopeCommand>) {
              appendEvents(eventTrack.events, profile.interpretEnvelope(typedCommand, state));
            } else if constexpr (std::is_same_v<TypedCommand, DriverSpecificCommand>) {
              appendEvents(eventTrack.events, profile.interpretDriverSpecific(typedCommand, state));
            } else if constexpr (std::is_same_v<TypedCommand, RepeatCommand>) {
              if (typedCommand.slot >= state.repeatCounters.size()) {
                result.diagnostics.push_back(warning("Repeat command uses an unsupported repeat slot",
                                                     typedCommand.range));
                return;
              }
              auto& counter = state.repeatCounters[typedCommand.slot];
              if (typedCommand.count == 0 && counter == 0) {
                eventTrack.events.push_back(Marker{.tick = state.tick, .text = "Loop"});
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
                appendEvents(eventTrack.events, profile.interpretRepeatBreak(typedCommand, state));
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
                  if (state.tick == 0 || !playOnceStopTick.has_value() || state.tick >= *playOnceStopTick) {
                    ended = true;
                    return;
                  }
                  loopPlaybackStopTick = playOnceStopTick;
                }
                if (loopPolicy != LoopPolicy::PlayOnce) {
                  eventTrack.events.push_back(Marker{.tick = state.tick, .text = "Jump"});
                }
                pc = *target;
                incrementPc = false;
              } else {
                result.diagnostics.push_back(warning("Jump destination was not decoded", typedCommand.range));
                ended = true;
              }
            } else if constexpr (std::is_same_v<TypedCommand, LoopBoundaryCommand>) {
              if (loopPolicy == LoopPolicy::PlayOnce) {
                if (const auto target = destinationIndex(indexes, typedCommand.destination);
                    target.has_value() && playOnceStopTick.has_value() && state.tick < *playOnceStopTick &&
                    *target < pc) {
                  loopPlaybackStopTick = playOnceStopTick;
                  pc = *target;
                  incrementPc = false;
                  return;
                }
              }
              if (loopPolicy != LoopPolicy::PlayOnce) {
                eventTrack.events.push_back(Marker{.tick = state.tick, .text = "Loop"});
              }
              ended = true;
            } else if constexpr (std::is_same_v<TypedCommand, EndCommand>) {
              eventTrack.events.push_back(EndOfTrack{.tick = state.tick});
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

      if (loopPlaybackStopTick.has_value() && state.tick >= *loopPlaybackStopTick) {
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

    if (executedCommands >= kMaxExecutedCommandsPerTrack) {
      const auto range = track.commands.empty() ? SourceRange{} : commandRange(track.commands.back());
      result.diagnostics.push_back(warning("Event sequence building stopped after too many executed commands", range));
    }
    if (eventTrack.events.empty() || !std::holds_alternative<EndOfTrack>(eventTrack.events.back())) {
      eventTrack.events.push_back(EndOfTrack{.tick = state.tick});
    }
    result.tracks.push_back(std::move(eventTrack));
  }

  return result;
}

void SequencerProfileRegistry::add(std::string format, Factory factory) {
  if (format.empty()) {
    throw std::invalid_argument("Cannot register a SequencerProfile with an empty format name");
  }
  if (!factory) {
    throw std::invalid_argument("Cannot register an empty SequencerProfile factory");
  }
  factories_[std::move(format)] = std::move(factory);
}

std::unique_ptr<SequencerProfile> SequencerProfileRegistry::create(std::string_view format) const {
  const auto found = factories_.find(std::string(format));
  if (found == factories_.end()) {
    return nullptr;
  }
  return found->second();
}

bool SequencerProfileRegistry::contains(std::string_view format) const {
  return factories_.contains(std::string(format));
}

}  // namespace vgmtrans::core
