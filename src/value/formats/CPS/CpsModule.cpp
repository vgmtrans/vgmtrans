/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/CPS/Cps.h"

#include "value/extractors/MameRomSetExtractor.h"

#include <vector>

namespace vgmtrans::formats::cps {

using namespace core;

namespace {

[[nodiscard]] bool canScanCps(const SourceFile& source, std::span<const u8>) {
  const auto format = source.attribute(mame::kMameFormatAttribute);
  return format == "CPS1" || format == "CPS2";
}

[[nodiscard]] CollectionKey collectionKey(SourceId source, u32 sequenceIndex) {
  return CollectionKey{
      .resolver = std::string(kCpsFormatName),
      .value = "source:" + std::to_string(source.value) + ":sequence:" + std::to_string(sequenceIndex),
  };
}

[[nodiscard]] ScanResult scanCps(const ScanInput& input) {
  ScanResultBuilder result(input, std::string(kCpsFormatName));
  auto layout = findCpsLayout(input.source, input.reader, &result.diagnostics());
  if (!layout) {
    return result.finish();
  }

  const auto firstInstruments = result.reserveInstrumentSet();
  const auto secondInstruments = isCps1(layout->version) ? std::optional{result.reserveInstrumentSet()} : std::nullopt;
  const auto samples = result.reserveSampleCollection();

  std::vector<ScanMiscAssetRef> miscAssets;
  const auto addTable = [&](std::string name, u32 offset, u32 size) {
    if (size == 0 || !input.reader.has(offset, size)) {
      return;
    }
    const SourceRange range = input.reader.range(offset, size);
    const auto bytes = input.reader.slice(offset, size);
    const auto misc = result.misc(name, range).payload(std::vector<u8>(bytes.begin(), bytes.end()));
    result.sourceMap().table(name, range).owner(ObjectRefs::misc(misc.id)).kind("cps-driver-table");
    miscAssets.push_back(misc);
  };
  addTable(layout->game + " Sequence Pointer Table", layout->sequenceTableOffset, layout->sequenceTableLength);
  if (!isCps1(layout->version)) {
    addTable(layout->game + " QSound Sample Info Table", layout->sampleInfoTableOffset, layout->sampleInfoTableLength);
    if (layout->articulationTableOffset) {
      addTable(layout->game + " QSound Articulation Table", *layout->articulationTableOffset,
               layout->articulationTableLength);
    }
  }

  Cps1SynthAvailability cps1Availability;
  bool qsoundAvailable = false;
  if (isCps1(layout->version)) {
    cps1Availability = addCps1Synth(result, firstInstruments, *secondInstruments, samples, *layout);
  } else {
    qsoundAvailable = addCpsQSoundSynth(result, firstInstruments, samples, *layout);
  }

  for (const auto& sourceSequence : layout->sequences) {
    const auto sequence = result.reserveSequence();
    result.sequence(sequence, sourceSequence.name, input.reader.range(sourceSequence.offset, 1))
        .program(decodeCpsSequence(input.reader, *layout, sourceSequence, sequence.id, &result.sourceMap(),
                                   &result.diagnostics()));

    auto collection = result.collection(sourceSequence.name, collectionKey(input.source.id, sourceSequence.index));
    collection.sequence(sequence);
    if (isCps1(layout->version)) {
      if (cps1Availability.ym2151) {
        collection.instrumentSet(firstInstruments);
      }
      if (cps1Availability.oki) {
        collection.instrumentSet(*secondInstruments).samples(samples);
      }
    } else if (qsoundAvailable) {
      collection.instrumentSet(firstInstruments).samples(samples);
    }
    for (const auto misc : miscAssets) {
      collection.misc(misc);
    }

    result.sourceFact(sequence.id,
                      FormatSpecificFact{
                          .kind = "cps-sequence",
                          .fields =
                              {
                                  MatchField{.name = "game", .value = layout->game},
                                  MatchField{.name = "version", .value = cpsVersionName(layout->version)},
                                  MatchField{.name = "index", .value = std::to_string(sourceSequence.index)},
                              },
                      });
  }
  return result.finish();
}

}  // namespace

FormatDefinition cpsDefinition() {
  return FormatDefinition{
      .module =
          {
              .name = std::string(kCpsFormatName),
              .canScan = canScanCps,
              .scan = scanCps,
          },
      .sequenceDialects = {cps1V1Dialect(), cpsEarlyDialect(), cpsLateDialect()},
  };
}

}  // namespace vgmtrans::formats::cps
