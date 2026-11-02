/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/WinkysoftSnes/WinkysoftSnes.h"
#include "value/platform/SnesSampleDirectory.h"
#include "value/synth/SnesDsp.h"

#include <algorithm>
#include <cmath>
#include <fmt/format.h>

namespace vgmtrans::formats::winkysoft_snes {

using namespace core;

double unityKey(const Patch& patch, const DriverData& data) {
  // At source key 60 the pitch table is unshifted. Instrument tuning is an
  // unsigned, big-endian 8.8 multiplier, also used as an absolute drum pitch.
  const double pitch = static_cast<double>(data.pitches[0]) * patch.pitch / 256.0;
  return 60.0 - 12.0 * std::log2(std::max(1.0, pitch) / 4096.0);
}

std::optional<ScanSoundBankDraft> addSynth(ScanResultBuilder& builder, const Layout& layout,
                                          const DriverData& data, const std::set<u8>& programs,
                                          const std::set<u8>& noiseRates,
                                          std::string_view name) {
  const ByteReader reader = builder.reader();
  const auto catalog = readSnesBrrCatalog(reader, layout.directory, programs,
                                         [&](u8 index) { return data.patches[index].sample; });
  if (catalog.empty() && noiseRates.empty()) {
    return std::nullopt;
  }
  auto bank = builder.soundBank(fmt::format("{} Instruments", name));
  const auto samples = addSnesBrrSamples(bank.localSamples(), reader, catalog);
  for (const u8 index : programs) {
    const Patch& patch = data.patches[index];
    const auto sample = samples.findSrcn(patch.sample);
    if (!patch.range.valid() || patch.pitch == 0 || sample.empty()) {
      continue;
    }
    auto entry = bank.instruments().append(core::Instrument{
        .identity = InstrumentIdentity{.domain = std::string(kInstrumentDomain), .key = index},
        .name = fmt::format("Instrument {}", index), .range = patch.range,
    });
    const auto root = entry.source(fmt::format("Instrument {}", index), patch.range, "winkysoft-snes-instrument").id();
    entry.source("Parameters", patch.range, "winkysoft-snes-instrument-parameters")
        .parent(root).fieldsAsChildren()
        .field("sample", reader.range(patch.range.offset, 1), patch.sample)
        .field("adsr1", reader.range(patch.range.offset + 1, 1), patch.adsr1, SourceValueDisplay::Hex)
        .field("adsr2", reader.range(patch.range.offset + 2, 1), patch.adsr2, SourceValueDisplay::Hex)
        .field("pitch (8.8)", reader.range(patch.range.offset + 3, 2), patch.pitch, SourceValueDisplay::Hex)
        .field("volume", reader.range(patch.range.offset + 5, 1), patch.volume)
        .field("pan", reader.range(patch.range.offset + 6, 1), patch.pan)
        .field("transpose", reader.range(patch.range.offset + 7, 1), static_cast<s8>(patch.transpose),
               SourceValueDisplay::SignedDecimal);
    entry.region(Region{.sample = sample, .unityKey = unityKey(patch, data),
                         .envelope = snesDspEnvelope(patch.adsr1, patch.adsr2, 0)})
        .source("Region", patch.range, "winkysoft-snes-region").parent(root);
  }
  for (const u8 rate : noiseRates) {
    const auto sample = bank.localSamples()
        .add(0x100u + rate, Sample{.name = fmt::format("DSP Noise {}", rate),
                                   .codec = AudioCodec::SnesDspNoise, .encodedData = reader.range(0, 0),
                                   .sampleRate = kSnesDspSampleRate,
                                   .loop = Loop{.enabled = true, .length = kSnesDspNoiseSampleCount},
                                   .codecParameter = rate}).ref();
    // Noise ignores SRCN and pitch, but retains the selected patch's ADSR.
    for (const u8 index : programs) {
      const Patch& patch = data.patches[index];
      if (!patch.range.valid()) {
        continue;
      }
      bank.instruments().append(core::Instrument{
          .identity = InstrumentIdentity{.domain = std::string(kInstrumentDomain), .key = noiseInstrumentKey(index, rate)},
          .name = fmt::format("Instrument {} / Noise {}", index, rate), .range = patch.range,
      }).region(Region{.sample = sample, .unityKey = 60,
                         .envelope = snesDspEnvelope(patch.adsr1, patch.adsr2, 0)});
    }
  }
  return bank;
}

}  // namespace vgmtrans::formats::winkysoft_snes
