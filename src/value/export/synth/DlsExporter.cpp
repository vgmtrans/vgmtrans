/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/export/synth/SynthExportData.h"

#include "value/export/BinaryWriter.h"
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

constexpr u16 kWaveFormatPcm = 1;
constexpr u16 kBitsPerSample = 16;
constexpr u16 kDlsConnSrcNone = 0;
constexpr u16 kDlsConnSrcLfo = 0x0001;
constexpr u16 kDlsConnSrcKeyOnVelocity = 0x0002;
constexpr u16 kDlsConnSrcKeyNumber = 0x0003;
constexpr u16 kDlsConnSrcEg1 = 0x0004;
constexpr u16 kDlsConnSrcPitchWheel = 0x0006;
constexpr u16 kDlsConnSrcPolyPressure = 0x0007;
constexpr u16 kDlsConnSrcChannelPressure = 0x0008;
constexpr u16 kDlsConnSrcVibrato = 0x0009;
constexpr u16 kDlsConnSrcCc1 = 0x0081;
constexpr u16 kDlsConnSrcCc91 = 0x00db;
constexpr u16 kDlsConnSrcCc93 = 0x00dd;
constexpr u16 kDlsConnDstAttenuation = 0x0001;
constexpr u16 kDlsConnDstPitch = 0x0003;
constexpr u16 kDlsConnDstPan = 0x0004;
constexpr u16 kDlsConnDstLfoFrequency = 0x0104;
constexpr u16 kDlsConnDstLfoStartDelay = 0x0105;
constexpr u16 kDlsConnDstVibFrequency = 0x0114;
constexpr u16 kDlsConnDstVibStartDelay = 0x0115;
constexpr u16 kDlsConnDstEg1AttackTime = 0x0206;
constexpr u16 kDlsConnDstEg1DecayTime = 0x0207;
constexpr u16 kDlsConnDstEg1ReleaseTime = 0x0209;
constexpr u16 kDlsConnDstEg1SustainLevel = 0x020a;
constexpr u16 kDlsConnDstEg1HoldTime = 0x020c;
constexpr u16 kDlsConnTrnNone = 0;
constexpr s32 kDlsSustainLevelFullScale = 0x03e80000;

using Chunk = RiffChunk;

using DecodedDlsSample = DecodedSynthSample;

struct DlsConnection {
  // DLS articulation is a list of source/control/destination/scale connections. This is
  // the DLS equivalent of SF2 generators and modulators.
  u16 source = kDlsConnSrcNone;
  u16 control = kDlsConnSrcNone;
  u16 destination = 0;
  s32 scale = 0;
};

void writeFixedString(std::vector<u8>& bytes, std::string_view text) {
  writeAscii(bytes, text);
  bytes.push_back(0);
}

[[nodiscard]] std::string dlsName(std::string name, std::string_view fallback) {
  if (name.empty()) {
    return std::string(fallback);
  }
  return name;
}

[[nodiscard]] u8 clampU7(s32 value) {
  return static_cast<u8>(std::clamp<s32>(value, 0, 127));
}

[[nodiscard]] s16 clampS16(s32 value) {
  return static_cast<s16>(std::clamp<s32>(value, std::numeric_limits<s16>::min(), std::numeric_limits<s16>::max()));
}

[[nodiscard]] s32 clampS32(s64 value) {
  return static_cast<s32>(std::clamp<s64>(value, std::numeric_limits<s32>::min(), std::numeric_limits<s32>::max()));
}

[[nodiscard]] s32 dls16Dot16Scale(s32 value) {
  // DLS articulation scales are signed 16.16 fixed-point values.
  return clampS32(static_cast<s64>(value) * 65536);
}

[[nodiscard]] s32 dlsPitchScale(s32 cents) {
  return dls16Dot16Scale(cents);
}

[[nodiscard]] s32 dlsAttenuation(const Region& region, const DecodedDlsSample& sample) {
  constexpr double centibelsPerDb = 10.0;
  const double units = std::clamp((region.attenuationDb + sample.attenuationDb) * centibelsPerDb * 65536.0, 0.0,
                                  static_cast<double>(std::numeric_limits<s32>::max()));
  return -static_cast<s32>(units);
}

