/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/HOSA/HOSA.h"

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

namespace vgmtrans::formats::hosa {

using namespace core;

namespace {

constexpr u32 kRegionSize = 0x10;

[[nodiscard]] double unityKey(u8 semitone, u8 fine) {
  const double key = std::fmod(120.0 - semitone - fine / 256.0, 128.0);
  return key < 0.0 ? key + 128.0 : key;
}

[[nodiscard]] u8 sustainDirection(u8 mode) {
  // PsyQ treats modes 1 and 5 as increasing, mode 7 as exponential
  // decreasing, and the remaining modes as linear decreasing.
  return mode == 1 || mode == 5 ? 0 : 1;
}

[[nodiscard]] Region readRegion(ByteReader reader, u32 offset) {
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
  const u16 adsr1 = composePsxAdsr1(attackMode == 5, (rates >> 20) & 0x7f, (rates >> 16) & 0x0f, rates & 0x0f);
  const u16 adsr2 = composePsxAdsr2(sustainMode == 5 || sustainMode == 7, sustainDirection(sustainMode),
                                    (rates >> 9) & 0x7f, releaseMode == 7, (rates >> 4) & 0x1f);
  record.derived("adsr1", adsr1, SourceValueDisplay::Hex);
  record.derived("adsr2", adsr2, SourceValueDisplay::Hex);
  return Region{
      .offset = offset,
      .sampleOffset = sampleOffset,
      .volume = volume,
      .keyHigh = keyHigh,
      .unityKey = unityKey(semitone, fine),
      .panOverride = (rawPan & 0x80) != 0 ? std::optional<u8>{rawPan & 0x7f} : std::nullopt,
      .reverb = reverb,
      .adsr1 = adsr1,
      .adsr2 = adsr2,
      .source = std::move(record).finish(),
  };
}

[[nodiscard]] std::vector<Region> effectiveRegions(const std::vector<Region>& raw) {
  if (raw.empty()) return {};
  std::vector<Region> regions;
  for (u16 key = 0; key < 128; ++key) {
    auto selected = std::ranges::find_if(raw, [key](const Region& region) { return key <= region.keyHigh; });
    if (selected == raw.end()) selected = raw.begin();
    if (regions.empty() || regions.back().offset != selected->offset) {
      regions.push_back(*selected);
      regions.back().keyLow = static_cast<u8>(key);
    }
    regions.back().keyHigh = static_cast<u8>(key);
  }
  return regions;
}

[[nodiscard]] std::vector<Instrument> readInstruments(ByteReader reader, const BankLayout& layout) {
  std::vector<Instrument> instruments;
  instruments.reserve(layout.instrumentAddresses.size());
  for (const u32 offset : layout.instrumentAddresses) {
    const u32 count = reader.u8At(offset);
    RecordReader record(reader, offset, offset + 4 + count * kRegionSize);
    (void)record.u8At(0, "region_count");
    Instrument instrument{.source = std::move(record).finish()};
    std::vector<Region> raw;
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

std::optional<ScannedBank> addBank(ScanResultBuilder& result, const BankLayout& layout) {
  if (!layout.sampleDataOffset) return std::nullopt;
  const ByteReader reader = result.reader();
  const u32 sampleBase = *layout.sampleDataOffset;
  auto parsed = readInstruments(reader, layout);

  std::set<u32> offsets;
  for (const auto& instrument : parsed) {
    for (const auto& region : instrument.regions) offsets.insert(region.sampleOffset);
  }
  std::map<u32, PsxAdpcmStream> streams;
  for (auto current = offsets.begin(); current != offsets.end(); ++current) {
    const u32 start = sampleBase + *current;
    const u32 boundary = std::next(current) == offsets.end()
                             ? static_cast<u32>(reader.size())
                             : sampleBase + *std::next(current);
    if (auto stream = inspectPsxAdpcmStream(reader, start, boundary)) streams.emplace(*current, *stream);
  }
  if (streams.empty()) return std::nullopt;

  auto bank = result.soundBank("HOSA Bank");
  auto& bankInstruments = bank.instruments();
  auto& samples = bank.localSamples();
  bankInstruments.include(reader.range(layout.offset, layout.length));
  u32 sampleDataEnd = sampleBase;
  for (const auto& [_, stream] : streams) {
    sampleDataEnd = static_cast<u32>(std::max<u64>(sampleDataEnd, stream.encodedData.endOffset()));
  }
  const u32 sampleDataLength = sampleDataEnd - sampleBase;
  samples.include(reader.range(sampleBase, sampleDataLength));
  const SourceAnnotationId sampleRoot =
      samples.source(SourceRole::SamplePool, "PS1 ADPCM Sample Data", reader.range(sampleBase, sampleDataLength),
                     "hosa-sample-data")
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

  const SourceAnnotationId instrumentRoot =
      bankInstruments
          .source(SourceRole::Table, "Instrument Bank", reader.range(layout.offset, layout.length),
                  "hosa-instrument-bank")
          .id();
  for (u32 program = 0; program < parsed.size(); ++program) {
    const auto& source = parsed[program];
    auto instrument = bankInstruments.append(core::Instrument{
        .explicitAddress = InstrumentAddress{.bank = 0, .program = static_cast<u8>(program)},
        .identity = instrumentIdentity(static_cast<u8>(program)),
        .reverb = 0.0,
        .name = fmt::format("Instrument {}", program),
        .range = source.source.range,
    });
    instrument.source(fmt::format("Instrument {}", program), source.source, "hosa-instrument").parent(instrumentRoot);
    for (const auto& regionSource : source.regions) {
      const auto sample = sampleRefs.find(regionSource.sampleOffset);
      if (sample == sampleRefs.end()) continue;
      core::Region region{
          .keyRange = KeyRange{.low = regionSource.keyLow, .high = regionSource.keyHigh},
          .range = regionSource.source.range,
          .unityKey = regionSource.unityKey,
          .envelope = psxSpuEnvelope(regionSource.adsr1, regionSource.adsr2),
          .attenuationDb = linearAmplitudeToAttenuationDb(regionSource.volume / 127.0),
      };
      instrument.region(sample->second, std::move(region)).source("Region", regionSource.source, "hosa-region");
    }
  }
  return ScannedBank{.bank = bank, .instruments = std::move(parsed)};
}

}  // namespace vgmtrans::formats::hosa
