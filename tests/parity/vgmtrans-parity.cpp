/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "Root.h"
#include "components/VGMColl.h"
#include "components/VGMSampColl.h"
#include "components/instr/VGMInstrSet.h"
#include "components/instr/VGMRgn.h"
#include "components/seq/VGMSeq.h"
#include "conversion/MidiFile.h"
#include "core/Export.h"
#include "core/MidiExporter.h"
#include "core/Model.h"
#include "core/ProjectSession.h"
#include "core/SampleDecoder.h"
#include "formats/CapcomSnes/CapcomSnesModule.h"
#include "formats/CapcomSnes/CapcomSnesProfile.h"
#include "formats/ValueFormats.h"
#include "io/RawFile.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

using namespace vgmtrans::core;
using namespace vgmtrans::formats::capcom_snes;

namespace {

void expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

std::vector<u8> readFile(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    throw std::runtime_error("failed to open input file: " + path.string());
  }

  stream.seekg(0, std::ios::end);
  const auto size = stream.tellg();
  if (size < 0) {
    throw std::runtime_error("failed to stat input file: " + path.string());
  }
  stream.seekg(0, std::ios::beg);

  std::vector<u8> bytes(static_cast<size_t>(size));
  if (!bytes.empty()) {
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  }
  if (!stream) {
    throw std::runtime_error("failed to read input file: " + path.string());
  }
  return bytes;
}

void writeLe16(std::vector<u8>& bytes, size_t offset, u16 value) {
  bytes[offset] = static_cast<u8>(value & 0xff);
  bytes[offset + 1] = static_cast<u8>((value >> 8) & 0xff);
}

void writeBe16(std::vector<u8>& bytes, size_t offset, u16 value) {
  bytes[offset] = static_cast<u8>((value >> 8) & 0xff);
  bytes[offset + 1] = static_cast<u8>(value & 0xff);
}

template <size_t Size>
void writeBytes(std::vector<u8>& bytes, size_t offset, const std::array<u8, Size>& values) {
  std::ranges::copy(values, bytes.begin() + static_cast<std::ptrdiff_t>(offset));
}

std::vector<u8> makeCapcomSnesAram() {
  std::vector<u8> bytes(0x10000);

  constexpr std::array<u8, 16> readBgmAddressPattern{0x6f, 0x3f, 0xef, 0x06, 0x8f, 0x0d, 0xa1, 0x8f,
                                                     0xaf, 0xa0, 0x3f, 0x82, 0x05, 0x8d, 0x00, 0xdd};
  writeBytes(bytes, 0x0500, readBgmAddressPattern);
  bytes[0x0500 + 5] = 0x20;
  bytes[0x0500 + 8] = 0x00;

  constexpr std::array<u8, 12> loadInstrTablePattern{0x8d, 0x06, 0xcf, 0xda, 0xa0, 0x60,
                                                     0x98, 0xac, 0xa0, 0x98, 0x47, 0xa1};
  writeBytes(bytes, 0x0600, loadInstrTablePattern);
  bytes[0x0600 + 7] = 0x00;
  bytes[0x0600 + 10] = 0x40;

  constexpr std::array<u8, 16> dspRegInitPattern{0x8d, 0x03, 0xf6, 0x63, 0x04, 0xc5, 0xf2, 0x00,
                                                 0xf6, 0x66, 0x04, 0xc5, 0xf3, 0x00, 0xfe, 0xf2};
  writeBytes(bytes, 0x0700, dspRegInitPattern);
  bytes[0x0700 + 1] = 1;
  writeLe16(bytes, 0x0700 + 3, 0x0800);
  writeLe16(bytes, 0x0700 + 9, 0x0810);
  bytes[0x0801] = 0x5d;
  bytes[0x0811] = 0x50;

  for (int track = 0; track < 8; ++track) {
    writeBe16(bytes, 0x2001 + track * 2, 0x3000);
  }

  bytes[0x3000] = 0x05;
  bytes[0x3001] = 0x12;
  bytes[0x3002] = 0x34;
  bytes[0x3003] = 0x07;
  bytes[0x3004] = 0x40;
  bytes[0x3005] = 0x18;
  bytes[0x3006] = 0x00;
  bytes[0x3007] = 0x1a;
  bytes[0x3008] = 0x00;
  bytes[0x3009] = 0x20;
  bytes[0x300a] = 0x41;
  bytes[0x300b] = 0x17;

  bytes[0x4000] = 0x00;
  bytes[0x4001] = 0x8f;
  bytes[0x4002] = 0xe0;
  bytes[0x4003] = 0x00;
  writeBe16(bytes, 0x4004, 0x0100);

  writeLe16(bytes, 0x5000, 0x6000);
  writeLe16(bytes, 0x5002, 0x6000);
  bytes[0x6000] = 0x01;

  return bytes;
}

class HeadlessRoot final : public VGMRoot {
public:
  void UI_setRootPtr(VGMRoot** root) override { *root = this; }
  void UI_log(LogItem*) override {}

  std::filesystem::path UI_getSaveFilePath(const std::string& suggestedFilename,
                                           const std::string& extension = "") override {
    return std::filesystem::path(suggestedFilename).replace_extension(extension);
  }

  std::filesystem::path UI_getSaveDirPath(const std::filesystem::path& suggestedDir = {}) override {
    if (!suggestedDir.empty()) {
      return suggestedDir;
    }
    return std::filesystem::current_path();
  }
};

std::unique_ptr<HeadlessRoot> scanLegacyCapcomSnes(std::span<const u8> aramBytes, const std::string& name) {
  if (aramBytes.size() > std::numeric_limits<u32>::max()) {
    throw std::runtime_error("input is too large for legacy VirtFile");
  }

  auto root = std::make_unique<HeadlessRoot>();
  root->init();

  auto rawFile = std::make_unique<VirtFile>(aramBytes.data(), static_cast<u32>(aramBytes.size()), name);
  rawFile->setUseLoaders(false);

  if (!root->loadRawFile(std::move(rawFile))) {
    throw std::runtime_error("legacy scanner did not discover any files");
  }

  return root;
}

std::unique_ptr<HeadlessRoot> scanLegacyFile(const std::filesystem::path& path) {
  auto root = std::make_unique<HeadlessRoot>();
  root->init();
  if (!root->openRawFile(path)) {
    throw std::runtime_error("legacy scanner did not discover any files in: " + path.string());
  }
  return root;
}

struct NamedBytes {
  std::string name;
  std::vector<u8> bytes;
};

std::vector<NamedBytes> legacyExtractedArams(const std::filesystem::path& path) {
  const auto root = scanLegacyFile(path);
  std::vector<NamedBytes> arams;

  for (const auto* rawFile : root->rawFiles()) {
    if (rawFile == nullptr || rawFile->size() != 0x10000) {
      continue;
    }
    const auto* begin = reinterpret_cast<const u8*>(rawFile->data());
    arams.push_back(NamedBytes{
        .name = rawFile->name(),
        .bytes = std::vector<u8>(begin, begin + rawFile->size()),
    });
  }

  std::ranges::sort(arams, {}, &NamedBytes::name);
  return arams;
}

std::vector<u8> legacyCapcomSnesMidi(std::span<const u8> aramBytes, const std::string& name) {
  const auto root = scanLegacyCapcomSnes(aramBytes, name);

  for (const auto& file : root->vgmFiles()) {
    const auto* sequenceSlot = std::get_if<VGMSeq*>(&file);
    if (sequenceSlot == nullptr || *sequenceSlot == nullptr) {
      continue;
    }
    auto* sequence = *sequenceSlot;

    auto midi = sequence->convertToMidi(nullptr);
    if (!midi) {
      throw std::runtime_error("legacy sequence failed to convert to MIDI");
    }

    std::vector<u8> bytes;
    midi->writeMidiToBuffer(bytes);
    return bytes;
  }

  throw std::runtime_error("legacy scanner did not discover a sequence");
}

