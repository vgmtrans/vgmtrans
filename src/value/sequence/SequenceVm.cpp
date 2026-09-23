/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/sequence/SequenceVm.h"
#include "value/sequence/TempoRelativeModulation.h"

#include <any>
#include <algorithm>
#include <compare>
#include <fmt/format.h>
#include <map>
#include <memory>
#include <optional>
#include <utility>

namespace vgmtrans::core {

namespace detail {

[[nodiscard]] Diagnostic vmWarning(std::string message, SourceRange range) {
  return Diagnostic{
      .severity = Severity::Warning,
      .message = std::move(message),
      .range = range,
  };
}

[[nodiscard]] std::optional<u32> continuationIndex(const TrackProgram& track, CommandId command, Address continuation) {
  const size_t next = static_cast<size_t>(command.value) + 1;
  if (next < track.commands.size() && track.commands[next].address.value == continuation.value) {
    return static_cast<u32>(next);
  }
  return track.commandIndex(continuation);
}

struct VisitState {
  u32 commandIndex = 0;
  std::vector<u32> callStack;
  std::map<u8, u32> repeat;

  friend auto operator<=>(const VisitState&, const VisitState&) = default;
};

// LoopDetector only answers "have we executed this same playback state before?"
// The executor decides how that loop should be exported or replayed.
class LoopDetector {
public:
  [[nodiscard]] std::optional<u64> observe(const VisitState& state, u64 tick) {
    const auto [previous, inserted] = visited_.try_emplace(state, tick);
    return inserted ? std::nullopt : std::optional{previous->second};
  }

  [[nodiscard]] std::optional<u64> findExact(const VisitState& state) const {
    const auto found = visited_.find(state);
    return found != visited_.end() ? std::optional{found->second} : std::nullopt;
  }

  [[nodiscard]] std::optional<u64> findLoopCandidateIgnoringRepeatState(u32 commandIndex,
                                                                     const std::vector<u32>& callStack) const {
    // LoopCandidate is a source-driver hint that the jump target is a loop point.
    // Repeat counters are ignored here so the hint still applies when the loop
    // command appears while a finite repeat is active.
    // An empty repeat map sorts before every repeat state for this command and stack.
    const auto found = visited_.lower_bound(VisitState{.commandIndex = commandIndex, .callStack = callStack});
    if (found != visited_.end() && found->first.commandIndex == commandIndex && found->first.callStack == callStack) {
      return found->second;
    }
    return std::nullopt;
  }

  void clear() { visited_.clear(); }

  void record(const VisitState& state, u64 tick) { visited_.emplace(state, tick); }

private:
  // A command reached through a different return stack or repeat-counter state
  // is distinct playback. This keeps normal calls/repeats from looking like
  // infinite loops while still stopping true control-flow cycles.
  std::map<VisitState, u64> visited_;
};

void addInitialTrackEvents(PerformanceEmitter out, const SequenceProgramBehavior& behavior, bool includeGlobalEvents) {
  if (behavior.initialReverbSend) {
    out.reverb(*behavior.initialReverbSend);
  }
  if (behavior.initialLevel) {
    out.level(*behavior.initialLevel);
  }
  if (includeGlobalEvents && behavior.initialMasterLevel) {
    out.masterLevel(*behavior.initialMasterLevel);
  }
  if (behavior.initialExpression) {
    out.expression(*behavior.initialExpression);
  }
  if (behavior.initialChannelPan) {
    out.channelPan(*behavior.initialChannelPan);
  }
  if (behavior.initialStereoBalance) {
    out.stereoBalance(behavior.initialStereoBalance->leftGain, behavior.initialStereoBalance->rightGain);
  }
  if (behavior.initialMonoModeChannels) {
    out.monoMode(*behavior.initialMonoModeChannels);
  }
  if (behavior.initialPitchBendRangeSemitones) {
    out.pitchBendRange(*behavior.initialPitchBendRangeSemitones);
  }
  if (behavior.initialSourceInstrument) {
    out.instrument(*behavior.initialSourceInstrument);
  }
}

void endTrackAt(PerformanceTrack& track, u64 endTick, bool retainBoundaryEvents = false) {
  std::erase_if(track.events, [&](const PerformanceEvent& event) {
    const u64 tick = performanceEventHeader(event).tick;
    if (tick > endTick || (!retainBoundaryEvents && tick == endTick)) {
      return true;
    }
    // Notes beginning exactly at a section boundary have no audible extent.
    return tick == endTick && std::holds_alternative<NotePerformanceEvent>(event);
  });
  for (PerformanceEvent& event : track.events) {
    if (auto* note = std::get_if<NotePerformanceEvent>(&event)) {
      note->durationTicks = static_cast<u32>(std::min<u64>(note->durationTicks, endTick - note->header.tick));
    }
  }
  std::erase_if(track.automations, [=](const PerformanceAutomation& automation) {
    return retainBoundaryEvents ? automation.header.tick > endTick : automation.header.tick >= endTick;
  });
  for (auto& automation : track.automations) {
    automation.realization.startTick = std::min(automation.realization.startTick, endTick);
    automation.realization.endTick = std::min(automation.realization.endTick, endTick);
  }
  track.endTick = endTick;
}

void endSourceSpansAt(std::vector<SourcePlaybackSpan>& spans, u64 endTick) {
  std::erase_if(spans, [endTick](const SourcePlaybackSpan& span) { return span.beginTick >= endTick; });
  for (auto& span : spans) {
    span.endTick = std::min(span.endTick, endTick);
  }
}

[[nodiscard]] u64 eventEndTick(const PerformanceEvent& event) {
  const auto& header = performanceEventHeader(event);
  u64 duration = 1;
  if (const auto* note = std::get_if<NotePerformanceEvent>(&event)) {
    duration = std::max<u64>(1, note->durationTicks);
  }
  return addTicks(header.tick, duration);
}

struct PlaylistVisitState {
  u32 commandIndex = 0;
  std::map<u32, u32> repeatRemaining;

