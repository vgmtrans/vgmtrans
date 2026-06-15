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
#include <tuple>
#include <utility>

namespace vgmtrans::core {

namespace detail {

struct RepeatReplayWindow {
  Address beginAddress;
  Address endAddress;
  u32 stopIndex = 0;
  bool hasAddressWindow = false;
};

[[nodiscard]] std::optional<Address> commandEndAddress(const SourceCommand& command) {
  if (command.encodedSize == 0) {
    return std::nullopt;
  }
  if (command.address.value > std::numeric_limits<u64>::max() - command.encodedSize) {
    return std::nullopt;
  }
  return Address{command.address.value + command.encodedSize};
}

struct RepeatStateSnapshot {
  std::map<u8, u32> remaining;

  friend bool operator<(const RepeatStateSnapshot& lhs, const RepeatStateSnapshot& rhs) {
    return lhs.remaining < rhs.remaining;
  }
};

// RepeatState keeps repeat counters out of format commands and exposes a small
// snapshot so loop detection can distinguish normal repeats from infinite loops.
class RepeatState {
public:
  [[nodiscard]] Step repeatUntil(u8 slot, u32 count, Address destination, const SourceCommand& command,
                                 u32 currentIndex) {
    // Most drivers count the first encounter as one iteration. The VM keeps that
    // state here so format commands only name the slot, count, and target.
    auto& remaining = remaining_[slot];
    if (remaining == 0) {
      remaining = count;
    }

    if (remaining > 1) {
      --remaining;
      replayWindow_ = repeatReplayWindow(destination, command, currentIndex);
      return Step::jump(destination);
    }

    remaining_.erase(slot);
    replayWindow_.reset();
    return Step::next();
  }

  [[nodiscard]] Step repeatBreak(u8 slot, Address destination) {
    const auto found = remaining_.find(slot);
    if (found != remaining_.end() && found->second == 1) {
      // A repeat-break branch is taken only on the last repeat pass.
      remaining_.erase(found);
      replayWindow_.reset();
      return Step::branch(destination);
    }
    return Step::next();
  }

  [[nodiscard]] bool isReplayingRepeat(const SourceCommand& command, u32 currentIndex) const {
    if (!replayWindow_) {
      return false;
    }

    const RepeatReplayWindow& window = *replayWindow_;
    if (window.hasAddressWindow) {
      return command.address.value >= window.beginAddress.value && command.address.value < window.endAddress.value;
    }

    // Synthetic tests do not always use encoded command sizes. Keep the older
    // index fallback for those programs; real bytecode should use command addresses.
    return currentIndex <= window.stopIndex;
  }

  [[nodiscard]] RepeatStateSnapshot snapshot() const { return RepeatStateSnapshot{.remaining = remaining_}; }

private:
  [[nodiscard]] static RepeatReplayWindow repeatReplayWindow(Address destination, const SourceCommand& command,
                                                            u32 currentIndex) {
    RepeatReplayWindow window{.stopIndex = currentIndex};
    if (const auto endAddress = commandEndAddress(command)) {
      // Repeat replay suppression covers the contiguous command-address interval
      // between the repeat target and repeat command. Formats with non-contiguous
      // repeat bodies should model that flow explicitly instead of using this helper.
      const u64 destinationEnd =
          destination.value == std::numeric_limits<u64>::max() ? destination.value : destination.value + 1;
      window.beginAddress = Address{std::min(destination.value, command.address.value)};
      window.endAddress = Address{std::max(destinationEnd, endAddress->value)};
      window.hasAddressWindow = true;
    }
    return window;
  }

  std::map<u8, u32> remaining_;
  std::optional<RepeatReplayWindow> replayWindow_;
};

// Mutable playback state for one track. The parsed SequenceProgram stays unchanged
// while the VM advances ticks, calls, repeats, and loop detection.
struct VmTrackRuntime {
  u64 tick = 0;
  std::vector<u32> callStack;
  RepeatState repeat;
  CommandId lastCommand;
};

struct VmApiAccess {
  [[nodiscard]] static VmApi make(VmTrackRuntime& runtime, PerformanceSequence& sequence, const SourceCommand& command,
                                  u32 currentIndex) {
    return VmApi(runtime, sequence, command, currentIndex);
  }
};

}  // namespace detail

namespace {

using detail::RepeatStateSnapshot;
using detail::VmTrackRuntime;

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
      // The next command is usually at address + size. Use that address instead of
      // vector order, because decoding can store commands out of byte order.
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

