/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/core/SequenceVm.h"

#include <any>
#include <fmt/format.h>
#include <optional>
#include <set>
#include <stdexcept>
#include <utility>

namespace vgmtrans::core {

struct VmTrackRuntime {
  u64 tick = 0;
  std::vector<u32> callStack;
  std::map<u8, u32> repeatRemaining;
  std::optional<u32> repeatReplayStopIndex;
};

namespace {

[[nodiscard]] Diagnostic vmWarning(std::string message, SourceRange range) {
  return Diagnostic{
      .severity = Severity::Warning,
      .message = std::move(message),
      .range = range.valid() ? std::optional<SourceRange>{range} : std::nullopt,
  };
}

[[nodiscard]] std::optional<u32> nextCommandIndex(const TrackProgram& track, u32 index) {
  const u32 next = index + 1;
  if (next >= track.commands.size()) {
    return std::nullopt;
  }
  return next;
}

[[nodiscard]] std::optional<u32> destinationIndex(const TrackProgram& track, Address destination) {
  return track.addressIndex.find(destination);
}

}  // namespace

Emit::Emit(PerformanceTrack& track, CommandId sourceCommand, u64 tick)
    : track_(track), sourceCommand_(sourceCommand), tick_(tick) {
}

void Emit::note(NotePerformanceEvent event) {
  event.header = header();
  track_.events.emplace_back(std::move(event));
}

void Emit::tempo(TempoPerformanceEvent event) {
  event.header = header();
  track_.events.emplace_back(std::move(event));
}

void Emit::instrument(InstrumentPerformanceEvent event) {
  event.header = header();
  track_.events.emplace_back(std::move(event));
}

void Emit::level(LevelPerformanceEvent event) {
  event.header = header();
  track_.events.emplace_back(std::move(event));
}

void Emit::pan(PanPerformanceEvent event) {
  event.header = header();
  track_.events.emplace_back(std::move(event));
}

void Emit::masterLevel(MasterLevelPerformanceEvent event) {
  event.header = header();
  track_.events.emplace_back(std::move(event));
}

void Emit::reverb(ReverbPerformanceEvent event) {
  event.header = header();
  track_.events.emplace_back(std::move(event));
}

void Emit::tuning(TuningPerformanceEvent event) {
  event.header = header();
  track_.events.emplace_back(std::move(event));
}

void Emit::portamento(PortamentoPerformanceEvent event) {
  event.header = header();
  track_.events.emplace_back(std::move(event));
}

void Emit::modulation(ModulationPerformanceEvent event) {
  event.header = header();
  track_.events.emplace_back(std::move(event));
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
    runtime_.repeatReplayStopIndex = currentIndex_;
    return jump(destination);
  }

  runtime_.repeatRemaining.erase(slot);
  runtime_.repeatReplayStopIndex.reset();
  return next();
}

Step VmApi::repeatBreak(u8 slot, Address destination) {
  const auto found = runtime_.repeatRemaining.find(slot);
  if (found == runtime_.repeatRemaining.end() || found->second <= 1) {
    runtime_.repeatReplayStopIndex.reset();
    return jump(destination);
  }
  return next();
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

SequenceVm::SequenceVm(LoopPolicy loopPolicy) : loopPolicy_(loopPolicy) {
}

PerformanceSequence SequenceVm::render(const SequenceProgram& program, const SequenceDialect& dialect) const {
  PerformanceSequence sequence{
      .timebase = program.timebase,
  };

  const LoopPolicy loopPolicy = resolvedLoopPolicy(program, dialect);
  for (const TrackProgram& track : program.tracks) {
    PerformanceTrack performanceTrack{
        .id = track.id,
        .sourceTrackNumber = track.sourceTrackNumber,
    };

    std::any trackState = dialect.createTrackState != nullptr ? dialect.createTrackState(program, track, dialect.context)
                                                              : std::any{};
    VmTrackRuntime runtime;
    // A command reached through a different return stack is a distinct playback
    // state. This keeps repeated subroutine calls from looking like loops.
    std::set<std::pair<u32, std::vector<u32>>> visited;
    std::optional<u32> current = destinationIndex(track, track.startAddress);
    if (!current && !track.commands.empty()) {
      current = 0;
    }

    u32 executedCommands = 0;
    while (current) {
      if (executedCommands >= program.behavior.commandLimit) {
        sequence.diagnostics.push_back(vmWarning("Sequence VM command limit reached", SourceRange{}));
        break;
      }

      const bool replayingRepeat =
          runtime.repeatReplayStopIndex && *current <= *runtime.repeatReplayStopIndex;
      const auto visitState = std::pair{*current, runtime.callStack};
      if (loopPolicy != LoopPolicy::Preserve && visited.contains(visitState) && !replayingRepeat) {
        break;
      }
      if (!replayingRepeat) {
        visited.insert(visitState);
      }

      const SourceCommand& command = track.commands.at(*current);
      const CommandHandler* handler = dialect.handler(command.handler);
      if (handler == nullptr || handler->execute == nullptr) {
        sequence.diagnostics.push_back(vmWarning(fmt::format("Missing sequence command handler {}", command.handler.value),
                                                 command.range));
        break;
      }

      Emit emit{performanceTrack, command.id, runtime.tick};
      VmApi vm{runtime, sequence, command.range, *current};
      const Effects effects = handler->execute(command, track, trackState, emit, vm, dialect.context);
      runtime.tick += effects.advanceTicks;

      switch (effects.step.kind) {
        case Step::Kind::Next:
          current = nextCommandIndex(track, *current);
          break;

        case Step::Kind::End:
          current = std::nullopt;
          break;

        case Step::Kind::Jump:
          current = destinationIndex(track, effects.step.destination);
          if (!current) {
            sequence.diagnostics.push_back(
                vmWarning(fmt::format("Sequence jump target ${:04X} was not decoded", effects.step.destination.value),
                          command.range));
          }
          break;

        case Step::Kind::Call: {
          if (const auto returnIndex = nextCommandIndex(track, *current)) {
            runtime.callStack.push_back(*returnIndex);
          }
          current = destinationIndex(track, effects.step.destination);
          if (!current) {
            sequence.diagnostics.push_back(
                vmWarning(fmt::format("Sequence call target ${:04X} was not decoded", effects.step.destination.value),
                          command.range));
          }
          break;
        }

        case Step::Kind::Return:
          if (runtime.callStack.empty()) {
            sequence.diagnostics.push_back(vmWarning("Sequence return had no active call", command.range));
            current = std::nullopt;
          } else {
            current = runtime.callStack.back();
            runtime.callStack.pop_back();
          }
          break;
      }

      ++executedCommands;
    }

    performanceTrack.endTick = runtime.tick;
    sequence.tracks.push_back(std::move(performanceTrack));
  }

  return sequence;
}

LoopPolicy SequenceVm::resolvedLoopPolicy(const SequenceProgram& program, const SequenceDialect& dialect) const {
  if (loopPolicy_ != LoopPolicy::Default) {
    return loopPolicy_;
  }
  if (program.behavior.defaultLoopPolicy != LoopPolicy::Default) {
    return program.behavior.defaultLoopPolicy;
  }
  if (dialect.defaultBehavior.defaultLoopPolicy != LoopPolicy::Default) {
    return dialect.defaultBehavior.defaultLoopPolicy;
  }
  return LoopPolicy::PlayOnce;
}

}  // namespace vgmtrans::core
