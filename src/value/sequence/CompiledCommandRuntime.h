/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

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

// Program, stream, and track state follow the same rule: use context and immutable
// settings when requested, otherwise allow a plain state object.
template <class State, class Context>
[[nodiscard]] std::any createCompiledState(const Context& context) {
  if constexpr (std::constructible_from<State, const Context&>) {
    return State{context};
  } else if constexpr (std::default_initializable<State>) {
    return State{};
  } else {
    static_assert(alwaysFalse<State>, "Compiled state has no supported construction path");
  }
}

template <class State, class Context, class Config>
[[nodiscard]] std::any createCompiledState(const Context& context, const Config& config) {
  if constexpr (std::constructible_from<State, const Context&, const Config&>) {
    return State{context, config};
  } else if constexpr (std::constructible_from<State, const Config&>) {
    return State{config};
  } else {
    return createCompiledState<State>(context);
  }
}

}  // namespace detail

template <class Playback, class ProgramState = EmptyCompiledProgramState>
struct CompiledCommandRuntime {
  using TrackState = typename Playback::TrackState;
  using StreamState = typename Playback::StreamState;

  template <class Execute>
  [[nodiscard]] static decltype(auto) withPlayback(std::any& programState, std::any& trackState,
                                                   PerformanceEmitter& out, VmApi& vm, Execute execute) {
    auto& typedProgramState = std::any_cast<ProgramState&>(programState);
    auto& typedTrackState = std::any_cast<TrackState&>(trackState);
    const auto context = [&] {
      if constexpr (std::is_void_v<StreamState>) {
        return SequencePlayback<TrackState>{typedTrackState, out, vm};
      } else {
        return SequencePlayback<TrackState, StreamState>{{typedTrackState, out, vm},
                                                         std::any_cast<StreamState&>(vm.streamState())};
      }
    }();
    // Playback may also borrow song-wide state alongside its execution context.
    if constexpr (requires { Playback{context, typedProgramState}; }) {
      Playback playback{context, typedProgramState};
      return execute(playback);
    } else if constexpr (requires { Playback{context}; }) {
      Playback playback{context};
      return execute(playback);
    } else {
      static_assert(detail::alwaysFalse<Playback>, "Playback has no supported construction path");
    }
  }

