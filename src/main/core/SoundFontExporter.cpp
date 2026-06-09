/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "core/SoundFontExporter.h"

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

constexpr u16 kSfGenPan = 17;
constexpr u16 kSfGenInstrument = 41;
constexpr u16 kSfGenKeyRange = 43;
constexpr u16 kSfGenVelRange = 44;
constexpr u16 kSfGenInitialAttenuation = 48;
constexpr u16 kSfGenCoarseTune = 51;
constexpr u16 kSfGenFineTune = 52;
constexpr u16 kSfGenSampleId = 53;
constexpr u16 kSfGenSampleModes = 54;
constexpr u16 kSfGenOverridingRootKey = 58;

constexpr u32 kSf2SamplePaddingFrames = 46;
constexpr u8 kDefaultRootKey = 60;
constexpr u32 kInstrumentRegionGenerators = 9;

struct Chunk {
  std::string id;
  u32 size = 0;
  std::vector<u8> payload;
};

struct DecodedSfSample {
  AssetId collectionId;
  u32 localIndex = 0;
  std::string name;
  Tuning pitch;
  double attenuationDb = 0.0;
  DecodedSample decoded;
  u32 startFrame = 0;
  u32 endFrame = 0;
};

struct SfRegion {
  const Region* region = nullptr;
  u16 sampleIndex = 0;
};

struct SfInstrument {
  const Instrument* instrument = nullptr;
  std::vector<SfRegion> regions;
};

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

[[nodiscard]] Diagnostic exportWarning(std::string message, std::optional<SourceRange> range = std::nullopt) {
  return Diagnostic{
      .severity = Severity::Warning,
      .message = std::move(message),
      .range = range,
  };
}

void writeAscii(std::vector<u8>& bytes, std::string_view text) {
  bytes.insert(bytes.end(), text.begin(), text.end());
}

