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

  [[nodiscard]] RepeatStateSnapshot snapshot() const { return RepeatStateSnapshot{.remaining = remaining_}; }

private:
  std::map<u8, u32> remaining_;
};

// Mutable playback state for one track. The parsed SequenceProgram stays unchanged
// while the VM advances ticks, calls, repeats, and loop detection.
struct VmTrackRuntime {
  u64 tick = 0;
  u64 outputSequence = 0;
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
        .cents = static_cast<u16>(static_cast<u16>(*behavior.initialPitchBendRangeSemitones) * 100),
    });
  }
}

void endTrackAt(PerformanceTrack& track, u64 endTick) {
  // Same-tick scheduling can emit another channel before the final loop
  // boundary is known, so trim the completed performance rather than relying
  // only on EndOfTrack metadata.
  std::erase_if(track.events,
                [endTick](const PerformanceEvent& event) { return performanceEventHeader(event).tick >= endTick; });
  for (PerformanceEvent& event : track.events) {
    if (auto* note = std::get_if<NotePerformanceEvent>(&event)) {
      note->durationTicks = static_cast<u32>(std::min<u64>(note->durationTicks, endTick - note->header.tick));
    }
  }
  std::erase_if(track.automations,
                [endTick](const PerformanceAutomation& automation) { return automation.header.tick >= endTick; });
  for (auto& automation : track.automations) {
    std::erase_if(automation.points,
                  [endTick](const PerformanceEvent& event) { return performanceEventHeader(event).tick >= endTick; });
  }
  track.endTick = endTick;
}

struct RenderedTrack {
  PerformanceTrack track;
  std::optional<u64> loopStopTick;
};

// VmTrackExecutor owns the mutable playback state for one track. SequenceVm keeps
// whole-sequence coordination, such as synchronized stopping across tracks.
class VmTrackExecutor {
public:
  VmTrackExecutor(const SequenceProgram& program, const TrackProgram& track, const SequenceDialect& dialect,
                  const SequenceProgramBehavior& behavior, const SequenceVmOptions& options,
                  PerformanceSequence& targetSequence, std::optional<u64> stopTick, std::any* programState = nullptr,
                  bool sequenceCoordinatesLoops = false)
      : track_(track), dialect_(dialect), behavior_(behavior), loopPolicy_(behavior.defaultLoopPolicy),
        options_(options), targetSequence_(targetSequence), stopTick_(stopTick),
        performanceTrack_(PerformanceTrack{
            .id = track.id,
            .sourceTrackNumber = track.sourceTrackNumber,
        }),
        trackState_(dialect.createTrackState != nullptr ? dialect.createTrackState(program, track) : std::any{}),
        programState_(programState), current_(destinationIndex(track, track.startAddress)),
        sequenceCoordinatesLoops_(sequenceCoordinatesLoops) {
    addInitialTrackEvents(performanceTrack_, behavior_);
    for (auto& event : performanceTrack_.events) {
      std::visit([&](auto& typedEvent) { typedEvent.header.sequence = runtime_.outputSequence++; }, event);
    }
    if (!current_ && !track_.commands.empty()) {
      current_ = 0;
    }
  }

  [[nodiscard]] RenderedTrack render() {
    while (active()) {
      executeNext();
    }

    return finish();
  }

  [[nodiscard]] bool active() const noexcept { return current_.has_value(); }
  [[nodiscard]] u64 tick() const noexcept { return runtime_.tick; }
  [[nodiscard]] std::optional<u64> loopStopTick() const noexcept { return loopStopTick_; }

  void executeNext() {
    if (!current_) {
      return;
    }
    if (executedCommands_ >= behavior_.commandLimit) {
      const SourceCommand& command = track_.commands.at(*current_);
      warn(fmt::format("Sequence VM command limit reached: track={}, address=${:04X}, tick={}, executed={}, limit={}",
                       track_.sourceTrackNumber, command.address.value, runtime_.tick, executedCommands_,
                       behavior_.commandLimit),
           command.range);
      current_ = std::nullopt;
      return;
    }
    if (stopTick_ && runtime_.tick >= *stopTick_) {
      current_ = std::nullopt;
      return;
    }

    const SourceCommand& command = track_.commands.at(*current_);
    const VisitState visitState = LoopDetector::visitState(*current_, runtime_);
    if (const auto loop = loopDetector_.observe(visitState, command, runtime_, arrivedByControlFlow_)) {
      if (handleLoop(*loop, *current_, visitState).kind == LoopActionKind::StopTrack) {
        return;
      }
    }

    PerformanceEmitter out{performanceTrack_, command.id, command.annotation, runtime_.tick, runtime_.outputSequence};
    VmApi vm = detail::VmApiAccess::make(runtime_, targetSequence_, command);
    if (programState_ == nullptr || dialect_.execute == nullptr) {
      warn("Missing sequence dialect executor state", command.range);
      current_ = std::nullopt;
      return;
    }
    const Effects effects = dialect_.execute(command, *programState_, trackState_, out, vm);
    advanceTicks(command, effects.advanceTicks);
    runtime_.lastCommand = command.id;
    applyStep(command, effects.step);

    ++executedCommands_;
  }