  friend auto operator<=>(const PlaylistVisitState&, const PlaylistVisitState&) = default;
};

struct PlaylistAdvance {
  const std::vector<std::optional<Address>>* streamStarts = nullptr;
  std::optional<u64> preservedLoopStart;
};

// Interprets the small, source-independent control graph between parallel
// sections. Formats normalize their raw playlist quirks into play, repeat, and
// end operations; synchronized scheduling remains a generic VM concern.
class SectionPlaylistRunner {
public:
  SectionPlaylistRunner(const SectionPlaylist& playlist, const SequenceVmOptions& options)
      : playlist_(playlist), options_(options), current_(commandIndex(playlist.startAddress)) {
  }

  [[nodiscard]] PlaylistAdvance advance(u64 tick) {
    constexpr u32 kPlaylistCommandLimit = 100000;
    for (u32 executed = 0; current_ && executed < kPlaylistCommandLimit; ++executed) {
      const PlaylistVisitState state{
          .commandIndex = *current_,
          .repeatRemaining = repeatRemaining_,
      };
      if (const auto [previous, inserted] = visited_.try_emplace(state, tick); !inserted) {
        if (options_.loopPolicy == LoopPolicy::PlayOnce && loopRepeats_ < options_.sequenceLoops) {
          ++loopRepeats_;
          visited_.clear();
          visited_.emplace(state, tick);
        } else {
          return PlaylistAdvance{
              .preservedLoopStart =
                  options_.loopPolicy == LoopPolicy::Preserve ? std::optional<u64>{previous->second} : std::nullopt,
          };
        }
      }

      const PlaylistCommand& command = playlist_.commands[*current_];
      if (command.kind == PlaylistCommandKind::PlaySection) {
        current_ = commandIndex(command.fallthrough);
        return PlaylistAdvance{.streamStarts = &command.streamStarts};
      }
      if (command.kind == PlaylistCommandKind::Repeat) {
        if (command.additionalPlays == 0) {
          current_ = commandIndex(command.target);
          continue;
        }

        const auto [counter, _] = repeatRemaining_.try_emplace(*current_, command.additionalPlays);
        if (counter->second != 0) {
          --counter->second;
          current_ = commandIndex(command.target);
        } else {
          repeatRemaining_.erase(*current_);
          current_ = commandIndex(command.fallthrough);
        }
        continue;
      }

      return {};
    }
    return {};
  }

private:
  [[nodiscard]] std::optional<u32> commandIndex(Address address) const {
    const auto found = std::ranges::find_if(playlist_.commands, [address](const PlaylistCommand& command) {
      return command.address.value == address.value;
    });
    if (found == playlist_.commands.end()) {
      return std::nullopt;
    }
    return static_cast<u32>(std::distance(playlist_.commands.begin(), found));
  }

  const SectionPlaylist& playlist_;
  const SequenceVmOptions& options_;
  std::optional<u32> current_;
  std::map<u32, u32> repeatRemaining_;
  std::map<PlaylistVisitState, u64> visited_;
  u32 loopRepeats_ = 0;
};

// Source position and unconsumed time travel together when a driver restores a
// global loop checkpoint. Musical state and the running clock remain separate.
struct StreamPosition {
  std::optional<u32> command;
  u32 pendingTicks = 0;
  u32 tickCommand = 0;
  bool delayedCommand = false;
};

struct VmChannel {
  PerformanceTrack performance;
  std::any state;
  u32 nextNote = 0;
  u32 nextAutomation = 0;
  ActiveNoteState activeNotes;
};

// A stream shares source position, timing, and control flow across its channels.
// SequenceVm coordinates independent streams and sequence-wide loop boundaries.
class VmStreamExecutor {
public:
  VmStreamExecutor(const SequenceProgram& program, const TrackProgram& track, const SequenceStream& stream,
                   const SequenceRuntime& runtime, TrackId sourceTrackId, u32& nextTrack,
                   const SequenceVmOptions& options, PerformanceSequence& targetSequence, u64& outputSequence,
                   std::any& programState, bool startsActive = true)
      : track_(track), sourceTrackId_(sourceTrackId), sequenceRuntime_(runtime), behavior_(program.behavior),
        options_(options), targetSequence_(targetSequence), outputSequence_(outputSequence),
        programState_(programState),
        position_{.command = startsActive ? track_.commandIndex(track_.startAddress) : std::optional<u32>{}} {
    if (stream.channels.empty()) {
      throw std::invalid_argument("A sequence stream must have at least one channel");
    }
    streamState_ = runtime.createStreamState ? runtime.createStreamState({program, track}) : std::any{};
    channels_.reserve(stream.channels.size());
    for (const u32 number : stream.channels) {
      channels_.push_back(VmChannel{
          .performance = {.id = TrackId{nextTrack++}, .sourceTrackNumber = number, .name = track.name},
          .state = runtime.createTrackState ? runtime.createTrackState({program, track, number}) : std::any{},
      });
      auto& channel = channels_.back();
      addInitialTrackEvents(outputAt(channel, 0), behavior_, channel.performance.id.value == 0);
    }
    if (startsActive && !position_.command && !track_.commands.empty()) {
      warn(fmt::format("Sequence track start ${:04X} was not decoded", track_.startAddress.value), {});
    }
  }

