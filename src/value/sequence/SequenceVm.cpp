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

  void clear() { remaining_.clear(); }

  [[nodiscard]] RepeatStateSnapshot snapshot() const { return RepeatStateSnapshot{.remaining = remaining_}; }

private:
  std::map<u8, u32> remaining_;
};

// Mutable playback state for one track. The parsed SequenceProgram stays unchanged
// while the VM advances ticks, calls, repeats, and loop detection.
struct VmTrackRuntime {
  u64 tick = 0;
  u64 outputSequence = 0;
  u32 nextNote = 0;
  u32 nextAutomation = 0;
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

void endTrackAt(PerformanceTrack& track, u64 endTick, u64 noteEndTick,
                bool retainBoundaryEvents,
                bool retainBoundaryAutomations = false) {
  // Same-tick scheduling can emit another channel before the final loop
  // boundary is known, so trim the completed performance rather than relying
  // only on EndOfTrack metadata.
  const auto continuesAfterBoundary = [](const PerformanceAutomation& automation) {
    const auto* scalar = std::get_if<ScalarPerformanceAutomationIntent>(&automation.intent);
    // Ordinary fades are scheduled controller state and remain committed once
    // their command executes. Note envelopes and pitch transitions are driven
    // by the track tick and stop when the section deactivates that track.
    return scalar != nullptr && scalar->motion != PerformanceAutomationMotion::Envelope;
  };
  const auto committedScheduledAutomation = [&](const PerformanceEventHeader& header) {
    if (!header.automation) {
      return false;
    }
    const auto found = std::ranges::find(track.automations, *header.automation,
                                         &PerformanceAutomation::id);
    return found != track.automations.end() && continuesAfterBoundary(*found) &&
           (found->header.tick < endTick ||
            (retainBoundaryAutomations && found->header.tick == endTick));
  };
  std::erase_if(track.events, [&](const PerformanceEvent& event) {
    const PerformanceEventHeader& header = performanceEventHeader(event);
    const bool outside = retainBoundaryEvents ? header.tick > endTick
                                              : header.tick >= endTick;
    return outside && !committedScheduledAutomation(header);
  });
  for (PerformanceEvent& event : track.events) {
    if (auto* note = std::get_if<NotePerformanceEvent>(&event)) {
      note->durationTicks =
          static_cast<u32>(std::min<u64>(note->durationTicks, noteEndTick - note->header.tick));
    }
  }
  std::erase_if(track.automations, [=](PerformanceAutomation& automation) {
    // Judge commitment by the source command, not by the delayed realization.
    // A command reached at the boundary is real, but its tick-driven motion
    // cannot continue after the section scheduler deactivates the track.
    const bool speculative = retainBoundaryAutomations
                                 ? automation.header.tick > endTick
                                 : automation.header.tick >= endTick;
    if (speculative) {
      return true;
    }
    if (continuesAfterBoundary(automation)) {
      return false;
    }
    automation.realization.startTick =
        std::min(automation.realization.startTick, endTick);
    automation.realization.endTick =
        std::min(automation.realization.endTick, endTick);
    return false;
  });
  track.endTick = endTick;
}

[[nodiscard]] std::optional<u64> noteEndTick(const PerformanceTrack& track, PerformanceNoteId id) {
  std::optional<u64> endTick;
  for (const auto& event : track.events) {
    const auto* note = std::get_if<NotePerformanceEvent>(&event);
    if (note == nullptr || note->note != id) {
      continue;
    }
    const u64 candidate = note->header.tick > std::numeric_limits<u64>::max() - note->durationTicks
                              ? std::numeric_limits<u64>::max()
                              : note->header.tick + note->durationTicks;
    endTick = std::max(endTick.value_or(0), candidate);
  }
  return endTick;
}

void finalizeAutomations(PerformanceTrack& track) {
  for (auto& automation : track.automations) {
    auto* pitch = pitchTransitionIntent(automation);
    if (pitch == nullptr || pitch->continuesAcrossNotes) {
      continue;
    }
    const auto noteEnd = noteEndTick(track, pitch->note);
    if (!noteEnd || *noteEnd >= automation.realization.endTick) {
      continue;
    }
    automation.realization.endTick = std::max(automation.realization.startTick, *noteEnd);
  }
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

struct RenderedTrack {
  PerformanceTrack track;
  std::optional<u64> loopStopTick;
  u64 scheduledWakeTick = 0;
};

struct PlaylistVisitState {
  u32 commandIndex = 0;
  std::map<u32, u32> repeatRemaining;

  friend bool operator<(const PlaylistVisitState& lhs, const PlaylistVisitState& rhs) {
    return std::tie(lhs.commandIndex, lhs.repeatRemaining) <
           std::tie(rhs.commandIndex, rhs.repeatRemaining);
  }
};

struct PlaylistAdvance {
  const SequenceSection* section = nullptr;
  bool ended = false;
  bool preservedLoop = false;
  u64 loopStartTick = 0;
};

// Interprets the small, source-independent control graph between parallel
// sections. Formats normalize their raw playlist quirks into play, repeat, and
// end operations; synchronized scheduling remains a generic VM concern.
class SectionPlaylistRunner {
public:
  SectionPlaylistRunner(const SectionPlaylist& playlist, LoopPolicy loopPolicy,
                        const SequenceVmOptions& options)
      : playlist_(playlist), loopPolicy_(loopPolicy), options_(options),
        current_(commandIndex(playlist.startAddress)) {}

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
              .ended = true,
              .preservedLoop = loopPolicy_ == LoopPolicy::Preserve,
              .loopStartTick = previous->second,
          };
        }
      } else {
        visited_.emplace(state, tick);
      }

      const PlaylistCommand& command = playlist_.commands[*current_];
      if (const auto* play = std::get_if<PlaylistPlaySection>(&command.operation)) {
        current_ = commandIndex(command.fallthrough);
        const auto section = sectionByAddress(play->section);
        return PlaylistAdvance{
            .section = section,
            .ended = section == nullptr,
        };
      }
      if (const auto* repeat = std::get_if<PlaylistRepeat>(&command.operation)) {
        if (repeat->infinite) {
          current_ = commandIndex(repeat->destination);
          continue;
        }

        const auto [counter, _] =
            repeatRemaining_.try_emplace(*current_, repeat->additionalPlays);
        if (counter->second != 0) {
          --counter->second;
          current_ = commandIndex(repeat->destination);
        } else {
          repeatRemaining_.erase(*current_);
          current_ = commandIndex(command.fallthrough);
        }
        continue;
      }

      return PlaylistAdvance{.ended = true};
    }
    return PlaylistAdvance{.ended = true};
  }