std::map<std::string, std::vector<u8>> legacyCapcomSnesRsnMidis(const std::filesystem::path& path) {
  const auto root = scanLegacyFile(path);
  std::map<std::string, std::vector<u8>> midis;

  for (auto* collection : root->vgmColls()) {
    if (collection == nullptr || collection->seq() == nullptr) {
      continue;
    }

    auto midi = collection->seq()->convertToMidi(collection);
    if (!midi) {
      throw std::runtime_error("legacy collection failed to convert to MIDI: " + collection->name());
    }

    std::vector<u8> bytes;
    midi->writeMidiToBuffer(bytes);
    auto [_, inserted] = midis.emplace(collection->name(), std::move(bytes));
    if (!inserted) {
      throw std::runtime_error("duplicate legacy collection name from RSN: " + collection->name());
    }
  }

  if (midis.empty()) {
    throw std::runtime_error("legacy scanner did not discover collections in: " + path.string());
  }
  return midis;
}

struct SampleSummary {
  u32 index = 0;
  u32 sourceOffset = 0;
  u32 sourceSize = 0;
  u32 sampleRate = 0;
  u8 channels = 0;
  u32 frameCount = 0;
  bool loopEnabled = false;
  u32 loopStart = 0;
  u32 loopLength = 0;
  u64 pcmHash = 0;

  friend bool operator==(const SampleSummary&, const SampleSummary&) = default;
};

struct RegionSummary {
  u32 bank = 0;
  u32 program = 0;
  u32 sourceOffset = 0;
  u8 keyLow = 0;
  u8 keyHigh = 0;
  u8 velocityLow = 0;
  u8 velocityHigh = 0;
  u32 sampleSourceOffset = 0;
  s32 tuningCents = 0;
  u32 envelopeAttack = 0;
  u32 envelopeDecay = 0;
  u32 envelopeSustain = 0;
  u32 envelopeRelease = 0;

  friend bool operator==(const RegionSummary&, const RegionSummary&) = default;
};

struct GeneratorSummary {
  s32 destination = 0;
  s32 amount = 0;

  friend bool operator==(const GeneratorSummary&, const GeneratorSummary&) = default;
};

struct ModulatorSummary {
  std::optional<s32> source;
  s32 destination = 0;
  s32 amount = 0;

  friend bool operator==(const ModulatorSummary&, const ModulatorSummary&) = default;
};

struct InstrumentSynthSummary {
  u32 bank = 0;
  u32 program = 0;
  u32 sourceOffset = 0;
  std::vector<GeneratorSummary> generators;
  std::vector<ModulatorSummary> modulators;

  friend bool operator==(const InstrumentSynthSummary&, const InstrumentSynthSummary&) = default;
};

struct CapcomSnesSummary {
  u32 sequenceCount = 0;
  std::vector<u32> trackCounts;
  u32 instrumentBankCount = 0;
  u32 sampleCollectionCount = 0;
  std::vector<SampleSummary> samples;
  std::vector<RegionSummary> regions;
  std::vector<InstrumentSynthSummary> instrumentSynths;

  friend bool operator==(const CapcomSnesSummary&, const CapcomSnesSummary&) = default;
};

u64 fnv1a(std::span<const u8> bytes) {
  u64 hash = 14695981039346656037ull;
  for (const u8 byte : bytes) {
    hash ^= byte;
    hash *= 1099511628211ull;
  }
  return hash;
}

u64 fnv1aPcm16(std::span<const s16> samples) {
  u64 hash = 14695981039346656037ull;
  for (const s16 sample : samples) {
    const auto value = static_cast<u16>(sample);
    hash ^= static_cast<u8>(value & 0xff);
    hash *= 1099511628211ull;
    hash ^= static_cast<u8>((value >> 8) & 0xff);
    hash *= 1099511628211ull;
  }
  return hash;
}

u32 loopFramesFromLegacyBytes(const VGMSamp& sample, u32 byteOffset) {
  if (sample.dataLength % 9 == 0) {
    return (byteOffset / 9) * 16;
  }
  const auto bytesPerFrame = std::max<int>(1, sample.bytesPerSample() * sample.channels);
  return byteOffset / static_cast<u32>(bytesPerFrame);
}

u32 envelopeMicros(double seconds) {
  if (seconds < 0.0 || !std::isfinite(seconds)) {
    return kEnvelopeInfinite;
  }
  constexpr double microsPerSecond = 1000000.0;
  const double micros = seconds * microsPerSecond;
  if (micros >= static_cast<double>(std::numeric_limits<u32>::max())) {
    return std::numeric_limits<u32>::max();
  }
  return static_cast<u32>(std::lround(std::max(0.0, micros)));
}

u32 envelopePermille(double level) {
  return static_cast<u32>(std::lround(std::clamp(level, 0.0, 1.0) * 1000.0));
}

[[nodiscard]] s32 destinationCode(SynthDestination destination) {
  switch (destination) {
    case SynthDestination::Pitch:
      return 1;
    case SynthDestination::FilterCutoff:
      return 2;
    case SynthDestination::Volume:
      return 3;
    case SynthDestination::Pan:
      return 4;
    case SynthDestination::VibratoDepth:
      return 10;
    case SynthDestination::VibratoRate:
      return 11;
    case SynthDestination::TremoloDepth:
      return 20;
    case SynthDestination::TremoloRate:
      return 21;
    case SynthDestination::Unknown:
      return -1;
  }

  return -1;
}

[[nodiscard]] s32 destinationCode(ModDest destination) {
  switch (destination) {
    case ModDest::VibLfoToPitch:
      return destinationCode(SynthDestination::VibratoDepth);
    case ModDest::VibLfoFreq:
      return destinationCode(SynthDestination::VibratoRate);
    case ModDest::ModLfoToVol:
      return destinationCode(SynthDestination::TremoloDepth);
    case ModDest::ModLfoFreq:
      return destinationCode(SynthDestination::TremoloRate);
    case ModDest::InitialAtten:
      return destinationCode(SynthDestination::Volume);
    case ModDest::VibLfoDelay:
    case ModDest::ModLfoDelay:
      return destinationCode(SynthDestination::Unknown);
  }

  return destinationCode(SynthDestination::Unknown);
}

[[nodiscard]] std::optional<s32> sourceCode(std::optional<SynthSource> source) {
  if (!source) {
    return std::nullopt;
  }

  switch (*source) {
    case SynthSource::NoteOnVelocity:
      return 1;
    case SynthSource::KeyNumber:
      return 2;
    case SynthSource::Lfo:
      return 3;
    case SynthSource::Envelope:
      return 4;
    case SynthSource::MidiController:
      return 1000;
    case SynthSource::ChannelPressure:
      return 128;
    case SynthSource::PolyPressure:
      return 129;
    case SynthSource::PitchWheel:
      return 130;
    case SynthSource::Unknown:
      return -1;
  }

  return -1;
}

[[nodiscard]] std::optional<s32> sourceCode(std::optional<ModSource> source) {
  if (!source) {
    return std::nullopt;
  }

  if (const auto controller = midiControllerForModSource(*source)) {
    return 1000 + *controller;
  }

  switch (*source) {
    case ModSource::None:
      return -1;
    case ModSource::ChannelPressure:
      return 128;
    case ModSource::PolyPressure:
      return 129;
    case ModSource::PitchWheel:
      return 130;
    default:
      return -1;
  }
}