  [[nodiscard]] bool active() const noexcept { return position_.command.has_value() || position_.pendingTicks != 0; }
  [[nodiscard]] u64 tick() const noexcept { return tick_; }
  [[nodiscard]] u64 nextActionTick() const noexcept {
    if (position_.pendingTicks == 0) {
      return tick_;
    }
    return addTicks(tick_, observesEachWaitTick() ? 1 : position_.pendingTicks);
  }
  [[nodiscard]] std::optional<u64> loopStopTick() const noexcept { return loopStopTick_; }

  [[nodiscard]] SequenceCoordinatorSignal executeNext() {
    if (!sectionEntered_ && position_.command) {
      sectionEntered_ = true;
      if (sequenceRuntime_.beginStreamSection) {
        sequenceRuntime_.beginStreamSection(streamState_, !started_);
      }
      if (sequenceRuntime_.beginTrackSection) {
        const auto& command = track_.commands.at(*position_.command);
        VmApi vm(*this, command);
        for (auto& channel : channels_) {
          auto out = outputAt(channel, tick_, CommandId{*position_.command}, command.annotation);
          sequenceRuntime_.beginTrackSection(!started_, programState_, channel.state, out, vm);
        }
      }
      started_ = true;
    }
    if (!position_.command && position_.pendingTicks == 0) {
      return SequenceCoordinatorSignal::None;
    }
    if (position_.pendingTicks != 0) {
      const u32 elapsed = observesEachWaitTick() ? 1 : position_.pendingTicks;
      tick_ = addTicks(tick_, elapsed);
      if (elapsed == 1) {
        tickRuntime(position_.tickCommand);
        if (position_.pendingTicks > 1 && !position_.delayedCommand) {
          executeReadyCommandDuringWait();
        }
      }
      position_.pendingTicks -= elapsed;
      if (position_.pendingTicks != 0) {
        return SequenceCoordinatorSignal::None;
      }
    }

    // The source driver gives one stream control until it schedules another
    // wait. Keep consuming zero-time commands here; yielding between them
    // would let a later stream run too early at the same tick.
    while (position_.command && position_.pendingTicks == 0) {
      const bool hadLoopStop = loopStopTick_.has_value();
      if (!position_.delayedCommand) {
        if (!beginCommand()) {
          return SequenceCoordinatorSignal::None;
        }
        const SourceCommand& command = track_.commands.at(*position_.command);
        if (command.execution.delayTicks != 0) {
          position_.delayedCommand = true;
          scheduleTicks(*position_.command, command.execution.delayTicks);
          return SequenceCoordinatorSignal::None;
        }
      }
      const SequenceCoordinatorSignal signal = executeCommand();
      position_.delayedCommand = false;
      if (signal != SequenceCoordinatorSignal::None) {
        return signal;
      }
      if (!hadLoopStop && loopStopTick_) {
        // Let the sequence coordinator observe the newly discovered common
        // loop boundary before this zero-time loop can execute again.
        return SequenceCoordinatorSignal::None;
      }
      if (position_.pendingTicks != 0) {
        executeReadyCommandDuringWait();
      }
    }
    return SequenceCoordinatorSignal::None;
  }

  // A section switch preserves the format's typed channel state, but resets
  // source control flow and timing to the shared boundary tick.
  void beginSection(std::optional<Address> start, u64 tick) {
    tick_ = tick;
    callStack_.clear();
    repeat_.clear();
    lastCommand_ = {};
    position_ = {.command = start ? track_.commandIndex(*start) : std::optional<u32>{}};
    arrivedByControlFlow_ = true;
    loopDetector_.clear();
    loopStopTick_.reset();
    loopRepeats_ = 0;
    sectionEntered_ = false;
    if (start && !position_.command) {
      warn(fmt::format("Sequence section target ${:04X} was not decoded", start->value), {});
    }
  }

  [[nodiscard]] StreamPosition synchronizedLoopSnapshot(u64 boundary) const {
    // Another stream's current delay may span the loop boundary, so save only
    // the portion that remains after it.
    StreamPosition snapshot = position_;
    if (active()) {
      if (tick_ > boundary || boundary - tick_ > snapshot.pendingTicks) {
        throw std::logic_error("Synchronized loop point was not reached in global stream order");
      }
      snapshot.pendingTicks -= static_cast<u32>(boundary - tick_);
    }
    return snapshot;
  }