private:
  [[nodiscard]] std::optional<u32> commandIndex(Address address) const {
    const auto found = std::ranges::find_if(
        playlist_.commands, [address](const PlaylistCommand& command) { return command.address.value == address.value; });
    if (found == playlist_.commands.end()) {
      return std::nullopt;
    }
    return static_cast<u32>(std::distance(playlist_.commands.begin(), found));
  }

  [[nodiscard]] const SequenceSection* sectionByAddress(Address address) const {
    const auto found = std::ranges::find_if(
        playlist_.sections, [address](const SequenceSection& section) { return section.address.value == address.value; });
    return found == playlist_.sections.end() ? nullptr : &*found;
  }

  const SectionPlaylist& playlist_;
  LoopPolicy loopPolicy_ = LoopPolicy::PlayOnce;
  const SequenceVmOptions& options_;
  std::optional<u32> current_;
  std::map<u32, u32> repeatRemaining_;
  std::map<PlaylistVisitState, u64> visited_;
  u32 loopRepeats_ = 0;
};

// VmTrackExecutor owns the mutable playback state for one track. SequenceVm keeps
// whole-sequence coordination, such as synchronized stopping across tracks.
class VmTrackExecutor {
public:
  VmTrackExecutor(const SequenceProgram& program, const TrackProgram& track, const SequenceDialect& dialect,
                  const SequenceProgramBehavior& behavior, const SequenceVmOptions& options,
                  PerformanceSequence& targetSequence, std::optional<u64> stopTick, std::any* programState = nullptr,
                  bool sequenceCoordinatesLoops = false, bool startsActive = true,
                  bool discoverSectionSourceRange = false)
      : track_(track), dialect_(dialect), behavior_(behavior), loopPolicy_(behavior.defaultLoopPolicy),
        options_(options), targetSequence_(targetSequence), stopTick_(stopTick),
        performanceTrack_(PerformanceTrack{
            .id = track.id,
            .sourceTrackNumber = track.sourceTrackNumber,
        }),
        trackState_(dialect.createTrackState != nullptr ? dialect.createTrackState(program, track) : std::any{}),
        programState_(programState),
        current_(startsActive ? destinationIndex(track, track.startAddress) : std::optional<u32>{}),
        sectionTickPending_(startsActive),
        sequenceCoordinatesLoops_(sequenceCoordinatesLoops),
        discoverSectionSourceRange_(discoverSectionSourceRange) {
    addInitialTrackEvents(performanceTrack_, behavior_);
    for (auto& event : performanceTrack_.events) {
      std::visit([&](auto& typedEvent) { typedEvent.header.sequence = runtime_.outputSequence++; }, event);
    }
    if (startsActive && !current_ && !track_.commands.empty()) {
      current_ = 0;
    }
  }

