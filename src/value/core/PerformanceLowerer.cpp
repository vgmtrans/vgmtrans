/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/core/PerformanceLowerer.h"

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
    const CommandSequence& program,
    const CommandTrack& track,
    const SequencerProfile& profile,
    u8 channel) {
  // Dry-run the track state to find the first musical loop without emitting events.
  const auto indexes = commandIndexByOffset(track);
  TrackState state{
      .trackIndex = track.sourceTrackNumber,
      .channel = channel,
      .globalTranspose = program.behavior.initialGlobalTranspose,
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
            static_cast<void>(profile.lowerNoteState(typedCommand, state));
          } else if constexpr (std::is_same_v<TypedCommand, DurationCommand>) {
            profile.applyDuration(typedCommand, state);
          } else if constexpr (std::is_same_v<TypedCommand, TransposeCommand>) {
            profile.applyTranspose(typedCommand, state);
          } else if constexpr (std::is_same_v<TypedCommand, GlobalTransposeCommand>) {
            state.globalTranspose = typedCommand.rawSemitones;
          } else if constexpr (std::is_same_v<TypedCommand, PortamentoCommand>) {
            static_cast<void>(profile.lowerPortamento(typedCommand, state));
          } else if constexpr (std::is_same_v<TypedCommand, LfoCommand>) {
            static_cast<void>(profile.lowerLfo(typedCommand, state));
          } else if constexpr (std::is_same_v<TypedCommand, DriverSpecificCommand>) {
            static_cast<void>(profile.lowerDriverSpecific(typedCommand, state));
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
              static_cast<void>(profile.lowerRepeatBreak(typedCommand, state));
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

std::vector<Event> SequencerProfile::lowerNoteState(
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

std::vector<Event> SequencerProfile::lowerTempo(
    const TempoCommand& command,
    const TrackState& state) const {
  return {Tempo{
      .tick = state.tick,
      .microsecondsPerQuarter = command.rawValue == 0 ? 500000 : command.rawValue,
  }};
}

std::vector<Event> SequencerProfile::lowerProgram(
    const ProgramCommand& command,
    const TrackState& state) const {
  return {ProgramChange{
      .tick = state.tick,
      .channel = state.channel,
      .program = static_cast<u8>(std::min<u32>(command.rawProgram, 127)),
  }};
}

std::vector<Event> SequencerProfile::lowerVolume(
    const VolumeCommand& command,
    const TrackState& state) const {
  return {Volume{
      .tick = state.tick,
      .channel = state.channel,
      .value = static_cast<u8>(std::min<u32>(command.rawValue, 127)),
  }};
}

std::vector<Event> SequencerProfile::lowerPan(
    const PanCommand& command,
    const TrackState& state) const {
  return {Pan{
      .tick = state.tick,
      .channel = state.channel,
      .value = static_cast<u8>(std::min<u32>(command.rawValue, 127)),
  }};
}

std::vector<Event> SequencerProfile::lowerMasterVolume(
    const MasterVolumeCommand& command,
    const TrackState& state) const {
  return {MasterVolume{
      .tick = state.tick,
      .value = static_cast<u16>(std::min<u32>(command.rawValue, 0x3fff)),
  }};
}

std::vector<Event> SequencerProfile::lowerReverb(
    const ReverbCommand& command,
    const TrackState& state) const {
  return {Reverb{
      .tick = state.tick,
      .channel = state.channel,
      .value = static_cast<u8>(std::min<u32>(command.rawValue, 127)),
  }};
}

std::vector<Event> SequencerProfile::lowerTuning(
    const TuningCommand& command,
    const TrackState& state) const {
  return {FineTune{
      .tick = state.tick,
      .channel = state.channel,
      .cents = static_cast<double>(std::clamp<s32>(command.rawValue, -8192, 8191)),
  }};
}

std::vector<Event> SequencerProfile::lowerPortamento(
    const PortamentoCommand& command,
    TrackState& state) const {
  return {PortamentoTime{
      .tick = state.tick,
      .channel = state.channel,
      .value = static_cast<u8>(std::min<u32>(command.rawTime, 127)),
  }};
}

std::vector<Event> SequencerProfile::lowerLfo(
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

std::vector<Event> SequencerProfile::lowerEnvelope(
    const EnvelopeCommand&,
    const TrackState&) const {
  return {};
}

std::vector<Event> SequencerProfile::lowerDriverSpecific(
    const DriverSpecificCommand&,
    TrackState&) const {
  return {};
}

std::vector<Event> SequencerProfile::lowerRepeatBreak(
    const RepeatBreakCommand&,
    TrackState&) const {
  return {};
}

EventSequence PerformanceLowerer::lower(
    const CommandSequence& program,
    const SequencerProfile& profile,
    LoopPolicy loopPolicy) const {
  if (loopPolicy == LoopPolicy::Default) {
    loopPolicy = program.behavior.defaultLoopPolicy;
  }
  if (loopPolicy == LoopPolicy::Default) {
    loopPolicy = LoopPolicy::PlayOnce;
  }

  EventSequence result{
      .timebase = program.timebase,
  };

  std::optional<u64> playOnceStopTick;
  std::vector<std::optional<u64>> firstLoopTicks(program.tracks.size());
  if (loopPolicy == LoopPolicy::PlayOnce) {
    // All tracks stop at the latest first-loop tick so short tracks do not truncate the song.
    for (size_t trackIndex = 0; trackIndex < program.tracks.size(); ++trackIndex) {
      const auto loopTick = firstLoopTick(program,
                                         program.tracks[trackIndex],
                                         profile,
                                         static_cast<u8>(trackIndex % 16));
      firstLoopTicks[trackIndex] = loopTick;
      if (loopTick.has_value() && (!playOnceStopTick.has_value() || *loopTick > *playOnceStopTick)) {
        playOnceStopTick = loopTick;
      }
    }
  }

  for (size_t trackIndex = 0; trackIndex < program.tracks.size(); ++trackIndex) {
    const auto& track = program.tracks[trackIndex];
    TrackState state{
        .trackIndex = static_cast<u32>(trackIndex),
        .channel = static_cast<u8>(trackIndex % 16),
        .globalTranspose = program.behavior.initialGlobalTranspose,
    };
    EventTrack loweredTrack{
        .name = "Track " + std::to_string(track.sourceTrackNumber),
    };

    if (program.behavior.writeInitialMonoMode) {
      loweredTrack.events.push_back(MonoMode{
          .tick = 0,
          .channel = state.channel,
          .channels = 0,
      });
    }
    if (program.behavior.writeInitialReverb) {
      loweredTrack.events.push_back(Reverb{
          .tick = 0,
          .channel = state.channel,
          .value = program.behavior.initialReverb,
      });
    }
    profile.beginTrack(program, track, state, loweredTrack.events);

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
              appendEvents(loweredTrack.events, std::move(timing.beforeEvents));
              if (timing.extendsPrevious) {
                // Slurred notes extend the existing note event instead of starting a new note.
                extendPendingNotes(loweredTrack.events, pendingNoteIndexes, state.tick + soundingTicks);
              } else {
                purgeEndedPendingNotes(loweredTrack.events, pendingNoteIndexes, state.tick);
                const size_t noteIndex = loweredTrack.events.size();
                loweredTrack.events.push_back(NoteDuration{
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
              appendEvents(loweredTrack.events, profile.lowerNoteState(typedCommand, state));
            } else if constexpr (std::is_same_v<TypedCommand, DurationCommand>) {
              profile.applyDuration(typedCommand, state);
            } else if constexpr (std::is_same_v<TypedCommand, TransposeCommand>) {
              profile.applyTranspose(typedCommand, state);
            } else if constexpr (std::is_same_v<TypedCommand, GlobalTransposeCommand>) {
              state.globalTranspose = typedCommand.rawSemitones;
            } else if constexpr (std::is_same_v<TypedCommand, TempoCommand>) {
              appendEvents(loweredTrack.events, profile.lowerTempo(typedCommand, state));
            } else if constexpr (std::is_same_v<TypedCommand, ProgramCommand>) {
              appendEvents(loweredTrack.events, profile.lowerProgram(typedCommand, state));
            } else if constexpr (std::is_same_v<TypedCommand, VolumeCommand>) {
              appendEvents(loweredTrack.events, profile.lowerVolume(typedCommand, state));
            } else if constexpr (std::is_same_v<TypedCommand, PanCommand>) {
              appendEvents(loweredTrack.events, profile.lowerPan(typedCommand, state));
            } else if constexpr (std::is_same_v<TypedCommand, MasterVolumeCommand>) {
              appendEvents(loweredTrack.events, profile.lowerMasterVolume(typedCommand, state));
            } else if constexpr (std::is_same_v<TypedCommand, ReverbCommand>) {
              appendEvents(loweredTrack.events, profile.lowerReverb(typedCommand, state));
            } else if constexpr (std::is_same_v<TypedCommand, TuningCommand>) {
              appendEvents(loweredTrack.events, profile.lowerTuning(typedCommand, state));
            } else if constexpr (std::is_same_v<TypedCommand, PortamentoCommand>) {
              appendEvents(loweredTrack.events, profile.lowerPortamento(typedCommand, state));
            } else if constexpr (std::is_same_v<TypedCommand, LfoCommand>) {
              appendEvents(loweredTrack.events, profile.lowerLfo(typedCommand, state));
            } else if constexpr (std::is_same_v<TypedCommand, EnvelopeCommand>) {
              appendEvents(loweredTrack.events, profile.lowerEnvelope(typedCommand, state));
            } else if constexpr (std::is_same_v<TypedCommand, DriverSpecificCommand>) {
              appendEvents(loweredTrack.events, profile.lowerDriverSpecific(typedCommand, state));
            } else if constexpr (std::is_same_v<TypedCommand, RepeatCommand>) {
              if (typedCommand.slot >= state.repeatCounters.size()) {
                result.diagnostics.push_back(warning("Repeat command uses an unsupported repeat slot",
                                                     typedCommand.range));
                return;
              }
              auto& counter = state.repeatCounters[typedCommand.slot];
              if (typedCommand.count == 0 && counter == 0) {
                loweredTrack.events.push_back(Marker{.tick = state.tick, .text = "Loop"});
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
                appendEvents(loweredTrack.events, profile.lowerRepeatBreak(typedCommand, state));
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
                  loweredTrack.events.push_back(Marker{.tick = state.tick, .text = "Jump"});
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
                loweredTrack.events.push_back(Marker{.tick = state.tick, .text = "Loop"});
              }
              ended = true;
            } else if constexpr (std::is_same_v<TypedCommand, EndCommand>) {
              loweredTrack.events.push_back(EndOfTrack{.tick = state.tick});
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
      result.diagnostics.push_back(warning("Track lowering stopped after too many executed commands", range));
    }
    if (loweredTrack.events.empty() || !std::holds_alternative<EndOfTrack>(loweredTrack.events.back())) {
      loweredTrack.events.push_back(EndOfTrack{.tick = state.tick});
    }
    result.tracks.push_back(std::move(loweredTrack));
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
