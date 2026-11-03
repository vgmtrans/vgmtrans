/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/GraphResSnes/GraphResSnes.h"

#include "value/platform/SnesSampleDirectory.h"
#include "value/synth/SnesDsp.h"

#include <fmt/format.h>

#include <string>
#include <vector>

namespace vgmtrans::formats::graph_res_snes {

using namespace core;

// Build one instrument for each sample used by the sequence. The tuning data
// says that sample pitch $1000 corresponds to MIDI note 57.
std::optional<ScanSoundBankDraft> addSynth(ScanResultBuilder& builder, const Layout& layout,
                                           const std::set<u8>& programs, std::string_view displayName) {
  const ByteReader reader = builder.reader();
  auto catalog = readSnesBrrCatalog(reader, layout.spcDirAddress, programs);
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
    const SourceRange source = sampleInfo.directoryEntry;
    const auto sample = samples.findSrcn(program);
    if (!sample) {
      continue;
    }
    auto entry = instruments.append(Instrument{
        .identity = InstrumentIdentity{.domain = std::string(kInstrumentDomain), .key = program},
        .name = fmt::format("Instrument {}", program),
        .range = source,
    });
    const SourceAnnotationId root =
        entry.source(fmt::format("Instrument {}", program), source, "graph-res-snes-instrument").id();
    entry
        .region(*sample,
                Region{
                    .unityKey = kUnityKey,
                    // Regular instruments use ADSR; the chip's GAIN
                    // mode is unused here.
                    .envelope = snesDspEnvelope(kDefaultAdsr1, kDefaultAdsr2, 0),
                })
        .source("Region", source, "graph-res-snes-region")
        .parent(root)
        .description(fmt::format("SRCN {}", program));
  }
  return bank;
}

}  // namespace vgmtrans::formats::graph_res_snes
