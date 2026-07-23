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
#include "ConversionContext.h"
#include "conversion/DLSConversion.h"
#include "conversion/DLSFile.h"
#include "conversion/MidiFile.h"
#include "conversion/SF2Conversion.h"
#include "conversion/SF2File.h"
#include "formats/NDS/NDSInstrSet.h"
#include "value/export/ExportTypes.h"
#include "value/export/midi/MidiExporter.h"
#include "value/export/synth/ModulationScaling.h"
#include "value/session/Session.h"
#include "value/sequence/SequenceVm.h"
#include "value/synth/SampleDecoder.h"
#include "value/formats/CapcomSnes/CapcomSnes.h"
#include "value/formats/ValueFormats.h"
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
#include <set>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
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

void writeFile(const std::filesystem::path& path, std::span<const u8> bytes) {
  std::ofstream stream(path, std::ios::binary);
  if (!stream) {
    throw std::runtime_error("failed to open output file: " + path.string());
  }
  stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (!stream) {
    throw std::runtime_error("failed to write output file: " + path.string());
  }
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

    const ConversionContext playOnceContext;
    auto midi = sequence->convertToMidi(nullptr, playOnceContext);
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

    const ConversionContext playOnceContext;
    auto midi = collection->seq()->convertToMidi(collection, playOnceContext);
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

struct SynthExportBytes {
  std::vector<u8> sf2;
  std::vector<u8> dls;
};

SynthExportBytes legacyCollectionSynthExports(const VGMColl& collection) {
  SynthExportBytes exports;

  const auto sf2 = conversion::createSF2File(collection);
  if (sf2 == nullptr) {
    throw std::runtime_error("legacy collection failed to create SF2: " + collection.name());
  }
  exports.sf2 = sf2->saveToMem();

  DLSFile dls;
  if (!conversion::createDLSFile(dls, collection)) {
    throw std::runtime_error("legacy collection failed to create DLS: " + collection.name());
  }
  dls.writeDLSToBuffer(exports.dls);

  return exports;
}

std::map<std::string, SynthExportBytes> legacyCapcomSnesRsnSynthExports(const std::filesystem::path& path) {
  const auto root = scanLegacyFile(path);
  std::map<std::string, SynthExportBytes> exports;

  for (auto* collection : root->vgmColls()) {
    if (collection == nullptr || collection->instrSets().empty() || collection->sampColls().empty()) {
      continue;
    }

    auto [_, inserted] = exports.emplace(collection->name(), legacyCollectionSynthExports(*collection));
    if (!inserted) {
      throw std::runtime_error("duplicate legacy collection name from RSN: " + collection->name());
    }
  }

  if (exports.empty()) {
    throw std::runtime_error("legacy scanner did not discover synth-exportable collections in: " + path.string());
  }
  return exports;
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
  u32 instrumentSetCount = 0;
  u32 sampleCollectionCount = 0;
  std::vector<SampleSummary> samples;
  std::vector<RegionSummary> regions;
  std::vector<InstrumentSynthSummary> instrumentSynths;

  friend bool operator==(const CapcomSnesSummary&, const CapcomSnesSummary&) = default;
};

struct AkaoCollectionSummary {
  u32 sequenceOffset = 0;
  u32 trackCount = 0;
  u32 instrumentSetCount = 0;
  u32 sampleCollectionCount = 0;
  u32 sampleCount = 0;
  std::vector<SampleSummary> samples;
  std::vector<RegionSummary> regions;
  std::vector<InstrumentSynthSummary> instrumentSynths;

  friend bool operator==(const AkaoCollectionSummary&, const AkaoCollectionSummary&) = default;
};

struct AkaoSummary {
  std::vector<AkaoCollectionSummary> collections;

  friend bool operator==(const AkaoSummary&, const AkaoSummary&) = default;
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

u32 loopFramesFromLegacyValue(const VGMSamp& sample, u32 value, LoopMeasure measure) {
  if (measure == LM_SAMPLES) {
    return value;
  }
  if (sample.dataLength % 9 == 0) {
    return (value / 9) * 16;
  }
  const auto bytesPerFrame = std::max<int>(1, sample.bytesPerSample() * sample.channels);
  return value / static_cast<u32>(bytesPerFrame);
}

u32 envelopeMicros(std::optional<double> seconds) {
  if (!seconds) {
    return 0;
  }
  if (*seconds < 0.0 || !std::isfinite(*seconds)) {
    return std::numeric_limits<u32>::max();
  }
  constexpr double microsPerSecond = 1000000.0;
  const double micros = *seconds * microsPerSecond;
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
    case SynthDestination::VolumeAttenuation:
      return 3;
    case SynthDestination::Pan:
      return 4;
    case SynthDestination::VibratoDepth:
      return 10;
    case SynthDestination::VibratoRate:
      return 11;
    case SynthDestination::VibratoDelay:
      return 12;
    case SynthDestination::TremoloDepth:
      return 20;
    case SynthDestination::TremoloRate:
      return 21;
    case SynthDestination::TremoloDelay:
      return destinationCode(SynthDestination::Unknown);
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
    case ModDest::VibLfoDelay:
      return destinationCode(SynthDestination::VibratoDelay);
    case ModDest::ModLfoToVol:
      return destinationCode(SynthDestination::TremoloDepth);
    case ModDest::ModLfoFreq:
      return destinationCode(SynthDestination::TremoloRate);
    case ModDest::InitialAtten:
      return destinationCode(SynthDestination::VolumeAttenuation);
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

  if (region.sampCollPtr != nullptr && region.sampNum < region.sampCollPtr->samples().size() &&
      region.sampCollPtr->sample(region.sampNum) != nullptr) {
    return region.sampCollPtr->sample(region.sampNum)->dataOff;
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

void appendLegacySamples(CapcomSnesSummary& summary, std::vector<VGMSamp*>& samples,
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
        .loopStart = sample->loop.loopStatus > 0
                         ? loopFramesFromLegacyValue(*sample, sample->loop.loopStart, sample->loop.loopStartMeasure)
                         : 0,
        .loopLength = sample->loop.loopStatus > 0
                          ? loopFramesFromLegacyValue(*sample, sample->loop.loopLength, sample->loop.loopLengthMeasure)
                          : 0,
        .pcmHash = fnv1a(pcm),
    });
  }
}

void appendLegacyInstruments(CapcomSnesSummary& summary, std::span<VGMInstrSet* const> instrumentSets,
                             std::span<VGMSamp* const> samples, bool useExportInstruments = false) {
  for (const auto* instrumentSet : instrumentSets) {
    if (instrumentSet == nullptr) {
      continue;
    }

    ++summary.instrumentSetCount;
    const auto instruments = useExportInstruments ? instrumentSet->exportInstrs() : instrumentSet->instrs();
    for (const auto* instrument : instruments) {
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
            .tuningCents = region->unityKey >= 0 ? static_cast<s32>((region->unityKey - 96) * 100 - region->fineTune)
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
  std::ranges::sort(summary.samples, [](const SampleSummary& lhs, const SampleSummary& rhs) {
    return std::tie(lhs.sourceOffset, lhs.sourceSize, lhs.sampleRate, lhs.channels, lhs.frameCount, lhs.loopEnabled,
                    lhs.loopStart, lhs.loopLength, lhs.pcmHash) <
           std::tie(rhs.sourceOffset, rhs.sourceSize, rhs.sampleRate, rhs.channels, rhs.frameCount, rhs.loopEnabled,
                    rhs.loopStart, rhs.loopLength, rhs.pcmHash);
  });
  std::ranges::sort(summary.regions, [](const RegionSummary& lhs, const RegionSummary& rhs) {
    return std::tie(lhs.bank, lhs.program, lhs.sourceOffset, lhs.sampleSourceOffset, lhs.keyLow, lhs.keyHigh,
                    lhs.velocityLow, lhs.velocityHigh, lhs.tuningCents, lhs.envelopeAttack, lhs.envelopeDecay,
                    lhs.envelopeSustain, lhs.envelopeRelease) <
           std::tie(rhs.bank, rhs.program, rhs.sourceOffset, rhs.sampleSourceOffset, rhs.keyLow, rhs.keyHigh,
                    rhs.velocityLow, rhs.velocityHigh, rhs.tuningCents, rhs.envelopeAttack, rhs.envelopeDecay,
                    rhs.envelopeSustain, rhs.envelopeRelease);
  });
  for (auto& synth : summary.instrumentSynths) {
    std::ranges::sort(synth.generators, [](const GeneratorSummary& lhs, const GeneratorSummary& rhs) {
      return std::tie(lhs.destination, lhs.amount) < std::tie(rhs.destination, rhs.amount);
    });
    std::ranges::sort(synth.modulators, [](const ModulatorSummary& lhs, const ModulatorSummary& rhs) {
      return std::tie(lhs.source, lhs.destination, lhs.amount) < std::tie(rhs.source, rhs.destination, rhs.amount);
    });
  }
  std::ranges::sort(summary.instrumentSynths, [](const InstrumentSynthSummary& lhs, const InstrumentSynthSummary& rhs) {
    return std::tie(lhs.bank, lhs.program, lhs.sourceOffset) < std::tie(rhs.bank, rhs.program, rhs.sourceOffset);
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

  std::vector<VGMInstrSet*> instrumentSets;
  appendUnique(instrumentSets, collection.instrSets());

  std::vector<VGMSampColl*> sampleCollections;
  appendUnique(sampleCollections, collection.sampColls());
  for (const auto* instrumentSet : instrumentSets) {
    appendUnique(sampleCollections, instrumentSet->sampColl());
  }

  std::vector<VGMSamp*> samples;
  appendLegacySamples(summary, samples, sampleCollections);
  appendLegacyInstruments(summary, instrumentSets, samples);
  normalizeSummary(summary);

  return summary;
}

CapcomSnesSummary legacyPreparedCollectionSummary(const VGMColl& collection) {
  CapcomSnesSummary summary;

  if (const auto* sequence = collection.seq()) {
    ++summary.sequenceCount;
    summary.trackCounts.push_back(static_cast<u32>(sequence->trackCount()));
  }

  std::vector<VGMInstrSet*> instrumentSets;
  appendUnique(instrumentSets, collection.instrSets());
  for (auto* instrumentSet : instrumentSets) {
    instrumentSet->prepareForExport(&collection);
  }

  std::vector<VGMSampColl*> sampleCollections;
  appendUnique(sampleCollections, collection.sampColls());
  for (const auto* instrumentSet : instrumentSets) {
    appendUnique(sampleCollections, instrumentSet->sampColl());
  }

  std::vector<VGMSamp*> samples;
  appendLegacySamples(summary, samples, sampleCollections);
  appendLegacyInstruments(summary, instrumentSets, samples, true);
  normalizeSummary(summary);

  for (auto* instrumentSet : instrumentSets) {
    instrumentSet->cleanupAfterExport();
  }

  return summary;
}

CapcomSnesSummary legacyCapcomSnesSummary(std::span<const u8> aramBytes, const std::string& name) {
  const auto root = scanLegacyCapcomSnes(aramBytes, name);

  CapcomSnesSummary summary;
  std::vector<VGMInstrSet*> instrumentSets;
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
      appendUnique(instrumentSets, *instrumentSlot);
    }
  }

  for (const auto* instrumentSet : instrumentSets) {
    appendUnique(sampleCollections, instrumentSet->sampColl());
  }

  std::vector<VGMSamp*> samples;
  appendLegacySamples(summary, samples, sampleCollections);
  appendLegacyInstruments(summary, instrumentSets, samples);
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

CapcomSnesSummary valueCapcomSnesSummary(const SessionSnapshot& project, const SourceStore& sources,
                                         const Collection& collection) {
  const auto decoders = SampleDecoderRegistry::withDefaultDecoders();

  CapcomSnesSummary summary;
  std::map<u32, const SampleCollectionAsset*> sampleCollectionsById;
  std::optional<AssetId> fallbackSampleCollection;

  if (collection.sequence) {
    if (const auto* sequenceProgram = project.asset<SequenceProgramAsset>(*collection.sequence)) {
      ++summary.sequenceCount;
      summary.trackCounts.push_back(static_cast<u32>(sequenceProgram->program.tracks.size()));
    }
  }

  for (const auto sampleCollectionId : collection.sampleCollections) {
    const auto* sampleCollection = project.asset<SampleCollectionAsset>(sampleCollectionId);
    if (sampleCollection == nullptr) {
      continue;
    }

    ++summary.sampleCollectionCount;
    if (!fallbackSampleCollection) {
      fallbackSampleCollection = sampleCollection->metadata.id;
    }
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

  for (const auto instrumentSetId : collection.instrumentSets) {
    const auto* instrumentSet = project.asset<InstrumentSetAsset>(instrumentSetId);
    if (instrumentSet == nullptr) {
      continue;
    }

    ++summary.instrumentSetCount;
    for (const auto& instrument : instrumentSet->instruments) {
      const InstrumentAddress address = resolveInstrumentAddress(instrument.explicitAddress, instrument.identity);
      InstrumentSynthSummary synth{
          .bank = address.bank,
          .program = address.program,
          .sourceOffset = static_cast<u32>(instrument.range.offset),
      };
      const auto modulation = lowerSynthModulation(instrument.modulation);
      for (const auto& generator : modulation.generators) {
        synth.generators.push_back(summarizeGenerator(generator));
      }
      for (const auto& modulator : modulation.modulators) {
        synth.modulators.push_back(summarizeModulator(modulator));
      }
      summary.instrumentSynths.push_back(std::move(synth));

      for (const auto& region : instrument.regions) {
        u32 sampleSourceOffset = 0;
        const auto regionSampleCollection =
            region.sample.collection ? region.sample.collection : fallbackSampleCollection;
        if (regionSampleCollection) {
          const auto sampleCollection = sampleCollectionsById.find(regionSampleCollection->value);
          if (sampleCollection != sampleCollectionsById.end() &&
              region.sample.index < sampleCollection->second->samples.samples.size()) {
            sampleSourceOffset =
                static_cast<u32>(sampleCollection->second->samples.samples[region.sample.index].encodedData.offset);
          }
        }

        const s32 tuningCents = static_cast<s32>(std::lround((region.unityKey - 96.0) * 100.0));

        summary.regions.push_back(RegionSummary{
            .bank = address.bank,
            .program = address.program,
            .sourceOffset = static_cast<u32>(region.range.offset),
            .keyLow = region.keyRange.low,
            .keyHigh = region.keyRange.high,
            .velocityLow = region.velocityRange.low,
            .velocityHigh = region.velocityRange.high,
            .sampleSourceOffset = sampleSourceOffset,
            .tuningCents = tuningCents,
            .envelopeAttack = envelopeMicros(region.envelope.attackSeconds),
            .envelopeDecay = envelopeMicros(region.envelope.decaySeconds),
            .envelopeSustain = envelopePermille(region.envelope.sustainAmplitude.value_or(0.0)),
            .envelopeRelease = envelopeMicros(region.envelope.releaseSeconds),
        });
      }
    }
  }

  normalizeSummary(summary);
  return summary;
}

CapcomSnesSummary valueCapcomSnesSummary(std::vector<u8> aramBytes, const std::string& name) {
  Session session;
  vgmtrans::formats::registerValueFormats(session);
  session.addSource(SourceFile{.name = name}, std::move(aramBytes));

  session.scanPendingSources();

  const SessionSnapshot project = session.snapshot();
  expect(project.collections().size() == 1, "value ARAM summary expected one collection");
  return valueCapcomSnesSummary(project, session.sources(), project.collections().front());
}

std::map<std::string, CapcomSnesSummary> valueCapcomSnesRsnSummaries(const std::filesystem::path& path) {
  Session session;
  vgmtrans::formats::registerValueFormats(session);
  session.addSource(SourceFile{.name = path.filename().string(), .path = path}, readFile(path));

  session.scanPendingSources();

  const SessionSnapshot project = session.snapshot();
  if (project.collections().empty()) {
    std::ostringstream message;
    message << "value scanner did not discover collections from RSN";
    if (!project.diagnostics().empty()) {
      message << ": " << project.diagnostics().front().message;
    }
    throw std::runtime_error(message.str());
  }

  std::map<std::string, CapcomSnesSummary> summaries;
  for (const auto& collection : project.collections()) {
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

std::string describeTrackCounts(const std::vector<u32>& trackCounts) {
  std::ostringstream out;
  out << "[";
  for (size_t i = 0; i < trackCounts.size(); ++i) {
    if (i != 0) {
      out << ", ";
    }
    out << trackCounts[i];
  }
  out << "]";
  return out.str();
}

template <class Map>
std::string describeMapKeys(const Map& values) {
  std::ostringstream out;
  out << "[";
  bool first = true;
  for (const auto& [key, _] : values) {
    if (!first) {
      out << ", ";
    }
    first = false;
    out << key;
  }
  out << "]";
  return out.str();
}

bool compareSummary(const CapcomSnesSummary& legacy, const CapcomSnesSummary& value, std::ostream& out,
                    std::string_view label = "CapcomSnes") {
  if (legacy == value) {
    out << label << " summary parity ok: sequences=" << legacy.sequenceCount << " instruments=" << legacy.regions.size()
        << " synths=" << legacy.instrumentSynths.size() << " samples=" << legacy.samples.size() << "\n";
    return true;
  }

  out << label << " summary parity mismatch\n";
  out << "legacy counts: sequences=" << legacy.sequenceCount << " trackCounts=" << legacy.trackCounts.size()
      << " instrumentSets=" << legacy.instrumentSetCount << " sampleCollections=" << legacy.sampleCollectionCount
      << " regions=" << legacy.regions.size() << " synths=" << legacy.instrumentSynths.size()
      << " samples=" << legacy.samples.size() << "\n";
  out << "value counts:  sequences=" << value.sequenceCount << " trackCounts=" << value.trackCounts.size()
      << " instrumentSets=" << value.instrumentSetCount << " sampleCollections=" << value.sampleCollectionCount
      << " regions=" << value.regions.size() << " synths=" << value.instrumentSynths.size()
      << " samples=" << value.samples.size() << "\n";

  if (legacy.trackCounts != value.trackCounts) {
    out << "track count vectors differ: legacy=" << describeTrackCounts(legacy.trackCounts)
        << " value=" << describeTrackCounts(value.trackCounts) << "\n";
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
  Session session;
  vgmtrans::formats::registerValueFormats(session);
  session.addSource(SourceFile{.name = name}, std::move(aramBytes));

  session.scanPendingSources();

  const SessionSnapshot project = session.snapshot();
  if (project.collections().empty()) {
    std::ostringstream message;
    message << "value scanner did not discover a collection";
    if (!project.diagnostics().empty()) {
      message << ": " << project.diagnostics().front().message;
    }
    throw std::runtime_error(message.str());
  }

  const auto artifacts = session.exportCollection(
      project.collections().front().id, ExportRequest{
                                            .kinds = {ExportKind::Midi},
                                            .loopPolicy = LoopPolicy::PlayOnce,
                                            .sequenceLoops = 0,
                                            .modulationConversion = ModulationConversionPolicy::SynthModulators,
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

std::vector<u8> valueCapcomSnesMidi(Session& session, CollectionId collection) {
  const auto artifacts =
      session.exportCollection(collection, ExportRequest{
                                               .kinds = {ExportKind::Midi},
                                               .loopPolicy = LoopPolicy::PlayOnce,
                                               .sequenceLoops = 0,
                                               .modulationConversion = ModulationConversionPolicy::SynthModulators,
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
  Session session;
  vgmtrans::formats::registerValueFormats(session);
  session.addSource(SourceFile{.name = path.filename().string(), .path = path}, readFile(path));

  session.scanPendingSources();

  const SessionSnapshot project = session.snapshot();
  if (project.collections().empty()) {
    std::ostringstream message;
    message << "value scanner did not discover collections from RSN";
    if (!project.diagnostics().empty()) {
      message << ": " << project.diagnostics().front().message;
    }
    throw std::runtime_error(message.str());
  }

  std::map<std::string, std::vector<u8>> midis;
  for (const auto& collection : project.collections()) {
    std::vector<u8> midi;
    try {
      midi = valueCapcomSnesMidi(session, collection.id);
    } catch (const std::exception& ex) {
      throw std::runtime_error("value MIDI export failed for collection '" + collection.name + "': " + ex.what());
    }
    auto [_, inserted] = midis.emplace(collection.name, std::move(midi));
    if (!inserted) {
      throw std::runtime_error("duplicate value collection name from RSN: " + collection.name);
    }
  }
  return midis;
}

SynthExportBytes valueCapcomSnesSynthExports(Session& session, CollectionId collection) {
  const auto artifacts = session.exportCollection(collection, ExportRequest{
                                                                  .kinds = {ExportKind::SoundFont2, ExportKind::Dls},
                                                              });

  SynthExportBytes exports;
  for (const auto& artifact : artifacts) {
    if (!artifact.diagnostics.empty()) {
      throw std::runtime_error("value synth export reported: " + artifact.diagnostics.front().message);
    }
    if (artifact.mediaType == "audio/soundfont") {
      exports.sf2 = artifact.bytes;
    } else if (artifact.mediaType == "audio/dls") {
      exports.dls = artifact.bytes;
    }
  }

  if (exports.sf2.empty()) {
    throw std::runtime_error("value exporter did not produce an SF2 artifact");
  }
  if (exports.dls.empty()) {
    throw std::runtime_error("value exporter did not produce a DLS artifact");
  }
  return exports;
}

std::map<std::string, SynthExportBytes> valueCapcomSnesRsnSynthExports(const std::filesystem::path& path) {
  Session session;
  vgmtrans::formats::registerValueFormats(session);
  session.addSource(SourceFile{.name = path.filename().string(), .path = path}, readFile(path));

  session.scanPendingSources();

  const SessionSnapshot project = session.snapshot();
  if (project.collections().empty()) {
    std::ostringstream message;
    message << "value scanner did not discover collections from RSN";
    if (!project.diagnostics().empty()) {
      message << ": " << project.diagnostics().front().message;
    }
    throw std::runtime_error(message.str());
  }

  std::map<std::string, SynthExportBytes> exports;
  for (const auto& collection : project.collections()) {
    auto [_, inserted] = exports.emplace(collection.name, valueCapcomSnesSynthExports(session, collection.id));
    if (!inserted) {
      throw std::runtime_error("duplicate value collection name from RSN: " + collection.name);
    }
  }
  return exports;
}

std::map<std::string, CapcomSnesSummary> legacyCollectionSummaries(const std::filesystem::path& path) {
  const auto root = scanLegacyFile(path);
  std::map<std::string, CapcomSnesSummary> summaries;

  for (const auto* collection : root->vgmColls()) {
    if (collection == nullptr || collection->seq() == nullptr) {
      continue;
    }
    auto [_, inserted] = summaries.emplace(collection->name(), legacyCapcomSnesCollectionSummary(*collection));
    if (!inserted) {
      throw std::runtime_error("duplicate legacy collection name: " + collection->name());
    }
  }

  if (summaries.empty()) {
    throw std::runtime_error("legacy scanner did not discover collections in: " + path.string());
  }
  return summaries;
}

std::map<std::string, CapcomSnesSummary> valueCollectionSummaries(const std::filesystem::path& path) {
  Session session;
  vgmtrans::formats::registerValueFormats(session);
  session.addSource(SourceFile{.name = path.filename().string(), .path = path}, readFile(path));

  session.scanPendingSources();

  const SessionSnapshot project = session.snapshot();
  if (project.collections().empty()) {
    std::ostringstream message;
    message << "value scanner did not discover collections";
    if (!project.diagnostics().empty()) {
      message << ": " << project.diagnostics().front().message;
    }
    throw std::runtime_error(message.str());
  }

  std::map<std::string, CapcomSnesSummary> summaries;
  for (const auto& collection : project.collections()) {
    auto [_, inserted] =
        summaries.emplace(collection.name, valueCapcomSnesSummary(project, session.sources(), collection));
    if (!inserted) {
      throw std::runtime_error("duplicate value collection name: " + collection.name);
    }
  }
  return summaries;
}

std::map<std::string, CapcomSnesSummary> legacyFormatCollectionSummaries(const std::filesystem::path& path,
                                                                         std::string_view formatName,
                                                                         std::string_view label) {
  const auto root = scanLegacyFile(path);
  std::map<std::string, CapcomSnesSummary> summaries;

  for (const auto* collection : root->vgmColls()) {
    if (collection == nullptr || collection->seq() == nullptr || collection->seq()->formatName() != formatName) {
      continue;
    }
    auto [_, inserted] = summaries.emplace(collection->name(), legacyCapcomSnesCollectionSummary(*collection));
    if (!inserted) {
      throw std::runtime_error("duplicate legacy " + std::string(label) + " collection name: " + collection->name());
    }
  }

  if (summaries.empty()) {
    throw std::runtime_error("legacy scanner did not discover " + std::string(label) +
                             " collections in: " + path.string());
  }
  return summaries;
}

bool valueCollectionHasSequenceFormat(const SessionSnapshot& project, const Collection& collection,
                                      std::string_view formatName) {
  if (!collection.sequence) {
    return false;
  }
  const auto* sequence = project.asset<SequenceProgramAsset>(*collection.sequence);
  return sequence != nullptr && sequence->metadata.format == formatName;
}

std::map<std::string, CapcomSnesSummary> valueFormatCollectionSummaries(const std::filesystem::path& path,
                                                                        std::string_view formatName,
                                                                        std::string_view label) {
  Session session;
  vgmtrans::formats::registerValueFormats(session);
  session.addSource(SourceFile{.name = path.filename().string(), .path = path}, readFile(path));

  session.scanPendingSources();

  const SessionSnapshot project = session.snapshot();
  if (project.collections().empty()) {
    std::ostringstream message;
    message << "value scanner did not discover " << label << " collections";
    if (!project.diagnostics().empty()) {
      message << ": " << project.diagnostics().front().message;
    }
    throw std::runtime_error(message.str());
  }

  std::map<std::string, CapcomSnesSummary> summaries;
  for (const auto& collection : project.collections()) {
    if (!valueCollectionHasSequenceFormat(project, collection, formatName)) {
      continue;
    }
    auto [_, inserted] =
        summaries.emplace(collection.name, valueCapcomSnesSummary(project, session.sources(), collection));
    if (!inserted) {
      throw std::runtime_error("duplicate value " + std::string(label) + " collection name: " + collection.name);
    }
  }

  if (summaries.empty()) {
    throw std::runtime_error("value scanner did not discover " + std::string(label) +
                             " collections in: " + path.string());
  }
  return summaries;
}

AkaoSummary legacyAkaoSummary(const std::filesystem::path& path) {
  const auto root = scanLegacyFile(path);
  AkaoSummary summary;
  for (const auto* collection : root->vgmColls()) {
    if (collection == nullptr || collection->seq() == nullptr || collection->seq()->formatName() != "Akao") {
      continue;
    }
    AkaoCollectionSummary shape{
        .sequenceOffset = collection->seq()->offset(),
        .trackCount = static_cast<u32>(collection->seq()->trackCount()),
        .instrumentSetCount = static_cast<u32>(collection->instrSets().size()),
        .sampleCollectionCount = static_cast<u32>(collection->sampColls().size()),
    };
    const auto detailed = legacyPreparedCollectionSummary(*collection);
    shape.sampleCount = static_cast<u32>(detailed.samples.size());
    shape.samples = detailed.samples;
    for (auto& sample : shape.samples) {
      sample.loopEnabled = false;
      sample.loopStart = 0;
      sample.loopLength = 0;
    }
    shape.regions = detailed.regions;
    shape.instrumentSynths = detailed.instrumentSynths;
    summary.collections.push_back(shape);
  }
  std::ranges::sort(summary.collections, {}, &AkaoCollectionSummary::sequenceOffset);
  if (summary.collections.empty()) {
    throw std::runtime_error("legacy scanner did not discover Akao collections in: " + path.string());
  }
  return summary;
}

AkaoSummary valueAkaoSummary(const std::filesystem::path& path, std::ostream& diagnostics) {
  Session session;
  vgmtrans::formats::registerValueFormats(session);
  session.addSource(SourceFile{.name = path.filename().string(), .path = path}, readFile(path));

  session.scanPendingSources();

  const SessionSnapshot project = session.snapshot();
  for (const auto& diagnostic : project.diagnostics()) {
    diagnostics << "value diagnostic: " << diagnostic.message << "\n";
  }

  AkaoSummary summary;
  for (const auto& collection : project.collections()) {
    if (!collection.sequence) {
      continue;
    }
    const auto* sequence = project.asset<SequenceProgramAsset>(*collection.sequence);
    if (sequence == nullptr || sequence->metadata.format != "Akao") {
      continue;
    }
    AkaoCollectionSummary shape{
        .sequenceOffset = static_cast<u32>(sequence->metadata.range.offset),
        .trackCount = static_cast<u32>(sequence->program.tracks.size()),
        .instrumentSetCount = static_cast<u32>(collection.instrumentSets.size()),
        .sampleCollectionCount = static_cast<u32>(collection.sampleCollections.size()),
    };
    const auto detailed = valueCapcomSnesSummary(project, session.sources(), collection);
    shape.sampleCount = static_cast<u32>(detailed.samples.size());
    shape.samples = detailed.samples;
    for (auto& sample : shape.samples) {
      sample.loopEnabled = false;
      sample.loopStart = 0;
      sample.loopLength = 0;
    }
    shape.regions = detailed.regions;
    shape.instrumentSynths = detailed.instrumentSynths;
    summary.collections.push_back(shape);
  }
  if (std::ranges::any_of(summary.collections, [](const AkaoCollectionSummary& collection) {
        return collection.sampleCollectionCount == 0;
      })) {
    u32 sampleAssets = 0;
    u32 sampleFacts = 0;
    for (const auto& asset : project.assets()) {
      if (const auto* sampleCollection = std::get_if<SampleCollectionAsset>(&asset);
          sampleCollection != nullptr && sampleCollection->metadata.format == "Akao") {
        ++sampleAssets;
      }
    }
    for (const auto& fact : project.matchFacts()) {
      if (fact.format == "Akao") {
        if (std::holds_alternative<SampleCoverageFact>(fact.payload)) {
          ++sampleFacts;
        }
      }
    }
    diagnostics << "value Akao unresolved sample context: sampleAssets=" << sampleAssets
                << " sampleFacts=" << sampleFacts << "\n";
  }
  std::ranges::sort(summary.collections, {}, &AkaoCollectionSummary::sequenceOffset);
  if (summary.collections.empty()) {
    throw std::runtime_error("value scanner did not discover Akao collections in: " + path.string());
  }
  return summary;
}

std::string describeAkaoCollection(const AkaoCollectionSummary& summary) {
  std::ostringstream out;
  out << "seq=0x" << std::hex << summary.sequenceOffset << std::dec << " tracks=" << summary.trackCount
      << " instrSets=" << summary.instrumentSetCount << " sampleCollections=" << summary.sampleCollectionCount
      << " samples=" << summary.sampleCount << " regions=" << summary.regions.size()
      << " synths=" << summary.instrumentSynths.size();
  return out.str();
}

bool describeAkaoCollectionMismatch(const AkaoCollectionSummary& legacy, const AkaoCollectionSummary& value) {
  if (legacy.trackCount != value.trackCount || legacy.instrumentSetCount != value.instrumentSetCount ||
      legacy.sampleCollectionCount != value.sampleCollectionCount || legacy.sampleCount != value.sampleCount) {
    return false;
  }

  const size_t sharedSamples = std::min(legacy.samples.size(), value.samples.size());
  for (size_t i = 0; i < sharedSamples; ++i) {
    if (!(legacy.samples[i] == value.samples[i])) {
      std::cout << "first sample mismatch at " << i << "\n";
      std::cout << "legacy: " << describeSample(legacy.samples[i]) << "\n";
      std::cout << "value:  " << describeSample(value.samples[i]) << "\n";
      return true;
    }
  }
  if (legacy.samples.size() != value.samples.size()) {
    std::cout << "sample count differs\n";
    return true;
  }

  const size_t sharedRegions = std::min(legacy.regions.size(), value.regions.size());
  for (size_t i = 0; i < sharedRegions; ++i) {
    if (!(legacy.regions[i] == value.regions[i])) {
      std::cout << "first region mismatch at " << i << "\n";
      std::cout << "legacy: " << describeRegion(legacy.regions[i]) << "\n";
      std::cout << "value:  " << describeRegion(value.regions[i]) << "\n";
      return true;
    }
  }
  if (legacy.regions.size() != value.regions.size()) {
    std::cout << "region count differs\n";
    return true;
  }

  const size_t sharedSynths = std::min(legacy.instrumentSynths.size(), value.instrumentSynths.size());
  for (size_t i = 0; i < sharedSynths; ++i) {
    if (!(legacy.instrumentSynths[i] == value.instrumentSynths[i])) {
      std::cout << "first instrument synth mismatch at " << i << "\n";
      std::cout << "legacy: " << describeInstrumentSynth(legacy.instrumentSynths[i]) << "\n";
      std::cout << "value:  " << describeInstrumentSynth(value.instrumentSynths[i]) << "\n";
      return true;
    }
  }
  if (legacy.instrumentSynths.size() != value.instrumentSynths.size()) {
    std::cout << "instrument synth count differs\n";
    return true;
  }

  return false;
}

int compareAkaoDirectSummary(const std::filesystem::path& path) {
  const auto legacy = legacyAkaoSummary(path);
  const auto value = valueAkaoSummary(path, std::cout);
  if (legacy == value) {
    std::cout << "Akao direct summary parity ok: collections=" << legacy.collections.size() << "\n";
    return 0;
  }

  std::cout << "Akao direct summary parity mismatch\n";
  std::cout << "legacy collections=" << legacy.collections.size() << " value collections=" << value.collections.size()
            << "\n";
  const size_t shared = std::min(legacy.collections.size(), value.collections.size());
  for (size_t i = 0; i < shared; ++i) {
    if (!(legacy.collections[i] == value.collections[i])) {
      std::cout << "first mismatch at collection " << i << "\n";
      std::cout << "legacy: " << describeAkaoCollection(legacy.collections[i]) << "\n";
      std::cout << "value:  " << describeAkaoCollection(value.collections[i]) << "\n";
      static_cast<void>(describeAkaoCollectionMismatch(legacy.collections[i], value.collections[i]));
      return 1;
    }
  }
  return 1;
}

std::string legacyMidiCollectionKey(const VGMColl& collection);
std::string valueMidiCollectionKey(const SessionSnapshot& project, const Collection& collection);
std::vector<u8> valueCollectionMidi(
    Session& session, CollectionId collection, u32 sequenceLoops,
    ModulationConversionPolicy modulationConversion = ModulationConversionPolicy::SynthModulators,
    MidiExportOptions midiOptions = {},
    ModulationScalingPolicy modulationScaling = ModulationScalingPolicy::FullFormatRange);

std::map<std::string, std::vector<u8>> legacyAkaoCollectionMidis(const std::filesystem::path& path,
                                                                 u32 sequenceLoops = 0) {
  const auto root = scanLegacyFile(path);
  std::map<std::string, std::vector<u8>> midis;

  for (auto* collection : root->vgmColls()) {
    if (collection == nullptr || collection->seq() == nullptr || collection->seq()->formatName() != "Akao") {
      continue;
    }
    ConversionContext context;
    context.sequenceLoops = static_cast<int>(sequenceLoops);
    auto midi = collection->seq()->convertToMidi(collection, context);
    if (!midi) {
      throw std::runtime_error("legacy Akao collection failed to convert to MIDI: " + collection->name());
    }
    std::vector<u8> bytes;
    midi->writeMidiToBuffer(bytes);
    const std::string key = legacyMidiCollectionKey(*collection);
    auto [_, inserted] = midis.emplace(key, std::move(bytes));
    if (!inserted) {
      throw std::runtime_error("duplicate legacy Akao MIDI collection key: " + key);
    }
  }

  if (midis.empty()) {
    throw std::runtime_error("legacy scanner did not discover Akao MIDI collections in: " + path.string());
  }
  return midis;
}

std::map<std::string, std::vector<u8>> valueAkaoCollectionMidis(const std::filesystem::path& path,
                                                                u32 sequenceLoops = 0) {
  Session session;
  vgmtrans::formats::registerValueFormats(session);
  session.addSource(SourceFile{.name = path.filename().string(), .path = path}, readFile(path));

  session.scanPendingSources();

  const SessionSnapshot project = session.snapshot();
  if (project.collections().empty()) {
    std::ostringstream message;
    message << "value scanner did not discover Akao MIDI collections";
    if (!project.diagnostics().empty()) {
      message << ": " << project.diagnostics().front().message;
    }
    throw std::runtime_error(message.str());
  }

  std::map<std::string, std::vector<u8>> midis;
  for (const auto& collection : project.collections()) {
    if (!collection.sequence) {
      continue;
    }
    const auto* sequence = project.asset<SequenceProgramAsset>(*collection.sequence);
    if (sequence == nullptr || sequence->metadata.format != "Akao") {
      continue;
    }
    std::vector<u8> midi;
    try {
      midi = valueCollectionMidi(session, collection.id, sequenceLoops);
    } catch (const std::exception& ex) {
      throw std::runtime_error("value Akao MIDI export failed for collection '" + collection.name + "': " + ex.what());
    }
    const std::string key = valueMidiCollectionKey(project, collection);
    auto [_, inserted] = midis.emplace(key, std::move(midi));
    if (!inserted) {
      throw std::runtime_error("duplicate value Akao MIDI collection key: " + key);
    }
  }
  if (midis.empty()) {
    throw std::runtime_error("value scanner did not discover Akao MIDI collections");
  }
  return midis;
}

std::map<std::string, SynthExportBytes> legacyAkaoCollectionSynthExports(const std::filesystem::path& path) {
  const auto root = scanLegacyFile(path);
  std::map<std::string, SynthExportBytes> exports;

  for (auto* collection : root->vgmColls()) {
    if (collection == nullptr || collection->seq() == nullptr || collection->seq()->formatName() != "Akao" ||
        collection->instrSets().empty() || collection->sampColls().empty()) {
      continue;
    }
    const std::string key = legacyMidiCollectionKey(*collection);
    auto [_, inserted] = exports.emplace(key, legacyCollectionSynthExports(*collection));
    if (!inserted) {
      throw std::runtime_error("duplicate legacy Akao synth collection key: " + key);
    }
  }

  if (exports.empty()) {
    throw std::runtime_error("legacy scanner did not discover Akao synth-exportable collections in: " + path.string());
  }
  return exports;
}

std::map<std::string, SynthExportBytes> valueAkaoCollectionSynthExports(const std::filesystem::path& path) {
  Session session;
  vgmtrans::formats::registerValueFormats(session);
  session.addSource(SourceFile{.name = path.filename().string(), .path = path}, readFile(path));

  session.scanPendingSources();

  const SessionSnapshot project = session.snapshot();
  if (project.collections().empty()) {
    std::ostringstream message;
    message << "value scanner did not discover Akao synth collections";
    if (!project.diagnostics().empty()) {
      message << ": " << project.diagnostics().front().message;
    }
    throw std::runtime_error(message.str());
  }

  std::map<std::string, SynthExportBytes> exports;
  for (const auto& collection : project.collections()) {
    if (!collection.sequence || collection.instrumentSets.empty() || collection.sampleCollections.empty()) {
      continue;
    }
    const auto* sequence = project.asset<SequenceProgramAsset>(*collection.sequence);
    if (sequence == nullptr || sequence->metadata.format != "Akao") {
      continue;
    }
    const std::string key = valueMidiCollectionKey(project, collection);
    auto [_, inserted] = exports.emplace(key, valueCapcomSnesSynthExports(session, collection.id));
    if (!inserted) {
      throw std::runtime_error("duplicate value Akao synth collection key: " + key);
    }
  }

  if (exports.empty()) {
    throw std::runtime_error("value scanner did not discover Akao synth-exportable collections in: " + path.string());
  }
  return exports;
}

u32 parseLoopCount(std::string_view text) {
  size_t parsed = 0;
  const unsigned long value = std::stoul(std::string(text), &parsed, 10);
  if (parsed != text.size() || value > std::numeric_limits<u32>::max()) {
    throw std::runtime_error("invalid loop count: " + std::string(text));
  }
  return static_cast<u32>(value);
}

std::string midiCollectionKey(std::string_view name, u64 sequenceOffset) {
  std::ostringstream key;
  key << name << " @ 0x" << std::hex << sequenceOffset;
  return key.str();
}

std::string legacyMidiCollectionKey(const VGMColl& collection) {
  return midiCollectionKey(collection.name(), collection.seq()->offset());
}

std::string valueMidiCollectionKey(const SessionSnapshot& project, const Collection& collection) {
  if (!collection.sequence) {
    throw std::runtime_error("value MIDI collection had no sequence: " + collection.name);
  }
  const auto* sequence = project.asset<SequenceProgramAsset>(*collection.sequence);
  if (sequence == nullptr) {
    throw std::runtime_error("value MIDI collection referenced a missing sequence: " + collection.name);
  }
  return midiCollectionKey(collection.name, sequence->metadata.range.offset);
}

std::map<std::string, std::vector<u8>> legacyCollectionMidis(const std::filesystem::path& path, u32 sequenceLoops = 0) {
  const auto root = scanLegacyFile(path);
  std::map<std::string, std::vector<u8>> midis;

  for (auto* collection : root->vgmColls()) {
    if (collection == nullptr || collection->seq() == nullptr) {
      continue;
    }
    ConversionContext context;
    context.sequenceLoops = static_cast<int>(sequenceLoops);
    auto midi = collection->seq()->convertToMidi(collection, context);
    if (!midi) {
      throw std::runtime_error("legacy collection failed to convert to MIDI: " + collection->name());
    }
    std::vector<u8> bytes;
    midi->writeMidiToBuffer(bytes);
    const std::string key = legacyMidiCollectionKey(*collection);
    auto [_, inserted] = midis.emplace(key, std::move(bytes));
    if (!inserted) {
      throw std::runtime_error("duplicate legacy MIDI collection key: " + key);
    }
  }

  if (midis.empty()) {
    throw std::runtime_error("legacy scanner did not discover MIDI collections in: " + path.string());
  }
  return midis;
}

std::vector<u8> valueCollectionMidi(Session& session, CollectionId collection, u32 sequenceLoops,
                                    ModulationConversionPolicy modulationConversion, MidiExportOptions midiOptions,
                                    ModulationScalingPolicy modulationScaling) {
  const auto artifacts = session.exportCollection(collection, ExportRequest{
                                                                  .kinds = {ExportKind::Midi},
                                                                  .loopPolicy = LoopPolicy::PlayOnce,
                                                                  .sequenceLoops = sequenceLoops,
                                                                  .midi = midiOptions,
                                                                  .modulationScaling = modulationScaling,
                                                                  .modulationConversion = modulationConversion,
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

std::map<std::string, std::vector<u8>> valueCollectionMidis(
    const std::filesystem::path& path, u32 sequenceLoops = 0,
    ModulationConversionPolicy modulationConversion = ModulationConversionPolicy::SynthModulators,
    MidiExportOptions midiOptions = {},
    ModulationScalingPolicy modulationScaling = ModulationScalingPolicy::FullFormatRange) {
  Session session;
  vgmtrans::formats::registerValueFormats(session);
  session.addSource(SourceFile{.name = path.filename().string(), .path = path}, readFile(path));

  session.scanPendingSources();

  const SessionSnapshot project = session.snapshot();
  if (project.collections().empty()) {
    std::ostringstream message;
    message << "value scanner did not discover MIDI collections";
    if (!project.diagnostics().empty()) {
      message << ": " << project.diagnostics().front().message;
    }
    throw std::runtime_error(message.str());
  }

  std::map<std::string, std::vector<u8>> midis;
  for (const auto& collection : project.collections()) {
    if (!collection.sequence) {
      continue;
    }
    std::vector<u8> midi;
    try {
      midi = valueCollectionMidi(session, collection.id, sequenceLoops, modulationConversion, midiOptions,
                                 modulationScaling);
    } catch (const std::exception& ex) {
      throw std::runtime_error("value MIDI export failed for collection '" + collection.name + "': " + ex.what());
    }
    const std::string key = valueMidiCollectionKey(project, collection);
    auto [_, inserted] = midis.emplace(key, std::move(midi));
    if (!inserted) {
      throw std::runtime_error("duplicate value MIDI collection key: " + key);
    }
  }
  if (midis.empty()) {
    throw std::runtime_error("value scanner did not discover MIDI collections");
  }
  return midis;
}

std::map<std::string, std::vector<u8>> legacyFormatCollectionMidis(const std::filesystem::path& path,
                                                                   std::string_view formatName, std::string_view label,
                                                                   u32 sequenceLoops = 0) {
  const auto root = scanLegacyFile(path);
  std::map<std::string, std::vector<u8>> midis;

  for (auto* collection : root->vgmColls()) {
    if (collection == nullptr || collection->seq() == nullptr || collection->seq()->formatName() != formatName) {
      continue;
    }
    ConversionContext context;
    context.sequenceLoops = static_cast<int>(sequenceLoops);
    auto midi = collection->seq()->convertToMidi(collection, context);
    if (!midi) {
      throw std::runtime_error("legacy " + std::string(label) +
                               " collection failed to convert to MIDI: " + collection->name());
    }
    std::vector<u8> bytes;
    midi->writeMidiToBuffer(bytes);
    const std::string key = legacyMidiCollectionKey(*collection);
    auto [_, inserted] = midis.emplace(key, std::move(bytes));
    if (!inserted) {
      throw std::runtime_error("duplicate legacy " + std::string(label) + " MIDI collection key: " + key);
    }
  }

  if (midis.empty()) {
    throw std::runtime_error("legacy scanner did not discover " + std::string(label) +
                             " MIDI collections in: " + path.string());
  }
  return midis;
}

std::map<std::string, std::vector<u8>> valueFormatCollectionMidis(
    const std::filesystem::path& path, std::string_view formatName, std::string_view label, u32 sequenceLoops = 0,
    ModulationConversionPolicy modulationConversion = ModulationConversionPolicy::SynthModulators,
    MidiExportOptions midiOptions = {},
    ModulationScalingPolicy modulationScaling = ModulationScalingPolicy::FullFormatRange) {
  Session session;
  vgmtrans::formats::registerValueFormats(session);
  session.addSource(SourceFile{.name = path.filename().string(), .path = path}, readFile(path));

  session.scanPendingSources();

  const SessionSnapshot project = session.snapshot();
  if (project.collections().empty()) {
    std::ostringstream message;
    message << "value scanner did not discover " << label << " MIDI collections";
    if (!project.diagnostics().empty()) {
      message << ": " << project.diagnostics().front().message;
    }
    throw std::runtime_error(message.str());
  }

  std::map<std::string, std::vector<u8>> midis;
  for (const auto& collection : project.collections()) {
    if (!valueCollectionHasSequenceFormat(project, collection, formatName)) {
      continue;
    }
    std::vector<u8> midi;
    try {
      midi = valueCollectionMidi(session, collection.id, sequenceLoops, modulationConversion, midiOptions,
                                 modulationScaling);
    } catch (const std::exception& ex) {
      throw std::runtime_error("value " + std::string(label) + " MIDI export failed for collection '" +
                               collection.name + "': " + ex.what());
    }
    const std::string key = valueMidiCollectionKey(project, collection);
    auto [_, inserted] = midis.emplace(key, std::move(midi));
    if (!inserted) {
      throw std::runtime_error("duplicate value " + std::string(label) + " MIDI collection key: " + key);
    }
  }
  if (midis.empty()) {
    throw std::runtime_error("value scanner did not discover " + std::string(label) + " MIDI collections");
  }
  return midis;
}

std::map<std::string, SynthExportBytes> legacyCollectionSynthExports(const std::filesystem::path& path) {
  const auto root = scanLegacyFile(path);
  std::map<std::string, SynthExportBytes> exports;

  for (auto* collection : root->vgmColls()) {
    if (collection == nullptr || collection->instrSets().empty() || collection->sampColls().empty()) {
      continue;
    }
    auto [_, inserted] = exports.emplace(collection->name(), legacyCollectionSynthExports(*collection));
    if (!inserted) {
      throw std::runtime_error("duplicate legacy synth collection name: " + collection->name());
    }
  }

  if (exports.empty()) {
    throw std::runtime_error("legacy scanner did not discover synth-exportable collections in: " + path.string());
  }
  return exports;
}

std::map<std::string, SynthExportBytes> legacyFormatCollectionSynthExports(const std::filesystem::path& path,
                                                                           std::string_view formatName,
                                                                           std::string_view label) {
  const auto root = scanLegacyFile(path);
  std::map<std::string, SynthExportBytes> exports;

  for (auto* collection : root->vgmColls()) {
    if (collection == nullptr || collection->seq() == nullptr || collection->seq()->formatName() != formatName ||
        collection->instrSets().empty() || collection->sampColls().empty()) {
      continue;
    }
    const std::string key = legacyMidiCollectionKey(*collection);
    auto [_, inserted] = exports.emplace(key, legacyCollectionSynthExports(*collection));
    if (!inserted) {
      throw std::runtime_error("duplicate legacy " + std::string(label) + " synth collection key: " + key);
    }
  }

  if (exports.empty()) {
    throw std::runtime_error("legacy scanner did not discover " + std::string(label) +
                             " synth-exportable collections in: " + path.string());
  }
  return exports;
}

std::map<std::string, SynthExportBytes> valueCollectionSynthExports(const std::filesystem::path& path) {
  Session session;
  vgmtrans::formats::registerValueFormats(session);
  session.addSource(SourceFile{.name = path.filename().string(), .path = path}, readFile(path));

  session.scanPendingSources();

  const SessionSnapshot project = session.snapshot();
  if (project.collections().empty()) {
    std::ostringstream message;
    message << "value scanner did not discover synth collections";
    if (!project.diagnostics().empty()) {
      message << ": " << project.diagnostics().front().message;
    }
    throw std::runtime_error(message.str());
  }

  std::map<std::string, SynthExportBytes> exports;
  for (const auto& collection : project.collections()) {
    if (collection.instrumentSets.empty() || collection.sampleCollections.empty()) {
      continue;
    }
    auto [_, inserted] = exports.emplace(collection.name, valueCapcomSnesSynthExports(session, collection.id));
    if (!inserted) {
      throw std::runtime_error("duplicate value synth collection name: " + collection.name);
    }
  }

  if (exports.empty()) {
    throw std::runtime_error("value scanner did not discover synth-exportable collections in: " + path.string());
  }
  return exports;
}

std::map<std::string, SynthExportBytes> valueFormatCollectionSynthExports(const std::filesystem::path& path,
                                                                          std::string_view formatName,
                                                                          std::string_view label) {
  Session session;
  vgmtrans::formats::registerValueFormats(session);
  session.addSource(SourceFile{.name = path.filename().string(), .path = path}, readFile(path));

  session.scanPendingSources();

  const SessionSnapshot project = session.snapshot();
  if (project.collections().empty()) {
    std::ostringstream message;
    message << "value scanner did not discover " << label << " synth collections";
    if (!project.diagnostics().empty()) {
      message << ": " << project.diagnostics().front().message;
    }
    throw std::runtime_error(message.str());
  }

  std::map<std::string, SynthExportBytes> exports;
  for (const auto& collection : project.collections()) {
    if (!valueCollectionHasSequenceFormat(project, collection, formatName) || collection.instrumentSets.empty() ||
        collection.sampleCollections.empty()) {
      continue;
    }
    const std::string key = valueMidiCollectionKey(project, collection);
    auto [_, inserted] = exports.emplace(key, valueCapcomSnesSynthExports(session, collection.id));
    if (!inserted) {
      throw std::runtime_error("duplicate value " + std::string(label) + " synth collection key: " + key);
    }
  }

  if (exports.empty()) {
    throw std::runtime_error("value scanner did not discover " + std::string(label) +
                             " synth-exportable collections in: " + path.string());
  }
  return exports;
}

bool bytesMatch(std::span<const u8> bytes, size_t offset, std::string_view expected) {
  if (offset > bytes.size() || expected.size() > bytes.size() - offset) {
    return false;
  }

  for (size_t i = 0; i < expected.size(); ++i) {
    if (bytes[offset + i] != static_cast<u8>(expected[i])) {
      return false;
    }
  }
  return true;
}

bool endsWith(std::string_view text, std::string_view suffix) {
  return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
}

u32 valueSampleCount(const SessionSnapshot& project, const Collection& collection) {
  u32 sampleCount = 0;
  for (const auto sampleCollectionId : collection.sampleCollections) {
    if (const auto* sampleCollection = project.asset<SampleCollectionAsset>(sampleCollectionId)) {
      sampleCount += static_cast<u32>(sampleCollection->samples.samples.size());
    }
  }
  return sampleCount;
}

struct ExportSmokeCounts {
  u32 midi = 0;
  u32 soundFont2 = 0;
  u32 dls = 0;
  u32 wav = 0;
};

bool validateExportArtifact(const Artifact& artifact, std::string_view expectedExtension, std::string_view header,
                            std::string_view formatTag, std::ostream& out) {
  if (!artifact.diagnostics.empty()) {
    out << "artifact '" << artifact.filename << "' reported: " << artifact.diagnostics.front().message << "\n";
    return false;
  }

  if (artifact.bytes.empty()) {
    out << "artifact '" << artifact.filename << "' was empty\n";
    return false;
  }

  if (!endsWith(artifact.filename, expectedExtension)) {
    out << "artifact '" << artifact.filename << "' did not end with " << expectedExtension << "\n";
    return false;
  }

  if (!bytesMatch(artifact.bytes, 0, header)) {
    out << "artifact '" << artifact.filename << "' did not start with " << header << "\n";
    return false;
  }

  if (!formatTag.empty() && !bytesMatch(artifact.bytes, 8, formatTag)) {
    out << "artifact '" << artifact.filename << "' did not contain " << formatTag << " at offset 8\n";
    return false;
  }

  return true;
}

u16 le16At(std::span<const u8> bytes, size_t offset) {
  expect(offset <= bytes.size() && bytes.size() - offset >= 2, "truncated little-endian u16");
  return static_cast<u16>(bytes[offset] | (bytes[offset + 1] << 8));
}

s16 leS16At(std::span<const u8> bytes, size_t offset) {
  return static_cast<s16>(le16At(bytes, offset));
}

u32 le32At(std::span<const u8> bytes, size_t offset) {
  expect(offset <= bytes.size() && bytes.size() - offset >= 4, "truncated little-endian u32");
  return static_cast<u32>(bytes[offset]) | (static_cast<u32>(bytes[offset + 1]) << 8) |
         (static_cast<u32>(bytes[offset + 2]) << 16) | (static_cast<u32>(bytes[offset + 3]) << 24);
}

s32 leS32At(std::span<const u8> bytes, size_t offset) {
  return static_cast<s32>(le32At(bytes, offset));
}

std::string asciiAt(std::span<const u8> bytes, size_t offset, size_t count) {
  expect(offset <= bytes.size() && bytes.size() - offset >= count, "truncated ASCII field");
  return std::string(reinterpret_cast<const char*>(bytes.data() + offset), count);
}

struct RiffNode {
  std::string id;
  std::string type;
  size_t dataOffset = 0;
  size_t size = 0;
  std::vector<RiffNode> children;
};

std::vector<RiffNode> parseRiffChildren(std::span<const u8> bytes, size_t begin, size_t end);

RiffNode parseRiff(std::span<const u8> bytes, std::string_view expectedType) {
  expect(bytes.size() >= 12, "RIFF file is too small");
  expect(asciiAt(bytes, 0, 4) == "RIFF", "RIFF file missing RIFF header");
  const u32 declaredSize = le32At(bytes, 4);
  const size_t riffEnd = std::min(bytes.size(), static_cast<size_t>(declaredSize) + 8);
  expect(riffEnd <= bytes.size(), "RIFF declared size extends past file end");
  const std::string type = asciiAt(bytes, 8, 4);
  expect(type == expectedType, "RIFF file has unexpected type");
  return RiffNode{
      .id = "RIFF",
      .type = type,
      .dataOffset = 12,
      .size = riffEnd - 12,
      .children = parseRiffChildren(bytes, 12, riffEnd),
  };
}

std::vector<RiffNode> parseRiffChildren(std::span<const u8> bytes, size_t begin, size_t end) {
  std::vector<RiffNode> nodes;
  size_t offset = begin;
  while (offset + 8 <= end) {
    RiffNode node{
        .id = asciiAt(bytes, offset, 4),
        .dataOffset = offset + 8,
        .size = le32At(bytes, offset + 4),
    };
    const size_t payloadEnd = node.dataOffset + node.size;
    expect(payloadEnd <= end, "RIFF chunk extends past parent");
    if (node.id == "LIST") {
      expect(node.size >= 4, "RIFF LIST chunk is missing a type");
      node.type = asciiAt(bytes, node.dataOffset, 4);
      node.children = parseRiffChildren(bytes, node.dataOffset + 4, payloadEnd);
    }
    nodes.push_back(std::move(node));
    offset = payloadEnd + (node.size & 1u);
  }
  expect(offset == end || offset == end + 1, "RIFF chunk padding mismatch");
  return nodes;
}

const RiffNode* childChunk(const RiffNode& node, std::string_view id) {
  const auto found = std::ranges::find_if(node.children, [id](const RiffNode& child) { return child.id == id; });
  return found == node.children.end() ? nullptr : &*found;
}

const RiffNode* childList(const RiffNode& node, std::string_view type) {
  const auto found = std::ranges::find_if(
      node.children, [type](const RiffNode& child) { return child.id == "LIST" && child.type == type; });
  return found == node.children.end() ? nullptr : &*found;
}

const RiffNode* firstChunk(const RiffNode& node, std::string_view id) {
  if (node.id == id) {
    return &node;
  }
  for (const auto& child : node.children) {
    if (const auto* found = firstChunk(child, id)) {
      return found;
    }
  }
  return nullptr;
}

std::vector<const RiffNode*> childLists(const RiffNode& node, std::string_view type) {
  std::vector<const RiffNode*> lists;
  for (const auto& child : node.children) {
    if (child.id == "LIST" && child.type == type) {
      lists.push_back(&child);
    }
  }
  return lists;
}

void validateSf2RiffStructure(const RiffNode& root) {
  const auto* info = childList(root, "INFO");
  expect(info != nullptr, "SF2 file is missing INFO list");
  for (const auto& chunk : info->children) {
    expect((chunk.size & 1u) == 0, "SF2 INFO chunk has odd declared size");
  }
}

std::vector<u8> chunkBytes(std::span<const u8> bytes, const RiffNode* node) {
  if (node == nullptr) {
    return {};
  }
  expect(node->dataOffset <= bytes.size() && bytes.size() - node->dataOffset >= node->size,
         "RIFF chunk payload is out of range");
  return {bytes.begin() + static_cast<std::ptrdiff_t>(node->dataOffset),
          bytes.begin() + static_cast<std::ptrdiff_t>(node->dataOffset + node->size)};
}

struct Sf2Generator {
  u16 operation = 0;
  s16 amount = 0;

  friend bool operator==(const Sf2Generator&, const Sf2Generator&) = default;
};

struct Sf2Modulator {
  u16 source = 0;
  u16 destination = 0;
  s16 amount = 0;
  u16 amountSource = 0;
  u16 transform = 0;

  friend bool operator==(const Sf2Modulator&, const Sf2Modulator&) = default;
};

struct Sf2Zone {
  std::vector<Sf2Generator> generators;
  std::vector<Sf2Modulator> modulators;

  friend bool operator==(const Sf2Zone&, const Sf2Zone&) = default;
};

struct Sf2Preset {
  u16 preset = 0;
  u16 bank = 0;
  std::vector<Sf2Zone> zones;

  friend bool operator==(const Sf2Preset&, const Sf2Preset&) = default;
};

struct Sf2Instrument {
  std::vector<Sf2Zone> zones;

  friend bool operator==(const Sf2Instrument&, const Sf2Instrument&) = default;
};

struct Sf2Sample {
  u32 length = 0;
  u32 loopStart = 0;
  u32 loopEnd = 0;
  u32 sampleRate = 0;
  u8 originalPitch = 0;
  s8 pitchCorrection = 0;
  u16 sampleLink = 0;
  u16 sampleType = 0;
  u64 pcmHash = 0;

  friend bool operator==(const Sf2Sample&, const Sf2Sample&) = default;
};

struct NormalizedSf2 {
  std::vector<Sf2Preset> presets;
  std::vector<Sf2Instrument> instruments;
  std::vector<Sf2Sample> samples;

  friend bool operator==(const NormalizedSf2&, const NormalizedSf2&) = default;
};

struct Sf2Bag {
  u16 generatorIndex = 0;
  u16 modulatorIndex = 0;
};

std::vector<Sf2Bag> readSf2Bags(std::span<const u8> bytes, const RiffNode& node) {
  expect(node.size % 4 == 0, "SF2 bag chunk has invalid size");
  std::vector<Sf2Bag> bags;
  for (size_t offset = node.dataOffset; offset < node.dataOffset + node.size; offset += 4) {
    bags.push_back(Sf2Bag{
        .generatorIndex = le16At(bytes, offset),
        .modulatorIndex = le16At(bytes, offset + 2),
    });
  }
  return bags;
}

std::vector<Sf2Generator> readSf2Generators(std::span<const u8> bytes, const RiffNode& node) {
  expect(node.size % 4 == 0, "SF2 generator chunk has invalid size");
  std::vector<Sf2Generator> generators;
  for (size_t offset = node.dataOffset; offset < node.dataOffset + node.size; offset += 4) {
    generators.push_back(Sf2Generator{
        .operation = le16At(bytes, offset),
        .amount = leS16At(bytes, offset + 2),
    });
  }
  return generators;
}

std::vector<Sf2Modulator> readSf2Modulators(std::span<const u8> bytes, const RiffNode& node) {
  expect(node.size % 10 == 0, "SF2 modulator chunk has invalid size");
  std::vector<Sf2Modulator> modulators;
  for (size_t offset = node.dataOffset; offset < node.dataOffset + node.size; offset += 10) {
    modulators.push_back(Sf2Modulator{
        .source = le16At(bytes, offset),
        .destination = le16At(bytes, offset + 2),
        .amount = leS16At(bytes, offset + 4),
        .amountSource = le16At(bytes, offset + 6),
        .transform = le16At(bytes, offset + 8),
    });
  }
  return modulators;
}

void sortSf2Zone(Sf2Zone& zone) {
  std::ranges::sort(zone.generators, [](const auto& lhs, const auto& rhs) {
    return std::tie(lhs.operation, lhs.amount) < std::tie(rhs.operation, rhs.amount);
  });
  std::ranges::sort(zone.modulators, [](const auto& lhs, const auto& rhs) {
    return std::tie(lhs.source, lhs.destination, lhs.amount, lhs.amountSource, lhs.transform) <
           std::tie(rhs.source, rhs.destination, rhs.amount, rhs.amountSource, rhs.transform);
  });
}

void normalizeSf2ZonePitch(Sf2Zone& zone) {
  const auto root = std::ranges::find(zone.generators, u16{58}, &Sf2Generator::operation);
  if (root == zone.generators.end()) {
    return;
  }
  const auto coarse = std::ranges::find(zone.generators, u16{51}, &Sf2Generator::operation);
  const auto fine = std::ranges::find(zone.generators, u16{52}, &Sf2Generator::operation);
  const s32 unityCents = static_cast<s32>(root->amount) * 100 -
                         (coarse != zone.generators.end() ? static_cast<s32>(coarse->amount) * 100 : 0) -
                         (fine != zone.generators.end() ? static_cast<s32>(fine->amount) : 0);
  std::erase_if(zone.generators, [](const Sf2Generator& generator) {
    return generator.operation == 51 || generator.operation == 52 || generator.operation == 58;
  });
  zone.generators.push_back(Sf2Generator{
      .operation = 58,
      .amount = static_cast<s16>(unityCents),
  });
}

Sf2Zone readSf2Zone(const std::vector<Sf2Bag>& bags, const std::vector<Sf2Generator>& generators,
                    const std::vector<Sf2Modulator>& modulators, size_t bagIndex) {
  expect(bagIndex + 1 < bags.size(), "SF2 bag index is missing terminal bag");
  const auto& bag = bags[bagIndex];
  const auto& nextBag = bags[bagIndex + 1];
  expect(bag.generatorIndex <= nextBag.generatorIndex && nextBag.generatorIndex <= generators.size(),
         "SF2 generator index is out of range");
  expect(bag.modulatorIndex <= nextBag.modulatorIndex && nextBag.modulatorIndex <= modulators.size(),
         "SF2 modulator index is out of range");
  Sf2Zone zone{
      .generators = {generators.begin() + bag.generatorIndex, generators.begin() + nextBag.generatorIndex},
      .modulators = {modulators.begin() + bag.modulatorIndex, modulators.begin() + nextBag.modulatorIndex},
  };
  normalizeSf2ZonePitch(zone);
  sortSf2Zone(zone);
  return zone;
}

NormalizedSf2 normalizeSf2(std::span<const u8> bytes) {
  const auto root = parseRiff(bytes, "sfbk");
  validateSf2RiffStructure(root);
  const auto* phdr = firstChunk(root, "phdr");
  const auto* pbagNode = firstChunk(root, "pbag");
  const auto* pgenNode = firstChunk(root, "pgen");
  const auto* pmodNode = firstChunk(root, "pmod");
  const auto* inst = firstChunk(root, "inst");
  const auto* ibagNode = firstChunk(root, "ibag");
  const auto* igenNode = firstChunk(root, "igen");
  const auto* imodNode = firstChunk(root, "imod");
  const auto* shdr = firstChunk(root, "shdr");
  const auto* smpl = firstChunk(root, "smpl");
  expect(phdr != nullptr && pbagNode != nullptr && pgenNode != nullptr && pmodNode != nullptr && inst != nullptr &&
             ibagNode != nullptr && igenNode != nullptr && imodNode != nullptr && shdr != nullptr && smpl != nullptr,
         "SF2 file is missing required pdta/sdta chunks");

  const auto pbag = readSf2Bags(bytes, *pbagNode);
  const auto ibag = readSf2Bags(bytes, *ibagNode);
  const auto pgen = readSf2Generators(bytes, *pgenNode);
  const auto igen = readSf2Generators(bytes, *igenNode);
  const auto pmod = readSf2Modulators(bytes, *pmodNode);
  const auto imod = readSf2Modulators(bytes, *imodNode);

  expect(phdr->size % 38 == 0 && phdr->size >= 38, "SF2 phdr chunk has invalid size");
  expect(inst->size % 22 == 0 && inst->size >= 22, "SF2 inst chunk has invalid size");
  expect(shdr->size % 46 == 0 && shdr->size >= 46, "SF2 shdr chunk has invalid size");

  NormalizedSf2 normalized;
  const size_t presetRecords = (phdr->size / 38) - 1;
  for (size_t i = 0; i < presetRecords; ++i) {
    const size_t offset = phdr->dataOffset + i * 38;
    const u16 bagStart = le16At(bytes, offset + 24);
    const u16 bagEnd = le16At(bytes, offset + 38 + 24);
    expect(bagStart <= bagEnd && bagEnd < pbag.size(), "SF2 preset bag range is out of range");
    Sf2Preset preset{
        .preset = le16At(bytes, offset + 20),
        .bank = le16At(bytes, offset + 22),
    };
    for (size_t bagIndex = bagStart; bagIndex < bagEnd; ++bagIndex) {
      preset.zones.push_back(readSf2Zone(pbag, pgen, pmod, bagIndex));
    }
    normalized.presets.push_back(std::move(preset));
  }

  const size_t instrumentRecords = (inst->size / 22) - 1;
  for (size_t i = 0; i < instrumentRecords; ++i) {
    const size_t offset = inst->dataOffset + i * 22;
    const u16 bagStart = le16At(bytes, offset + 20);
    const u16 bagEnd = le16At(bytes, offset + 22 + 20);
    expect(bagStart <= bagEnd && bagEnd < ibag.size(), "SF2 instrument bag range is out of range");
    Sf2Instrument instrument;
    for (size_t bagIndex = bagStart; bagIndex < bagEnd; ++bagIndex) {
      instrument.zones.push_back(readSf2Zone(ibag, igen, imod, bagIndex));
    }
    normalized.instruments.push_back(std::move(instrument));
  }

  const size_t sampleRecords = (shdr->size / 46) - 1;
  for (size_t i = 0; i < sampleRecords; ++i) {
    const size_t offset = shdr->dataOffset + i * 46;
    const u32 start = le32At(bytes, offset + 20);
    const u32 end = le32At(bytes, offset + 24);
    const u32 loopStart = le32At(bytes, offset + 28);
    const u32 loopEnd = le32At(bytes, offset + 32);
    expect(start <= end, "SF2 sample has invalid start/end");
    const size_t sampleOffset = smpl->dataOffset + static_cast<size_t>(start) * 2;
    const size_t sampleSize = static_cast<size_t>(end - start) * 2;
    expect(sampleOffset <= bytes.size() && bytes.size() - sampleOffset >= sampleSize,
           "SF2 sample points outside smpl chunk");
    normalized.samples.push_back(Sf2Sample{
        .length = end - start,
        .loopStart = loopStart - start,
        .loopEnd = loopEnd - start,
        .sampleRate = le32At(bytes, offset + 36),
        // Every exported region has overridingRootKey, so the sample header's
        // copied root is representational rather than audible. Zone tuning is
        // normalized above; retain only the independent pitch correction here.
        .originalPitch = 0,
        .pitchCorrection = static_cast<s8>(bytes[offset + 41]),
        .sampleLink = le16At(bytes, offset + 42),
        .sampleType = le16At(bytes, offset + 44),
        .pcmHash = fnv1a(bytes.subspan(sampleOffset, sampleSize)),
    });
  }

  return normalized;
}

struct DlsConnection {
  u16 source = 0;
  u16 control = 0;
  u16 destination = 0;
  u16 transform = 0;
  s32 scale = 0;

  friend bool operator==(const DlsConnection&, const DlsConnection&) = default;
};

struct DlsRegionSummary {
  std::vector<u8> header;
  std::vector<u8> sample;
  std::vector<u8> link;
  std::vector<DlsConnection> connections;

  friend bool operator==(const DlsRegionSummary&, const DlsRegionSummary&) = default;
};

struct DlsInstrumentSummary {
  u32 bank = 0;
  u32 program = 0;
  std::vector<DlsRegionSummary> regions;

  friend bool operator==(const DlsInstrumentSummary&, const DlsInstrumentSummary&) = default;
};

struct DlsWaveSummary {
  std::vector<u8> format;
  std::vector<u8> sample;
  u32 dataSize = 0;
  u64 dataHash = 0;

  friend bool operator==(const DlsWaveSummary&, const DlsWaveSummary&) = default;
};

struct NormalizedDls {
  std::vector<DlsInstrumentSummary> instruments;
  std::vector<DlsWaveSummary> waves;

  friend bool operator==(const NormalizedDls&, const NormalizedDls&) = default;
};

std::vector<DlsConnection> readDlsConnections(std::span<const u8> bytes, const RiffNode* art) {
  if (art == nullptr) {
    return {};
  }
  expect(art->size >= 8, "DLS art chunk is too small");
  const u32 connectionCount = le32At(bytes, art->dataOffset + 4);
  expect(art->size >= 8 + static_cast<size_t>(connectionCount) * 12, "DLS art chunk is truncated");
  std::vector<DlsConnection> connections;
  for (u32 i = 0; i < connectionCount; ++i) {
    const size_t offset = art->dataOffset + 8 + static_cast<size_t>(i) * 12;
    connections.push_back(DlsConnection{
        .source = le16At(bytes, offset),
        .control = le16At(bytes, offset + 2),
        .destination = le16At(bytes, offset + 4),
        .transform = le16At(bytes, offset + 6),
        .scale = leS32At(bytes, offset + 8),
    });
  }
  std::ranges::sort(connections, [](const auto& lhs, const auto& rhs) {
    return std::tie(lhs.source, lhs.control, lhs.destination, lhs.transform, lhs.scale) <
           std::tie(rhs.source, rhs.control, rhs.destination, rhs.transform, rhs.scale);
  });
  return connections;
}

DlsRegionSummary normalizeDlsRegion(std::span<const u8> bytes, const RiffNode& regionList) {
  auto connections = readDlsConnections(bytes, childChunk(regionList, "art2"));
  if (connections.empty()) {
    connections = readDlsConnections(bytes, childChunk(regionList, "art1"));
  }
  auto sample = chunkBytes(bytes, childChunk(regionList, "wsmp"));
  if (sample.size() >= 8) {
    const s32 rootKey = static_cast<s32>(sample[4] | (sample[5] << 8));
    const s16 fineTune = static_cast<s16>(sample[6] | (sample[7] << 8));
    const s32 unityCents = rootKey * 100 - fineTune;
    const s32 canonicalRoot = static_cast<s32>(std::lround(unityCents / 100.0));
    const s16 canonicalFine = static_cast<s16>(canonicalRoot * 100 - unityCents);
    sample[4] = static_cast<u8>(canonicalRoot & 0xff);
    sample[5] = static_cast<u8>((canonicalRoot >> 8) & 0xff);
    sample[6] = static_cast<u8>(canonicalFine & 0xff);
    sample[7] = static_cast<u8>((static_cast<u16>(canonicalFine) >> 8) & 0xff);
  }
  return DlsRegionSummary{
      .header = chunkBytes(bytes, childChunk(regionList, "rgnh")),
      .sample = std::move(sample),
      .link = chunkBytes(bytes, childChunk(regionList, "wlnk")),
      .connections = std::move(connections),
  };
}

NormalizedDls normalizeDls(std::span<const u8> bytes) {
  const auto root = parseRiff(bytes, "DLS ");
  NormalizedDls normalized;

  if (const auto* lins = childList(root, "lins")) {
    for (const auto* instrumentList : childLists(*lins, "ins ")) {
      const auto* insh = childChunk(*instrumentList, "insh");
      expect(insh != nullptr && insh->size >= 12, "DLS instrument is missing insh");
      DlsInstrumentSummary instrument{
          .bank = le32At(bytes, insh->dataOffset + 4),
          .program = le32At(bytes, insh->dataOffset + 8),
      };
      if (const auto* lrgn = childList(*instrumentList, "lrgn")) {
        for (const auto& region : lrgn->children) {
          if (region.id == "LIST" && (region.type == "rgn " || region.type == "rgn2")) {
            instrument.regions.push_back(normalizeDlsRegion(bytes, region));
          }
        }
      }
      normalized.instruments.push_back(std::move(instrument));
    }
  }

  if (const auto* wvpl = childList(root, "wvpl")) {
    for (const auto* waveList : childLists(*wvpl, "wave")) {
      const auto data = chunkBytes(bytes, childChunk(*waveList, "data"));
      normalized.waves.push_back(DlsWaveSummary{
          .format = chunkBytes(bytes, childChunk(*waveList, "fmt ")),
          .sample = chunkBytes(bytes, childChunk(*waveList, "wsmp")),
          .dataSize = static_cast<u32>(data.size()),
          .dataHash = fnv1a(data),
      });
    }
  }

  return normalized;
}

std::string describeSf2Counts(const NormalizedSf2& sf2) {
  std::ostringstream out;
  out << "presets=" << sf2.presets.size() << " instruments=" << sf2.instruments.size()
      << " samples=" << sf2.samples.size();
  return out.str();
}

std::string describeSf2Zone(const Sf2Zone& zone) {
  std::ostringstream out;
  out << "gens=[";
  for (size_t i = 0; i < zone.generators.size(); ++i) {
    if (i != 0) {
      out << ", ";
    }
    out << "(" << zone.generators[i].operation << "," << zone.generators[i].amount << ")";
  }
  out << "] mods=[";
  for (size_t i = 0; i < zone.modulators.size(); ++i) {
    if (i != 0) {
      out << ", ";
    }
    const auto& modulator = zone.modulators[i];
    out << "(" << modulator.source << "," << modulator.destination << "," << modulator.amount << ","
        << modulator.amountSource << "," << modulator.transform << ")";
  }
  out << "]";
  return out.str();
}

std::string describeSf2Preset(const Sf2Preset& preset) {
  std::ostringstream out;
  out << "preset=" << preset.preset << " bank=" << preset.bank << " zones=" << preset.zones.size();
  if (!preset.zones.empty()) {
    out << " firstZone{" << describeSf2Zone(preset.zones.front()) << "}";
  }
  return out.str();
}

std::string describeSf2Instrument(const Sf2Instrument& instrument) {
  std::ostringstream out;
  out << "zones=" << instrument.zones.size();
  if (!instrument.zones.empty()) {
    out << " firstZone{" << describeSf2Zone(instrument.zones.front()) << "}";
  }
  return out.str();
}

void describeFirstZoneMismatch(std::ostream& out, std::span<const Sf2Zone> legacyZones,
                               std::span<const Sf2Zone> valueZones) {
  const size_t shared = std::min(legacyZones.size(), valueZones.size());
  for (size_t i = 0; i < shared; ++i) {
    if (!(legacyZones[i] == valueZones[i])) {
      out << "first zone mismatch at " << i << "\n";
      out << "legacy zone: " << describeSf2Zone(legacyZones[i]) << "\n";
      out << "value zone:  " << describeSf2Zone(valueZones[i]) << "\n";
      return;
    }
  }
  if (legacyZones.size() != valueZones.size()) {
    out << "zone count differs: legacy=" << legacyZones.size() << " value=" << valueZones.size() << "\n";
  }
}

std::string describeSf2Sample(const Sf2Sample& sample) {
  std::ostringstream out;
  out << "length=" << sample.length << " loop=" << sample.loopStart << "-" << sample.loopEnd
      << " rate=" << sample.sampleRate << " pitch=" << static_cast<int>(sample.originalPitch)
      << " tune=" << static_cast<int>(sample.pitchCorrection) << " type=" << sample.sampleType << " hash=0x" << std::hex
      << sample.pcmHash;
  return out.str();
}

std::string describeDlsCounts(const NormalizedDls& dls) {
  std::ostringstream out;
  out << "instruments=" << dls.instruments.size() << " waves=" << dls.waves.size();
  return out.str();
}

std::string describeBytesSummary(std::span<const u8> bytes) {
  std::ostringstream out;
  out << "size=" << bytes.size() << " hash=0x" << std::hex << fnv1a(bytes);
  if (bytes.size() <= 24) {
    out << " bytes=";
    for (const u8 byte : bytes) {
      const auto high = static_cast<u8>((byte >> 4) & 0x0f);
      const auto low = static_cast<u8>(byte & 0x0f);
      out << static_cast<char>(high < 10 ? '0' + high : 'a' + high - 10)
          << static_cast<char>(low < 10 ? '0' + low : 'a' + low - 10);
    }
  }
  return out.str();
}

std::string describeDlsConnections(std::span<const DlsConnection> connections) {
  std::ostringstream out;
  out << "[";
  for (size_t i = 0; i < connections.size(); ++i) {
    if (i != 0) {
      out << ", ";
    }
    const auto& connection = connections[i];
    out << "(" << connection.source << "," << connection.control << "," << connection.destination << ","
        << connection.transform << "," << connection.scale << ")";
  }
  out << "]";
  return out.str();
}

std::string describeDlsRegion(const DlsRegionSummary& region) {
  std::ostringstream out;
  out << "header{" << describeBytesSummary(region.header) << "} sample{" << describeBytesSummary(region.sample)
      << "} link{" << describeBytesSummary(region.link)
      << "} connections=" << describeDlsConnections(region.connections);
  return out.str();
}

std::string describeDlsInstrument(const DlsInstrumentSummary& instrument) {
  std::ostringstream out;
  out << "bank=" << instrument.bank << " program=" << instrument.program << " regions=" << instrument.regions.size();
  if (!instrument.regions.empty()) {
    out << " firstRegion{" << describeDlsRegion(instrument.regions.front()) << "}";
  }
  return out.str();
}

void describeFirstDlsRegionMismatch(std::ostream& out, std::span<const DlsRegionSummary> legacyRegions,
                                    std::span<const DlsRegionSummary> valueRegions) {
  const size_t shared = std::min(legacyRegions.size(), valueRegions.size());
  for (size_t i = 0; i < shared; ++i) {
    if (!(legacyRegions[i] == valueRegions[i])) {
      out << "first region mismatch at " << i << "\n";
      out << "legacy region: " << describeDlsRegion(legacyRegions[i]) << "\n";
      out << "value region:  " << describeDlsRegion(valueRegions[i]) << "\n";
      return;
    }
  }
  if (legacyRegions.size() != valueRegions.size()) {
    out << "region count differs: legacy=" << legacyRegions.size() << " value=" << valueRegions.size() << "\n";
  }
}

std::string describeDlsWave(const DlsWaveSummary& wave) {
  std::ostringstream out;
  out << "format{" << describeBytesSummary(wave.format) << "} sample{" << describeBytesSummary(wave.sample)
      << "} dataSize=" << wave.dataSize << " dataHash=0x" << std::hex << wave.dataHash;
  return out.str();
}

bool compareSf2(std::span<const u8> legacyBytes, std::span<const u8> valueBytes, std::ostream& out) {
  const auto legacy = normalizeSf2(legacyBytes);
  const auto value = normalizeSf2(valueBytes);
  if (legacy == value) {
    out << "SF2 parity ok: " << describeSf2Counts(legacy) << "\n";
    return true;
  }

  out << "SF2 parity mismatch\n";
  out << "legacy: " << describeSf2Counts(legacy) << "\n";
  out << "value:  " << describeSf2Counts(value) << "\n";
  if (legacy.presets != value.presets) {
    out << "preset structures differ\n";
    const size_t shared = std::min(legacy.presets.size(), value.presets.size());
    for (size_t i = 0; i < shared; ++i) {
      if (!(legacy.presets[i] == value.presets[i])) {
        out << "first preset mismatch at " << i << "\n";
        out << "legacy: " << describeSf2Preset(legacy.presets[i]) << "\n";
        out << "value:  " << describeSf2Preset(value.presets[i]) << "\n";
        describeFirstZoneMismatch(out, legacy.presets[i].zones, value.presets[i].zones);
        break;
      }
    }
  } else if (legacy.instruments != value.instruments) {
    out << "instrument zones/generators/modulators differ\n";
    const size_t shared = std::min(legacy.instruments.size(), value.instruments.size());
    for (size_t i = 0; i < shared; ++i) {
      if (!(legacy.instruments[i] == value.instruments[i])) {
        out << "first instrument mismatch at " << i << "\n";
        out << "legacy: " << describeSf2Instrument(legacy.instruments[i]) << "\n";
        out << "value:  " << describeSf2Instrument(value.instruments[i]) << "\n";
        describeFirstZoneMismatch(out, legacy.instruments[i].zones, value.instruments[i].zones);
        break;
      }
    }
  } else if (legacy.samples != value.samples) {
    out << "sample headers or PCM hashes differ\n";
    const size_t shared = std::min(legacy.samples.size(), value.samples.size());
    for (size_t i = 0; i < shared; ++i) {
      if (!(legacy.samples[i] == value.samples[i])) {
        out << "first sample mismatch at " << i << "\n";
        out << "legacy: " << describeSf2Sample(legacy.samples[i]) << "\n";
        out << "value:  " << describeSf2Sample(value.samples[i]) << "\n";
        break;
      }
    }
  }
  return false;
}

bool compareDls(std::span<const u8> legacyBytes, std::span<const u8> valueBytes, std::ostream& out) {
  const auto legacy = normalizeDls(legacyBytes);
  const auto value = normalizeDls(valueBytes);
  if (legacy == value) {
    out << "DLS parity ok: " << describeDlsCounts(legacy) << "\n";
    return true;
  }

  out << "DLS parity mismatch\n";
  out << "legacy: " << describeDlsCounts(legacy) << "\n";
  out << "value:  " << describeDlsCounts(value) << "\n";
  if (legacy.instruments != value.instruments) {
    out << "instrument regions/articulations differ\n";
    const size_t shared = std::min(legacy.instruments.size(), value.instruments.size());
    for (size_t i = 0; i < shared; ++i) {
      if (!(legacy.instruments[i] == value.instruments[i])) {
        out << "first instrument mismatch at " << i << "\n";
        out << "legacy: " << describeDlsInstrument(legacy.instruments[i]) << "\n";
        out << "value:  " << describeDlsInstrument(value.instruments[i]) << "\n";
        describeFirstDlsRegionMismatch(out, legacy.instruments[i].regions, value.instruments[i].regions);
        break;
      }
    }
  } else if (legacy.waves != value.waves) {
    out << "wave format/sample data differ\n";
    const size_t shared = std::min(legacy.waves.size(), value.waves.size());
    for (size_t i = 0; i < shared; ++i) {
      if (!(legacy.waves[i] == value.waves[i])) {
        out << "first wave mismatch at " << i << "\n";
        out << "legacy: " << describeDlsWave(legacy.waves[i]) << "\n";
        out << "value:  " << describeDlsWave(value.waves[i]) << "\n";
        break;
      }
    }
  }
  return false;
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

struct NormalizedMidi {
  std::vector<NormalizedMidiEvent> events;
  std::vector<u64> endOfTrackTicks;
};

bool normalizedMidiEventLess(const NormalizedMidiEvent& lhs, const NormalizedMidiEvent& rhs) {
  return std::tie(lhs.track, lhs.tick, lhs.kind, lhs.channel, lhs.a, lhs.b, lhs.c, lhs.text) <
         std::tie(rhs.track, rhs.tick, rhs.kind, rhs.channel, rhs.a, rhs.b, rhs.c, rhs.text);
}

struct MidiCompareOptions {
  bool useSharedPlayOnceHorizon = false;
};

struct ParitySuite {
  std::string_view format;
  std::string_view label;
  bool filterCollectionsByFormat = false;
  MidiExportOptions midi;
  ModulationScalingPolicy modulationScaling = ModulationScalingPolicy::FullFormatRange;
  MidiCompareOptions midiComparison;
};

constexpr ParitySuite kCapcomSnesSuite{
    .format = "CapcomSnes",
    .label = "CapcomSnes",
    .midiComparison = {.useSharedPlayOnceHorizon = true},
};

constexpr ParitySuite kKonamiSnesSuite{
    .format = "KonamiSnes",
    .label = "KonamiSnes",
    .midiComparison = {.useSharedPlayOnceHorizon = true},
};

constexpr ParitySuite kAkaoSnesSuite{
    .format = "AkaoSnes",
    .label = "AkaoSnes",
    .filterCollectionsByFormat = true,
    .midi = {.writePortMetaEvents = false},
    .midiComparison = {.useSharedPlayOnceHorizon = true},
};

constexpr ParitySuite kAkaoSuite{
    .format = "Akao",
    .label = "Akao",
    .filterCollectionsByFormat = true,
    .midiComparison = {.useSharedPlayOnceHorizon = true},
};

constexpr ParitySuite kNdsSuite{
    .format = "NDS",
    .label = "NDS",
    .midiComparison = {.useSharedPlayOnceHorizon = true},
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

NormalizedMidi normalizeMidi(std::span<const u8> bytes) {
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

  NormalizedMidi normalized;
  auto& events = normalized.events;
  normalized.endOfTrackTicks.resize(trackCount, std::numeric_limits<u64>::max());

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
        const auto payload = reader.bytes(static_cast<size_t>(length), trackEnd);
        if (type == 0x2f) {
          normalized.endOfTrackTicks[track] = tick;
        }
        addMetaEvent(events, track, tick, type, std::move(payload));
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
    expect(normalized.endOfTrackTicks[track] != std::numeric_limits<u64>::max(),
           "MIDI track is missing its End-of-Track event");
    reader.skip(trackEnd - reader.position(), bytes.size());
  }

  expect(reader.empty(), "MIDI has trailing bytes after declared tracks");

  std::ranges::sort(events, normalizedMidiEventLess);
  return normalized;
}

struct SimulatedModulationStats {
  size_t pitchBendCount = 0;
  size_t nonCenterPitchBendCount = 0;
  u32 maxAbsPitchBend = 0;
  u32 maxPitchBendRangeSemitones = 2;
  double maxAbsPitchBendSemitones = 0.0;
  size_t redundantPitchBendCount = 0;
  size_t vibratoControllerCount = 0;
};

struct PerformanceModulationStats {
  size_t noteEvents = 0;
  size_t drumBankNoteEvents = 0;
  size_t melodicBankNoteEvents = 0;
  size_t drumBankInstrumentEvents = 0;
  size_t melodicInstrumentEvents = 0;
  u64 firstDrumBankNoteTick = 0;
  u64 lastDrumBankNoteTick = 0;
  u64 lastNoteTick = 0;
  size_t vibratoDepthEvents = 0;
  size_t activeVibratoDepthEvents = 0;
  size_t vibratoRateEvents = 0;
  size_t vibratoDelayEvents = 0;
  size_t activeVibratoDelayEvents = 0;
  size_t sourcePitchBendEvents = 0;
  size_t nonZeroSourcePitchBendEvents = 0;
  double maxVibratoPitchDepthSemitones = 0.0;
  double maxVibratoNormalizedAmount = 0.0;
  double maxVibratoObservedRangeAmount = 0.0;
  double maxVibratoRateNormalizedAmount = 0.0;
  double maxVibratoRateObservedRangeAmount = 0.0;
  double maxVibratoRateHz = 0.0;
  double maxSourcePitchBendSemitones = 0.0;
  u32 maxVibratoDelayTicks = 0;
  std::string maxVibratoDepthLocation;
  std::string maxSourcePitchBendLocation;
};

SimulatedModulationStats simulatedModulationStats(std::span<const u8> midiBytes) {
  SimulatedModulationStats stats;
  MidiReader reader(midiBytes);
  expect(reader.ascii(4, midiBytes.size()) == "MThd", "MIDI missing MThd header");
  const u32 headerLength = reader.be32(midiBytes.size());
  expect(headerLength >= 6, "MIDI header is too short");
  const size_t headerEnd = reader.position() + headerLength;
  expect(headerEnd <= midiBytes.size(), "MIDI header extends past end of file");
  static_cast<void>(reader.be16(headerEnd));
  const u16 trackCount = reader.be16(headerEnd);
  static_cast<void>(reader.be16(headerEnd));
  reader.skip(headerEnd - reader.position(), midiBytes.size());

  struct ChannelState {
    u8 rpnMsb = 0x7f;
    u8 rpnLsb = 0x7f;
    u8 pitchBendRangeSemitones = 2;
    u8 pitchBendRangeFineCents = 0;
    u16 pitchBendRangeCents = 200;
    std::optional<u32> lastPitchBend;
  };
  std::map<std::tuple<u32, u8>, ChannelState> channelStates;

  for (u32 track = 0; track < trackCount; ++track) {
    expect(reader.ascii(4, midiBytes.size()) == "MTrk", "MIDI missing MTrk header");
    const u32 trackLength = reader.be32(midiBytes.size());
    const size_t trackEnd = reader.position() + trackLength;
    expect(trackEnd <= midiBytes.size(), "MIDI track extends past end of file");

    std::optional<u8> runningStatus;
    while (reader.position() < trackEnd) {
      static_cast<void>(reader.variableLength(trackEnd));
      u8 status = reader.readU8(trackEnd);
      std::optional<u8> firstDataByte;
      if (status < 0x80) {
        if (!runningStatus) {
          throw std::runtime_error("MIDI running status used before status byte");
        }
        firstDataByte = status;
        status = *runningStatus;
      } else if (status < 0xf0) {
        runningStatus = status;
      } else {
        runningStatus.reset();
      }

      if (status == 0xff || status == 0xf0 || status == 0xf7) {
        if (status == 0xff) {
          static_cast<void>(reader.readU8(trackEnd));
        }
        const u64 length = reader.variableLength(trackEnd);
        if (length > std::numeric_limits<size_t>::max()) {
          throw std::runtime_error("MIDI event is too large");
        }
        static_cast<void>(reader.bytes(static_cast<size_t>(length), trackEnd));
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
      auto& state = channelStates[std::tuple{track, channel}];

      if (command == 0xb0) {
        if (data1 == 1 || data1 == 76 || data1 == 78) {
          ++stats.vibratoControllerCount;
        }
        if (data1 == 101) {
          state.rpnMsb = data2;
        } else if (data1 == 100) {
          state.rpnLsb = data2;
        } else if ((data1 == 6 || data1 == 38) && state.rpnMsb == 0 && state.rpnLsb == 0) {
          if (data1 == 6) {
            state.pitchBendRangeSemitones = data2;
          } else {
            state.pitchBendRangeFineCents = data2;
          }
          state.pitchBendRangeCents =
              std::max<u16>(200, static_cast<u16>(state.pitchBendRangeSemitones) * 100 + state.pitchBendRangeFineCents);
          stats.maxPitchBendRangeSemitones = std::max<u32>(
              stats.maxPitchBendRangeSemitones, static_cast<u32>(std::ceil(state.pitchBendRangeCents / 100.0)));
        }
      } else if (command == 0xe0) {
        ++stats.pitchBendCount;
        const u32 unsignedBend = static_cast<u32>(data1 | (data2 << 7));
        const s32 signedBend = static_cast<s32>(unsignedBend) - 8192;
        const u32 absBend = static_cast<u32>(std::abs(signedBend));
        stats.maxAbsPitchBend = std::max(stats.maxAbsPitchBend, absBend);
        stats.maxAbsPitchBendSemitones = std::max(
            stats.maxAbsPitchBendSemitones, (static_cast<double>(absBend) * state.pitchBendRangeCents) / 819200.0);
        if (signedBend != 0) {
          ++stats.nonCenterPitchBendCount;
        }
        if (state.lastPitchBend && *state.lastPitchBend == unsignedBend) {
          ++stats.redundantPitchBendCount;
        }
        state.lastPitchBend = unsignedBend;
      }
    }

    reader.skip(trackEnd - reader.position(), midiBytes.size());
  }

  expect(reader.empty(), "MIDI has trailing bytes after declared tracks");
  return stats;
}

std::string performanceEventLocation(const SequenceProgram& program, const PerformanceEventHeader& header) {
  std::ostringstream out;
  out << "track=" << header.track.value << " tick=" << header.tick;
  const auto* command = sourceCommandForEvent(program, header);
  if (command != nullptr) {
    out << " addr=0x" << std::hex << command->address.value << std::dec << " opcode=0x" << std::hex
        << static_cast<int>(command->opcode) << std::dec;
  }
  return out.str();
}

PerformanceModulationStats performanceModulationStats(const SequenceProgram& program,
                                                      const SequenceDialectRegistry& dialects, u32 sequenceLoops) {
  const auto* dialect = dialects.find(program.dialect.value);
  if (dialect == nullptr) {
    throw std::runtime_error("No sequence dialect registered for '" + program.dialect.value + "'");
  }

  const PerformanceSequence performance = SequenceVm(SequenceVmOptions{
                                                         .loopPolicy = LoopPolicy::PlayOnce,
                                                         .sequenceLoops = sequenceLoops,
                                                     })
                                              .render(program, *dialect);
  if (!performance.diagnostics.empty()) {
    throw std::runtime_error("performance render reported: " + performance.diagnostics.front().message);
  }

  PerformanceModulationStats stats;
  struct InstrumentState {
    u32 bank = 0;
    u32 program = 0;
  };
  std::map<u32, InstrumentState> instruments;
  for (const auto& track : performance.tracks) {
    auto& instrument = instruments[track.id.value];
    for (const auto& event : track.events) {
      if (const auto* note = std::get_if<NotePerformanceEvent>(&event)) {
        ++stats.noteEvents;
        stats.lastNoteTick = std::max(stats.lastNoteTick, note->header.tick);
        if (instrument.bank == (0x7f << 7) && instrument.program == 0) {
          ++stats.drumBankNoteEvents;
          if (stats.drumBankNoteEvents == 1) {
            stats.firstDrumBankNoteTick = note->header.tick;
          }
          stats.lastDrumBankNoteTick = std::max(stats.lastDrumBankNoteTick, note->header.tick);
        } else {
          ++stats.melodicBankNoteEvents;
        }
      } else if (const auto* instrumentEvent = std::get_if<InstrumentPerformanceEvent>(&event)) {
        const std::optional<InstrumentAddress> explicitAddress = instrumentEvent->sourceInstrument
                                                                     ? std::nullopt
                                                                     : std::optional{InstrumentAddress{
                                                                           .bank = instrumentEvent->bank,
                                                                           .program = instrumentEvent->program,
                                                                       }};
        const InstrumentAddress address = resolveInstrumentAddress(explicitAddress, instrumentEvent->sourceInstrument);
        instrument.bank = address.bank;
        instrument.program = address.program;
        if (instrument.bank == (0x7f << 7) && instrument.program == 0) {
          ++stats.drumBankInstrumentEvents;
        } else {
          ++stats.melodicInstrumentEvents;
        }
      } else if (const auto* pitchBend = std::get_if<PitchBendPerformanceEvent>(&event)) {
        ++stats.sourcePitchBendEvents;
        const double semitones = std::abs(pitchBend->semitones);
        if (semitones > 0.0001) {
          ++stats.nonZeroSourcePitchBendEvents;
        }
        if (semitones > stats.maxSourcePitchBendSemitones) {
          stats.maxSourcePitchBendSemitones = semitones;
          stats.maxSourcePitchBendLocation = performanceEventLocation(program, pitchBend->header);
        }
      } else if (const auto* delay = std::get_if<VibratoDelayPerformanceEvent>(&event)) {
        ++stats.vibratoDelayEvents;
        stats.maxVibratoDelayTicks = std::max(stats.maxVibratoDelayTicks, delay->delayTicks);
        if (delay->delayTicks > 0) {
          ++stats.activeVibratoDelayEvents;
        }
      } else if (const auto* modulation = std::get_if<ModulationPerformanceEvent>(&event)) {
        if (modulation->target == ModulationPerformanceTarget::VibratoDepth) {
          ++stats.vibratoDepthEvents;
          if (modulation->amount > 0.0001) {
            ++stats.activeVibratoDepthEvents;
          }
          stats.maxVibratoNormalizedAmount = std::max(stats.maxVibratoNormalizedAmount, modulation->amount);
          if (modulation->controllerRangeMaxAmount) {
            stats.maxVibratoObservedRangeAmount =
                std::max(stats.maxVibratoObservedRangeAmount, *modulation->controllerRangeMaxAmount);
          }
          const double semitones = modulation->pitchDepthSemitones.value_or(0.0);
          if (semitones > stats.maxVibratoPitchDepthSemitones) {
            stats.maxVibratoPitchDepthSemitones = semitones;
            stats.maxVibratoDepthLocation = performanceEventLocation(program, modulation->header);
          }
        } else if (modulation->target == ModulationPerformanceTarget::VibratoRate) {
          ++stats.vibratoRateEvents;
          stats.maxVibratoRateNormalizedAmount = std::max(stats.maxVibratoRateNormalizedAmount, modulation->amount);
          if (modulation->controllerRangeMaxAmount) {
            stats.maxVibratoRateObservedRangeAmount =
                std::max(stats.maxVibratoRateObservedRangeAmount, *modulation->controllerRangeMaxAmount);
          }
          if (modulation->frequencyHz) {
            stats.maxVibratoRateHz = std::max(stats.maxVibratoRateHz, *modulation->frequencyHz);
          }
        }
      }
    }
  }

  return stats;
}

std::map<std::string, PerformanceModulationStats> valueFormatPerformanceModulationStats(
    const std::filesystem::path& path, std::string_view formatName, std::string_view label, u32 sequenceLoops) {
  Session session;
  vgmtrans::formats::registerValueFormats(session);
  session.addSource(SourceFile{.name = path.filename().string(), .path = path}, readFile(path));

  session.scanPendingSources();

  const SessionSnapshot project = session.snapshot();
  if (project.collections().empty()) {
    std::ostringstream message;
    message << "value scanner did not discover " << label << " performance collections";
    if (!project.diagnostics().empty()) {
      message << ": " << project.diagnostics().front().message;
    }
    throw std::runtime_error(message.str());
  }

  std::map<std::string, PerformanceModulationStats> statsByCollection;
  for (const auto& collection : project.collections()) {
    if (!valueCollectionHasSequenceFormat(project, collection, formatName)) {
      continue;
    }
    const auto* sequence = project.asset<SequenceProgramAsset>(*collection.sequence);
    const std::string key = valueMidiCollectionKey(project, collection);
    auto [_, inserted] = statsByCollection.emplace(
        key, performanceModulationStats(sequence->program, session.dialects(), sequenceLoops));
    if (!inserted) {
      throw std::runtime_error("duplicate " + std::string(label) + " performance collection key: " + key);
    }
  }
  if (statsByCollection.empty()) {
    throw std::runtime_error("value scanner did not discover " + std::string(label) + " performance collections");
  }
  return statsByCollection;
}

int validateFormatDirectMidiSimulation(const std::filesystem::path& path, std::string_view formatName,
                                       std::string_view label, u32 sequenceLoops = 0) {
  const auto valueMidis = valueFormatCollectionMidis(path, formatName, label, sequenceLoops,
                                                     ModulationConversionPolicy::SequenceEventSimulation);
  const auto performanceStats = valueFormatPerformanceModulationStats(path, formatName, label, sequenceLoops);
  if (valueMidis.empty()) {
    std::cout << "value " << label << " simulation scan did not produce MIDI collections\n";
    return 1;
  }

  for (const auto& [collectionName, midi] : valueMidis) {
    const auto stats = simulatedModulationStats(midi);
    const auto performanceFound = performanceStats.find(collectionName);
    if (performanceFound == performanceStats.end()) {
      std::cout << "value " << label << " simulation scan did not produce performance stats for '" << collectionName
                << "'\n";
      return 1;
    }
    const auto& performance = performanceFound->second;
    std::cout << "checking " << collectionName << " SequenceEventSimulation MIDI: pitchBends=" << stats.pitchBendCount
              << " nonCenter=" << stats.nonCenterPitchBendCount << " maxAbs=" << stats.maxAbsPitchBend
              << " maxRange=" << stats.maxPitchBendRangeSemitones << " maxSemitones=" << stats.maxAbsPitchBendSemitones
              << "\n";
    std::cout << "  performance notes: notes=" << performance.noteEvents
              << " drumBankNotes=" << performance.drumBankNoteEvents
              << " melodicBankNotes=" << performance.melodicBankNoteEvents
              << " drumBankPrograms=" << performance.drumBankInstrumentEvents
              << " melodicPrograms=" << performance.melodicInstrumentEvents
              << " firstDrumTick=" << performance.firstDrumBankNoteTick
              << " lastDrumTick=" << performance.lastDrumBankNoteTick << " lastNoteTick=" << performance.lastNoteTick
              << "\n";
    std::cout << "  performance vibrato: depthEvents=" << performance.vibratoDepthEvents
              << " activeDepthEvents=" << performance.activeVibratoDepthEvents
              << " maxDepthSemi=" << performance.maxVibratoPitchDepthSemitones
              << " maxDepthCents=" << (performance.maxVibratoPitchDepthSemitones * 100.0)
              << " maxAmount=" << performance.maxVibratoNormalizedAmount
              << " observedRangeAmount=" << performance.maxVibratoObservedRangeAmount
              << " rateEvents=" << performance.vibratoRateEvents << " maxRateHz=" << performance.maxVibratoRateHz
              << " maxRateAmount=" << performance.maxVibratoRateNormalizedAmount
              << " observedRateRangeAmount=" << performance.maxVibratoRateObservedRangeAmount
              << " delayEvents=" << performance.vibratoDelayEvents
              << " activeDelays=" << performance.activeVibratoDelayEvents
              << " maxDelayTicks=" << performance.maxVibratoDelayTicks
              << " sourcePitchBends=" << performance.sourcePitchBendEvents
              << " nonZeroSourcePitchBends=" << performance.nonZeroSourcePitchBendEvents
              << " maxSourcePitchSemi=" << performance.maxSourcePitchBendSemitones << "\n";
    if (!performance.maxVibratoDepthLocation.empty()) {
      std::cout << "  max vibrato depth at " << performance.maxVibratoDepthLocation << "\n";
    }
    if (!performance.maxSourcePitchBendLocation.empty()) {
      std::cout << "  max source pitch bend at " << performance.maxSourcePitchBendLocation << "\n";
    }
    if (stats.vibratoControllerCount != 0) {
      std::cout << "sequence-event simulation leaked " << stats.vibratoControllerCount
                << " synth vibrato controller events\n";
      return 1;
    }
    if (performance.activeVibratoDepthEvents != 0 && performance.maxVibratoPitchDepthSemitones <= 0.0) {
      std::cout << label << " performance emitted active vibrato without a physical pitch depth\n";
      return 1;
    }
    if (performance.activeVibratoDepthEvents != 0 && performance.maxVibratoRateHz <= 0.0) {
      std::cout << label << " performance emitted active vibrato without a physical rate\n";
      return 1;
    }
    if (performance.activeVibratoDepthEvents != 0 && stats.nonCenterPitchBendCount == 0) {
      std::cout << "sequence-event simulation did not produce non-center pitch bends\n";
      return 1;
    }
    if (stats.redundantPitchBendCount != 0) {
      std::cout << "sequence-event simulation wrote " << stats.redundantPitchBendCount
                << " redundant pitch bend events\n";
      return 1;
    }
  }

  std::cout << label << " direct MIDI simulation sanity ok: collections=" << valueMidis.size()
            << " loops=" << sequenceLoops << "\n";
  return 0;
}

int validateKonamiSnesDirectMidiSimulation(const std::filesystem::path& path, u32 sequenceLoops = 0) {
  return validateFormatDirectMidiSimulation(path, "KonamiSnes", "KonamiSnes", sequenceLoops);
}

int validateCapcomSnesDirectMidiSimulation(const std::filesystem::path& path, u32 sequenceLoops = 0) {
  return validateFormatDirectMidiSimulation(path, "CapcomSnes", "CapcomSnes", sequenceLoops);
}

int validateAkaoSnesDirectMidiSimulation(const std::filesystem::path& path, u32 sequenceLoops = 0) {
  return validateFormatDirectMidiSimulation(path, "AkaoSnes", "AkaoSnes", sequenceLoops);
}

MidiCompareOptions sharedPlayOnceHorizonOptions() {
  return MidiCompareOptions{
      .useSharedPlayOnceHorizon = true,
  };
}

std::vector<NormalizedMidiEvent> eventsBeforeHorizons(const std::vector<NormalizedMidiEvent>& events,
                                                      std::span<const u64> horizons) {
  // End-of-Track is the exclusive end of the audible play-once interval.
  // Controller setup stamped exactly at that boundary belongs to no rendered
  // time; notes crossing it are clipped so their audible portions still compare.
  std::vector<NormalizedMidiEvent> result;
  result.reserve(events.size());
  for (const auto& event : events) {
    if (event.track >= horizons.size() || event.tick >= horizons[event.track]) {
      continue;
    }

    NormalizedMidiEvent clipped = event;
    if (clipped.kind == "note" && clipped.c > horizons[event.track] - clipped.tick) {
      clipped.c = static_cast<u32>(horizons[event.track] - clipped.tick);
    }
    result.push_back(std::move(clipped));
  }
  return result;
}

bool compareMidi(std::span<const u8> legacyBytes, std::span<const u8> valueBytes, std::ostream& out,
                 MidiCompareOptions options = {}) {
  const auto legacyMidi = normalizeMidi(legacyBytes);
  const auto valueMidi = normalizeMidi(valueBytes);
  const auto& fullLegacy = legacyMidi.events;
  const auto& fullValue = valueMidi.events;
  if (fullLegacy == fullValue) {
    out << "MIDI parity ok: " << fullLegacy.size() << " normalized events\n";
    return true;
  }

  std::vector<NormalizedMidiEvent> horizonLegacy;
  std::vector<NormalizedMidiEvent> horizonValue;
  bool horizonApplied = false;
  if (options.useSharedPlayOnceHorizon && legacyMidi.endOfTrackTicks.size() == valueMidi.endOfTrackTicks.size()) {
    horizonApplied = true;
    std::vector<u64> horizons(legacyMidi.endOfTrackTicks.size());
    std::ranges::transform(legacyMidi.endOfTrackTicks, valueMidi.endOfTrackTicks, horizons.begin(),
                           [](u64 legacyEnd, u64 valueEnd) { return std::min(legacyEnd, valueEnd); });
    horizonLegacy = eventsBeforeHorizons(fullLegacy, horizons);
    horizonValue = eventsBeforeHorizons(fullValue, horizons);
    if (horizonLegacy == horizonValue) {
      out << "MIDI parity ok within shared play-once horizon: " << horizonLegacy.size() << " normalized events"
          << " (outside horizon: legacy=" << fullLegacy.size() - horizonLegacy.size()
          << " value=" << fullValue.size() - horizonValue.size() << ")\n";
      return true;
    }
  }

  const auto& legacy = horizonApplied ? horizonLegacy : fullLegacy;
  const auto& value = horizonApplied ? horizonValue : fullValue;
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

using MidiCollectionMap = std::map<std::string, std::vector<u8>>;
using SummaryCollectionMap = std::map<std::string, CapcomSnesSummary>;
using SynthCollectionMap = std::map<std::string, SynthExportBytes>;

int runMidiParity(const ParitySuite& suite, const MidiCollectionMap& legacy, const MidiCollectionMap& value,
                  u32 sequenceLoops = 0) {
  if (value.size() != legacy.size()) {
    std::cout << suite.label << " MIDI collection count differs: legacy=" << legacy.size() << " value=" << value.size()
              << "\n";
    return 1;
  }

  const MidiCompareOptions compareOptions = sequenceLoops == 0 ? suite.midiComparison : MidiCompareOptions{};
  for (const auto& [collectionName, legacyMidi] : legacy) {
    const auto found = value.find(collectionName);
    if (found == value.end()) {
      std::cout << "value " << suite.label << " scan did not produce MIDI for collection '" << collectionName << "'\n";
      return 1;
    }

    std::cout << "checking " << collectionName << " MIDI via direct " << suite.label
              << " value scan, loops=" << sequenceLoops << "\n";
    if (!compareMidi(legacyMidi, found->second, std::cout, compareOptions)) {
      return 1;
    }
  }

  std::cout << suite.label << " direct MIDI parity ok: collections=" << legacy.size() << " loops=" << sequenceLoops
            << "\n";
  return 0;
}

int runSummaryParity(const ParitySuite& suite, const SummaryCollectionMap& legacy, const SummaryCollectionMap& value) {
  if (value.size() != legacy.size()) {
    std::cout << suite.label << " collection count differs: legacy=" << legacy.size() << " value=" << value.size()
              << "\n"
              << "legacy collections: " << describeMapKeys(legacy) << "\n"
              << "value collections: " << describeMapKeys(value) << "\n";
    return 1;
  }

  for (const auto& [collectionName, legacySummary] : legacy) {
    const auto found = value.find(collectionName);
    if (found == value.end()) {
      std::cout << "value " << suite.label << " scan did not produce collection '" << collectionName << "'\n";
      return 1;
    }

    std::cout << "checking " << collectionName << " via direct " << suite.label << " value summary\n";
    if (!compareSummary(legacySummary, found->second, std::cout, suite.label)) {
      return 1;
    }
  }

  std::cout << suite.label << " direct summary parity ok: collections=" << legacy.size() << "\n";
  return 0;
}

int runSynthParity(const ParitySuite& suite, const SynthCollectionMap& legacy, const SynthCollectionMap& value) {
  if (value.size() != legacy.size()) {
    std::cout << suite.label << " synth collection count differs: legacy=" << legacy.size() << " value=" << value.size()
              << "\n";
    return 1;
  }

  for (const auto& [collectionName, legacyExport] : legacy) {
    const auto found = value.find(collectionName);
    if (found == value.end()) {
      std::cout << "value " << suite.label << " scan did not produce synth exports for collection '" << collectionName
                << "'\n";
      return 1;
    }

    std::cout << "checking " << collectionName << " SF2 via direct " << suite.label << " value scan\n";
    if (!compareSf2(legacyExport.sf2, found->second.sf2, std::cout)) {
      return 1;
    }
    std::cout << "checking " << collectionName << " DLS via direct " << suite.label << " value scan\n";
    if (!compareDls(legacyExport.dls, found->second.dls, std::cout)) {
      return 1;
    }
  }

  std::cout << suite.label << " direct SF2/DLS parity ok: collections=" << legacy.size() << "\n";
  return 0;
}

SummaryCollectionMap legacySummariesForSuite(const std::filesystem::path& path, const ParitySuite& suite) {
  if (suite.filterCollectionsByFormat) {
    return legacyFormatCollectionSummaries(path, suite.format, suite.label);
  }
  return legacyCollectionSummaries(path);
}

SummaryCollectionMap valueSummariesForSuite(const std::filesystem::path& path, const ParitySuite& suite) {
  if (suite.filterCollectionsByFormat) {
    return valueFormatCollectionSummaries(path, suite.format, suite.label);
  }
  return valueCollectionSummaries(path);
}

MidiCollectionMap legacyMidisForSuite(const std::filesystem::path& path, const ParitySuite& suite, u32 sequenceLoops) {
  if (suite.filterCollectionsByFormat) {
    return legacyFormatCollectionMidis(path, suite.format, suite.label, sequenceLoops);
  }
  return legacyCollectionMidis(path, sequenceLoops);
}

MidiCollectionMap valueMidisForSuite(const std::filesystem::path& path, const ParitySuite& suite, u32 sequenceLoops) {
  if (suite.filterCollectionsByFormat) {
    return valueFormatCollectionMidis(path, suite.format, suite.label, sequenceLoops,
                                      ModulationConversionPolicy::SynthModulators, suite.midi, suite.modulationScaling);
  }
  return valueCollectionMidis(path, sequenceLoops, ModulationConversionPolicy::SynthModulators, suite.midi,
                              suite.modulationScaling);
}

SynthCollectionMap legacySynthsForSuite(const std::filesystem::path& path, const ParitySuite& suite) {
  if (suite.filterCollectionsByFormat) {
    return legacyFormatCollectionSynthExports(path, suite.format, suite.label);
  }
  return legacyCollectionSynthExports(path);
}

SynthCollectionMap valueSynthsForSuite(const std::filesystem::path& path, const ParitySuite& suite) {
  if (suite.filterCollectionsByFormat) {
    return valueFormatCollectionSynthExports(path, suite.format, suite.label);
  }
  return valueCollectionSynthExports(path);
}

class LegacyLevelPrecisionTrack final : public SeqTrack {
public:
  explicit LegacyLevelPrecisionTrack(VGMSeq* parentFile, u32 offset, u32 length)
      : SeqTrack(parentFile, offset, length, "LegacyLevelPrecisionTrack") {}

  bool readEvent() override {
    constexpr double kPreciseExpression = 0.93039;
    addExpressionNoItem(kPreciseExpression, Resolution::SevenBit);
    addPanNoItem(0);
    addTime(1);
    addEndOfTrackNoItem();
    curOffset = offset() + length();
    return false;
  }
};

class LegacyLevelPrecisionSeq final : public VGMSeq {
public:
  explicit LegacyLevelPrecisionSeq(RawFile* file) : VGMSeq("LegacyLevelPrecision", file, 0, 1, "LegacyLevelPrecision") {
    setUseLinearAmplitudeScale(true);
    setUseLinearPanAmplitudeScale(PanVolumeCorrectionMode::kAdjustExpressionController);
    setPPQN(48);
  }

  void prepare() {
    clearTracks();
    addTrack<LegacyLevelPrecisionTrack>(this, 0, 1);
    nNumTracks = static_cast<u32>(trackCount());
  }
};

void legacyLevelReapplicationKeepsPreCurvePrecision() {
  std::array<u8, 1> bytes{};
  VirtFile raw(bytes.data(), static_cast<u32>(bytes.size()), "legacy-level-precision.bin");
  LegacyLevelPrecisionSeq sequence(&raw);
  sequence.prepare();

  auto midi = sequence.convertToMidi(nullptr);
  expect(midi != nullptr, "legacy precision fixture should convert to MIDI");

  std::vector<u8> midiBytes;
  midi->writeMidiToBuffer(midiBytes);
  const auto normalized = normalizeMidi(midiBytes);

  int expressionEvents = 0;
  for (const auto& event : normalized.events) {
    if (event.kind == "control" && event.a == 11) {
      ++expressionEvents;
      expect(event.b == 123, "legacy expression reapplication should apply the curve before 7-bit quantization");
    }
  }

  expect(expressionEvents == 2, "legacy precision fixture should emit original and reapplied expression events");
}

int selfTest() {
  const MidiSequence midiSequence{
      .timebase = Timebase{.ppqn = 48},
      .tracks = {vgmtrans::core::MidiTrack{
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

  const auto midi = encodeMidiFile(midiSequence);
  const auto normalized = normalizeMidi(midi);

  expect(std::ranges::any_of(
             normalized.events,
             [](const auto& event) { return event.kind == "tempo" && event.tick == 0 && event.a == 500000; }),
         "self-test should normalize tempo events");
  expect(std::ranges::any_of(
             normalized.events,
             [](const auto& event) { return event.kind == "program" && event.channel == 2 && event.a == 12; }),
         "self-test should normalize program changes");
  expect(std::ranges::any_of(normalized.events,
                             [](const auto& event) {
                               return event.kind == "note" && event.tick == 24 && event.channel == 2 && event.a == 64 &&
                                      event.b == 100 && event.c == 36;
                             }),
         "self-test should pair note durations");

  std::ostringstream parityOutput;
  expect(compareMidi(midi, midi, parityOutput), "self-test should compare identical MIDI");

  MidiSequence longerSequence = midiSequence;
  longerSequence.tracks[0].events.insert(longerSequence.tracks[0].events.end() - 1,
                                         ProgramChange{.tick = 60, .channel = 2, .program = 13});
  std::get<EndOfTrack>(longerSequence.tracks[0].events.back()).tick = 72;
  const auto longerMidi = encodeMidiFile(longerSequence);
  std::ostringstream exactHorizonOutput;
  expect(!compareMidi(longerMidi, midi, exactHorizonOutput),
         "self-test should detect events beyond the shorter play-once endpoint without a horizon");
  std::ostringstream sharedHorizonOutput;
  expect(compareMidi(longerMidi, midi, sharedHorizonOutput, sharedPlayOnceHorizonOptions()),
         "self-test should compare only events before the shared play-once endpoint");

  const auto aramBytes = makeCapcomSnesAram();
  std::ostringstream summaryOutput;
  expect(compareCapcomSnesSummary(aramBytes, "synthetic.spc", summaryOutput),
         "self-test should compare CapcomSnes summary parity: " + summaryOutput.str());
  legacyLevelReapplicationKeepsPreCurvePrecision();

  std::cout << "vgmtrans-parity self-test ok\n";
  return 0;
}

int compareCapcomSnesAramMidi(const std::filesystem::path& path) {
  const auto aramBytes = readFile(path);
  const std::string name = path.filename().string();
  const auto legacyMidi = legacyCapcomSnesMidi(aramBytes, name);
  const auto valueMidi = valueCapcomSnesMidi(aramBytes, name);
  return compareMidi(legacyMidi, valueMidi, std::cout, sharedPlayOnceHorizonOptions()) ? 0 : 1;
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
    if (!compareMidi(legacyMidi, valueMidi, std::cout, sharedPlayOnceHorizonOptions())) {
      return 1;
    }
  }

  std::cout << "CapcomSnes RSN MIDI parity ok: files=" << arams.size() << "\n";
  return 0;
}

int compareCapcomSnesRsnDirectMidi(const std::filesystem::path& path) {
  return runMidiParity(kCapcomSnesSuite, legacyCapcomSnesRsnMidis(path), valueCapcomSnesRsnMidis(path));
}

int compareCapcomSnesRsnDirectSynth(const std::filesystem::path& path) {
  return runSynthParity(kCapcomSnesSuite, legacyCapcomSnesRsnSynthExports(path), valueCapcomSnesRsnSynthExports(path));
}

int compareCapcomSnesRsnDirectSummary(const std::filesystem::path& path) {
  return runSummaryParity(kCapcomSnesSuite, legacyCapcomSnesRsnSummaries(path), valueCapcomSnesRsnSummaries(path));
}

int compareNdsDirectSummary(const std::filesystem::path& path) {
  return runSummaryParity(kNdsSuite, legacySummariesForSuite(path, kNdsSuite), valueSummariesForSuite(path, kNdsSuite));
}

int compareKonamiSnesDirectSummary(const std::filesystem::path& path) {
  return runSummaryParity(kKonamiSnesSuite, legacySummariesForSuite(path, kKonamiSnesSuite),
                          valueSummariesForSuite(path, kKonamiSnesSuite));
}

int compareAkaoSnesDirectSummary(const std::filesystem::path& path) {
  return runSummaryParity(kAkaoSnesSuite, legacySummariesForSuite(path, kAkaoSnesSuite),
                          valueSummariesForSuite(path, kAkaoSnesSuite));
}

int compareKonamiSnesDirectMidi(const std::filesystem::path& path, u32 sequenceLoops = 0) {
  return runMidiParity(kKonamiSnesSuite, legacyMidisForSuite(path, kKonamiSnesSuite, sequenceLoops),
                       valueMidisForSuite(path, kKonamiSnesSuite, sequenceLoops), sequenceLoops);
}

int compareAkaoSnesDirectMidi(const std::filesystem::path& path, u32 sequenceLoops = 0) {
  return runMidiParity(kAkaoSnesSuite, legacyMidisForSuite(path, kAkaoSnesSuite, sequenceLoops),
                       valueMidisForSuite(path, kAkaoSnesSuite, sequenceLoops), sequenceLoops);
}

std::string safeDumpFilename(std::string_view name) {
  std::string safe;
  safe.reserve(name.size());
  for (const char ch : name) {
    if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_') {
      safe.push_back(ch);
    } else {
      safe.push_back('_');
    }
  }
  if (safe.empty()) {
    return "collection";
  }
  return safe;
}

int dumpKonamiSnesDirectMidis(const std::filesystem::path& path, const std::filesystem::path& dir,
                              u32 sequenceLoops = 0) {
  std::filesystem::create_directories(dir);
  const auto legacyMidis = legacyCollectionMidis(path, sequenceLoops);
  const auto valueMidis = valueCollectionMidis(path, sequenceLoops);
  const auto simulatedMidis =
      valueCollectionMidis(path, sequenceLoops, ModulationConversionPolicy::SequenceEventSimulation);

  for (const auto& [collectionName, legacyMidi] : legacyMidis) {
    const std::string base = safeDumpFilename(collectionName);
    writeFile(dir / (base + "-legacy.mid"), legacyMidi);
    if (const auto found = valueMidis.find(collectionName); found != valueMidis.end()) {
      writeFile(dir / (base + "-value.mid"), found->second);
    }
    if (const auto found = simulatedMidis.find(collectionName); found != simulatedMidis.end()) {
      writeFile(dir / (base + "-value-sim.mid"), found->second);
    }
    std::cout << "wrote MIDI dumps for " << collectionName << " to " << dir.string() << "\n";
  }

  return 0;
}

int compareKonamiSnesDirectSynth(const std::filesystem::path& path) {
  return runSynthParity(kKonamiSnesSuite, legacySynthsForSuite(path, kKonamiSnesSuite),
                        valueSynthsForSuite(path, kKonamiSnesSuite));
}

int compareAkaoSnesDirectSynth(const std::filesystem::path& path) {
  return runSynthParity(kAkaoSnesSuite, legacySynthsForSuite(path, kAkaoSnesSuite),
                        valueSynthsForSuite(path, kAkaoSnesSuite));
}

int compareNdsDirectMidi(const std::filesystem::path& path, u32 sequenceLoops = 0) {
  return runMidiParity(kNdsSuite, legacyMidisForSuite(path, kNdsSuite, sequenceLoops),
                       valueMidisForSuite(path, kNdsSuite, sequenceLoops), sequenceLoops);
}

int compareNdsDirectSynth(const std::filesystem::path& path) {
  return runSynthParity(kNdsSuite, legacySynthsForSuite(path, kNdsSuite), valueSynthsForSuite(path, kNdsSuite));
}

int compareAkaoDirectMidi(const std::filesystem::path& path, u32 sequenceLoops = 0) {
  return runMidiParity(kAkaoSuite, legacyAkaoCollectionMidis(path, sequenceLoops),
                       valueAkaoCollectionMidis(path, sequenceLoops), sequenceLoops);
}

int compareAkaoDirectSynth(const std::filesystem::path& path) {
  return runSynthParity(kAkaoSuite, legacyAkaoCollectionSynthExports(path), valueAkaoCollectionSynthExports(path));
}

bool validateValueCollectionExports(const SessionSnapshot& project, const Collection& collection,
                                    std::span<const Artifact> artifacts, u32 expectedWavs, std::ostream& out,
                                    u64& totalArtifacts) {
  ExportSmokeCounts counts;
  for (const auto& artifact : artifacts) {
    if (artifact.mediaType == "audio/midi") {
      ++counts.midi;
      if (!validateExportArtifact(artifact, ".mid", "MThd", std::string_view{}, out)) {
        return false;
      }
    } else if (artifact.mediaType == "audio/soundfont") {
      ++counts.soundFont2;
      if (!validateExportArtifact(artifact, ".sf2", "RIFF", "sfbk", out)) {
        return false;
      }
    } else if (artifact.mediaType == "audio/dls") {
      ++counts.dls;
      if (!validateExportArtifact(artifact, ".dls", "RIFF", "DLS ", out)) {
        return false;
      }
    } else if (artifact.mediaType == "audio/wav") {
      ++counts.wav;
      if (!validateExportArtifact(artifact, ".wav", "RIFF", "WAVE", out)) {
        return false;
      }
    } else {
      out << "collection '" << collection.name << "' produced unexpected media type '" << artifact.mediaType
          << "' in artifact '" << artifact.filename << "'\n";
      return false;
    }
  }

  if (counts.midi != 1 || counts.soundFont2 != 1 || counts.dls != 1 || counts.wav != expectedWavs) {
    out << "unexpected export counts for '" << collection.name << "': midi=" << counts.midi
        << " sf2=" << counts.soundFont2 << " dls=" << counts.dls << " wav=" << counts.wav
        << " expectedWav=" << expectedWavs << "\n";
    return false;
  }

  const auto valueWavs = valueSampleCount(project, collection);
  if (valueWavs != expectedWavs) {
    out << "collection '" << collection.name << "' value sample count changed during export: before=" << expectedWavs
        << " after=" << valueWavs << "\n";
    return false;
  }

  totalArtifacts += artifacts.size();
  out << "exported " << collection.name << " via direct RSN value scan: artifacts=" << artifacts.size()
      << " wavs=" << counts.wav << "\n";
  return true;
}

int compareCapcomSnesRsnDirectExport(const std::filesystem::path& path) {
  const auto legacySummaries = legacyCapcomSnesRsnSummaries(path);

  Session session;
  vgmtrans::formats::registerValueFormats(session);
  session.addSource(SourceFile{.name = path.filename().string(), .path = path}, readFile(path));
  session.scanPendingSources();
  const SessionSnapshot project = session.snapshot();

  if (project.collections().empty()) {
    std::ostringstream message;
    message << "value scanner did not discover collections from RSN";
    if (!project.diagnostics().empty()) {
      message << ": " << project.diagnostics().front().message;
    }
    throw std::runtime_error(message.str());
  }

  if (project.collections().size() != legacySummaries.size()) {
    std::cout << "value RSN collection count differs: legacy=" << legacySummaries.size()
              << " value=" << project.collections().size() << "\n";
    return 1;
  }

  const auto collectionExports = session.exportAllCollections(ExportRequest{
      .kinds = {ExportKind::Midi, ExportKind::SoundFont2, ExportKind::Dls, ExportKind::Wav},
      .loopPolicy = LoopPolicy::PlayOnce,
  });
  if (collectionExports.size() != project.collections().size()) {
    std::cout << "value all-collection export count differs: collections=" << project.collections().size()
              << " exports=" << collectionExports.size() << "\n";
    return 1;
  }

  u64 totalArtifacts = 0;
  std::vector<CollectionId> exportedCollections;
  for (const auto& collectionExport : collectionExports) {
    if (std::ranges::find(exportedCollections, collectionExport.collection) != exportedCollections.end()) {
      std::cout << "value all-collection export repeated collection id " << collectionExport.collection.value << "\n";
      return 1;
    }
    exportedCollections.push_back(collectionExport.collection);

    const auto* collection = project.collection(collectionExport.collection);
    if (collection == nullptr) {
      std::cout << "value all-collection export referenced missing collection id " << collectionExport.collection.value
                << "\n";
      return 1;
    }

    const auto found = legacySummaries.find(collection->name);
    if (found == legacySummaries.end()) {
      std::cout << "value RSN scan produced collection not found in legacy scan: '" << collection->name << "'\n";
      return 1;
    }

    const auto expectedWavs = valueSampleCount(project, *collection);
    const auto legacySampleCount = static_cast<u32>(found->second.samples.size());
    if (expectedWavs != legacySampleCount) {
      std::cout << "value sample count differs for '" << collection->name << "': legacy=" << legacySampleCount
                << " value=" << expectedWavs << "\n";
      return 1;
    }

    if (!validateValueCollectionExports(project, *collection, collectionExport.artifacts, expectedWavs, std::cout,
                                        totalArtifacts)) {
      return 1;
    }
  }

  std::cout << "CapcomSnes direct RSN export smoke ok: collections=" << project.collections().size()
            << " artifacts=" << totalArtifacts << "\n";
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
      << "  vgmtrans-parity capcom-snes-rsn-direct-export <rsn-file>\n"
      << "  vgmtrans-parity capcom-snes-rsn-direct-midi <rsn-file>\n"
      << "  vgmtrans-parity capcom-snes-rsn-direct-midi-sim <rsn-file> [sequence-loops]\n"
      << "  vgmtrans-parity capcom-snes-rsn-direct-synth <rsn-file>\n"
      << "  vgmtrans-parity capcom-snes-rsn-direct-summary <rsn-file>\n"
      << "  vgmtrans-parity capcom-snes-rsn-summary <rsn-file>\n"
      << "  vgmtrans-parity akao-direct-midi <psf-or-raw-file> [sequence-loops]\n"
      << "  vgmtrans-parity akao-direct-synth <psf-or-raw-file>\n"
      << "  vgmtrans-parity akao-direct-summary <psf-or-raw-file>\n"
      << "  vgmtrans-parity akao-snes-direct-midi <rsn-or-spc-file> [sequence-loops]\n"
      << "  vgmtrans-parity akao-snes-direct-midi-sim <rsn-or-spc-file> [sequence-loops]\n"
      << "  vgmtrans-parity akao-snes-direct-synth <rsn-or-spc-file>\n"
      << "  vgmtrans-parity akao-snes-direct-summary <rsn-or-spc-file>\n"
      << "  vgmtrans-parity konami-snes-direct-midi <rsn-or-spc-file> [sequence-loops]\n"
      << "  vgmtrans-parity konami-snes-direct-midi-dump <rsn-or-spc-file> <dir> [sequence-loops]\n"
      << "  vgmtrans-parity konami-snes-direct-midi-sim <rsn-or-spc-file> [sequence-loops]\n"
      << "  vgmtrans-parity konami-snes-direct-synth <rsn-or-spc-file>\n"
      << "  vgmtrans-parity konami-snes-direct-summary <rsn-or-spc-file>\n"
      << "  vgmtrans-parity nds-direct-midi <nds-or-2sf-file> [sequence-loops]\n"
      << "  vgmtrans-parity nds-direct-synth <nds-or-2sf-file>\n"
      << "  vgmtrans-parity nds-direct-summary <nds-or-2sf-file>\n";
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

    if (argc == 3 && std::string(argv[1]) == "capcom-snes-rsn-direct-midi-sim") {
      return validateCapcomSnesDirectMidiSimulation(argv[2]);
    }

    if (argc == 4 && std::string(argv[1]) == "capcom-snes-rsn-direct-midi-sim") {
      return validateCapcomSnesDirectMidiSimulation(argv[2], parseLoopCount(argv[3]));
    }

    if (argc == 3 && std::string(argv[1]) == "capcom-snes-rsn-direct-export") {
      return compareCapcomSnesRsnDirectExport(argv[2]);
    }

    if (argc == 3 && std::string(argv[1]) == "capcom-snes-rsn-direct-synth") {
      return compareCapcomSnesRsnDirectSynth(argv[2]);
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

    if (argc == 3 && std::string(argv[1]) == "nds-direct-summary") {
      return compareNdsDirectSummary(argv[2]);
    }

    if (argc == 3 && std::string(argv[1]) == "konami-snes-direct-summary") {
      return compareKonamiSnesDirectSummary(argv[2]);
    }

    if (argc == 3 && std::string(argv[1]) == "akao-snes-direct-summary") {
      return compareAkaoSnesDirectSummary(argv[2]);
    }

    if (argc == 3 && std::string(argv[1]) == "akao-snes-direct-midi") {
      return compareAkaoSnesDirectMidi(argv[2]);
    }

    if (argc == 4 && std::string(argv[1]) == "akao-snes-direct-midi") {
      return compareAkaoSnesDirectMidi(argv[2], parseLoopCount(argv[3]));
    }

    if (argc == 3 && std::string(argv[1]) == "akao-snes-direct-midi-sim") {
      return validateAkaoSnesDirectMidiSimulation(argv[2]);
    }

    if (argc == 4 && std::string(argv[1]) == "akao-snes-direct-midi-sim") {
      return validateAkaoSnesDirectMidiSimulation(argv[2], parseLoopCount(argv[3]));
    }

    if (argc == 3 && std::string(argv[1]) == "akao-snes-direct-synth") {
      return compareAkaoSnesDirectSynth(argv[2]);
    }

    if (argc == 3 && std::string(argv[1]) == "konami-snes-direct-midi") {
      return compareKonamiSnesDirectMidi(argv[2]);
    }

    if (argc == 4 && std::string(argv[1]) == "konami-snes-direct-midi") {
      return compareKonamiSnesDirectMidi(argv[2], parseLoopCount(argv[3]));
    }

    if (argc == 4 && std::string(argv[1]) == "konami-snes-direct-midi-dump") {
      return dumpKonamiSnesDirectMidis(argv[2], argv[3]);
    }

    if (argc == 5 && std::string(argv[1]) == "konami-snes-direct-midi-dump") {
      return dumpKonamiSnesDirectMidis(argv[2], argv[3], parseLoopCount(argv[4]));
    }

    if (argc == 3 && std::string(argv[1]) == "konami-snes-direct-midi-sim") {
      return validateKonamiSnesDirectMidiSimulation(argv[2]);
    }

    if (argc == 4 && std::string(argv[1]) == "konami-snes-direct-midi-sim") {
      return validateKonamiSnesDirectMidiSimulation(argv[2], parseLoopCount(argv[3]));
    }

    if (argc == 3 && std::string(argv[1]) == "konami-snes-direct-synth") {
      return compareKonamiSnesDirectSynth(argv[2]);
    }

    if (argc == 3 && std::string(argv[1]) == "akao-direct-summary") {
      return compareAkaoDirectSummary(argv[2]);
    }

    if (argc == 3 && std::string(argv[1]) == "akao-direct-midi") {
      return compareAkaoDirectMidi(argv[2]);
    }

    if (argc == 4 && std::string(argv[1]) == "akao-direct-midi") {
      return compareAkaoDirectMidi(argv[2], parseLoopCount(argv[3]));
    }

    if (argc == 3 && std::string(argv[1]) == "akao-direct-synth") {
      return compareAkaoDirectSynth(argv[2]);
    }

    if (argc == 3 && std::string(argv[1]) == "nds-direct-midi") {
      return compareNdsDirectMidi(argv[2]);
    }

    if (argc == 4 && std::string(argv[1]) == "nds-direct-midi") {
      return compareNdsDirectMidi(argv[2], parseLoopCount(argv[3]));
    }

    if (argc == 3 && std::string(argv[1]) == "nds-direct-synth") {
      return compareNdsDirectSynth(argv[2]);
    }

    printUsage(std::cerr);
    return 2;
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 1;
  }
}
