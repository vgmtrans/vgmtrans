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
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

namespace vgmtrans::core {

// This adapter is the only place a compiled format sees std::any. Format
// commands and Playback methods remain fully typed.
struct EmptyCompiledProgramState {};

namespace detail {

template <class Type>
inline constexpr bool alwaysFalse = false;

}  // namespace detail

template <class TrackState, class Playback, class ProgramState = EmptyCompiledProgramState>
struct CompiledCommandRuntime {
  [[nodiscard]] static std::any createProgramState(const SequenceProgram& program) {
    // A format can read immutable program settings in its constructor. Formats
    // that need no settings keep working with an ordinary default constructor.
    if constexpr (std::constructible_from<ProgramState, const SequenceProgram&>) {
      return ProgramState{program};
    } else if constexpr (std::default_initializable<ProgramState>) {
      return ProgramState{};
    } else {
      static_assert(detail::alwaysFalse<ProgramState>, "ProgramState has no supported construction path");
    }
  }

  template <class Config>
  [[nodiscard]] static std::any createProgramState(const SequenceProgram& program, const Config& config) {
    if constexpr (std::constructible_from<ProgramState, const SequenceProgram&, const Config&>) {
      return ProgramState{program, config};
    } else if constexpr (std::constructible_from<ProgramState, const Config&>) {
      return ProgramState{config};
    } else {
      return createProgramState(program);
    }
  }

  [[nodiscard]] static std::any createTrackState(const SequenceProgram& program, const TrackProgram& track) {
    // Choose the most informative constructor the state type provides. This
    // keeps track identity out of runtime configuration.
    if constexpr (std::constructible_from<TrackState, const SequenceProgram&, const TrackProgram&>) {
      return TrackState{program, track};
    } else if constexpr (std::constructible_from<TrackState, const SequenceProgram&>) {
      return TrackState{program};
    } else if constexpr (std::constructible_from<TrackState, const TrackProgram&>) {
      return TrackState{track};
    } else if constexpr (std::default_initializable<TrackState>) {
      return TrackState{};
    } else {
      static_assert(detail::alwaysFalse<TrackState>, "TrackState has no supported construction path");
    }
  }

  template <class Config>
  [[nodiscard]] static std::any createTrackState(const SequenceProgram& program, const TrackProgram& track,
                                                 const Config& config) {
    if constexpr (std::constructible_from<TrackState, const SequenceProgram&, const TrackProgram&, const Config&>) {
      return TrackState{program, track, config};
    } else if constexpr (std::constructible_from<TrackState, const TrackProgram&, const Config&>) {
      return TrackState{track, config};
    } else if constexpr (std::constructible_from<TrackState, const Config&>) {
      return TrackState{config};
    } else {
      return createTrackState(program, track);
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
    } else if constexpr (requires { Playback{typedTrackState, out, vm}; }) {
      Playback playback{typedTrackState, out, vm};
      return execute(playback);
    } else {
      static_assert(detail::alwaysFalse<Playback>, "Playback has no supported construction path");
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
    withPlayback(programState, trackState, out, vm, [](Playback& playback) { playback.tick(); });
  }

  static void finishPrepass(std::any& programState) {
    auto& typedProgramState = std::any_cast<ProgramState&>(programState);
    // Give the format one clear boundary between silent collection and the
    // real render. Collected results remain in the same typed object.
    typedProgramState.finishPrepass();
  }

  static void beginTrackSection(std::any& trackState) {
    auto& typedTrackState = std::any_cast<TrackState&>(trackState);
    typedTrackState.beginSection();
  }

  static void finalizePerformance(std::any& programState, PerformanceSequence& performance) {
    auto& typedProgramState = std::any_cast<ProgramState&>(programState);
    typedProgramState.finalizePerformance(performance);
  }
};

namespace detail {

template <class TrackState, class Playback, class ProgramState>
void installCompiledRuntimeHooks(SequenceRuntime& runtime) {
  using Compiled = CompiledCommandRuntime<TrackState, Playback, ProgramState>;
  runtime.execute = Compiled::execute;
  runtime.readyDuringWait = Compiled::readyDuringWait;
  if constexpr (requires(Playback& playback) { playback.tick(); }) {
    runtime.tick = Compiled::tick;
  }
  if constexpr (requires(ProgramState& state) { state.finishPrepass(); }) {
    runtime.finishPrepass = Compiled::finishPrepass;
  }
  if constexpr (requires(TrackState& state) { state.beginSection(); }) {
    runtime.beginTrackSection = Compiled::beginTrackSection;
  }
  if constexpr (requires(ProgramState& state, PerformanceSequence& performance) {
                  state.finalizePerformance(performance);
                }) {
    runtime.finalizePerformance = Compiled::finalizePerformance;
  }
}

}  // namespace detail

// Construct one complete erased runtime for a format whose state needs no
// program-specific immutable configuration.
template <class TrackState, class Playback, class ProgramState = EmptyCompiledProgramState>
[[nodiscard]] SequenceRuntime makeCompiledRuntime() {
  using Compiled = CompiledCommandRuntime<TrackState, Playback, ProgramState>;
  SequenceRuntime runtime;
  runtime.createProgramState = [](const SequenceProgram& program) { return Compiled::createProgramState(program); };
  runtime.createTrackState = [](const SequenceProgram& program, const TrackProgram& track) {
    return Compiled::createTrackState(program, track);
  };
  detail::installCompiledRuntimeHooks<TrackState, Playback, ProgramState>(runtime);
  return runtime;
}

// Construct one complete erased runtime whose state factories close over
// immutable typed format settings.
template <class TrackState, class Playback, class ProgramState = EmptyCompiledProgramState, class Config>
[[nodiscard]] SequenceRuntime makeCompiledRuntime(Config config) {
  using Compiled = CompiledCommandRuntime<TrackState, Playback, ProgramState>;
  constexpr bool programConsumesConfig =
      std::constructible_from<ProgramState, const SequenceProgram&, const Config&> ||
      std::constructible_from<ProgramState, const Config&>;
  constexpr bool trackConsumesConfig =
      std::constructible_from<TrackState, const SequenceProgram&, const TrackProgram&, const Config&> ||
      std::constructible_from<TrackState, const TrackProgram&, const Config&> ||
      std::constructible_from<TrackState, const Config&>;
  static_assert(programConsumesConfig || trackConsumesConfig,
                "A supplied runtime Config must be consumed by ProgramState or TrackState");
  SequenceRuntime runtime;
  auto settings = std::make_shared<const Config>(std::move(config));
  runtime.createProgramState = [settings](const SequenceProgram& sequence) {
    return Compiled::createProgramState(sequence, *settings);
  };
  runtime.createTrackState = [settings](const SequenceProgram& sequence, const TrackProgram& track) {
    return Compiled::createTrackState(sequence, track, *settings);
  };
  detail::installCompiledRuntimeHooks<TrackState, Playback, ProgramState>(runtime);
  return runtime;
}

// Execute a compiled program and project its final typed song state into a
// durable value. This is intended for sequence-defined synth preparation and
// similar analysis that must share playback's calls, repeats, and timing. The
// format-facing projector remains fully typed; only this adapter touches any.
template <class ProgramState, class Project>
[[nodiscard]] std::remove_cvref_t<std::invoke_result_t<Project&, const ProgramState&>> analyzeCompiledProgram(
    const SequenceProgram& program, Project project, SequenceVmOptions options = {}) {
  const std::any state = detail::analyzeSequenceProgram(SequenceVm(options), program);
  return std::invoke(project, std::any_cast<const ProgramState&>(state));
}

}  // namespace vgmtrans::core
