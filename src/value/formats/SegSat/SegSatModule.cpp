/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/SegSat/SegSat.h"

#include <fmt/format.h>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <optional>
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
  std::vector<BankAssets> banks;
  banks.reserve(bankLayouts.size());
  for (const auto& layout : bankLayouts) {
    const u8 bankNumber = layout.sourceBank.value_or(0);
    const auto scanned = addSegSatBank(result, layout, version, bankNumber);
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
    sequenceDraft.program(parseSegSatSequenceProgram(input.reader, sequenceDraft.id(), sequence, &result.sourceMap(),
                                                     &result.diagnostics()));

    auto collection = result.collection(name, collectionKey(result.source(), sequence)).sequence(sequenceDraft);
    if (banks.size() == 1) {
      collection.instrumentSet(banks.front().instruments).samples(banks.front().samples);
      continue;
    }

    // A stream without an explicit bank command begins on the driver's bank
    // zero. Treat that implicit dependency exactly like a referenced bank.
    const std::vector<u8> referencedBanks =
        sequence.referencedBanks.empty() ? std::vector<u8>{0} : sequence.referencedBanks;
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

[[nodiscard]] Diagnostic preparationWarning(std::string message, SourceRange range = {}) {
  return Diagnostic{
      .severity = Severity::Warning,
      .message = std::move(message),
      .range = range.valid() ? std::optional<SourceRange>{range} : std::nullopt,
  };
}

[[nodiscard]] PreparedCollectionAssets prepareSegSatCollection(const CollectionPrepareContext& context) {
  PreparedCollectionAssets prepared;
  const auto& members = context.collection.members;
  if (!members.sequence) {
    return prepared;
  }
  const auto* sequence = context.snapshot.asset<SequenceProgramAsset>(*members.sequence);
  if (sequence == nullptr) {
    return prepared;
  }

  std::vector<SegSatVelocityBank> velocityBanks;
  velocityBanks.reserve(members.instrumentSets.size());
  const std::vector<u8> bankAliases = segSatSequenceBanks(sequence->program);
  if (bankAliases.size() != members.instrumentSets.size()) {
    prepared.diagnostics.push_back(preparationWarning(
        fmt::format("SegSat sequence refers to {} banks, but the collection contains {} instrument sets",
                    bankAliases.size(), members.instrumentSets.size()),
        sequence->metadata.range));
  }
  auto& replacementInstrumentSets = prepared.replacementInstrumentSets.emplace();
  replacementInstrumentSets.reserve(members.instrumentSets.size());
  for (size_t bankIndex = 0; bankIndex < members.instrumentSets.size(); ++bankIndex) {
    const AssetId asset = members.instrumentSets[bankIndex];
    const auto* instruments = context.snapshot.asset<InstrumentSetAsset>(asset);
    if (instruments == nullptr) {
      continue;
    }
    InstrumentSetAsset replacement = *instruments;
    const u8 durableBank =
        static_cast<u8>(replacement.instruments.empty() || !replacement.instruments.front().explicitAddress
                            ? 0
                            : replacement.instruments.front().explicitAddress->bank);
    const u8 sourceBank = bankIndex < bankAliases.size() ? bankAliases[bankIndex] : durableBank;
    const u8 exportBank = members.instrumentSets.size() == 1 ? 0 : sourceBank;
    for (auto& instrument : replacement.instruments) {
      const auto address = resolveInstrumentAddress(instrument.explicitAddress, instrument.identity);
      instrument.explicitAddress = InstrumentAddress{.bank = exportBank, .program = address.program};
      instrument.identity = segSatInstrumentIdentity(sourceBank, static_cast<u8>(address.program));
    }
    replacementInstrumentSets.push_back(std::move(replacement));

    if (!instruments->metadata.range.valid() || !context.sources.contains(instruments->metadata.range.source)) {
      prepared.diagnostics.push_back(preparationWarning("SegSat preparation could not read an instrument bank source",
                                                        instruments->metadata.range));
      continue;
    }
    if (instruments->metadata.range.offset > std::numeric_limits<u32>::max()) {
      prepared.diagnostics.push_back(
          preparationWarning("SegSat instrument bank offset is too large", instruments->metadata.range));
      continue;
    }
    const ByteReader reader = context.sources.reader(instruments->metadata.range.source);
    const auto layout = readSegSatBankLayout(reader, static_cast<u32>(instruments->metadata.range.offset));
    if (!layout) {
      prepared.diagnostics.push_back(preparationWarning(
          "SegSat preparation could not read the selected instrument bank", instruments->metadata.range));
      continue;
    }
    velocityBanks.push_back(readSegSatVelocityBank(reader, *layout, sourceBank));
  }

  if (!velocityBanks.empty()) {
    prepared.finalizePerformance = [velocityBanks = std::move(velocityBanks)](PerformanceSequence& performance) {
      applySegSatVelocityTables(performance, velocityBanks);
    };
  }
  return prepared;
}

}  // namespace

FormatDefinition segSatDefinition() {
  return FormatDefinition{
      .module =
          FormatModule{
              .name = std::string(kSegSatFormatName),
              .collectionResolverId = std::string(kSegSatCollectionResolver),
              .scan = scanSegSat,
              .prepareCollection = prepareSegSatCollection,
          },
      .sequenceDialects = {segSatSequenceDialect()},
  };
}

}  // namespace vgmtrans::formats::segsat