  [[nodiscard]] RenderedTrack finish() {
    performanceTrack_.endTick = runtime_.tick;
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
      addLoopMarker(performanceTrack_, loop.start.command, loop.start.tick, runtime_.outputSequence, "Loop Start");
      addLoopMarker(performanceTrack_, loop.endCommand, loop.endTick, runtime_.outputSequence, "Loop End");
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

    if (sequenceCoordinatesLoops_ && loopPolicy_ == LoopPolicy::PlayOnce) {
      // Keep shorter channel loops running while the scheduler discovers the
      // longest requested endpoint. The sequence-level cutoff removes any
      // temporary events rendered past that common boundary.
      if (!loopStopTick_) {
        loopStopTick_ = loop.endTick;
      }
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
      PerformanceEmitter out{performanceTrack_, command.id, command.annotation, runtime_.tick, runtime_.outputSequence};
      VmApi vm = detail::VmApiAccess::make(runtime_, targetSequence_, command);
      if (programState_ == nullptr) {
        warn("Missing sequence program state", command.range);
        return;
      }
      dialect_.tick(command, *programState_, trackState_, out, vm);
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
    targetSequence_.diagnostics.push_back(vmWarning(std::move(message), range));
  }

  const TrackProgram& track_;
  const SequenceDialect& dialect_;
  const SequenceProgramBehavior& behavior_;
  LoopPolicy loopPolicy_;
  const SequenceVmOptions& options_;
  PerformanceSequence& targetSequence_;
  std::optional<u64> stopTick_;
  PerformanceTrack performanceTrack_;
  std::any trackState_;
  std::any* programState_ = nullptr;
  VmTrackRuntime runtime_;
  LoopDetector loopDetector_;
  std::optional<u32> current_;
  u32 executedCommands_ = 0;
  std::optional<u64> firstLoopTick_;
  std::optional<u64> loopStopTick_;
  u32 loopRepeats_ = 0;
  bool arrivedByControlFlow_ = true;
  bool sequenceCoordinatesLoops_ = false;
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
    return Effects{.step = jump(destination)};
  }

  counter.finish();
  return Effects{.step = next()};
}

BranchResult VmApi::countedRepeatBreak(u8 slot, Address destination) {
  RepeatCounter counter = repeatCounter(slot);
  if (counter.remainingPlays() == 1) {
    counter.finish();
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

PerformanceSequence SequenceVm::render(const SequenceProgram& program, const SequenceDialect& dialect) const {
  PerformanceSequence sequence{
      .timebase = program.timebase,
  };

  const SequenceProgramBehavior behavior = resolvedBehavior(program, dialect);
  const LoopPolicy loopPolicy = behavior.defaultLoopPolicy;

  if (dialect.execute != nullptr) {
    // Some formats must inspect the whole song before the first event can be
    // exported. Keep one song-wide state object across an optional silent pass
    // and the real render so collected information is retained.
    std::any programState = dialect.createProgramState != nullptr ? dialect.createProgramState(program) : std::any{};
    const auto renderSemanticPass = [&](PerformanceSequence& target) {
      std::vector<std::unique_ptr<VmTrackExecutor>> executors;
      executors.reserve(program.tracks.size());
      for (const TrackProgram& track : program.tracks) {
        executors.push_back(std::make_unique<VmTrackExecutor>(program, track, dialect, behavior, options_, target,
                                                              std::nullopt, &programState,
                                                              loopPolicy == LoopPolicy::PlayOnce));
      }

      // Execute the earliest channel first; source track order is the stable
      // tie-break. A channel keeps control at the same tick while it consumes
      // zero-time commands, matching how these drivers run until their next wait.
      std::optional<u64> sequenceEndTick;
      while (true) {
        size_t selected = executors.size();
        for (size_t i = 0; i < executors.size(); ++i) {
          if (!executors[i]->active()) {
            continue;
          }
          if (selected == executors.size() || executors[i]->tick() < executors[selected]->tick()) {
            selected = i;
          }
        }
        if (selected == executors.size()) {
          break;
        }

        executors[selected]->executeNext();
        const bool hasLoopBoundary =
            std::ranges::any_of(executors, [](const auto& executor) { return executor->loopStopTick().has_value(); });
        if (loopPolicy == LoopPolicy::PlayOnce && hasLoopBoundary &&
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
      for (auto& executor : executors) {
        auto rendered = executor->finish();
        if (sequenceEndTick) {
          endTrackAt(rendered.track, *sequenceEndTick);
        }
        tracks.push_back(std::move(rendered.track));
      }
      return tracks;
    };

    if (dialect.prepass == SemanticPrepassMode::ScheduledPlayback) {
      // Run commands in normal time order but discard every emitted event. This
      // preserves song-wide interactions between tracks during collection.
      PerformanceSequence prepass{.timebase = program.timebase};
      static_cast<void>(renderSemanticPass(prepass));
    } else if (dialect.prepass == SemanticPrepassMode::DecodedCommands) {
      // Some limits must include every valid source block, even when a jump
      // skips that block during normal playback. Run each already-decoded
      // command once in stable order and discard its events, timing, and jumps.
      PerformanceSequence prepass{.timebase = program.timebase};
      for (const TrackProgram& track : program.tracks) {
        std::any trackState =
            dialect.createTrackState != nullptr ? dialect.createTrackState(program, track) : std::any{};
        PerformanceTrack output{
            .id = track.id,
            .sourceTrackNumber = track.sourceTrackNumber,
        };
        VmTrackRuntime runtime;
        for (const SourceCommand& command : track.commands) {
          PerformanceEmitter out{output, command.id, command.annotation, 0, runtime.outputSequence};
          VmApi vm = detail::VmApiAccess::make(runtime, prepass, command);
          static_cast<void>(dialect.execute(command, programState, trackState, out, vm));
        }
      }
    }
    if (dialect.prepass != SemanticPrepassMode::None) {
      if (dialect.finishPrepass != nullptr) {
        // Tell the format that collection is complete before fresh track state
        // is created for the real render.
        dialect.finishPrepass(programState);
      }
    }
    sequence.tracks = renderSemanticPass(sequence);
    return sequence;
  }

  sequence.diagnostics.push_back(vmWarning("Sequence dialect has no executor", {}));
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

  return behavior;
}

}  // namespace vgmtrans::core
