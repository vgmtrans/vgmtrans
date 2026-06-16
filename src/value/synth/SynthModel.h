/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/model/MetadataModel.h"

#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace vgmtrans::core {

// Common instrument and sample data after scanning. Format scanners fill this once,
// then SF2, DLS, WAV, or future exporters choose how to write it.

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
  // index is local to collection. collection may be empty when a format implies
  // "use the collection paired with this instrument set".
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

[[nodiscard]] inline bool hasCoarseEnvelope(const Envelope& envelope) {
  return envelope.attack != 0 || envelope.hold != 0 || envelope.decay != 0 || envelope.sustain != 0 ||
         envelope.release != 0;
}

[[nodiscard]] inline bool hasPreciseEnvelope(const Envelope& envelope) {
  return envelope.attackSeconds.has_value() || envelope.holdSeconds.has_value() || envelope.decaySeconds.has_value() ||
         envelope.releaseSeconds.has_value() || envelope.sustainAmplitude.has_value();
}

[[nodiscard]] inline bool hasAnyEnvelopeData(const Envelope& envelope) {
  return hasCoarseEnvelope(envelope) || hasPreciseEnvelope(envelope);
}

[[nodiscard]] inline bool hasExplicitEnvelope(const Envelope& envelope) {
  return hasAnyEnvelopeData(envelope);
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
  // Empty source means the modulation is always active, such as a default vibrato
  // amount attached to an instrument.
  std::optional<SynthSource> source;
  SynthDestination destination = SynthDestination::Unknown;
  s32 amount = 0;
};

struct Region {
  // One playable zone inside an instrument: key/velocity range, sample reference,
  // tuning, envelope, pan, and attenuation.
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
  // Bank/program select the instrument. regions/generators/modulators describe
  // how it sounds once selected.
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
  // Encoded bytes stay in SourceStore. Samples keep only a source range plus the
  // codec settings needed to decode it later.
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
  // Interleaved signed PCM16 used by WAV, SF2, and DLS exporters.
  u32 sampleRate = 0;
  u8 channels = 1;
  std::vector<s16> pcm;
  Loop loop;
};

}  // namespace vgmtrans::core