[[nodiscard]] std::optional<DlsConnection> dlsConnectionForGenerator(const SynthGenerator& generator) {
  // Generators with no controller source become DLS connections that are always active.
  switch (generator.destination) {
    case SynthDestination::Pitch:
      return DlsConnection{.destination = kDlsConnDstPitch, .scale = dlsPitchScale(generator.amount)};
    case SynthDestination::VolumeAttenuation:
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
    case SynthDestination::VibratoDelay:
      return DlsConnection{.destination = kDlsConnDstVibStartDelay, .scale = dls16Dot16Scale(generator.amount)};
    case SynthDestination::TremoloDepth:
      return DlsConnection{
          .source = kDlsConnSrcLfo,
          .destination = kDlsConnDstAttenuation,
          .scale = dls16Dot16Scale(generator.amount),
      };
    case SynthDestination::TremoloRate:
      return DlsConnection{.destination = kDlsConnDstLfoFrequency, .scale = dlsPitchScale(generator.amount)};
    case SynthDestination::TremoloDelay:
      return DlsConnection{.destination = kDlsConnDstLfoStartDelay, .scale = dls16Dot16Scale(generator.amount)};
    case SynthDestination::FilterCutoff:
    case SynthDestination::Unknown:
      return std::nullopt;
  }

  return std::nullopt;
}

[[nodiscard]] std::optional<u16> dlsSourceForSynthSource(SynthSource source) {
  switch (source) {
    case SynthSource::NoteOnVelocity:
      return kDlsConnSrcKeyOnVelocity;
    case SynthSource::KeyNumber:
      return kDlsConnSrcKeyNumber;
    case SynthSource::Lfo:
      return kDlsConnSrcLfo;
    case SynthSource::Envelope:
      return kDlsConnSrcEg1;
    case SynthSource::MidiController:
      return std::nullopt;
    case SynthSource::ChannelPressure:
      return kDlsConnSrcChannelPressure;
    case SynthSource::PolyPressure:
      return kDlsConnSrcPolyPressure;
    case SynthSource::PitchWheel:
      return kDlsConnSrcPitchWheel;
    case SynthSource::Unknown:
      return std::nullopt;
  }

  return std::nullopt;
}

[[nodiscard]] std::optional<u16> dlsDefaultSourceForDestination(SynthDestination destination) {
  switch (destination) {
    case SynthDestination::VibratoDepth:
      return kDlsConnSrcCc1;
    case SynthDestination::VibratoRate:
    case SynthDestination::TremoloRate:
      return kDlsConnSrcChannelPressure;
    case SynthDestination::VibratoDelay:
    case SynthDestination::TremoloDepth:
    case SynthDestination::VolumeAttenuation:
      return kDlsConnSrcCc93;
    case SynthDestination::TremoloDelay:
      return kDlsConnSrcCc91;
    case SynthDestination::Pitch:
    case SynthDestination::FilterCutoff:
    case SynthDestination::Pan:
    case SynthDestination::Unknown:
      return std::nullopt;
  }

  return std::nullopt;
}

[[nodiscard]] std::optional<DlsConnection> dlsConnectionForModulator(
    const SynthModulator& modulator, const MidiModulationUsage* midiModulationUsage = nullptr,
    ModulationScalingPolicy modulationScaling = ModulationScalingPolicy::FullFormatRange,
    ModulationConversionPolicy modulationConversion = ModulationConversionPolicy::SynthModulators) {
  // DLS represents vibrato/tremolo depth as LFO or vibrato sources controlled by another
  // source. That is why depth destinations set source/control separately below.
  if (!shouldExportSynthModulator(modulator, modulationConversion)) {
    return std::nullopt;
  }
  const auto source = modulator.source ? dlsSourceForSynthSource(*modulator.source)
                                       : dlsDefaultSourceForDestination(modulator.destination);
  if (!source) {
    return std::nullopt;
  }

  const auto amount = scaledSynthModulatorAmount(modulator, midiModulationUsage, modulationScaling);

  switch (modulator.destination) {
    case SynthDestination::Pitch:
      return DlsConnection{
          .source = *source,
          .destination = kDlsConnDstPitch,
          .scale = dlsPitchScale(amount),
      };
    case SynthDestination::VolumeAttenuation:
      return DlsConnection{
          .source = *source,
          .destination = kDlsConnDstAttenuation,
          .scale = dls16Dot16Scale(amount),
      };
    case SynthDestination::Pan:
      return DlsConnection{
          .source = *source,
          .destination = kDlsConnDstPan,
          .scale = dls16Dot16Scale(amount),
      };
    case SynthDestination::VibratoDepth:
      return DlsConnection{
          .source = kDlsConnSrcVibrato,
          .control = *source,
          .destination = kDlsConnDstPitch,
          .scale = dlsPitchScale(amount),
      };
    case SynthDestination::VibratoRate:
      return DlsConnection{
          .source = *source,
          .destination = kDlsConnDstVibFrequency,
          .scale = dlsPitchScale(amount),
      };
    case SynthDestination::VibratoDelay:
      return std::nullopt;
    case SynthDestination::TremoloDelay:
      return DlsConnection{
          .source = *source,
          .destination = kDlsConnDstLfoStartDelay,
          .scale = dls16Dot16Scale(amount),
      };
    case SynthDestination::TremoloDepth:
      return DlsConnection{
          .source = kDlsConnSrcLfo,
          .control = *source,
          .destination = kDlsConnDstAttenuation,
          .scale = dls16Dot16Scale(amount),
      };
    case SynthDestination::TremoloRate:
      return DlsConnection{
          .source = *source,
          .destination = kDlsConnDstLfoFrequency,
          .scale = dlsPitchScale(amount),
      };
    case SynthDestination::FilterCutoff:
    case SynthDestination::Unknown:
      return std::nullopt;
  }

  return std::nullopt;
}

