/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/core/SequenceProgram.h"

#include <any>
#include <concepts>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace vgmtrans::core {

class Emit;
class ItemTreeBuilder;
class VmApi;

struct Step {
  enum class Kind {
    Next,
    End,
    Jump,
    Call,
    Return,
  };

  Kind kind = Kind::Next;
  Address destination;

  [[nodiscard]] static constexpr Step next() noexcept { return Step{.kind = Kind::Next}; }
  [[nodiscard]] static constexpr Step end() noexcept { return Step{.kind = Kind::End}; }
  [[nodiscard]] static constexpr Step jump(Address destination) noexcept {
    return Step{.kind = Kind::Jump, .destination = destination};
  }
  [[nodiscard]] static constexpr Step call(Address destination) noexcept {
    return Step{.kind = Kind::Call, .destination = destination};
  }
  [[nodiscard]] static constexpr Step return_() noexcept { return Step{.kind = Kind::Return}; }
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

struct CommandInfo {
  std::string name;
  std::string detailKind;
  std::vector<CommandInfoField> fields;

  void field(std::string fieldName, std::string value);
  void field(std::string fieldName, u64 value);
  void field(std::string fieldName, s64 value);
  void field(std::string fieldName, Address value);
};

using DescribeSourceCommand =
    void (*)(const SourceCommand&, const TrackProgram&, CommandInfo&, const std::any& context);
using ExecuteSourceCommand = Effects (*)(const SourceCommand&, const TrackProgram&, std::any& trackState, Emit& out,
                                         VmApi& vm, const std::any& context);
using CreateTrackState = std::any (*)(const SequenceProgram&, const TrackProgram&, const std::any& context);

struct CommandHandler {
  CommandHandlerId id;
  CommandKindId kind;
  std::string kindName;
  std::string name;
  std::string detailKind;
  DescribeSourceCommand describe = nullptr;
  ExecuteSourceCommand execute = nullptr;
};

struct SequenceDialect {
  DialectId id;
  Timebase timebase;
  SequenceProgramBehavior defaultBehavior;
  CreateTrackState createTrackState = nullptr;
  std::vector<CommandHandler> handlers;
  std::any context;

  [[nodiscard]] const CommandHandler* handler(CommandHandlerId id) const;
  [[nodiscard]] const CommandHandler* handlerForKind(std::string_view kindName) const;
  [[nodiscard]] CommandInfo describe(const TrackProgram& track, const SourceCommand& command) const;
};

[[nodiscard]] std::string commandInfoDescription(const CommandInfo& info);
[[nodiscard]] ItemId addSourceCommandItem(ItemTreeBuilder& items, std::optional<ItemId> parent,
                                          const SequenceDialect& dialect, const TrackProgram& track,
                                          const SourceCommand& command);

class SequenceDialectRegistry {
public:
  void add(SequenceDialect dialect);
  [[nodiscard]] const SequenceDialect* find(std::string_view id) const;
  [[nodiscard]] bool contains(std::string_view id) const;

private:
  std::unordered_map<std::string, SequenceDialect> dialects_;
};

namespace detail {

template <class Command>
concept HasDescribe = requires(const Command& command, CommandInfo& out) { command.describe(out); };

template <class Command, class Context>
concept HasDescribeWithContext =
    requires(const Command& command, CommandInfo& out, const Context& context) { command.describe(out, context); };

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
void describeCommand(const SourceCommand& record, const TrackProgram& track, CommandInfo& out, const std::any& context) {
  CommandReader reader{record.range, track.bytesFor(record)};
  const Command command = Command::parse(reader);
  out.name = std::string(Command::name);
  out.detailKind = std::string(Command::kind);
  if constexpr (HasDescribeWithContext<Command, Context>) {
    command.describe(out, std::any_cast<const Context&>(context));
  } else if constexpr (HasDescribe<Command>) {
    command.describe(out);
  }
}

template <class Command, class TrackState, class Context>
Effects executeCommand(const SourceCommand& record, const TrackProgram& track, std::any& trackState, Emit& out,
                       VmApi& vm, const std::any& context) {
  CommandReader reader{record.range, track.bytesFor(record)};
  const Command command = Command::parse(reader);
  return command.execute(std::any_cast<TrackState&>(trackState), out, vm, std::any_cast<const Context&>(context));
}

}  // namespace detail

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
    (addCommand<Commands>(), ...);
    return std::move(dialect_);
  }

private:
  template <class Command>
  void addCommand() {
    const auto index = static_cast<u32>(dialect_.handlers.size());
    dialect_.handlers.push_back(CommandHandler{
        .id = CommandHandlerId{index},
        .kind = CommandKindId{index},
        .kindName = std::string(Command::kind),
        .name = std::string(Command::name),
        .detailKind = std::string(Command::kind),
        .describe = detail::describeCommand<Command, Context>,
        .execute = detail::executeCommand<Command, TrackState, Context>,
    });
  }

  SequenceDialect dialect_;
};

}  // namespace vgmtrans::core
