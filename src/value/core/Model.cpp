/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/core/Model.h"

#include <fmt/format.h>

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

[[nodiscard]] std::string_view defaultCommandName(const VibratoCommand&) {
  return "Vibrato";
}

[[nodiscard]] std::string_view defaultCommandName(const TremoloCommand&) {
  return "Tremolo";
}

[[nodiscard]] std::string_view defaultCommandName(const ModulationRateCommand&) {
  return "Modulation Rate";
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

[[nodiscard]] std::string_view defaultCommandDetailKind(const NoteCommand&) {
  return "note";
}

[[nodiscard]] std::string_view defaultCommandDetailKind(const RestCommand&) {
  return "rest";
}

[[nodiscard]] std::string_view defaultCommandDetailKind(const NoteStateCommand& command) {
  switch (command.action) {
    case NoteStateAction::ToggleTriplet:
      return "toggle-triplet";
    case NoteStateAction::ToggleSlur:
      return "toggle-slur";
    case NoteStateAction::EnableDotted:
      return "enable-dotted";
    case NoteStateAction::ToggleOctaveUp:
      return "toggle-octave-up";
    case NoteStateAction::Attributes:
      return "note-attributes";
    case NoteStateAction::Octave:
      return "octave";
  }
  return "note-state";
}

[[nodiscard]] std::string_view defaultCommandDetailKind(const DurationCommand&) {
  return "duration";
}

[[nodiscard]] std::string_view defaultCommandDetailKind(const ProgramCommand&) {
  return "program";
}

[[nodiscard]] std::string_view defaultCommandDetailKind(const VolumeCommand&) {
  return "volume";
}

[[nodiscard]] std::string_view defaultCommandDetailKind(const PanCommand&) {
  return "pan";
}

[[nodiscard]] std::string_view defaultCommandDetailKind(const TempoCommand&) {
  return "tempo";
}

[[nodiscard]] std::string_view defaultCommandDetailKind(const TransposeCommand&) {
  return "transpose";
}

[[nodiscard]] std::string_view defaultCommandDetailKind(const GlobalTransposeCommand&) {
  return "global-transpose";
}

[[nodiscard]] std::string_view defaultCommandDetailKind(const TuningCommand&) {
  return "tuning";
}

[[nodiscard]] std::string_view defaultCommandDetailKind(const PortamentoCommand&) {
  return "portamento";
}

[[nodiscard]] std::string_view defaultCommandDetailKind(const VibratoCommand&) {
  return "vibrato";
}

[[nodiscard]] std::string_view defaultCommandDetailKind(const TremoloCommand&) {
  return "tremolo";
}

[[nodiscard]] std::string_view defaultCommandDetailKind(const ModulationRateCommand&) {
  return "modulation-rate";
}

[[nodiscard]] std::string_view defaultCommandDetailKind(const ReverbCommand&) {
  return "reverb";
}

[[nodiscard]] std::string_view defaultCommandDetailKind(const EnvelopeCommand&) {
  return "envelope";
}

[[nodiscard]] std::string_view defaultCommandDetailKind(const MasterVolumeCommand&) {
  return "master-volume";
}

[[nodiscard]] std::string_view defaultCommandDetailKind(const JumpCommand&) {
  return "jump";
}

[[nodiscard]] std::string_view defaultCommandDetailKind(const RepeatCommand&) {
  return "repeat";
}

[[nodiscard]] std::string_view defaultCommandDetailKind(const RepeatBreakCommand&) {
  return "repeat-break";
}

[[nodiscard]] std::string_view defaultCommandDetailKind(const LoopBoundaryCommand&) {
  return "loop-boundary";
}

[[nodiscard]] std::string_view defaultCommandDetailKind(const EndCommand&) {
  return "end";
}

[[nodiscard]] std::string_view defaultCommandDetailKind(const UnknownCommand&) {
  return "unknown";
}

[[nodiscard]] std::string_view defaultCommandDetailKind(const DriverSpecificCommand&) {
  return "driver-specific";
}

[[nodiscard]] std::string defaultCommandDescription(const NoteCommand& command) {
  return fmt::format("Key {}, length index {}", command.key, command.rawDuration);
}

[[nodiscard]] std::string defaultCommandDescription(const RestCommand& command) {
  return fmt::format("Length index {}", command.rawDuration);
}

[[nodiscard]] std::string defaultCommandDescription(const NoteStateCommand& command) {
  switch (command.action) {
    case NoteStateAction::ToggleTriplet:
      return "Toggle triplet";
    case NoteStateAction::ToggleSlur:
      return "Toggle slur";
    case NoteStateAction::EnableDotted:
      return "Enable dotted note";
    case NoteStateAction::ToggleOctaveUp:
      return "Toggle 2-octave up";
    case NoteStateAction::Attributes:
      return fmt::format("Raw {}", command.rawValue);
    case NoteStateAction::Octave:
      return fmt::format("Octave {}", command.rawValue);
  }
  return fmt::format("Raw {}", command.rawValue);
}

[[nodiscard]] std::string defaultCommandDescription(const DurationCommand& command) {
  return fmt::format("Raw {}", command.rawValue);
}

[[nodiscard]] std::string defaultCommandDescription(const ProgramCommand& command) {
  return fmt::format("Program {}", command.rawProgram);
}

[[nodiscard]] std::string defaultCommandDescription(const VolumeCommand& command) {
  return fmt::format("Raw {}", command.rawValue);
}

[[nodiscard]] std::string defaultCommandDescription(const PanCommand& command) {
  return fmt::format("Raw {}", command.rawValue);
}

[[nodiscard]] std::string defaultCommandDescription(const TempoCommand& command) {
  return fmt::format("Raw {}", command.rawValue);
}

[[nodiscard]] std::string defaultCommandDescription(const TransposeCommand& command) {
  return fmt::format("Semitones {}", command.rawSemitones);
}

[[nodiscard]] std::string defaultCommandDescription(const GlobalTransposeCommand& command) {
  return fmt::format("Semitones {}", command.rawSemitones);
}

[[nodiscard]] std::string defaultCommandDescription(const TuningCommand& command) {
  return fmt::format("Raw {}", command.rawValue);
}

[[nodiscard]] std::string defaultCommandDescription(const PortamentoCommand& command) {
  return fmt::format("Time {}", command.rawTime);
}

[[nodiscard]] std::string defaultCommandDescription(const VibratoCommand& command) {
  return fmt::format("Depth {}", command.rawDepth);
}

[[nodiscard]] std::string defaultCommandDescription(const TremoloCommand& command) {
  return fmt::format("Depth {}", command.rawDepth);
}

[[nodiscard]] std::string defaultCommandDescription(const ModulationRateCommand& command) {
  return fmt::format("Rate {}", command.rawRate);
}

[[nodiscard]] std::string defaultCommandDescription(const ReverbCommand& command) {
  return fmt::format("Raw {}", command.rawValue);
}

[[nodiscard]] std::string defaultCommandDescription(const EnvelopeCommand& command) {
  return fmt::format("Release {}", command.rawRelease);
}

[[nodiscard]] std::string defaultCommandDescription(const MasterVolumeCommand& command) {
  return fmt::format("Raw {}", command.rawValue);
}

[[nodiscard]] std::string defaultCommandDescription(const JumpCommand& command) {
  return fmt::format("Destination ${}", command.destination.value);
}

[[nodiscard]] std::string defaultCommandDescription(const RepeatCommand& command) {
  return fmt::format("Slot {}, count {}, destination ${}",
                     static_cast<u32>(command.slot),
                     command.count,
                     command.destination.value);
}

[[nodiscard]] std::string defaultCommandDescription(const RepeatBreakCommand& command) {
  return fmt::format("Slot {}, attributes {}, destination ${}",
                     static_cast<u32>(command.slot),
                     static_cast<u32>(command.rawAttributes),
                     command.destination.value);
}

[[nodiscard]] std::string defaultCommandDescription(const LoopBoundaryCommand& command) {
  return fmt::format("Destination ${}, trigger ${}", command.destination.value, command.trigger.value);
}

[[nodiscard]] std::string defaultCommandDescription(const EndCommand&) {
  return {};
}

[[nodiscard]] std::string defaultCommandDescription(const UnknownCommand& command) {
  return fmt::format("Opcode {}", command.opcode);
}

[[nodiscard]] std::string defaultCommandDescription(const DriverSpecificCommand& command) {
  return fmt::format("Bytes {}", command.bytes.size());
}

}  // namespace

AssetMetadata& metadata(Asset& asset) {
  return std::visit([](auto& typedAsset) -> AssetMetadata& { return typedAsset.metadata; }, asset);
}

const AssetMetadata& metadata(const Asset& asset) {
  return std::visit([](const auto& typedAsset) -> const AssetMetadata& { return typedAsset.metadata; }, asset);
}

SourceRange commandRange(const Command& command) {
  return std::visit([](const auto& typedCommand) { return typedCommand.range; }, command);
}

std::string defaultCommandName(const Command& command) {
  return std::visit([](const auto& typedCommand) { return std::string(defaultCommandName(typedCommand)); }, command);
}

std::string defaultCommandDetailKind(const Command& command) {
  return std::visit([](const auto& typedCommand) {
    return std::string(defaultCommandDetailKind(typedCommand));
  }, command);
}

std::string defaultCommandDescription(const Command& command) {
  return std::visit([](const auto& typedCommand) { return defaultCommandDescription(typedCommand); }, command);
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
