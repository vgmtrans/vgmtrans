/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/core/MetadataModel.h"

#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace vgmtrans::core {

// SynthModel represents sample-backed instruments before choosing an output container
// such as SF2, DLS, or eventually a tracker instrument format. Values are normalized
// enough to share exporters, but source ranges and codec metadata remain available.

constexpr double kDefaultInstrumentReverbSend = 0.25;

struct KeyRange {
  u8 low = 0;
  u8 high = 127;
};

struct VelocityRange {
  u8 low = 0;
  u8 high = 127;
};

struct SampleRef {
  // Region sample indexes are local to the referenced SampleCollectionAsset. Keeping the
  // collection optional lets incomplete scans report diagnostics instead of crashing.
  std::optional<AssetId> collection;
  u32 index = 0;
};

struct Tuning {
  s32 cents = 0;
};

inline constexpr u32 kEnvelopeInfinite = std::numeric_limits<u32>::max();

struct Envelope {
  // The all-zero default means no explicit envelope was parsed.
  // Time fields are microseconds. kEnvelopeInfinite means the stage has no finite duration.
  u32 attack = 0;
  u32 hold = 0;
  u32 decay = 0;
  // Linear amplitude level, where 1000 is full scale.
  u32 sustain = 0;
  u32 release = 0;
  std::optional<double> attackSeconds;
  std::optional<double> holdSeconds;
  std::optional<double> decaySeconds;
  std::optional<double> releaseSeconds;
  std::optional<double> sustainAmplitude;
};

[[nodiscard]] inline bool hasExplicitEnvelope(const Envelope& envelope) {
  return envelope.attack != 0 || envelope.hold != 0 || envelope.decay != 0 || envelope.sustain != 0 ||
         envelope.release != 0;
}

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
  ChannelPressure,
  PolyPressure,
  PitchWheel,
  Unknown,
};

struct SynthGenerator {
  // A generator is an unconditional contribution, such as a base vibrato depth/rate.
  SynthDestination destination = SynthDestination::Unknown;
  s32 amount = 0;
};

struct SynthModulator {
  // Missing source means a constant/default modulator. Exporters map these to the closest
  // concept available in SF2/DLS rather than inventing per-format parser code.
  std::optional<SynthSource> source;
  SynthDestination destination = SynthDestination::Unknown;
  s32 amount = 0;
};

struct Region {
  // Region is the common zone abstraction: key/velocity selection, sample reference,
  // tuning, envelope, and per-zone placement.
  KeyRange keyRange;
  VelocityRange velocityRange;
  SampleRef sample;
  SourceRange range;
  Tuning tuning;
  std::optional<u8> rootKey;
  s16 coarseTuneSemitones = 0;
  s16 fineTuneCents = 0;
  Envelope envelope;
  double pan = 0.5;
  double attenuationDb = 0.0;
};

struct Instrument {
  // Bank/program are the public selection identity. The vectors below describe how the
  // instrument sounds once selected.
  u32 bank = 0;
  u32 program = 0;
  double reverb = kDefaultInstrumentReverbSend;
  std::string name;
  SourceRange range;
  std::vector<Region> regions;
  std::vector<SynthGenerator> generators;
  std::vector<SynthModulator> modulators;
};

struct InstrumentSetAsset {
  AssetMetadata metadata;
  std::vector<Instrument> instruments;
};

enum class AudioCodec {
  Unknown,
  PcmS8,
  PcmS16,
  SnesBrr,
  NdsImaAdpcm,
  NdsPsg,
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
  // Encoded bytes stay in SourceStore. That keeps assets cheap to copy and lets exporters
  // decode with source-aware diagnostics.
  SourceRange encodedData;
  u32 sampleRate = 0;
  u8 channels = 1;
  u16 bitsPerSample = 16;
  Loop loop;
  Tuning pitch;
  u32 codecParameter = 0;
  double attenuationDb = 0.0;
};

struct SampleCollection {
  std::vector<Sample> samples;
};

struct SampleCollectionAsset {
  AssetMetadata metadata;
  SampleCollection samples;
};

struct DecodedSample {
  // Decoders return interleaved signed PCM16, the common handoff format for WAV/SF2/DLS.
  u32 sampleRate = 0;
  u8 channels = 1;
  std::vector<s16> pcm;
  Loop loop;
};

}  // namespace vgmtrans::core