  [[nodiscard]] RenderedTrack render() {
    while (active()) {
      static_cast<void>(executeNext());
    }

    return finish();
  }

  [[nodiscard]] bool active() const noexcept {
    return current_.has_value() || pendingTicks_ != 0;
  }
  [[nodiscard]] u64 tick() const noexcept { return runtime_.tick; }
  [[nodiscard]] u64 nextActionTick() const noexcept {
    if (sectionTickPending_ || pendingTicks_ == 0) {
      return runtime_.tick;
    }
    return runtime_.tick == std::numeric_limits<u64>::max()
               ? runtime_.tick
               : runtime_.tick + 1;
  }
  [[nodiscard]] std::optional<u64> loopStopTick() const noexcept { return loopStopTick_; }

  [[nodiscard]] bool executeNext() {
    if (!current_ && pendingTicks_ == 0) {
      return false;
    }
    if (sectionTickPending_) {
      sectionTickPending_ = false;
      tickDialect(track_.commands.at(*current_));
    } else if (pendingTicks_ != 0) {
      if (runtime_.tick != std::numeric_limits<u64>::max()) {
        ++runtime_.tick;
      }
      tickDialect(track_.commands.at(pendingTickCommand_));
      --pendingTicks_;
      if (pendingTicks_ != 0) {
        return false;
      }
    }

    // The source driver gives one channel control until it schedules another
    // wait. Keep consuming zero-time commands here; yielding between them
    // would let a later channel run too early at the same tick.
    while (current_ && pendingTicks_ == 0) {
      // A discovered source range is an exclusive parser boundary, checked
      // only when the channel wakes to read another command. Per-tick motion
      // for the wait which reached this address has already run above.
      if (sectionSourceStop_) {
        const SourceCommand& candidate = track_.commands.at(*current_);
        if (candidate.address.value >= *sectionSourceStop_) {
          current_.reset();
          retiredAtSourceStop_ = true;
          break;
        }
      }
      const bool hadLoopStop = loopStopTick_.has_value();
      if (executeCommand()) {
        return true;
      }
      if (!hadLoopStop && loopStopTick_) {
        // Let the sequence coordinator observe the newly discovered common
        // loop boundary before this zero-time loop can execute again.
        return false;
      }
    }
    return false;
  }

  // A section switch preserves the format's typed channel state, but resets
  // source control flow and timing to the shared boundary tick.
  void beginSection(std::optional<Address> start, u64 tick,
                    std::optional<u64> sourceStop = std::nullopt) {
    runtime_.tick = tick;
    runtime_.callStack.clear();
    runtime_.repeat.clear();
    runtime_.lastCommand = {};
    pendingTicks_ = 0;
    current_ = start ? destinationIndex(track_, *start) : std::optional<u32>{};
    sectionTickPending_ = current_.has_value();
    arrivedByControlFlow_ = true;
    loopDetector_.clear();
    firstLoopTick_.reset();
    loopStopTick_.reset();
    loopRepeats_ = 0;
    sectionSourceStop_ = sourceStop;
    discoveredSectionSourceStop_.reset();
    retiredAtSourceStop_ = false;
    if (dialect_.beginTrackSection != nullptr) {
      dialect_.beginTrackSection(trackState_, sourceStop);
    }
    if (start && !current_) {
      warn(fmt::format("Sequence section target ${:04X} was not decoded", start->value), {});
    }
  }

