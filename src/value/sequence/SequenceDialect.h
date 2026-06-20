/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/sequence/PerformanceModel.h"
#include "value/sequence/SequenceProgram.h"

#include <any>
#include <concepts>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace vgmtrans::core {

class PerformanceEmitter;
class ItemTreeBuilder;
class VmApi;

enum class StepKind {
  Next,
  End,
  Jump,
  Call,
  Return,
};

enum class JumpSemantics {
  Normal,
  FiniteBranch,
  FiniteRepeat,
  LoopCandidate,
  DeclaredLoop,
};

// Step names the primitive control-flow result of a command. JumpSemantics
// annotates jump-like steps when the VM needs loop-policy context.
struct Step {
  StepKind kind = StepKind::Next;
  Address destination;
  JumpSemantics jumpSemantics = JumpSemantics::Normal;

  [[nodiscard]] static constexpr Step next() noexcept { return Step{.kind = StepKind::Next}; }
  [[nodiscard]] static constexpr Step end() noexcept { return Step{.kind = StepKind::End}; }
  [[nodiscard]] static constexpr Step jump(Address destination,
                                           JumpSemantics semantics = JumpSemantics::Normal) noexcept {
    return Step{.kind = StepKind::Jump, .destination = destination, .jumpSemantics = semantics};
  }
  [[nodiscard]] static constexpr Step call(Address destination) noexcept {
    return Step{.kind = StepKind::Call, .destination = destination};
  }
  [[nodiscard]] static constexpr Step return_() noexcept { return Step{.kind = StepKind::Return}; }
};

struct Effects {
  u32 advanceTicks = 0;
  Step step = Step::next();

  [[nodiscard]] static constexpr Effects none() noexcept { return Effects{}; }
  [[nodiscard]] static constexpr Effects wait(u32 ticks) noexcept { return Effects{.advanceTicks = ticks}; }
};

struct CommandInfoField {
  std::string name;
  std::string value;
};

// Details shown for a parsed command. Core adds operands read from the bytes; the
// format adds fields that explain what those operands mean.
struct CommandInfo {
  std::string name;
  std::string detailKind;
  CommandPlaybackStatus playbackStatus = CommandPlaybackStatus::AffectsPlayback;
  std::vector<CommandInfoField> fields;

  void field(std::string fieldName, std::string value);
  void field(std::string fieldName, double value);
  void field(std::string fieldName, u8 value);
  void field(std::string fieldName, s8 value);
  void field(std::string fieldName, u16 value);
  void field(std::string fieldName, s16 value);
  void field(std::string fieldName, u32 value);
  void field(std::string fieldName, s32 value);
  void field(std::string fieldName, u64 value);
  void field(std::string fieldName, s64 value);
  void field(std::string fieldName, Address value);
};

// Commands can declare relationships they imply, such as "this selects
// instrument bank/program X". Parsers decide which instrument set that refers to.
class CommandReferences {
public:
  void instrument(u32 bank, u32 program, std::optional<SourceRange> range = std::nullopt);
  [[nodiscard]] std::vector<CommandInstrumentReference> takeInstruments();

private:
  std::vector<CommandInstrumentReference> instruments_;
};

using DescribeSourceCommand = void (*)(const SourceCommand&, const TrackProgram&, CommandInfo&,
                                       const std::any& context);
using CollectSourceCommandReferences = void (*)(const SourceCommand&, const TrackProgram&, CommandReferences&,
                                                const std::any& context);
using ExecuteSourceCommand = Effects (*)(const SourceCommand&, const TrackProgram&, std::any& trackState,
                                         PerformanceEmitter& out, VmApi& vm, const std::any& context);
using CreateTrackState = std::any (*)(const SequenceProgram&, const TrackProgram&, const std::any& context);
using CommandTypeToken = const void*;

namespace detail {

template <class Command>
[[nodiscard]] CommandTypeToken commandTypeToken();

}  // namespace detail

struct CommandHandler {
  CommandHandlerId id;
  CommandTypeToken typeToken = nullptr;
  DescribeSourceCommand describe = nullptr;
  CollectSourceCommandReferences collectReferences = nullptr;
  ExecuteSourceCommand execute = nullptr;
};

