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

namespace {

// A program number directly selects an entry in the sample list. Keep only
// entries that contain a real sample and point into the sample-data area.
[[nodiscard]] std::vector<u8> availableSrcns(ByteReader reader, const Layout& layout,
                                             const std::set<u8>& programs) {
  std::vector<u8> result;
  const SnesSampleDirectory directory(reader, layout.spcDirAddress);
  for (const u8 program : programs) {
    const auto sample = directory.entry(program);
    if (sample && sample->stream && sample->startAddress >= layout.spcDirAddress) {
      result.push_back(program);
    }
  }
  return result;
}

}  // namespace

// Build one instrument for each sample used by the sequence. The tuning data
// says that sample pitch $1000 corresponds to MIDI note 57.
std::optional<ScanSoundBankDraft> addSynth(ScanResultBuilder& builder, const Layout& layout,
                                           const std::set<u8>& programs, std::string_view displayName) {
  const ByteReader reader = builder.reader();
  const SnesBrrCatalog catalog = readSnesBrrCatalog(reader, layout.spcDirAddress,
                                                    availableSrcns(reader, layout, programs));
  if (catalog.samples.empty()) {
    return std::nullopt;
  }

  auto bank = builder.soundBank(fmt::format("{} Instruments", displayName));
  auto& instruments = bank.instruments();
  auto& samplePool = bank.localSamples();
  const SnesBrrSampleRefs samples = addSnesBrrSamples(samplePool, reader, catalog);
  for (const SnesBrrSample& sampleInfo : catalog.samples) {
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