[[nodiscard]] GeneratorSummary summarizeGenerator(const ::SynthGenerator& generator) {
  return GeneratorSummary{
      .destination = destinationCode(generator.destination),
      .amount = generator.amount,
  };
}

[[nodiscard]] GeneratorSummary summarizeGenerator(const vgmtrans::core::SynthGenerator& generator) {
  return GeneratorSummary{
      .destination = destinationCode(generator.destination),
      .amount = generator.amount,
  };
}

[[nodiscard]] ModulatorSummary summarizeModulator(const ::SynthModulator& modulator) {
  return ModulatorSummary{
      .source = sourceCode(modulator.source),
      .destination = destinationCode(modulator.destination),
      .amount = modulator.amount,
  };
}

[[nodiscard]] ModulatorSummary summarizeModulator(const vgmtrans::core::SynthModulator& modulator) {
  return ModulatorSummary{
      .source = sourceCode(modulator.source),
      .destination = destinationCode(modulator.destination),
      .amount = modulator.amount,
  };
}

std::optional<u32> legacyRegionSampleOffset(const VGMRgn& region, std::span<VGMSamp* const> samples) {
  if (region.sampOffset >= 0) {
    const auto baseOffset = region.sampCollPtr ? region.sampCollPtr->offset()
                            : region.parInstr->parInstrSet->sampColl()
                                ? region.parInstr->parInstrSet->sampColl()->offset()
                                : 0;
    const auto sourceOffset = baseOffset + static_cast<u32>(region.sampOffset);
    const auto found = std::ranges::find_if(samples, [sourceOffset](const VGMSamp* sample) {
      return sample != nullptr && sample->dataOff == sourceOffset;
    });
    if (found != samples.end()) {
      return (*found)->dataOff;
    }

    const auto relativeFound = std::ranges::find_if(samples, [&region](const VGMSamp* sample) {
      return sample != nullptr && sample->parSampColl != nullptr &&
             sample->dataOff == sample->parSampColl->offset() + static_cast<u32>(region.sampOffset);
    });
    if (relativeFound != samples.end()) {
      return (*relativeFound)->dataOff;
    }
  }

  if (region.sampNum < samples.size() && samples[region.sampNum] != nullptr) {
    return samples[region.sampNum]->dataOff;
  }
  return std::nullopt;
}

template <typename T>
void appendUnique(std::vector<T*>& items, T* item) {
  if (item != nullptr && std::ranges::find(items, item) == items.end()) {
    items.push_back(item);
  }
}

template <typename T>
void appendUnique(std::vector<T*>& items, std::span<T* const> newItems) {
  for (auto* item : newItems) {
    appendUnique(items, item);
  }
}

void appendLegacySamples(
    CapcomSnesSummary& summary,
    std::vector<VGMSamp*>& samples,
    std::span<VGMSampColl* const> sampleCollections) {
  for (auto* sampleCollection : sampleCollections) {
    if (sampleCollection == nullptr) {
      continue;
    }

    ++summary.sampleCollectionCount;
    for (auto* sample : sampleCollection->samples()) {
      appendUnique(samples, sample);
    }
  }

  for (u32 i = 0; i < samples.size(); ++i) {
    auto* sample = samples[i];
    auto pcm = sample->toPcm(Signedness::Signed, Endianness::Little, BPS::PCM16);
    summary.samples.push_back(SampleSummary{
        .index = i,
        .sourceOffset = sample->dataOff,
        .sourceSize = sample->dataLength,
        .sampleRate = sample->rate,
        .channels = sample->channels,
        .frameCount = static_cast<u32>(pcm.size() / std::max<int>(1, sample->bytesPerSample() * sample->channels)),
        .loopEnabled = sample->loop.loopStatus > 0,
        .loopStart = sample->loop.loopStatus > 0 ? loopFramesFromLegacyBytes(*sample, sample->loop.loopStart) : 0,
        .loopLength = sample->loop.loopStatus > 0 ? loopFramesFromLegacyBytes(*sample, sample->loop.loopLength) : 0,
        .pcmHash = fnv1a(pcm),
    });
  }
}

void appendLegacyInstruments(
    CapcomSnesSummary& summary,
    std::span<VGMInstrSet* const> instrumentBanks,
    std::span<VGMSamp* const> samples) {
  for (const auto* instrumentBank : instrumentBanks) {
    if (instrumentBank == nullptr) {
      continue;
    }

    ++summary.instrumentBankCount;
    for (const auto* instrument : instrumentBank->instrs()) {
      InstrumentSynthSummary synth{
          .bank = instrument->bank,
          .program = instrument->instrNum,
          .sourceOffset = instrument->offset(),
      };
      for (const auto& generator : instrument->generators()) {
        synth.generators.push_back(summarizeGenerator(generator));
      }
      for (const auto& modulator : instrument->modulators()) {
        synth.modulators.push_back(summarizeModulator(modulator));
      }
      summary.instrumentSynths.push_back(std::move(synth));

      for (const auto* region : instrument->regions()) {
        summary.regions.push_back(RegionSummary{
            .bank = instrument->bank,
            .program = instrument->instrNum,
            .sourceOffset = region->offset(),
            .keyLow = region->keyLow,
            .keyHigh = region->keyHigh,
            .velocityLow = region->velLow,
            .velocityHigh = region->velHigh,
            .sampleSourceOffset = legacyRegionSampleOffset(*region, samples).value_or(0),
            .tuningCents = region->unityKey >= 0 ? static_cast<s32>((region->unityKey - 96) * 100 + region->fineTune)
                                                 : region->fineTune,
            .envelopeAttack = envelopeMicros(region->attack_time),
            .envelopeDecay = envelopeMicros(region->decay_time),
            .envelopeSustain = envelopePermille(region->sustain_level),
            .envelopeRelease = envelopeMicros(region->release_time),
        });
      }
    }
  }
}

void normalizeSummary(CapcomSnesSummary& summary) {
  std::ranges::sort(summary.trackCounts);
  std::ranges::sort(summary.samples, {}, &SampleSummary::sourceOffset);
  std::ranges::sort(summary.regions, [](const RegionSummary& lhs, const RegionSummary& rhs) {
    return std::tie(lhs.bank, lhs.program, lhs.sourceOffset, lhs.sampleSourceOffset, lhs.keyLow, lhs.keyHigh,
                    lhs.velocityLow, lhs.velocityHigh, lhs.tuningCents, lhs.envelopeAttack, lhs.envelopeDecay,
                    lhs.envelopeSustain, lhs.envelopeRelease) <
           std::tie(rhs.bank, rhs.program, rhs.sourceOffset, rhs.sampleSourceOffset, rhs.keyLow, rhs.keyHigh,
                    rhs.velocityLow, rhs.velocityHigh, rhs.tuningCents, rhs.envelopeAttack, rhs.envelopeDecay,
                    rhs.envelopeSustain, rhs.envelopeRelease);
  });
  std::ranges::sort(summary.instrumentSynths, [](const InstrumentSynthSummary& lhs,
                                                 const InstrumentSynthSummary& rhs) {
    return std::tie(lhs.bank, lhs.program, lhs.sourceOffset) <
           std::tie(rhs.bank, rhs.program, rhs.sourceOffset);
  });
  for (u32 i = 0; i < summary.samples.size(); ++i) {
    summary.samples[i].index = i;
  }
}