// SequenceProgram stores a handler ID for behavior and a kind ID for source-facing
// identity. That lets one executable handler serve many opcode names without
// losing distinct UI labels or playback metadata.
struct SequenceDialect {
  DialectId id;
  std::string commandKindPrefix;
  Timebase timebase;
  SequenceProgramBehavior defaultBehavior;
  CreateTrackState createTrackState = nullptr;
  std::vector<CommandKind> kinds;
  std::vector<CommandHandler> handlers;
  std::any context;

  [[nodiscard]] const CommandHandler* handler(CommandHandlerId id) const;
  [[nodiscard]] const CommandHandler* handlerForType(CommandTypeToken typeToken) const;
  template <class Command>
  [[nodiscard]] const CommandHandler* handlerForCommand() const {
    return handlerForType(detail::commandTypeToken<Command>());
  }
  [[nodiscard]] const CommandKind* kind(CommandKindId id) const;
  [[nodiscard]] const CommandKind* kindForName(std::string_view kindName) const;
  [[nodiscard]] CommandInfo describe(const TrackProgram& track, const SourceCommand& command) const;
  [[nodiscard]] std::vector<CommandInstrumentReference> instrumentReferences(const TrackProgram& track,
                                                                             const SourceCommand& command) const;
};

template <class TrackState, class Context>
struct CommandRuntime {
  TrackState& state;
  PerformanceEmitter& out;
  VmApi& vm;
  const Context& context;

  // Format commands use these helpers for emitted musical events. The actual
  // event construction still lives in PerformanceEmitter.
  void note(double key, double linearVelocity, u32 durationTicks, bool extendsPrevious = false);
  void tempo(u32 microsecondsPerQuarter);
  void timeSignature(u8 numerator, u8 denominator, u8 clocksPerMetronomeClick);
  void instrument(u32 bank, u32 program, bool forceBankSelect = false);
  void level(double linearGain, LevelPrecisionHint precisionHint = LevelPrecisionHint::SevenBit);
  void expression(double linearGain, LevelPrecisionHint precisionHint = LevelPrecisionHint::SevenBit);
  void pan(double stereoPosition);
  void pan(double stereoPosition, double linearGain);
  void masterLevel(double linearGain);
  void reverb(double send);
  void tuning(double cents);
  void globalTranspose(s32 semitones);
  void pitchBend(double semitones);
  void pitchBendRange(u8 semitones);
  void portamento(double timeMilliseconds, double previousKey);
  void portamentoEnable(bool enabled);
  void portamentoTime(double timeMilliseconds);
  void portamentoControl(double previousKey);
  void legatoPedal(bool enabled);
  void modulation(ModulationPerformanceTarget target, double amount);

  [[nodiscard]] static constexpr Effects none() noexcept { return Effects::none(); }
  [[nodiscard]] static constexpr Effects wait(u32 ticks) noexcept { return Effects::wait(ticks); }
  [[nodiscard]] static constexpr Effects next() noexcept { return Effects{.step = Step::next()}; }
  [[nodiscard]] static constexpr Effects end() noexcept { return Effects{.step = Step::end()}; }
  [[nodiscard]] static constexpr Effects jump(Address destination) noexcept {
    return Effects{.step = Step::jump(destination)};
  }
  [[nodiscard]] static constexpr Effects finiteBranch(Address destination) noexcept {
    return Effects{.step = Step::jump(destination, JumpSemantics::FiniteBranch)};
  }
  [[nodiscard]] static constexpr Effects loopCandidate(Address destination) noexcept {
    return Effects{.step = Step::jump(destination, JumpSemantics::LoopCandidate)};
  }
  [[nodiscard]] static constexpr Effects declaredLoop(Address destination) noexcept {
    return Effects{.step = Step::jump(destination, JumpSemantics::DeclaredLoop)};
  }
  [[nodiscard]] static constexpr Effects call(Address destination) noexcept {
    return Effects{.step = Step::call(destination)};
  }
  [[nodiscard]] static constexpr Effects return_() noexcept { return Effects{.step = Step::return_()}; }
};

