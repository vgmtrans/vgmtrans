/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/sequence/SequenceVm.h"

#include <any>
#include <algorithm>
#include <fmt/format.h>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace vgmtrans::core {

struct RepeatReplayWindow {
  u32 stopIndex = 0;
  bool hasSourceWindow = false;
  u64 beginOffset = 0;
  u64 endOffset = 0;
};

struct VmTrackRuntime {
  u64 tick = 0;
  std::vector<u32> callStack;
  std::map<u8, u32> repeatRemaining;
  std::optional<RepeatReplayWindow> repeatReplayWindow;
  CommandId lastCommand;
};

namespace {

constexpr u32 kFallbackCommandLimit = 100000;

[[nodiscard]] Diagnostic vmWarning(std::string message, SourceRange range) {
  return Diagnostic{
      .severity = Severity::Warning,
      .message = std::move(message),
      .range = range.valid() ? std::optional<SourceRange>{range} : std::nullopt,
  };
}

[[nodiscard]] std::optional<u32> nextCommandIndex(const TrackProgram& track, u32 index) {
  if (index < track.commands.size()) {
    const SourceCommand& command = track.commands[index];
    if (command.encodedSize > 0) {
      if (command.address.value > std::numeric_limits<u64>::max() - command.encodedSize) {
        return std::nullopt;
      }
      if (const auto byAddress = track.addressIndex.find(Address{command.address.value + command.encodedSize})) {
        return byAddress;
      }
    }
  }

  const u32 next = index + 1;
  if (next >= track.commands.size()) {
    return std::nullopt;
  }
  return next;
}

[[nodiscard]] std::optional<u32> destinationIndex(const TrackProgram& track, Address destination) {
  return track.addressIndex.find(destination);
}

[[nodiscard]] bool isReplayingRepeat(const VmTrackRuntime& runtime, u32 currentIndex, const SourceCommand& command) {
  if (!runtime.repeatReplayWindow) {
    return false;
  }

  const RepeatReplayWindow& window = *runtime.repeatReplayWindow;
  if (window.hasSourceWindow && command.range.valid()) {
    return command.range.offset >= window.beginOffset && command.range.offset < window.endOffset;
  }

  // Synthetic tests do not always use source ranges. Keep the older index
  // fallback for those programs, but real bytecode should use the source window
  // because decoded command order can differ from source-address order.
  return currentIndex <= window.stopIndex;
}

struct VisitState {
  u32 commandIndex = 0;
  std::vector<u32> callStack;
  std::map<u8, u32> repeatRemaining;

  friend bool operator<(const VisitState& lhs, const VisitState& rhs) {
    return std::tie(lhs.commandIndex, lhs.callStack, lhs.repeatRemaining) <
           std::tie(rhs.commandIndex, rhs.callStack, rhs.repeatRemaining);
  }
};

struct VisitRecord {
  u64 tick = 0;
  CommandId command;
};

void addLoopMarker(PerformanceTrack& track, CommandId sourceCommand, u64 tick, std::string text) {
  track.events.emplace_back(MarkerPerformanceEvent{
      .header =
          PerformanceEventHeader{
              .sourceCommand = sourceCommand,
              .track = track.id,
              .tick = tick,
          },
      .text = std::move(text),
  });
}

void addInitialTrackEvents(PerformanceTrack& track, const SequenceProgramBehavior& behavior) {
  const PerformanceEventHeader header{
      .track = track.id,
      .tick = 0,
  };

  if (behavior.initialReverbSend) {
    track.events.emplace_back(ReverbPerformanceEvent{
        .header = header,
        .send = *behavior.initialReverbSend,
    });
  }
  if (behavior.initialMonoModeChannels) {
    track.events.emplace_back(MonoModePerformanceEvent{
        .header = header,
        .channels = *behavior.initialMonoModeChannels,
    });
  }
}

}  // namespace

Emit::Emit(PerformanceTrack& track, CommandId sourceCommand, u64 tick)
    : track_(track), sourceCommand_(sourceCommand), tick_(tick) {
}

void Emit::note(NotePerformanceEvent event) {
  event.header = header();
  track_.events.emplace_back(std::move(event));
}

