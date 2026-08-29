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

[[nodiscard]] double fineTuningCents(u8 fine) {
  return 1200.0 * std::log2(1.0 + fine / 256.0);
}

[[nodiscard]] u32 tuningTableSize(const Layout& layout) {
  // Every driver places the equal-length parallel arrays back-to-back.
  const int distance = static_cast<int>(layout.coarseTableAddress) - static_cast<int>(layout.fineTableAddress);
  return std::min<u32>(std::abs(distance), 256);
}

}  // namespace

std::optional<ScanSoundBankDraft> addSynth(ScanResultBuilder& builder, const Layout& layout,
                                           const SequenceReferences& references, std::string_view displayName) {
  const ByteReader reader = builder.reader();
  const u32 tableSize = tuningTableSize(layout);
  if (tableSize == 0 || !reader.has(layout.coarseTableAddress, tableSize) ||
      !reader.has(layout.fineTableAddress, tableSize)) {
    return std::nullopt;
  }
  std::vector<u8> srcns(references.srcns.begin(), references.srcns.end());
  const SnesBrrCatalog catalog = readSnesBrrCatalog(reader, layout.spcDirAddress, srcns);
  if (catalog.samples.empty()) {
    return std::nullopt;
  }

  auto bank = builder.soundBank(fmt::format("{} Instruments", displayName));
  auto& instruments = bank.instruments();
  auto coarseTable = instruments
                         .source(SourceRole::Table, "Coarse Tuning Table",
                                 reader.range(layout.coarseTableAddress, tableSize),
                                 "softcreat-coarse-tuning-table")
                         .fieldsAsChildren()
                         .description(fmt::format("{} signed semitone entries indexed by SRCN", tableSize));
  auto fineTable = instruments
                       .source(SourceRole::Table, "Fine Tuning Table", reader.range(layout.fineTableAddress, tableSize),
                               "softcreat-fine-tuning-table")
                       .fieldsAsChildren()
                       .description(fmt::format("{} fractional tuning entries indexed by SRCN", tableSize));
  for (u32 srcn = 0; srcn < tableSize; ++srcn) {
    const SourceRange coarse = reader.range(layout.coarseTableAddress + srcn, 1);
    const SourceRange fine = reader.range(layout.fineTableAddress + srcn, 1);
    coarseTable.field(fmt::format("SRCN {}", srcn), coarse, reader.s8At(coarse.offset),
                      SourceValueDisplay::SignedDecimal);
    fineTable.field(fmt::format("SRCN {}", srcn), fine, fineTuningCents(reader.u8At(fine.offset)),
                    SourceValueDisplay::Cents);
  }
  const SnesBrrSampleRefs samples = addSnesBrrSamples(bank.localSamples(), reader, catalog);
  for (const u8 srcn : srcns) {
    const auto sample = samples.findSrcn(srcn);
    const u32 coarseAddress = layout.coarseTableAddress + srcn;
    const u32 fineAddress = layout.fineTableAddress + srcn;
    if (!sample || srcn >= tableSize) {
      continue;
    }
    const SourceRange coarseSource = reader.range(coarseAddress, 1);
    const SourceRange fineSource = reader.range(fineAddress, 1);
    const s8 coarse = reader.s8At(coarseAddress);
    const u8 fine = reader.u8At(fineAddress);
    const double tuning = coarse + fineTuningCents(fine) / 100.0;
    auto entry = instruments.append(Instrument{
        .explicitAddress = InstrumentAddress{.bank = static_cast<u32>(srcn >> 7),
                                             .program = static_cast<u32>(srcn & 0x7f)},
        .identity = InstrumentIdentity{.domain = std::string(kInstrumentDomain), .key = srcn},
        .name = fmt::format("Instrument {}", srcn),
        .range = coarseSource,
    });
    entry.source(fmt::format("Instrument {}", srcn), coarseSource, "softcreat-instrument")
        .parent(coarseTable.id())
        .fieldsAsChildren()
        .field("coarse_tuning", coarseSource, coarse, SourceValueDisplay::SignedDecimal)
        .field("fine_tuning", fineSource, fineTuningCents(fine), SourceValueDisplay::Cents)
        .description(fmt::format("Bank {}, program {}; selects SRCN {}", srcn >> 7, srcn & 0x7f, srcn));
    entry.region(*sample,
                 Region{
                     // The pitch table reaches $1000 at internal note 62.
                     // Source note zero is exported as MIDI key 24.
                     .unityKey = 86.0 - tuning,
                     .envelope = kNeutralGainEnvelope,
                 });
  }
  return instruments.empty() ? std::nullopt : std::optional<ScanSoundBankDraft>{std::move(bank)};
}

}  // namespace vgmtrans::formats::softcreat
