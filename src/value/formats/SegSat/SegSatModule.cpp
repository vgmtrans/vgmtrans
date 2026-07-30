/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/SegSat/SegSat.h"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace vgmtrans::formats::segsat {

using namespace core;

namespace {

struct BankAssets {
  SegSatBankLayout layout;
  u8 exportBank = 0;
  ScanInstrumentSetRef instruments;
  ScanSampleCollectionRef samples;
};

[[nodiscard]] CollectionKey collectionKey(SourceId source, const SegSatSequenceLayout& sequence) {
  return CollectionKey{
      .resolver = std::string(kSegSatCollectionResolver),
      .value = "source:" + std::to_string(source.value) + ":sequence:" + std::to_string(sequence.offset),
  };
}

[[nodiscard]] u32 normalStreamEnd(ByteReader reader, const SegSatSequenceLayout& sequence) {
  u32 offset = sequence.offset + sequence.normalTrack;
  while (offset < sequence.end) {
    const u8 status = reader.u8At(offset);
    u32 size = 1;
    if (status <= 0x7f) {
      size = 5;
    } else if ((status & 0xf0) == 0xb0) {
      size = 4;
    } else if ((status & 0xf0) == 0xc0 || (status & 0xf0) == 0xd0 || (status & 0xf0) == 0xe0) {
      size = 3;
    } else if (status == 0x81) {
      size = 4;
    } else if (status == 0x82) {
      size = 2;
    } else if (status == 0x83) {
      return offset + 1;
    }
    if (size > sequence.end - offset) {
      break;
    }
    offset += size;
  }
  return std::min(offset, sequence.end);
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
          .exportBank = bankNumber,
          .instruments = scanned->instruments,
          .samples = scanned->samples,
      });
    }
  }

  for (const auto& sequence : sequenceLayouts) {
    const std::string sourceName =
        result.sourceFile().name.empty() ? result.sourceDisplayName() : result.sourceFile().name;
    const std::string name = fmt::format("{} {}_{}", sourceName, sequence.tableIndex, sequence.sequenceIndex);
    const u32 sequenceEnd = normalStreamEnd(input.reader, sequence);
    const auto sequenceRef = result.reserveSequence();
    result.sequence(sequenceRef, name, input.reader.range(sequence.offset, sequenceEnd - sequence.offset))
        .program(parseSegSatSequenceProgram(input.reader, sequenceRef.id, sequence, &result.sourceMap(),
                                            &result.diagnostics()));

    auto collection = result.collection(name, collectionKey(result.source(), sequence)).sequence(sequenceRef);
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

[[nodiscard]] std::vector<u8> sequenceBankAliases(const SequenceProgram& program) {
  constexpr std::array<u8, 4> signature{'S', 'B', 'R', '1'};
  const auto& bytes = program.config.driverData;
  if (bytes.size() < signature.size() ||
      !std::equal(signature.begin(), signature.end(), bytes.begin())) {
    return {0};
  }
  std::vector<u8> aliases(bytes.begin() + static_cast<std::ptrdiff_t>(signature.size()), bytes.end());
  if (aliases.empty()) {
    aliases.push_back(0);
  }
  return aliases;
}

[[nodiscard]] PreparedCollectionAssets prepareSegSatCollection(const CollectionPrepareContext& context) {
  PreparedCollectionAssets prepared;
  if (!context.collection.sequence) {
    return prepared;
  }
  const auto* sequence = context.snapshot.asset<SequenceProgramAsset>(*context.collection.sequence);
  if (sequence == nullptr) {
    return prepared;
  }

  std::vector<SegSatBankBinding> bindings;
  std::optional<SourceId> bindingSource;
  const std::vector<u8> bankAliases = sequenceBankAliases(sequence->program);
  for (size_t bankIndex = 0; bankIndex < context.collection.instrumentSets.size(); ++bankIndex) {
    const AssetId asset = context.collection.instrumentSets[bankIndex];
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
    const u8 exportBank = context.collection.instrumentSets.size() == 1 ? 0 : sourceBank;
    for (auto& instrument : replacement.instruments) {
      const auto address = resolveInstrumentAddress(instrument.explicitAddress, instrument.identity);
      instrument.explicitAddress = InstrumentAddress{.bank = exportBank, .program = address.program};
      instrument.identity = segSatInstrumentIdentity(sourceBank, static_cast<u8>(address.program));
    }
    prepared.replacementInstrumentSets.push_back(std::move(replacement));

    if (!instruments->metadata.range.valid() || !context.sources.contains(instruments->metadata.range.source)) {
      prepared.diagnostics.push_back(preparationWarning("SegSat preparation could not read an instrument bank source",
                                                        instruments->metadata.range));
      continue;
    }
    if (bindingSource && *bindingSource != instruments->metadata.range.source) {
      prepared.diagnostics.push_back(
          preparationWarning("SegSat velocity banks came from different sources; only the first source was used",
                             instruments->metadata.range));
      continue;
    }
    bindingSource = instruments->metadata.range.source;
    const ByteReader reader = context.sources.reader(instruments->metadata.range.source);
    const auto layouts = findSegSatBanks(reader);
    const auto layout =
        std::ranges::find(layouts, static_cast<u32>(instruments->metadata.range.offset), &SegSatBankLayout::offset);
    if (layout == layouts.end()) {
      prepared.diagnostics.push_back(preparationWarning(
          "SegSat preparation could not rediscover the selected instrument bank", instruments->metadata.range));
      continue;
    }
    bindings.push_back(SegSatBankBinding{
        .layout = *layout,
        .sourceBank = sourceBank,
        .exportBank = exportBank,
    });
  }

  SequenceProgramAsset replacement = *sequence;
  if (!bindings.empty() && bindingSource && context.sources.contains(*bindingSource)) {
    replacement.program.config.driverData = makeSegSatVelocityContext(context.sources.reader(*bindingSource), bindings);
  }
  prepared.replacementSequence = std::move(replacement);
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