CapcomSnesSummary legacyCapcomSnesCollectionSummary(const VGMColl& collection) {
  CapcomSnesSummary summary;

  if (const auto* sequence = collection.seq()) {
    ++summary.sequenceCount;
    summary.trackCounts.push_back(static_cast<u32>(sequence->trackCount()));
  }

  std::vector<VGMInstrSet*> instrumentBanks;
  appendUnique(instrumentBanks, collection.instrSets());

  std::vector<VGMSampColl*> sampleCollections;
  appendUnique(sampleCollections, collection.sampColls());
  for (const auto* instrumentBank : instrumentBanks) {
    appendUnique(sampleCollections, instrumentBank->sampColl());
  }

  std::vector<VGMSamp*> samples;
  appendLegacySamples(summary, samples, sampleCollections);
  appendLegacyInstruments(summary, instrumentBanks, samples);
  normalizeSummary(summary);

  return summary;
}

CapcomSnesSummary legacyCapcomSnesSummary(std::span<const u8> aramBytes, const std::string& name) {
  const auto root = scanLegacyCapcomSnes(aramBytes, name);

  CapcomSnesSummary summary;
  std::vector<VGMInstrSet*> instrumentBanks;
  std::vector<VGMSampColl*> sampleCollections;

  for (const auto& file : root->vgmFiles()) {
    if (const auto* sequenceSlot = std::get_if<VGMSeq*>(&file); sequenceSlot != nullptr && *sequenceSlot != nullptr) {
      ++summary.sequenceCount;
      summary.trackCounts.push_back(static_cast<u32>((*sequenceSlot)->trackCount()));
    } else if (const auto* sampleSlot = std::get_if<VGMSampColl*>(&file);
               sampleSlot != nullptr && *sampleSlot != nullptr) {
      appendUnique(sampleCollections, *sampleSlot);
    } else if (const auto* instrumentSlot = std::get_if<VGMInstrSet*>(&file);
               instrumentSlot != nullptr && *instrumentSlot != nullptr) {
      appendUnique(instrumentBanks, *instrumentSlot);
    }
  }

  for (const auto* instrumentBank : instrumentBanks) {
    appendUnique(sampleCollections, instrumentBank->sampColl());
  }

  std::vector<VGMSamp*> samples;
  appendLegacySamples(summary, samples, sampleCollections);
  appendLegacyInstruments(summary, instrumentBanks, samples);
  normalizeSummary(summary);
  return summary;
}

std::map<std::string, CapcomSnesSummary> legacyCapcomSnesRsnSummaries(const std::filesystem::path& path) {
  const auto root = scanLegacyFile(path);
  std::map<std::string, CapcomSnesSummary> summaries;

  for (const auto* collection : root->vgmColls()) {
    if (collection == nullptr || collection->seq() == nullptr) {
      continue;
    }

    auto [_, inserted] = summaries.emplace(collection->name(), legacyCapcomSnesCollectionSummary(*collection));
    if (!inserted) {
      throw std::runtime_error("duplicate legacy collection name from RSN: " + collection->name());
    }
  }

  if (summaries.empty()) {
    throw std::runtime_error("legacy scanner did not discover collections in: " + path.string());
  }
  return summaries;
}

CapcomSnesSummary valueCapcomSnesSummary(
    const Project& project,
    const SourceStore& sources,
    const Collection& collection) {
  const auto decoders = SampleDecoderRegistry::withDefaultDecoders();

  CapcomSnesSummary summary;
  std::map<u32, const SampleCollectionAsset*> sampleCollectionsById;

  if (collection.sequence) {
    if (const auto* sequence = assetById<SequenceAsset>(project, *collection.sequence)) {
      ++summary.sequenceCount;
      summary.trackCounts.push_back(static_cast<u32>(sequence->program.tracks.size()));
    }
  }

  for (const auto sampleCollectionId : collection.sampleCollections) {
    const auto* sampleCollection = assetById<SampleCollectionAsset>(project, sampleCollectionId);
    if (sampleCollection == nullptr) {
      continue;
    }

    ++summary.sampleCollectionCount;
    sampleCollectionsById[sampleCollection->metadata.id.value] = sampleCollection;
    for (u32 i = 0; i < sampleCollection->samples.samples.size(); ++i) {
      const auto& sample = sampleCollection->samples.samples[i];
      const auto decoded = decoders.decode(sample, sources.bytes(sample.encodedData.source));
      expect(decoded.has_value(), "value sample summary expected decodable sample");
      summary.samples.push_back(SampleSummary{
          .index = i,
          .sourceOffset = static_cast<u32>(sample.encodedData.offset),
          .sourceSize = static_cast<u32>(sample.encodedData.size),
          .sampleRate = decoded->sampleRate,
          .channels = decoded->channels,
          .frameCount = static_cast<u32>(decoded->pcm.size() / std::max<u8>(1, decoded->channels)),
          .loopEnabled = decoded->loop.enabled,
          .loopStart = decoded->loop.enabled ? decoded->loop.start : 0,
          .loopLength = decoded->loop.enabled ? decoded->loop.length : 0,
          .pcmHash = fnv1aPcm16(decoded->pcm),
      });
    }
  }

  for (const auto instrumentBankId : collection.instrumentBanks) {
    const auto* instrumentBank = assetById<InstrumentBankAsset>(project, instrumentBankId);
    if (instrumentBank == nullptr) {
      continue;
    }

    ++summary.instrumentBankCount;
    for (const auto& instrument : instrumentBank->bank.instruments) {
      InstrumentSynthSummary synth{
          .bank = instrument.bank,
          .program = instrument.program,
          .sourceOffset = static_cast<u32>(instrument.range.offset),
      };
      for (const auto& generator : instrument.generators) {
        synth.generators.push_back(summarizeGenerator(generator));
      }
      for (const auto& modulator : instrument.modulators) {
        synth.modulators.push_back(summarizeModulator(modulator));
      }
      summary.instrumentSynths.push_back(std::move(synth));

      for (const auto& region : instrument.regions) {
        u32 sampleSourceOffset = 0;
        if (region.sample.collection) {
          const auto sampleCollection = sampleCollectionsById.find(region.sample.collection->value);
          if (sampleCollection != sampleCollectionsById.end() &&
              region.sample.index < sampleCollection->second->samples.samples.size()) {
            sampleSourceOffset =
                static_cast<u32>(sampleCollection->second->samples.samples[region.sample.index].encodedData.offset);
          }
        }

        summary.regions.push_back(RegionSummary{
            .bank = instrument.bank,
            .program = instrument.program,
            .sourceOffset = static_cast<u32>(region.range.offset),
            .keyLow = region.keyRange.low,
            .keyHigh = region.keyRange.high,
            .velocityLow = region.velocityRange.low,
            .velocityHigh = region.velocityRange.high,
            .sampleSourceOffset = sampleSourceOffset,
            .tuningCents = region.tuning.cents,
            .envelopeAttack = region.envelope.attack,
            .envelopeDecay = region.envelope.decay,
            .envelopeSustain = region.envelope.sustain,
            .envelopeRelease = region.envelope.release,
        });
      }
    }
  }

  normalizeSummary(summary);
  return summary;
}

CapcomSnesSummary valueCapcomSnesSummary(std::vector<u8> aramBytes, const std::string& name) {
  ProjectSession session;
  vgmtrans::formats::registerValueFormats(session);
  session.addSource(SourceFile{.name = name}, std::move(aramBytes));

  const Project project = session.scan();
  expect(project.collections.size() == 1, "value ARAM summary expected one collection");
  return valueCapcomSnesSummary(project, session.sources(), project.collections.front());
}

