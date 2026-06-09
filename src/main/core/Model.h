/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "base/Types.h"
#include "core/CoreTypes.h"
#include "core/Source.h"

#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace vgmtrans::core {

enum class ItemKind {
  Source,
  Header,
  Sequence,
  Track,
  Command,
  InstrumentBank,
  Instrument,
  Region,
  SampleCollection,
  Sample,
  Misc,
};

struct ItemNode {
  ItemId id;
  std::optional<ItemId> parent;
  ItemKind kind = ItemKind::Misc;
  std::string detailKind;
  std::string name;
  std::string description;
  SourceRange range;
  std::vector<ItemId> children;
};

struct ItemTree {
  std::optional<ItemId> root;
  std::vector<ItemNode> nodes;
};

struct AssetMetadata {
  AssetId id;
  std::string format;
  std::string name;
  SourceRange range;
  ItemTree items;
};

struct Address {
  u64 value = 0;
};

struct Timebase {
  u32 ppqn = 48;
};

enum class LoopPolicy {
  Default,
  PlayOnce,
  Preserve,
};

struct InstrumentRef {
  std::optional<AssetId> asset;
  u32 bank = 0;
  u32 program = 0;
  std::optional<SourceRange> range;
};

enum class LfoTarget {
  Pitch,
  Volume,
  Pan,
  Unknown,
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

struct LfoCommand {
  LfoTarget target = LfoTarget::Unknown;
  u32 rawType = 0;
  u32 rawAmount = 0;
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

using SequencerCommand = std::variant<
    NoteCommand,
    RestCommand,
    DurationCommand,
    ProgramCommand,
    VolumeCommand,
    PanCommand,
    TempoCommand,
    TransposeCommand,
    GlobalTransposeCommand,
    TuningCommand,
    PortamentoCommand,
    LfoCommand,
    ReverbCommand,
    EnvelopeCommand,
    MasterVolumeCommand,
    JumpCommand,
    RepeatCommand,
    RepeatBreakCommand,
    EndCommand,
    UnknownCommand,
    DriverSpecificCommand>;

struct TrackProgram {
  TrackId id;
  u32 sourceTrackNumber = 0;
  Address startAddress;
  std::vector<SequencerCommand> commands;
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

struct SequenceProgram {
  Timebase timebase;
  std::vector<TrackProgram> tracks;
  std::vector<InstrumentRef> referencedInstruments;
  SequenceBehavior behavior;
};

struct SequenceAsset {
  AssetMetadata metadata;
  SequenceProgram program;
};

struct KeyRange {
  u8 low = 0;
  u8 high = 127;
};

struct VelocityRange {
  u8 low = 0;
  u8 high = 127;
};

struct SampleRef {
  std::optional<AssetId> collection;
  u32 index = 0;
};

struct Tuning {
  s32 cents = 0;
};

struct Envelope {
  u32 attack = 0;
  u32 decay = 0;
  u32 sustain = 0;
  u32 release = 0;
};

enum class SynthDestination {
  Pitch,
  FilterCutoff,
  Volume,
  Pan,
  VibratoDepth,
  VibratoRate,
  TremoloDepth,
  TremoloRate,
  Unknown,
};

enum class SynthSource {
  NoteOnVelocity,
  KeyNumber,
  Lfo,
  Envelope,
  MidiController,
  Unknown,
};

struct SynthGenerator {
  SynthDestination destination = SynthDestination::Unknown;
  s32 amount = 0;
};

struct SynthModulator {
  std::optional<SynthSource> source;
  SynthDestination destination = SynthDestination::Unknown;
  s32 amount = 0;
};

struct Region {
  KeyRange keyRange;
  VelocityRange velocityRange;
  SampleRef sample;
  SourceRange range;
  Tuning tuning;
  Envelope envelope;
  double pan = 0.5;
  double attenuationDb = 0.0;
};

struct Instrument {
  u32 bank = 0;
  u32 program = 0;
  std::string name;
  SourceRange range;
  std::vector<Region> regions;
  std::vector<SynthGenerator> generators;
  std::vector<SynthModulator> modulators;
};

struct InstrumentBank {
  std::vector<Instrument> instruments;
};

struct InstrumentBankAsset {
  AssetMetadata metadata;
  InstrumentBank bank;
};

enum class AudioCodec {
  Unknown,
  PcmS16,
  SnesBrr,
  PsxAdpcm,
  OkiAdpcm,
};

struct Loop {
  bool enabled = false;
  u32 start = 0;
  u32 length = 0;
};

struct Sample {
  std::string name;
  AudioCodec codec = AudioCodec::Unknown;
  SourceRange encodedData;
  u32 sampleRate = 0;
  u8 channels = 1;
  u16 bitsPerSample = 16;
  Loop loop;
  Tuning pitch;
  double attenuationDb = 0.0;
};

struct SampleCollection {
  std::vector<Sample> samples;
};

struct SampleCollectionAsset {
  AssetMetadata metadata;
  SampleCollection samples;
};

struct MiscAsset {
  AssetMetadata metadata;
  std::vector<u8> payload;
};

using Asset = std::variant<
    SequenceAsset,
    InstrumentBankAsset,
    SampleCollectionAsset,
    MiscAsset>;

struct Collection {
  CollectionId id;
  std::string name;
  std::optional<AssetId> sequence;
  std::vector<AssetId> instrumentBanks;
  std::vector<AssetId> sampleCollections;
  std::vector<AssetId> miscAssets;
};

struct Project {
  std::vector<SourceFile> sources;
  std::vector<Asset> assets;
  std::vector<Collection> collections;
  std::vector<Diagnostic> diagnostics;
};

struct DecodedSample {
  u32 sampleRate = 0;
  u8 channels = 1;
  std::vector<s16> pcm;
  Loop loop;
};

struct NoteOn {
  u64 tick = 0;
  u8 channel = 0;
  u8 key = 0;
  u8 velocity = 0;
};

struct NoteOff {
  u64 tick = 0;
  u8 channel = 0;
  u8 key = 0;
  u8 velocity = 0;
};

struct NoteDuration {
  u64 tick = 0;
  u8 channel = 0;
  u8 key = 0;
  u8 velocity = 0;
  u32 duration = 0;
};

struct Tempo {
  u64 tick = 0;
  u32 microsecondsPerQuarter = 500000;
};

struct ProgramChange {
  u64 tick = 0;
  u8 channel = 0;
  u8 program = 0;
};

struct BankSelect {
  u64 tick = 0;
  u8 channel = 0;
  u16 bank = 0;
  bool writeLsb = true;
};

struct Volume {
  u64 tick = 0;
  u8 channel = 0;
  u8 value = 0;
};

struct Volume14 {
  u64 tick = 0;
  u8 channel = 0;
  u16 value = 0;
};

struct Pan {
  u64 tick = 0;
  u8 channel = 0;
  u8 value = 64;
};

struct Expression {
  u64 tick = 0;
  u8 channel = 0;
  u8 value = 127;
};

struct MasterVolume {
  u64 tick = 0;
  u16 value = 0;
};

struct Reverb {
  u64 tick = 0;
  u8 channel = 0;
  u8 value = 0;
};

struct FineTune {
  u64 tick = 0;
  u8 channel = 0;
  double cents = 0.0;
};

struct CoarseTune {
  u64 tick = 0;
  u8 channel = 0;
  s8 semitones = 0;
};

struct PitchBend {
  u64 tick = 0;
  u8 channel = 0;
  s16 value = 0;
};

struct PitchBendRange {
  u64 tick = 0;
  u8 channel = 0;
  u8 semitones = 2;
};

struct VibratoDepth {
  u64 tick = 0;
  u8 channel = 0;
  u8 value = 0;
};

struct VibratoFrequency {
  u64 tick = 0;
  u8 channel = 0;
  u8 value = 0;
};

struct VibratoDelay {
  u64 tick = 0;
  u8 channel = 0;
  u32 ticks = 0;
};

struct TremoloDepth {
  u64 tick = 0;
  u8 channel = 0;
  u8 value = 0;
};

struct TremoloFrequency {
  u64 tick = 0;
  u8 channel = 0;
  u8 value = 0;
};

struct TremoloDelay {
  u64 tick = 0;
  u8 channel = 0;
  u32 ticks = 0;
};

struct PortamentoEnable {
  u64 tick = 0;
  u8 channel = 0;
  bool enabled = false;
};

struct PortamentoTime {
  u64 tick = 0;
  u8 channel = 0;
  u8 value = 0;
};

struct PortamentoTime14 {
  u64 tick = 0;
  u8 channel = 0;
  u16 value = 0;
};

struct PortamentoControl {
  u64 tick = 0;
  u8 channel = 0;
  u8 key = 0;
};

struct LegatoPedal {
  u64 tick = 0;
  u8 channel = 0;
  bool enabled = false;
};

struct MonoMode {
  u64 tick = 0;
  u8 channel = 0;
  u8 channels = 1;
};

struct EndOfTrack {
  u64 tick = 0;
};

struct Marker {
  u64 tick = 0;
  std::string text;
};

using PerformanceEvent = std::variant<
    NoteOn,
    NoteOff,
    NoteDuration,
    Tempo,
    ProgramChange,
    BankSelect,
    Volume,
    Volume14,
    Pan,
    Expression,
    MasterVolume,
    Reverb,
    FineTune,
    CoarseTune,
    PitchBend,
    PitchBendRange,
    VibratoDepth,
    VibratoFrequency,
    VibratoDelay,
    TremoloDepth,
    TremoloFrequency,
    TremoloDelay,
    PortamentoEnable,
    PortamentoTime,
    PortamentoTime14,
    PortamentoControl,
    LegatoPedal,
    MonoMode,
    EndOfTrack,
    Marker>;

struct PerformanceTrack {
  std::string name;
  std::vector<PerformanceEvent> events;
};

struct PerformanceSequence {
  Timebase timebase;
  std::vector<PerformanceTrack> tracks;
  std::vector<Diagnostic> diagnostics;
};

[[nodiscard]] AssetMetadata& metadata(Asset& asset);
[[nodiscard]] const AssetMetadata& metadata(const Asset& asset);

}  // namespace vgmtrans::core