void Emit::note(double key, double linearVelocity, u32 durationTicks, bool extendsPrevious) {
  note(NotePerformanceEvent{
      .key = key,
      .linearVelocity = linearVelocity,
      .durationTicks = durationTicks,
      .extendsPrevious = extendsPrevious,
  });
}

void Emit::tempo(TempoPerformanceEvent event) {
  event.header = header();
  track_.events.emplace_back(std::move(event));
}

void Emit::tempo(u32 microsecondsPerQuarter) {
  tempo(TempoPerformanceEvent{
      .microsecondsPerQuarter = microsecondsPerQuarter,
  });
}

void Emit::instrument(InstrumentPerformanceEvent event) {
  event.header = header();
  track_.events.emplace_back(std::move(event));
}

void Emit::instrument(u32 bank, u32 program, bool forceBankSelect) {
  instrument(InstrumentPerformanceEvent{
      .bank = bank,
      .program = program,
      .forceBankSelect = forceBankSelect,
  });
}

void Emit::level(LevelPerformanceEvent event) {
  event.header = header();
  track_.events.emplace_back(std::move(event));
}

void Emit::level(double linearGain, LevelPrecisionHint precisionHint) {
  level(LevelPerformanceEvent{
      .linearGain = linearGain,
      .precisionHint = precisionHint,
  });
}

void Emit::expression(ExpressionPerformanceEvent event) {
  event.header = header();
  track_.events.emplace_back(std::move(event));
}

void Emit::expression(double linearGain, LevelPrecisionHint precisionHint) {
  expression(ExpressionPerformanceEvent{
      .linearGain = linearGain,
      .precisionHint = precisionHint,
  });
}

void Emit::pan(PanPerformanceEvent event) {
  event.header = header();
  track_.events.emplace_back(std::move(event));
}

void Emit::pan(double stereoPosition, double linearGain) {
  pan(PanPerformanceEvent{
      .stereoPosition = stereoPosition,
      .linearGain = linearGain,
  });
}

void Emit::masterLevel(MasterLevelPerformanceEvent event) {
  event.header = header();
  track_.events.emplace_back(std::move(event));
}

void Emit::masterLevel(double linearGain) {
  masterLevel(MasterLevelPerformanceEvent{
      .linearGain = linearGain,
  });
}

void Emit::reverb(ReverbPerformanceEvent event) {
  event.header = header();
  track_.events.emplace_back(std::move(event));
}

void Emit::reverb(double send) {
  reverb(ReverbPerformanceEvent{
      .send = send,
  });
}

void Emit::tuning(TuningPerformanceEvent event) {
  event.header = header();
  track_.events.emplace_back(std::move(event));
}

void Emit::tuning(double cents) {
  tuning(TuningPerformanceEvent{
      .cents = cents,
  });
}

void Emit::globalTranspose(GlobalTransposePerformanceEvent event) {
  event.header = header();
  track_.events.emplace_back(std::move(event));
}

void Emit::globalTranspose(s32 semitones) {
  globalTranspose(GlobalTransposePerformanceEvent{
      .semitones = semitones,
  });
}

void Emit::pitchBend(PitchBendPerformanceEvent event) {
  event.header = header();
  track_.events.emplace_back(std::move(event));
}

void Emit::pitchBend(s16 value) {
  pitchBend(PitchBendPerformanceEvent{
      .value = value,
  });
}

void Emit::pitchBendRange(PitchBendRangePerformanceEvent event) {
  event.header = header();
  track_.events.emplace_back(std::move(event));
}

void Emit::pitchBendRange(u8 semitones) {
  pitchBendRange(PitchBendRangePerformanceEvent{
      .semitones = semitones,
  });
}

void Emit::portamento(PortamentoPerformanceEvent event) {
  event.header = header();
  track_.events.emplace_back(std::move(event));
}

void Emit::portamento(double timeMilliseconds, double previousKey) {
  portamento(PortamentoPerformanceEvent{
      .timeMilliseconds = timeMilliseconds,
      .previousKey = previousKey,
  });
}