void writeU8(std::vector<u8>& bytes, u8 value) {
  bytes.push_back(value);
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

void writeFixedString(std::vector<u8>& bytes, std::string_view text, size_t width) {
  const auto copied = std::min(text.size(), width);
  bytes.insert(bytes.end(), text.begin(), text.begin() + static_cast<std::ptrdiff_t>(copied));
  bytes.insert(bytes.end(), width - copied, 0);
}

[[nodiscard]] std::vector<u8> withEvenPad(std::vector<u8> payload) {
  if ((payload.size() & 1) != 0) {
    payload.push_back(0);
  }
  return payload;
}

[[nodiscard]] Chunk makeChunk(std::string id, std::vector<u8> payload) {
  if (payload.size() > std::numeric_limits<u32>::max()) {
    throw std::overflow_error("SF2 chunk is too large");
  }

  return Chunk{
      .id = std::move(id),
      .size = static_cast<u32>(payload.size()),
      .payload = withEvenPad(std::move(payload)),
  };
}

[[nodiscard]] Chunk makeStringChunk(std::string id, std::string_view text) {
  std::vector<u8> payload;
  writeAscii(payload, text);
  payload.push_back(0);
  return makeChunk(std::move(id), std::move(payload));
}

void appendChunk(std::vector<u8>& bytes, const Chunk& chunk) {
  writeAscii(bytes, chunk.id);
  writeLe32(bytes, chunk.size);
  bytes.insert(bytes.end(), chunk.payload.begin(), chunk.payload.end());
}

[[nodiscard]] Chunk makeListChunk(std::string type, std::vector<Chunk> children) {
  std::vector<u8> payload;
  writeAscii(payload, type);
  for (const auto& child : children) {
    appendChunk(payload, child);
  }
  return makeChunk("LIST", std::move(payload));
}

[[nodiscard]] std::vector<u8> riffSoundFont(std::vector<Chunk> children) {
  std::vector<u8> payload;
  writeAscii(payload, "sfbk");
  for (const auto& child : children) {
    appendChunk(payload, child);
  }

  std::vector<u8> bytes;
  writeAscii(bytes, "RIFF");
  if (payload.size() > std::numeric_limits<u32>::max()) {
    throw std::overflow_error("SF2 RIFF payload is too large");
  }
  writeLe32(bytes, static_cast<u32>(payload.size()));
  bytes.insert(bytes.end(), payload.begin(), payload.end());
  return bytes;
}

[[nodiscard]] std::string sf2Name(std::string name, std::string_view fallback) {
  if (name.empty()) {
    return std::string(fallback);
  }
  return name;
}

[[nodiscard]] u16 sf2Bank(u32 bank) {
  if (bank > 128) {
    return static_cast<u16>((bank >> 8) & 0x7f);
  }
  return static_cast<u16>(bank);
}

[[nodiscard]] s16 clampS16(s32 value) {
  return static_cast<s16>(std::clamp<s32>(value, std::numeric_limits<s16>::min(), std::numeric_limits<s16>::max()));
}

[[nodiscard]] u16 clampU16(u32 value) {
  return static_cast<u16>(std::min<u32>(value, std::numeric_limits<u16>::max()));
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

[[nodiscard]] s16 sf2Pan(double pan) {
  return clampS16(static_cast<s32>(std::lround((std::clamp(pan, 0.0, 1.0) - 0.5) * 1000.0)));
}

[[nodiscard]] u16 sf2Attenuation(const Region& region, const Sample& sample) {
  constexpr double centibelsPerDb = 10.0;
  constexpr double maxInitialAttenuation = 1440.0;
  return static_cast<u16>(std::clamp(std::lround((region.attenuationDb + sample.attenuationDb) * centibelsPerDb), 0l,
                                     static_cast<long>(maxInitialAttenuation)));
}

[[nodiscard]] std::vector<Chunk> infoChunks(const std::string& name) {
  std::vector<u8> ifil;
  writeLe16(ifil, 2);
  writeLe16(ifil, 1);

  return {
      makeChunk("ifil", std::move(ifil)),
      makeStringChunk("isng", "VGMTrans"),
      makeStringChunk("INAM", name),
      makeStringChunk("ISFT", "VGMTrans value core"),
  };
}

[[nodiscard]] std::vector<DecodedSfSample> decodeSamples(const SoundFontInput& input, const SourceStore& sources,
                                                         std::vector<Diagnostic>& diagnostics) {
  std::vector<DecodedSfSample> samples;
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
      if (decoded->channels != 1) {
        diagnostics.push_back(
            exportWarning("Skipping non-mono sample for SoundFont2 export", diagnosticRange(sample.encodedData)));
        continue;
      }

      samples.push_back(DecodedSfSample{
          .collectionId = collection->metadata.id,
          .localIndex = sampleIndex,
          .name = sf2Name(sample.name, "Sample"),
          .pitch = sample.pitch,
          .attenuationDb = sample.attenuationDb,
          .decoded = std::move(*decoded),
      });
    }
  }

  u32 frameCursor = 0;
  for (auto& sample : samples) {
    if (sample.decoded.pcm.size() > std::numeric_limits<u32>::max()) {
      throw std::overflow_error("SF2 sample is too large");
    }
    sample.startFrame = frameCursor;
    sample.endFrame = frameCursor + static_cast<u32>(sample.decoded.pcm.size());
    if (sample.endFrame > std::numeric_limits<u32>::max() - kSf2SamplePaddingFrames) {
      throw std::overflow_error("SF2 sample data is too large");
    }
    frameCursor = sample.endFrame + kSf2SamplePaddingFrames;
  }

  return samples;
}

using SampleIndexKey = std::pair<u32, u32>;

[[nodiscard]] std::map<SampleIndexKey, u16> sampleIndexMap(std::span<const DecodedSfSample> samples) {
  std::map<SampleIndexKey, u16> indexes;
  for (u32 i = 0; i < samples.size(); ++i) {
    indexes[{samples[i].collectionId.value, samples[i].localIndex}] = clampU16(i);
  }
  return indexes;
}

