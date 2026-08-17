/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/ItikitiSnes/ItikitiSnes.h"

#include "value/platform/SnesSampleDirectory.h"
#include "value/synth/SnesDsp.h"

#include <fmt/format.h>

#include <cmath>
#include <vector>

namespace vgmtrans::formats::itikiti_snes {

using namespace core;

Envelope driverEnvelope(u8 adsr1, u8 adsr2) {
  return snesDspEnvelope(static_cast<u8>(adsr1 | 0x80), adsr2, 0);
}

namespace {

struct Patch {
  u8 program;
  u8 adsr1;
  u8 adsr2;
  u16 tuning;
  SourceRange tuningSource;
  SourceRange adsrSource;
};

[[nodiscard]] double pitchScale(u16 tuning) {
  return tuning < 0x8000 ? 1.0 + tuning / 32768.0 : tuning / 65536.0;
}

[[nodiscard]] std::vector<Patch> collectPatches(ByteReader reader, const Layout& layout,
                                                const ReferencedPrograms& references) {
  std::vector<Patch> result;
  result.reserve(references.programs.size());
  for (const u8 program : references.programs) {
    const u16 tuning = static_cast<u16>(layout.tuningTableAddress + program * 2u);
    const u16 adsr = static_cast<u16>(layout.adsrTableAddress + program * 2u);
    if (!reader.has(tuning, 2) || !reader.has(adsr, 2) ||
        !readSnesSampleDirectoryEntry(reader, layout.spcDirAddress + program * 4u, true)) {
      continue;
    }
    result.push_back(Patch{
        .program = program,
        .adsr1 = reader.u8At(adsr),
        .adsr2 = reader.u8At(adsr + 1),
        .tuning = reader.be16(tuning),
        .tuningSource = reader.range(tuning, 2),
        .adsrSource = reader.range(adsr, 2),
    });
  }
  return result;
}

[[nodiscard]] std::vector<u8> programs(const std::vector<Patch>& patches) {
  std::vector<u8> result;
  result.reserve(patches.size());
  for (const Patch& patch : patches) {
    result.push_back(patch.program);
  }
  return result;
}

}  // namespace

std::optional<ScanSoundBankRef> addSynth(ScanResultBuilder& builder, const Layout& layout,
                                         const ReferencedPrograms& references, std::string_view displayName) {
  const ByteReader reader = builder.reader();
  const std::vector<Patch> patches = collectPatches(reader, layout, references);
  if (patches.empty()) {
    return std::nullopt;
  }
  const SnesBrrCatalog catalog = readSnesBrrCatalog(reader, layout.spcDirAddress, programs(patches));
  if (catalog.samples.empty()) {
    return std::nullopt;
  }

  auto instruments = builder.soundBank(fmt::format("{} Instruments", displayName));
  auto& samplePool = instruments.samples();
  const SnesBrrSampleRefs samples = addSnesBrrSamples(samplePool, reader, catalog);

  for (const Patch& patch : patches) {
    const auto sample = samples.findSrcn(patch.program);
    if (!sample) {
      continue;
    }
    auto entry = instruments.append(Instrument{
        .explicitAddress = InstrumentAddress{.bank = 0, .program = patch.program},
        .identity = InstrumentIdentity{.domain = std::string(kInstrumentDomain), .key = patch.program},
        .name = fmt::format("Instrument {}", patch.program),
        .range = patch.adsrSource,
    });
    const SourceAnnotationId root =
        entry.source(fmt::format("Instrument {}", patch.program), patch.adsrSource, "itikiti-snes-instrument").id();
    entry.source("Pitch scale", patch.tuningSource, "itikiti-snes-tuning")
        .parent(root)
        .description(fmt::format("scale ${:04X}", patch.tuning));
    entry
        .region(*sample,
                Region{
                    .range = patch.adsrSource,
                    .unityKey = 72.0 - std::log2(pitchScale(patch.tuning)) * 12.0,
                    .envelope = driverEnvelope(patch.adsr1, patch.adsr2),
                })
        .source("Region", patch.adsrSource, "itikiti-snes-region")
        .description(fmt::format("SRCN {}, ADSR ${:02X}{:02X}", patch.program, patch.adsr1, patch.adsr2));
  }
  return instruments.ref();
}

}  // namespace vgmtrans::formats::itikiti_snes