  void restoreSynchronizedLoop(const StreamPosition& snapshot, u64 tick) {
    // Continue from the loop-end tick so each repetition follows the previous one.
    tick_ = tick;
    position_ = snapshot;
    arrivedByControlFlow_ = true;
    loopDetector_.clear();
    loopStopTick_.reset();
  }

  void trimAt(u64 tick, bool retainBoundaryEvents) {
    for (auto& channel : channels_) {
      outputAt(channel, tick, lastCommand_).allNotesOff();
      endTrackAt(channel.performance, tick, retainBoundaryEvents);
    }
  }

  void preserveLoop(u64 startTick, u64 endTick) {
    for (auto& channel : channels_) {
      outputAt(channel, startTick).marker("Loop Start");
      outputAt(channel, endTick, lastCommand_).marker("Loop End");
    }
  }

  void finish(std::optional<u64> endTick) {
    const u64 finalTick = endTick.value_or(tick_);
    for (auto& channel : channels_) {
      outputAt(channel, active() ? finalTick : std::min(finalTick, tick_), lastCommand_).allNotesOff();
      auto& track = channel.performance;
      track.endTick = tick_;
      if (endTick) {
        endTrackAt(track, *endTick);
      }
      // Future-dated events retain source order at the same tick.
      std::ranges::stable_sort(track.events, [](const PerformanceEvent& lhs, const PerformanceEvent& rhs) {
        return performanceEventHeader(lhs).tick < performanceEventHeader(rhs).tick;
      });
      targetSequence_.tracks.push_back(std::move(track));
    }
  }

private:
  friend class ::vgmtrans::core::VmApi;

  [[nodiscard]] VisitState visitState(u32 commandIndex) const {
    return {.commandIndex = commandIndex, .callStack = callStack_, .repeat = repeat_};
  }

  [[nodiscard]] bool observesEachWaitTick() const noexcept {
    return sequenceRuntime_.tick != nullptr ||
           (!position_.delayedCommand && position_.command && sequenceRuntime_.readyDuringWait != nullptr &&
            track_.commands[*position_.command].execution.duringWait);
  }

  [[nodiscard]] VmChannel* commandChannel(const SourceCommand& command) {
    if (!command.execution.channel) {
      return &channels_.front();
    }
    const auto found = std::ranges::find(channels_, *command.execution.channel, [](const VmChannel& channel) {
      return channel.performance.sourceTrackNumber;
    });
    return found == channels_.end() ? nullptr : &*found;
  }

  [[nodiscard]] PerformanceEmitter outputAt(VmChannel& channel, u64 tick, CommandId command = {},
                                            SourceAnnotationId annotation = {}) {
    return {channel.performance,
            {sourceTrackId_, command},
            annotation,
            tick,
            outputSequence_,
            channel.nextNote,
            channel.nextAutomation,
            behavior_.panLaw,
            &channel.activeNotes,
            &targetSequence_.sourceSpans};
  }

  void tickRuntime(u32 commandIndex) {
    if (sequenceRuntime_.tick == nullptr) {
      return;
    }
    const SourceCommand& command = track_.commands.at(commandIndex);
    VmApi vm(*this, command);
    for (auto& channel : channels_) {
      auto out = outputAt(channel, tick_, CommandId{commandIndex}, command.annotation);
      sequenceRuntime_.tick(command, programState_, channel.state, out, vm);
    }
  }

  void executeReadyCommandDuringWait() {
    if (position_.pendingTicks == 0 || position_.delayedCommand || !position_.command ||
        sequenceRuntime_.readyDuringWait == nullptr) {
      return;
    }
    const u32 commandIndex = *position_.command;
    const SourceCommand& command = track_.commands.at(commandIndex);
    if (!command.execution.duringWait) {
      return;
    }
    auto* channel = commandChannel(command);
    if (!channel) {
      return;
    }
    auto out = outputAt(*channel, tick_, CommandId{commandIndex}, command.annotation);
    VmApi vm(*this, command);
    if (!sequenceRuntime_.readyDuringWait(command, programState_, channel->state, out, vm)) {
      return;
    }
    if (beginCommand()) {
      static_cast<void>(executeCommand(true));
    }
  }

  // A loop returns to the command's delay as well as its body. Record arrival
  // before consuming that delay so markers and stopping boundaries include it.
  [[nodiscard]] bool beginCommand() {
    if (executedCommands_ >= behavior_.commandLimit) {
      const SourceCommand& command = track_.commands.at(*position_.command);
      warn(fmt::format("Sequence VM command limit reached: track={}, address=${:04X}, tick={}, executed={}, limit={}",
                       channels_.front().performance.sourceTrackNumber, command.address.value, tick_, executedCommands_,
                       behavior_.commandLimit),
           command.range);
      position_.command = std::nullopt;
      return false;
    }
    const u32 commandIndex = *position_.command;
    const CommandId commandId{commandIndex};
    const VisitState state = visitState(commandIndex);
    const auto previous = loopDetector_.observe(state, tick_);
    if (behavior_.inferLoopsFromRepeatedState && arrivedByControlFlow_ && previous) {
      handleLoop(*previous, lastCommand_.valid() ? lastCommand_ : commandId, commandIndex, state);
    }
    return position_.command.has_value();
  }