  void trimAt(u64 tick, bool retainBoundaryEvents) {
    // Legacy's tick scheduler stops reading every channel at the shared
    // section boundary, but duration notes are capped at that channel's next
    // scheduled wake-up. Those can differ when one channel ends the section
    // while another is still waiting on a longer note.
    const u64 noteEndTick =
        dialect_.sectionNoteEndPolicy == SectionNoteEndPolicy::ScheduledWake
            ? std::max(tick, scheduledWakeTick())
            : tick;
    endTrackAt(performanceTrack_, tick, noteEndTick, retainBoundaryEvents,
               retainBoundaryEvents);
    if (dialect_.reconcileTrackAfterTrim != nullptr) {
      dialect_.reconcileTrackAfterTrim(trackState_, performanceTrack_, tick);
    }
  }

  [[nodiscard]] std::optional<u64> discoveredSectionSourceStop() const noexcept {
    return discoveredSectionSourceStop_;
  }

  void advanceRetiredMotionTo(u64 tick) {
    if (!retiredAtSourceStop_ || !lastCommandIndex_) {
      return;
    }
    while (runtime_.tick < tick) {
      ++runtime_.tick;
      tickDialect(track_.commands.at(*lastCommandIndex_));
    }
  }

  void endTrackTick(u64 tick) {
    if (dialect_.endTrackTick == nullptr || !lastCommandIndex_) {
      return;
    }
    const SourceCommand& command = track_.commands.at(*lastCommandIndex_);
    PerformanceEmitter out{performanceTrack_,       command.id,        command.annotation,      tick,
                           runtime_.outputSequence, runtime_.nextNote, runtime_.nextAutomation, behavior_.panLaw};
    VmApi vm = detail::VmApiAccess::make(runtime_, targetSequence_, command);
    if (programState_ == nullptr) {
      warn("Missing sequence program state", command.range);
      return;
    }
    dialect_.endTrackTick(command, *programState_, trackState_, out, vm, tick);
  }

  void preservePlaylistLoop(u64 startTick, u64 endTick) {
    addLoopMarker(performanceTrack_, {}, startTick, runtime_.outputSequence, "Loop Start");
    addLoopMarker(performanceTrack_, runtime_.lastCommand, endTick, runtime_.outputSequence, "Loop End");
  }

  [[nodiscard]] RenderedTrack finish() {
    performanceTrack_.endTick = runtime_.tick;
    finalizeAutomations(performanceTrack_);
    // Commands may schedule events inside an earlier note (for example a
    // delayed pitch slide discovered after that note has advanced the VM).
    // Keep the target-neutral performance timeline chronological while
    // preserving source order among events at the same tick.
    std::ranges::stable_sort(performanceTrack_.events, [](const PerformanceEvent& lhs, const PerformanceEvent& rhs) {
      return performanceEventHeader(lhs).tick < performanceEventHeader(rhs).tick;
    });
    return RenderedTrack{
        .track = std::move(performanceTrack_),
        .loopStopTick = loopStopTick_ ? loopStopTick_ : firstLoopTick_,
        .scheduledWakeTick = scheduledWakeTick(),
    };
  }

private:
  [[nodiscard]] u64 scheduledWakeTick() const noexcept {
    if (runtime_.tick > std::numeric_limits<u64>::max() - pendingTicks_) {
      return std::numeric_limits<u64>::max();
    }
    return runtime_.tick + pendingTicks_;
  }

