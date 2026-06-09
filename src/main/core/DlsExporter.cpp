/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "core/DlsExporter.h"

#include "core/SampleDecoder.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace vgmtrans::core {

namespace {

constexpr u16 kWaveFormatPcm = 1;
constexpr u16 kBitsPerSample = 16;
constexpr u16 kDefaultRootKey = 60;
constexpr u16 kDlsConnSrcNone = 0;
constexpr u16 kDlsConnSrcLfo = 0x0001;
constexpr u16 kDlsConnSrcVibrato = 0x0009;
constexpr u16 kDlsConnDstAttenuation = 0x0001;
constexpr u16 kDlsConnDstPitch = 0x0003;
constexpr u16 kDlsConnDstPan = 0x0004;
constexpr u16 kDlsConnDstLfoFrequency = 0x0104;
constexpr u16 kDlsConnDstVibFrequency = 0x0114;
constexpr u16 kDlsConnDstEg1AttackTime = 0x0206;
constexpr u16 kDlsConnDstEg1DecayTime = 0x0207;
constexpr u16 kDlsConnDstEg1ReleaseTime = 0x0209;
constexpr u16 kDlsConnDstEg1SustainLevel = 0x020a;
constexpr u16 kDlsConnTrnNone = 0;
constexpr s32 kDlsSustainLevelFullScale = 0x03e80000;

struct Chunk {
  std::string id;
  u32 size = 0;
  std::vector<u8> payload;
};

struct DecodedDlsSample {
  AssetId collectionId;
  u32 localIndex = 0;
  std::string name;
  Tuning pitch;
  double attenuationDb = 0.0;
  DecodedSample decoded;
};

struct DlsRegion {
  const Region* region = nullptr;
  u16 waveIndex = 0;
};

struct DlsInstrument {
  const Instrument* instrument = nullptr;
  std::vector<DlsRegion> regions;
};

struct DlsConnection {
  u16 source = kDlsConnSrcNone;
  u16 control = kDlsConnSrcNone;
  u16 destination = 0;
  s32 scale = 0;
};

using SampleIndexKey = std::pair<u32, u32>;

[[nodiscard]] std::optional<SourceRange> diagnosticRange(SourceRange range) {
  if (!range.valid()) {
    return std::nullopt;
  }
  return range;
}

[[nodiscard]] Diagnostic exportError(std::string message, std::optional<SourceRange> range = std::nullopt) {
  return Diagnostic{
      .severity = Severity::Error,
      .message = std::move(message),
      .range = range,
  };
}

void writeAscii(std::vector<u8>& bytes, std::string_view text) {
  bytes.insert(bytes.end(), text.begin(), text.end());
}

void writeLe16(std::vector<u8>& bytes, u16 value) {
  bytes.push_back(static_cast<u8>(value & 0xff));
  bytes.push_back(static_cast<u8>((value >> 8) & 0xff));
}

void writeLeS16(std::vector<u8>& bytes, s16 value) {
  writeLe16(bytes, static_cast<u16>(value));
}

void writeLe32(std::vector<u8>& bytes, u32 value) {
  bytes.push_back(static_cast<u8>(value & 0xff));
  bytes.push_back(static_cast<u8>((value >> 8) & 0xff));
  bytes.push_back(static_cast<u8>((value >> 16) & 0xff));
  bytes.push_back(static_cast<u8>((value >> 24) & 0xff));
}

void writeLeS32(std::vector<u8>& bytes, s32 value) {
  writeLe32(bytes, static_cast<u32>(value));
}

void writeFixedString(std::vector<u8>& bytes, std::string_view text) {
  writeAscii(bytes, text);
  bytes.push_back(0);
}

[[nodiscard]] std::vector<u8> withEvenPad(std::vector<u8> payload) {
  if ((payload.size() & 1) != 0) {
    payload.push_back(0);
  }
  return payload;
}

[[nodiscard]] Chunk makeChunk(std::string id, std::vector<u8> payload) {
  if (payload.size() > std::numeric_limits<u32>::max()) {
    throw std::overflow_error("DLS chunk is too large");
  }
  return Chunk{
      .id = std::move(id),
      .size = static_cast<u32>(payload.size()),
      .payload = withEvenPad(std::move(payload)),
  };
}

void appendChunk(std::vector<u8>& bytes, const Chunk& chunk) {
  writeAscii(bytes, chunk.id);
  writeLe32(bytes, chunk.size);
  bytes.insert(bytes.end(), chunk.payload.begin(), chunk.payload.end());
}

[[nodiscard]] u32 chunkStorageSize(const Chunk& chunk) {
  if (chunk.payload.size() > std::numeric_limits<u32>::max() - 8) {
    throw std::overflow_error("DLS chunk is too large");
  }
  return static_cast<u32>(8 + chunk.payload.size());
}

[[nodiscard]] Chunk makeListChunk(std::string type, std::vector<Chunk> children) {
  std::vector<u8> payload;
  writeAscii(payload, type);
  for (const auto& child : children) {
    appendChunk(payload, child);
  }
  return makeChunk("LIST", std::move(payload));
}

[[nodiscard]] std::vector<u8> riffDls(std::vector<Chunk> children) {
  std::vector<u8> payload;
  writeAscii(payload, "DLS ");
  for (const auto& child : children) {
    appendChunk(payload, child);
  }

  if (payload.size() > std::numeric_limits<u32>::max()) {
    throw std::overflow_error("DLS RIFF payload is too large");
  }

  std::vector<u8> bytes;
  writeAscii(bytes, "RIFF");
  writeLe32(bytes, static_cast<u32>(payload.size()));
  bytes.insert(bytes.end(), payload.begin(), payload.end());
  return bytes;
}

[[nodiscard]] std::string dlsName(std::string name, std::string_view fallback) {
  if (name.empty()) {
    return std::string(fallback);
  }
  return name;
}

[[nodiscard]] u16 clampU16(u32 value) {
  return static_cast<u16>(std::min<u32>(value, std::numeric_limits<u16>::max()));
}

[[nodiscard]] u8 clampU7(s32 value) {
  return static_cast<u8>(std::clamp<s32>(value, 0, 127));
}

[[nodiscard]] std::pair<s16, s16> splitTuneCents(s32 cents) {
  s16 coarse = static_cast<s16>(cents / 100);
  s16 fine = static_cast<s16>(cents % 100);
  if (fine > 50) {
    ++coarse;
    fine -= 100;
  } else if (fine < -50) {
    --coarse;
    fine += 100;
  }
  return {coarse, fine};
}

[[nodiscard]] s32 clampS32(s64 value) {
  return static_cast<s32>(std::clamp<s64>(
      value, std::numeric_limits<s32>::min(), std::numeric_limits<s32>::max()));
}

[[nodiscard]] s32 dls16Dot16Scale(s32 value) {
  return clampS32(static_cast<s64>(value) * 65536);
}

[[nodiscard]] s32 dlsPitchScale(s32 cents) {
  return dls16Dot16Scale(cents);
}

[[nodiscard]] s32 dlsAttenuation(const Region& region, const DecodedDlsSample& sample) {
  constexpr double centibelsPerDb = 10.0;
  return static_cast<s32>(std::lround((region.attenuationDb + sample.attenuationDb) * centibelsPerDb));
}

[[nodiscard]] std::optional<DlsConnection> dlsConnectionForGenerator(const SynthGenerator& generator) {
  switch (generator.destination) {
    case SynthDestination::Pitch:
      return DlsConnection{.destination = kDlsConnDstPitch, .scale = dlsPitchScale(generator.amount)};
    case SynthDestination::Volume:
      return DlsConnection{.destination = kDlsConnDstAttenuation, .scale = dls16Dot16Scale(generator.amount)};
    case SynthDestination::Pan:
      return DlsConnection{.destination = kDlsConnDstPan, .scale = dls16Dot16Scale(generator.amount)};
    case SynthDestination::VibratoDepth:
      return DlsConnection{
          .source = kDlsConnSrcVibrato,
          .destination = kDlsConnDstPitch,
          .scale = dlsPitchScale(generator.amount),
      };
    case SynthDestination::VibratoRate:
      return DlsConnection{.destination = kDlsConnDstVibFrequency, .scale = dlsPitchScale(generator.amount)};
    case SynthDestination::TremoloDepth:
      return DlsConnection{
          .source = kDlsConnSrcLfo,
          .destination = kDlsConnDstAttenuation,
          .scale = dls16Dot16Scale(generator.amount),
      };
    case SynthDestination::TremoloRate:
      return DlsConnection{.destination = kDlsConnDstLfoFrequency, .scale = dlsPitchScale(generator.amount)};
    case SynthDestination::FilterCutoff:
    case SynthDestination::Unknown:
      return std::nullopt;
  }

  return std::nullopt;
}

[[nodiscard]] s32 dlsEnvelopeTimecents(u32 microseconds) {
  if (microseconds == 0) {
    return std::numeric_limits<s32>::min();
  }
  if (microseconds == kEnvelopeInfinite) {
    return std::numeric_limits<s32>::max();
  }

  const double seconds = static_cast<double>(microseconds) / 1'000'000.0;
  const double timecents = 1200.0 * std::log2(seconds) * 65536.0;
  return static_cast<s32>(std::clamp(std::lround(timecents), static_cast<long>(std::numeric_limits<s32>::min()),
                                     static_cast<long>(std::numeric_limits<s32>::max())));
}

[[nodiscard]] s32 dlsSustainLevel(const Envelope& envelope) {
  if (envelope.sustain == 0) {
    return 0;
  }

  const double amplitude = std::clamp(static_cast<double>(envelope.sustain) / 1000.0, 0.0, 1.0);
  if (amplitude >= 1.0) {
    return kDlsSustainLevelFullScale;
  }

  const double attenuationDb = std::clamp(-20.0 * std::log10(amplitude), 0.0, 96.0);
  const double scaledLevel = ((96.0 - attenuationDb) / 96.0) * kDlsSustainLevelFullScale;
  return static_cast<s32>(std::clamp(std::lround(scaledLevel), 0l, static_cast<long>(kDlsSustainLevelFullScale)));
}

[[nodiscard]] std::vector<DecodedDlsSample> decodeSamples(const DlsInput& input, const SourceStore& sources,
                                                          std::vector<Diagnostic>& diagnostics) {
  std::vector<DecodedDlsSample> samples;
  auto decoders = SampleDecoderRegistry::withDefaultDecoders();

  for (const auto* collection : input.sampleCollections) {
    if (collection == nullptr) {
      continue;
    }

    for (u32 sampleIndex = 0; sampleIndex < collection->samples.samples.size(); ++sampleIndex) {
      const auto& sample = collection->samples.samples[sampleIndex];
      if (!sources.contains(sample.encodedData.source)) {
        diagnostics.push_back(exportError("Sample source was not found", diagnosticRange(sample.encodedData)));
        continue;
      }

      auto decoded = decoders.decode(sample, sources.bytes(sample.encodedData.source));
      if (!decoded) {
        diagnostics.push_back(exportError("No decoder registered for sample codec", diagnosticRange(sample.encodedData)));
        continue;
      }

      samples.push_back(DecodedDlsSample{
          .collectionId = collection->metadata.id,
          .localIndex = sampleIndex,
          .name = dlsName(sample.name, "Wave"),
          .pitch = sample.pitch,
          .attenuationDb = sample.attenuationDb,
          .decoded = std::move(*decoded),
      });
    }
  }

  return samples;
}

[[nodiscard]] std::map<SampleIndexKey, u16> sampleIndexMap(std::span<const DecodedDlsSample> samples) {
  std::map<SampleIndexKey, u16> indexes;
  for (u32 i = 0; i < samples.size(); ++i) {
    indexes[{samples[i].collectionId.value, samples[i].localIndex}] = clampU16(i);
  }
  return indexes;
}

[[nodiscard]] std::optional<AssetId> defaultSampleCollection(const DlsInput& input) {
  for (const auto* collection : input.sampleCollections) {
    if (collection != nullptr) {
      return collection->metadata.id;
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::vector<DlsInstrument> collectInstruments(const DlsInput& input,
                                                            const std::map<SampleIndexKey, u16>& samples,
                                                            std::vector<Diagnostic>& diagnostics) {
  std::vector<DlsInstrument> instruments;
  const auto fallbackCollection = defaultSampleCollection(input);

  for (const auto* bank : input.instrumentBanks) {
    if (bank == nullptr) {
      continue;
    }

    for (const auto& instrument : bank->bank.instruments) {
      DlsInstrument dlsInstrument{.instrument = &instrument};
      for (const auto& region : instrument.regions) {
        const std::optional<AssetId> collectionId =
            region.sample.collection ? region.sample.collection : fallbackCollection;
        if (!collectionId) {
          diagnostics.push_back(exportError("Region does not reference a sample collection", diagnosticRange(region.range)));
          continue;
        }

        const auto found = samples.find({collectionId->value, region.sample.index});
        if (found == samples.end()) {
          diagnostics.push_back(exportError("Region sample reference was not found", diagnosticRange(region.range)));
          continue;
        }

        dlsInstrument.regions.push_back(DlsRegion{
            .region = &region,
            .waveIndex = found->second,
        });
      }

      if (!dlsInstrument.regions.empty()) {
        instruments.push_back(std::move(dlsInstrument));
      }
    }
  }

  return instruments;
}

[[nodiscard]] Chunk colhChunk(std::span<const DlsInstrument> instruments) {
  std::vector<u8> payload;
  writeLe32(payload, static_cast<u32>(instruments.size()));
  return makeChunk("colh", std::move(payload));
}

[[nodiscard]] Chunk infoList(std::string name) {
  std::vector<u8> inam;
  writeFixedString(inam, dlsName(std::move(name), "DLS"));
  return makeListChunk("INFO", {makeChunk("INAM", std::move(inam))});
}

[[nodiscard]] Chunk inshChunk(const DlsInstrument& instrument) {
  std::vector<u8> payload;
  writeLe32(payload, static_cast<u32>(instrument.regions.size()));
  writeLe32(payload, instrument.instrument->bank);
  writeLe32(payload, instrument.instrument->program);
  return makeChunk("insh", std::move(payload));
}

[[nodiscard]] Chunk rgnhChunk(const Region& region) {
  std::vector<u8> payload;
  writeLe16(payload, region.keyRange.low);
  writeLe16(payload, region.keyRange.high);
  writeLe16(payload, region.velocityRange.low);
  writeLe16(payload, region.velocityRange.high);
  writeLe16(payload, 1);
  writeLe16(payload, 0);
  writeLe16(payload, 1);
  return makeChunk("rgnh", std::move(payload));
}

[[nodiscard]] Chunk wsmpChunk(const Region& region, const DecodedDlsSample& sample) {
  const auto combinedTune = splitTuneCents(region.tuning.cents + sample.pitch.cents);
  const u8 unityKey = clampU7(static_cast<s32>(kDefaultRootKey) - combinedTune.first);

  std::vector<u8> payload;
  writeLe32(payload, 20);
  writeLe16(payload, unityKey);
  writeLeS16(payload, combinedTune.second);
  writeLeS32(payload, dlsAttenuation(region, sample));
  writeLe32(payload, 1);
  writeLe32(payload, sample.decoded.loop.enabled ? 1 : 0);
  if (sample.decoded.loop.enabled) {
    writeLe32(payload, 16);
    writeLe32(payload, 0);
    writeLe32(payload, sample.decoded.loop.start);
    writeLe32(payload, sample.decoded.loop.length);
  }
  return makeChunk("wsmp", std::move(payload));
}

[[nodiscard]] Chunk wlnkChunk(u16 waveIndex) {
  std::vector<u8> payload;
  writeLe16(payload, 0);
  writeLe16(payload, 0);
  writeLe32(payload, 1);
  writeLe32(payload, waveIndex);
  return makeChunk("wlnk", std::move(payload));
}

void writeConnection(std::vector<u8>& bytes, DlsConnection connection) {
  writeLe16(bytes, connection.source);
  writeLe16(bytes, connection.control);
  writeLe16(bytes, connection.destination);
  writeLe16(bytes, kDlsConnTrnNone);
  writeLeS32(bytes, connection.scale);
}

void writeConnection(std::vector<u8>& bytes, u16 destination, s32 scale) {
  writeConnection(bytes, DlsConnection{.destination = destination, .scale = scale});
}

[[nodiscard]] Chunk art2Chunk(const Instrument& instrument, const Region& region) {
  const auto panScale = static_cast<s32>(std::lround((std::clamp(region.pan, 0.0, 1.0) - 0.5) * 65536.0));

  std::vector<u8> connections;
  writeConnection(connections, kDlsConnDstPan, panScale);
  if (hasExplicitEnvelope(region.envelope)) {
    writeConnection(connections, kDlsConnDstEg1AttackTime, dlsEnvelopeTimecents(region.envelope.attack));
    writeConnection(connections, kDlsConnDstEg1DecayTime, dlsEnvelopeTimecents(region.envelope.decay));
    writeConnection(connections, kDlsConnDstEg1SustainLevel, dlsSustainLevel(region.envelope));
    writeConnection(connections, kDlsConnDstEg1ReleaseTime, dlsEnvelopeTimecents(region.envelope.release));
  }
  for (const auto& generator : instrument.generators) {
    const auto connection = dlsConnectionForGenerator(generator);
    if (!connection) {
      continue;
    }
    writeConnection(connections, *connection);
  }

  std::vector<u8> art;
  writeLe32(art, 8);
  writeLe32(art, static_cast<u32>(connections.size() / 12));
  art.insert(art.end(), connections.begin(), connections.end());
  return makeListChunk("lar2", {makeChunk("art2", std::move(art))});
}

[[nodiscard]] Chunk rgn2Chunk(
    const Instrument& instrument,
    const DlsRegion& dlsRegion,
    std::span<const DecodedDlsSample> samples) {
  const auto& region = *dlsRegion.region;
  const auto& sample = samples[dlsRegion.waveIndex];
  return makeListChunk("rgn2", {
                                   rgnhChunk(region),
                                   wsmpChunk(region, sample),
                                   wlnkChunk(dlsRegion.waveIndex),
                                   art2Chunk(instrument, region),
                               });
}

[[nodiscard]] Chunk lrgnList(const DlsInstrument& instrument, std::span<const DecodedDlsSample> samples) {
  std::vector<Chunk> regions;
  regions.reserve(instrument.regions.size());
  for (const auto& region : instrument.regions) {
    regions.push_back(rgn2Chunk(*instrument.instrument, region, samples));
  }
  return makeListChunk("lrgn", std::move(regions));
}

[[nodiscard]] Chunk insList(const DlsInstrument& instrument, std::span<const DecodedDlsSample> samples) {
  return makeListChunk("ins ", {
                                   inshChunk(instrument),
                                   lrgnList(instrument, samples),
                                   infoList(dlsName(instrument.instrument->name, "Instrument")),
                               });
}

[[nodiscard]] Chunk linsList(std::span<const DlsInstrument> instruments, std::span<const DecodedDlsSample> samples) {
  std::vector<Chunk> instrumentChunks;
  instrumentChunks.reserve(instruments.size());
  for (const auto& instrument : instruments) {
    instrumentChunks.push_back(insList(instrument, samples));
  }
  return makeListChunk("lins", std::move(instrumentChunks));
}

[[nodiscard]] Chunk fmtChunk(const DecodedDlsSample& sample) {
  const u16 channels = std::max<u8>(sample.decoded.channels, 1);
  const u32 sampleRate = sample.decoded.sampleRate == 0 ? 32000 : sample.decoded.sampleRate;
  const u16 blockAlign = static_cast<u16>(channels * (kBitsPerSample / 8));
  const u32 byteRate = sampleRate * blockAlign;

  std::vector<u8> payload;
  writeLe16(payload, kWaveFormatPcm);
  writeLe16(payload, channels);
  writeLe32(payload, sampleRate);
  writeLe32(payload, byteRate);
  writeLe16(payload, blockAlign);
  writeLe16(payload, kBitsPerSample);
  writeLe16(payload, 0);
  return makeChunk("fmt ", std::move(payload));
}

[[nodiscard]] Chunk dataChunk(const DecodedDlsSample& sample) {
  std::vector<u8> payload;
  payload.reserve(sample.decoded.pcm.size() * 2);
  for (const s16 value : sample.decoded.pcm) {
    writeLeS16(payload, value);
  }
  return makeChunk("data", std::move(payload));
}

[[nodiscard]] Chunk waveList(const DecodedDlsSample& sample) {
  return makeListChunk("wave", {
                                   fmtChunk(sample),
                                   dataChunk(sample),
                                   infoList(sample.name),
                               });
}

[[nodiscard]] Chunk ptblChunk(std::span<const Chunk> waveChunks) {
  std::vector<u8> payload;
  writeLe32(payload, 8);
  writeLe32(payload, static_cast<u32>(waveChunks.size()));

  u32 offset = 0;
  for (const auto& wave : waveChunks) {
    writeLe32(payload, offset);
    offset += chunkStorageSize(wave);
  }

  return makeChunk("ptbl", std::move(payload));
}

[[nodiscard]] std::vector<Chunk> waveChunks(std::span<const DecodedDlsSample> samples) {
  std::vector<Chunk> waves;
  waves.reserve(samples.size());
  for (const auto& sample : samples) {
    waves.push_back(waveList(sample));
  }
  return waves;
}

}  // namespace

DlsResult DlsExporter::exportDls(const DlsInput& input, const SourceStore& sources) const {
  DlsResult result;

  auto samples = decodeSamples(input, sources, result.diagnostics);
  const auto samplesByReference = sampleIndexMap(samples);
  auto instruments = collectInstruments(input, samplesByReference, result.diagnostics);

  if (samples.empty()) {
    result.diagnostics.push_back(exportError("No decodable samples available for DLS export"));
    return result;
  }
  if (instruments.empty()) {
    result.diagnostics.push_back(exportError("No instruments with valid sample regions available for DLS export"));
    return result;
  }

  auto waves = waveChunks(samples);
  result.bytes = riffDls({
      colhChunk(instruments),
      linsList(instruments, samples),
      ptblChunk(waves),
      makeListChunk("wvpl", std::move(waves)),
      infoList(dlsName(input.name, "DLS")),
  });
  return result;
}

}  // namespace vgmtrans::core
