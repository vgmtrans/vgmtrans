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

// SequenceModel is the parsed, format-preserving layer. Commands keep raw driver values
// and source ranges so later lowerings can choose MIDI, tracker, or another target without
// forcing every format parser to pre-bake MIDI controller semantics.

struct InstrumentRef {
  std::optional<AssetId> asset;
  u32 bank = 0;
  u32 program = 0;
  std::optional<SourceRange> range;
};

// Notes store source key/velocity/duration values, not final MIDI values. A
// MidiSequenceProfile applies per-driver octave state, duration rate, transpose,
// portamento, and slur behavior when building a MidiSequence.
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

// These are common note-state concepts seen across drivers. The raw byte is preserved
// because the meaning of each bit is still profile-specific.
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

// Control-flow commands keep source addresses rather than resolved indexes. The lowering
// pass resolves addresses against the decoded command list so diagnostics can still point
// back to the original bytes.
struct JumpCommand {
  Address destination;
  SourceRange range;
};

struct CallCommand {
  Address destination;
  Address returnAddress;
  SourceRange range;
};

struct ReturnCommand {
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

// Escape hatch for driver features that are too specific for the shared command set
// but should still pass through the generic sequence lowering pipeline.
struct DriverSpecificCommand {
  std::string name;
  std::vector<u8> bytes;
  SourceRange range;
};

using Command =
    std::variant<NoteCommand, RestCommand, NoteStateCommand, DurationCommand, ProgramCommand, VolumeCommand, PanCommand,
                 TempoCommand, TransposeCommand, GlobalTransposeCommand, TuningCommand, PortamentoCommand,
                 VibratoCommand, TremoloCommand, ModulationRateCommand, ReverbCommand, EnvelopeCommand,
                 MasterVolumeCommand, JumpCommand, CallCommand, ReturnCommand, RepeatCommand, RepeatBreakCommand,
                 LoopBoundaryCommand, EndCommand, UnknownCommand, DriverSpecificCommand>;

// These defaults let formats name only commands whose display differs from the shared model.
[[nodiscard]] std::string defaultCommandName(const Command& command);
[[nodiscard]] std::string defaultCommandDetailKind(const Command& command);
[[nodiscard]] std::string defaultCommandDescription(const Command& command);
[[nodiscard]] SourceRange commandRange(const Command& command);

struct CommandTrack {
  TrackId id;
  // Logical driver track/channel number. It may differ from vector index after filtering
  // empty tracks or reordering source track pointers.
  u32 sourceTrackNumber = 0;
  Address startAddress;
  std::vector<Command> commands;
};

struct SequenceBehavior {
  // Behavior flags describe playback conventions discovered by the scanner, not export
  // policy. Exporters consume them only after the shared lowering stage.
  bool monophonicTracks = false;
  bool linearAmplitudeScale = false;
  bool writeInitialReverb = false;
  u8 initialReverb = 0;
  bool writeInitialMonoMode = false;
  bool skipChannel10 = true;
  bool truncateSustainedNotesAtLoopBoundary = true;
  bool suppressEventsWhenPlaybackTicksZero = false;
  std::optional<u64> maxPlaybackTicks;
  s32 initialGlobalTranspose = 0;
  LoopPolicy defaultLoopPolicy = LoopPolicy::Default;
};

struct CommandSequence {
  Timebase timebase;
  std::vector<CommandTrack> tracks;
  // References are advisory: they help build complete collections even when a sequence
  // can name instruments that are stored in a separate asset.
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