[[nodiscard]] s32 dlsEnvelopeTimecents(std::optional<double> seconds) {
  if (seconds && std::isfinite(*seconds) && *seconds > 0.0) {
    const double timecents = 1200.0 * std::log2(*seconds) * 65536.0;
    return static_cast<s32>(std::clamp(std::lround(timecents), static_cast<long>(std::numeric_limits<s32>::min()),
                                       static_cast<long>(std::numeric_limits<s32>::max())));
  }
  return std::numeric_limits<s32>::min();
}

[[nodiscard]] s32 dlsSustainLevel(const Envelope& envelope) {
  const double amplitude = std::clamp(envelope.sustainAmplitude.value_or(1.0), 0.0, 1.0);
  const double attenuationDb = std::clamp(-20.0 * std::log10(amplitude), 0.0, 96.0);
  const double scaledLevel = ((96.0 - attenuationDb) / 96.0) * kDlsSustainLevelFullScale;
  return static_cast<s32>(std::clamp(std::lround(scaledLevel), 0l, static_cast<long>(kDlsSustainLevelFullScale)));
}

[[nodiscard]] s32 dlsPanScale(double pan) {
  const auto tenthPercentUnits = static_cast<s32>(std::lround(std::clamp(pan, 0.0, 1.0) * 1000.0)) - 500;
  return dls16Dot16Scale(tenthPercentUnits);
}

[[nodiscard]] Chunk colhChunk(std::span<const ResolvedSynthInstrument> instruments) {
  std::vector<u8> payload;
  writeLe32(payload, static_cast<u32>(instruments.size()));
  return makeChunk("colh", std::move(payload));
}

[[nodiscard]] Chunk infoList(std::string name) {
  std::vector<u8> inam;
  writeFixedString(inam, dlsName(std::move(name), "DLS"));
  return makeListChunk("INFO", {makeChunk("INAM", std::move(inam))});
}