[[nodiscard]] std::string commandInfoDescription(const CommandInfo& info);
[[nodiscard]] ItemId addSourceCommandItem(ItemTreeBuilder& items, std::optional<ItemId> parent,
                                          const SequenceDialect& dialect, const TrackProgram& track,
                                          const SourceCommand& command);
void addSourceCommandItems(ItemTreeBuilder& items, std::optional<ItemId> parent, const SequenceDialect& dialect,
                           const TrackProgram& track);
void addCommandInstrumentReferences(SequenceProgram& program, const SequenceDialect& dialect, const TrackProgram& track,
                                    const SourceCommand& command, std::optional<AssetId> instrumentSetId);
void addSourceCommandItemsAndInstrumentReferences(ItemTreeBuilder& items, std::optional<ItemId> parent,
                                                  SequenceProgram& program, const SequenceDialect& dialect,
                                                  const TrackProgram& track, std::optional<AssetId> instrumentSetId);

class SequenceDialectRegistry {
public:
  void add(SequenceDialect dialect);
  void seal() noexcept;
  [[nodiscard]] const SequenceDialect* find(std::string_view id) const;
  [[nodiscard]] bool contains(std::string_view id) const;
  [[nodiscard]] bool sealed() const noexcept { return sealed_; }

private:
  std::unordered_map<std::string, SequenceDialect> dialects_;
  bool sealed_ = false;
};