  [[nodiscard]] static Effects execute(const SourceCommand& command, std::any& programState, std::any& trackState,
                                       PerformanceEmitter& out, VmApi& vm) {
    return withPlayback(programState, trackState, out, vm, [&](Playback& playback) {
      // Per-command driver work runs at execution time. beginSection handles
      // startup output that must precede the first command's delay.
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

  static void installHooks(SequenceRuntime& runtime) {
    runtime.execute = execute;
    runtime.readyDuringWait = readyDuringWait;
    if constexpr (requires(Playback& playback) { playback.beginSection(true); }) {
      runtime.beginTrackSection = [](bool first, std::any& programState, std::any& trackState, PerformanceEmitter& out,
                                     VmApi& vm) {
        withPlayback(programState, trackState, out, vm, [first](Playback& playback) { playback.beginSection(first); });
      };
    }
    if constexpr (requires(Playback& playback) { playback.tick(); }) {
      // Rebuild the lightweight view so fades use the current emitter and VM position.
      runtime.tick = [](const SourceCommand&, std::any& programState, std::any& trackState, PerformanceEmitter& out,
                        VmApi& vm) {
        withPlayback(programState, trackState, out, vm, [](Playback& playback) { playback.tick(); });
      };
    }
    if constexpr (requires(ProgramState& state) { state.finishPrepass(); }) {
      // Collected results stay in the same typed object for the real render.
      runtime.finishPrepass = [](std::any& state) { std::any_cast<ProgramState&>(state).finishPrepass(); };
    }
    if constexpr (!std::is_void_v<StreamState>) {
      if constexpr (requires(StreamState& state) { state.beginSection(true); }) {
        runtime.beginStreamSection = [](std::any& state, bool first) {
          std::any_cast<StreamState&>(state).beginSection(first);
        };
      }
    }
    if constexpr (requires(ProgramState& state, PerformanceSequence& performance) {
                    state.finalizePerformance(performance);
                  }) {
      runtime.finalizePerformance = [](std::any& state, PerformanceSequence& performance) {
        std::any_cast<ProgramState&>(state).finalizePerformance(performance);
      };
    }
  }
};

// Construct one complete erased runtime for a format whose state needs no
// program-specific immutable configuration.
template <class Playback, class ProgramState = EmptyCompiledProgramState>
[[nodiscard]] SequenceRuntime makeCompiledRuntime() {
  using Compiled = CompiledCommandRuntime<Playback, ProgramState>;
  SequenceRuntime runtime;
  runtime.createProgramState = [](const SequenceProgram& program) {
    return detail::createCompiledState<ProgramState>(program);
  };
  runtime.createTrackState = [](TrackStateContext context) {
    return detail::createCompiledState<typename Playback::TrackState>(context);
  };
  if constexpr (!std::is_void_v<typename Playback::StreamState>) {
    runtime.createStreamState = [](StreamStateContext context) {
      return detail::createCompiledState<typename Playback::StreamState>(context);
    };
  }
  Compiled::installHooks(runtime);
  return runtime;
}

// Construct one complete erased runtime whose state factories close over
// immutable typed format settings.
template <class Playback, class ProgramState = EmptyCompiledProgramState, class Config>
[[nodiscard]] SequenceRuntime makeCompiledRuntime(Config config) {
  using Compiled = CompiledCommandRuntime<Playback, ProgramState>;
  using TrackState = typename Playback::TrackState;
  using StreamState = typename Playback::StreamState;
  constexpr bool programConsumesConfig = std::constructible_from<ProgramState, const SequenceProgram&, const Config&> ||
                                         std::constructible_from<ProgramState, const Config&>;
  constexpr bool trackConsumesConfig = std::constructible_from<TrackState, const TrackStateContext&, const Config&> ||
                                       std::constructible_from<TrackState, const Config&>;
  constexpr bool streamConsumesConfig =
      std::constructible_from<StreamState, const StreamStateContext&, const Config&> ||
      std::constructible_from<StreamState, const Config&>;
  static_assert(programConsumesConfig || trackConsumesConfig || streamConsumesConfig,
                "A supplied runtime Config must be consumed by program, stream, or channel state");
  SequenceRuntime runtime;
  auto settings = std::make_shared<const Config>(std::move(config));
  runtime.createProgramState = [settings](const SequenceProgram& sequence) {
    return detail::createCompiledState<ProgramState>(sequence, *settings);
  };
  runtime.createTrackState = [settings](TrackStateContext context) {
    return detail::createCompiledState<TrackState>(context, *settings);
  };
  if constexpr (!std::is_void_v<StreamState>) {
    runtime.createStreamState = [settings](StreamStateContext context) {
      return detail::createCompiledState<StreamState>(context, *settings);
    };
  }
  Compiled::installHooks(runtime);
  return runtime;
}

// Execute a compiled program and project its final typed song state into a
// durable value. This is intended for sequence-defined synth preparation and
// similar analysis that must share playback's calls, repeats, and timing. The
// format-facing projector remains fully typed; only this adapter touches any.
template <class ProgramState, class Project>
[[nodiscard]] std::remove_cvref_t<std::invoke_result_t<Project&, const ProgramState&>> analyzeCompiledProgram(
    const SequenceProgram& program, Project project, std::vector<Diagnostic>* diagnostics = nullptr,
    SequenceVmOptions options = {}) {
  const std::any state = detail::analyzeSequenceProgram(SequenceVm(options), program, diagnostics);
  return std::invoke(project, std::any_cast<const ProgramState&>(state));
}

}  // namespace vgmtrans::core
