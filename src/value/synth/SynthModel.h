/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "value/model/EnvelopeModel.h"
#include "value/model/InstrumentIdentity.h"
#include "value/model/MetadataModel.h"
#include "value/model/ModulationModel.h"
#include "value/synth/SampleFiltering.h"
#include "value/synth/Ym2151.h"

#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace vgmtrans::core {

// Common instrument and sample data after scanning. Format scanners fill this once,
// then SF2, DLS, WAV, or future exporters choose how to write it.

constexpr double kDefaultInstrumentReverbSend = 0.25;

struct KeyRange {
  u8 low = 0;
  u8 high = 127;

  friend bool operator==(const KeyRange&, const KeyRange&) = default;
};

struct VelocityRange {
  u8 low = 0;
  u8 high = 127;

  friend bool operator==(const VelocityRange&, const VelocityRange&) = default;
};

struct SampleRef {
  // index is local to collection. collection may be empty when a format implies
  // "use the collection paired with this instrument set". An invalid index
  // represents a structural region whose sample has not been bound yet.
  std::optional<AssetId> collection;
  u32 index = 0;

  [[nodiscard]] bool valid() const noexcept { return index != invalidIdValue; }
};

struct Tuning {
  s32 cents = 0;
};

struct Loop {
  bool enabled = false;
  u32 start = 0;
  u32 length = 0;

  friend bool operator==(const Loop&, const Loop&) = default;
};

struct Region {
  // One playable zone inside an instrument: key/velocity range, sample reference,
  // tuning, envelope, pan, attenuation, and optional region-specific loop.
  KeyRange keyRange;
  VelocityRange velocityRange;
  SampleRef sample;
  SourceRange range;
  // Exact region unity key before the independent Sample::pitch correction. A
  // fractional value preserves sub-semitone tuning without target-specific
  // root/coarse/fine fields.
  double unityKey = 60.0;
  Envelope envelope;
  std::optional<Loop> loop;
  // Synth region pan is unipolar: 0.0 left, 0.5 center, 1.0 right.
  double pan = 0.5;
  double attenuationDb = 0.0;
  // Layered hardware voices may give each zone an independent LFO. Keeping
  // that modulation on the region avoids applying one layer's curve to every
  // sample in the instrument.
  InstrumentModulation modulation;
};

struct Instrument {
  std::optional<InstrumentAddress> explicitAddress;
  std::optional<InstrumentIdentity> identity;
  // Some synthesizers give each instrument its own pitch-wheel sensitivity.
  std::optional<u16> pitchBendRangeCents;
  double reverb = kDefaultInstrumentReverbSend;
  std::string name;
  SourceRange range;
  // Hardware-synth voices are retained in their native, exporter-neutral form.
  // An instrument may also have sampled regions for layered hardware designs.
  using SynthVoice = std::variant<Ym2151Voice>;
  std::optional<SynthVoice> synthVoice;
  std::vector<Region> regions;
  InstrumentModulation modulation;
};

struct InstrumentSetAsset {
  AssetMetadata metadata;
  std::vector<Instrument> instruments;
  AssetPrivateData privateData;
};

enum class AudioCodec {
  Unknown,
  PcmS8,
  PcmS16,
  SnesBrr,
  NdsImaAdpcm,
  NdsPsg,
  GbaBdpcm,
  GbaPsg,
  GbaPsgWave,
  PsxAdpcm,
  KonamiK054539Adpcm,
  OkiAdpcm,
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
  // PCM byte order is source data, not a property of the host. Compressed
  // codecs ignore this flag.
  bool bigEndian = false;
  // Some hardware walks encoded sample memory backwards. Keeping that direction
  // explicit avoids copying or mutating source bytes during scanning.
  bool reverse = false;
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
  SampleFilter preferredFilter = SampleFilter::None;
  AssetPrivateData privateData;
};

struct DecodedSample {
  // Interleaved signed PCM16 used by WAV, SF2, and DLS exporters.
  u32 sampleRate = 0;
  u8 channels = 1;
  std::vector<s16> pcm;
  Loop loop;
};

}  // namespace vgmtrans::core
