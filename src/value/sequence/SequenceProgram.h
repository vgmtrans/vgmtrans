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
#include <string_view>
#include <unordered_map>
#include <variant>
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

// Decoded command flow drives discovery and supplies the runtime default.
// continuation is recorded independently because every encoded command has a
// physical successor even when its default transition is a jump, call, return,
// or end.
struct CommandFlow {
  Address continuation;
  CommandTransition defaultTransition;
  std::vector<Address> additionalTargets;

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

  template <class Visitor>
  void forEachDiscoveryTarget(Visitor&& visitor) const {
    if (defaultTransition.kind == CommandTransitionKind::Jump ||
        defaultTransition.kind == CommandTransitionKind::Call) {
      std::invoke(visitor, defaultTransition.destination);
    }
    for (const Address target : additionalTargets) {
      std::invoke(visitor, target);
    }
  }

  [[nodiscard]] bool endsPlayback() const noexcept {
    return defaultTransition.kind == CommandTransitionKind::End ||
           defaultTransition.kind == CommandTransitionKind::EndSection;
  }

  [[nodiscard]] bool unconditionalJump() const noexcept {
    return defaultTransition.kind == CommandTransitionKind::Jump;
  }

  [[nodiscard]] bool callTarget() const noexcept {
    return defaultTransition.kind == CommandTransitionKind::Call;
  }

  [[nodiscard]] std::optional<Address> defaultDestination() const noexcept {
    if (defaultTransition.kind != CommandTransitionKind::Jump &&
        defaultTransition.kind != CommandTransitionKind::Call) {
      return std::nullopt;
    }
    return defaultTransition.destination;
  }
};

using SemanticOperandValue = std::variant<bool, u64, s64, double, Address, std::string>;
// Compiled programs are process-local executable values. One erased callable
// retains a source command's typed behavior without a second argument language.
using CommandBody = std::function<Effects(void* playback)>;
using CommandPredicate = std::function<bool(void* playback)>;

struct CommandExecution {
  // Cursor helpers compose their operations while decoding. The durable source
  // command retains only the resulting body, not an inspectable micro-program.
  CommandBody body;
  // Some drivers poll the next command while the current wait is still active.
  // The predicate is format-owned; SequenceVm only provides the polling timing.
  CommandPredicate duringWait;

  [[nodiscard]] bool valid() const noexcept { return static_cast<bool>(body); }
};

// The role is intentionally small and format-independent. Operand names are the
// executor's precise vocabulary; roles let generic analysis and SourceMap
// projection recognize the few relationships shared by all drivers.
enum class SemanticOperandRole : u8 {
  Value,
  Channel,
  NoteKey,
  Duration,
  Pitch,
  Level,
  Pan,
  Modulation,
  State,
  Count,
  Address,
  JumpTarget,
  CallTarget,
  LoopTarget,
  RepeatTarget,
  Instrument,
  InstrumentBank,
  InstrumentProgram,
  InstrumentTablePointer,
};

struct SemanticOperand {
  // These values describe the source command for annotations and analysis.
  // Compiler-cursor playback captures its typed values independently. The
  // optional encoded form is useful only when showing both raw and resolved
  // values materially improves the source presentation.
  SemanticOperandValue value;
  SourceRange range;
  std::string name;
  SourceValueDisplay display = SourceValueDisplay::Default;
  SemanticOperandRole role = SemanticOperandRole::Value;
  std::optional<SemanticOperandValue> encodedValue;
  std::string encodedName;
  SourceValueDisplay encodedDisplay = SourceValueDisplay::Default;
};

[[nodiscard]] SourceValue semanticOperandSourceValue(const SemanticOperandValue& value);

// One decoded source opcode. Commands retain source metadata, discovery flow,
// and source-free execution data. Encoded bytes remain in SourceStore and are
// reached through range when a view needs them.
struct SourceCommand {
  CommandId id;
  u8 opcode = 0;
  Address address;
  SourceRange range;
  SourceAnnotationId annotation;
  SequenceSemantic semantic = SequenceSemantic::Unknown;
  std::vector<SemanticOperand> operands;
  CommandFlow flow;
  CommandExecution execution;
};

// Operand names are presentation vocabulary for source inspection and analysis.
// This lookup is not part of compiler-cursor playback.
[[nodiscard]] const SemanticOperand* semanticOperand(const SourceCommand& command, std::string_view name);

// Maps source addresses to command indexes. The VM uses this for jumps, calls,
// and finding the next command by source address when vector order is different.
struct AddressIndex {
  std::unordered_map<u64, u32> commandByAddress;

  void add(Address address, u32 commandIndex);
  [[nodiscard]] std::optional<u32> find(Address address) const;
};

struct TrackProgram {
  TrackId id;
  u32 sourceTrackNumber = 0;
  Address startAddress;
  std::vector<SourceCommand> commands;
  AddressIndex addressIndex;
};

// Some drivers arrange a song as a playlist of parallel track sections. A
// section starts every listed channel at once, and the first EndSection command
// advances the whole playlist. Track state survives that boundary; call stacks
// and other control-flow state do not.
struct SequenceSection {
  Address address;
  // Entries align with SequenceProgram::tracks. nullopt means that channel is
  // inactive for this section.
  std::vector<std::optional<Address>> trackStarts;
};

struct PlaylistPlaySection {
  Address section;
};

struct PlaylistRepeat {
  // Number of additional jumps after the first pass through the destination.
  u32 additionalPlays = 0;
  Address destination;
  bool infinite = false;
};

struct PlaylistEnd {};

using PlaylistOperation = std::variant<PlaylistPlaySection, PlaylistRepeat, PlaylistEnd>;

struct PlaylistCommand {
  Address address;
  Address fallthrough;
  SourceRange range;
  PlaylistOperation operation;
};

struct SectionPlaylist {
  Address startAddress;
  std::vector<SequenceSection> sections;
  std::vector<PlaylistCommand> commands;
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
  std::vector<TrackProgram> tracks;
  std::optional<SectionPlaylist> sectionPlaylist;
};

[[nodiscard]] const TrackProgram* trackById(const SequenceProgram& program, TrackId id);
[[nodiscard]] const SourceCommand* sourceCommandById(const TrackProgram& track, CommandId id);
[[nodiscard]] bool trackUsesSemantic(const TrackProgram& track, SequenceSemantic semantic);
[[nodiscard]] bool sequenceUsesSemantic(const SequenceProgram& program, SequenceSemantic semantic);
[[nodiscard]] SourceRange sequenceSourceRange(ByteReader reader, SourceRange baseRange, const SequenceProgram& program);

struct SequenceProgramAsset {
  AssetMetadata metadata;
  SequenceProgram program;
};

class TrackProgramBuilder {
public:
  explicit TrackProgramBuilder(TrackProgram& track);

  const SourceCommand& addSemantic(Address address, u8 opcode, SourceRange range,
                                   std::vector<SemanticOperand> operands, CommandFlow flow,
                                   SourceAnnotationId annotation = {}, CommandExecution execution = {},
                                   SequenceSemantic semantic = SequenceSemantic::Unknown);

private:
  TrackProgram& track_;
};

}  // namespace vgmtrans::core