  [[nodiscard]] std::optional<LoopPoint> observe(const VisitState& state, const SourceCommand& command,
                                                 const VmTrackRuntime& runtime, bool arrivedByControlFlow) {
    // Repeats intentionally revisit commands. While replaying the repeat body, do not
    // treat that revisit as an infinite loop.
    if (runtime.repeat.isReplayingRepeat(command, state.commandIndex)) {
      return std::nullopt;
    }

    if (const auto previous = visited_.find(state); previous != visited_.end()) {
      if (!arrivedByControlFlow) {
        return std::nullopt;
      }
      return LoopPoint{
          .start = previous->second,
          .endCommand = runtime.lastCommand.valid() ? runtime.lastCommand : command.id,
          .endTick = runtime.tick,
      };
    }

    visited_.emplace(state, VisitRecord{.tick = runtime.tick, .command = command.id});
    return std::nullopt;
  }

  [[nodiscard]] const VisitRecord* findExact(const VisitState& state) const {
    const auto found = visited_.find(state);
    return found != visited_.end() ? &found->second : nullptr;
  }

  [[nodiscard]] const VisitRecord* findLoopCandidateIgnoringRepeatState(u32 commandIndex,
                                                                        const std::vector<u32>& callStack) const {
    // JumpOrLoopForever is a source-driver hint that the jump target is a loop
    // point. Repeat counters are ignored here so the hint still applies when the
    // loop command appears while a finite repeat is active.
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

struct RenderedTrack {
  PerformanceTrack track;
  std::optional<u64> loopStopTick;
};

enum class TrackRenderMode {
  Normal,
  DryRunForLoopStop,
};

// VmTrackExecutor owns the mutable playback state for one track. SequenceVm keeps
// whole-sequence coordination, such as synchronized stopping across tracks.
class VmTrackExecutor {
public:
  VmTrackExecutor(const SequenceProgram& program, const TrackProgram& track, const SequenceDialect& dialect,
                  const SequenceProgramBehavior& behavior, const SequenceVmOptions& options,
                  PerformanceSequence& targetSequence, std::optional<u64> stopTick, TrackRenderMode mode)
      : track_(track),
        dialect_(dialect),
        behavior_(behavior),
        loopPolicy_(behavior.defaultLoopPolicy),
        options_(options),
        targetSequence_(targetSequence),
        stopTick_(stopTick),
        mode_(mode),
        performanceTrack_(PerformanceTrack{
            .id = track.id,
            .sourceTrackNumber = track.sourceTrackNumber,
        }),
        trackState_(dialect.createTrackState != nullptr ? dialect.createTrackState(program, track, dialect.context)
                                                        : std::any{}),
        current_(destinationIndex(track, track.startAddress)) {
    addInitialTrackEvents(performanceTrack_, behavior_);
    if (!current_ && !track_.commands.empty()) {
      current_ = 0;
    }
  }

  [[nodiscard]] RenderedTrack render() {
    while (current_) {
      if (executedCommands_ >= behavior_.commandLimit) {
        warn("Sequence VM command limit reached", SourceRange{});
        break;
      }
      if (stopTick_ && runtime_.tick >= *stopTick_) {
        break;
      }

      const SourceCommand& command = track_.commands.at(*current_);
      const VisitState visitState = LoopDetector::visitState(*current_, runtime_);
      if (const auto loop = loopDetector_.observe(visitState, command, runtime_, arrivedByControlFlow_)) {
        if (handleLoop(*loop, *current_, visitState).kind == LoopActionKind::StopTrack) {
          break;
        }
      }

      const CommandHandler* handler = dialect_.handler(command.handler);
      if (handler == nullptr || handler->execute == nullptr) {
        warn(fmt::format("Missing sequence command handler {}", command.handler.value), command.range);
        break;
      }

      PerformanceEmitter out{performanceTrack_, command.id, runtime_.tick};
      VmApi vm = detail::VmApiAccess::make(runtime_, targetSequence_, command, *current_);
      const Effects effects = handler->execute(command, track_, trackState_, out, vm, dialect_.context);
      runtime_.tick += effects.advanceTicks;
      runtime_.lastCommand = command.id;
      applyStep(command, effects.step);

      ++executedCommands_;
    }

    performanceTrack_.endTick = runtime_.tick;
    if (mode_ == TrackRenderMode::DryRunForLoopStop) {
      performanceTrack_.events.clear();
    }
    return RenderedTrack{
        .track = std::move(performanceTrack_),
        .loopStopTick = loopStopTick_ ? loopStopTick_ : firstLoopTick_,
    };
  }

private:
  [[nodiscard]] LoopAction handleLoop(const LoopPoint& loop, u32 replayIndex,
                                      std::optional<VisitState> recordAfterClear = std::nullopt) {
    // Once a loop is identified, all loop sources use the same export policy:
    // preserve markers, replay for the requested loop count, or stop the track.
    if (!firstLoopTick_) {
      firstLoopTick_ = loop.endTick;
    }

    if (loopPolicy_ == LoopPolicy::Preserve) {
      addLoopMarker(performanceTrack_, loop.start.command, loop.start.tick, "Loop Start");
      addLoopMarker(performanceTrack_, loop.endCommand, loop.endTick, "Loop End");
      current_ = std::nullopt;
      arrivedByControlFlow_ = false;
      return LoopAction{.kind = LoopActionKind::StopTrack};
    }

    if (loopPolicy_ == LoopPolicy::PlayOnce && loopRepeats_ < options_.sequenceLoops) {
      ++loopRepeats_;
      loopDetector_.clear();
      if (recordAfterClear) {
        loopDetector_.record(*recordAfterClear,
                             VisitRecord{.tick = loop.endTick, .command = track_.commands.at(replayIndex).id});
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

  void applyStep(const SourceCommand& command, const Step& step) {
    switch (step.kind) {
      case Step::Kind::Next:
        current_ = nextCommandIndex(track_, *current_);
        arrivedByControlFlow_ = false;
        break;

      case Step::Kind::End:
        current_ = std::nullopt;
        arrivedByControlFlow_ = false;
        break;

      case Step::Kind::Jump:
        current_ = destinationIndex(track_, step.destination);
        arrivedByControlFlow_ = true;
        if (!current_) {
          warn(fmt::format("Sequence jump target ${:04X} was not decoded", step.destination.value), command.range);
        }
        break;

      case Step::Kind::Branch:
        current_ = destinationIndex(track_, step.destination);
        arrivedByControlFlow_ = false;
        if (!current_) {
          warn(fmt::format("Sequence branch target ${:04X} was not decoded", step.destination.value), command.range);
        }
        break;

      case Step::Kind::JumpOrLoopForever:
        applyJumpOrLoopForever(command, step.destination);
        break;

      case Step::Kind::LoopForever:
        applyLoopForever(command, step.destination);
        break;

      case Step::Kind::Call:
        if (const auto returnIndex = nextCommandIndex(track_, *current_)) {
          runtime_.callStack.push_back(*returnIndex);
        }
        current_ = destinationIndex(track_, step.destination);
        arrivedByControlFlow_ = true;
        if (!current_) {
          warn(fmt::format("Sequence call target ${:04X} was not decoded", step.destination.value), command.range);
        }
        break;

      case Step::Kind::Return:
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

  void applyJumpOrLoopForever(const SourceCommand& command, Address destinationAddress) {
    const auto destination = destinationIndex(track_, destinationAddress);
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
        .endCommand = command.id,
        .endTick = runtime_.tick,
    };
    static_cast<void>(handleLoop(loop, *destination));
  }

  void applyLoopForever(const SourceCommand& command, Address destinationAddress) {
    const auto destination = destinationIndex(track_, destinationAddress);
    if (!destination) {
      warn(fmt::format("Sequence loop target ${:04X} was not decoded", destinationAddress.value), command.range);
      current_ = std::nullopt;
      arrivedByControlFlow_ = false;
      return;
    }

    const SourceCommand& destinationCommand = track_.commands.at(*destination);
    VisitRecord start{
        .tick = runtime_.tick,
        .command = destinationCommand.id,
    };
    if (const auto* previous = loopDetector_.findExact(LoopDetector::visitState(*destination, runtime_))) {
      start = *previous;
    }

    const LoopPoint loop{
        .start = start,
        .endCommand = command.id,
        .endTick = runtime_.tick,
    };
    static_cast<void>(handleLoop(loop, *destination));
  }

  void warn(std::string message, SourceRange range) {
    if (mode_ == TrackRenderMode::DryRunForLoopStop) {
      return;
    }
    targetSequence_.diagnostics.push_back(vmWarning(std::move(message), range));
  }

  const TrackProgram& track_;
  const SequenceDialect& dialect_;
  const SequenceProgramBehavior& behavior_;
  LoopPolicy loopPolicy_;
  const SequenceVmOptions& options_;
  PerformanceSequence& targetSequence_;
  std::optional<u64> stopTick_;
  TrackRenderMode mode_ = TrackRenderMode::Normal;
  PerformanceTrack performanceTrack_;
  std::any trackState_;
  VmTrackRuntime runtime_;
  LoopDetector loopDetector_;
  std::optional<u32> current_;
  u32 executedCommands_ = 0;
  std::optional<u64> firstLoopTick_;
  std::optional<u64> loopStopTick_;
  u32 loopRepeats_ = 0;
  bool arrivedByControlFlow_ = true;
};

}  // namespace

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
  return runtime_.repeat.repeatUntil(slot, count, destination, command_, currentIndex_);
}

Effects VmApi::repeatUntilEffect(u8 slot, u32 count, Address destination) {
  return Effects{.step = repeatUntil(slot, count, destination)};
}

Step VmApi::repeatBreak(u8 slot, Address destination) {
  return runtime_.repeat.repeatBreak(slot, destination);
}

BranchResult VmApi::repeatBreakBranch(u8 slot, Address destination) {
  const Step step = repeatBreak(slot, destination);
  return BranchResult{
      .taken = step.kind == Step::Kind::Jump || step.kind == Step::Kind::Branch,
      .effects = Effects{.step = step},
  };
}

u64 VmApi::tick() const noexcept {
  return runtime_.tick;
}

void VmApi::diagnostic(Diagnostic diagnostic) {
  if (!diagnostic.range && command_.range.valid()) {
    diagnostic.range = command_.range;
  }
  sequence_.diagnostics.push_back(std::move(diagnostic));
}

VmApi::VmApi(detail::VmTrackRuntime& runtime, PerformanceSequence& sequence, const SourceCommand& command,
             u32 currentIndex)
    : runtime_(runtime), sequence_(sequence), command_(command), currentIndex_(currentIndex) {
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

  std::optional<u64> synchronizedStopTick;
  if (loopPolicy == LoopPolicy::PlayOnce && behavior.stopAllTracksAtFirstLoop) {
    // First find when each track reaches its loop. Then render all tracks again,
    // stopping them at the earliest loop tick.
    PerformanceSequence dryRunSequence{
        .timebase = program.timebase,
    };
    for (const TrackProgram& track : program.tracks) {
      const auto rendered = VmTrackExecutor(program, track, dialect, behavior, options_, dryRunSequence, std::nullopt,
                                            TrackRenderMode::DryRunForLoopStop)
                                .render();
      if (rendered.loopStopTick && (!synchronizedStopTick || *rendered.loopStopTick < *synchronizedStopTick)) {
        synchronizedStopTick = rendered.loopStopTick;
      }
    }
  }

  for (size_t i = 0; i < program.tracks.size(); ++i) {
    sequence.tracks.push_back(
        VmTrackExecutor(program, program.tracks[i], dialect, behavior, options_, sequence, synchronizedStopTick,
                        TrackRenderMode::Normal)
            .render()
            .track);
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
