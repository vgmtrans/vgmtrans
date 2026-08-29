/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/PandoraBoxSnes/PandoraBoxSnes.h"

#include "value/platform/SnesSampleDirectory.h"
#include "value/synth/SnesDsp.h"

#include <fmt/format.h>

#include <algorithm>
#include <string>
#include <vector>

namespace vgmtrans::formats::pandora_box_snes {

using namespace core;

namespace {

struct Patch {
  u8 program;
  u8 globalInstrument;
  u8 srcn;
  SourceRange source;
};

[[nodiscard]] std::vector<Patch> collectPatches(ByteReader reader, const Layout& layout,
                                                const SequenceReferences& references) {
  std::vector<Patch> patches;
  patches.reserve(references.programs.size());
  for (const u8 program : references.programs) {
    const u8 localOffset = static_cast<u8>(layout.localInstrumentTableAddress - layout.sequenceHeaderAddress);
    const u16 address =
        static_cast<u16>(layout.sequenceHeaderAddress + static_cast<u8>(localOffset + program));
    const auto srcn = programSrcn(reader, layout, program);
    if (!srcn || !reader.has(address, 1)) {
      continue;
    }
    patches.push_back(Patch{
        .program = program,
        .globalInstrument = reader.u8At(address),
        .srcn = *srcn,
        .source = reader.range(address, 1),
    });
  }
  return patches;
}

[[nodiscard]] std::vector<u8> referencedSrcns(const std::vector<Patch>& patches) {
  std::vector<u8> srcns;
  srcns.reserve(patches.size());
  for (const Patch& patch : patches) {
    srcns.push_back(patch.srcn);
  }
  std::ranges::sort(srcns);
  srcns.erase(std::unique(srcns.begin(), srcns.end()), srcns.end());
  return srcns;
}

}  // namespace

std::optional<ScanSoundBankDraft> addSynth(ScanResultBuilder& builder, const Layout& layout,
                                           const SequenceReferences& references, std::string_view displayName) {
  const ByteReader reader = builder.reader();
  const std::vector<Patch> patches = collectPatches(reader, layout, references);
  if (patches.empty()) {
    return std::nullopt;
  }
  const SnesBrrCatalog catalog = readSnesBrrCatalog(reader, layout.spcDirAddress, referencedSrcns(patches));
  if (catalog.samples.empty()) {
    return std::nullopt;
  }

  auto bank = builder.soundBank(fmt::format("{} Instruments", displayName));
  auto& instruments = bank.instruments();
  const SnesBrrSampleRefs samples = addSnesBrrSamples(bank.localSamples(), reader, catalog);
  const SourceRange globalTable = reader.range(layout.globalInstrumentTableAddress, layout.globalInstrumentCount);
  auto globalAnnotation =
      instruments
          .source(SourceRole::Table, "Global Instrument to SRCN Table", globalTable,
                  "pandora-box-snes-global-instrument-table")
          .fieldsAsChildren()
          .description("The driver searches this table backward; the matching zero-based index is the SRCN");
  for (u32 srcn = 0; srcn < layout.globalInstrumentCount; ++srcn) {
    globalAnnotation.field(fmt::format("SRCN {}", srcn), reader.range(layout.globalInstrumentTableAddress + srcn, 1),
                           reader.u8At(layout.globalInstrumentTableAddress + srcn));
  }

  for (const Patch& patch : patches) {
    const auto sample = samples.findSrcn(patch.srcn);
    if (!sample) {
      continue;
    }
    auto entry = instruments.append(Instrument{
        .explicitAddress = InstrumentAddress{.bank = 0, .program = patch.program},
        .identity = InstrumentIdentity{.domain = std::string(kInstrumentDomain), .key = patch.program},
        .name = fmt::format("Instrument {} (SRCN {})", patch.program, patch.srcn),
        .range = patch.source,
    });
    entry.source(fmt::format("Instrument {}", patch.program), patch.source, "pandora-box-snes-instrument")
        .fieldsAsChildren()
        .field("global_instrument", patch.source, patch.globalInstrument)
        .description(fmt::format("Local program {} maps global instrument {} to SRCN {}", patch.program,
                                 patch.globalInstrument, patch.srcn));
    entry
        .region(*sample,
                Region{
                    .range = patch.source,
                    // The audited pitch table reaches the DSP's $1000 unity
                    // ratio at sequence octave 3, A (exported key 45).
                    .unityKey = 45.0,
                    .envelope = snesDspEnvelope(0xff, 0xe0, 0),
                })
        .source("Region", patch.source, "pandora-box-snes-region")
        .description(fmt::format("SRCN {}; driver reset ADSR $FF/$E0", patch.srcn));
  }
  return instruments.empty() ? std::nullopt : std::optional<ScanSoundBankDraft>{std::move(bank)};
}

}  // namespace vgmtrans::formats::pandora_box_snes
