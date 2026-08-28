/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/sequence/SequenceVm.h"
#include "value/sequence/TempoRelativeModulation.h"

#include <any>
#include <algorithm>
#include <fmt/format.h>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <tuple>
#include <utility>

namespace vgmtrans::core {

namespace detail {

struct RepeatStateSnapshot {
  std::map<u8, u32> remaining;

  friend bool operator<(const RepeatStateSnapshot& lhs, const RepeatStateSnapshot& rhs) {
    return lhs.remaining < rhs.remaining;
  }
};

// Remaining counter values are part of VisitState, so each legitimate finite
// pass is distinct. When the same command, call stack, and counters recur, the
// future control flow is identical and the VM has found a real loop.
class RepeatState {
public:
  [[nodiscard]] bool active(u8 slot) const { return remaining_.contains(slot); }

  [[nodiscard]] u32 remainingPlays(u8 slot) const {
    const auto found = remaining_.find(slot);
    return found != remaining_.end() ? found->second : 0;
  }

  void start(u8 slot, u32 totalPlays) { remaining_[slot] = totalPlays; }

  [[nodiscard]] bool consumeReplay(u8 slot) {
    auto found = remaining_.find(slot);
    if (found == remaining_.end() || found->second <= 1) {
      return false;
    }
    --found->second;
    return true;
  }

  void finish(u8 slot) { remaining_.erase(slot); }

  void clear() { remaining_.clear(); }

