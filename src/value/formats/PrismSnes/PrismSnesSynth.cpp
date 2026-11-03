/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/PrismSnes/PrismSnes.h"

#include "value/platform/SnesSampleDirectory.h"
#include "value/synth/SnesDsp.h"

#include <fmt/format.h>

#include <ranges>
#include <string>
#include <vector>

namespace vgmtrans::formats::prism_snes {

using namespace core;

Envelope driverEnvelope(u8 adsr1, u8 adsr2) {
  return snesDspEnvelope(static_cast<u8>(adsr1 | 0x80), adsr2, 0);
}

std::optional<ScanSoundBankDraft> addSynth(ScanResultBuilder& builder, const Layout& layout,
                                           std::string_view displayName) {
  const ByteReader reader = builder.reader();
  auto catalog = readSnesBrrCatalog(reader, layout.spcDirAddress, std::views::iota(0, 0x100));
  std::erase_if(catalog, [&](const SnesBrrSample& sample) { return sample.startAddress < layout.spcDirAddress; });
  if (catalog.empty()) {
    return std::nullopt;
  }

  auto bank = builder.soundBank(fmt::format("{} Instruments", displayName));
  auto& instruments = bank.instruments();
  auto& samplePool = bank.localSamples();
  const SnesBrrSampleRefs samples = addSnesBrrSamples(samplePool, reader, catalog);
  for (const SnesBrrSample& sampleInfo : catalog) {
    const u8 program = sampleInfo.srcn;
    const auto sample = samples.findSrcn(program);
    if (!sample) {
      continue;
    }
    const u32 adsr1 = layout.adsr1TableAddress + program;
    const u32 adsr2 = layout.adsr2TableAddress + program;
    const u32 tuningHigh = layout.tuningHighTableAddress + program;
    const u32 tuningLow = layout.tuningLowTableAddress + program;
    const s16 tuning = static_cast<s16>(reader.u8At(tuningLow) | (reader.u8At(tuningHigh) << 8));
    const double semitones = tuning / 256.0;
    const SourceRange range = reader.range(adsr1, 1);
    auto entry = instruments.append(Instrument{
        .identity = InstrumentIdentity{.domain = std::string(kInstrumentDomain), .key = program},
        .name = fmt::format("Instrument {}", program),
        .range = range,
    });
    const SourceAnnotationId root =
        entry.source(fmt::format("Instrument {}", program), range, "prism-snes-instrument").id();
    entry.source("ADSR1", range, "prism-snes-adsr1").parent(root);
    entry.source("ADSR2", reader.range(adsr2, 1), "prism-snes-adsr2").parent(root);
    entry.source("Coarse Tuning", reader.range(tuningHigh, 1), "prism-snes-tuning-high").parent(root);
    entry.source("Fine Tuning", reader.range(tuningLow, 1), "prism-snes-tuning-low").parent(root);
    entry
        .region(*sample,
                Region{
                    .unityKey = 93.0 - semitones,
                    .envelope = driverEnvelope(reader.u8At(adsr1), reader.u8At(adsr2)),
                })
        .source("Region", range, "prism-snes-region")
        .parent(root)
        .description(fmt::format("SRCN {}, tuning {:.4f} semitones", program, semitones));
  }
  return bank;
}

}  // namespace vgmtrans::formats::prism_snes