namespace detail {

template <class Command>
[[nodiscard]] CommandTypeToken commandTypeToken() {
  static const int token = 0;
  return &token;
}

[[nodiscard]] inline CommandTypeToken preservedCommandTypeToken() {
  static const int token = 0;
  return &token;
}

template <class Command>
concept HasDescribe = requires(const Command& command, CommandInfo& out) { command.describe(out); };

template <class Command>
concept HasParseCommand = requires(CommandReader& in) {
  { Command::parse(in) } -> std::same_as<Command>;
};

template <class Command, class Context>
concept HasDescribeWithContext =
    requires(const Command& command, CommandInfo& out, const Context& context) { command.describe(out, context); };

template <class Command>
concept HasReferences =
    requires(const Command& command, CommandReferences& references) { command.references(references); };

template <class Command, class Context>
concept HasReferencesWithContext = requires(const Command& command, CommandReferences& references,
                                            const Context& context) { command.references(references, context); };

template <class Command>
concept HasSourceCommandReferences =
    requires(const SourceCommand& record, const TrackProgram& track, CommandReferences& references,
             const std::any& context) { Command::references(record, track, references, context); };

template <class Command, class TrackState, class Context>
concept HasRuntimeEffectsExecute = requires(const Command& command, CommandRuntime<TrackState, Context>& rt) {
  { command.execute(rt) } -> std::same_as<Effects>;
};

template <class Command, class TrackState, class Context>
concept HasRuntimeVoidExecute = requires(const Command& command, CommandRuntime<TrackState, Context>& rt) {
  { command.execute(rt) } -> std::same_as<void>;
};

template <class Command, class TrackState, class Context>
concept HasLegacyExecute =
    requires(const Command& command, TrackState& state, PerformanceEmitter& out, VmApi& vm, const Context& context) {
      { command.execute(state, out, vm, context) } -> std::same_as<Effects>;
    };

template <class Command, class TrackState, class Context>
concept HasSourceCommandExecute = requires(const SourceCommand& record, const TrackProgram& track, std::any& trackState,
                                           PerformanceEmitter& out, VmApi& vm, const std::any& context) {
  { Command::execute(record, track, trackState, out, vm, context) } -> std::same_as<Effects>;
};

template <class Command>
concept HasPlaybackStatus = requires { Command::playbackStatus; };

template <class>
inline constexpr bool kAlwaysFalse = false;

template <class Command>
[[nodiscard]] consteval CommandPlaybackStatus commandPlaybackStatus() {
  if constexpr (HasPlaybackStatus<Command>) {
    return Command::playbackStatus;
  } else {
    return CommandPlaybackStatus::AffectsPlayback;
  }
}

inline void describePreservedSourceCommand(const SourceCommand&, const TrackProgram&, CommandInfo&, const std::any&) {
}

inline void collectPreservedSourceCommandReferences(const SourceCommand&, const TrackProgram&, CommandReferences&,
                                                    const std::any&) {
}

inline Effects executePreservedSourceCommand(const SourceCommand&, const TrackProgram&, std::any&, PerformanceEmitter&,
                                             VmApi&, const std::any&) {
  return Effects::none();
}

template <class TrackState, class Context>
std::any createTrackState(const SequenceProgram& program, const TrackProgram& track, const std::any& context) {
  if constexpr (std::constructible_from<TrackState, const SequenceProgram&, const TrackProgram&, const Context&>) {
    return TrackState{program, track, std::any_cast<const Context&>(context)};
  } else if constexpr (std::constructible_from<TrackState, const SequenceProgram&, const TrackProgram&>) {
    return TrackState{program, track};
  } else {
    return TrackState{};
  }
}

template <class Command, class Context>
void describeCommand(const SourceCommand& record, const TrackProgram& track, CommandInfo& out,
                     const std::any& context) {
  // SourceCommand stores bytes and IDs. Rebuild the command type here so the
  // format's describe() method can use its normal parsed fields.
  if constexpr (!HasParseCommand<Command>) {
    return;
  } else {
    CommandReader reader{record.range, track.bytesFor(record)};
    const Command command = Command::parse(reader);
    if constexpr (HasDescribeWithContext<Command, Context>) {
      command.describe(out, std::any_cast<const Context&>(context));
    } else if constexpr (HasDescribe<Command>) {
      command.describe(out);
    }
  }
}

template <class Command, class Context>
void collectCommandReferences(const SourceCommand& record, const TrackProgram& track, CommandReferences& references,
                              const std::any& context) {
  if constexpr (HasSourceCommandReferences<Command>) {
    Command::references(record, track, references, context);
  } else if constexpr (!HasParseCommand<Command>) {
    return;
  } else {
    CommandReader reader{record.range, track.bytesFor(record)};
    const Command command = Command::parse(reader);
    if constexpr (HasReferencesWithContext<Command, Context>) {
      command.references(references, std::any_cast<const Context&>(context));
    } else if constexpr (HasReferences<Command>) {
      command.references(references);
    }
  }
}

template <class Command, class TrackState, class Context>
Effects executeCommand(const SourceCommand& record, const TrackProgram& track, std::any& trackState,
                       PerformanceEmitter& out, VmApi& vm, const std::any& context) {
  // SourceCommand stores bytes and IDs, while format code expects its own command
  // and track-state types. Do the casts here before calling execute().
  if constexpr (HasSourceCommandExecute<Command, TrackState, Context>) {
    return Command::execute(record, track, trackState, out, vm, context);
  } else {
    static_assert(HasParseCommand<Command>, "Sequence command must implement parse() or source-command execute()");
    CommandReader reader{record.range, track.bytesFor(record)};
    const Command command = Command::parse(reader);
    auto& typedTrackState = std::any_cast<TrackState&>(trackState);
    const auto& typedContext = std::any_cast<const Context&>(context);
    CommandRuntime<TrackState, Context> rt{
        .state = typedTrackState,
        .out = out,
        .vm = vm,
        .context = typedContext,
    };
    if constexpr (HasRuntimeEffectsExecute<Command, TrackState, Context>) {
      return command.execute(rt);
    } else if constexpr (HasRuntimeVoidExecute<Command, TrackState, Context>) {
      command.execute(rt);
      return Effects::none();
    } else if constexpr (HasLegacyExecute<Command, TrackState, Context>) {
      return command.execute(typedTrackState, out, vm, typedContext);
    } else if constexpr (HasPlaybackStatus<Command>) {
      static_assert(Command::playbackStatus == CommandPlaybackStatus::SourceOnly ||
                        Command::playbackStatus == CommandPlaybackStatus::NoOp,
                    "Commands without execute() must be marked source-only or no-op");
      return Effects::none();
    } else {
      static_assert(kAlwaysFalse<Command>, "Sequence command must implement execute() or be marked source-only/no-op");
      return Effects::none();
    }
  }
}

}  // namespace detail

