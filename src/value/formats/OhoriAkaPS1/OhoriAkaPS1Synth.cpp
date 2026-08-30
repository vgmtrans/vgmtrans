/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/OhoriAkaPS1/OhoriAkaPS1.h"

#include "value/base/RecordReader.h"
#include "value/synth/PsxAdpcm.h"
#include "value/synth/PsxSpu.h"
#include "value/synth/SynthMath.h"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <utility>

namespace vgmtrans::formats::ohori_aka_ps1 {

using namespace core;

namespace {

constexpr u32 kRegionSize = 0x10;

[[nodiscard]] double unityKey(u8 semitone, u8 fine) {
  // The driver masks the integral result to seven bits before looking up the
  // SPU pitch. Preserve that wrap here so values such as 0xfb describe key
  // 125, rather than an unrepresentable negative MIDI root key.
  const double key = std::fmod(120.0 - semitone - fine / 256.0, 128.0);
  return key < 0.0 ? key + 128.0 : key;
}

[[nodiscard]] u8 sustainDirection(u8 mode) {
  // libspu accepts 1/5 as increasing, 7 as exponential decreasing,
  // and treats the remaining modes as linear decreasing.
  return mode == 1 || mode == 5 ? 0 : 1;
}

[[nodiscard]] OhoriAkaPs1Region readRegion(ByteReader reader, u32 offset) {
  RecordReader record(reader, offset, offset + kRegionSize);
  const u32 sampleOffset = *record.u32leAt(0, "sample_offset", SourceValueDisplay::Address);
  const u8 volume = *record.u8At(4, "volume");
  const u8 keyHigh = *record.u8At(5, "key_high");
  const u8 semitone = *record.u8At(6, "semitone_tune");
  const u8 fine = *record.u8At(7, "fine_tune");
  const u8 modes = *record.u8At(8, "sustain_release_modes", SourceValueDisplay::Hex);
  const u8 attackMode = *record.u8At(9, "attack_mode", SourceValueDisplay::Hex) & 0x0f;
  const bool reverb = *record.u8At(10, "reverb") != 0;
  const u8 rawPan = *record.u8At(11, "pan_override", SourceValueDisplay::Hex);
  const u32 rates = *record.u32leAt(12, "adsr_rates", SourceValueDisplay::Hex);
  const u8 sustainMode = modes >> 4;
  const u8 releaseMode = modes & 0x0f;
  const u8 attackRate = (rates >> 20) & 0x7f;
  const u8 decayRate = (rates >> 16) & 0x0f;
  const u8 sustainRate = (rates >> 9) & 0x7f;
  const u8 releaseRate = (rates >> 4) & 0x1f;
  const u8 sustainLevel = rates & 0x0f;
  const u16 adsr1 = composePsxAdsr1(attackMode == 5, attackRate, decayRate, sustainLevel);
  const u16 adsr2 = composePsxAdsr2(sustainMode == 5 || sustainMode == 7, sustainDirection(sustainMode), sustainRate,
                                    releaseMode == 7, releaseRate);
  record.derived("adsr1", adsr1, SourceValueDisplay::Hex);
  record.derived("adsr2", adsr2, SourceValueDisplay::Hex);
  return OhoriAkaPs1Region{
      .offset = offset,
      .sampleOffset = sampleOffset,
      .volume = volume,
      .keyLow = 0,
      .keyHigh = keyHigh,
      .unityKey = unityKey(semitone, fine),
      .panOverride = (rawPan & 0x80) != 0 ? std::optional<u8>(rawPan & 0x7f) : std::nullopt,
      .reverb = reverb,
      .adsr1 = adsr1,
      .adsr2 = adsr2,
      .source = std::move(record).finish(),
  };
}

[[nodiscard]] std::vector<OhoriAkaPs1Region> effectiveRegions(const std::vector<OhoriAkaPs1Region>& raw) {
  std::vector<OhoriAkaPs1Region> regions;
  for (u16 key = 0; key < 128 && !raw.empty(); ++key) {
    auto selected = std::ranges::find_if(raw, [key](const auto& region) { return key <= region.keyHigh; });
    if (selected == raw.end()) selected = raw.begin();
    if (regions.empty() || regions.back().offset != selected->offset) {
      regions.push_back(*selected);
      regions.back().keyLow = static_cast<u8>(key);
    }
    regions.back().keyHigh = static_cast<u8>(key);
  }
  return regions;
}

[[nodiscard]] std::vector<OhoriAkaPs1Instrument> readInstruments(ByteReader reader,
                                                                 const OhoriAkaPs1BankLayout& layout) {
  std::vector<OhoriAkaPs1Instrument> instruments;
  instruments.reserve(layout.instrumentCount);
  for (u32 program = 0; program < layout.instrumentCount; ++program) {
    const u32 offset = layout.instrumentAddresses[program];
    const u32 count = reader.le32(offset);
    RecordReader record(reader, offset, offset + 4 + count * kRegionSize);
    (void)record.u32leAt(0, "region_count");
    OhoriAkaPs1Instrument instrument{
        .program = static_cast<u8>(program),
        .source = std::move(record).finish(),
    };
    std::vector<OhoriAkaPs1Region> raw;
    raw.reserve(count);
    for (u32 region = 0; region < count; ++region) {
      raw.push_back(readRegion(reader, offset + 4 + region * kRegionSize));
    }
    instrument.regions = effectiveRegions(raw);
    instruments.push_back(std::move(instrument));
  }
  return instruments;
}

}  // namespace

std::optional<OhoriAkaPs1ScannedBank> addOhoriAkaPs1Bank(ScanResultBuilder& result,
                                                         const OhoriAkaPs1BankLayout& layout) {
  const ByteReader reader = result.reader();
  auto parsed = readInstruments(reader, layout);
  if (layout.sampleDataLength == 0) {
    return std::nullopt;
  }
  std::set<u32> offsets;
  for (const auto& instrument : parsed) {
    for (const auto& region : instrument.regions) {
      offsets.insert(region.sampleOffset);
    }
  }
  std::map<u32, PsxAdpcmStream> streams;
  for (auto current = offsets.begin(); current != offsets.end(); ++current) {
    const u32 start = layout.sampleDataOffset + *current;
    const u32 boundary = std::next(current) == offsets.end()
                             ? static_cast<u32>(std::min<u64>(reader.size(), layout.sampleDataOffset + layout.sampleDataLength))
                             : layout.sampleDataOffset + *std::next(current);
    if (auto stream = inspectPsxAdpcmStream(reader, start, boundary)) {
      streams.emplace(*current, *stream);
    }
  }
  if (streams.empty()) {
    return std::nullopt;
  }

  auto bank = result.soundBank("OhoriAkaPS1 Bank");
  auto& instruments = bank.instruments();
  auto& samples = bank.localSamples();
  instruments.include(reader.range(layout.offset, layout.length));
  u32 sampleDataEnd = layout.sampleDataOffset;
  for (const auto& [offset, stream] : streams) {
    sampleDataEnd = static_cast<u32>(std::max<u64>(sampleDataEnd, stream.encodedData.endOffset()));
  }
  const u32 usedSampleDataLength = sampleDataEnd - layout.sampleDataOffset;
  samples.include(reader.range(layout.sampleDataOffset, usedSampleDataLength));
  const auto sampleRoot = samples
                              .source(SourceRole::SamplePool, "PS1 ADPCM Sample Data",
                                      reader.range(layout.sampleDataOffset, usedSampleDataLength),
                                      "ohori-aka-ps1-sample-data")
                              .id();
  std::map<u32, SampleRef> sampleRefs;
  for (const auto& [offset, stream] : streams) {
    const u32 index = static_cast<u32>(sampleRefs.size());
    auto sample = samples.add(offset, Sample{
                                          .name = fmt::format("Sample {}", index),
                                          .codec = AudioCodec::PsxAdpcm,
                                          .encodedData = stream.encodedData,
                                          .sampleRate = kPs1SpuSampleRate,
                                          .channels = 1,
                                          .bitsPerSample = 16,
                                          .loop = stream.loop,
                                      });
    sample.source(fmt::format("Sample {}", index), stream.encodedData, "psx-adpcm-sample").parent(sampleRoot);
    sampleRefs.emplace(offset, sample.ref());
  }

  const auto instrumentRoot = instruments
                                  .source(SourceRole::Table, "Instrument Bank", reader.range(layout.offset, layout.length),
                                          "ohori-aka-ps1-instrument-bank")
                                  .id();
  for (const auto& source : parsed) {
    auto instrument = instruments.append(Instrument{
        .explicitAddress = InstrumentAddress{.bank = 0, .program = source.program},
        .identity = ohoriAkaPs1InstrumentIdentity(source.program),
        .name = fmt::format("Instrument {}", source.program),
        .range = source.source.range,
    });
    instrument.source(fmt::format("Instrument {}", source.program), source.source, "ohori-aka-ps1-instrument")
        .parent(instrumentRoot);
    for (const auto& regionSource : source.regions) {
      const auto sample = sampleRefs.find(regionSource.sampleOffset);
      if (sample == sampleRefs.end()) continue;
      Region region{
          .keyRange = KeyRange{.low = regionSource.keyLow, .high = regionSource.keyHigh},
          .range = regionSource.source.range,
          .unityKey = regionSource.unityKey,
          .envelope = psxSpuEnvelope(regionSource.adsr1, regionSource.adsr2),
          .attenuationDb = linearAmplitudeToAttenuationDb(regionSource.volume / 127.0),
      };
      instrument.region(sample->second, std::move(region))
          .source("Region", regionSource.source, "ohori-aka-ps1-region");
    }
  }
  return OhoriAkaPs1ScannedBank{.bank = bank, .instruments = std::move(parsed)};
}

}  // namespace vgmtrans::formats::ohori_aka_ps1