  [[nodiscard]] SequenceCoordinatorSignal executeCommand(bool duringWait = false) {
    const u32 commandIndex = *position_.command;
    const CommandId commandId{commandIndex};
    const SourceCommand& command = track_.commands.at(commandIndex);
    const u64 beginTick = tick_;
    auto* channel = commandChannel(command);
    const size_t firstEvent = channel ? channel->performance.events.size() : 0;
    const size_t firstAutomation = channel ? channel->performance.automations.size() : 0;
    Effects effects;
    if (channel) {
      auto out = outputAt(*channel, beginTick, commandId, command.annotation);
      VmApi vm(*this, command);
      effects = sequenceRuntime_.execute(command, programState_, channel->state, out, vm);
    }
    if (duringWait) {
      if (effects.advanceTicks != 0 || effects.flowOverride ||
          command.flow.defaultTransition.kind != CommandTransitionKind::Fallthrough) {
        throw std::logic_error("A command executed during a wait must not advance time or alter control flow");
      }
    } else {
      scheduleTicks(commandIndex, effects.advanceTicks);
    }
    const CommandTransition effectiveTransition = effects.flowOverride.value_or(command.flow.defaultTransition);
    if (command.annotation.valid()) {
      u64 endTick = addTicks(tick_, std::max(effects.advanceTicks, 1u));
      if (channel) {
        for (size_t i = firstEvent; i < channel->performance.events.size(); ++i) {
          endTick = std::max(endTick, eventEndTick(channel->performance.events[i]));
        }
        for (size_t i = firstAutomation; i < channel->performance.automations.size(); ++i) {
          endTick = std::max(endTick, channel->performance.automations[i].realization.endTick);
        }
      }
      const size_t sourceSpanIndex = targetSequence_.sourceSpans.size();
      targetSequence_.sourceSpans.push_back(SourcePlaybackSpan{
          .annotation = command.annotation,
          .channel = command.sourceChannel,
          .beginTick = beginTick,
          .endTick = endTick,
      });
      if (channel) {
        for (auto& [_, note] : channel->activeNotes.notes) {
          if (note.eventIndex >= firstEvent) {
            note.sourceSpanIndex = sourceSpanIndex;
          }
        }
      }
    }
    lastCommand_ = commandId;
    const SequenceCoordinatorSignal signal = effectiveTransition.kind == CommandTransitionKind::EndSection
                                                 ? SequenceCoordinatorSignal::SectionEnd
                                                 : command.execution.coordinatorSignal;
    applyTransition(commandId, command, effectiveTransition);

    ++executedCommands_;
    return signal;
  }

  void handleLoop(u64 startTick, CommandId endCommand, u32 replayIndex,
                  std::optional<VisitState> recordAfterClear = std::nullopt) {
    // Once a loop is identified, all loop sources use the same export policy:
    // preserve markers, replay for the requested loop count, or stop the track.
    if (options_.loopPolicy == LoopPolicy::Preserve) {
      for (auto& channel : channels_) {
        outputAt(channel, startTick, CommandId{replayIndex}).marker("Loop Start");
        outputAt(channel, tick_, endCommand).marker("Loop End");
      }
      position_.command = std::nullopt;
      arrivedByControlFlow_ = false;
      return;
    }

    if (options_.loopPolicy == LoopPolicy::PlayOnce) {
      if (loopRepeats_ < options_.sequenceLoops) {
        ++loopRepeats_;
      } else if (!loopStopTick_) {
        // Keep shorter stream loops running while the scheduler discovers the
        // longest requested endpoint. The sequence-level cutoff removes any
        // temporary events rendered past that common boundary.
        loopStopTick_ = tick_;
      }
      loopDetector_.clear();
      if (recordAfterClear) {
        loopDetector_.record(*recordAfterClear, tick_);
      }
      position_.command = replayIndex;
      arrivedByControlFlow_ = true;
      return;
    }

    loopStopTick_ = tick_;
    position_.command = std::nullopt;
    arrivedByControlFlow_ = false;
  }

  void applyTransition(CommandId commandId, const SourceCommand& command, const CommandTransition& transition) {
    switch (transition.kind) {
      case CommandTransitionKind::Fallthrough:
        position_.command = continuationIndex(track_, commandId, command.flow.continuation);
        arrivedByControlFlow_ = false;
        if (!position_.command) {
          warn(fmt::format("Sequence continuation ${:04X} was not decoded", command.flow.continuation.value),
               command.range);
        }
        break;

      case CommandTransitionKind::End:
      case CommandTransitionKind::EndSection:
        position_.command = std::nullopt;
        arrivedByControlFlow_ = false;
        break;

      case CommandTransitionKind::Jump:
        applyJump(commandId, command, transition.destination, transition.jumpSemantics);
        break;

      case CommandTransitionKind::Call:
        if (const auto returnIndex = continuationIndex(track_, commandId, command.flow.continuation)) {
          callStack_.push_back(*returnIndex);
        } else {
          warn(fmt::format("Sequence call continuation ${:04X} was not decoded", command.flow.continuation.value),
               command.range);
          position_.command = std::nullopt;
          arrivedByControlFlow_ = false;
          break;
        }
        position_.command = track_.commandIndex(transition.destination);
        arrivedByControlFlow_ = true;
        if (!position_.command) {
          warn(fmt::format("Sequence call target ${:04X} was not decoded", transition.destination.value),
               command.range);
        }
        break;

      case CommandTransitionKind::Return:
        if (callStack_.empty()) {
          warn("Sequence return had no active call", command.range);
          position_.command = std::nullopt;
          arrivedByControlFlow_ = false;
        } else {
          position_.command = callStack_.back();
          callStack_.pop_back();
          arrivedByControlFlow_ = true;
        }
        break;
    }
  }

