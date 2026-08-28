/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/SoftCreat/SoftCreat.h"

#include "value/platform/SnesSampleDirectory.h"

#include <cmath>
#include <string>
#include <vector>

#include <fmt/format.h>

namespace vgmtrans::formats::softcreat {

using namespace core;

namespace {

[[nodiscard]] double tuningSemitones(u8 coarse, u8 fine) {
  return static_cast<s8>(coarse) + 12.0 * std::log2(1.0 + fine / 256.0);
}

}  // namespace

std::optional<ScanSoundBankDraft> addSynth(ScanResultBuilder& builder, const Layout& layout,
                                           const SequenceReferences& references, std::string_view displayName) {
  const ByteReader reader = builder.reader();
  std::vector<u8> srcns(references.srcns.begin(), references.srcns.end());
  const SnesBrrCatalog catalog = readSnesBrrCatalog(reader, layout.spcDirAddress, srcns);
  if (catalog.samples.empty()) {
    return std::nullopt;
  }

  auto bank = builder.soundBank(fmt::format("{} Instruments", displayName));
  auto& instruments = bank.instruments();
  const SnesBrrSampleRefs samples = addSnesBrrSamples(bank.localSamples(), reader, catalog);
  for (const u8 srcn : srcns) {
    const auto sample = samples.findSrcn(srcn);
    const u32 coarseAddress = layout.coarseTableAddress + srcn;
    const u32 fineAddress = layout.fineTableAddress + srcn;
    if (!sample || !reader.has(coarseAddress, 1) || !reader.has(fineAddress, 1)) {
      continue;
    }
    const SourceRange coarseSource = reader.range(coarseAddress, 1);
    const SourceRange fineSource = reader.range(fineAddress, 1);
    auto entry = instruments.append(Instrument{
        .explicitAddress = InstrumentAddress{.bank = static_cast<u32>(srcn >> 7),
                                             .program = static_cast<u32>(srcn & 0x7f)},
        .identity = InstrumentIdentity{.domain = std::string(kInstrumentDomain), .key = srcn},
        .name = fmt::format("Instrument {}", srcn),
        .range = coarseSource,
    });
    const SourceAnnotationId root =
        entry.source(fmt::format("Instrument {}", srcn), coarseSource, "softcreat-instrument").id();
    entry.source("Coarse Tuning", coarseSource, "softcreat-coarse-tuning").parent(root);
    entry.source("Fine Tuning", fineSource, "softcreat-fine-tuning").parent(root);
    entry.region(*sample,
                 Region{
                     .sample = *sample,
                     .range = coarseSource,
                     // The pitch table reaches $1000 at internal note 62.
                     // Source note zero is exported as MIDI key 24.
                     .unityKey = 86.0 - tuningSemitones(reader.u8At(coarseAddress), reader.u8At(fineAddress)),
                     .envelope = neutralGainEnvelope(),
                 })
        .source(fmt::format("Instrument {} Region", srcn), coarseSource, "softcreat-region");
  }
  return instruments.empty() ? std::nullopt : std::optional<ScanSoundBankDraft>{std::move(bank)};
}

}  // namespace vgmtrans::formats::softcreat
