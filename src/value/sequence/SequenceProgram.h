/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/model/InstrumentIdentity.h"
#include "value/model/MetadataModel.h"
#include "value/model/SourceMap.h"
#include "value/sequence/SequenceExecution.h"

#include <any>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace vgmtrans::core {

class ByteReader;
class PerformanceEmitter;
class VmApi;
struct PerformanceSequence;
struct SequenceProgram;
struct SourceCommand;
struct TrackProgram;

enum class PitchTransitionRenderingHint {
  Portamento,
  PitchBend,
};

using CreateProgramState = std::function<std::any(const SequenceProgram&)>;
using CreateTrackState = std::function<std::any(const SequenceProgram&, const TrackProgram&)>;
using ExecuteCommand = Effects (*)(const SourceCommand&, std::any& programState, std::any& trackState,
                                   PerformanceEmitter& out, VmApi& vm);
using CommandReadyDuringWait = bool (*)(const SourceCommand&, std::any& programState, std::any& trackState,
                                        PerformanceEmitter& out, VmApi& vm);
using TickTrackState = void (*)(const SourceCommand&, std::any& programState, std::any& trackState,
                                PerformanceEmitter& out, VmApi& vm);
using FinishPrepass = void (*)(std::any& programState);
using BeginTrackSection = void (*)(std::any& trackState);
using FinalizePerformance = void (*)(std::any& programState, PerformanceSequence& performance);

// A parsed program owns the exact process-local runtime that executes it.
// Only state creation is closure-backed so immutable typed format settings can
// be captured without a generic configuration schema.
struct SequenceRuntime {
  // Runtimes created by the same typed adapter family share this token even
  // when their state factories capture different immutable settings.
  const void* family = nullptr;
  CreateProgramState createProgramState;
  CreateTrackState createTrackState;
  ExecuteCommand execute = nullptr;
  CommandReadyDuringWait readyDuringWait = nullptr;
  TickTrackState tick = nullptr;
  FinishPrepass finishPrepass = nullptr;
  BeginTrackSection beginTrackSection = nullptr;
  FinalizePerformance finalizePerformance = nullptr;

  [[nodiscard]] bool valid() const noexcept { return execute != nullptr; }
};

// Executable command flow supplies the runtime default and primary discovery
// path. continuation is recorded independently because every encoded command
// has a physical successor even when its default transition is a jump, call,
// return, or end. Decoder-only alternatives never reach this durable value.
struct CommandFlow {
  Address continuation;
  CommandTransition defaultTransition;

  [[nodiscard]] static CommandFlow fallthroughTo(Address continuation) {
    return CommandFlow{
        .continuation = continuation,
        .defaultTransition = CommandTransition::fallthrough(),
    };
  }

  [[nodiscard]] static CommandFlow jumpTo(Address destination, Address continuation,
                                          JumpSemantics semantics = JumpSemantics::Normal) {
    return CommandFlow{
        .continuation = continuation,
        .defaultTransition = CommandTransition::jump(destination, semantics),
    };
  }

  [[nodiscard]] static CommandFlow call(Address destination, Address continuation) {
    return CommandFlow{
        .continuation = continuation,
        .defaultTransition = CommandTransition::call(destination),
    };
  }

  [[nodiscard]] static CommandFlow return_(Address continuation) {
    return CommandFlow{
        .continuation = continuation,
        .defaultTransition = CommandTransition::return_(),
    };
  }

  [[nodiscard]] static CommandFlow end(Address continuation) {
    return CommandFlow{
        .continuation = continuation,
        .defaultTransition = CommandTransition::end(),
    };
  }

  [[nodiscard]] static CommandFlow endSection(Address continuation) {
    return CommandFlow{
        .continuation = continuation,
        .defaultTransition = CommandTransition::endSection(),
    };
  }

  [[nodiscard]] std::optional<Address> discoveryContinuation() const noexcept {
    switch (defaultTransition.kind) {
      case CommandTransitionKind::Fallthrough:
      case CommandTransitionKind::Call:
        return continuation;
      case CommandTransitionKind::Jump:
      case CommandTransitionKind::Return:
      case CommandTransitionKind::End:
      case CommandTransitionKind::EndSection:
        return std::nullopt;
    }
    return std::nullopt;
  }

  [[nodiscard]] bool endsPlayback() const noexcept {
    return defaultTransition.kind == CommandTransitionKind::End ||
           defaultTransition.kind == CommandTransitionKind::EndSection;
  }

  [[nodiscard]] bool unconditionalJump() const noexcept {
    return defaultTransition.kind == CommandTransitionKind::Jump;
  }

  [[nodiscard]] bool callTarget() const noexcept { return defaultTransition.kind == CommandTransitionKind::Call; }

  [[nodiscard]] std::optional<Address> defaultDestination() const noexcept {
    if (defaultTransition.kind != CommandTransitionKind::Jump &&
        defaultTransition.kind != CommandTransitionKind::Call) {
      return std::nullopt;
    }
    return defaultTransition.destination;
  }
};

// Compiled programs are process-local executable values. One erased callable
// retains a source command's typed behavior without a second argument language.
using CommandBody = std::function<Effects(void* playback)>;
using CommandPredicate = std::function<bool(void* playback)>;

enum class SequenceCoordinatorSignal : u8 {
  None,
  SectionEnd,
  SynchronizedLoopStart,
  SynchronizedLoopEnd,
};

