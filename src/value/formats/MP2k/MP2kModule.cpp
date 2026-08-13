/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/MP2k/MP2k.h"

#include <fmt/format.h>

#include <charconv>
#include <map>
#include <optional>
#include <string>

namespace vgmtrans::formats::mp2k {

using namespace core;

namespace {

struct BankAssets {
  std::optional<ScanSampleCollectionDraft> samples;
  ScanInstrumentSetDraft instruments;
};

[[nodiscard]] std::optional<u32> selectedSong(const SourceFile& source) {
  const auto text = source.attribute("mp2k.song-index");
  if (!text) {
    return std::nullopt;
  }
  u32 value = 0;
  const auto [end, error] = std::from_chars(text->data(), text->data() + text->size(), value);
  if (error != std::errc{} || end != text->data() + text->size()) {
    return std::nullopt;
  }
  return value;
}

[[nodiscard]] CollectionKey collectionKey(SourceId source, u32 table, u32 song) {
  return CollectionKey{
      .resolver = std::string(kMp2kFormatName),
      .value = fmt::format("source:{}:table:{}:song:{}", source.value, table, song),
  };
}

void scanLayout(const Mp2kLayout& layout, ScanResultBuilder& result) {
  auto psg = addMp2kPsgSamples(result, layout.engine.sampleRate);
  std::map<u32, BankAssets> banks;
  for (const auto& bank : layout.banks) {
    std::optional<ScanSampleCollectionDraft> samples;
    auto instruments = addMp2kInstrumentSet(result, bank, layout.engine.sampleRate,
                                            layout.engine.directSoundMasterVolume, layout.engine.dacBits, psg, samples);
    banks.emplace(bank.offset, BankAssets{.samples = std::move(samples), .instruments = instruments});
  }

  const auto selected = selectedSong(result.sourceFile());
  for (const auto& song : layout.songs) {
    if (selected && song.index != *selected) {
      continue;
    }
    const bool titled = selected == song.index && result.sourceFile().title && !result.sourceFile().title->empty();
    const std::string name = titled ? *result.sourceFile().title : fmt::format("MP2k Song #{:03}", song.index);
    const u32 headerSize = 8 + song.declaredTracks * 4;
    auto sequence = result.sequence(name, result.reader().range(song.offset, headerSize));
    sequence.program(
        parseMp2kSequenceProgram(result.reader(), sequence.id(), song, &result.sourceMap(), &result.diagnostics()));

    auto collection =
        result.collection(name, collectionKey(result.source(), layout.engine.songTableOffset, song.index));
    collection.sequence(sequence).samples(psg);
    if (const auto found = banks.find(song.bankOffset); found != banks.end()) {
      collection.instrumentSet(found->second.instruments);
      if (found->second.samples) {
        collection.samples(*found->second.samples);
      }
    }
  }
}

[[nodiscard]] ScanResult scanMp2k(const ScanInput& input) {
  ScanResultBuilder result(input, std::string(kMp2kFormatName));
  for (const auto& layout : findMp2kLayouts(result)) {
    scanLayout(layout, result);
  }
  return result.finish();
}

}  // namespace

FormatDefinition mp2kDefinition() {
  return FormatDefinition{
      .module = {
          .name = std::string(kMp2kFormatName),
          .acceptedFormats = {source_formats::kGbaRom},
          .scan = scanMp2k,
      },
      .sequenceDialects = {mp2kSequenceDialect()},
  };
}

}  // namespace vgmtrans::formats::mp2k
