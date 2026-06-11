/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/core/SequenceDialect.h"

#include "value/core/ScanTypes.h"

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

void CommandInfo::field(std::string fieldName, u64 value) {
  field(std::move(fieldName), fmt::format("{}", value));
}

void CommandInfo::field(std::string fieldName, s64 value) {
  field(std::move(fieldName), fmt::format("{}", value));
}

void CommandInfo::field(std::string fieldName, Address value) {
  field(std::move(fieldName), fmt::format("${:04X}", value.value));
}

const CommandHandler* SequenceDialect::handler(CommandHandlerId handlerId) const {
  const auto found = std::ranges::find_if(handlers, [handlerId](const CommandHandler& handler) {
    return handler.id == handlerId;
  });
  if (found == handlers.end()) {
    return nullptr;
  }
  return &*found;
}

const CommandHandler* SequenceDialect::handlerForKind(std::string_view kindName) const {
  const auto found = std::ranges::find_if(handlers, [kindName](const CommandHandler& handler) {
    return handler.kindName == kindName;
  });
  if (found == handlers.end()) {
    return nullptr;
  }
  return &*found;
}

CommandInfo SequenceDialect::describe(const TrackProgram& track, const SourceCommand& command) const {
  const auto* commandHandler = handler(command.handler);
  if (commandHandler == nullptr || commandHandler->describe == nullptr) {
    return CommandInfo{};
  }

  CommandInfo info{
      .name = commandHandler->name,
      .detailKind = commandHandler->detailKind,
  };
  commandHandler->describe(command, track, info, context);
  return info;
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
  return items.add(parent, ItemKind::Command, info.detailKind, info.name, command.range,
                   commandInfoDescription(info));
}

void SequenceDialectRegistry::add(SequenceDialect dialect) {
  if (!dialect.id.valid()) {
    throw std::invalid_argument("Cannot register a SequenceDialect with an empty id");
  }

  const auto id = dialect.id.value;
  if (!dialects_.emplace(id, std::move(dialect)).second) {
    throw std::invalid_argument(fmt::format("Duplicate SequenceDialect registered: {}", id));
  }
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
