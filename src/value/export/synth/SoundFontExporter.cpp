/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/export/synth/SynthExportData.h"

#include "value/export/BinaryWriter.h"
#include "value/export/ExportDiagnostics.h"
#include "value/export/synth/ModulationScaling.h"

#include <algorithm>
#include <array>
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
constexpr u16 kSfGenDelayModLfo = 21;
constexpr u16 kSfGenFreqModLfo = 22;
constexpr u16 kSfGenDelayVibLfo = 23;
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
constexpr u16 kSfModVibratoDelay = kSfModMidiContinuousController | 78;
constexpr u16 kSfModSoundController10 = kSfModMidiContinuousController | 79;
constexpr u16 kSfModTremoloDepth = kSfModMidiContinuousController | 92;
constexpr u16 kSfTransformLinear = 0;
constexpr double kSoundFontVolumeEnvelopeRangeDb = 100.0;

constexpr u32 kSf2SamplePaddingFrames = 46;
constexpr u8 kDefaultRootKey = 60;
constexpr u32 kBaseInstrumentRegionGenerators = 9;
constexpr u32 kEnvelopeInstrumentRegionGenerators = 5;
constexpr std::array<u16, kEnvelopeInstrumentRegionGenerators> kSfEnvelopeGenerators{
    kSfGenAttackVolEnv, kSfGenHoldVolEnv, kSfGenDecayVolEnv, kSfGenSustainVolEnv, kSfGenReleaseVolEnv,
};

using Chunk = RiffChunk;

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

using SfEnvelope = std::array<s16, kEnvelopeInstrumentRegionGenerators>;

struct SfPreset {
  const ResolvedSynthInstrument* source = nullptr;
  u32 instrumentIndex = 0;
  SfEnvelope envelopeOffsets{};
};

struct SfLayout {
  std::vector<SfPreset> presets;
  std::vector<ResolvedSynthInstrument> instruments;
};