  void tickDialect(const SourceCommand& command) {
    if (dialect_.tick == nullptr) {
      return;
    }
    PerformanceEmitter out{performanceTrack_,       command.id,        command.annotation,      runtime_.tick,
                           runtime_.outputSequence, runtime_.nextNote, runtime_.nextAutomation, behavior_.panLaw};
    VmApi vm = detail::VmApiAccess::make(runtime_, targetSequence_, command);
    if (programState_ == nullptr) {
      warn("Missing sequence program state", command.range);
      return;
    }
    dialect_.tick(command, *programState_, trackState_, out, vm);
    discoverDialectSourceStop(false);
  }

  [[nodiscard]] bool executeCommand() {
    if (executedCommands_ >= behavior_.commandLimit) {
      const SourceCommand& command = track_.commands.at(*current_);
      warn(fmt::format("Sequence VM command limit reached: track={}, address=${:04X}, tick={}, executed={}, limit={}",
                       track_.sourceTrackNumber, command.address.value, runtime_.tick, executedCommands_,
                       behavior_.commandLimit),
           command.range);
      current_ = std::nullopt;
      return false;
    }
    if (stopTick_ && runtime_.tick >= *stopTick_) {
      current_ = std::nullopt;
      return false;
    }

    const u32 commandIndex = *current_;
    const SourceCommand& command = track_.commands.at(commandIndex);
    const VisitState visitState = LoopDetector::visitState(commandIndex, runtime_);
    if (const auto loop = loopDetector_.observe(visitState, command, runtime_, arrivedByControlFlow_)) {
      if (handleLoop(*loop, commandIndex, visitState).kind == LoopActionKind::StopTrack) {
        if (sectionSourceStop_) {
          retiredAtSourceStop_ = true;
        }
        return false;
      }
    }

    const u64 beginTick = runtime_.tick;
    const size_t firstEvent = performanceTrack_.events.size();
    const size_t firstAutomation = performanceTrack_.automations.size();
    PerformanceEmitter out{performanceTrack_,       command.id,        command.annotation,      beginTick,
                           runtime_.outputSequence, runtime_.nextNote, runtime_.nextAutomation, behavior_.panLaw};
    VmApi vm = detail::VmApiAccess::make(runtime_, targetSequence_, command);
    if (programState_ == nullptr || dialect_.execute == nullptr) {
      warn("Missing sequence dialect executor state", command.range);
      current_ = std::nullopt;
      return false;
    }
    const Effects effects = dialect_.execute(command, *programState_, trackState_, out, vm);
    scheduleTicks(commandIndex, effects.advanceTicks);
    discoverSectionCommand(command, effects.step);
    const bool sourceOnlyTerminator =
        dialect_.includeSectionSourceCommand != nullptr &&
        !dialect_.includeSectionSourceCommand(command) &&
        command.flow.kind == DecodeFlow::Kind::Return;
    discoverDialectSourceStop(!sourceOnlyTerminator);
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
      targetSequence_.sourceSpans.push_back(SourcePlaybackSpan{
          .annotation = command.annotation,
          .beginTick = beginTick,
          .endTick = endTick,
      });
    }
    runtime_.lastCommand = command.id;
    lastCommandIndex_ = commandIndex;
    const bool endedSection = effects.step.kind == StepKind::EndSection;
    applyStep(command, effects.step);

