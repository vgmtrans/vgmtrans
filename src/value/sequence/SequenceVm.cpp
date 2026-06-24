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

struct LoopSuppressionWindow {
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

[[nodiscard]] LoopSuppressionWindow loopSuppressionWindow(Address destination, const SourceCommand& command,
                                                          u32 currentIndex) {
  LoopSuppressionWindow window{.stopIndex = currentIndex};
  if (const auto endAddress = commandEndAddress(command)) {
    // This suppression covers the contiguous command-address interval between
    // the repeat target and repeat command. Formats with non-contiguous repeat
    // bodies should use lower-level VM flow instead of the counted-repeat helper.
    const u64 destinationEnd =
        destination.value == std::numeric_limits<u64>::max() ? destination.value : destination.value + 1;
    window.beginAddress = Address{std::min(destination.value, command.address.value)};
    window.endAddress = Address{std::max(destinationEnd, endAddress->value)};
    window.hasAddressWindow = true;
  }
  return window;
}

[[nodiscard]] bool suppressesLoopDetection(const std::optional<LoopSuppressionWindow>& window,
                                           const SourceCommand& command, u32 currentIndex) {
  if (!window) {
    return false;
  }

  if (window->hasAddressWindow) {
    return command.address.value >= window->beginAddress.value && command.address.value < window->endAddress.value;
  }

  // Synthetic tests do not always use encoded command sizes. Keep the older
  // index fallback for those programs; real bytecode should use command addresses.
  return currentIndex <= window->stopIndex;
}

// RepeatState only tracks counters. Jump semantics decide how a repeat branch
// affects loop detection and export policy.
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

  [[nodiscard]] RepeatStateSnapshot snapshot() const { return RepeatStateSnapshot{.remaining = remaining_}; }

private:
  std::map<u8, u32> remaining_;
};