std::map<std::string, CapcomSnesSummary> valueCapcomSnesRsnSummaries(const std::filesystem::path& path) {
  ProjectSession session;
  vgmtrans::formats::registerValueFormats(session);
  session.addSource(SourceFile{.name = path.filename().string(), .path = path}, readFile(path));

  const Project project = session.scan();
  if (project.collections.empty()) {
    std::ostringstream message;
    message << "value scanner did not discover collections from RSN";
    if (!project.diagnostics.empty()) {
      message << ": " << project.diagnostics.front().message;
    }
    throw std::runtime_error(message.str());
  }

  std::map<std::string, CapcomSnesSummary> summaries;
  for (const auto& collection : project.collections) {
    auto [_, inserted] =
        summaries.emplace(collection.name, valueCapcomSnesSummary(project, session.sources(), collection));
    if (!inserted) {
      throw std::runtime_error("duplicate value collection name from RSN: " + collection.name);
    }
  }
  return summaries;
}

std::string describeSample(const SampleSummary& sample) {
  std::ostringstream out;
  out << "sample offset=0x" << std::hex << sample.sourceOffset << std::dec << " size=" << sample.sourceSize
      << " rate=" << sample.sampleRate << " channels=" << static_cast<int>(sample.channels)
      << " frames=" << sample.frameCount << " loop=" << sample.loopEnabled << " loopStart=" << sample.loopStart
      << " loopLength=" << sample.loopLength << " hash=0x" << std::hex << sample.pcmHash;
  return out.str();
}

std::string describeRegion(const RegionSummary& region) {
  std::ostringstream out;
  out << "region bank=" << region.bank << " program=" << region.program << " offset=0x" << std::hex
      << region.sourceOffset << " sampleOffset=0x" << region.sampleSourceOffset << std::dec
      << " key=" << static_cast<int>(region.keyLow) << "-" << static_cast<int>(region.keyHigh)
      << " vel=" << static_cast<int>(region.velocityLow) << "-" << static_cast<int>(region.velocityHigh)
      << " tuning=" << region.tuningCents << " envelope={attack=" << region.envelopeAttack
      << ", decay=" << region.envelopeDecay << ", sustain=" << region.envelopeSustain
      << ", release=" << region.envelopeRelease << "}";
  return out.str();
}

std::string describeGenerator(const GeneratorSummary& generator) {
  std::ostringstream out;
  out << "{destination=" << generator.destination << ", amount=" << generator.amount << "}";
  return out.str();
}

std::string describeModulator(const ModulatorSummary& modulator) {
  std::ostringstream out;
  out << "{source=";
  if (modulator.source) {
    out << *modulator.source;
  } else {
    out << "default";
  }
  out << ", destination=" << modulator.destination << ", amount=" << modulator.amount << "}";
  return out.str();
}

std::string describeInstrumentSynth(const InstrumentSynthSummary& synth) {
  std::ostringstream out;
  out << "instrument synth bank=" << synth.bank << " program=" << synth.program << " offset=0x" << std::hex
      << synth.sourceOffset << std::dec << " generators=[";
  for (size_t i = 0; i < synth.generators.size(); ++i) {
    if (i != 0) {
      out << ", ";
    }
    out << describeGenerator(synth.generators[i]);
  }
  out << "] modulators=[";
  for (size_t i = 0; i < synth.modulators.size(); ++i) {
    if (i != 0) {
      out << ", ";
    }
    out << describeModulator(synth.modulators[i]);
  }
  out << "]";
  return out.str();
}

bool compareSummary(const CapcomSnesSummary& legacy, const CapcomSnesSummary& value, std::ostream& out) {
  if (legacy == value) {
    out << "CapcomSnes summary parity ok: sequences=" << legacy.sequenceCount
        << " instruments=" << legacy.regions.size() << " synths=" << legacy.instrumentSynths.size()
        << " samples=" << legacy.samples.size() << "\n";
    return true;
  }

  out << "CapcomSnes summary parity mismatch\n";
  out << "legacy counts: sequences=" << legacy.sequenceCount << " trackCounts=" << legacy.trackCounts.size()
      << " instrumentBanks=" << legacy.instrumentBankCount << " sampleCollections=" << legacy.sampleCollectionCount
      << " regions=" << legacy.regions.size() << " synths=" << legacy.instrumentSynths.size()
      << " samples=" << legacy.samples.size() << "\n";
  out << "value counts:  sequences=" << value.sequenceCount << " trackCounts=" << value.trackCounts.size()
      << " instrumentBanks=" << value.instrumentBankCount << " sampleCollections=" << value.sampleCollectionCount
      << " regions=" << value.regions.size() << " synths=" << value.instrumentSynths.size()
      << " samples=" << value.samples.size() << "\n";

  if (legacy.trackCounts != value.trackCounts) {
    out << "track count vectors differ\n";
    return false;
  }

  const size_t sharedSamples = std::min(legacy.samples.size(), value.samples.size());
  for (size_t i = 0; i < sharedSamples; ++i) {
    if (!(legacy.samples[i] == value.samples[i])) {
      out << "first sample mismatch at " << i << "\n";
      out << "legacy: " << describeSample(legacy.samples[i]) << "\n";
      out << "value:  " << describeSample(value.samples[i]) << "\n";
      return false;
    }
  }
  if (legacy.samples.size() != value.samples.size()) {
    out << "sample count differs\n";
    return false;
  }

  const size_t sharedRegions = std::min(legacy.regions.size(), value.regions.size());
  for (size_t i = 0; i < sharedRegions; ++i) {
    if (!(legacy.regions[i] == value.regions[i])) {
      out << "first region mismatch at " << i << "\n";
      out << "legacy: " << describeRegion(legacy.regions[i]) << "\n";
      out << "value:  " << describeRegion(value.regions[i]) << "\n";
      return false;
    }
  }
  if (legacy.regions.size() != value.regions.size()) {
    out << "region count differs\n";
    return false;
  }

  const size_t sharedSynths = std::min(legacy.instrumentSynths.size(), value.instrumentSynths.size());
  for (size_t i = 0; i < sharedSynths; ++i) {
    if (!(legacy.instrumentSynths[i] == value.instrumentSynths[i])) {
      out << "first instrument synth mismatch at " << i << "\n";
      out << "legacy: " << describeInstrumentSynth(legacy.instrumentSynths[i]) << "\n";
      out << "value:  " << describeInstrumentSynth(value.instrumentSynths[i]) << "\n";
      return false;
    }
  }
  if (legacy.instrumentSynths.size() != value.instrumentSynths.size()) {
    out << "instrument synth count differs\n";
    return false;
  }

  return false;
}

bool compareCapcomSnesSummary(std::span<const u8> aramBytes, const std::string& name, std::ostream& out) {
  const auto legacy = legacyCapcomSnesSummary(aramBytes, name);
  const auto value = valueCapcomSnesSummary(std::vector<u8>(aramBytes.begin(), aramBytes.end()), name);
  return compareSummary(legacy, value, out);
}

std::vector<u8> valueCapcomSnesMidi(std::vector<u8> aramBytes, const std::string& name) {
  ProjectSession session;
  vgmtrans::formats::registerValueFormats(session);
  session.addSource(SourceFile{.name = name}, std::move(aramBytes));

  const Project project = session.scan();
  if (project.collections.empty()) {
    std::ostringstream message;
    message << "value scanner did not discover a collection";
    if (!project.diagnostics.empty()) {
      message << ": " << project.diagnostics.front().message;
    }
    throw std::runtime_error(message.str());
  }

  const auto artifacts =
      session.exportCollection(project.collections.front().id, ExportRequest{
                                                                   .kinds = {ExportKind::Midi},
                                                                   .loopPolicy = LoopPolicy::PlayOnce,
                                                               });

  for (const auto& artifact : artifacts) {
    if (artifact.mediaType == "audio/midi") {
      if (!artifact.diagnostics.empty()) {
        throw std::runtime_error("value MIDI export reported: " + artifact.diagnostics.front().message);
      }
      return artifact.bytes;
    }
  }

  throw std::runtime_error("value exporter did not produce a MIDI artifact");
}