// Used while registering a sequence driver. Each command kind gets its own name
// and metadata, while identical command types share the wrapper functions that
// parse, describe, and execute the command.
template <class TrackState, class Context>
class SequenceDialectBuilder {
public:
  SequenceDialectBuilder(std::string id, Context context) {
    dialect_.id = DialectId{.value = std::move(id)};
    dialect_.context = std::move(context);
    dialect_.createTrackState = detail::createTrackState<TrackState, Context>;
  }

  SequenceDialectBuilder& timebase(Timebase timebase) {
    dialect_.timebase = timebase;
    return *this;
  }

  SequenceDialectBuilder& defaultBehavior(SequenceProgramBehavior behavior) {
    dialect_.defaultBehavior = behavior;
    return *this;
  }

  template <class... Commands>
  SequenceDialect commands() {
    (addCommand<Commands>(Commands::kind, Commands::name), ...);
    return finish();
  }

  [[nodiscard]] SequenceDialect finish() { return std::move(dialect_); }

  struct RegisteredCommand {
    CommandHandlerId handler;
    CommandKindId kind;
  };

  template <class Command>
  RegisteredCommand addCommand(std::string_view kindName, std::string_view name) {
    return addCommand<Command>(kindName, name, detail::commandPlaybackStatus<Command>());
  }

  template <class Command>
  RegisteredCommand addCommand(std::string_view kindName, std::string_view name, CommandPlaybackStatus playbackStatus) {
    return addCommand(detail::commandTypeToken<Command>(), kindName, name, detail::describeCommand<Command, Context>,
                      detail::collectCommandReferences<Command, Context>,
                      detail::executeCommand<Command, TrackState, Context>, playbackStatus);
  }

  RegisteredCommand addPreservedCommand(std::string_view kindName, std::string_view name) {
    return addCommand(detail::preservedCommandTypeToken(), kindName, name, detail::describePreservedSourceCommand,
                      detail::collectPreservedSourceCommandReferences, detail::executePreservedSourceCommand,
                      CommandPlaybackStatus::SourceOnly);
  }

private:
  RegisteredCommand addCommand(CommandTypeToken typeToken, std::string_view kindName, std::string_view name,
                               DescribeSourceCommand describe, CollectSourceCommandReferences collectReferences,
                               ExecuteSourceCommand execute, CommandPlaybackStatus playbackStatus) {
    if (dialect_.kindForName(kindName) != nullptr) {
      throw std::logic_error("Sequence command kind was registered twice");
    }
    return RegisteredCommand{
        .handler = addHandler(typeToken, describe, collectReferences, execute),
        .kind = addKind(kindName, name, playbackStatus),
    };
  }

  CommandKindId addKind(std::string_view kindName, std::string_view name, CommandPlaybackStatus playbackStatus) {
    const auto index = static_cast<u32>(dialect_.kinds.size());
    dialect_.kinds.push_back(CommandKind{
        .id = CommandKindId{index},
        .kindName = std::string(kindName),
        .name = std::string(name),
        .detailKind = std::string(kindName),
        .playbackStatus = playbackStatus,
    });
    return CommandKindId{index};
  }

  CommandHandlerId addHandler(CommandTypeToken typeToken, DescribeSourceCommand describe,
                              CollectSourceCommandReferences collectReferences, ExecuteSourceCommand execute) {
    if (const auto* existing = dialect_.handlerForType(typeToken)) {
      return existing->id;
    }
    const auto index = static_cast<u32>(dialect_.handlers.size());
    dialect_.handlers.push_back(CommandHandler{
        .id = CommandHandlerId{index},
        .typeToken = typeToken,
        .describe = describe,
        .collectReferences = collectReferences,
        .execute = execute,
    });
    return CommandHandlerId{index};
  }

  SequenceDialect dialect_;
};

}  // namespace vgmtrans::core
