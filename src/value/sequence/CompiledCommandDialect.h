/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/sequence/CompilerCursor.h"
#include "value/sequence/SequenceVm.h"

#include <any>
#include <concepts>
#include <stdexcept>

namespace vgmtrans::core {

// This adapter is the only place a compiled format sees std::any. Format
// commands and Playback methods remain fully typed.
struct EmptyCompiledProgramState {};

template <class TrackState, class Playback, class ProgramState = EmptyCompiledProgramState>
struct CompiledCommandDialect {
  [[nodiscard]] static std::any createProgramState(const SequenceProgram& program) {
    // A format can read immutable program settings in its constructor. Formats
    // that need no settings keep working with an ordinary default constructor.
    if constexpr (std::constructible_from<ProgramState, const SequenceProgram&>) {
      return ProgramState{program};
    } else {
      return ProgramState{};
    }
  }

  [[nodiscard]] static std::any createTrackState(const SequenceProgram& program, const TrackProgram& track) {
    // Choose the most informative constructor the state type provides. This
    // keeps version and track identity out of loosely typed extra settings.
    if constexpr (std::constructible_from<TrackState, const SequenceProgram&, const TrackProgram&>) {
      return TrackState{program, track};
    } else if constexpr (std::constructible_from<TrackState, const SequenceProgram&>) {
      return TrackState{program};
    } else if constexpr (std::constructible_from<TrackState, const TrackProgram&>) {
      return TrackState{track};
    } else {
      return TrackState{};
    }
  }

  template <class Execute>
  [[nodiscard]] static decltype(auto) withPlayback(std::any& programState, std::any& trackState,
                                                   PerformanceEmitter& out, VmApi& vm, Execute execute) {
    auto& typedProgramState = std::any_cast<ProgramState&>(programState);
    auto& typedTrackState = std::any_cast<TrackState&>(trackState);
    // Playback may ask for song-wide state as a fourth reference. Simpler
    // formats continue to use the original three-reference form.
    if constexpr (requires { Playback{typedTrackState, out, vm, typedProgramState}; }) {
      Playback playback{typedTrackState, out, vm, typedProgramState};
      return execute(playback);
    } else {
      Playback playback{typedTrackState, out, vm};
      return execute(playback);
    }
  }

  [[nodiscard]] static Effects execute(const SourceCommand& command, std::any& programState, std::any& trackState,
                                       PerformanceEmitter& out, VmApi& vm) {
    return withPlayback(programState, trackState, out, vm, [&](Playback& playback) {
      // Formats use this optional hook for information that must be emitted
      // before whichever command happens to be first.
      if constexpr (requires { playback.beforeCommand(); }) {
        playback.beforeCommand();
      }

      if (!command.execution.body) {
        return Effects{};
      }
      return command.execution.body(&playback);
    });
  }

  [[nodiscard]] static bool readyDuringWait(const SourceCommand& command, std::any& programState, std::any& trackState,
                                            PerformanceEmitter& out, VmApi& vm) {
    if (!command.execution.duringWait) {
      return false;
    }
    return withPlayback(programState, trackState, out, vm,
                        [&](Playback& playback) { return command.execution.duringWait(&playback); });
  }

  static void tick(const SourceCommand&, std::any& programState, std::any& trackState, PerformanceEmitter& out,
                   VmApi& vm) {
    // Rebuild the lightweight Playback view for each elapsed tick so active
    // fades can use the current emitter and VM position without storing either.
    static_cast<void>(withPlayback(programState, trackState, out, vm, [](Playback& playback) {
      if constexpr (requires { playback.tick(); }) {
        playback.tick();
      }
      return Effects{};
    }));
  }

  static void finishPrepass(std::any& programState) {
    auto& typedProgramState = std::any_cast<ProgramState&>(programState);
    // Give the format one clear boundary between silent collection and the
    // real render. Collected results remain in the same typed object.
    if constexpr (requires { typedProgramState.finishPrepass(); }) {
      typedProgramState.finishPrepass();
    }
  }

  static void beginTrackSection(std::any& trackState) {
    auto& typedTrackState = std::any_cast<TrackState&>(trackState);
    if constexpr (requires { typedTrackState.beginSection(); }) {
      typedTrackState.beginSection();
    }
  }

  static void finalizePerformance(std::any& programState, PerformanceSequence& performance) {
    auto& typedProgramState = std::any_cast<ProgramState&>(programState);
    if constexpr (requires { typedProgramState.finalizePerformance(performance); }) {
      typedProgramState.finalizePerformance(performance);
    }
  }
};

// Fill the mechanical executor hooks while leaving identity, timebase, and
// playback defaults visible in the format's ordinary SequenceDialect value.
template <class TrackState, class Playback, class ProgramState = EmptyCompiledProgramState>
[[nodiscard]] SequenceDialect makeCompiledDialect(SequenceDialect dialect) {
  using Compiled = CompiledCommandDialect<TrackState, Playback, ProgramState>;
  dialect.createProgramState = Compiled::createProgramState;
  dialect.createTrackState = Compiled::createTrackState;
  dialect.execute = Compiled::execute;
  dialect.readyDuringWait = Compiled::readyDuringWait;
  if constexpr (requires(Playback& playback) { playback.tick(); }) {
    dialect.tick = Compiled::tick;
  }
  if constexpr (requires(ProgramState& state) { state.finishPrepass(); }) {
    dialect.finishPrepass = Compiled::finishPrepass;
  }
  if constexpr (requires(TrackState& state) { state.beginSection(); }) {
    dialect.beginTrackSection = Compiled::beginTrackSection;
  }
  if constexpr (requires(ProgramState& state, PerformanceSequence& performance) {
                  state.finalizePerformance(performance);
                }) {
    dialect.finalizePerformance = Compiled::finalizePerformance;
  }
  return dialect;
}

// Execute a compiled program and project its final typed song state into a
// durable value. This is intended for sequence-defined synth preparation and
// similar analysis that must share playback's calls, repeats, and timing. The
// format-facing projector remains fully typed; only this adapter touches any.
template <class ProgramState, class Result>
[[nodiscard]] Result analyzeCompiledProgram(const SequenceProgram& program, const SequenceDialect& dialect,
                                            Result (*project)(const ProgramState&), SequenceVmOptions options = {}) {
  const std::any state = detail::analyzeSequenceProgram(SequenceVm(options), program, dialect);
  return project(std::any_cast<const ProgramState&>(state));
}

}  // namespace vgmtrans::core
