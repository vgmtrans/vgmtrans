/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/CompileSnes/CompileSnes.h"

#include "value/platform/SnesSampleDirectory.h"
#include "value/synth/SnesDsp.h"

#include <fmt/format.h>

namespace vgmtrans::formats::compile_snes {

using namespace core;

Envelope driverEnvelope(u8 adsr1, u8 adsr2, u8 gain) {
  return snesDspEnvelope(adsr1, adsr2, gain);
}

namespace {

[[nodiscard]] std::vector<InstrumentInfo> collectInstruments(ByteReader reader, const Layout& layout,
                                                             const std::set<u8>& programs) {
  std::vector<InstrumentInfo> result;
  for (const u8 program : programs) {
    const auto instrument = readInstrumentInfo(reader, layout, program);
    if (!instrument || !readSnesSampleDirectoryEntry(reader, layout.spcDirAddress + program * 4u, true)) {
      continue;
    }
    result.push_back(*instrument);
  }
  return result;
}

[[nodiscard]] std::vector<u8> referencedSrcns(const std::vector<InstrumentInfo>& instruments) {
  std::vector<u8> result;
  result.reserve(instruments.size());
  for (const InstrumentInfo& instrument : instruments) {
    result.push_back(instrument.program);
  }
  return result;
}

}  // namespace

std::optional<ScanSoundBankRef> addSynth(ScanResultBuilder& builder, const Layout& layout, const std::set<u8>& programs,
                                         std::string_view displayName) {
  const ByteReader reader = builder.reader();
  const std::vector<InstrumentInfo> instrumentInfo = collectInstruments(reader, layout, programs);
  if (instrumentInfo.empty()) {
    return std::nullopt;
  }
  const SnesBrrCatalog catalog = readSnesBrrCatalog(reader, layout.spcDirAddress, referencedSrcns(instrumentInfo));
  if (catalog.samples.empty()) {
    return std::nullopt;
  }

  auto instruments = builder.soundBank(fmt::format("{} Instruments", displayName));
  auto& samplePool = instruments.samples();
  const SnesBrrSampleRefs samples = addSnesBrrSamples(samplePool, reader, catalog);
  for (const InstrumentInfo& info : instrumentInfo) {
    const auto sample = samples.findSrcn(info.program);
    if (!sample) {
      continue;
    }
    const double unityKey = instrumentUnityKey(reader, info);
    auto entry = instruments.append(Instrument{
        .explicitAddress = InstrumentAddress{.bank = 0, .program = info.program},
        .identity = InstrumentIdentity{.domain = std::string(kInstrumentDomain), .key = info.program},
        .name = fmt::format("Instrument {}", info.program),
        .range = info.source,
    });
    const SourceAnnotationId root =
        entry.source(fmt::format("Instrument {}", info.program), info.source, "compile-snes-instrument").id();
    entry.source("Tuning", info.source, "compile-snes-tuning")
        .parent(root)
        .description(fmt::format("transpose {}, pitch table {}", info.transpose, info.pitchTable));
    entry
        .region(*sample,
                Region{
                    .range = info.source,
                    .unityKey = unityKey,
                    // Compile owns ADSR in track state; this is merely a safe
                    // fallback for consumers that ignore dynamic overrides.
                    .envelope = driverEnvelope(0x8f, 0xe0),
                })
        .source("Region", info.source, "compile-snes-region")
        .description(fmt::format("SRCN {}, unity key {:.3f}", info.program, unityKey));
  }
  return instruments.ref();
}

}  // namespace vgmtrans::formats::compile_snes
