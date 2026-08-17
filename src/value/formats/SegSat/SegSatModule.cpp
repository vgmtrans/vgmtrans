/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/SegSat/SegSat.h"

#include <fmt/format.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace vgmtrans::formats::segsat {

using namespace core;

namespace {

struct BankAssets {
  SegSatBankLayout layout;
  ScanInstrumentSetRef instruments;
  ScanSampleCollectionRef samples;
};

[[nodiscard]] CollectionKey collectionKey(SourceId source, const SegSatSequenceLayout& sequence) {
  return CollectionKey{
      .resolver = std::string(kSegSatCollectionResolver),
      .value = "source:" + std::to_string(source.value) + ":sequence:" + std::to_string(sequence.offset),
  };
}

[[nodiscard]] ScanResult scanSegSat(const ScanInput& input) {
  const auto bankLayouts = findSegSatBanks(input.reader);
  const auto sequenceLayouts = findSegSatSequences(input.reader);
  if (bankLayouts.empty() && sequenceLayouts.empty()) {
    return {};
  }

  ScanResultBuilder result(input, std::string(kSegSatFormatName), std::string(kSegSatCollectionResolver));
  const SegSatDriverVersion version = determineSegSatDriverVersion(input.reader);
  const SegSatVolumeModel volumeModel = determineSegSatVolumeModel(input.reader);
  std::vector<BankAssets> banks;
  banks.reserve(bankLayouts.size());
  for (const auto& layout : bankLayouts) {
    const u8 bankNumber = layout.sourceBank.value_or(0);
    const auto scanned = addSegSatBank(result, layout, version, volumeModel, bankNumber);
    if (scanned) {
      banks.push_back(BankAssets{
          .layout = layout,
          .instruments = scanned->instruments,
          .samples = scanned->samples,
      });
    }
  }

  for (const auto& sequence : sequenceLayouts) {
    const std::string sourceName =
        result.sourceFile().name.empty() ? result.sourceDisplayName() : result.sourceFile().name;
    const std::string name = fmt::format("{} {}_{}", sourceName, sequence.tableIndex, sequence.sequenceIndex);
    auto sequenceDraft =
        result.sequence(name, input.reader.range(sequence.offset, sequence.normalTrackEnd - sequence.offset));
    auto parsed =
        parseSegSatSequence(input.reader, sequenceDraft.id(), sequence, &result.sourceMap(), &result.diagnostics());
    const std::vector<u8> referencedBanks =
        sequence.referencedBanks.empty() ? std::vector<u8>{0} : sequence.referencedBanks;
    sequenceDraft
        .data(SegSatSequenceBindingData{
            .volumeModel = volumeModel,
            .referencedBanks = referencedBanks,
            .controllerChanges = std::move(parsed.controllerChanges),
        })
        .program(std::move(parsed.program));

    auto collection = result.collection(name, collectionKey(result.source(), sequence)).sequence(sequenceDraft);
    if (banks.size() == 1) {
      collection.instrumentSet(banks.front().instruments).samples(banks.front().samples);
      continue;
    }

    // A stream without an explicit bank command begins on the driver's bank
    // zero. Treat that implicit dependency exactly like a referenced bank.
    for (const u8 referencedBank : referencedBanks) {
      auto selected =
          std::ranges::find_if(banks, [&](const BankAssets& bank) { return bank.layout.sourceBank == referencedBank; });
      if (selected == banks.end() && !banks.empty()) {
        // This is the Saturn driver's practical fallback used by the legacy
        // scanner when a sequence names an unloaded bank.
        selected = banks.begin();
      }
      if (selected != banks.end()) {
        collection.instrumentSet(selected->instruments).samples(selected->samples);
      }
    }
  }
  return result.finish();
}

}  // namespace

void bindSegSatCollection(CollectionBindingContext& context) {
  const auto* sequence = context.sequence;
  if (sequence == nullptr) {
    return;
  }
  const auto* sequenceData = sequence->privateData.get<SegSatSequenceBindingData>();
  if (sequenceData == nullptr) {
    context.fail("SegSat sequence is missing retained collection-binding data", sequence->metadata.range);
    return;
  }

  struct SelectedBank {
    InstrumentSetAsset* instruments;
    SegSatVelocityBank runtime;
    bool exactMatch = false;
  };
  std::vector<SelectedBank> selectedBanks;
  for (auto& instruments : context.instrumentSets) {
    if (instruments.metadata.format != kSegSatFormatName) {
      continue;
    }
    const auto* data = instruments.privateData.get<SegSatBankBindingData>();
    if (data == nullptr) {
      context.fail("SegSat instrument set is missing retained collection-binding data", instruments.metadata.range);
      return;
    }
    selectedBanks.push_back(SelectedBank{.instruments = &instruments, .runtime = *data});
  }

  const size_t bankCount = selectedBanks.size();
  if (bankCount == 0 && !sequenceData->referencedBanks.empty()) {
    context.fail("SegSat collection does not contain a retained SegSat instrument bank", sequence->metadata.range);
    return;
  }

  if (sequenceData->referencedBanks.size() != bankCount) {
    context.warning(fmt::format("SegSat sequence refers to {} banks, but the collection contains {} SegSat banks",
                                sequenceData->referencedBanks.size(), bankCount),
                    sequence->metadata.range);
  }

  std::vector<u8> unmatchedReferences = sequenceData->referencedBanks;
  // Reserve every exact physical match before an earlier missing role can
  // consume that bank as its fallback.
  for (auto& bank : selectedBanks) {
    const auto exact = std::ranges::find(unmatchedReferences, bank.runtime.sourceBank);
    if (exact != unmatchedReferences.end()) {
      bank.exactMatch = true;
      unmatchedReferences.erase(exact);
    }
  }
  auto fallback = unmatchedReferences.begin();
  for (auto& bank : selectedBanks) {
    if (!bank.exactMatch && fallback != unmatchedReferences.end()) {
      bank.runtime.sourceBank = *fallback++;
    }
  }

  std::vector<SegSatVelocityBank> velocityBanks;
  velocityBanks.reserve(bankCount);
  for (auto& bank : selectedBanks) {
    auto& instruments = *bank.instruments;
    const u8 logicalBank = bank.runtime.sourceBank;
    const u8 exportBank = bankCount == 1 ? 0 : logicalBank;
    for (auto& instrument : instruments.instruments) {
      const auto address = resolveInstrumentAddress(instrument.explicitAddress, instrument.identity);
      instrument.explicitAddress = InstrumentAddress{.bank = exportBank, .program = address.program};
      instrument.identity = segSatInstrumentIdentity(logicalBank, static_cast<u8>(address.program));
    }
    velocityBanks.push_back(std::move(bank.runtime));
  }

  if (!velocityBanks.empty()) {
    if (!context.replaceSequenceRuntime(segSatSequenceRuntime(SegSatRuntimeConfig{
            .velocityBanks = std::move(velocityBanks),
            .volumeModel = sequenceData->volumeModel,
            .controllerChanges = sequenceData->controllerChanges,
        }))) {
      return;
    }
  }
}

FormatModule segSatModule() {
  return FormatModule{
      .name = std::string(kSegSatFormatName),
      .acceptedFormats = {source_formats::kSaturnRam},
      .scan = scanSegSat,
      .collectionResolverId = std::string(kSegSatCollectionResolver),
      .bindCollection = bindSegSatCollection,
  };
}

}  // namespace vgmtrans::formats::segsat
