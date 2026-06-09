/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "core/PerformanceLowerer.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace vgmtrans::core {

namespace {

constexpr size_t kMaxExecutedCommandsPerTrack = 65536;

template <typename T>
void appendEvents(std::vector<PerformanceEvent>& destination, std::vector<T> events) {
  destination.insert(destination.end(),
                     std::make_move_iterator(events.begin()),
                     std::make_move_iterator(events.end()));
}

bool extendPreviousNote(std::vector<PerformanceEvent>& events, u8 channel, u8 key, u64 endTick) {
  for (auto event = events.rbegin(); event != events.rend(); ++event) {
    auto* note = std::get_if<NoteDuration>(&*event);
    if (note == nullptr) {
      continue;
    }
    if (note->channel == channel && note->key == key) {
      if (endTick > note->tick) {
        note->duration = static_cast<u32>(std::max<u64>(note->duration, endTick - note->tick));
      }
      return true;
    }
  }
  return false;
}

[[nodiscard]] SourceRange commandRange(const SequencerCommand& command) {
  return std::visit([](const auto& typedCommand) { return typedCommand.range; }, command);
}

[[nodiscard]] Diagnostic warning(std::string message, SourceRange range) {
  return Diagnostic{
      .severity = Severity::Warning,
      .message = std::move(message),
      .range = range,
  };
}

[[nodiscard]] std::unordered_map<u64, size_t> commandIndexByOffset(const TrackProgram& track) {
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

[[nodiscard]] std::optional<u64> firstLoopTick(
    const SequenceProgram& program,
    const TrackProgram& track,
    const SequencerProfile& profile,
    u8 channel) {
  const auto indexes = commandIndexByOffset(track);
  TrackState state{
      .trackIndex = track.sourceTrackNumber,
      .channel = channel,
      .globalTranspose = program.behavior.initialGlobalTranspose,
  };
  size_t pc = 0;
  size_t executedCommands = 0;

  while (pc < track.commands.size() && executedCommands++ < kMaxExecutedCommandsPerTrack) {
    const auto& command = track.commands[pc];
    bool incrementPc = true;
    bool ended = false;
    std::optional<u64> loopTick;

    std::visit(
        [&](const auto& typedCommand) {
          using Command = std::decay_t<decltype(typedCommand)>;
          if constexpr (std::is_same_v<Command, NoteCommand>) {
            state.tick += profile.noteTiming(typedCommand, state).advanceTicks;
          } else if constexpr (std::is_same_v<Command, RestCommand>) {
            state.tick += profile.restTicks(typedCommand, state);
          } else if constexpr (std::is_same_v<Command, DurationCommand>) {
            profile.applyDuration(typedCommand, state);
          } else if constexpr (std::is_same_v<Command, TransposeCommand>) {
            profile.applyTranspose(typedCommand, state);
          } else if constexpr (std::is_same_v<Command, GlobalTransposeCommand>) {
            state.globalTranspose = typedCommand.rawSemitones;
          } else if constexpr (std::is_same_v<Command, PortamentoCommand>) {
            static_cast<void>(profile.lowerPortamento(typedCommand, state));
          } else if constexpr (std::is_same_v<Command, LfoCommand>) {
            static_cast<void>(profile.lowerLfo(typedCommand, state));
          } else if constexpr (std::is_same_v<Command, DriverSpecificCommand>) {
            static_cast<void>(profile.lowerDriverSpecific(typedCommand, state));
          } else if constexpr (std::is_same_v<Command, RepeatCommand>) {
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
          } else if constexpr (std::is_same_v<Command, RepeatBreakCommand>) {
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
          } else if constexpr (std::is_same_v<Command, JumpCommand>) {
            if (const auto target = destinationIndex(indexes, typedCommand.destination)) {
              if (*target <= pc) {
                loopTick = state.tick;
                ended = true;
                return;
              }
              pc = *target;
              incrementPc = false;
            } else {
              ended = true;
            }
          } else if constexpr (std::is_same_v<Command, EndCommand>) {
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
  }

  return std::nullopt;
}

}  // namespace

void SequencerProfile::beginTrack(
    const SequenceProgram&,
    const TrackProgram&,
    TrackState&,
    std::vector<PerformanceEvent>&) const {
}

u32 SequencerProfile::restTicks(const RestCommand& command, TrackState&) const {
  return command.rawDuration;
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

std::vector<PerformanceEvent> SequencerProfile::lowerTempo(
    const TempoCommand& command,
    const TrackState& state) const {
  return {Tempo{
      .tick = state.tick,
      .microsecondsPerQuarter = command.rawValue == 0 ? 500000 : command.rawValue,
  }};
}

std::vector<PerformanceEvent> SequencerProfile::lowerProgram(
    const ProgramCommand& command,
    const TrackState& state) const {
  return {ProgramChange{
      .tick = state.tick,
      .channel = state.channel,
      .program = static_cast<u8>(std::min<u32>(command.rawProgram, 127)),
  }};
}

std::vector<PerformanceEvent> SequencerProfile::lowerVolume(
    const VolumeCommand& command,
    const TrackState& state) const {
  return {Volume{
      .tick = state.tick,
      .channel = state.channel,
      .value = static_cast<u8>(std::min<u32>(command.rawValue, 127)),
  }};
}

std::vector<PerformanceEvent> SequencerProfile::lowerPan(
    const PanCommand& command,
    const TrackState& state) const {
  return {Pan{
      .tick = state.tick,
      .channel = state.channel,
      .value = static_cast<u8>(std::min<u32>(command.rawValue, 127)),
  }};
}

std::vector<PerformanceEvent> SequencerProfile::lowerMasterVolume(
    const MasterVolumeCommand& command,
    const TrackState& state) const {
  return {MasterVolume{
      .tick = state.tick,
      .value = static_cast<u16>(std::min<u32>(command.rawValue, 0x3fff)),
  }};
}

std::vector<PerformanceEvent> SequencerProfile::lowerReverb(
    const ReverbCommand& command,
    const TrackState& state) const {
  return {Reverb{
      .tick = state.tick,
      .channel = state.channel,
      .value = static_cast<u8>(std::min<u32>(command.rawValue, 127)),
  }};
}

std::vector<PerformanceEvent> SequencerProfile::lowerTuning(
    const TuningCommand& command,
    const TrackState& state) const {
  return {FineTune{
      .tick = state.tick,
      .channel = state.channel,
      .cents = static_cast<double>(std::clamp<s32>(command.rawValue, -8192, 8191)),
  }};
}

std::vector<PerformanceEvent> SequencerProfile::lowerPortamento(
    const PortamentoCommand& command,
    TrackState& state) const {
  return {PortamentoTime{
      .tick = state.tick,
      .channel = state.channel,
      .value = static_cast<u8>(std::min<u32>(command.rawTime, 127)),
  }};
}

std::vector<PerformanceEvent> SequencerProfile::lowerLfo(
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

std::vector<PerformanceEvent> SequencerProfile::lowerEnvelope(
    const EnvelopeCommand&,
    const TrackState&) const {
  return {};
}

std::vector<PerformanceEvent> SequencerProfile::lowerDriverSpecific(
    const DriverSpecificCommand&,
    TrackState&) const {
  return {};
}

std::vector<PerformanceEvent> SequencerProfile::lowerRepeatBreak(
    const RepeatBreakCommand&,
    TrackState&) const {
  return {};
}

PerformanceSequence PerformanceLowerer::lower(
    const SequenceProgram& program,
    const SequencerProfile& profile,
    LoopPolicy loopPolicy) const {
  if (loopPolicy == LoopPolicy::Default) {
    loopPolicy = program.behavior.defaultLoopPolicy;
  }

  PerformanceSequence result{
      .timebase = program.timebase,
  };

  std::optional<u64> playOnceStopTick;
  if (loopPolicy == LoopPolicy::PlayOnce) {
    for (size_t trackIndex = 0; trackIndex < program.tracks.size(); ++trackIndex) {
      const auto loopTick = firstLoopTick(program,
                                         program.tracks[trackIndex],
                                         profile,
                                         static_cast<u8>(trackIndex % 16));
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
    PerformanceTrack loweredTrack{
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
    while (pc < track.commands.size() && executedCommands++ < kMaxExecutedCommandsPerTrack) {
      const auto& command = track.commands[pc];
      bool incrementPc = true;

      std::visit(
          [&](const auto& typedCommand) {
            using Command = std::decay_t<decltype(typedCommand)>;
            if constexpr (std::is_same_v<Command, NoteCommand>) {
              auto timing = profile.noteTiming(typedCommand, state);
              u32 soundingTicks = timing.soundingTicks;
              if (loopPlaybackStopTick.has_value() && soundingTicks > timing.advanceTicks + 1) {
                const u64 stopEndTick = *loopPlaybackStopTick + 1;
                if (state.tick < stopEndTick && state.tick + soundingTicks > stopEndTick) {
                  soundingTicks = static_cast<u32>(stopEndTick - state.tick);
                }
              }
              appendEvents(loweredTrack.events, std::move(timing.beforeEvents));
              if (!timing.extendsPrevious ||
                  !extendPreviousNote(loweredTrack.events,
                                      state.channel,
                                      timing.key,
                                      state.tick + soundingTicks)) {
                loweredTrack.events.push_back(NoteDuration{
                    .tick = state.tick,
                    .channel = state.channel,
                    .key = timing.key,
                    .velocity = timing.velocity,
                    .duration = soundingTicks,
                });
              }
              state.tick += timing.advanceTicks;
            } else if constexpr (std::is_same_v<Command, RestCommand>) {
              state.tick += profile.restTicks(typedCommand, state);
            } else if constexpr (std::is_same_v<Command, DurationCommand>) {
              profile.applyDuration(typedCommand, state);
            } else if constexpr (std::is_same_v<Command, TransposeCommand>) {
              profile.applyTranspose(typedCommand, state);
            } else if constexpr (std::is_same_v<Command, GlobalTransposeCommand>) {
              state.globalTranspose = typedCommand.rawSemitones;
            } else if constexpr (std::is_same_v<Command, TempoCommand>) {
              appendEvents(loweredTrack.events, profile.lowerTempo(typedCommand, state));
            } else if constexpr (std::is_same_v<Command, ProgramCommand>) {
              appendEvents(loweredTrack.events, profile.lowerProgram(typedCommand, state));
            } else if constexpr (std::is_same_v<Command, VolumeCommand>) {
              appendEvents(loweredTrack.events, profile.lowerVolume(typedCommand, state));
            } else if constexpr (std::is_same_v<Command, PanCommand>) {
              appendEvents(loweredTrack.events, profile.lowerPan(typedCommand, state));
            } else if constexpr (std::is_same_v<Command, MasterVolumeCommand>) {
              appendEvents(loweredTrack.events, profile.lowerMasterVolume(typedCommand, state));
            } else if constexpr (std::is_same_v<Command, ReverbCommand>) {
              appendEvents(loweredTrack.events, profile.lowerReverb(typedCommand, state));
            } else if constexpr (std::is_same_v<Command, TuningCommand>) {
              appendEvents(loweredTrack.events, profile.lowerTuning(typedCommand, state));
            } else if constexpr (std::is_same_v<Command, PortamentoCommand>) {
              appendEvents(loweredTrack.events, profile.lowerPortamento(typedCommand, state));
            } else if constexpr (std::is_same_v<Command, LfoCommand>) {
              appendEvents(loweredTrack.events, profile.lowerLfo(typedCommand, state));
            } else if constexpr (std::is_same_v<Command, EnvelopeCommand>) {
              appendEvents(loweredTrack.events, profile.lowerEnvelope(typedCommand, state));
            } else if constexpr (std::is_same_v<Command, DriverSpecificCommand>) {
              appendEvents(loweredTrack.events, profile.lowerDriverSpecific(typedCommand, state));
            } else if constexpr (std::is_same_v<Command, RepeatCommand>) {
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
            } else if constexpr (std::is_same_v<Command, RepeatBreakCommand>) {
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
            } else if constexpr (std::is_same_v<Command, JumpCommand>) {
              if (const auto target = destinationIndex(indexes, typedCommand.destination)) {
                if (loopPolicy == LoopPolicy::PlayOnce && *target <= pc) {
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
            } else if constexpr (std::is_same_v<Command, EndCommand>) {
              loweredTrack.events.push_back(EndOfTrack{.tick = state.tick});
              ended = true;
            } else if constexpr (std::is_same_v<Command, UnknownCommand>) {
              result.diagnostics.push_back(warning("Unknown sequencer command was skipped", typedCommand.range));
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
