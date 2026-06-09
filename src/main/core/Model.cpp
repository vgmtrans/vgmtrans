/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "core/Model.h"

#include <string_view>

namespace vgmtrans::core {

namespace {

[[nodiscard]] std::string_view defaultCommandName(const NoteCommand&) {
  return "Note";
}

[[nodiscard]] std::string_view defaultCommandName(const RestCommand&) {
  return "Rest";
}

[[nodiscard]] std::string_view defaultCommandName(const NoteStateCommand& command) {
  switch (command.action) {
    case NoteStateAction::ToggleTriplet:
      return "Toggle Triplet";
    case NoteStateAction::ToggleSlur:
      return "Toggle Slur";
    case NoteStateAction::EnableDotted:
      return "Dotted Note";
    case NoteStateAction::ToggleOctaveUp:
      return "Toggle 2-Octave Up";
    case NoteStateAction::Attributes:
      return "Note Attributes";
    case NoteStateAction::Octave:
      return "Octave";
  }
  return "Note State";
}

[[nodiscard]] std::string_view defaultCommandName(const DurationCommand&) {
  return "Duration";
}

[[nodiscard]] std::string_view defaultCommandName(const ProgramCommand&) {
  return "Program";
}

[[nodiscard]] std::string_view defaultCommandName(const VolumeCommand&) {
  return "Volume";
}

[[nodiscard]] std::string_view defaultCommandName(const PanCommand&) {
  return "Pan";
}

[[nodiscard]] std::string_view defaultCommandName(const TempoCommand&) {
  return "Tempo";
}

[[nodiscard]] std::string_view defaultCommandName(const TransposeCommand&) {
  return "Transpose";
}

[[nodiscard]] std::string_view defaultCommandName(const GlobalTransposeCommand&) {
  return "Global Transpose";
}

[[nodiscard]] std::string_view defaultCommandName(const TuningCommand&) {
  return "Tuning";
}

[[nodiscard]] std::string_view defaultCommandName(const PortamentoCommand&) {
  return "Portamento";
}

[[nodiscard]] std::string_view defaultCommandName(const LfoCommand&) {
  return "LFO";
}

[[nodiscard]] std::string_view defaultCommandName(const ReverbCommand&) {
  return "Reverb";
}

[[nodiscard]] std::string_view defaultCommandName(const EnvelopeCommand&) {
  return "Envelope";
}

[[nodiscard]] std::string_view defaultCommandName(const MasterVolumeCommand&) {
  return "Master Volume";
}

[[nodiscard]] std::string_view defaultCommandName(const JumpCommand&) {
  return "Jump";
}

[[nodiscard]] std::string_view defaultCommandName(const RepeatCommand&) {
  return "Repeat";
}

[[nodiscard]] std::string_view defaultCommandName(const RepeatBreakCommand&) {
  return "Repeat Break";
}

[[nodiscard]] std::string_view defaultCommandName(const LoopBoundaryCommand&) {
  return "Loop Boundary";
}

[[nodiscard]] std::string_view defaultCommandName(const EndCommand&) {
  return "End";
}

[[nodiscard]] std::string_view defaultCommandName(const UnknownCommand&) {
  return "Unknown";
}

[[nodiscard]] std::string_view defaultCommandName(const DriverSpecificCommand& command) {
  return command.name;
}

}  // namespace

AssetMetadata& metadata(Asset& asset) {
  return std::visit([](auto& typedAsset) -> AssetMetadata& { return typedAsset.metadata; }, asset);
}

const AssetMetadata& metadata(const Asset& asset) {
  return std::visit([](const auto& typedAsset) -> const AssetMetadata& { return typedAsset.metadata; }, asset);
}

SourceRange commandRange(const SequencerCommand& command) {
  return std::visit([](const auto& typedCommand) { return typedCommand.range; }, command);
}

std::string defaultCommandName(const SequencerCommand& command) {
  return std::visit([](const auto& typedCommand) { return std::string(defaultCommandName(typedCommand)); }, command);
}

ItemNode* itemById(ItemTree& tree, ItemId id) {
  const auto found = std::ranges::find_if(tree.nodes, [id](const ItemNode& item) {
    return item.id == id;
  });
  if (found == tree.nodes.end()) {
    return nullptr;
  }
  return &*found;
}

const ItemNode* itemById(const ItemTree& tree, ItemId id) {
  const auto found = std::ranges::find_if(tree.nodes, [id](const ItemNode& item) {
    return item.id == id;
  });
  if (found == tree.nodes.end()) {
    return nullptr;
  }
  return &*found;
}

Asset* assetById(Project& project, AssetId id) {
  const auto found = std::ranges::find_if(project.assets, [id](const Asset& asset) {
    return metadata(asset).id == id;
  });
  if (found == project.assets.end()) {
    return nullptr;
  }
  return &*found;
}

const Asset* assetById(const Project& project, AssetId id) {
  const auto found = std::ranges::find_if(project.assets, [id](const Asset& asset) {
    return metadata(asset).id == id;
  });
  if (found == project.assets.end()) {
    return nullptr;
  }
  return &*found;
}

const Collection* collectionById(const Project& project, CollectionId id) {
  const auto found = std::ranges::find_if(project.collections, [id](const Collection& collection) {
    return collection.id == id;
  });
  if (found == project.collections.end()) {
    return nullptr;
  }
  return &*found;
}

}  // namespace vgmtrans::core
