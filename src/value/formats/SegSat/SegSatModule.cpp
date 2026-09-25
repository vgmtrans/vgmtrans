/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/SegSat/SegSat.h"
#include "value/scan/AssetResolution.h"

#include <fmt/format.h>

#include <algorithm>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace vgmtrans::formats::segsat {

using namespace core;

namespace {

struct BankAssets {
  SegSatBankLayout layout;
  ScanSoundBankDraft bank;
};

[[nodiscard]] ScanResult scanSegSat(const ScanInput& input) {
  const auto bankLayouts = findSegSatBanks(input.reader);
  const auto sequenceLayouts = findSegSatSequences(input.reader);
  if (bankLayouts.empty() && sequenceLayouts.empty()) {
    return {};
  }

  ScanResultBuilder result(input, std::string(kSegSatFormatName));
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
          .bank = *scanned,
      });
    }
  }

  // Song tables may share physical streams. Keep the first entry as the
  // canonical sequence, including its name and collection.
  std::unordered_set<u32> publishedSequences;
  for (const auto& sequence : sequenceLayouts) {
    if (!publishedSequences.insert(sequence.offset).second) {
      continue;
    }
    const std::string sourceName =
        result.sourceFile().name.empty() ? result.sourceDisplayName() : result.sourceFile().name;
    const std::string name = fmt::format("{} {}_{}", sourceName, sequence.tableIndex, sequence.sequenceIndex);
    auto sequenceDraft =
        result.sequence(name, input.reader.range(sequence.offset, sequence.normalTrackEnd - sequence.offset));
    auto parsed =
        parseSegSatSequence(input.reader, sequenceDraft.id(), sequence, &result.sourceMap(), &result.diagnostics());
    const std::vector<u8> referencedBanks =
        sequence.referencedBanks.empty() ? std::vector<u8>{0} : sequence.referencedBanks;
    sequenceDraft.assignBanks(assignSegSatBanks)
        .prepare<SegSatSequenceBindingData>(prepareSegSatSequence)
        .data(SegSatSequenceBindingData{
            .volumeModel = volumeModel,
            .referencedBanks = referencedBanks,
            .controllerChanges = std::move(parsed.controllerChanges),
        })
        .program(std::move(parsed.program));

    if (banks.size() == 1) {
      sequenceDraft.useBank(banks.front().bank);
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
        sequenceDraft.useBank(selected->bank);
      }
    }
  }
  return result.finish();
}

}  // namespace

void assignSegSatBanks(BankAssignmentContext& context) {
  const auto& sequence = context.data<SegSatSequenceBindingData>();
  auto banks = context.banks<SegSatBankBindingData>();
  if (banks.size() != sequence.referencedBanks.size()) {
    context.warning(fmt::format("SegSat sequence refers to {} banks, but the collection contains {} SegSat banks",
                                sequence.referencedBanks.size(), banks.size()));
  }

  // Reserve exact physical matches before assigning missing logical roles to
  // the remaining banks. The result belongs to each sequence-bank connection.
  std::vector<u8> unmatched = sequence.referencedBanks;
  std::vector<bool> exact(banks.size());
  for (size_t i = 0; i < banks.size(); ++i) {
    const auto found = std::ranges::find(unmatched, banks[i].data.sourceBank);
    if (found != unmatched.end()) {
      exact[i] = true;
      unmatched.erase(found);
    }
  }
  auto fallback = unmatched.begin();
  for (size_t i = 0; i < banks.size(); ++i) {
    const u8 logical = !exact[i] && fallback != unmatched.end() ? *fallback++ : banks[i].data.sourceBank;
    banks[i].placement = AssetPrivateData::make(
        SegSatBankUse{.logicalBank = logical, .exportBank = static_cast<u8>(banks.size() == 1 ? 0 : logical)});
  }
}

void prepareSegSatBank(BankPreparationContext& context, const SegSatBankBindingData&) {
  const auto* use = context.placement.get<SegSatBankUse>();
  if (use == nullptr) {
    return;
  }
  for (auto& instrument : context.bank.instruments) {
    const auto address = resolveInstrumentAddress(instrument.explicitAddress, instrument.identity);
    instrument.explicitAddress = InstrumentAddress{.bank = use->exportBank, .program = address.program};
    instrument.identity = segSatInstrumentIdentity(use->logicalBank, static_cast<u8>(address.program));
  }
}

void prepareSegSatSequence(SequencePreparationContext& context, const SegSatSequenceBindingData& sequence) {
  std::vector<SegSatVelocityBank> velocityBanks;
  for (const auto& bank : context.soundBanks) {
    if (bank.metadata.format != kSegSatFormatName) {
      continue;
    }
    const auto* data = bank.privateData.get<SegSatBankBindingData>();
    const auto* use = context.bankPlacement<SegSatBankUse>(bank.metadata.id);
    if (data == nullptr || use == nullptr) {
      context.fail("SegSat bank is missing retained data or its logical bank assignment", bank.metadata.range);
      return;
    }
    auto runtime = *data;
    runtime.sourceBank = use->logicalBank;
    velocityBanks.push_back(std::move(runtime));
  }
  if (velocityBanks.empty() && !sequence.referencedBanks.empty()) {
    context.fail("SegSat collection does not contain a retained SegSat instrument bank");
    return;
  }
  if (!velocityBanks.empty()) {
    static_cast<void>(context.replaceSequenceRuntime(
        segSatSequenceRuntime(SegSatRuntimeConfig{.velocityBanks = std::move(velocityBanks),
                                                  .volumeModel = sequence.volumeModel,
                                                  .controllerChanges = sequence.controllerChanges})));
  }
}

FormatModule segSatModule() {
  return FormatModule{
      .name = std::string(kSegSatFormatName),
      .acceptedFormats = {source_formats::kSaturnRam},
      .scan = scanSegSat,
  };
}

}  // namespace vgmtrans::formats::segsat
