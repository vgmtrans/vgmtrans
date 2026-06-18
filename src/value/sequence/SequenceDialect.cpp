/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/sequence/SequenceDialect.h"

#include "value/scan/ScanTypes.h"

#include <algorithm>
#include <fmt/format.h>
#include <stdexcept>
#include <utility>

namespace vgmtrans::core {

void CommandInfo::field(std::string fieldName, std::string value) {
  fields.push_back(CommandInfoField{
      .name = std::move(fieldName),
      .value = std::move(value),
  });
}

void CommandInfo::field(std::string fieldName, double value) {
  field(std::move(fieldName), fmt::format("{}", value));
}

void CommandInfo::field(std::string fieldName, u8 value) {
  field(std::move(fieldName), static_cast<u64>(value));
}

void CommandInfo::field(std::string fieldName, s8 value) {
  field(std::move(fieldName), static_cast<s64>(value));
}

void CommandInfo::field(std::string fieldName, u16 value) {
  field(std::move(fieldName), static_cast<u64>(value));
}

void CommandInfo::field(std::string fieldName, s16 value) {
  field(std::move(fieldName), static_cast<s64>(value));
}

void CommandInfo::field(std::string fieldName, u32 value) {
  field(std::move(fieldName), static_cast<u64>(value));
}

void CommandInfo::field(std::string fieldName, s32 value) {
  field(std::move(fieldName), static_cast<s64>(value));
}

void CommandInfo::field(std::string fieldName, u64 value) {
  field(std::move(fieldName), fmt::format("{}", value));
}

void CommandInfo::field(std::string fieldName, s64 value) {
  field(std::move(fieldName), fmt::format("{}", value));
}

void CommandInfo::field(std::string fieldName, Address value) {
  field(std::move(fieldName), fmt::format("${:04X}", value.value));
}

void CommandReferences::instrument(u32 bank, u32 program, std::optional<SourceRange> range) {
  instruments_.push_back(CommandInstrumentReference{
      .bank = bank,
      .program = program,
      .range = std::move(range),
  });
}

std::vector<CommandInstrumentReference> CommandReferences::takeInstruments() {
  return std::move(instruments_);
}

const CommandHandler* SequenceDialect::handler(CommandHandlerId handlerId) const {
  if (!handlerId.valid() || handlerId.value >= handlers.size()) {
    return nullptr;
  }

  const auto& commandHandler = handlers[handlerId.value];
  return commandHandler.id == handlerId ? &commandHandler : nullptr;
}

const CommandHandler* SequenceDialect::handlerForType(CommandTypeToken typeToken) const {
  if (typeToken == nullptr) {
    return nullptr;
  }
  const auto found = std::ranges::find_if(handlers, [typeToken](const CommandHandler& handler) {
    return handler.typeToken == typeToken;
  });
  if (found == handlers.end()) {
    return nullptr;
  }
  return &*found;
}

const CommandKind* SequenceDialect::kind(CommandKindId kindId) const {
  if (!kindId.valid() || kindId.value >= kinds.size()) {
    return nullptr;
  }

  const auto& commandKind = kinds[kindId.value];
  return commandKind.id == kindId ? &commandKind : nullptr;
}

const CommandKind* SequenceDialect::kindForName(std::string_view kindName) const {
  const auto found = std::ranges::find_if(
      kinds, [kindName](const CommandKind& commandKind) { return commandKind.kindName == kindName; });
  if (found == kinds.end()) {
    return nullptr;
  }
  return &*found;
}

CommandInfo SequenceDialect::describe(const TrackProgram& track, const SourceCommand& command) const {
  const auto* commandHandler = handler(command.handler);
  const auto* commandKind = kind(command.kind);
  if (commandHandler == nullptr || commandKind == nullptr || commandHandler->describe == nullptr) {
    return CommandInfo{};
  }

  CommandInfo info{
      .name = commandKind->name,
      .detailKind = commandKind->detailKind,
      .playbackStatus = commandKind->playbackStatus,
  };
  // Operands read from the bytes are already listed. The format hook should add
  // higher-level details instead of printing the same operands again.
  for (const CommandOperand& operand : track.operandsFor(command)) {
    std::visit([&](const auto& value) { info.field(operand.name, value); }, operand.value);
  }
  commandHandler->describe(command, track, info, context);
  return info;
}

std::vector<CommandInstrumentReference> SequenceDialect::instrumentReferences(const TrackProgram& track,
                                                                              const SourceCommand& command) const {
  const auto* commandHandler = handler(command.handler);
  if (commandHandler == nullptr || commandHandler->collectReferences == nullptr) {
    return {};
  }

  CommandReferences references;
  commandHandler->collectReferences(command, track, references, context);
  auto instruments = references.takeInstruments();
  for (auto& instrument : instruments) {
    if (!instrument.range && command.range.valid()) {
      instrument.range = command.range;
    }
  }
  return instruments;
}

std::string commandInfoDescription(const CommandInfo& info) {
  std::string description;
  for (const auto& field : info.fields) {
    if (!description.empty()) {
      description += ", ";
    }
    description += field.name + " " + field.value;
  }
  return description;
}

ItemId addSourceCommandItem(ItemTreeBuilder& items, std::optional<ItemId> parent, const SequenceDialect& dialect,
                            const TrackProgram& track, const SourceCommand& command) {
  const CommandInfo info = dialect.describe(track, command);
  return items.add(parent, ItemKind::Command, info.detailKind, info.name, command.range, commandInfoDescription(info));
}

void addSourceCommandItems(ItemTreeBuilder& items, std::optional<ItemId> parent, const SequenceDialect& dialect,
                           const TrackProgram& track) {
  for (const auto& command : track.commands) {
    static_cast<void>(addSourceCommandItem(items, parent, dialect, track, command));
  }
}

void addCommandInstrumentReferences(SequenceProgram& program, const SequenceDialect& dialect, const TrackProgram& track,
                                    const SourceCommand& command, std::optional<AssetId> instrumentSetId) {
  for (const auto& ref : dialect.instrumentReferences(track, command)) {
    addUniqueReferencedInstrument(program, instrumentSetId, ref.bank, ref.program, ref.range);
  }
}

void addSourceCommandItemsAndInstrumentReferences(ItemTreeBuilder& items, std::optional<ItemId> parent,
                                                  SequenceProgram& program, const SequenceDialect& dialect,
                                                  const TrackProgram& track,
                                                  std::optional<AssetId> instrumentSetId) {
  for (const auto& command : track.commands) {
    static_cast<void>(addSourceCommandItem(items, parent, dialect, track, command));
    addCommandInstrumentReferences(program, dialect, track, command, instrumentSetId);
  }
}

void SequenceDialectRegistry::add(SequenceDialect dialect) {
  if (sealed_) {
    throw std::logic_error("Cannot register sequence dialects after session mutation has started");
  }
  if (!dialect.id.valid()) {
    throw std::invalid_argument("Cannot register a SequenceDialect with an empty id");
  }

  const auto id = dialect.id.value;
  if (!dialects_.emplace(id, std::move(dialect)).second) {
    throw std::invalid_argument(fmt::format("Duplicate SequenceDialect registered: {}", id));
  }
}

void SequenceDialectRegistry::seal() noexcept {
  sealed_ = true;
}

const SequenceDialect* SequenceDialectRegistry::find(std::string_view id) const {
  const auto found = dialects_.find(std::string(id));
  if (found == dialects_.end()) {
    return nullptr;
  }
  return &found->second;
}

bool SequenceDialectRegistry::contains(std::string_view id) const {
  return find(id) != nullptr;
}

}  // namespace vgmtrans::core