  void scheduleTicks(u32 commandIndex, u32 ticks) {
    position_.pendingTicks = ticks;
    position_.tickCommand = commandIndex;
  }

  void applyJump(CommandId commandId, const SourceCommand& command, Address destination, JumpSemantics semantics) {
    position_.command = track_.commandIndex(destination);
    arrivedByControlFlow_ = semantics == JumpSemantics::Normal || semantics == JumpSemantics::LoopCandidate;
    if (!position_.command) {
      const std::string_view target = semantics == JumpSemantics::FiniteBranch   ? "branch"
                                      : semantics == JumpSemantics::DeclaredLoop ? "loop"
                                                                                 : "jump";
      warn(fmt::format("Sequence {} target ${:04X} was not decoded", target, destination.value), command.range);
      return;
    }

    std::optional<u64> previous;
    switch (semantics) {
      case JumpSemantics::Normal:
      case JumpSemantics::FiniteBranch:
        return;
      case JumpSemantics::LoopCandidate:
        previous = loopDetector_.findLoopCandidateIgnoringRepeatState(*position_.command, callStack_);
        if (!previous) {
          return;
        }
        break;
      case JumpSemantics::DeclaredLoop:
        previous = loopDetector_.findExact(visitState(*position_.command));
        break;
    }

    handleLoop(previous.value_or(tick_), commandId, *position_.command);
  }

  void warn(std::string message, SourceRange range) {
    targetSequence_.diagnostics.push_back(vmWarning(std::move(message), range));
  }

