/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/NeverlandSnes/NeverlandSnes.h"

#include "value/platform/SnesSampleDirectory.h"
#include "value/synth/SnesDsp.h"

#include <fmt/format.h>

#include <vector>

namespace vgmtrans::formats::neverland_snes {

using namespace core;

namespace {

[[nodiscard]] Envelope driverEnvelope(u8 adsr1, u8 adsr2) {
  return snesDspEnvelope(static_cast<u8>(adsr1 | 0x80), adsr2, 0);
}

struct Patch {
  u8 program;
  u8 adsr1;
  u8 adsr2;
  u16 tuning;
  SourceRange source;
};

[[nodiscard]] std::vector<Patch> collectPatches(ByteReader reader, const Layout& layout,
                                                const ReferencedPrograms& references) {
  std::vector<Patch> result;
  for (const u8 program : references) {
    const u32 address = layout.instrumentTableAddress + program * 4u;
    if (!reader.has(address, 4) || !readSnesSampleDirectoryEntry(reader, layout.spcDirAddress + program * 4u, true)) {
      continue;
    }
    const u8 adsr1 = reader.u8At(address);
    const u8 adsr2 = reader.u8At(address + 1);
    if (adsr1 == 0xff && adsr2 == 0xff && reader.be16(address + 2) == 0xffff) {
      continue;
    }
    result.push_back(Patch{
        .program = program,
        .adsr1 = adsr1,
        .adsr2 = adsr2,
        .tuning = reader.be16(address + 2),
        .source = reader.range(address, 4),
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

std::optional<ScanSoundBankDraft> addSynth(ScanResultBuilder& builder, const Layout& layout,
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

  auto bank = builder.soundBank(fmt::format("{} Instruments", displayName));
  auto& instruments = bank.instruments();
  auto& samplePool = bank.localSamples();
  const SnesBrrSampleRefs samples = addSnesBrrSamples(samplePool, reader, catalog);
  for (const Patch& patch : patches) {
    const auto sample = samples.findSrcn(patch.program);
    if (!sample) {
      continue;
    }
    auto instrument = instruments.append(Instrument{
        .explicitAddress = InstrumentAddress{.bank = 0, .program = patch.program},
        .identity = InstrumentIdentity{.domain = std::string(kInstrumentDomain), .key = patch.program},
        .name = fmt::format("Instrument {}", patch.program),
        .range = patch.source,
    });
    const SourceAnnotationId root =
        instrument.source(fmt::format("Instrument {}", patch.program), patch.source, "neverland-snes-instrument").id();
    instrument.source("Pitch scale", reader.range(patch.source.offset + 2, 2), "neverland-snes-tuning")
        .parent(root)
        .description(fmt::format("scale ${:04X}", patch.tuning));
    instrument
        .region(*sample,
                Region{
                    .range = patch.source,
                    .unityKey = instrumentUnityKey(patch.tuning),
                    .envelope = driverEnvelope(patch.adsr1, patch.adsr2),
                })
        .source("Region", patch.source, "neverland-snes-region")
        .description(fmt::format("SRCN {}, ADSR ${:02X}{:02X}", patch.program, patch.adsr1, patch.adsr2));
  }
  return bank;
}

}  // namespace vgmtrans::formats::neverland_snes