struct CommandExecution {
  // Cursor helpers compose their operations while decoding. The durable source
  // command retains only the resulting body, not an inspectable micro-program.
  CommandBody body;
  // Some drivers poll the next command while the current wait is still active.
  // The predicate is format-owned; SequenceVm only provides the polling timing.
  CommandPredicate duringWait;
  // Some bytecodes encode time before an event rather than after it. Delay the
  // body and its control-flow transition until that event time is reached.
  u32 delayTicks = 0;
  // Notify the sequence coordinator without changing this track's control flow.
  SequenceCoordinatorSignal coordinatorSignal = SequenceCoordinatorSignal::None;

  [[nodiscard]] bool valid() const noexcept { return static_cast<bool>(body); }
};

// One executable source opcode. Detailed decoded fields and alternative
// discovery targets are projected into SourceMap and discarded before this
// durable program is assembled.
struct SourceCommand {
  u8 opcode = 0;
  Address address;
  SourceRange range;
  SourceAnnotationId annotation;
  SequenceSemantic semantic = SequenceSemantic::Unknown;
  std::optional<u32> sourceChannel;
  CommandFlow flow;
  CommandExecution execution;
};

struct TrackProgram {
  u32 sourceTrackNumber = 0;
  Address startAddress;
  SourceAnnotationId annotation;
  // Commands are stored in strictly increasing source-address order, allowing
  // address lookup without a parallel index. CommandId is stable positional identity.
  std::vector<SourceCommand> commands;

  [[nodiscard]] std::optional<u32> commandIndex(Address address) const;
  [[nodiscard]] const SourceCommand* command(CommandId id) const;
};

// Some drivers arrange a song as a playlist of parallel track sections. A play
// command starts every listed channel at once, and the first EndSection command
// advances the playlist. Track state survives that boundary; call stacks and
// other control-flow state do not.
enum class PlaylistCommandKind {
  PlaySection,
  Repeat,
  End,
};

struct PlaylistCommand {
  Address address;
  Address fallthrough;
  SourceRange range;
  PlaylistCommandKind kind = PlaylistCommandKind::End;
  // A play command retains the source section address for attribution and
  // carries its normalized entries directly. A repeat command targets another
  // playlist command.
  Address target;
  std::vector<std::optional<Address>> trackStarts;
  // Repeat only: zero denotes an infinite repeat; positive values are the
  // number of additional jumps after the first pass through the destination.
  u32 additionalPlays = 0;
};

struct SectionPlaylist {
  Address startAddress;
  std::vector<PlaylistCommand> commands;
};

// Positional pan needs a source-domain law to define its channel gains.
// Unspecified means the program has not declared one; emitting positional pan
// with it is an error.
enum class PanLaw {
  Unspecified,
  ConstantSum,
  ConstantMaximum,
  EqualPower,
};

struct ChannelPan {
  // A channel-pan controller is additive to each voice's intrinsic pan. This
  // differs from both an absolute spatial position and final left/right gain.
  double position = 0.5;
  PanLaw voicePanLaw = PanLaw::Unspecified;
};

struct StereoBalance {
  double leftGain = 1.0;
  double rightGain = 1.0;
};

// Program-level playback and rendering policy that is not an individual source
// command, such as loop handling, lowering preferences, or initial channel state.
struct SequenceProgramBehavior {
  LoopPolicy loopPolicy = LoopPolicy::PlayOnce;
  u32 commandLimit = 100'000;
  // Detect a loop when a jump returns to the same command with the same nested calls and repeat counts.
  // Disable this when a format keeps other state that can make playback continue differently from that command.
  bool inferLoopsFromRepeatedState = true;
  PitchTransitionRenderingHint preferredPitchTransitionRendering = PitchTransitionRenderingHint::Portamento;
  // Formats that emit a normalized pan position declare its law once here.
  // Formats with exact left/right gains should emit StereoBalance instead.
  PanLaw panLaw = PanLaw::Unspecified;
  // Some drivers rely on channel defaults that are not source opcodes. Keep
  // them in behavior so formats opt in explicitly and exporters can emit stable
  // initialization without attaching it to a fake source command.
  std::optional<InstrumentIdentity> initialSourceInstrument;
  std::optional<double> initialLevel;
  // Song-wide gain is initialized once, independently of per-track level.
  std::optional<double> initialMasterLevel;
  std::optional<double> initialExpression;
  std::optional<double> initialReverbSend;
  std::optional<ChannelPan> initialChannelPan;
  std::optional<StereoBalance> initialStereoBalance;
  std::optional<u8> initialMonoModeChannels;
  std::optional<u8> initialPitchBendRangeSemitones;
  // The source tempo also governs tempo-relative effects before the first
  // explicit tempo command.
  u32 initialTempoMicrosecondsPerQuarter = 500'000;
};

struct SequenceProgram {
  SequenceRuntime runtime;
  Timebase timebase;
  SequenceProgramBehavior behavior;
  // A track's position is its TrackId. sourceTrackNumber separately preserves
  // the channel or slot identity encoded by the source format.
  std::vector<TrackProgram> tracks;
  std::optional<SectionPlaylist> sectionPlaylist;
};

[[nodiscard]] bool trackUsesSemantic(const TrackProgram& track, SequenceSemantic semantic);
[[nodiscard]] bool sequenceUsesSemantic(const SequenceProgram& program, SequenceSemantic semantic);
[[nodiscard]] SourceRange sequenceSourceRange(ByteReader reader, SourceRange baseRange, const SequenceProgram& program);

struct SequenceProgramAsset {
  AssetMetadata metadata;
  SequenceProgram program;
  AssetPrivateData privateData;
};

}  // namespace vgmtrans::core
