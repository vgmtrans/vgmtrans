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

Envelope driverEnvelope(u8 adsr1, u8 adsr2) {
  return snesDspEnvelope(adsr1, adsr2, 0);
}

namespace {

struct Patch {
  u8 program = 0;
  SourceRange source;
};

[[nodiscard]] std::vector<Patch> collectPatches(ByteReader reader, const Layout& layout,
                                                const std::set<u8>& programs) {
  std::vector<Patch> patches;
  const SnesSampleDirectory directory(reader, layout.spcDirAddress);
  for (const u8 program : programs) {
    const auto sample = directory.entry(program);
    if (!sample || !sample->stream || sample->startAddress < layout.spcDirAddress) {
      continue;
    }
    patches.push_back(Patch{.program = program, .source = reader.range(layout.spcDirAddress + program * 4u, 4)});
  }
  return patches;
}

[[nodiscard]] std::vector<u8> srcns(const std::vector<Patch>& patches) {
  std::vector<u8> result;
  result.reserve(patches.size());
  for (const Patch& patch : patches) {
    result.push_back(patch.program);
  }
  return result;
}

}  // namespace

std::optional<ScanSoundBankDraft> addSynth(ScanResultBuilder& builder, const Layout& layout,
                                           const std::set<u8>& programs, std::string_view displayName) {
  const ByteReader reader = builder.reader();
  const std::vector<Patch> patches = collectPatches(reader, layout, programs);
  if (patches.empty()) {
    return std::nullopt;
  }
  const SnesBrrCatalog catalog = readSnesBrrCatalog(reader, layout.spcDirAddress, srcns(patches));
  if (catalog.samples.empty()) {
    return std::nullopt;
  }

  auto bank = builder.soundBank(fmt::format("{} Instruments", displayName));
  auto& instruments = bank.instruments();
  auto& samplePool = bank.localSamples();
  const SnesBrrSampleRefs samples = addSnesBrrSamples(samplePool, reader, catalog);
  for (const Patch& patch : patches) {
    const auto sample = samples.findSrcn(patch.program);
    if (!sample) {
      continue;
    }
    auto entry = instruments.append(Instrument{
        .explicitAddress = InstrumentAddress{.bank = static_cast<u32>(patch.program >> 7),
                                             .program = static_cast<u32>(patch.program & 0x7f)},
        .identity = InstrumentIdentity{.domain = std::string(kInstrumentDomain), .key = patch.program},
        .name = fmt::format("Instrument {}", patch.program),
        .range = patch.source,
    });
    const SourceAnnotationId root =
        entry.source(fmt::format("Instrument {}", patch.program), patch.source, "graph-res-snes-instrument").id();
    entry
        .region(*sample,
                Region{
                    .range = patch.source,
                    .unityKey = 57.0,
                    .envelope = driverEnvelope(0x8f, 0xe0),
                })
        .source("Region", patch.source, "graph-res-snes-region")
        .parent(root)
        .description(fmt::format("SRCN {}", patch.program));
  }
  return bank;
}

}  // namespace vgmtrans::formats::graph_res_snes
