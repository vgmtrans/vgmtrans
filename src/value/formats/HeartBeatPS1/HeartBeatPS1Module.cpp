/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/HeartBeatPS1/HeartBeatPS1.h"

#include <fmt/format.h>

#include <string>
#include <utility>
#include <vector>

namespace vgmtrans::formats::heartbeat_ps1 {

using namespace core;

namespace {

[[nodiscard]] ScanResult scanHeartBeatPs1(const ScanInput& input) {
  const auto containers = findHeartBeatPs1Containers(input.reader);
  if (containers.empty()) {
    return {};
  }

  ScanResultBuilder result(input, std::string(kHeartBeatPs1FormatName));
  std::vector<ScanSoundBankDraft> banks;
  std::vector<HeartBeatPs1InstrumentInfo> instruments;
  for (const auto& container : containers) {
    for (const auto& layout : container.banks) {
      if (auto bank = addHeartBeatPs1Bank(result, layout)) {
        banks.push_back(bank->bank);
        for (auto& instrument : bank->instruments) {
          instruments.push_back(std::move(instrument));
        }
      } else {
        result.warning("HeartBeatPS1 wave-bank header was recognized, but no playable instruments were found",
                       input.reader.range(layout.attributeOffset, layout.attributeSize));
      }
    }
  }

  for (const auto& container : containers) {
    if (!container.sequence) {
      continue;
    }
    const auto& layout = *container.sequence;
    const std::string name = fmt::format("HeartBeatPS1 Sequence {}", layout.sequenceId);
    auto sequence = result.sequence(name, input.reader.range(layout.containerOffset, layout.containerSize));
    sequence.program(parseHeartBeatPs1Sequence(input.reader, sequence.id(), layout, instruments, &result.sourceMap(),
                                               &result.diagnostics()));
    auto collection =
        result
            .collection(name, CollectionKey{.value = fmt::format("source:{}:heartbeat-sequence:{}",
                                                                 result.source().value, layout.containerOffset)})
            .sequence(sequence);
    // Driver bank-select values index the four IDs in the sequence container.
    // Attach every loaded bank so those source identities remain resolvable.
    for (const auto bank : banks) {
      collection.soundBank(bank);
    }
  }
  return result.finish();
}

}  // namespace

FormatModule heartBeatPs1Module() {
  return FormatModule{
      .name = std::string(kHeartBeatPs1FormatName),
      .preferredSampleFilter = SampleFilter::PsxSpuLowPass,
      .acceptedFormats = {source_formats::kPlayStationRam},
      .scan = scanHeartBeatPs1,
  };
}

}  // namespace vgmtrans::formats::heartbeat_ps1