[[nodiscard]] std::optional<AssetId> defaultSampleCollection(const SoundFontInput& input) {
  for (const auto* collection : input.sampleCollections) {
    if (collection != nullptr) {
      return collection->metadata.id;
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::vector<SfInstrument> collectInstruments(const SoundFontInput& input,
                                                           const std::map<SampleIndexKey, u16>& samples,
                                                           std::vector<Diagnostic>& diagnostics) {
  std::vector<SfInstrument> instruments;
  const auto fallbackCollection = defaultSampleCollection(input);

  for (const auto* bank : input.instrumentBanks) {
    if (bank == nullptr) {
      continue;
    }

    for (const auto& instrument : bank->bank.instruments) {
      SfInstrument sfInstrument{.instrument = &instrument};
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

        sfInstrument.regions.push_back(SfRegion{
            .region = &region,
            .sampleIndex = found->second,
        });
      }

      if (!sfInstrument.regions.empty()) {
        instruments.push_back(std::move(sfInstrument));
      }
    }
  }

  return instruments;
}

[[nodiscard]] Chunk smplChunk(std::span<const DecodedSfSample> samples) {
  std::vector<u8> payload;
  for (const auto& sample : samples) {
    for (const s16 value : sample.decoded.pcm) {
      writeLeS16(payload, value);
    }
    for (u32 i = 0; i < kSf2SamplePaddingFrames; ++i) {
      writeLeS16(payload, 0);
    }
  }
  return makeChunk("smpl", std::move(payload));
}

void writeRangeGen(std::vector<u8>& bytes, u16 generator, u8 low, u8 high) {
  writeLe16(bytes, generator);
  writeU8(bytes, low);
  writeU8(bytes, high);
}

void writeAmountGen(std::vector<u8>& bytes, u16 generator, s16 value) {
  writeLe16(bytes, generator);
  writeLeS16(bytes, value);
}

void writeWordGen(std::vector<u8>& bytes, u16 generator, u16 value) {
  writeLe16(bytes, generator);
  writeLe16(bytes, value);
}

[[nodiscard]] Chunk phdrChunk(std::span<const SfInstrument> instruments) {
  std::vector<u8> payload;
  for (u32 i = 0; i < instruments.size(); ++i) {
    const auto& instrument = *instruments[i].instrument;
    writeFixedString(payload, sf2Name(instrument.name, "Preset"), 20);
    writeLe16(payload, clampU16(instrument.program));
    writeLe16(payload, sf2Bank(instrument.bank));
    writeLe16(payload, clampU16(i));
    writeLe32(payload, 0);
    writeLe32(payload, 0);
    writeLe32(payload, 0);
  }

  writeFixedString(payload, "EOP", 20);
  writeLe16(payload, 0);
  writeLe16(payload, 0);
  writeLe16(payload, clampU16(instruments.size()));
  writeLe32(payload, 0);
  writeLe32(payload, 0);
  writeLe32(payload, 0);
  return makeChunk("phdr", std::move(payload));
}

[[nodiscard]] Chunk pbagChunk(std::span<const SfInstrument> instruments) {
  std::vector<u8> payload;
  for (u32 i = 0; i < instruments.size(); ++i) {
    writeLe16(payload, clampU16(i));
    writeLe16(payload, 0);
  }
  writeLe16(payload, clampU16(instruments.size()));
  writeLe16(payload, 0);
  return makeChunk("pbag", std::move(payload));
}

[[nodiscard]] Chunk pgenChunk(std::span<const SfInstrument> instruments) {
  std::vector<u8> payload;
  for (u32 i = 0; i < instruments.size(); ++i) {
    writeWordGen(payload, kSfGenInstrument, clampU16(i));
  }
  writeWordGen(payload, 0, 0);
  return makeChunk("pgen", std::move(payload));
}

[[nodiscard]] Chunk terminalModChunk(std::string id) {
  std::vector<u8> payload(10);
  return makeChunk(std::move(id), std::move(payload));
}

[[nodiscard]] Chunk instChunk(std::span<const SfInstrument> instruments) {
  std::vector<u8> payload;
  u32 bagIndex = 0;
  for (const auto& instrument : instruments) {
    writeFixedString(payload, sf2Name(instrument.instrument->name, "Instrument"), 20);
    writeLe16(payload, clampU16(bagIndex));
    bagIndex += static_cast<u32>(instrument.regions.size());
  }

  writeFixedString(payload, "EOI", 20);
  writeLe16(payload, clampU16(bagIndex));
  return makeChunk("inst", std::move(payload));
}

[[nodiscard]] Chunk ibagChunk(std::span<const SfInstrument> instruments) {
  std::vector<u8> payload;
  u32 generatorIndex = 0;
  for (const auto& instrument : instruments) {
    for (size_t i = 0; i < instrument.regions.size(); ++i) {
      writeLe16(payload, clampU16(generatorIndex));
      writeLe16(payload, 0);
      generatorIndex += kInstrumentRegionGenerators;
    }
  }

  writeLe16(payload, clampU16(generatorIndex));
  writeLe16(payload, 0);
  return makeChunk("ibag", std::move(payload));
}

[[nodiscard]] Chunk igenChunk(std::span<const SfInstrument> instruments,
                              std::span<const DecodedSfSample> samplesByIndex) {
  std::vector<u8> payload;
  for (const auto& instrument : instruments) {
    for (const auto& sfRegion : instrument.regions) {
      const auto& region = *sfRegion.region;
      const auto& sample = samplesByIndex[sfRegion.sampleIndex];
      const auto [coarseTune, fineTune] = splitTuneCents(region.tuning.cents + sample.pitch.cents);

      writeRangeGen(payload, kSfGenKeyRange, region.keyRange.low, region.keyRange.high);
      writeRangeGen(payload, kSfGenVelRange, region.velocityRange.low, region.velocityRange.high);
      writeAmountGen(payload, kSfGenInitialAttenuation,
                     static_cast<s16>(sf2Attenuation(region, Sample{.attenuationDb = sample.attenuationDb})));
      writeAmountGen(payload, kSfGenPan, sf2Pan(region.pan));
      writeAmountGen(payload, kSfGenCoarseTune, coarseTune);
      writeAmountGen(payload, kSfGenFineTune, fineTune);
      writeWordGen(payload, kSfGenOverridingRootKey, kDefaultRootKey);
      writeWordGen(payload, kSfGenSampleModes, sample.decoded.loop.enabled ? 1 : 0);
      writeWordGen(payload, kSfGenSampleId, sfRegion.sampleIndex);
    }
  }

  writeWordGen(payload, 0, 0);
  return makeChunk("igen", std::move(payload));
}

[[nodiscard]] Chunk shdrChunk(std::span<const DecodedSfSample> samples) {
  std::vector<u8> payload;
  for (const auto& sample : samples) {
    writeFixedString(payload, sf2Name(sample.name, "Sample"), 20);
    writeLe32(payload, sample.startFrame);
    writeLe32(payload, sample.endFrame);
    const u32 loopStart =
        sample.decoded.loop.enabled ? sample.startFrame + sample.decoded.loop.start : sample.startFrame;
    const u32 loopEnd = sample.decoded.loop.enabled ? loopStart + sample.decoded.loop.length : sample.endFrame;
    writeLe32(payload, loopStart);
    writeLe32(payload, std::min(loopEnd, sample.endFrame));
    writeLe32(payload, sample.decoded.sampleRate == 0 ? 32000 : sample.decoded.sampleRate);
    writeU8(payload, kDefaultRootKey);
    writeU8(payload, 0);
    writeLe16(payload, 0);
    writeLe16(payload, 1);
  }

  writeFixedString(payload, "EOS", 20);
  writeLe32(payload, 0);
  writeLe32(payload, 0);
  writeLe32(payload, 0);
  writeLe32(payload, 0);
  writeLe32(payload, 0);
  writeU8(payload, 0);
  writeU8(payload, 0);
  writeLe16(payload, 0);
  writeLe16(payload, 1);
  return makeChunk("shdr", std::move(payload));
}

[[nodiscard]] std::vector<Chunk> pdtaChunks(std::span<const SfInstrument> instruments,
                                            std::span<const DecodedSfSample> samples) {
  return {
      phdrChunk(instruments),   pbagChunk(instruments),          terminalModChunk("pmod"),
      pgenChunk(instruments),   instChunk(instruments),          ibagChunk(instruments),
      terminalModChunk("imod"), igenChunk(instruments, samples), shdrChunk(samples),
  };
}

}  // namespace

SoundFontResult SoundFontExporter::exportSoundFont(const SoundFontInput& input, const SourceStore& sources) const {
  SoundFontResult result;

  auto samples = decodeSamples(input, sources, result.diagnostics);
  const auto samplesByReference = sampleIndexMap(samples);
  auto instruments = collectInstruments(input, samplesByReference, result.diagnostics);

  if (samples.empty()) {
    result.diagnostics.push_back(exportError("No decodable samples available for SoundFont2 export"));
    return result;
  }
  if (instruments.empty()) {
    result.diagnostics.push_back(
        exportError("No instruments with valid sample regions available for SoundFont2 export"));
    return result;
  }

  result.bytes = riffSoundFont({
      makeListChunk("INFO", infoChunks(sf2Name(input.name, "VGMTrans"))),
      makeListChunk("sdta", {smplChunk(samples)}),
      makeListChunk("pdta", pdtaChunks(instruments, samples)),
  });
  return result;
}

}  // namespace vgmtrans::core