  const TrackProgram& track_;
  TrackId sourceTrackId_;
  const SequenceRuntime& sequenceRuntime_;
  const SequenceProgramBehavior& behavior_;
  const SequenceVmOptions& options_;
  PerformanceSequence& targetSequence_;
  u64& outputSequence_;
  std::vector<VmChannel> channels_;
  std::any streamState_;
  std::any& programState_;
  u64 tick_ = 0;
  bool sectionEntered_ = false;
  bool started_ = false;
  std::vector<u32> callStack_;
  // Remaining plays distinguish legitimate finite passes in loop detection.
  std::map<u8, u32> repeat_;
  CommandId lastCommand_;
  LoopDetector loopDetector_;
  StreamPosition position_;
  u32 executedCommands_ = 0;
  std::optional<u64> loopStopTick_;
  u32 loopRepeats_ = 0;
  bool arrivedByControlFlow_ = true;
};

}  // namespace detail

RepeatCounter::RepeatCounter(std::map<u8, u32>& remaining, u8 slot) noexcept : remaining_(&remaining), slot_(slot) {
}

bool RepeatCounter::active() const {
  return remaining_->contains(slot_);
}

bool RepeatCounter::firstVisit() const {
  return !active();
}

u32 RepeatCounter::remainingPlays() const {
  const auto found = remaining_->find(slot_);
  return found != remaining_->end() ? found->second : 0;
}

void RepeatCounter::start(u32 totalPlays) {
  (*remaining_)[slot_] = totalPlays;
}

bool RepeatCounter::consumeReplay() {
  const auto found = remaining_->find(slot_);
  if (found == remaining_->end() || found->second <= 1) {
    return false;
  }
  --found->second;
  return true;
}

void RepeatCounter::finish() {
  remaining_->erase(slot_);
}

Effects VmApi::fallthrough() const noexcept {
  return Effects{.flowOverride = CommandTransition::fallthrough()};
}

Effects VmApi::end() const noexcept {
  return Effects{.flowOverride = CommandTransition::end()};
}

Effects VmApi::endSection() const noexcept {
  return Effects{.flowOverride = CommandTransition::endSection()};
}

Effects VmApi::jump(Address destination) const noexcept {
  return Effects{.flowOverride = CommandTransition::jump(destination)};
}

Effects VmApi::finiteBranch(Address destination) const noexcept {
  return Effects{.flowOverride = CommandTransition::jump(destination, JumpSemantics::FiniteBranch)};
}

Effects VmApi::loopCandidate(Address destination) const noexcept {
  return Effects{.flowOverride = CommandTransition::jump(destination, JumpSemantics::LoopCandidate)};
}

Effects VmApi::declaredLoop(Address destination) const noexcept {
  return Effects{.flowOverride = CommandTransition::jump(destination, JumpSemantics::DeclaredLoop)};
}

Effects VmApi::call(Address destination) const noexcept {
  return Effects{.flowOverride = CommandTransition::call(destination)};
}

Effects VmApi::return_() const noexcept {
  return Effects{.flowOverride = CommandTransition::return_()};
}

bool VmApi::inSubroutine() const noexcept {
  return !executor_.callStack_.empty();
}

RepeatCounter VmApi::repeatCounter(u8 slot) {
  return RepeatCounter(executor_.repeat_, slot);
}

Effects VmApi::countedRepeatUntil(u8 slot, u32 totalPlays, Address destination) {
  RepeatCounter counter = repeatCounter(slot);
  if (counter.firstVisit()) {
    counter.start(totalPlays);
  }

  if (counter.consumeReplay()) {
    return jump(destination);
  }

  counter.finish();
  return Effects{};
}

Effects VmApi::countedRepeatBreak(u8 slot, Address destination) {
  RepeatCounter counter = repeatCounter(slot);
  if (counter.remainingPlays() == 1) {
    counter.finish();
    return finiteBranch(destination);
  }

  return Effects{};
}

u64 VmApi::tick() const noexcept {
  return executor_.tick_;
}

SourceRange VmApi::sourceRange() const noexcept {
  return command_.range;
}

const PerformanceSequence& VmApi::sequence() const noexcept {
  return executor_.targetSequence_;
}

void VmApi::diagnostic(Diagnostic diagnostic) {
  if (!diagnostic.range.valid() && command_.range.valid()) {
    diagnostic.range = command_.range;
  }
  if (!diagnostic.annotation && command_.annotation.valid()) {
    diagnostic.annotation = command_.annotation;
  }
  executor_.targetSequence_.diagnostics.push_back(std::move(diagnostic));
}

std::any& VmApi::streamState() const noexcept {
  return executor_.streamState_;
}

VmApi::VmApi(detail::VmStreamExecutor& executor, const SourceCommand& command)
    : executor_(executor), command_(command) {
}

SequenceVm::SequenceVm(LoopPolicy loopPolicy) : options_(SequenceVmOptions{.loopPolicy = loopPolicy}) {
}

SequenceVm::SequenceVm(SequenceVmOptions options) : options_(options) {
}

PerformanceSequence SequenceVm::render(const SequenceProgram& program) const {
  return renderImpl(program, program.runtime, nullptr);
}

PerformanceSequence SequenceVm::render(const SequenceProgram& program, const SequenceRuntime& runtime) const {
  return renderImpl(program, runtime, nullptr);
}

PerformanceSequence SequenceVm::renderImpl(const SequenceProgram& program, const SequenceRuntime& runtime,
                                           std::any* analyzedProgramState) const {
  const SequenceProgramBehavior& behavior = program.behavior;
  PerformanceSequence sequence{
      .timebase = program.timebase,
      .initialTempoMicrosecondsPerQuarter = behavior.initialTempoMicrosecondsPerQuarter,
      .preferredPitchTransitionRendering = behavior.preferredPitchTransitionRendering,
  };

  const SequenceVmOptions options{
      .loopPolicy = options_.loopPolicy == LoopPolicy::Default ? behavior.loopPolicy : options_.loopPolicy,
      .sequenceLoops = options_.sequenceLoops,
  };

  if (runtime.valid()) {
    // Some formats must inspect the whole song before the first event can be
    // exported. Keep one song-wide state object across an optional silent pass
    // and the real render so collected information is retained.
    std::any programState = runtime.createProgramState ? runtime.createProgramState(program) : std::any{};
    const auto renderSemanticPass = [&](PerformanceSequence& target) {
      u64 outputSequence = 0;
      std::vector<std::unique_ptr<detail::VmStreamExecutor>> executors;
      executors.reserve(program.streamCount());
      u32 nextTrack = 0;
      const bool hasSectionPlaylist = program.sectionPlaylist.has_value();
      for (size_t trackIndex = 0; trackIndex < program.tracks.size(); ++trackIndex) {
        const TrackProgram& track = program.tracks[trackIndex];
        for (const auto& stream : track.streams) {
          executors.push_back(std::make_unique<detail::VmStreamExecutor>(
              program, track, stream, runtime, TrackId{static_cast<u32>(trackIndex)}, nextTrack, options, target,
              outputSequence, programState, !hasSectionPlaylist));
        }
      }
      auto beginSection = [&](const std::vector<std::optional<Address>>& starts, u64 tick) {
        for (size_t i = 0; i < executors.size(); ++i) {
          executors[i]->beginSection(i < starts.size() ? starts[i] : std::nullopt, tick);
        }
      };

      std::optional<detail::SectionPlaylistRunner> playlist;
      if (program.sectionPlaylist) {
        playlist.emplace(*program.sectionPlaylist, options);
        const detail::PlaylistAdvance first = playlist->advance(0);
        if (first.streamStarts != nullptr) {
          beginSection(*first.streamStarts, 0);
        }
      }

      // Execute the earliest stream first; declaration order is the stable
      // tie-break. A stream keeps control at the same tick while it consumes
      // zero-time commands, matching how these drivers run until their next wait.
      std::optional<u64> sequenceEndTick;
      std::optional<u64> synchronizedLoopStartTick;
      std::vector<detail::StreamPosition> synchronizedLoopSnapshot;
      u32 synchronizedLoopRepeats = 0;
      while (true) {
        size_t selected = executors.size();
        for (size_t i = 0; i < executors.size(); ++i) {
          if (!executors[i]->active()) {
            continue;
          }
          if (selected == executors.size() || executors[i]->nextActionTick() < executors[selected]->nextActionTick()) {
            selected = i;
          }
        }
        if (selected == executors.size()) {
          break;
        }

        const SequenceCoordinatorSignal signal = executors[selected]->executeNext();
        if (signal == SequenceCoordinatorSignal::SynchronizedLoopStart && !playlist) {
          const u64 boundary = executors[selected]->tick();
          synchronizedLoopSnapshot.clear();
          synchronizedLoopSnapshot.reserve(executors.size());
          for (const auto& executor : executors) {
            synchronizedLoopSnapshot.push_back(executor->synchronizedLoopSnapshot(boundary));
          }
          synchronizedLoopStartTick = boundary;
          synchronizedLoopRepeats = 0;
          continue;
        }

        if (signal == SequenceCoordinatorSignal::SynchronizedLoopEnd && !playlist) {
          const u64 boundary = executors[selected]->tick();
          if (!synchronizedLoopSnapshot.empty()) {
            if (options.loopPolicy == LoopPolicy::PlayOnce && synchronizedLoopRepeats < options.sequenceLoops) {
              ++synchronizedLoopRepeats;
              for (size_t i = 0; i < executors.size(); ++i) {
                executors[i]->restoreSynchronizedLoop(synchronizedLoopSnapshot[i], boundary);
              }
              continue;
            }

            if (options.loopPolicy == LoopPolicy::Preserve && synchronizedLoopStartTick) {
              for (auto& executor : executors) {
                executor->preserveLoop(*synchronizedLoopStartTick, boundary);
              }
            }
            sequenceEndTick = boundary;
            break;
          }
        }

        if (signal == SequenceCoordinatorSignal::SectionEnd && playlist) {
          if (program.sectionPlaylist->waitForAllTracks &&
              std::ranges::any_of(executors, [](const auto& executor) { return executor->active(); })) {
            continue;
          }
          const u64 boundary = executors[selected]->tick();
          // Streams are visited in stable source order, so keep same-tick work
          // from streams processed before the boundary command.
          for (size_t i = 0; i < executors.size(); ++i) {
            executors[i]->trimAt(boundary, i <= selected);
          }
          detail::endSourceSpansAt(target.sourceSpans, boundary);

          const detail::PlaylistAdvance next = playlist->advance(boundary);
          if (next.preservedLoopStart) {
            for (auto& executor : executors) {
              executor->preserveLoop(*next.preservedLoopStart, boundary);
            }
          }
          if (next.streamStarts == nullptr) {
            sequenceEndTick = boundary;
            break;
          }
          beginSection(*next.streamStarts, boundary);
          continue;
        }

        const bool hasLoopBoundary =
            std::ranges::any_of(executors, [](const auto& executor) { return executor->loopStopTick().has_value(); });
        if ((!playlist || program.sectionPlaylist->waitForAllTracks) &&
            options.loopPolicy == LoopPolicy::PlayOnce && hasLoopBoundary &&
            std::ranges::all_of(executors,
                                [](const auto& executor) { return !executor->active() || executor->loopStopTick(); })) {
          sequenceEndTick = 0;
          for (const auto& executor : executors) {
            *sequenceEndTick = std::max(*sequenceEndTick, executor->loopStopTick().value_or(executor->tick()));
          }
          break;
        }
      }

      target.tracks.reserve(program.playbackTrackCount());
      for (auto& executor : executors) {
        executor->finish(sequenceEndTick);
      }
      if (sequenceEndTick) {
        // Closing notes updates their source spans; trim only after all tracks finish.
        detail::endSourceSpansAt(target.sourceSpans, *sequenceEndTick);
      }
    };

    if (runtime.finishPrepass != nullptr || analyzedProgramState != nullptr) {
      // Analysis and formats with a prepass execute the same silent pass in
      // normal time order. Keep its song-wide state and discard its events.
      PerformanceSequence prepass{
          .timebase = program.timebase,
          .initialTempoMicrosecondsPerQuarter = behavior.initialTempoMicrosecondsPerQuarter,
      };
      renderSemanticPass(prepass);
      if (runtime.finishPrepass != nullptr) {
        runtime.finishPrepass(programState);
      }
      if (analyzedProgramState != nullptr) {
        // Analysis needs only this pass; its caller retains the state and diagnostics.
        *analyzedProgramState = std::move(programState);
        return prepass;
      }
    }
    renderSemanticPass(sequence);
    if (runtime.finalizePerformance != nullptr) {
      runtime.finalizePerformance(programState, sequence);
    }
    resolveTempoRelativeModulation(sequence);
    return sequence;
  }

  sequence.diagnostics.push_back(detail::vmWarning("Sequence program has no runtime executor", {}));
  return sequence;
}

std::any detail::analyzeSequenceProgram(const SequenceVm& vm, const SequenceProgram& program,
                                        std::vector<Diagnostic>* diagnostics) {
  std::any state;
  const PerformanceSequence analysis = vm.renderImpl(program, program.runtime, &state);
  if (diagnostics != nullptr) {
    diagnostics->insert(diagnostics->end(), analysis.diagnostics.begin(), analysis.diagnostics.end());
  }
  return state;
}

}  // namespace vgmtrans::core