[[nodiscard]] Chunk inshChunk(const ResolvedSynthInstrument& instrument) {
  const u32 dlsBank = (instrument.address.bank & 0x7f) << 8;
  std::vector<u8> payload;
  writeLe32(payload, static_cast<u32>(instrument.regions.size()));
  writeLe32(payload, dlsBank);
  writeLe32(payload, instrument.address.program);
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
  // wsmp carries sample playback metadata for a region: unity key, fine tune,
  // attenuation, and loop points.
  const Loop loop = effectiveRegionLoop(region, sample);
  const double effectiveUnityKey = region.unityKey - (sample.pitch.cents / 100.0);
  const u8 unityKey = clampU7(static_cast<s32>(std::lround(effectiveUnityKey)));
  const s16 fineTune = clampS16(static_cast<s32>(std::lround((unityKey - effectiveUnityKey) * 100.0)));

  std::vector<u8> payload;
  writeLe32(payload, 20);
  writeLe16(payload, unityKey);
  writeLeS16(payload, fineTune);
  writeLeS32(payload, dlsAttenuation(region, sample));
  writeLe32(payload, 1);
  writeLe32(payload, loop.enabled ? 1 : 0);
  if (loop.enabled) {
    writeLe32(payload, 16);
    writeLe32(payload, 0);
    writeLe32(payload, loop.start);
    writeLe32(payload, loop.length);
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

[[nodiscard]] Chunk art2Chunk(const ResolvedSynthInstrument& instrument, const Region& region,
                              const MidiModulationUsage* midiModulationUsage, ModulationScalingPolicy modulationScaling,
                              ModulationConversionPolicy modulationConversion) {
  // Each region gets a DLS2 articulation list. Region envelope/pan is always written;
  // instrument generators/modulators are appended as additional connections.
  std::vector<u8> connections;
  writeConnection(connections, kDlsConnDstPan, dlsPanScale(region.pan));
  const bool explicitEnvelope = hasExplicitEnvelope(region.envelope);
  writeConnection(connections, kDlsConnDstEg1AttackTime, dlsEnvelopeTimecents(region.envelope.attackSeconds));
  writeConnection(connections, kDlsConnDstEg1HoldTime, dlsEnvelopeTimecents(region.envelope.holdSeconds));
  writeConnection(connections, kDlsConnDstEg1DecayTime, dlsEnvelopeTimecents(region.envelope.decaySeconds));
  writeConnection(connections, kDlsConnDstEg1SustainLevel,
                  explicitEnvelope ? dlsSustainLevel(region.envelope) : kDlsSustainLevelFullScale);
  writeConnection(connections, kDlsConnDstEg1ReleaseTime, dlsEnvelopeTimecents(region.envelope.releaseSeconds));
  for (const auto& generator : instrument.generators) {
    if (!shouldExportSynthGenerator(generator, modulationConversion)) {
      continue;
    }
    const auto connection = dlsConnectionForGenerator(generator);
    if (!connection) {
      continue;
    }
    writeConnection(connections, *connection);
  }
  for (const auto& modulator : instrument.modulators) {
    const auto connection =
        dlsConnectionForModulator(modulator, midiModulationUsage, modulationScaling, modulationConversion);
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

[[nodiscard]] Chunk rgn2Chunk(const ResolvedSynthInstrument& instrument, const ResolvedSynthRegion& resolvedRegion,
                              std::span<const DecodedDlsSample> samples, const MidiModulationUsage* midiModulationUsage,
                              ModulationScalingPolicy modulationScaling,
                              ModulationConversionPolicy modulationConversion) {
  const auto& region = *resolvedRegion.region;
  const auto& sample = samples[resolvedRegion.sampleIndex];
  return makeListChunk("rgn2",
                       {
                           rgnhChunk(region),
                           wsmpChunk(region, sample),
                           wlnkChunk(resolvedRegion.sampleIndex),
                           art2Chunk(instrument, region, midiModulationUsage, modulationScaling, modulationConversion),
                       });
}

[[nodiscard]] Chunk lrgnList(const ResolvedSynthInstrument& instrument, std::span<const DecodedDlsSample> samples,
                             const MidiModulationUsage* midiModulationUsage, ModulationScalingPolicy modulationScaling,
                             ModulationConversionPolicy modulationConversion) {
  std::vector<Chunk> regions;
  regions.reserve(instrument.regions.size());
  for (const auto& region : instrument.regions) {
    regions.push_back(
        rgn2Chunk(instrument, region, samples, midiModulationUsage, modulationScaling, modulationConversion));
  }
  return makeListChunk("lrgn", std::move(regions));
}

[[nodiscard]] Chunk insList(const ResolvedSynthInstrument& instrument, std::span<const DecodedDlsSample> samples,
                            const MidiModulationUsage* midiModulationUsage, ModulationScalingPolicy modulationScaling,
                            ModulationConversionPolicy modulationConversion) {
  return makeListChunk("ins ",
                       {
                           inshChunk(instrument),
                           lrgnList(instrument, samples, midiModulationUsage, modulationScaling, modulationConversion),
                           infoList(dlsName(instrument.instrument->name, "Instrument")),
                       });
}

[[nodiscard]] Chunk linsList(std::span<const ResolvedSynthInstrument> instruments,
                             std::span<const DecodedDlsSample> samples, const MidiModulationUsage* midiModulationUsage,
                             ModulationScalingPolicy modulationScaling,
                             ModulationConversionPolicy modulationConversion) {
  std::vector<Chunk> instrumentChunks;
  instrumentChunks.reserve(instruments.size());
  for (const auto& instrument : instruments) {
    instrumentChunks.push_back(
        insList(instrument, samples, midiModulationUsage, modulationScaling, modulationConversion));
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
  // The pool table points to each wave chunk inside wvpl by byte offset, not by sample id.
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

SynthExportResult buildDls(const SynthExportInput& input, const SourceStore& sources) {
  SynthExportResult result;

  // DLS accepts the decoded PCM view directly. After shared sample/instrument resolution,
  // this function is mostly RIFF table assembly.
  auto samples = decodeSynthSamples(input.sampleCollections, sources, result.diagnostics);
  for (auto& sample : samples) {
    sample.name = dlsName(std::move(sample.name), "Wave");
  }
  const auto samplesByReference = synthSampleIndexMap(samples);
  auto instruments =
      resolveSynthInstruments(input.instrumentSets, input.sampleCollections, samplesByReference, result.diagnostics);

  if (samples.empty()) {
    result.diagnostics.push_back(exportError("No decodable samples available for DLS export"));
    return result;
  }

  auto waves = waveChunks(samples);
  result.bytes = makeRiff("DLS ", {
                                      colhChunk(instruments),
                                      linsList(instruments, samples, input.midiModulationUsage, input.modulationScaling,
                                               input.modulationConversion),
                                      ptblChunk(waves),
                                      makeListChunk("wvpl", std::move(waves)),
                                      infoList(dlsName(input.name, "DLS")),
                                  });
  return result;
}

}  // namespace vgmtrans::core