void Emit::portamentoEnable(PortamentoEnablePerformanceEvent event) {
  event.header = header();
  track_.events.emplace_back(std::move(event));
}

void Emit::portamentoEnable(bool enabled) {
  portamentoEnable(PortamentoEnablePerformanceEvent{
      .enabled = enabled,
  });
}

void Emit::portamentoTime(PortamentoTimePerformanceEvent event) {
  event.header = header();
  track_.events.emplace_back(std::move(event));
}

void Emit::portamentoTime(u8 value) {
  portamentoTime(PortamentoTimePerformanceEvent{
      .value = value,
  });
}

void Emit::portamentoControl(PortamentoControlPerformanceEvent event) {
  event.header = header();
  track_.events.emplace_back(std::move(event));
}

void Emit::portamentoControl(double previousKey) {
  portamentoControl(PortamentoControlPerformanceEvent{
      .previousKey = previousKey,
  });
}

void Emit::legatoPedal(LegatoPedalPerformanceEvent event) {
  event.header = header();
  track_.events.emplace_back(std::move(event));
}

void Emit::legatoPedal(bool enabled) {
  legatoPedal(LegatoPedalPerformanceEvent{
      .enabled = enabled,
  });
}

void Emit::modulation(ModulationPerformanceEvent event) {
  event.header = header();
  track_.events.emplace_back(std::move(event));
}

void Emit::modulation(ModulationPerformanceTarget target, double amount) {
  modulation(ModulationPerformanceEvent{
      .target = target,
      .amount = amount,
  });
}

void Emit::marker(MarkerPerformanceEvent event) {
  event.header = header();
  track_.events.emplace_back(std::move(event));
}

PerformanceEventHeader Emit::header() const {
  return PerformanceEventHeader{
      .sourceCommand = sourceCommand_,
      .track = track_.id,
      .tick = tick_,
  };
}

Step VmApi::next() const noexcept {
  return Step::next();
}

Step VmApi::end() const noexcept {
  return Step::end();
}

Step VmApi::jump(Address destination) const noexcept {
  return Step::jump(destination);
}

Step VmApi::jumpOrLoopForever(Address destination) const noexcept {
  return Step::jumpOrLoopForever(destination);
}

Step VmApi::loopForever(Address destination) const noexcept {
  return Step::loopForever(destination);
}

Step VmApi::call(Address destination) const noexcept {
  return Step::call(destination);
}

Step VmApi::return_() const noexcept {
  return Step::return_();
}

Step VmApi::repeatUntil(u8 slot, u32 count, Address destination) {
  // Most drivers count the first encounter as one iteration. The VM keeps that
  // state here so format commands only name the slot, count, and target.
  auto& remaining = runtime_.repeatRemaining[slot];
  if (remaining == 0) {
    remaining = count;
  }

  if (remaining > 1) {
    --remaining;
    RepeatReplayWindow window{.stopIndex = currentIndex_};
    if (commandRange_.valid()) {
      const u64 destinationOffset = destination.value;
      window.hasSourceWindow = true;
      window.beginOffset = std::min(destinationOffset, commandRange_.offset);
      window.endOffset = std::max(destinationOffset + 1, commandRange_.endOffset());
    }
    runtime_.repeatReplayWindow = window;
    return jump(destination);
  }

  runtime_.repeatRemaining.erase(slot);
  runtime_.repeatReplayWindow.reset();
  return next();
}

Effects VmApi::repeatUntilEffect(u8 slot, u32 count, Address destination) {
  return Effects{.step = repeatUntil(slot, count, destination)};
}

Step VmApi::repeatBreak(u8 slot, Address destination) {
  const auto found = runtime_.repeatRemaining.find(slot);
  if (found != runtime_.repeatRemaining.end() && found->second == 1) {
    runtime_.repeatRemaining.erase(found);
    runtime_.repeatReplayWindow.reset();
    return jump(destination);
  }
  return next();
}

BranchResult VmApi::repeatBreakBranch(u8 slot, Address destination) {
  const Step step = repeatBreak(slot, destination);
  return BranchResult{
      .taken = step.kind == Step::Kind::Jump,
      .effects = Effects{.step = step},
  };
}

