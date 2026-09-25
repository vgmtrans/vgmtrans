/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/model/InstrumentIdentity.h"
#include "value/model/AssetRecipe.h"
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
struct TrackStateContext;
struct StreamStateContext;

enum class PitchTransitionRenderingHint {
  Portamento,
  PitchBend,
};

// A parsed program owns the exact process-local runtime that executes it.
// Only state creation is closure-backed so immutable typed format settings can
// be captured without a generic configuration schema.
struct SequenceRuntime {
  std::function<std::any(const SequenceProgram&)> createProgramState;
  std::function<std::any(TrackStateContext)> createTrackState;
  std::function<std::any(StreamStateContext)> createStreamState;
  // Enter an active section before its first delay: stream state first, then
  // each playback track. first is true only on this stream's first entry.
  void (*beginStreamSection)(std::any& streamState, bool first) = nullptr;
  void (*beginTrackSection)(bool first, std::any& programState, std::any& trackState, PerformanceEmitter& out,
                            VmApi& vm) = nullptr;
  // The typed executor identifies the Playback/ProgramState family even when
  // state factories capture different immutable settings.
  Effects (*execute)(const SourceCommand&, std::any& programState, std::any& trackState, PerformanceEmitter& out,
                     VmApi& vm) = nullptr;
  bool (*readyDuringWait)(const SourceCommand&, std::any& programState, std::any& trackState, PerformanceEmitter& out,
                          VmApi& vm) = nullptr;
  void (*tick)(const SourceCommand&, std::any& programState, std::any& trackState, PerformanceEmitter& out,
               VmApi& vm) = nullptr;
  void (*finishPrepass)(std::any& programState) = nullptr;
  void (*finalizePerformance)(std::any& programState, PerformanceSequence& performance) = nullptr;

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
  // Musical destination, independent of a command's source annotation. Empty
  // selects the stream's first channel for global commands and ordinary tracks.
  // An undeclared channel skips the body, retaining the command's delay and flow.
  std::optional<u32> channel;
  // Some drivers poll the next command while the current wait is still active.
  // The predicate reads Playback state; SequenceVm provides the polling timing.
  bool (*duringWait)(void* playback) = nullptr;
  // Some bytecodes encode time before an event rather than after it. Delay the
  // body and its control-flow transition until that event time is reached.
  u32 delayTicks = 0;
  // Notify the sequence coordinator without changing this stream's control flow.
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

// Positional identity in the decoded program, independent of where its events
// are placed when performance tracks are merged, split, or reordered.
struct SourceCommandRef {
  TrackId track;
  CommandId id;

  [[nodiscard]] constexpr bool valid() const noexcept { return track.valid() && id.valid(); }
  friend bool operator==(SourceCommandRef, SourceCommandRef) noexcept = default;
};

struct SequenceStream {
  // One instruction position, clock, and call/loop state serve these channels.
  std::vector<u32> channels{0};
};

struct TrackProgram {
  // Decoded source track, independent of its playback tracks/channels.
  // Most source tracks have one stream. Several streams can execute the same
  // decoded commands separately; an interleaved stream lists all its channels.
  std::vector<SequenceStream> streams{{}};
  std::string name;
  Address startAddress;
  SourceAnnotationId annotation;
  // Commands are stored in strictly increasing source-address order, allowing
  // address lookup without a parallel index. CommandId is stable positional identity.
  std::vector<SourceCommand> commands;

  [[nodiscard]] std::optional<u32> commandIndex(Address address) const;
  [[nodiscard]] const SourceCommand* command(CommandId id) const;
};

// Borrowed source data for one execution stream. Stream state does not belong
// to any particular playback track or channel.
struct StreamStateContext {
  const SequenceProgram& sequence;
  const TrackProgram& track;
};

// Borrowed initialization data for one playback track (one musical channel in
// an interleaved stream). The decoded source track stays alive during playback.
struct TrackStateContext {
  const SequenceProgram& sequence;
  const TrackProgram& track;
  u32 sourceTrackNumber;
};

// Some drivers arrange a song as a playlist of parallel track sections. A play
// command starts the listed streams together. EndSection advances the playlist
// according to waitForAllTracks. Track state survives that boundary; call stacks
// and other stream control-flow state do not.
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
  // One entry per execution stream, in decoded-program and stream order.
  std::vector<std::optional<Address>> streamStarts;
  // Repeat only: zero denotes an infinite repeat; positive values are the
  // number of additional jumps after the first pass through the destination.
  u32 additionalPlays = 0;
};

struct SectionPlaylist {
  Address startAddress;
  std::vector<PlaylistCommand> commands;
  // Wait for every active stream to end the section before advancing the playlist.
  // When false, the first stream to end the section advances the playlist.
  bool waitForAllTracks = false;
};

// Positional pan needs a source-domain law to define its channel gains.
// Unspecified means the program has not declared one; emitting positional pan
// with it is an error.
enum class PanLaw {
  Unspecified,
  ConstantSum,
  EqualPower,
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
  // A channel-pan controller is additive to each voice's intrinsic pan. This
  // differs from both an absolute spatial position and final left/right gain.
  std::optional<double> initialChannelPan;
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
  // A decoded track's position identifies its commands. Its streams determine
  // execution; their channels become the output performance tracks.
  std::vector<TrackProgram> tracks;
  std::optional<SectionPlaylist> sectionPlaylist;

  [[nodiscard]] const SourceCommand* command(SourceCommandRef source) const;
  [[nodiscard]] size_t playbackTrackCount() const;
  [[nodiscard]] size_t streamCount() const;
};

[[nodiscard]] bool trackUsesSemantic(const TrackProgram& track, SequenceSemantic semantic);
[[nodiscard]] bool sequenceUsesSemantic(const SequenceProgram& program, SequenceSemantic semantic);
[[nodiscard]] SourceRange sequenceSourceRange(ByteReader reader, SourceRange baseRange, const SequenceProgram& program);

struct SequenceProgramAsset {
  AssetMetadata metadata;
  SequenceProgram program;
  AssetPrivateData privateData;
  std::optional<SequenceCollection> collection;
  SequenceRecipe recipe;
  SequencePreparer prepare;
};

}  // namespace vgmtrans::core