std::vector<u8> valueCapcomSnesMidi(ProjectSession& session, CollectionId collection) {
  const auto artifacts =
      session.exportCollection(collection, ExportRequest{
                                             .kinds = {ExportKind::Midi},
                                             .loopPolicy = LoopPolicy::PlayOnce,
                                         });

  for (const auto& artifact : artifacts) {
    if (artifact.mediaType == "audio/midi") {
      if (!artifact.diagnostics.empty()) {
        throw std::runtime_error("value MIDI export reported: " + artifact.diagnostics.front().message);
      }
      return artifact.bytes;
    }
  }

  throw std::runtime_error("value exporter did not produce a MIDI artifact");
}

std::map<std::string, std::vector<u8>> valueCapcomSnesRsnMidis(const std::filesystem::path& path) {
  ProjectSession session;
  vgmtrans::formats::registerValueFormats(session);
  session.addSource(SourceFile{.name = path.filename().string(), .path = path}, readFile(path));

  const Project project = session.scan();
  if (project.collections.empty()) {
    std::ostringstream message;
    message << "value scanner did not discover collections from RSN";
    if (!project.diagnostics.empty()) {
      message << ": " << project.diagnostics.front().message;
    }
    throw std::runtime_error(message.str());
  }

  std::map<std::string, std::vector<u8>> midis;
  for (const auto& collection : project.collections) {
    auto [_, inserted] = midis.emplace(collection.name, valueCapcomSnesMidi(session, collection.id));
    if (!inserted) {
      throw std::runtime_error("duplicate value collection name from RSN: " + collection.name);
    }
  }
  return midis;
}

class MidiReader {
public:
  explicit MidiReader(std::span<const u8> bytes) : bytes_(bytes) {}

  [[nodiscard]] bool empty() const noexcept { return position_ >= bytes_.size(); }
  [[nodiscard]] size_t position() const noexcept { return position_; }

  void require(size_t count, size_t limit) const {
    if (position_ > limit || count > limit - position_) {
      throw std::runtime_error("truncated MIDI data");
    }
  }

  u8 readU8(size_t limit) {
    require(1, limit);
    return bytes_[position_++];
  }

  u16 be16(size_t limit) {
    const u16 hi = readU8(limit);
    const u16 lo = readU8(limit);
    return static_cast<u16>((hi << 8) | lo);
  }

  u32 be32(size_t limit) {
    const u32 b0 = readU8(limit);
    const u32 b1 = readU8(limit);
    const u32 b2 = readU8(limit);
    const u32 b3 = readU8(limit);
    return (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;
  }

  u64 variableLength(size_t limit) {
    u64 value = 0;
    for (int i = 0; i < 4; ++i) {
      const u8 byte = readU8(limit);
      value = (value << 7) | (byte & 0x7f);
      if ((byte & 0x80) == 0) {
        return value;
      }
    }
    throw std::runtime_error("invalid MIDI variable-length quantity");
  }

  std::string ascii(size_t count, size_t limit) {
    require(count, limit);
    const auto begin = reinterpret_cast<const char*>(bytes_.data() + position_);
    position_ += count;
    return std::string(begin, count);
  }

  std::vector<u8> bytes(size_t count, size_t limit) {
    require(count, limit);
    std::vector<u8> result(bytes_.begin() + static_cast<std::ptrdiff_t>(position_),
                           bytes_.begin() + static_cast<std::ptrdiff_t>(position_ + count));
    position_ += count;
    return result;
  }

  void skip(size_t count, size_t limit) {
    require(count, limit);
    position_ += count;
  }

private:
  std::span<const u8> bytes_;
  size_t position_ = 0;
};

struct NormalizedMidiEvent {
  u32 track = 0;
  u64 tick = 0;
  std::string kind;
  u8 channel = 0;
  u32 a = 0;
  u32 b = 0;
  u32 c = 0;
  std::string text;