  [[nodiscard]] RepeatStateSnapshot snapshot() const { return RepeatStateSnapshot{.remaining = remaining_}; }

private:
  std::map<u8, u32> remaining_;
};

// Mutable playback state for one track. The parsed SequenceProgram stays unchanged
// while the VM advances ticks, calls, repeats, and loop detection.
struct VmTrackRuntime {
  u64 tick = 0;
  u32 nextNote = 0;
  u32 nextAutomation = 0;
  ActiveNoteState activeNotes;
  std::vector<u32> callStack;
  RepeatState repeat;
  CommandId lastCommand;
};

struct VmApiAccess {
  [[nodiscard]] static VmApi make(VmTrackRuntime& runtime, PerformanceSequence& sequence,
                                  const SourceCommand& command) {
    return VmApi(runtime, sequence, command);
  }
};

}  // namespace detail

namespace {

using detail::RepeatStateSnapshot;
using detail::VmTrackRuntime;

[[nodiscard]] Diagnostic vmWarning(std::string message, SourceRange range) {
  return Diagnostic{
      .severity = Severity::Warning,
      .message = std::move(message),
      .range = range.valid() ? std::optional<SourceRange>{range} : std::nullopt,
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
  RepeatStateSnapshot repeat;

  friend bool operator<(const VisitState& lhs, const VisitState& rhs) {
    return std::tie(lhs.commandIndex, lhs.callStack, lhs.repeat) <
           std::tie(rhs.commandIndex, rhs.callStack, rhs.repeat);
  }
};

struct VisitRecord {
  u64 tick = 0;
  CommandId command;
};

struct LoopPoint {
  VisitRecord start;
  CommandId endCommand;
  u64 endTick = 0;
};

enum class LoopActionKind {
  ContinueExecution,
  StopTrack,
};

struct LoopAction {
  LoopActionKind kind = LoopActionKind::ContinueExecution;
};

// LoopDetector only answers "have we executed this same playback state before?"
// The executor decides how that loop should be exported or replayed.
class LoopDetector {
public:
  [[nodiscard]] static VisitState visitState(u32 commandIndex, const VmTrackRuntime& runtime) {
    return VisitState{
        .commandIndex = commandIndex,
        .callStack = runtime.callStack,
        .repeat = runtime.repeat.snapshot(),
    };
  }

  [[nodiscard]] std::optional<LoopPoint> observe(const VisitState& state, const VmTrackRuntime& runtime,
                                                 bool arrivedByControlFlow) {
    const CommandId commandId{state.commandIndex};
    if (const auto previous = visited_.find(state); previous != visited_.end()) {
      if (!arrivedByControlFlow) {
        return std::nullopt;
      }
      return LoopPoint{
          .start = previous->second,
          .endCommand = runtime.lastCommand.valid() ? runtime.lastCommand : commandId,
          .endTick = runtime.tick,
      };
    }

    visited_.emplace(state, VisitRecord{.tick = runtime.tick, .command = commandId});
    return std::nullopt;
  }

  [[nodiscard]] const VisitRecord* findExact(const VisitState& state) const {
    const auto found = visited_.find(state);
    return found != visited_.end() ? &found->second : nullptr;
  }

  [[nodiscard]] const VisitRecord* findLoopCandidateIgnoringRepeatState(u32 commandIndex,
                                                                        const std::vector<u32>& callStack) const {
    // LoopCandidate is a source-driver hint that the jump target is a loop point.
    // Repeat counters are ignored here so the hint still applies when the loop
    // command appears while a finite repeat is active.
    for (const auto& [state, record] : visited_) {
      if (state.commandIndex == commandIndex && state.callStack == callStack) {
        return &record;
      }
    }
    return nullptr;
  }

  void clear() { visited_.clear(); }

  void record(const VisitState& state, VisitRecord record) { visited_.emplace(state, std::move(record)); }

private:
  // A command reached through a different return stack or repeat-counter state
  // is distinct playback. This keeps normal calls/repeats from looking like
  // infinite loops while still stopping true control-flow cycles.
  std::map<VisitState, VisitRecord> visited_;
};

void addLoopMarker(PerformanceTrack& track, CommandId sourceCommand, u64 tick, u64& nextSequence, std::string text) {
  track.events.emplace_back(MarkerPerformanceEvent{
      .header =
          PerformanceEventHeader{
              .sourceCommand = sourceCommand,
              .track = track.id,
              .tick = tick,
              .sequence = nextSequence++,
          },
      .text = std::move(text),
  });
}

void addInitialTrackEvents(PerformanceTrack& track, const SequenceProgramBehavior& behavior, bool includeGlobalEvents) {
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
  if (behavior.initialLevel) {
    track.events.emplace_back(LevelPerformanceEvent{
        .header = header,
        .linearGain = *behavior.initialLevel,
    });
  }
  if (includeGlobalEvents && behavior.initialMasterLevel) {
    track.events.emplace_back(MasterLevelPerformanceEvent{
        .header = header,
        .linearGain = *behavior.initialMasterLevel,
    });
  }
  if (behavior.initialExpression) {
    track.events.emplace_back(ExpressionPerformanceEvent{
        .header = header,
        .linearGain = *behavior.initialExpression,
    });
  }
  if (behavior.initialStereoBalance) {
    track.events.emplace_back(StereoBalancePerformanceEvent{
        .header = header,
        .leftGain = behavior.initialStereoBalance->leftGain,
        .rightGain = behavior.initialStereoBalance->rightGain,
    });
  }
  if (behavior.initialMonoModeChannels) {
    track.events.emplace_back(MonoModePerformanceEvent{
        .header = header,
        .channels = *behavior.initialMonoModeChannels,
    });
  }
  if (behavior.initialPitchBendRangeSemitones) {
    track.events.emplace_back(PitchBendRangePerformanceEvent{
        .header = header,
        .cents = static_cast<u16>(static_cast<u16>(*behavior.initialPitchBendRangeSemitones) * 100),
    });
  }
  if (behavior.initialSourceInstrument) {
    track.events.emplace_back(InstrumentPerformanceEvent{
        .header = header,
        .sourceInstrument = *behavior.initialSourceInstrument,
    });
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
  return header.tick > std::numeric_limits<u64>::max() - duration ? std::numeric_limits<u64>::max()
                                                                  : header.tick + duration;
}

struct PlaylistVisitState {
  u32 commandIndex = 0;
  std::map<u32, u32> repeatRemaining;

  friend bool operator<(const PlaylistVisitState& lhs, const PlaylistVisitState& rhs) {
    return std::tie(lhs.commandIndex, lhs.repeatRemaining) < std::tie(rhs.commandIndex, rhs.repeatRemaining);
  }
};

struct PlaylistAdvance {
  const std::vector<std::optional<Address>>* trackStarts = nullptr;
  std::optional<u64> preservedLoopStart;
};

// Interprets the small, source-independent control graph between parallel
// sections. Formats normalize their raw playlist quirks into play, repeat, and
// end operations; synchronized scheduling remains a generic VM concern.
class SectionPlaylistRunner {
public:
  SectionPlaylistRunner(const SectionPlaylist& playlist, LoopPolicy loopPolicy, const SequenceVmOptions& options)
      : playlist_(playlist), loopPolicy_(loopPolicy), options_(options), current_(commandIndex(playlist.startAddress)) {
  }

  [[nodiscard]] PlaylistAdvance advance(u64 tick) {
    constexpr u32 kPlaylistCommandLimit = 100000;
    for (u32 executed = 0; current_ && executed < kPlaylistCommandLimit; ++executed) {
      const PlaylistVisitState state{
          .commandIndex = *current_,
          .repeatRemaining = repeatRemaining_,
      };
      if (const auto previous = visited_.find(state); previous != visited_.end()) {
        if (loopPolicy_ == LoopPolicy::PlayOnce && loopRepeats_ < options_.sequenceLoops) {
          ++loopRepeats_;
          visited_.clear();
          visited_.emplace(state, tick);
        } else {
          return PlaylistAdvance{
              .preservedLoopStart =
                  loopPolicy_ == LoopPolicy::Preserve ? std::optional<u64>{previous->second} : std::nullopt,
          };
        }
      } else {
        visited_.emplace(state, tick);
      }

      const PlaylistCommand& command = playlist_.commands[*current_];
      if (command.kind == PlaylistCommandKind::PlaySection) {
        current_ = commandIndex(command.fallthrough);
        return PlaylistAdvance{.trackStarts = &command.trackStarts};
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
  LoopPolicy loopPolicy_ = LoopPolicy::PlayOnce;
  const SequenceVmOptions& options_;
  std::optional<u32> current_;
  std::map<u32, u32> repeatRemaining_;
  std::map<PlaylistVisitState, u64> visited_;
  u32 loopRepeats_ = 0;
};

struct SynchronizedLoopTrackSnapshot {
  // Song loops restore source control and timing; musical state continues.
  std::optional<u32> current;
  u32 pendingTicks = 0;
  u32 pendingTickCommand = 0;
  bool delayedCommand = false;
};

// VmTrackExecutor owns the mutable playback state for one track. SequenceVm keeps
// whole-sequence coordination, such as synchronized stopping across tracks.
class VmTrackExecutor {
public:
  VmTrackExecutor(const SequenceProgram& program, const SequenceRuntime& runtime, TrackId trackId,
                  const TrackProgram& track, const SequenceVmOptions& options, PerformanceSequence& targetSequence,
                  u64& outputSequence, bool includeGlobalInitialEvents, std::any& programState,
                  bool startsActive = true)
      : track_(track), sequenceRuntime_(runtime), behavior_(program.behavior),
        loopPolicy_(options.loopPolicy == LoopPolicy::Default ? behavior_.loopPolicy : options.loopPolicy),
        options_(options), targetSequence_(targetSequence), outputSequence_(outputSequence),
        performanceTrack_(PerformanceTrack{
            .id = trackId,
            .sourceTrackNumber = track.sourceTrackNumber,
        }),
        trackState_(sequenceRuntime_.createTrackState ? sequenceRuntime_.createTrackState(program, track) : std::any{}),
        programState_(programState),
        current_(startsActive ? track.commandIndex(track.startAddress) : std::optional<u32>{}) {
    addInitialTrackEvents(performanceTrack_, behavior_, includeGlobalInitialEvents);
    for (auto& event : performanceTrack_.events) {
      std::visit([&](auto& typedEvent) { typedEvent.header.sequence = outputSequence_++; }, event);
    }
    if (startsActive && !current_ && !track_.commands.empty()) {
      warn(fmt::format("Sequence track start ${:04X} was not decoded", track_.startAddress.value), {});
    }
  }

  [[nodiscard]] bool active() const noexcept { return current_.has_value() || pendingTicks_ != 0; }
  [[nodiscard]] u64 tick() const noexcept { return runtime_.tick; }
  [[nodiscard]] u64 nextActionTick() const noexcept {
    if (pendingTicks_ == 0) {
      return runtime_.tick;
    }
    return runtime_.tick == std::numeric_limits<u64>::max() ? runtime_.tick : runtime_.tick + 1;
  }
  [[nodiscard]] std::optional<u64> loopStopTick() const noexcept { return loopStopTick_; }

  [[nodiscard]] SequenceCoordinatorSignal executeNext() {
    if (!current_ && pendingTicks_ == 0) {
      return SequenceCoordinatorSignal::None;
    }
    if (pendingTicks_ != 0) {
      if (runtime_.tick != std::numeric_limits<u64>::max()) {
        ++runtime_.tick;
      }
      tickRuntime(pendingTickCommand_);
      if (pendingTicks_ > 1 && !pendingDelayedCommand_) {
        executeReadyCommandDuringWait();
      }
      --pendingTicks_;
      if (pendingTicks_ != 0) {
        return SequenceCoordinatorSignal::None;
      }
    }

    // The source driver gives one channel control until it schedules another
    // wait. Keep consuming zero-time commands here; yielding between them
    // would let a later channel run too early at the same tick.
    while (current_ && pendingTicks_ == 0) {
      const SourceCommand& command = track_.commands.at(*current_);
      if (!pendingDelayedCommand_ && command.execution.delayTicks != 0) {
        pendingDelayedCommand_ = true;
        scheduleTicks(*current_, command.execution.delayTicks);
        return SequenceCoordinatorSignal::None;
      }
      const bool hadLoopStop = loopStopTick_.has_value();
      const SequenceCoordinatorSignal signal = executeCommand();
      pendingDelayedCommand_ = false;
      if (signal != SequenceCoordinatorSignal::None) {
        return signal;
      }
      if (!hadLoopStop && loopStopTick_) {
        // Let the sequence coordinator observe the newly discovered common
        // loop boundary before this zero-time loop can execute again.
        return SequenceCoordinatorSignal::None;
      }
      if (pendingTicks_ != 0) {
        executeReadyCommandDuringWait();
      }
    }
    return SequenceCoordinatorSignal::None;
  }

  // A section switch preserves the format's typed channel state, but resets
  // source control flow and timing to the shared boundary tick.
  void beginSection(std::optional<Address> start, u64 tick) {
    runtime_.tick = tick;
    runtime_.callStack.clear();
    runtime_.repeat.clear();
    runtime_.lastCommand = {};
    pendingTicks_ = 0;
    pendingDelayedCommand_ = false;
    current_ = start ? track_.commandIndex(*start) : std::optional<u32>{};
    arrivedByControlFlow_ = true;
    loopDetector_.clear();
    loopStopTick_.reset();
    loopRepeats_ = 0;
    if (sequenceRuntime_.beginTrackSection != nullptr) {
      sequenceRuntime_.beginTrackSection(trackState_);
    }
    if (start && !current_) {
      warn(fmt::format("Sequence section target ${:04X} was not decoded", start->value), {});
    }
  }

  [[nodiscard]] SynchronizedLoopTrackSnapshot synchronizedLoopSnapshot(u64 boundary) const {
    u32 remainingTicks = pendingTicks_;
    if (active()) {
      if (runtime_.tick > boundary || boundary - runtime_.tick > remainingTicks) {
        throw std::logic_error("Synchronized loop point was not reached in global track order");
      }
      remainingTicks -= static_cast<u32>(boundary - runtime_.tick);
    }
    return {current_, remainingTicks, pendingTickCommand_, pendingDelayedCommand_};
  }

  void restoreSynchronizedLoop(const SynchronizedLoopTrackSnapshot& snapshot, u64 tick) {
    runtime_.tick = tick;
    current_ = snapshot.current;
    pendingTicks_ = snapshot.pendingTicks;
    pendingTickCommand_ = snapshot.pendingTickCommand;
    pendingDelayedCommand_ = snapshot.delayedCommand;
    arrivedByControlFlow_ = true;
    loopDetector_.clear();
    loopStopTick_.reset();
  }

  void trimAt(u64 tick, bool retainBoundaryEvents) {
    closeActiveNotesAt(tick);
    endTrackAt(performanceTrack_, tick, retainBoundaryEvents);
  }

  void closeActiveNotesAt(u64 tick) { outputAt(tick, runtime_.lastCommand).allNotesOff(); }

  void preserveLoop(u64 startTick, u64 endTick) {
    addLoopMarker(performanceTrack_, {}, startTick, outputSequence_, "Loop Start");
    addLoopMarker(performanceTrack_, runtime_.lastCommand, endTick, outputSequence_, "Loop End");
  }

  [[nodiscard]] PerformanceTrack finish() {
    closeActiveNotesAt(runtime_.tick);
    performanceTrack_.endTick = runtime_.tick;
    // Commands may schedule events inside an earlier note (for example a
    // delayed pitch slide discovered after that note has advanced the VM).
    // Keep the target-neutral performance timeline chronological while
    // preserving source order among events at the same tick.
    std::ranges::stable_sort(performanceTrack_.events, [](const PerformanceEvent& lhs, const PerformanceEvent& rhs) {
      return performanceEventHeader(lhs).tick < performanceEventHeader(rhs).tick;
    });
    return std::move(performanceTrack_);
  }

private:
  [[nodiscard]] PerformanceEmitter outputAt(u64 tick, CommandId command = {}, SourceAnnotationId annotation = {}) {
    return {performanceTrack_,
            command,
            annotation,
            tick,
            outputSequence_,
            runtime_.nextNote,
            runtime_.nextAutomation,
            behavior_.panLaw,
            &runtime_.activeNotes,
            &targetSequence_.sourceSpans};
  }

  void tickRuntime(u32 commandIndex) {
    if (sequenceRuntime_.tick == nullptr) {
      return;
    }
    const SourceCommand& command = track_.commands.at(commandIndex);
    auto out = outputAt(runtime_.tick, CommandId{commandIndex}, command.annotation);
    VmApi vm = detail::VmApiAccess::make(runtime_, targetSequence_, command);
    sequenceRuntime_.tick(command, programState_, trackState_, out, vm);
  }

  void executeReadyCommandDuringWait() {
    if (pendingTicks_ == 0 || pendingDelayedCommand_ || !current_ || sequenceRuntime_.readyDuringWait == nullptr) {
      return;
    }
    const u32 commandIndex = *current_;
    const SourceCommand& command = track_.commands.at(commandIndex);
    if (!command.execution.duringWait) {
      return;
    }
    auto out = outputAt(runtime_.tick, CommandId{commandIndex}, command.annotation);
    VmApi vm = detail::VmApiAccess::make(runtime_, targetSequence_, command);
    if (!sequenceRuntime_.readyDuringWait(command, programState_, trackState_, out, vm)) {
      return;
    }
    static_cast<void>(executeCommand(true));
  }

  [[nodiscard]] SequenceCoordinatorSignal executeCommand(bool duringWait = false) {
    if (executedCommands_ >= behavior_.commandLimit) {
      const SourceCommand& command = track_.commands.at(*current_);
      warn(fmt::format("Sequence VM command limit reached: track={}, address=${:04X}, tick={}, executed={}, limit={}",
                       track_.sourceTrackNumber, command.address.value, runtime_.tick, executedCommands_,
                       behavior_.commandLimit),
           command.range);
      current_ = std::nullopt;
      return SequenceCoordinatorSignal::None;
    }
    const u32 commandIndex = *current_;
    const CommandId commandId{commandIndex};
    const SourceCommand& command = track_.commands.at(commandIndex);
    const VisitState visitState = LoopDetector::visitState(commandIndex, runtime_);
    const auto loop = loopDetector_.observe(visitState, runtime_, arrivedByControlFlow_);
    if (behavior_.inferLoopsFromRepeatedState && loop) {
      if (handleLoop(*loop, commandIndex, visitState).kind == LoopActionKind::StopTrack) {
        return SequenceCoordinatorSignal::None;
      }
    }

    const u64 beginTick = runtime_.tick;
    const size_t firstEvent = performanceTrack_.events.size();
    const size_t firstAutomation = performanceTrack_.automations.size();
    auto out = outputAt(beginTick, commandId, command.annotation);
    VmApi vm = detail::VmApiAccess::make(runtime_, targetSequence_, command);
    const Effects effects = sequenceRuntime_.execute(command, programState_, trackState_, out, vm);
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
      u64 endTick = beginTick == std::numeric_limits<u64>::max() ? beginTick : beginTick + 1;
      if (beginTick <= std::numeric_limits<u64>::max() - effects.advanceTicks) {
        endTick = std::max(endTick, beginTick + effects.advanceTicks);
      } else {
        endTick = std::numeric_limits<u64>::max();
      }
      for (size_t i = firstEvent; i < performanceTrack_.events.size(); ++i) {
        endTick = std::max(endTick, eventEndTick(performanceTrack_.events[i]));
      }
      for (size_t i = firstAutomation; i < performanceTrack_.automations.size(); ++i) {
        const auto& automation = performanceTrack_.automations[i];
        endTick = std::max(endTick, automation.realization.endTick);
      }
      const size_t sourceSpanIndex = targetSequence_.sourceSpans.size();
      targetSequence_.sourceSpans.push_back(SourcePlaybackSpan{
          .annotation = command.annotation,
          .channel = command.sourceChannel,
          .beginTick = beginTick,
          .endTick = endTick,
      });
      for (auto& [_, note] : runtime_.activeNotes.notes) {
        if (note.eventIndex >= firstEvent) {
          note.sourceSpanIndex = sourceSpanIndex;
        }
      }
    }
    runtime_.lastCommand = commandId;
    const SequenceCoordinatorSignal signal = effectiveTransition.kind == CommandTransitionKind::EndSection
                                                 ? SequenceCoordinatorSignal::SectionEnd
                                                 : command.execution.coordinatorSignal;
    applyTransition(commandId, command, effectiveTransition);

    ++executedCommands_;
    return signal;
  }

  [[nodiscard]] LoopAction handleLoop(const LoopPoint& loop, u32 replayIndex,
                                      std::optional<VisitState> recordAfterClear = std::nullopt) {
    // Once a loop is identified, all loop sources use the same export policy:
    // preserve markers, replay for the requested loop count, or stop the track.
    if (loopPolicy_ == LoopPolicy::Preserve) {
      addLoopMarker(performanceTrack_, loop.start.command, loop.start.tick, outputSequence_, "Loop Start");
      addLoopMarker(performanceTrack_, loop.endCommand, loop.endTick, outputSequence_, "Loop End");
      current_ = std::nullopt;
      arrivedByControlFlow_ = false;
      return LoopAction{.kind = LoopActionKind::StopTrack};
    }

    if (loopPolicy_ == LoopPolicy::PlayOnce && loopRepeats_ < options_.sequenceLoops) {
      ++loopRepeats_;
      loopDetector_.clear();
      if (recordAfterClear) {
        loopDetector_.record(*recordAfterClear, VisitRecord{.tick = loop.endTick, .command = CommandId{replayIndex}});
      }
      current_ = replayIndex;
      arrivedByControlFlow_ = true;
      return LoopAction{.kind = LoopActionKind::ContinueExecution};
    }

    if (loopPolicy_ == LoopPolicy::PlayOnce) {
      // Keep shorter channel loops running while the scheduler discovers the
      // longest requested endpoint. The sequence-level cutoff removes any
      // temporary events rendered past that common boundary.
      if (!loopStopTick_) {
        loopStopTick_ = loop.endTick;
      }
      loopDetector_.clear();
      if (recordAfterClear) {
        loopDetector_.record(*recordAfterClear, VisitRecord{.tick = loop.endTick, .command = CommandId{replayIndex}});
      }
      current_ = replayIndex;
      arrivedByControlFlow_ = true;
      return LoopAction{.kind = LoopActionKind::ContinueExecution};
    }

    loopStopTick_ = loop.endTick;
    current_ = std::nullopt;
    arrivedByControlFlow_ = false;
    return LoopAction{.kind = LoopActionKind::StopTrack};
  }

  void applyTransition(CommandId commandId, const SourceCommand& command, const CommandTransition& transition) {
    switch (transition.kind) {
      case CommandTransitionKind::Fallthrough:
        current_ = continuationIndex(track_, commandId, command.flow.continuation);
        arrivedByControlFlow_ = false;
        if (!current_) {
          warn(fmt::format("Sequence continuation ${:04X} was not decoded", command.flow.continuation.value),
               command.range);
        }
        break;

      case CommandTransitionKind::End:
      case CommandTransitionKind::EndSection:
        current_ = std::nullopt;
        arrivedByControlFlow_ = false;
        break;

      case CommandTransitionKind::Jump:
        applyJump(commandId, command, transition.destination, transition.jumpSemantics);
        break;

      case CommandTransitionKind::Call:
        if (const auto returnIndex = continuationIndex(track_, commandId, command.flow.continuation)) {
          runtime_.callStack.push_back(*returnIndex);
        } else {
          warn(fmt::format("Sequence call continuation ${:04X} was not decoded", command.flow.continuation.value),
               command.range);
          current_ = std::nullopt;
          arrivedByControlFlow_ = false;
          break;
        }
        current_ = track_.commandIndex(transition.destination);
        arrivedByControlFlow_ = true;
        if (!current_) {
          warn(fmt::format("Sequence call target ${:04X} was not decoded", transition.destination.value),
               command.range);
        }
        break;

      case CommandTransitionKind::Return:
        if (runtime_.callStack.empty()) {
          warn("Sequence return had no active call", command.range);
          current_ = std::nullopt;
          arrivedByControlFlow_ = false;
        } else {
          current_ = runtime_.callStack.back();
          runtime_.callStack.pop_back();
          arrivedByControlFlow_ = true;
        }
        break;
    }
  }

  void scheduleTicks(u32 commandIndex, u32 ticks) {
    pendingTicks_ = ticks;
    pendingTickCommand_ = commandIndex;
  }

  void applyJump(CommandId commandId, const SourceCommand& command, Address destinationAddress,
                 JumpSemantics semantics) {
    switch (semantics) {
      case JumpSemantics::Normal:
        applyPlainJump(command, destinationAddress, true, "jump");
        break;
      case JumpSemantics::FiniteBranch:
        applyPlainJump(command, destinationAddress, false, "branch");
        break;
      case JumpSemantics::LoopCandidate:
        applyLoopCandidateJump(commandId, command, destinationAddress);
        break;
      case JumpSemantics::DeclaredLoop:
        applyDeclaredLoop(commandId, command, destinationAddress);
        break;
    }
  }

  void applyPlainJump(const SourceCommand& command, Address destinationAddress, bool arrivedByControlFlow,
                      std::string_view targetName) {
    current_ = track_.commandIndex(destinationAddress);
    arrivedByControlFlow_ = arrivedByControlFlow;
    if (!current_) {
      warn(fmt::format("Sequence {} target ${:04X} was not decoded", targetName, destinationAddress.value),
           command.range);
    }
  }

  void applyLoopCandidateJump(CommandId commandId, const SourceCommand& command, Address destinationAddress) {
    const auto destination = track_.commandIndex(destinationAddress);
    if (!destination) {
      warn(fmt::format("Sequence jump target ${:04X} was not decoded", destinationAddress.value), command.range);
      current_ = std::nullopt;
      arrivedByControlFlow_ = true;
      return;
    }

    const VisitRecord* previousVisit =
        loopDetector_.findLoopCandidateIgnoringRepeatState(*destination, runtime_.callStack);
    if (previousVisit == nullptr) {
      current_ = destination;
      arrivedByControlFlow_ = true;
      return;
    }

    const LoopPoint loop{
        .start = *previousVisit,
        .endCommand = commandId,
        .endTick = runtime_.tick,
    };
    static_cast<void>(handleLoop(loop, *destination));
  }

  void applyDeclaredLoop(CommandId commandId, const SourceCommand& command, Address destinationAddress) {
    const auto destination = track_.commandIndex(destinationAddress);
    if (!destination) {
      warn(fmt::format("Sequence loop target ${:04X} was not decoded", destinationAddress.value), command.range);
      current_ = std::nullopt;
      arrivedByControlFlow_ = false;
      return;
    }

    VisitRecord start{
        .tick = runtime_.tick,
        .command = CommandId{*destination},
    };
    if (const auto* previous = loopDetector_.findExact(LoopDetector::visitState(*destination, runtime_))) {
      start = *previous;
    }

    const LoopPoint loop{
        .start = start,
        .endCommand = commandId,
        .endTick = runtime_.tick,
    };
    static_cast<void>(handleLoop(loop, *destination));
  }

  void warn(std::string message, SourceRange range) {
    targetSequence_.diagnostics.push_back(vmWarning(std::move(message), range));
  }

  const TrackProgram& track_;
  const SequenceRuntime& sequenceRuntime_;
  const SequenceProgramBehavior& behavior_;
  LoopPolicy loopPolicy_;
  const SequenceVmOptions& options_;
  PerformanceSequence& targetSequence_;
  u64& outputSequence_;
  PerformanceTrack performanceTrack_;
  std::any trackState_;
  std::any& programState_;
  VmTrackRuntime runtime_;
  LoopDetector loopDetector_;
  std::optional<u32> current_;
  u32 pendingTicks_ = 0;
  u32 pendingTickCommand_ = 0;
  bool pendingDelayedCommand_ = false;
  u32 executedCommands_ = 0;
  std::optional<u64> loopStopTick_;
  u32 loopRepeats_ = 0;
  bool arrivedByControlFlow_ = true;
};

}  // namespace

RepeatCounter::RepeatCounter(detail::RepeatState& state, u8 slot) noexcept : state_(&state), slot_(slot) {
}

bool RepeatCounter::active() const {
  return state_ != nullptr && state_->active(slot_);
}

bool RepeatCounter::firstVisit() const {
  return !active();
}

u32 RepeatCounter::remainingPlays() const {
  return state_ != nullptr ? state_->remainingPlays(slot_) : 0;
}

void RepeatCounter::start(u32 totalPlays) {
  if (state_ != nullptr) {
    state_->start(slot_, totalPlays);
  }
}

bool RepeatCounter::consumeReplay() {
  return state_ != nullptr && state_->consumeReplay(slot_);
}

void RepeatCounter::finish() {
  if (state_ != nullptr) {
    state_->finish(slot_);
  }
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
  return !runtime_.callStack.empty();
}

RepeatCounter VmApi::repeatCounter(u8 slot) {
  return RepeatCounter(runtime_.repeat, slot);
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

BranchResult VmApi::countedRepeatBreak(u8 slot, Address destination) {
  RepeatCounter counter = repeatCounter(slot);
  if (counter.remainingPlays() == 1) {
    counter.finish();
    return BranchResult{
        .taken = true,
        .effects = finiteBranch(destination),
    };
  }

  return BranchResult{
      .taken = false,
      .effects = Effects{},
  };
}

u64 VmApi::tick() const noexcept {
  return runtime_.tick;
}

const PerformanceSequence& VmApi::sequence() const noexcept {
  return sequence_;
}

void VmApi::diagnostic(Diagnostic diagnostic) {
  if (!diagnostic.range && command_.range.valid()) {
    diagnostic.range = command_.range;
  }
  if (!diagnostic.annotation && command_.annotation.valid()) {
    diagnostic.annotation = command_.annotation;
  }
  sequence_.diagnostics.push_back(std::move(diagnostic));
}

VmApi::VmApi(detail::VmTrackRuntime& runtime, PerformanceSequence& sequence, const SourceCommand& command)
    : runtime_(runtime), sequence_(sequence), command_(command) {
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

  const LoopPolicy loopPolicy = options_.loopPolicy == LoopPolicy::Default ? behavior.loopPolicy : options_.loopPolicy;

  if (runtime.valid()) {
    // Some formats must inspect the whole song before the first event can be
    // exported. Keep one song-wide state object across an optional silent pass
    // and the real render so collected information is retained.
    std::any programState = runtime.createProgramState ? runtime.createProgramState(program) : std::any{};
    const auto renderSemanticPass = [&](PerformanceSequence& target, std::any& passProgramState) {
      u64 outputSequence = 0;
      std::vector<std::unique_ptr<VmTrackExecutor>> executors;
      executors.reserve(program.tracks.size());
      const bool hasSectionPlaylist = program.sectionPlaylist.has_value();
      for (size_t trackIndex = 0; trackIndex < program.tracks.size(); ++trackIndex) {
        executors.push_back(std::make_unique<VmTrackExecutor>(
            program, runtime, TrackId{static_cast<u32>(trackIndex)}, program.tracks[trackIndex], options_, target,
            outputSequence, executors.empty(), passProgramState, !hasSectionPlaylist));
      }

      std::optional<SectionPlaylistRunner> playlist;
      if (program.sectionPlaylist) {
        playlist.emplace(*program.sectionPlaylist, loopPolicy, options_);
        const PlaylistAdvance first = playlist->advance(0);
        if (first.trackStarts != nullptr) {
          for (size_t i = 0; i < executors.size(); ++i) {
            const std::optional<Address> start = i < first.trackStarts->size() ? (*first.trackStarts)[i] : std::nullopt;
            executors[i]->beginSection(start, 0);
          }
        }
      }

      // Execute the earliest channel first; source track order is the stable
      // tie-break. A channel keeps control at the same tick while it consumes
      // zero-time commands, matching how these drivers run until their next wait.
      std::optional<u64> sequenceEndTick;
      std::optional<u64> synchronizedLoopStartTick;
      std::vector<SynchronizedLoopTrackSnapshot> synchronizedLoopSnapshot;
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
            if (loopPolicy == LoopPolicy::PlayOnce && synchronizedLoopRepeats < options_.sequenceLoops) {
              ++synchronizedLoopRepeats;
              for (size_t i = 0; i < executors.size(); ++i) {
                executors[i]->restoreSynchronizedLoop(synchronizedLoopSnapshot[i], boundary);
              }
              continue;
            }

            if (loopPolicy == LoopPolicy::Preserve && synchronizedLoopStartTick) {
              for (auto& executor : executors) {
                executor->preserveLoop(*synchronizedLoopStartTick, boundary);
              }
            }
            sequenceEndTick = boundary;
            break;
          }
        }

        if (signal == SequenceCoordinatorSignal::SectionEnd && playlist) {
          const u64 boundary = executors[selected]->tick();
          // Tracks are visited in stable source order, so keep same-tick work
          // from tracks processed before the boundary command.
          for (size_t i = 0; i < executors.size(); ++i) {
            executors[i]->trimAt(boundary, i <= selected);
          }
          endSourceSpansAt(target.sourceSpans, boundary);

          const PlaylistAdvance next = playlist->advance(boundary);
          if (next.preservedLoopStart) {
            for (auto& executor : executors) {
              executor->preserveLoop(*next.preservedLoopStart, boundary);
            }
          }
          if (next.trackStarts == nullptr) {
            sequenceEndTick = boundary;
            break;
          }
          for (size_t i = 0; i < executors.size(); ++i) {
            const std::optional<Address> start = i < next.trackStarts->size() ? (*next.trackStarts)[i] : std::nullopt;
            executors[i]->beginSection(start, boundary);
          }
          continue;
        }

        const bool hasLoopBoundary =
            std::ranges::any_of(executors, [](const auto& executor) { return executor->loopStopTick().has_value(); });
        if (!playlist && loopPolicy == LoopPolicy::PlayOnce && hasLoopBoundary &&
            std::ranges::all_of(executors,
                                [](const auto& executor) { return !executor->active() || executor->loopStopTick(); })) {
          sequenceEndTick = 0;
          for (const auto& executor : executors) {
            *sequenceEndTick = std::max(*sequenceEndTick, executor->loopStopTick().value_or(executor->tick()));
          }
          break;
        }
      }

      std::vector<PerformanceTrack> tracks;
      tracks.reserve(executors.size());
      if (sequenceEndTick) {
        for (auto& executor : executors) {
          const u64 noteEndTick = executor->active() ? *sequenceEndTick : std::min(*sequenceEndTick, executor->tick());
          executor->closeActiveNotesAt(noteEndTick);
        }
        endSourceSpansAt(target.sourceSpans, *sequenceEndTick);
      }
      for (auto& executor : executors) {
        auto track = executor->finish();
        if (sequenceEndTick) {
          endTrackAt(track, *sequenceEndTick);
        }
        tracks.push_back(std::move(track));
      }
      return tracks;
    };

    const bool hasPrepass = runtime.finishPrepass != nullptr;
    if (hasPrepass) {
      // Run commands in normal time order but discard every emitted event. This
      // preserves song-wide interactions between tracks during collection,
      // then lets the format prepare its state for the real render.
      PerformanceSequence prepass{
          .timebase = program.timebase,
          .initialTempoMicrosecondsPerQuarter = behavior.initialTempoMicrosecondsPerQuarter,
      };
      prepass.tracks = renderSemanticPass(prepass, programState);
      if (analyzedProgramState != nullptr) {
        sequence.diagnostics = std::move(prepass.diagnostics);
      }
      runtime.finishPrepass(programState);
    }
    if (analyzedProgramState != nullptr) {
      // Analysis needs the same control-flow semantics as rendering, but a
      // format with a prepass has already executed everything required to
      // collect its durable result. Do not perform the discarded output pass.
      if (!hasPrepass) {
        PerformanceSequence analysis{
            .timebase = program.timebase,
            .initialTempoMicrosecondsPerQuarter = behavior.initialTempoMicrosecondsPerQuarter,
        };
        analysis.tracks = renderSemanticPass(analysis, programState);
        sequence.diagnostics = std::move(analysis.diagnostics);
      }
      *analyzedProgramState = std::move(programState);
      return sequence;
    }
    sequence.tracks = renderSemanticPass(sequence, programState);
    if (runtime.finalizePerformance != nullptr) {
      runtime.finalizePerformance(programState, sequence);
    }
    resolveTempoRelativeModulation(sequence);
    return sequence;
  }

  sequence.diagnostics.push_back(vmWarning("Sequence program has no runtime executor", {}));
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