u64 VmApi::tick() const noexcept {
  return runtime_.tick;
}

void VmApi::diagnostic(Diagnostic diagnostic) {
  if (!diagnostic.range && commandRange_.valid()) {
    diagnostic.range = commandRange_;
  }
  sequence_.diagnostics.push_back(std::move(diagnostic));
}

VmApi::VmApi(VmTrackRuntime& runtime, PerformanceSequence& sequence, SourceRange commandRange, u32 currentIndex)
    : runtime_(runtime), sequence_(sequence), commandRange_(commandRange), currentIndex_(currentIndex) {
}

SequenceVm::SequenceVm(LoopPolicy loopPolicy) : options_(SequenceVmOptions{.loopPolicy = loopPolicy}) {
}

SequenceVm::SequenceVm(SequenceVmOptions options) : options_(options) {
}

PerformanceSequence SequenceVm::render(const SequenceProgram& program, const SequenceDialect& dialect) const {
  PerformanceSequence sequence{
      .timebase = program.timebase,
  };

  const SequenceProgramBehavior behavior = resolvedBehavior(program, dialect);
  const LoopPolicy loopPolicy = behavior.defaultLoopPolicy;

  struct RenderedTrack {
    PerformanceTrack track;
    std::optional<u64> loopStopTick;
  };

  const auto renderTrack = [&](const TrackProgram& track, PerformanceSequence& targetSequence,
                               std::optional<u64> stopTick) -> RenderedTrack {
    PerformanceTrack performanceTrack{
        .id = track.id,
        .sourceTrackNumber = track.sourceTrackNumber,
    };
    addInitialTrackEvents(performanceTrack, behavior);

    std::any trackState =
        dialect.createTrackState != nullptr ? dialect.createTrackState(program, track, dialect.context) : std::any{};
    VmTrackRuntime runtime;
    // A command reached through a different return stack or repeat-counter state
    // is distinct playback. This keeps normal calls/repeats from looking like
    // infinite loops while still stopping true control-flow cycles.
    std::map<VisitState, VisitRecord> visited;
    std::optional<u32> current = destinationIndex(track, track.startAddress);
    if (!current && !track.commands.empty()) {
      current = 0;
    }

    u32 executedCommands = 0;
    std::optional<u64> firstLoopTick;
    std::optional<u64> loopStopTick;
    u32 loopRepeats = 0;
    bool arrivedByControlFlow = true;
    while (current) {
      if (executedCommands >= behavior.commandLimit) {
        targetSequence.diagnostics.push_back(vmWarning("Sequence VM command limit reached", SourceRange{}));
        break;
      }
      if (stopTick && runtime.tick >= *stopTick) {
        break;
      }
      const SourceCommand& command = track.commands.at(*current);
      const bool replayingRepeat = isReplayingRepeat(runtime, *current, command);
      const auto visitState = VisitState{
          .commandIndex = *current,
          .callStack = runtime.callStack,
          .repeatRemaining = runtime.repeatRemaining,
      };
      if (!replayingRepeat) {
        if (const auto previous = visited.find(visitState); previous != visited.end()) {
          if (arrivedByControlFlow) {
            if (!firstLoopTick) {
              firstLoopTick = runtime.tick;
            }
            if (loopPolicy == LoopPolicy::Preserve) {
              // Preserve-mode exports one pass plus neutral loop markers instead of
              // replaying until the safety command limit.
              addLoopMarker(performanceTrack, previous->second.command, previous->second.tick, "Loop Start");
              addLoopMarker(performanceTrack, runtime.lastCommand.valid() ? runtime.lastCommand : command.id,
                            runtime.tick, "Loop End");
              break;
            }

            if (loopPolicy == LoopPolicy::PlayOnce && loopRepeats < options_.sequenceLoops) {
              ++loopRepeats;
              // A configured loop repeat is real playback, so let the repeated
              // command execute and start detecting the next pass from here.
              visited.clear();
              visited.emplace(visitState, VisitRecord{.tick = runtime.tick, .command = command.id});
            } else {
              loopStopTick = runtime.tick;
              break;
            }
          }
        } else {
          visited.emplace(visitState, VisitRecord{.tick = runtime.tick, .command = command.id});
        }
      }

      const CommandHandler* handler = dialect.handler(command.handler);
      if (handler == nullptr || handler->execute == nullptr) {
        targetSequence.diagnostics.push_back(
            vmWarning(fmt::format("Missing sequence command handler {}", command.handler.value), command.range));
        break;
      }

      Emit emit{performanceTrack, command.id, runtime.tick};
      VmApi vm{runtime, targetSequence, command.range, *current};
      const Effects effects = handler->execute(command, track, trackState, emit, vm, dialect.context);
      runtime.tick += effects.advanceTicks;
      runtime.lastCommand = command.id;

      switch (effects.step.kind) {
        case Step::Kind::Next:
          current = nextCommandIndex(track, *current);
          arrivedByControlFlow = false;
          break;

        case Step::Kind::End:
          current = std::nullopt;
          arrivedByControlFlow = false;
          break;

        case Step::Kind::Jump:
          current = destinationIndex(track, effects.step.destination);
          arrivedByControlFlow = true;
          if (!current) {
            targetSequence.diagnostics.push_back(
                vmWarning(fmt::format("Sequence jump target ${:04X} was not decoded", effects.step.destination.value),
                          command.range));
          }
          break;

        case Step::Kind::JumpOrLoopForever: {
          const auto destination = destinationIndex(track, effects.step.destination);
          if (!destination) {
            targetSequence.diagnostics.push_back(
                vmWarning(fmt::format("Sequence jump target ${:04X} was not decoded", effects.step.destination.value),
                          command.range));
            current = std::nullopt;
            arrivedByControlFlow = true;
            break;
          }

          const VisitRecord* previousVisit = nullptr;
          for (const auto& [state, record] : visited) {
            if (state.commandIndex == *destination && state.callStack == runtime.callStack) {
              previousVisit = &record;
              break;
            }
          }

          if (previousVisit == nullptr) {
            current = destination;
            arrivedByControlFlow = true;
            break;
          }

          if (!firstLoopTick) {
            firstLoopTick = runtime.tick;
          }

          if (loopPolicy == LoopPolicy::Preserve) {
            addLoopMarker(performanceTrack, previousVisit->command, previousVisit->tick, "Loop Start");
            addLoopMarker(performanceTrack, command.id, runtime.tick, "Loop End");
            current = std::nullopt;
            arrivedByControlFlow = false;
            break;
          }

          if (loopPolicy == LoopPolicy::PlayOnce && loopRepeats < options_.sequenceLoops) {
            ++loopRepeats;
            visited.clear();
            current = destination;
            arrivedByControlFlow = true;
          } else {
            loopStopTick = runtime.tick;
            current = std::nullopt;
            arrivedByControlFlow = false;
          }
          break;
        }

        case Step::Kind::LoopForever: {
          if (!firstLoopTick) {
            firstLoopTick = runtime.tick;
          }

          const auto destination = destinationIndex(track, effects.step.destination);
          if (!destination) {
            targetSequence.diagnostics.push_back(
                vmWarning(fmt::format("Sequence loop target ${:04X} was not decoded", effects.step.destination.value),
                          command.range));
            current = std::nullopt;
            arrivedByControlFlow = false;
            break;
          }

          if (loopPolicy == LoopPolicy::Preserve) {
            const auto previous = visited.find(VisitState{
                .commandIndex = *destination,
                .callStack = runtime.callStack,
                .repeatRemaining = runtime.repeatRemaining,
            });
            const SourceCommand& destinationCommand = track.commands.at(*destination);
            addLoopMarker(performanceTrack, destinationCommand.id,
                          previous != visited.end() ? previous->second.tick : runtime.tick, "Loop Start");
            addLoopMarker(performanceTrack, command.id, runtime.tick, "Loop End");
            current = std::nullopt;
            arrivedByControlFlow = false;
            break;
          }

          if (loopPolicy == LoopPolicy::PlayOnce && loopRepeats < options_.sequenceLoops) {
            ++loopRepeats;
            visited.clear();
            current = destination;
            arrivedByControlFlow = true;
          } else {
            loopStopTick = runtime.tick;
            current = std::nullopt;
            arrivedByControlFlow = false;
          }
          break;
        }

        case Step::Kind::Call: {
          if (const auto returnIndex = nextCommandIndex(track, *current)) {
            runtime.callStack.push_back(*returnIndex);
          }
          current = destinationIndex(track, effects.step.destination);
          arrivedByControlFlow = true;
          if (!current) {
            targetSequence.diagnostics.push_back(
                vmWarning(fmt::format("Sequence call target ${:04X} was not decoded", effects.step.destination.value),
                          command.range));
          }
          break;
        }

        case Step::Kind::Return:
          if (runtime.callStack.empty()) {
            targetSequence.diagnostics.push_back(vmWarning("Sequence return had no active call", command.range));
            current = std::nullopt;
            arrivedByControlFlow = false;
          } else {
            current = runtime.callStack.back();
            runtime.callStack.pop_back();
            arrivedByControlFlow = true;
          }
          break;
      }

      ++executedCommands;
    }

    performanceTrack.endTick = runtime.tick;
    return RenderedTrack{
        .track = std::move(performanceTrack),
        .loopStopTick = loopStopTick ? loopStopTick : firstLoopTick,
    };
  };

  std::optional<u64> synchronizedStopTick;
  if (loopPolicy == LoopPolicy::PlayOnce && behavior.stopAllTracksAtFirstLoop) {
    PerformanceSequence dryRunSequence{
        .timebase = program.timebase,
    };
    for (const TrackProgram& track : program.tracks) {
      const auto rendered = renderTrack(track, dryRunSequence, std::nullopt);
      if (rendered.loopStopTick && (!synchronizedStopTick || *rendered.loopStopTick < *synchronizedStopTick)) {
        synchronizedStopTick = rendered.loopStopTick;
      }
    }
  }

  for (size_t i = 0; i < program.tracks.size(); ++i) {
    sequence.tracks.push_back(renderTrack(program.tracks[i], sequence, synchronizedStopTick).track);
  }

  return sequence;
}