[[nodiscard]] Chunk makeStringChunk(std::string id, std::string_view text) {
  std::vector<u8> payload;
  writeAscii(payload, text);
  payload.push_back(0);
  if ((payload.size() & 1) != 0) {
    payload.push_back(0);
  }
  return makeChunk(std::move(id), std::move(payload));
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

[[nodiscard]] SfRegionPitch sf2RegionPitch(const Region& region) {
  const auto rootKey = static_cast<u8>(std::clamp(std::lround(region.unityKey), 0l, 127l));
  const auto tuningCents = static_cast<s32>(std::lround((rootKey - region.unityKey) * 100.0));
  const auto [coarseTune, fineTune] = splitTuneCents(tuningCents);
  return SfRegionPitch{
      .rootKey = rootKey,
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
    case SynthDestination::VibratoDelay:
      return kSfGenDelayVibLfo;
    case SynthDestination::TremoloDepth:
      return kSfGenModLfoToVolume;
    case SynthDestination::TremoloRate:
      return kSfGenFreqModLfo;
    case SynthDestination::TremoloDelay:
      return kSfGenDelayModLfo;
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
    case SynthDestination::VibratoDelay:
      return kSfModVibratoDelay;
    case SynthDestination::TremoloDepth:
    case SynthDestination::VolumeAttenuation:
      return kSfModTremoloDepth;
    case SynthDestination::TremoloRate:
      return kSfModSoundController6;
    case SynthDestination::TremoloDelay:
      return kSfModSoundController10;
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
    ModulationScalingPolicy modulationScaling = ModulationScalingPolicy::FullFormatRange,
    ModulationConversionPolicy modulationConversion = ModulationConversionPolicy::SynthModulators) {
  // SynthModulator names common controller behavior. This function chooses the SF2 controller
  // and generator numbers that represent it in the file.
  if (!shouldExportSynthModulator(modulator, modulationConversion)) {
    return std::nullopt;
  }
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
  return static_cast<u16>(std::clamp(
      std::lround((region.attenuationDb + sample.attenuationDb) * centibelsPerDb * emu8000InitialAttenuationScale), 0l,
      static_cast<long>(maxInitialAttenuation)));
}

[[nodiscard]] s16 sf2EnvelopeTimecents(std::optional<double> seconds) {
  if (seconds && std::isinf(*seconds) && *seconds > 0.0) {
    return std::numeric_limits<s16>::max();
  }
  if (seconds && std::isfinite(*seconds) && *seconds > 0.0) {
    return clampS16(static_cast<s32>(std::lround(1200.0 * std::log2(*seconds))));
  }
  return std::numeric_limits<s16>::min();
}

[[nodiscard]] s16 sf2SustainAttenuation(const Envelope& envelope) {
  constexpr long maxSustainAttenuationCentibels = static_cast<long>(kSoundFontVolumeEnvelopeRangeDb * 10.0);
  const double amplitude = std::clamp(envelope.sustainAmplitude.value_or(1.0), 0.0, 1.0);
  const double attenuationDb = amplitude == 0.0
                                   ? kSoundFontVolumeEnvelopeRangeDb
                                   : std::min(-20.0 * std::log10(amplitude), kSoundFontVolumeEnvelopeRangeDb);
  return clampS16(
      static_cast<s32>(std::clamp(attenuationDb * 10.0, 0.0, static_cast<double>(maxSustainAttenuationCentibels))));
}

[[nodiscard]] SfEnvelope sf2Envelope(const Region& region) {
  const Envelope envelope = approximateEnvelopeAsAdsr(region.envelope, kSoundFontVolumeEnvelopeRangeDb);
  return {
      sf2EnvelopeTimecents(envelope.attackSeconds),
      sf2EnvelopeTimecents(envelope.holdSeconds),
      sf2EnvelopeTimecents(envelope.decaySeconds),
      static_cast<s16>(hasExplicitEnvelope(envelope) ? sf2SustainAttenuation(envelope) : 0),
      sf2EnvelopeTimecents(envelope.releaseSeconds),
  };
}

[[nodiscard]] bool sameSampleMap(const ResolvedSynthInstrument& lhs, const ResolvedSynthInstrument& rhs) {
  return lhs.generators == rhs.generators && lhs.modulators == rhs.modulators &&
         std::ranges::equal(lhs.regions, rhs.regions, [](const ResolvedSynthRegion& a, const ResolvedSynthRegion& b) {
           const auto& x = *a.region;
           const auto& y = *b.region;
           return a.sampleIndex == b.sampleIndex && x.keyRange == y.keyRange && x.velocityRange == y.velocityRange &&
                  x.unityKey == y.unityKey && x.loop == y.loop && x.pan == y.pan &&
                  x.attenuationDb == y.attenuationDb && a.generators == b.generators && a.modulators == b.modulators;
         });
}

[[nodiscard]] std::optional<SfEnvelope> presetEnvelopeOffsets(const ResolvedSynthInstrument& preset,
                                                              const ResolvedSynthInstrument& instrument) {
  if (!sameSampleMap(preset, instrument)) {
    return std::nullopt;
  }

  SfEnvelope offsets{};
  for (size_t region = 0; region < preset.regions.size(); ++region) {
    const auto target = sf2Envelope(*preset.regions[region].region);
    const auto base = sf2Envelope(*instrument.regions[region].region);
    for (size_t field = 0; field < offsets.size(); ++field) {
      const s32 difference = static_cast<s32>(target[field]) - base[field];
      if (difference < std::numeric_limits<s16>::min() || difference > std::numeric_limits<s16>::max() ||
          (region != 0 && offsets[field] != difference)) {
        return std::nullopt;
      }
      offsets[field] = static_cast<s16>(difference);
    }
  }
  return offsets;
}

[[nodiscard]] SfLayout sf2Layout(std::span<const ResolvedSynthInstrument> presets) {
  SfLayout layout;
  layout.presets.reserve(presets.size());
  layout.instruments.reserve(presets.size());

  for (const auto& source : presets) {
    SfPreset preset{.source = &source, .instrumentIndex = static_cast<u32>(layout.instruments.size())};
    for (size_t i = 0; i < layout.instruments.size(); ++i) {
      if (const auto offsets = presetEnvelopeOffsets(source, layout.instruments[i])) {
        preset.instrumentIndex = static_cast<u32>(i);
        preset.envelopeOffsets = *offsets;
        break;
      }
    }
    if (preset.instrumentIndex == layout.instruments.size()) {
      layout.instruments.push_back(source);
    }
    layout.presets.push_back(preset);
  }
  return layout;
}

[[nodiscard]] u32 instrumentRegionGeneratorCount(
    const ResolvedSynthRegion& region,
    ModulationConversionPolicy modulationConversion = ModulationConversionPolicy::SynthModulators) {
  return kBaseInstrumentRegionGenerators + kEnvelopeInstrumentRegionGenerators +
         static_cast<u32>(
             std::ranges::count_if(region.generators, [modulationConversion](const SynthGenerator& generator) {
               return shouldExportSynthGenerator(generator, modulationConversion) &&
                      sf2GeneratorForDestination(generator.destination).has_value();
             }));
}

[[nodiscard]] u32 instrumentRegionModulatorCount(
    const ResolvedSynthRegion& region,
    ModulationConversionPolicy modulationConversion = ModulationConversionPolicy::SynthModulators) {
  return static_cast<u32>(
      std::ranges::count_if(region.modulators, [modulationConversion](const SynthModulator& modulator) {
        return sf2ModulatorFor(modulator, nullptr, ModulationScalingPolicy::FullFormatRange, modulationConversion)
            .has_value();
      }));
}

[[nodiscard]] u32 instrumentGlobalGeneratorCount(
    const ResolvedSynthInstrument& instrument,
    ModulationConversionPolicy modulationConversion = ModulationConversionPolicy::SynthModulators) {
  return static_cast<u32>(
      std::ranges::count_if(instrument.generators, [modulationConversion](const SynthGenerator& generator) {
        return shouldExportSynthGenerator(generator, modulationConversion) &&
               sf2GeneratorForDestination(generator.destination).has_value();
      }));
}

[[nodiscard]] u32 instrumentGlobalModulatorCount(
    const ResolvedSynthInstrument& instrument,
    ModulationConversionPolicy modulationConversion = ModulationConversionPolicy::SynthModulators) {
  return static_cast<u32>(
      std::ranges::count_if(instrument.modulators, [modulationConversion](const SynthModulator& modulator) {
        return sf2ModulatorFor(modulator, nullptr, ModulationScalingPolicy::FullFormatRange, modulationConversion)
            .has_value();
      }));
}

[[nodiscard]] bool hasInstrumentGlobalZone(
    const ResolvedSynthInstrument& instrument,
    ModulationConversionPolicy modulationConversion = ModulationConversionPolicy::SynthModulators) {
  return instrumentGlobalGeneratorCount(instrument, modulationConversion) != 0 ||
         instrumentGlobalModulatorCount(instrument, modulationConversion) != 0;
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

void writeEnvelope(std::vector<u8>& bytes, const SfEnvelope& envelope, bool omitZero = false) {
  for (size_t i = 0; i < envelope.size(); ++i) {
    if (!omitZero || envelope[i] != 0) {
      writeAmountGen(bytes, kSfEnvelopeGenerators[i], envelope[i]);
    }
  }
}

void writeWordGen(std::vector<u8>& bytes, u16 generator, u16 value) {
  writeLe16(bytes, generator);
  writeLe16(bytes, value);
}

void writeIndex(std::vector<u8>& bytes, u64 value) {
  if (value > std::numeric_limits<u16>::max()) {
    throw std::overflow_error("SoundFont2 table index exceeds 16 bits");
  }
  writeLe16(bytes, static_cast<u16>(value));
}

[[nodiscard]] Chunk phdrChunk(std::span<const SfPreset> presets) {
  // MIDI bank/program addresses remain one-to-one with presets even when several
  // envelope variants share the same sample-mapped SF2 instrument.
  std::vector<u8> payload;
  for (u32 i = 0; i < presets.size(); ++i) {
    const auto& resolved = *presets[i].source;
    const auto& instrument = *resolved.instrument;
    writeFixedString(payload, sf2Name(instrument.name, "Preset"), 20);
    writeLe16(payload, clampU16(resolved.address.program));
    writeLe16(payload, sf2Bank(resolved.address.bank));
    writeIndex(payload, i);
    writeLe32(payload, 0);
    writeLe32(payload, 0);
    writeLe32(payload, 0);
  }

  writeFixedString(payload, "EOP", 20);
  writeLe16(payload, 0);
  writeLe16(payload, 0);
  writeIndex(payload, presets.size());
  writeLe32(payload, 0);
  writeLe32(payload, 0);
  writeLe32(payload, 0);
  return makeChunk("phdr", std::move(payload));
}

[[nodiscard]] Chunk pbagChunk(std::span<const SfPreset> presets) {
  std::vector<u8> payload;
  u32 generatorIndex = 0;
  for (const auto& preset : presets) {
    writeIndex(payload, generatorIndex);
    writeLe16(payload, 0);
    generatorIndex += 2 + std::ranges::count_if(preset.envelopeOffsets, [](s16 value) { return value != 0; });
  }
  writeIndex(payload, generatorIndex);
  writeLe16(payload, 0);
  return makeChunk("pbag", std::move(payload));
}

[[nodiscard]] Chunk pgenChunk(std::span<const SfPreset> presets) {
  std::vector<u8> payload;
  for (const auto& preset : presets) {
    writeAmountGen(payload, kSfGenReverbEffectsSend, sf2ReverbSend(preset.source->instrument->reverb));
    writeEnvelope(payload, preset.envelopeOffsets, true);
    writeLe16(payload, kSfGenInstrument);
    writeIndex(payload, preset.instrumentIndex);
  }
  writeWordGen(payload, 0, 0);
  return makeChunk("pgen", std::move(payload));
}

[[nodiscard]] Chunk terminalModChunk(std::string id) {
  std::vector<u8> payload(10);
  return makeChunk(std::move(id), std::move(payload));
}

[[nodiscard]] Chunk instChunk(std::span<const ResolvedSynthInstrument> instruments,
                              ModulationConversionPolicy modulationConversion) {
  // SF2 instruments point into bag tables. Instruments with global generators/modulators
  // get one global bag before their sample regions.
  std::vector<u8> payload;
  u32 bagIndex = 0;
  for (const auto& instrument : instruments) {
    writeFixedString(payload, sf2Name(instrument.instrument->name, "Instrument"), 20);
    writeIndex(payload, bagIndex);
    bagIndex += static_cast<u32>(instrument.regions.size()) +
                (hasInstrumentGlobalZone(instrument, modulationConversion) ? 1 : 0);
  }

  writeFixedString(payload, "EOI", 20);
  writeIndex(payload, bagIndex);
  return makeChunk("inst", std::move(payload));
}

[[nodiscard]] Chunk ibagChunk(std::span<const ResolvedSynthInstrument> instruments,
                              ModulationConversionPolicy modulationConversion) {
  // Bags are index pairs into generator/modulator arrays. Counts must be predicted before
  // writing igen/imod so the table offsets line up exactly.
  std::vector<u8> payload;
  u32 generatorIndex = 0;
  u32 modulatorIndex = 0;
  for (const auto& instrument : instruments) {
    if (hasInstrumentGlobalZone(instrument, modulationConversion)) {
      writeIndex(payload, generatorIndex);
      writeIndex(payload, modulatorIndex);
      generatorIndex += instrumentGlobalGeneratorCount(instrument, modulationConversion);
      modulatorIndex += instrumentGlobalModulatorCount(instrument, modulationConversion);
    }

    for (const auto& region : instrument.regions) {
      writeIndex(payload, generatorIndex);
      writeIndex(payload, modulatorIndex);
      generatorIndex += instrumentRegionGeneratorCount(region, modulationConversion);
      modulatorIndex += instrumentRegionModulatorCount(region, modulationConversion);
    }
  }

  writeIndex(payload, generatorIndex);
  writeIndex(payload, modulatorIndex);
  return makeChunk("ibag", std::move(payload));
}

[[nodiscard]] Chunk imodChunk(std::span<const ResolvedSynthInstrument> instruments,
                              const MidiModulationUsage* midiModulationUsage, ModulationScalingPolicy modulationScaling,
                              ModulationConversionPolicy modulationConversion) {
  std::vector<u8> payload;
  for (const auto& instrument : instruments) {
    for (const auto& modulator : instrument.modulators) {
      const auto record = sf2ModulatorFor(modulator, midiModulationUsage, modulationScaling, modulationConversion);
      if (!record) {
        continue;
      }

      writeWordGen(payload, record->source, record->destination);
      writeLeS16(payload, record->amount);
      writeLe16(payload, 0);
      writeLe16(payload, kSfTransformLinear);
    }
    for (const auto& region : instrument.regions) {
      for (const auto& modulator : region.modulators) {
        const auto record = sf2ModulatorFor(modulator, midiModulationUsage, modulationScaling, modulationConversion);
        if (!record) {
          continue;
        }

        writeWordGen(payload, record->source, record->destination);
        writeLeS16(payload, record->amount);
        writeLe16(payload, 0);
        writeLe16(payload, kSfTransformLinear);
      }
    }
  }

  payload.insert(payload.end(), 10, 0);
  return makeChunk("imod", std::move(payload));
}

[[nodiscard]] Chunk igenChunk(std::span<const ResolvedSynthInstrument> instruments,
                              std::span<const DecodedSfSample> samplesByIndex,
                              ModulationConversionPolicy modulationConversion) {
  // Region generators are written in SF2's required order: ranges and placement first,
  // then envelope/tuning/sample linkage. Unsupported SynthGenerator destinations are skipped.
  std::vector<u8> payload;
  for (const auto& instrument : instruments) {
    for (const auto& generator : instrument.generators) {
      if (!shouldExportSynthGenerator(generator, modulationConversion)) {
        continue;
      }
      const auto sf2Generator = sf2GeneratorForDestination(generator.destination);
      if (!sf2Generator) {
        continue;
      }

      writeAmountGen(payload, *sf2Generator, sf2GeneratorAmount(generator));
    }
    for (const auto& sfRegion : instrument.regions) {
      const auto& region = *sfRegion.region;
      const auto& sample = samplesByIndex[sfRegion.sampleIndex];
      const auto pitch = sf2RegionPitch(region);

      writeRangeGen(payload, kSfGenKeyRange, region.keyRange.low, region.keyRange.high);
      writeRangeGen(payload, kSfGenVelRange, region.velocityRange.low, region.velocityRange.high);
      for (const auto& generator : sfRegion.generators) {
        if (!shouldExportSynthGenerator(generator, modulationConversion)) {
          continue;
        }
        const auto sf2Generator = sf2GeneratorForDestination(generator.destination);
        if (sf2Generator) {
          writeAmountGen(payload, *sf2Generator, sf2GeneratorAmount(generator));
        }
      }
      writeAmountGen(payload, kSfGenInitialAttenuation,
                     static_cast<s16>(sf2Attenuation(region, Sample{.attenuationDb = sample.attenuationDb})));
      writeAmountGen(payload, kSfGenPan, sf2Pan(region.pan));
      writeAmountGen(payload, kSfGenCoarseTune, pitch.coarseTune);
      writeAmountGen(payload, kSfGenFineTune, pitch.fineTune);
      writeEnvelope(payload, sf2Envelope(region));
      writeWordGen(payload, kSfGenOverridingRootKey, pitch.rootKey);
      writeWordGen(payload, kSfGenSampleModes, effectiveSfLoop(region, sample).enabled ? 1 : 0);
      writeWordGen(payload, kSfGenSampleId, sfRegion.sampleIndex);
    }
  }

  writeWordGen(payload, 0, 0);
  return makeChunk("igen", std::move(payload));
}

[[nodiscard]] std::vector<SfSampleHeaderInfo> sampleHeaderInfo(std::span<const DecodedSfSample> samples,
                                                               std::span<const ResolvedSynthInstrument> instruments) {
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
      const auto pitch = sf2RegionPitch(*sfRegion.region);
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

[[nodiscard]] std::vector<Chunk> pdtaChunks(const SfLayout& layout, std::span<const DecodedSfSample> samples,
                                            const MidiModulationUsage* midiModulationUsage,
                                            ModulationScalingPolicy modulationScaling,
                                            ModulationConversionPolicy modulationConversion) {
  return {
      phdrChunk(layout.presets),
      pbagChunk(layout.presets),
      terminalModChunk("pmod"),
      pgenChunk(layout.presets),
      instChunk(layout.instruments, modulationConversion),
      ibagChunk(layout.instruments, modulationConversion),
      imodChunk(layout.instruments, midiModulationUsage, modulationScaling, modulationConversion),
      igenChunk(layout.instruments, samples, modulationConversion),
      shdrChunk(samples, layout.instruments),
  };
}

}  // namespace

SynthExportResult buildSoundFont2(const SynthExportInput& input, const SourceStore& sources) {
  // SoundFont 2 sample data is mono PCM16. Shared decode/resolve helpers do the expensive
  // source work once before this file-specific table writer assembles RIFF chunks.
  auto [decodedSamples, instruments, diagnostics] =
      prepareSynthData(input, sources,
                       SynthSampleDecodeOptions{
                           .requireMono = true,
                           .nonMonoWarning = "Skipping non-mono sample for SoundFont2 export",
                       });
  auto samples = sf2Samples(std::move(decodedSamples));

  if (samples.empty()) {
    diagnostics.push_back(exportError("No decodable samples available for SoundFont2 export"));
    return SynthExportResult{.diagnostics = std::move(diagnostics)};
  }
  if (instruments.empty()) {
    diagnostics.push_back(exportError("No playable instruments available for SoundFont2 export"));
    return SynthExportResult{.diagnostics = std::move(diagnostics)};
  }
  const auto layout = sf2Layout(instruments);
  return SynthExportResult{
      .bytes = makeRiff("sfbk",
                        {
                            makeListChunk("INFO", infoChunks(sf2Name(input.name, "VGMTrans"))),
                            makeListChunk("sdta", {smplChunk(samples)}),
                            makeListChunk("pdta", pdtaChunks(layout, samples, input.midiModulationUsage,
                                                             input.modulationScaling, input.modulationConversion)),
                        }),
      .diagnostics = std::move(diagnostics),
  };
}

}  // namespace vgmtrans::core
