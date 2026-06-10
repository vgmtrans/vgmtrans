/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/core/MetadataModel.h"

#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace vgmtrans::core {

struct InstrumentRef {
  std::optional<AssetId> asset;
  u32 bank = 0;
  u32 program = 0;
  std::optional<SourceRange> range;
};

struct NoteCommand {
  u32 key = 0;
  u32 rawVelocity = 0;
  u32 rawDuration = 0;
  SourceRange range;
};

struct RestCommand {
  u32 rawDuration = 0;
  SourceRange range;
};

enum class NoteStateAction {
  ToggleTriplet,
  ToggleSlur,
  EnableDotted,
  ToggleOctaveUp,
  Attributes,
  Octave,
};

struct NoteStateCommand {
  NoteStateAction action = NoteStateAction::Attributes;
  u32 rawValue = 0;
  SourceRange range;
};

struct DurationCommand {
  u32 rawValue = 0;
  SourceRange range;
};

struct ProgramCommand {
  u32 rawProgram = 0;
  SourceRange range;
};

struct VolumeCommand {
  u32 rawValue = 0;
  SourceRange range;
};

struct PanCommand {
  u32 rawValue = 0;
  SourceRange range;
};

struct TempoCommand {
  u32 rawValue = 0;
  SourceRange range;
};

struct TransposeCommand {
  s32 rawSemitones = 0;
  SourceRange range;
};

struct GlobalTransposeCommand {
  s32 rawSemitones = 0;
  SourceRange range;
};

struct TuningCommand {
  s32 rawValue = 0;
  SourceRange range;
};

struct PortamentoCommand {
  u32 rawTime = 0;
  std::optional<u32> rawTargetKey;
  SourceRange range;
};

struct VibratoCommand {
  u32 rawDepth = 0;
  SourceRange range;
};

struct TremoloCommand {
  u32 rawDepth = 0;
  SourceRange range;
};

struct ModulationRateCommand {
  u32 rawRate = 0;
  SourceRange range;
};

struct ReverbCommand {
  u32 rawValue = 0;
  SourceRange range;
};

struct EnvelopeCommand {
  u32 rawAttack = 0;
  u32 rawDecay = 0;
  u32 rawSustain = 0;
  u32 rawRelease = 0;
  SourceRange range;
};

struct MasterVolumeCommand {
  u32 rawValue = 0;
  SourceRange range;
};

struct JumpCommand {
  Address destination;
  SourceRange range;
};

struct RepeatCommand {
  u8 slot = 0;
  u32 count = 0;
  Address destination;
  SourceRange range;
};

struct RepeatBreakCommand {
  u8 slot = 0;
  u8 rawAttributes = 0;
  Address destination;
  SourceRange range;
};

struct LoopBoundaryCommand {
  Address destination;
  Address trigger;
  SourceRange range;
};

struct EndCommand {
  SourceRange range;
};

struct UnknownCommand {
  u32 opcode = 0;
  std::vector<u8> bytes;
  SourceRange range;
};

struct DriverSpecificCommand {
  std::string name;
  std::vector<u8> bytes;
  SourceRange range;
};

using Command = std::variant<NoteCommand, RestCommand, NoteStateCommand, DurationCommand, ProgramCommand, VolumeCommand,
                             PanCommand, TempoCommand, TransposeCommand, GlobalTransposeCommand, TuningCommand,
                             PortamentoCommand, VibratoCommand, TremoloCommand, ModulationRateCommand, ReverbCommand,
                             EnvelopeCommand, MasterVolumeCommand, JumpCommand, RepeatCommand, RepeatBreakCommand,
                             LoopBoundaryCommand, EndCommand, UnknownCommand, DriverSpecificCommand>;

// These defaults let formats name only commands whose display differs from the shared model.
[[nodiscard]] std::string defaultCommandName(const Command& command);
[[nodiscard]] std::string defaultCommandDetailKind(const Command& command);
[[nodiscard]] std::string defaultCommandDescription(const Command& command);
[[nodiscard]] SourceRange commandRange(const Command& command);

struct CommandTrack {
  TrackId id;
  u32 sourceTrackNumber = 0;
  Address startAddress;
  std::vector<Command> commands;
};

struct SequenceBehavior {
  bool monophonicTracks = false;
  bool linearAmplitudeScale = false;
  bool writeInitialReverb = false;
  u8 initialReverb = 0;
  bool writeInitialMonoMode = false;
  s32 initialGlobalTranspose = 0;
  LoopPolicy defaultLoopPolicy = LoopPolicy::Default;
};

struct CommandSequence {
  Timebase timebase;
  std::vector<CommandTrack> tracks;
  std::vector<InstrumentRef> referencedInstruments;
  SequenceBehavior behavior;
  // Empty means use metadata.format; formats set this when one parser has multiple MIDI sequence dialects.
  std::string midiSequenceProfile;
};

struct SequenceAsset {
  AssetMetadata metadata;
  CommandSequence commandSequence;
};

}  // namespace vgmtrans::core
