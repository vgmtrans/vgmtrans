/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/export/synth/SoundFontExporter.h"

#include "value/export/ExportDiagnostics.h"
#include "value/export/synth/ModulationScaling.h"
#include "value/export/synth/SynthExportData.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace vgmtrans::core {

namespace {

constexpr u16 kSfGenVibLfoToPitch = 6;
constexpr u16 kSfGenInitialFilterFc = 8;
constexpr u16 kSfGenModLfoToVolume = 13;
constexpr u16 kSfGenReverbEffectsSend = 16;
constexpr u16 kSfGenPan = 17;
constexpr u16 kSfGenFreqModLfo = 22;
constexpr u16 kSfGenFreqVibLfo = 24;
constexpr u16 kSfGenAttackVolEnv = 34;
constexpr u16 kSfGenHoldVolEnv = 35;
constexpr u16 kSfGenDecayVolEnv = 36;
constexpr u16 kSfGenSustainVolEnv = 37;
constexpr u16 kSfGenReleaseVolEnv = 38;
constexpr u16 kSfGenInstrument = 41;
constexpr u16 kSfGenKeyRange = 43;
constexpr u16 kSfGenVelRange = 44;
constexpr u16 kSfGenInitialAttenuation = 48;
constexpr u16 kSfGenCoarseTune = 51;
constexpr u16 kSfGenFineTune = 52;
constexpr u16 kSfGenSampleId = 53;
constexpr u16 kSfGenSampleModes = 54;
constexpr u16 kSfGenOverridingRootKey = 58;
constexpr u16 kSfModNoteOnVelocity = 2;
constexpr u16 kSfModKeyNumber = 3;
constexpr u16 kSfModPolyPressure = 10;
constexpr u16 kSfModChannelPressure = 13;
constexpr u16 kSfModMidiContinuousController = 1u << 7;
constexpr u16 kSfModBipolar = 1u << 9;
constexpr u16 kSfModPitchWheel = kSfModBipolar | 14;
constexpr u16 kSfModModWheel = kSfModMidiContinuousController | 1;
constexpr u16 kSfModSoundController6 = kSfModMidiContinuousController | 75;
constexpr u16 kSfModVibratoRate = kSfModMidiContinuousController | 76;
constexpr u16 kSfModTremoloDepth = kSfModMidiContinuousController | 92;
constexpr u16 kSfTransformLinear = 0;

constexpr u32 kSf2SamplePaddingFrames = 46;
constexpr u8 kDefaultRootKey = 60;
constexpr u32 kPresetGeneratorsPerInstrument = 2;
constexpr u32 kBaseInstrumentRegionGenerators = 9;
constexpr u32 kEnvelopeInstrumentRegionGenerators = 5;

struct Chunk {
  // RIFF chunk size excludes the optional pad byte. payload is already padded so appending
  // chunks can stay mechanical.
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

struct SfRegionPitch {
  u8 rootKey = kDefaultRootKey;
  s16 coarseTune = 0;
  s16 fineTune = 0;
};

struct SfSampleHeaderPitch {
  u8 originalKey = kDefaultRootKey;
  s8 correction = 0;
};

struct SfSampleHeaderInfo {
  SfSampleHeaderPitch pitch;
  Loop loop;
};

struct SfModulatorRecord {
  u16 source = 0;
  u16 destination = 0;
  s16 amount = 0;
};

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
  // SF2 stores most tables inside RIFF LIST chunks whose first four payload bytes name
  // the list type, such as INFO, sdta, or pdta.
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

[[nodiscard]] s16 sf2ReverbSend(double reverb) {
  if (!std::isfinite(reverb)) {
    return 0;
  }
  return clampS16(static_cast<s32>(reverb * 1000.0));
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

[[nodiscard]] s8 clampS8(s32 value) {
  return static_cast<s8>(std::clamp<s32>(value, std::numeric_limits<s8>::min(), std::numeric_limits<s8>::max()));
}

[[nodiscard]] SfRegionPitch sf2RegionPitch(const Region& region, const DecodedSfSample& sample) {
  // If a region provides an explicit root key, trust its coarse/fine tuning fields. When
  // it does not, fold region and sample pitch into the default-key tuning generators.
  if (region.rootKey) {
    return SfRegionPitch{
        .rootKey = *region.rootKey,
        .coarseTune = region.coarseTuneSemitones,
        .fineTune = region.fineTuneCents,
    };
  }

  const auto [coarseTune, fineTune] = splitTuneCents(region.tuning.cents + sample.pitch.cents);
  return SfRegionPitch{
      .rootKey = kDefaultRootKey,
      .coarseTune = coarseTune,
      .fineTune = fineTune,
  };
}

[[nodiscard]] SfSampleHeaderPitch sf2SampleHeaderPitch(u8 rootKey, s16 fineTune) {
  return SfSampleHeaderPitch{
      .originalKey = static_cast<u8>(std::clamp<s32>(static_cast<s32>(rootKey) - (fineTune / 100), 0, 127)),
      .correction = clampS8(fineTune % 100),
  };
}

[[nodiscard]] Loop effectiveSfLoop(const Region& region, const DecodedSfSample& sample) {
  return region.loop.value_or(sample.decoded.loop);
}

[[nodiscard]] s16 sf2Pan(double pan) {
  return clampS16(static_cast<s32>(std::lround((std::clamp(pan, 0.0, 1.0) - 0.5) * 1000.0)));
}

[[nodiscard]] std::optional<u16> sf2GeneratorForDestination(SynthDestination destination) {
  switch (destination) {
    case SynthDestination::Pitch:
      return kSfGenFineTune;
    case SynthDestination::FilterCutoff:
      return kSfGenInitialFilterFc;
    case SynthDestination::VolumeAttenuation:
      return kSfGenInitialAttenuation;
    case SynthDestination::Pan:
      return kSfGenPan;
    case SynthDestination::VibratoDepth:
      return kSfGenVibLfoToPitch;
    case SynthDestination::VibratoRate:
      return kSfGenFreqVibLfo;
    case SynthDestination::TremoloDepth:
      return kSfGenModLfoToVolume;
    case SynthDestination::TremoloRate:
      return kSfGenFreqModLfo;
    case SynthDestination::Unknown:
      return std::nullopt;
  }

  return std::nullopt;
}

[[nodiscard]] s16 sf2GeneratorAmount(const SynthGenerator& generator) {
  return clampS16(generator.amount);
}

[[nodiscard]] std::optional<u16> sf2SourceForSynthSource(SynthSource source) {
  switch (source) {
    case SynthSource::NoteOnVelocity:
      return kSfModNoteOnVelocity;
    case SynthSource::KeyNumber:
      return kSfModKeyNumber;
    case SynthSource::Lfo:
    case SynthSource::Envelope:
    case SynthSource::MidiController:
      return std::nullopt;
    case SynthSource::ChannelPressure:
      return kSfModChannelPressure;
    case SynthSource::PolyPressure:
      return kSfModPolyPressure;
    case SynthSource::PitchWheel:
      return kSfModPitchWheel;
    case SynthSource::Unknown:
      return std::nullopt;
  }

  return std::nullopt;
}

[[nodiscard]] std::optional<u16> sf2DefaultSourceForDestination(SynthDestination destination) {
  switch (destination) {
    case SynthDestination::VibratoDepth:
      return kSfModModWheel;
    case SynthDestination::VibratoRate:
      return kSfModVibratoRate;
    case SynthDestination::TremoloDepth:
    case SynthDestination::VolumeAttenuation:
      return kSfModTremoloDepth;
    case SynthDestination::TremoloRate:
      return kSfModSoundController6;
    case SynthDestination::Pitch:
    case SynthDestination::FilterCutoff:
    case SynthDestination::Pan:
    case SynthDestination::Unknown:
      return std::nullopt;
  }

  return std::nullopt;
}

[[nodiscard]] std::optional<SfModulatorRecord> sf2ModulatorFor(
    const SynthModulator& modulator, const MidiModulationUsage* midiModulationUsage = nullptr,
    ModulationScalingPolicy modulationScaling = ModulationScalingPolicy::FullFormatRange) {
  // SynthModulator names common controller behavior. This function chooses the SF2 controller
  // and generator numbers that represent it in the file.
  const auto source = modulator.source ? sf2SourceForSynthSource(*modulator.source)
                                       : sf2DefaultSourceForDestination(modulator.destination);
  const auto destination = sf2GeneratorForDestination(modulator.destination);
  if (!source || !destination) {
    return std::nullopt;
  }

  return SfModulatorRecord{
      .source = *source,
      .destination = *destination,
      .amount = clampS16(scaledSynthModulatorAmount(modulator, midiModulationUsage, modulationScaling)),
  };
}

[[nodiscard]] u16 sf2Attenuation(const Region& region, const Sample& sample) {
  constexpr double centibelsPerDb = 10.0;
  // Legacy VGMTrans declares EMU8000-compatible SF2 output. EMU-compatible
  // synths apply a 0.4 factor to initialAttenuation, so the stored value uses
  // the reciprocal to preserve the requested dB attenuation.
  constexpr double emu8000InitialAttenuationScale = 2.5;
  constexpr double maxInitialAttenuation = 1440.0;
  return static_cast<u16>(std::clamp(std::lround((region.attenuationDb + sample.attenuationDb) * centibelsPerDb *
                                                 emu8000InitialAttenuationScale),
                                     0l, static_cast<long>(maxInitialAttenuation)));
}

[[nodiscard]] std::optional<double> envelopeSeconds(u32 microseconds, std::optional<double> preciseSeconds) {
  // Some source drivers use envelope timing that does not round cleanly to integer microseconds.
  // Prefer a precise seconds value when the format provides one.
  if (preciseSeconds && *preciseSeconds >= 0.0 && std::isfinite(*preciseSeconds)) {
    return *preciseSeconds;
  }
  if (microseconds == 0 || microseconds == kEnvelopeInfinite) {
    return std::nullopt;
  }
  return static_cast<double>(microseconds) / 1'000'000.0;
}

[[nodiscard]] s16 sf2EnvelopeTimecents(u32 microseconds, std::optional<double> preciseSeconds) {
  const auto seconds = envelopeSeconds(microseconds, preciseSeconds);
  if (seconds && *seconds > 0.0) {
    return clampS16(static_cast<s32>(std::lround(1200.0 * std::log2(*seconds))));
  }
  if (microseconds == kEnvelopeInfinite) {
    return std::numeric_limits<s16>::min();
  }
  if (microseconds == 0) {
    return std::numeric_limits<s16>::min();
  }
  return std::numeric_limits<s16>::min();
}

[[nodiscard]] s16 sf2SustainAttenuation(const Envelope& envelope) {
  constexpr long maxSustainAttenuationCentibels = 1000;
  if (envelope.sustainAmplitude) {
    const double amplitude = std::clamp(*envelope.sustainAmplitude, 0.0, 1.0);
    const double attenuationDb = amplitude == 0.0 ? 100.0 : std::min(-20.0 * std::log10(amplitude), 100.0);
    return clampS16(static_cast<s32>(std::clamp(attenuationDb * 10.0, 0.0, 1000.0)));
  }
  if (envelope.sustain == 0) {
    return 0;
  }

  const double amplitude = std::clamp(static_cast<double>(envelope.sustain) / 1000.0, 0.0, 1.0);
  if (amplitude >= 1.0) {
    return 0;
  }

  return static_cast<s16>(std::clamp(std::lround(-200.0 * std::log10(amplitude)), 0l, maxSustainAttenuationCentibels));
}

[[nodiscard]] u32 instrumentRegionGeneratorCount(const Region& region) {
  static_cast<void>(region);
  return kBaseInstrumentRegionGenerators + kEnvelopeInstrumentRegionGenerators;
}

[[nodiscard]] u32 instrumentGlobalGeneratorCount(const Instrument& instrument) {
  return static_cast<u32>(std::ranges::count_if(instrument.generators, [](const SynthGenerator& generator) {
    return sf2GeneratorForDestination(generator.destination).has_value();
  }));
}

[[nodiscard]] u32 instrumentGlobalModulatorCount(const Instrument& instrument) {
  return static_cast<u32>(std::ranges::count_if(
      instrument.modulators, [](const SynthModulator& modulator) { return sf2ModulatorFor(modulator).has_value(); }));
}

[[nodiscard]] bool hasInstrumentGlobalZone(const Instrument& instrument) {
  return instrumentGlobalGeneratorCount(instrument) != 0 || instrumentGlobalModulatorCount(instrument) != 0;
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

[[nodiscard]] std::vector<DecodedSfSample> sf2Samples(std::vector<DecodedSynthSample> decodedSamples) {
  // SF2 stores all sample PCM in one smpl chunk. startFrame/endFrame are offsets into
  // that concatenated stream, including the mandatory pad after each sample.
  std::vector<DecodedSfSample> samples;
  samples.reserve(decodedSamples.size());
  for (auto& sample : decodedSamples) {
    samples.push_back(DecodedSfSample{
        .collectionId = sample.collectionId,
        .localIndex = sample.localIndex,
        .name = sf2Name(std::move(sample.name), "Sample"),
        .pitch = sample.pitch,
        .attenuationDb = sample.attenuationDb,
        .decoded = std::move(sample.decoded),
    });
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

[[nodiscard]] Chunk phdrChunk(std::span<const ResolvedSynthInstrument> instruments) {
  // Each parsed Instrument becomes one SF2 preset pointing at the matching SF2 instrument
  // by index. The terminal EOP record is required by the SF2 table format.
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

[[nodiscard]] Chunk pbagChunk(std::span<const ResolvedSynthInstrument> instruments) {
  std::vector<u8> payload;
  for (u32 i = 0; i < instruments.size(); ++i) {
    writeLe16(payload, clampU16(i * kPresetGeneratorsPerInstrument));
    writeLe16(payload, 0);
  }
  writeLe16(payload, clampU16(instruments.size() * kPresetGeneratorsPerInstrument));
  writeLe16(payload, 0);
  return makeChunk("pbag", std::move(payload));
}

[[nodiscard]] Chunk pgenChunk(std::span<const ResolvedSynthInstrument> instruments) {
  std::vector<u8> payload;
  for (u32 i = 0; i < instruments.size(); ++i) {
    writeAmountGen(payload, kSfGenReverbEffectsSend, sf2ReverbSend(instruments[i].instrument->reverb));
    writeWordGen(payload, kSfGenInstrument, clampU16(i));
  }
  writeWordGen(payload, 0, 0);
  return makeChunk("pgen", std::move(payload));
}

[[nodiscard]] Chunk terminalModChunk(std::string id) {
  std::vector<u8> payload(10);
  return makeChunk(std::move(id), std::move(payload));
}

[[nodiscard]] Chunk instChunk(std::span<const ResolvedSynthInstrument> instruments) {
  // SF2 instruments point into bag tables. Instruments with global generators/modulators
  // get one global bag before their sample regions.
  std::vector<u8> payload;
  u32 bagIndex = 0;
  for (const auto& instrument : instruments) {
    writeFixedString(payload, sf2Name(instrument.instrument->name, "Instrument"), 20);
    writeLe16(payload, clampU16(bagIndex));
    bagIndex += static_cast<u32>(instrument.regions.size()) + (hasInstrumentGlobalZone(*instrument.instrument) ? 1 : 0);
  }

  writeFixedString(payload, "EOI", 20);
  writeLe16(payload, clampU16(bagIndex));
  return makeChunk("inst", std::move(payload));
}

[[nodiscard]] Chunk ibagChunk(std::span<const ResolvedSynthInstrument> instruments) {
  // Bags are index pairs into generator/modulator arrays. Counts must be predicted before
  // writing igen/imod so the table offsets line up exactly.
  std::vector<u8> payload;
  u32 generatorIndex = 0;
  u32 modulatorIndex = 0;
  for (const auto& instrument : instruments) {
    if (hasInstrumentGlobalZone(*instrument.instrument)) {
      writeLe16(payload, clampU16(generatorIndex));
      writeLe16(payload, clampU16(modulatorIndex));
      generatorIndex += instrumentGlobalGeneratorCount(*instrument.instrument);
      modulatorIndex += instrumentGlobalModulatorCount(*instrument.instrument);
    }

    for (size_t i = 0; i < instrument.regions.size(); ++i) {
      writeLe16(payload, clampU16(generatorIndex));
      writeLe16(payload, clampU16(modulatorIndex));
      generatorIndex += instrumentRegionGeneratorCount(*instrument.regions[i].region);
    }
  }

  writeLe16(payload, clampU16(generatorIndex));
  writeLe16(payload, clampU16(modulatorIndex));
  return makeChunk("ibag", std::move(payload));
}

[[nodiscard]] Chunk imodChunk(std::span<const ResolvedSynthInstrument> instruments,
                              const MidiModulationUsage* midiModulationUsage,
                              ModulationScalingPolicy modulationScaling) {
  std::vector<u8> payload;
  for (const auto& instrument : instruments) {
    for (const auto& modulator : instrument.instrument->modulators) {
      const auto record = sf2ModulatorFor(modulator, midiModulationUsage, modulationScaling);
      if (!record) {
        continue;
      }

      writeWordGen(payload, record->source, record->destination);
      writeLeS16(payload, record->amount);
      writeLe16(payload, 0);
      writeLe16(payload, kSfTransformLinear);
    }
  }

  payload.insert(payload.end(), 10, 0);
  return makeChunk("imod", std::move(payload));
}

[[nodiscard]] Chunk igenChunk(std::span<const ResolvedSynthInstrument> instruments,
                              std::span<const DecodedSfSample> samplesByIndex) {
  // Region generators are written in SF2's required order: ranges and placement first,
  // then envelope/tuning/sample linkage. Unsupported SynthGenerator destinations are skipped.
  std::vector<u8> payload;
  for (const auto& instrument : instruments) {
    for (const auto& generator : instrument.instrument->generators) {
      const auto sf2Generator = sf2GeneratorForDestination(generator.destination);
      if (!sf2Generator) {
        continue;
      }

      writeAmountGen(payload, *sf2Generator, sf2GeneratorAmount(generator));
    }

    for (const auto& sfRegion : instrument.regions) {
      const auto& region = *sfRegion.region;
      const auto& sample = samplesByIndex[sfRegion.sampleIndex];
      const auto pitch = sf2RegionPitch(region, sample);

      writeRangeGen(payload, kSfGenKeyRange, region.keyRange.low, region.keyRange.high);
      writeRangeGen(payload, kSfGenVelRange, region.velocityRange.low, region.velocityRange.high);
      writeAmountGen(payload, kSfGenInitialAttenuation,
                     static_cast<s16>(sf2Attenuation(region, Sample{.attenuationDb = sample.attenuationDb})));
      writeAmountGen(payload, kSfGenPan, sf2Pan(region.pan));
      writeAmountGen(payload, kSfGenCoarseTune, pitch.coarseTune);
      writeAmountGen(payload, kSfGenFineTune, pitch.fineTune);
      const bool explicitEnvelope = hasExplicitEnvelope(region.envelope);
      writeAmountGen(payload, kSfGenAttackVolEnv,
                     sf2EnvelopeTimecents(region.envelope.attack, region.envelope.attackSeconds));
      writeAmountGen(payload, kSfGenHoldVolEnv,
                     sf2EnvelopeTimecents(region.envelope.hold, region.envelope.holdSeconds));
      writeAmountGen(payload, kSfGenDecayVolEnv,
                     sf2EnvelopeTimecents(region.envelope.decay, region.envelope.decaySeconds));
      writeAmountGen(payload, kSfGenSustainVolEnv, explicitEnvelope ? sf2SustainAttenuation(region.envelope) : 0);
      writeAmountGen(payload, kSfGenReleaseVolEnv,
                     sf2EnvelopeTimecents(region.envelope.release, region.envelope.releaseSeconds));
      writeWordGen(payload, kSfGenOverridingRootKey, pitch.rootKey);
      writeWordGen(payload, kSfGenSampleModes, effectiveSfLoop(region, sample).enabled ? 1 : 0);
      writeWordGen(payload, kSfGenSampleId, sfRegion.sampleIndex);
    }
  }

  writeWordGen(payload, 0, 0);
  return makeChunk("igen", std::move(payload));
}

[[nodiscard]] std::vector<SfSampleHeaderInfo> sampleHeaderInfo(
    std::span<const DecodedSfSample> samples, std::span<const ResolvedSynthInstrument> instruments) {
  // SF2 sample headers have their own original-key/correction fields. Pick the first
  // region that references each sample so sample headers stay consistent with zones.
  std::vector<SfSampleHeaderInfo> info(samples.size());
  std::vector<bool> assigned(samples.size(), false);
  for (size_t i = 0; i < samples.size(); ++i) {
    info[i].loop = samples[i].decoded.loop;
  }
  for (const auto& instrument : instruments) {
    for (const auto& sfRegion : instrument.regions) {
      if (sfRegion.sampleIndex >= info.size() || assigned[sfRegion.sampleIndex]) {
        continue;
      }
      const auto pitch = sf2RegionPitch(*sfRegion.region, samples[sfRegion.sampleIndex]);
      info[sfRegion.sampleIndex].pitch =
          sf2SampleHeaderPitch(pitch.rootKey, clampS16(samples[sfRegion.sampleIndex].pitch.cents));
      info[sfRegion.sampleIndex].loop = effectiveSfLoop(*sfRegion.region, samples[sfRegion.sampleIndex]);
      assigned[sfRegion.sampleIndex] = true;
    }
  }
  return info;
}

[[nodiscard]] Chunk shdrChunk(std::span<const DecodedSfSample> samples,
                              std::span<const ResolvedSynthInstrument> instruments) {
  const auto headers = sampleHeaderInfo(samples, instruments);
  std::vector<u8> payload;
  for (size_t i = 0; i < samples.size(); ++i) {
    const auto& sample = samples[i];
    writeFixedString(payload, sf2Name(sample.name, "Sample"), 20);
    writeLe32(payload, sample.startFrame);
    writeLe32(payload, sample.endFrame);
    const u32 loopStart = sample.startFrame + headers[i].loop.start;
    const u32 loopEnd = loopStart + headers[i].loop.length;
    writeLe32(payload, loopStart);
    writeLe32(payload, std::min(loopEnd, sample.endFrame));
    writeLe32(payload, sample.decoded.sampleRate == 0 ? 32000 : sample.decoded.sampleRate);
    writeU8(payload, headers[i].pitch.originalKey);
    writeU8(payload, static_cast<u8>(headers[i].pitch.correction));
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

[[nodiscard]] std::vector<Chunk> pdtaChunks(std::span<const ResolvedSynthInstrument> instruments,
                                            std::span<const DecodedSfSample> samples,
                                            const MidiModulationUsage* midiModulationUsage,
                                            ModulationScalingPolicy modulationScaling) {
  return {
      phdrChunk(instruments),
      pbagChunk(instruments),
      terminalModChunk("pmod"),
      pgenChunk(instruments),
      instChunk(instruments),
      ibagChunk(instruments),
      imodChunk(instruments, midiModulationUsage, modulationScaling),
      igenChunk(instruments, samples),
      shdrChunk(samples, instruments),
  };
}

}  // namespace

SoundFontResult SoundFontExporter::exportSoundFont(const SoundFontInput& input, const SourceStore& sources) const {
  SoundFontResult result;

  // SoundFont 2 sample data is mono PCM16. Shared decode/resolve helpers do the expensive
  // source work once before this file-specific table writer assembles RIFF chunks.
  auto decodedSamples = decodeSynthSamples(input.sampleCollections, sources, result.diagnostics,
                                           SynthSampleDecodeOptions{
                                               .requireMono = true,
                                               .nonMonoWarning = "Skipping non-mono sample for SoundFont2 export",
                                           });
  const auto samplesByReference = synthSampleIndexMap(decodedSamples);
  auto samples = sf2Samples(std::move(decodedSamples));
  auto instruments =
      resolveSynthInstruments(input.instrumentSets, input.sampleCollections, samplesByReference, result.diagnostics);

  if (samples.empty()) {
    result.diagnostics.push_back(exportError("No decodable samples available for SoundFont2 export"));
    return result;
  }

  result.bytes = riffSoundFont({
      makeListChunk("INFO", infoChunks(sf2Name(input.name, "VGMTrans"))),
      makeListChunk("sdta", {smplChunk(samples)}),
      makeListChunk("pdta", pdtaChunks(instruments, samples, input.midiModulationUsage, input.modulationScaling)),
  });
  return result;
}

}  // namespace vgmtrans::core