  friend bool operator==(const NormalizedMidiEvent&, const NormalizedMidiEvent&) = default;
};

struct ActiveNote {
  u64 tick = 0;
  u8 velocity = 0;
};

using ActiveNoteKey = std::tuple<u32, u8, u8>;

std::string hexByte(u8 value) {
  constexpr char digits[] = "0123456789abcdef";
  std::string text = "0x00";
  text[2] = digits[(value >> 4) & 0x0f];
  text[3] = digits[value & 0x0f];
  return text;
}

std::string describeEvent(const NormalizedMidiEvent& event) {
  std::ostringstream out;
  out << "track=" << event.track << " tick=" << event.tick << " kind=" << event.kind;
  if (!event.kind.empty()) {
    out << " channel=" << static_cast<int>(event.channel) << " a=" << event.a << " b=" << event.b << " c=" << event.c;
  }
  if (!event.text.empty()) {
    out << " text=\"" << event.text << "\"";
  }
  return out.str();
}

void addEvent(std::vector<NormalizedMidiEvent>& events, NormalizedMidiEvent event) {
  events.push_back(std::move(event));
}

void addNoteOff(std::vector<NormalizedMidiEvent>& events, std::map<ActiveNoteKey, std::vector<ActiveNote>>& activeNotes,
                u32 track, u64 tick, u8 channel, u8 key, u8 releaseVelocity) {
  const ActiveNoteKey activeKey{track, channel, key};
  auto active = activeNotes.find(activeKey);
  if (active == activeNotes.end() || active->second.empty()) {
    addEvent(events, NormalizedMidiEvent{
                         .track = track,
                         .tick = tick,
                         .kind = "note-off",
                         .channel = channel,
                         .a = key,
                         .b = releaseVelocity,
                     });
    return;
  }

  const ActiveNote note = active->second.front();
  active->second.erase(active->second.begin());
  addEvent(events, NormalizedMidiEvent{
                       .track = track,
                       .tick = note.tick,
                       .kind = "note",
                       .channel = channel,
                       .a = key,
                       .b = note.velocity,
                       .c = static_cast<u32>(tick - note.tick),
                   });
}

void finishActiveNotes(std::vector<NormalizedMidiEvent>& events,
                       const std::map<ActiveNoteKey, std::vector<ActiveNote>>& activeNotes) {
  for (const auto& [key, notes] : activeNotes) {
    const auto [track, channel, midiKey] = key;
    for (const auto& note : notes) {
      addEvent(events, NormalizedMidiEvent{
                           .track = track,
                           .tick = note.tick,
                           .kind = "note-on",
                           .channel = channel,
                           .a = midiKey,
                           .b = note.velocity,
                       });
    }
  }
}

void addMetaEvent(std::vector<NormalizedMidiEvent>& events, u32 track, u64 tick, u8 type, std::vector<u8> payload) {
  if (type == 0x2f) {
    return;
  }

  if (type == 0x03) {
    return;
  }

  if (type == 0x51 && payload.size() == 3) {
    const u32 tempo =
        (static_cast<u32>(payload[0]) << 16) | (static_cast<u32>(payload[1]) << 8) | static_cast<u32>(payload[2]);
    addEvent(events, NormalizedMidiEvent{
                         .track = 0,
                         .tick = tick,
                         .kind = "tempo",
                         .a = tempo,
                     });
    return;
  }

  if (type >= 0x01 && type <= 0x07) {
    addEvent(events, NormalizedMidiEvent{
                         .track = track,
                         .tick = tick,
                         .kind = "meta-text",
                         .a = type,
                         .text = std::string(payload.begin(), payload.end()),
                     });
    return;
  }

  if (type == 0x21) {
    return;
  }

  addEvent(events, NormalizedMidiEvent{
                       .track = track,
                       .tick = tick,
                       .kind = "meta",
                       .a = type,
                       .b = static_cast<u32>(payload.size()),
                   });
}

std::vector<NormalizedMidiEvent> normalizeMidi(std::span<const u8> bytes) {
  MidiReader reader(bytes);
  expect(reader.ascii(4, bytes.size()) == "MThd", "MIDI missing MThd header");
  const u32 headerLength = reader.be32(bytes.size());
  expect(headerLength >= 6, "MIDI header is too short");
  const size_t headerEnd = reader.position() + headerLength;
  expect(headerEnd <= bytes.size(), "MIDI header extends past end of file");
  static_cast<void>(reader.be16(headerEnd));
  const u16 trackCount = reader.be16(headerEnd);
  static_cast<void>(reader.be16(headerEnd));
  reader.skip(headerEnd - reader.position(), bytes.size());

  std::vector<NormalizedMidiEvent> events;

  for (u32 track = 0; track < trackCount; ++track) {
    expect(reader.ascii(4, bytes.size()) == "MTrk", "MIDI missing MTrk header");
    const u32 trackLength = reader.be32(bytes.size());
    const size_t trackEnd = reader.position() + trackLength;
    expect(trackEnd <= bytes.size(), "MIDI track extends past end of file");

    u64 tick = 0;
    std::optional<u8> runningStatus;
    std::map<ActiveNoteKey, std::vector<ActiveNote>> activeNotes;

    while (reader.position() < trackEnd) {
      tick += reader.variableLength(trackEnd);
      u8 status = reader.readU8(trackEnd);
      std::optional<u8> firstDataByte;
      if (status < 0x80) {
        if (!runningStatus.has_value()) {
          throw std::runtime_error("MIDI running status used before status byte");
        }
        firstDataByte = status;
        status = *runningStatus;
      } else if (status < 0xf0) {
        runningStatus = status;
      } else {
        runningStatus.reset();
      }

      if (status == 0xff) {
        const u8 type = reader.readU8(trackEnd);
        const u64 length = reader.variableLength(trackEnd);
        if (length > std::numeric_limits<size_t>::max()) {
          throw std::runtime_error("MIDI meta event is too large");
        }
        addMetaEvent(events, track, tick, type, reader.bytes(static_cast<size_t>(length), trackEnd));
        continue;
      }

      if (status == 0xf0 || status == 0xf7) {
        const u64 length = reader.variableLength(trackEnd);
        if (length > std::numeric_limits<size_t>::max()) {
          throw std::runtime_error("MIDI sysex event is too large");
        }
        const auto payload = reader.bytes(static_cast<size_t>(length), trackEnd);
        if (tick == 0 && status == 0xf0 && payload.size() == 5 && payload[0] == 0x7e && payload[1] == 0x7f &&
            payload[2] == 0x09 && (payload[3] == 0x01 || payload[3] == 0x03) && payload[4] == 0xf7) {
          continue;
        }
        addEvent(events, NormalizedMidiEvent{
                             .track = track,
                             .tick = tick,
                             .kind = "sysex",
                             .a = status,
                             .b = static_cast<u32>(length),
                         });
        continue;
      }

      if (status >= 0xf0) {
        throw std::runtime_error("unsupported MIDI system event: " + hexByte(status));
      }

      const u8 command = static_cast<u8>(status & 0xf0);
      const u8 channel = static_cast<u8>(status & 0x0f);
      const bool oneDataByte = command == 0xc0 || command == 0xd0;
      const u8 data1 = firstDataByte.value_or(reader.readU8(trackEnd));
      const u8 data2 = oneDataByte ? 0 : reader.readU8(trackEnd);

      if (command == 0x80) {
        addNoteOff(events, activeNotes, track, tick, channel, data1, data2);
      } else if (command == 0x90) {
        if (data2 == 0) {
          addNoteOff(events, activeNotes, track, tick, channel, data1, data2);
        } else {
          activeNotes[{track, channel, data1}].push_back(ActiveNote{.tick = tick, .velocity = data2});
        }
      } else if (command == 0xb0) {
        addEvent(events, NormalizedMidiEvent{
                             .track = track,
                             .tick = tick,
                             .kind = "control",
                             .channel = channel,
                             .a = data1,
                             .b = data2,
                         });
      } else if (command == 0xc0) {
        addEvent(events, NormalizedMidiEvent{
                             .track = track,
                             .tick = tick,
                             .kind = "program",
                             .channel = channel,
                             .a = data1,
                         });
      } else if (command == 0xd0) {
        addEvent(events, NormalizedMidiEvent{
                             .track = track,
                             .tick = tick,
                             .kind = "channel-pressure",
                             .channel = channel,
                             .a = data1,
                         });
      } else if (command == 0xe0) {
        addEvent(events, NormalizedMidiEvent{
                             .track = track,
                             .tick = tick,
                             .kind = "pitch-bend",
                             .channel = channel,
                             .a = static_cast<u32>(data1 | (data2 << 7)),
                         });
      } else {
        addEvent(events, NormalizedMidiEvent{
                             .track = track,
                             .tick = tick,
                             .kind = "channel-event",
                             .channel = channel,
                             .a = command,
                             .b = data1,
                             .c = data2,
                         });
      }
    }

    finishActiveNotes(events, activeNotes);
    reader.skip(trackEnd - reader.position(), bytes.size());
  }

  expect(reader.empty(), "MIDI has trailing bytes after declared tracks");

  std::ranges::sort(events, [](const auto& lhs, const auto& rhs) {
    return std::tie(lhs.track, lhs.tick, lhs.kind, lhs.channel, lhs.a, lhs.b, lhs.c, lhs.text) <
           std::tie(rhs.track, rhs.tick, rhs.kind, rhs.channel, rhs.a, rhs.b, rhs.c, rhs.text);
  });
  return events;
}

bool compareMidi(std::span<const u8> legacyBytes, std::span<const u8> valueBytes, std::ostream& out) {
  const auto legacy = normalizeMidi(legacyBytes);
  const auto value = normalizeMidi(valueBytes);
  if (legacy == value) {
    out << "MIDI parity ok: " << legacy.size() << " normalized events\n";
    return true;
  }

  out << "MIDI parity mismatch\n";
  out << "legacy events: " << legacy.size() << "\n";
  out << "value events: " << value.size() << "\n";

  const size_t shared = std::min(legacy.size(), value.size());
  for (size_t i = 0; i < shared; ++i) {
    if (!(legacy[i] == value[i])) {
      out << "first mismatch at normalized event " << i << "\n";
      out << "legacy: " << describeEvent(legacy[i]) << "\n";
      out << "value:  " << describeEvent(value[i]) << "\n";
      const size_t begin = i > 3 ? i - 3 : 0;
      const size_t legacyEnd = std::min(legacy.size(), i + 4);
      const size_t valueEnd = std::min(value.size(), i + 4);
      out << "legacy context:\n";
      for (size_t context = begin; context < legacyEnd; ++context) {
        out << "  [" << context << "] " << describeEvent(legacy[context]) << "\n";
      }
      out << "value context:\n";
      for (size_t context = begin; context < valueEnd; ++context) {
        out << "  [" << context << "] " << describeEvent(value[context]) << "\n";
      }
      return false;
    }
  }

  if (legacy.size() > shared) {
    out << "first extra legacy event: " << describeEvent(legacy[shared]) << "\n";
  } else if (value.size() > shared) {
    out << "first extra value event: " << describeEvent(value[shared]) << "\n";
  }
  return false;
}

int selfTest() {
  const PerformanceSequence performance{
      .timebase = Timebase{.ppqn = 48},
      .tracks = {PerformanceTrack{
          .name = "Parity",
          .events =
              {
                  Tempo{.tick = 0, .microsecondsPerQuarter = 500000},
                  ProgramChange{.tick = 0, .channel = 2, .program = 12},
                  Volume{.tick = 0, .channel = 2, .value = 80},
                  Pan{.tick = 12, .channel = 2, .value = 32},
                  NoteDuration{.tick = 24, .channel = 2, .key = 64, .velocity = 100, .duration = 36},
                  EndOfTrack{.tick = 60},
              },
      }},
  };

  const auto midi = MidiExporter().exportMidi(performance);
  const auto normalized = normalizeMidi(midi);

  expect(
      std::ranges::any_of(
          normalized, [](const auto& event) { return event.kind == "tempo" && event.tick == 0 && event.a == 500000; }),
      "self-test should normalize tempo events");
  expect(
      std::ranges::any_of(
          normalized, [](const auto& event) { return event.kind == "program" && event.channel == 2 && event.a == 12; }),
      "self-test should normalize program changes");
  expect(std::ranges::any_of(normalized,
                             [](const auto& event) {
                               return event.kind == "note" && event.tick == 24 && event.channel == 2 && event.a == 64 &&
                                      event.b == 100 && event.c == 36;
                             }),
         "self-test should pair note durations");

  std::ostringstream parityOutput;
  expect(compareMidi(midi, midi, parityOutput), "self-test should compare identical MIDI");

  const auto aramBytes = makeCapcomSnesAram();
  std::ostringstream summaryOutput;
  expect(compareCapcomSnesSummary(aramBytes, "synthetic.spc", summaryOutput),
         "self-test should compare CapcomSnes summary parity: " + summaryOutput.str());

  std::cout << "vgmtrans-parity self-test ok\n";
  return 0;
}

int compareCapcomSnesAramMidi(const std::filesystem::path& path) {
  const auto aramBytes = readFile(path);
  const std::string name = path.filename().string();
  const auto legacyMidi = legacyCapcomSnesMidi(aramBytes, name);
  const auto valueMidi = valueCapcomSnesMidi(aramBytes, name);
  return compareMidi(legacyMidi, valueMidi, std::cout) ? 0 : 1;
}

int compareCapcomSnesRsnMidi(const std::filesystem::path& path) {
  const auto arams = legacyExtractedArams(path);
  if (arams.empty()) {
    throw std::runtime_error("legacy loader did not extract any 64 KiB ARAM files from: " + path.string());
  }

  for (const auto& aram : arams) {
    std::cout << "checking " << aram.name << "\n";
    const auto legacyMidi = legacyCapcomSnesMidi(aram.bytes, aram.name);
    const auto valueMidi = valueCapcomSnesMidi(aram.bytes, aram.name);
    if (!compareMidi(legacyMidi, valueMidi, std::cout)) {
      return 1;
    }
  }

  std::cout << "CapcomSnes RSN MIDI parity ok: files=" << arams.size() << "\n";
  return 0;
}

int compareCapcomSnesRsnDirectMidi(const std::filesystem::path& path) {
  const auto legacyMidis = legacyCapcomSnesRsnMidis(path);
  const auto valueMidis = valueCapcomSnesRsnMidis(path);
  if (valueMidis.size() != legacyMidis.size()) {
    std::cout << "value RSN collection count differs: legacy=" << legacyMidis.size()
              << " value=" << valueMidis.size() << "\n";
    return 1;
  }

  for (const auto& [collectionName, legacyMidi] : legacyMidis) {
    const auto found = valueMidis.find(collectionName);
    if (found == valueMidis.end()) {
      std::cout << "value RSN scan did not produce collection '" << collectionName << "'\n";
      return 1;
    }

    std::cout << "checking " << collectionName << " via direct RSN value scan\n";
    if (!compareMidi(legacyMidi, found->second, std::cout)) {
      return 1;
    }
  }

  std::cout << "CapcomSnes direct RSN MIDI parity ok: collections=" << legacyMidis.size() << "\n";
  return 0;
}

int compareCapcomSnesRsnDirectSummary(const std::filesystem::path& path) {
  const auto legacySummaries = legacyCapcomSnesRsnSummaries(path);
  const auto valueSummaries = valueCapcomSnesRsnSummaries(path);
  if (valueSummaries.size() != legacySummaries.size()) {
    std::cout << "value RSN collection count differs: legacy=" << legacySummaries.size()
              << " value=" << valueSummaries.size() << "\n";
    return 1;
  }

  for (const auto& [collectionName, legacySummary] : legacySummaries) {
    const auto found = valueSummaries.find(collectionName);
    if (found == valueSummaries.end()) {
      std::cout << "value RSN scan did not produce collection '" << collectionName << "'\n";
      return 1;
    }

    std::cout << "checking " << collectionName << " via direct RSN value summary\n";
    if (!compareSummary(legacySummary, found->second, std::cout)) {
      return 1;
    }
  }

  std::cout << "CapcomSnes direct RSN summary parity ok: collections=" << legacySummaries.size() << "\n";
  return 0;
}

int compareCapcomSnesAramSummary(const std::filesystem::path& path) {
  const auto aramBytes = readFile(path);
  return compareCapcomSnesSummary(aramBytes, path.filename().string(), std::cout) ? 0 : 1;
}

int compareCapcomSnesRsnSummary(const std::filesystem::path& path) {
  const auto arams = legacyExtractedArams(path);
  if (arams.empty()) {
    throw std::runtime_error("legacy loader did not extract any 64 KiB ARAM files from: " + path.string());
  }

  for (const auto& aram : arams) {
    std::cout << "checking " << aram.name << "\n";
    if (!compareCapcomSnesSummary(aram.bytes, aram.name, std::cout)) {
      return 1;
    }
  }

  std::cout << "CapcomSnes RSN summary parity ok: files=" << arams.size() << "\n";
  return 0;
}

void printUsage(std::ostream& out) {
  out << "usage:\n"
      << "  vgmtrans-parity --self-test\n"
      << "  vgmtrans-parity capcom-snes-aram-midi <raw-aram-file>\n"
      << "  vgmtrans-parity capcom-snes-aram-summary <raw-aram-file>\n"
      << "  vgmtrans-parity capcom-snes-rsn-midi <rsn-file>\n"
      << "  vgmtrans-parity capcom-snes-rsn-direct-midi <rsn-file>\n"
      << "  vgmtrans-parity capcom-snes-rsn-direct-summary <rsn-file>\n"
      << "  vgmtrans-parity capcom-snes-rsn-summary <rsn-file>\n";
}

}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc == 2 && std::string(argv[1]) == "--self-test") {
      return selfTest();
    }

    if (argc == 3 && std::string(argv[1]) == "capcom-snes-aram-midi") {
      return compareCapcomSnesAramMidi(argv[2]);
    }

    if (argc == 3 && std::string(argv[1]) == "capcom-snes-rsn-midi") {
      return compareCapcomSnesRsnMidi(argv[2]);
    }

    if (argc == 3 && std::string(argv[1]) == "capcom-snes-rsn-direct-midi") {
      return compareCapcomSnesRsnDirectMidi(argv[2]);
    }

    if (argc == 3 && std::string(argv[1]) == "capcom-snes-rsn-direct-summary") {
      return compareCapcomSnesRsnDirectSummary(argv[2]);
    }

    if (argc == 3 && std::string(argv[1]) == "capcom-snes-aram-summary") {
      return compareCapcomSnesAramSummary(argv[2]);
    }

    if (argc == 3 && std::string(argv[1]) == "capcom-snes-rsn-summary") {
      return compareCapcomSnesRsnSummary(argv[2]);
    }

    printUsage(std::cerr);
    return 2;
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }
}
