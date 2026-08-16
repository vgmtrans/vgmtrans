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

[[nodiscard]] Diagnostic bindingWarning(std::string message, SourceRange range = {}) {
  return Diagnostic{
      .severity = Severity::Warning,
      .message = std::move(message),
      .range = range.valid() ? std::optional<SourceRange>{range} : std::nullopt,
  };
}

}  // namespace

void bindSegSatCollection(CollectionBindingContext& context) {
  if (context.sequence == nullptr) {
    return;
  }
  const auto& sequence = *context.sequence;

  SegSatVolumeModel volumeModel = SegSatVolumeModel::V1_33;
  if (sequence.metadata.range.valid() && context.sources.contains(sequence.metadata.range.source)) {
    volumeModel = determineSegSatVolumeModel(context.sources.reader(sequence.metadata.range.source));
  }
  std::vector<SegSatControllerChange> controllerChanges = segSatControllerChanges(sequence.program);

  std::vector<SegSatVelocityBank> velocityBanks;
  velocityBanks.reserve(context.instrumentSets.size());
  const std::vector<u8> bankAliases = segSatSequenceBanks(sequence.program);
  if (bankAliases.size() != context.instrumentSets.size()) {
    context.diagnostics.push_back(
        bindingWarning(fmt::format("SegSat sequence refers to {} banks, but the collection contains {} instrument sets",
                                   bankAliases.size(), context.instrumentSets.size()),
                       sequence.metadata.range));
  }
  for (size_t bankIndex = 0; bankIndex < context.instrumentSets.size(); ++bankIndex) {
    auto& instruments = context.instrumentSets[bankIndex];
    const u8 durableBank =
        static_cast<u8>(instruments.instruments.empty() || !instruments.instruments.front().explicitAddress
                            ? 0
                            : instruments.instruments.front().explicitAddress->bank);
    const u8 sourceBank = bankIndex < bankAliases.size() ? bankAliases[bankIndex] : durableBank;
    const u8 exportBank = context.instrumentSets.size() == 1 ? 0 : sourceBank;
    for (auto& instrument : instruments.instruments) {
      const auto address = resolveInstrumentAddress(instrument.explicitAddress, instrument.identity);
      instrument.explicitAddress = InstrumentAddress{.bank = exportBank, .program = address.program};
      instrument.identity = segSatInstrumentIdentity(sourceBank, static_cast<u8>(address.program));
    }

    if (!instruments.metadata.range.valid() || !context.sources.contains(instruments.metadata.range.source)) {
      context.diagnostics.push_back(
          bindingWarning("SegSat binding could not read an instrument bank source", instruments.metadata.range));
      continue;
    }
    if (instruments.metadata.range.offset > std::numeric_limits<u32>::max()) {
      context.diagnostics.push_back(
          bindingWarning("SegSat instrument bank offset is too large", instruments.metadata.range));
      continue;
    }
    const ByteReader reader = context.sources.reader(instruments.metadata.range.source);
    const auto layout = readSegSatBankLayout(reader, static_cast<u32>(instruments.metadata.range.offset));
    if (!layout) {
      context.diagnostics.push_back(
          bindingWarning("SegSat binding could not read the selected instrument bank", instruments.metadata.range));
      continue;
    }
    velocityBanks.push_back(readSegSatVelocityBank(reader, *layout, sourceBank, volumeModel));
  }

  if (!velocityBanks.empty()) {
    context.sequenceRuntime = segSatSequenceRuntime(SegSatRuntimeConfig{
        .velocityBanks = std::move(velocityBanks),
        .volumeModel = volumeModel,
        .controllerChanges = std::move(controllerChanges),
    });
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
