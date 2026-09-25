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

void scanLayout(const Mp2kLayout& layout, ScanResultBuilder& result, const RetainedSource& source) {
  auto psg = result.samplePool("MP2k PSG samples");
  std::map<u32, Mp2kScannedBank> banks;
  for (const auto& bank : layout.banks) {
    auto scanned = addMp2kInstrumentSet(result, bank, layout.engine, psg);
    scanned.instruments.useSamples(psg);
    banks.emplace(bank.offset, std::move(scanned));
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
    const auto bank = banks.find(song.bankOffset);
    const std::span<const Mp2kTone> tones = bank == banks.end() ? std::span<const Mp2kTone>{} : bank->second.tones;
    sequence.program(
        parseMp2kSequenceProgram(source, sequence.id(), song, tones, &result.sourceMap(), &result.diagnostics()));

    sequence.collection();
    if (bank != banks.end()) {
      sequence.useBank(bank->second.instruments);
    }
  }
}

[[nodiscard]] ScanResult scanMp2k(const ScanInput& input) {
  ScanResultBuilder result(input, std::string(kMp2kFormatName));
  const RetainedSource source = input.retain();
  for (const auto& layout : findMp2kLayouts(result)) {
    scanLayout(layout, result, source);
  }
  return result.finish();
}

}  // namespace

FormatModule mp2kModule() {
  return FormatModule{
      .name = std::string(kMp2kFormatName),
      .acceptedFormats = {source_formats::kGbaRom},
      .scan = scanMp2k,
  };
}

}  // namespace vgmtrans::formats::mp2k