// Mutable playback state for one track. The parsed SequenceProgram stays unchanged
// while the VM advances ticks, calls, repeats, and loop detection.
struct VmTrackRuntime {
  u64 tick = 0;
  std::vector<u32> callStack;
  RepeatState repeat;
  std::optional<LoopSuppressionWindow> loopSuppression;
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
    // Finite repeat replays intentionally revisit commands. While replaying that
    // body, do not treat the revisit as an infinite loop.
    if (suppressesLoopDetection(runtime.loopSuppression, command, state.commandIndex)) {
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
  if (behavior.initialLevel) {
    track.events.emplace_back(LevelPerformanceEvent{
        .header = header,
        .linearGain = *behavior.initialLevel,
        .precisionHint = LevelPrecisionHint::SevenBit,
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
        .semitones = *behavior.initialPitchBendRangeSemitones,
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
      : track_(track), dialect_(dialect), behavior_(behavior), loopPolicy_(behavior.defaultLoopPolicy),
        options_(options), targetSequence_(targetSequence), stopTick_(stopTick), mode_(mode),
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

      if (dialect_.execute == nullptr) {
        warn("Missing sequence dialect executor", command.range);
        break;
      }

      PerformanceEmitter out{performanceTrack_, command.id, command.annotation, runtime_.tick};
      VmApi vm = detail::VmApiAccess::make(runtime_, targetSequence_, command);
      const Effects effects = dialect_.execute(command, track_, trackState_, out, vm, dialect_.context);
      advanceTicks(command, effects.advanceTicks);
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
      case StepKind::Next:
        current_ = nextCommandIndex(track_, *current_);
        arrivedByControlFlow_ = false;
        break;

      case StepKind::End:
        current_ = std::nullopt;
        arrivedByControlFlow_ = false;
        break;

      case StepKind::Jump:
        applyJump(command, step.destination, step.jumpSemantics);
        break;

      case StepKind::Call:
        if (const auto returnIndex = nextCommandIndex(track_, *current_)) {
          runtime_.callStack.push_back(*returnIndex);
        }
        current_ = destinationIndex(track_, step.destination);
        arrivedByControlFlow_ = true;
        if (!current_) {
          warn(fmt::format("Sequence call target ${:04X} was not decoded", step.destination.value), command.range);
        }
        break;

      case StepKind::Return:
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

  void advanceTicks(const SourceCommand& command, u32 ticks) {
    if (ticks == 0) {
      return;
    }
    if (dialect_.tick == nullptr) {
      runtime_.tick += ticks;
      return;
    }

    for (u32 elapsed = 0; elapsed < ticks; ++elapsed) {
      ++runtime_.tick;
      PerformanceEmitter out{performanceTrack_, command.id, command.annotation, runtime_.tick};
      VmApi vm = detail::VmApiAccess::make(runtime_, targetSequence_, command);
      dialect_.tick(command, track_, trackState_, out, vm, dialect_.context);
    }
  }

  void applyJump(const SourceCommand& command, Address destinationAddress, JumpSemantics semantics) {
    switch (semantics) {
      case JumpSemantics::Normal:
        applyPlainJump(command, destinationAddress, true, "jump");
        break;
      case JumpSemantics::FiniteBranch:
        applyPlainJump(command, destinationAddress, false, "branch");
        break;
      case JumpSemantics::FiniteRepeat:
        runtime_.loopSuppression = detail::loopSuppressionWindow(destinationAddress, command, *current_);
        applyPlainJump(command, destinationAddress, true, "repeat");
        break;
      case JumpSemantics::LoopCandidate:
        applyLoopCandidateJump(command, destinationAddress);
        break;
      case JumpSemantics::DeclaredLoop:
        applyDeclaredLoop(command, destinationAddress);
        break;
    }
  }

  void applyPlainJump(const SourceCommand& command, Address destinationAddress, bool arrivedByControlFlow,
                      std::string_view targetName) {
    current_ = destinationIndex(track_, destinationAddress);
    arrivedByControlFlow_ = arrivedByControlFlow;
    if (!current_) {
      warn(fmt::format("Sequence {} target ${:04X} was not decoded", targetName, destinationAddress.value),
           command.range);
    }
  }

  void applyLoopCandidateJump(const SourceCommand& command, Address destinationAddress) {
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

  void applyDeclaredLoop(const SourceCommand& command, Address destinationAddress) {
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

Step VmApi::next() const noexcept {
  return Step::next();
}

Step VmApi::end() const noexcept {
  return Step::end();
}

Step VmApi::jump(Address destination) const noexcept {
  return Step::jump(destination);
}

Step VmApi::finiteBranch(Address destination) const noexcept {
  return Step::jump(destination, JumpSemantics::FiniteBranch);
}

Step VmApi::loopCandidate(Address destination) const noexcept {
  return Step::jump(destination, JumpSemantics::LoopCandidate);
}

Step VmApi::declaredLoop(Address destination) const noexcept {
  return Step::jump(destination, JumpSemantics::DeclaredLoop);
}

Step VmApi::call(Address destination) const noexcept {
  return Step::call(destination);
}

Step VmApi::return_() const noexcept {
  return Step::return_();
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
    return Effects{.step = Step::jump(destination, JumpSemantics::FiniteRepeat)};
  }

  counter.finish();
  runtime_.loopSuppression.reset();
  return Effects{.step = next()};
}

BranchResult VmApi::countedRepeatBreak(u8 slot, Address destination) {
  RepeatCounter counter = repeatCounter(slot);
  if (counter.remainingPlays() == 1) {
    counter.finish();
    runtime_.loopSuppression.reset();
    return BranchResult{
        .taken = true,
        .effects = Effects{.step = finiteBranch(destination)},
    };
  }

  return BranchResult{
      .taken = false,
      .effects = Effects{.step = next()},
  };
}

u64 VmApi::tick() const noexcept {
  return runtime_.tick;
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
    sequence.tracks.push_back(VmTrackExecutor(program, program.tracks[i], dialect, behavior, options_, sequence,
                                              synchronizedStopTick, TrackRenderMode::Normal)
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

  if (program.behavior.initialLevel) {
    behavior.initialLevel = program.behavior.initialLevel;
  } else if (dialect.defaultBehavior.initialLevel) {
    behavior.initialLevel = dialect.defaultBehavior.initialLevel;
  }

  if (program.behavior.initialMonoModeChannels) {
    behavior.initialMonoModeChannels = program.behavior.initialMonoModeChannels;
  } else if (dialect.defaultBehavior.initialMonoModeChannels) {
    behavior.initialMonoModeChannels = dialect.defaultBehavior.initialMonoModeChannels;
  }

  if (program.behavior.initialPitchBendRangeSemitones) {
    behavior.initialPitchBendRangeSemitones = program.behavior.initialPitchBendRangeSemitones;
  } else if (dialect.defaultBehavior.initialPitchBendRangeSemitones) {
    behavior.initialPitchBendRangeSemitones = dialect.defaultBehavior.initialPitchBendRangeSemitones;
  }

  behavior.stopAllTracksAtFirstLoop =
      program.behavior.stopAllTracksAtFirstLoop || dialect.defaultBehavior.stopAllTracksAtFirstLoop;

  return behavior;
}

}  // namespace vgmtrans::core