SequenceProgramBehavior SequenceVm::resolvedBehavior(const SequenceProgram& program,
                                                     const SequenceDialect& dialect) const {
  SequenceProgramBehavior behavior{
      .defaultLoopPolicy = LoopPolicy::PlayOnce,
      .commandLimit = kFallbackCommandLimit,
  };

  if (program.behavior.defaultLoopPolicy != LoopPolicy::Default) {
    behavior.defaultLoopPolicy = program.behavior.defaultLoopPolicy;
  } else if (dialect.defaultBehavior.defaultLoopPolicy != LoopPolicy::Default) {
    behavior.defaultLoopPolicy = dialect.defaultBehavior.defaultLoopPolicy;
  }
  if (options_.loopPolicy != LoopPolicy::Default) {
    behavior.defaultLoopPolicy = options_.loopPolicy;
  }

  if (program.behavior.commandLimit != 0) {
    behavior.commandLimit = program.behavior.commandLimit;
  } else if (dialect.defaultBehavior.commandLimit != 0) {
    behavior.commandLimit = dialect.defaultBehavior.commandLimit;
  }

  if (program.behavior.initialReverbSend) {
    behavior.initialReverbSend = program.behavior.initialReverbSend;
  } else if (dialect.defaultBehavior.initialReverbSend) {
    behavior.initialReverbSend = dialect.defaultBehavior.initialReverbSend;
  }

  if (program.behavior.initialMonoModeChannels) {
    behavior.initialMonoModeChannels = program.behavior.initialMonoModeChannels;
  } else if (dialect.defaultBehavior.initialMonoModeChannels) {
    behavior.initialMonoModeChannels = dialect.defaultBehavior.initialMonoModeChannels;
  }

  behavior.stopAllTracksAtFirstLoop =
      program.behavior.stopAllTracksAtFirstLoop || dialect.defaultBehavior.stopAllTracksAtFirstLoop;

  return behavior;
}

}  // namespace vgmtrans::core