    ++executedCommands_;
    return endedSection;
  }

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

      case StepKind::EndSection:
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

  void scheduleTicks(u32 commandIndex, u32 ticks) {
    pendingTicks_ = ticks;
    pendingTickCommand_ = commandIndex;
  }

  void discoverSectionCommand(const SourceCommand& command,
                              const Step& step) {
    if (!discoverSectionSourceRange_) {
      return;
    }
    const auto included = [&](const SourceCommand& candidate) {
      return dialect_.includeSectionSourceCommand == nullptr ||
             dialect_.includeSectionSourceCommand(candidate);
    };
    // A source-only terminator is still part of the replay range when it
    // returns from a called pattern. The same byte at the outermost level is
    // deliberately excluded, allowing replay to stop before a section End
    // that discovery reached only through control flow.
    if (!included(command) && step.kind != StepKind::Return) {
      return;
    }

    rememberDiscoveredSourceStop(
        command.address.value + command.encodedSize,
        included(command));
  }

  void discoverDialectSourceStop(bool includeAdjacentTerminator) {
    if (!discoverSectionSourceRange_ ||
        dialect_.trackSectionSourceStop == nullptr) {
      return;
    }
    const auto stop = dialect_.trackSectionSourceStop(trackState_);
    if (stop) {
      rememberDiscoveredSourceStop(*stop, includeAdjacentTerminator);
    }
  }

  void rememberDiscoveredSourceStop(u64 stop,
                                    bool includeAdjacentTerminator) {
    discoveredSectionSourceStop_ =
        std::max(discoveredSectionSourceStop_.value_or(0), stop);
    if (!includeAdjacentTerminator ||
        dialect_.includeSectionSourceCommand == nullptr) {
      return;
    }

    const auto adjacent = track_.addressIndex.find(Address{stop});
    if (!adjacent) {
      return;
    }
    const SourceCommand& command = track_.commands.at(*adjacent);
    if (!dialect_.includeSectionSourceCommand(command) &&
        command.flow.kind == DecodeFlow::Kind::Return) {
      discoveredSectionSourceStop_ =
          std::max(*discoveredSectionSourceStop_,
                   command.address.value + command.encodedSize);
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
  std::optional<u32> lastCommandIndex_;
  u32 pendingTicks_ = 0;
  u32 pendingTickCommand_ = 0;
  u32 executedCommands_ = 0;
  std::optional<u64> firstLoopTick_;
  std::optional<u64> loopStopTick_;
  u32 loopRepeats_ = 0;
  bool arrivedByControlFlow_ = true;
  bool sectionTickPending_ = false;
  bool sequenceCoordinatesLoops_ = false;
  bool discoverSectionSourceRange_ = false;
  std::optional<u64> sectionSourceStop_;
  std::optional<u64> discoveredSectionSourceStop_;
  bool retiredAtSourceStop_ = false;
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

Step VmApi::endSection() const noexcept {
  return Step::endSection();
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

PerformanceSequence SequenceVm::render(const SequenceProgram& program, const SequenceDialect& dialect,
                                       detail::ProgramStateInspector inspector,
                                       void* inspectionDestination) const {
  PerformanceSequence sequence{
      .timebase = program.timebase,
      .preferredPitchTransitionRendering = dialect.preferredPitchTransitionRendering,
  };

  const SequenceProgramBehavior behavior = resolvedBehavior(program, dialect);
  const LoopPolicy loopPolicy = behavior.defaultLoopPolicy;

  if (dialect.execute != nullptr) {
    // Some formats must inspect the whole song before the first event can be
    // exported. Keep one song-wide state object across an optional silent pass
    // and the real render so collected information is retained.
    std::any programState = dialect.createProgramState != nullptr ? dialect.createProgramState(program) : std::any{};
    using SectionSourceStops =
        std::map<u64, std::vector<std::optional<u64>>>;
    SectionSourceStops discoveredSectionStops;
    const auto renderSemanticPass =
        [&](PerformanceSequence& target, std::any& passProgramState,
            bool discoverSectionStops,
            const SectionSourceStops* replaySectionStops) {
      std::vector<std::unique_ptr<VmTrackExecutor>> executors;
      executors.reserve(program.tracks.size());
      const bool hasSectionPlaylist = program.sectionPlaylist.has_value();
      for (const TrackProgram& track : program.tracks) {
        executors.push_back(std::make_unique<VmTrackExecutor>(program, track, dialect, behavior, options_, target,
                                                              std::nullopt, &passProgramState,
                                                              loopPolicy == LoopPolicy::PlayOnce,
                                                              !hasSectionPlaylist,
                                                              discoverSectionStops));
      }

      std::optional<SectionPlaylistRunner> playlist;
      const SequenceSection* currentSection = nullptr;
      const auto sourceStop = [&](const SequenceSection* section,
                                  size_t track) -> std::optional<u64> {
        if (section == nullptr || replaySectionStops == nullptr) {
          return std::nullopt;
        }
        const auto found = replaySectionStops->find(section->address.value);
        if (found == replaySectionStops->end() ||
            track >= found->second.size()) {
          return std::nullopt;
        }
        return found->second[track];
      };
      const auto rememberDiscoveredStops = [&] {
        if (!discoverSectionStops || currentSection == nullptr) {
          return;
        }
        auto& stops = discoveredSectionStops[currentSection->address.value];
        stops.resize(executors.size());
        for (size_t i = 0; i < executors.size(); ++i) {
          const auto discovered = executors[i]->discoveredSectionSourceStop();
          if (discovered) {
            stops[i] = std::max(stops[i].value_or(0), *discovered);
          }
        }
      };
      if (program.sectionPlaylist) {
        playlist.emplace(*program.sectionPlaylist, loopPolicy, options_);
        const PlaylistAdvance first = playlist->advance(0);
        if (first.section != nullptr) {
          currentSection = first.section;
          for (size_t i = 0; i < executors.size(); ++i) {
            const std::optional<Address> start =
                i < first.section->trackStarts.size() ? first.section->trackStarts[i] : std::nullopt;
            executors[i]->beginSection(start, 0,
                                       sourceStop(first.section, i));
          }
        }
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
          if (selected == executors.size() ||
              executors[i]->nextActionTick() < executors[selected]->nextActionTick()) {
            selected = i;
          }
        }
        if (selected == executors.size()) {
          break;
        }

        const bool endedSection = executors[selected]->executeNext();
        const bool allSectionTracksEnded =
            std::ranges::none_of(executors, [](const auto& executor) {
              return executor->active();
            });
        const bool reachedDiscoveredStops =
            replaySectionStops != nullptr && allSectionTracksEnded;
        const bool advancesPlaylist =
            endedSection || reachedDiscoveredStops;
        if (advancesPlaylist && playlist) {
          // Reaching parser stop ranges exits after the ordinary scheduler
          // increment. An explicit End suppresses that increment and switches
          // sections at the tick which read the command.
          const u64 processedTick = executors[selected]->tick();
          const u64 boundary =
              reachedDiscoveredStops && !endedSection &&
                      processedTick != std::numeric_limits<u64>::max()
                  ? processedTick + 1
                  : processedTick;
          if (reachedDiscoveredStops) {
            for (auto& executor : executors) {
              // Source-inactive channels no longer receive track callbacks,
              // but sliders they already created remain sequence-level work.
              // The compiled dialect models those sliders through its tick
              // hook, so realize their tail through the last processed tick.
              executor->advanceRetiredMotionTo(processedTick);
            }
          }
          rememberDiscoveredStops();
          for (size_t i = 0; i < executors.size(); ++i) {
            // The legacy driver processes channels in source order. Channels
            // through the one that reads End have already handled this tick;
            // later channels are deactivated before their tick callback runs.
            // If every channel instead reached its own discovered source stop,
            // all callbacks at this boundary were processed.
            executors[i]->trimAt(boundary,
                                 reachedDiscoveredStops || i <= selected);
          }
          endSourceSpansAt(target.sourceSpans, boundary);

          const PlaylistAdvance next = playlist->advance(boundary);
          if (next.preservedLoop) {
            for (auto& executor : executors) {
              executor->preservePlaylistLoop(next.loopStartTick, boundary);
            }
          }
          if (next.ended || next.section == nullptr) {
            sequenceEndTick = boundary;
            break;
          }
          currentSection = next.section;
          for (size_t i = 0; i < executors.size(); ++i) {
            const std::optional<Address> start =
                i < next.section->trackStarts.size() ? next.section->trackStarts[i] : std::nullopt;
            executors[i]->beginSection(start, boundary,
                                       sourceStop(next.section, i));
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

        const u64 processedTick = executors[selected]->tick();
        const bool hasActiveTrack =
            std::ranges::any_of(executors, [](const auto& executor) {
              return executor->active();
            });
        const bool sharedTickComplete =
            std::ranges::all_of(executors, [processedTick](const auto& executor) {
              return !executor->active() ||
                     executor->nextActionTick() > processedTick;
            });
        if (hasActiveTrack && sharedTickComplete) {
          for (auto& executor : executors) {
            executor->advanceRetiredMotionTo(processedTick);
          }
          for (auto& executor : executors) {
            executor->endTrackTick(processedTick);
          }
        }
      }
      rememberDiscoveredStops();

      std::vector<PerformanceTrack> tracks;
      tracks.reserve(executors.size());
      if (sequenceEndTick) {
        endSourceSpansAt(target.sourceSpans, *sequenceEndTick);
      }
      for (auto& executor : executors) {
        auto rendered = executor->finish();
        if (sequenceEndTick) {
          const bool scheduledWake =
              playlist && dialect.sectionNoteEndPolicy == SectionNoteEndPolicy::ScheduledWake;
          const u64 noteEndTick = scheduledWake
                                      ? std::max(*sequenceEndTick, rendered.scheduledWakeTick)
                                      : *sequenceEndTick;
          // Reaching the playlist end is a half-open stop, unlike an
          // intermediate section switch where commands already executed at
          // the shared tick remain committed.
          endTrackAt(rendered.track, *sequenceEndTick, noteEndTick, false,
                     playlist.has_value());
        }
        tracks.push_back(std::move(rendered.track));
      }
      return tracks;
    };

    PerformanceSequence prepass{.timebase = program.timebase};
    if (dialect.prepass == SemanticPrepassMode::ScheduledPlayback) {
      // Run commands in normal time order but discard every emitted event. This
      // preserves song-wide interactions between tracks during collection.
      prepass.tracks = renderSemanticPass(
          prepass, programState,
          dialect.sectionEndPolicy ==
              SectionEndPolicy::DiscoveredSourceRange,
          nullptr);
    } else if (dialect.prepass == SemanticPrepassMode::DecodedCommands) {
      // Some limits must include every valid source block, even when a jump
      // skips that block during normal playback. Run each already-decoded
      // command once in stable order and discard its events, timing, and jumps.
      for (const TrackProgram& track : program.tracks) {
        std::any trackState =
            dialect.createTrackState != nullptr ? dialect.createTrackState(program, track) : std::any{};
        PerformanceTrack output{
            .id = track.id,
            .sourceTrackNumber = track.sourceTrackNumber,
        };
        VmTrackRuntime runtime;
        for (const SourceCommand& command : track.commands) {
          PerformanceEmitter out{output,
                                 command.id,
                                 command.annotation,
                                 0,
                                 runtime.outputSequence,
                                 runtime.nextNote,
                                 runtime.nextAutomation,
                                 behavior.panLaw};
          VmApi vm = detail::VmApiAccess::make(runtime, prepass, command);
          static_cast<void>(dialect.execute(command, programState, trackState, out, vm));
        }
      }
    }
    if (dialect.sectionEndPolicy ==
            SectionEndPolicy::DiscoveredSourceRange &&
        dialect.prepass != SemanticPrepassMode::ScheduledPlayback) {
      // Range discovery is structural, so formats without a semantic prepass
      // use an isolated state object and leave their real render state pristine.
      std::any discoveryState =
          dialect.createProgramState != nullptr
              ? dialect.createProgramState(program)
              : std::any{};
      PerformanceSequence discovery{.timebase = program.timebase};
      discovery.tracks =
          renderSemanticPass(discovery, discoveryState, true, nullptr);
    }
    if (dialect.prepass != SemanticPrepassMode::None) {
      if (dialect.finishPrepass != nullptr) {
        // Tell the format that collection is complete before fresh track state
        // is created for the real render.
        dialect.finishPrepass(programState, prepass);
      }
    }
    sequence.tracks = renderSemanticPass(
        sequence, programState, false,
        dialect.sectionEndPolicy ==
                SectionEndPolicy::DiscoveredSourceRange
            ? &discoveredSectionStops
            : nullptr);
    if (dialect.finalizePerformance != nullptr) {
      dialect.finalizePerformance(programState, sequence);
    }
    if (inspector != nullptr) {
      inspector(&programState, inspectionDestination);
    }
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

  if (program.behavior.panLaw != PanLaw::Unspecified) {
    behavior.panLaw = program.behavior.panLaw;
  } else if (dialect.defaultBehavior.panLaw != PanLaw::Unspecified) {
    behavior.panLaw = dialect.defaultBehavior.panLaw;
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
