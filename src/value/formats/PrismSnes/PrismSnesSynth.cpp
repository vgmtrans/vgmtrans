/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/PrismSnes/PrismSnes.h"

#include "value/platform/SnesSampleDirectory.h"
#include "value/synth/SnesDsp.h"

#include <fmt/format.h>

#include <cmath>
#include <set>
#include <string>
#include <vector>

namespace vgmtrans::formats::prism_snes {

using namespace core;

Envelope driverEnvelope(u8 adsr1, u8 adsr2) {
  return snesDspEnvelope(static_cast<u8>(adsr1 | 0x80), adsr2, 0);
}

namespace {

struct Patch {
  u8 program = 0;
  u8 adsr1 = 0;
  u8 adsr2 = 0;
  s16 tuning = 0;
  SourceRange adsr1Source;
  SourceRange adsr2Source;
  SourceRange tuningHighSource;
  SourceRange tuningLowSource;
};

[[nodiscard]] std::vector<Patch> collectPatches(ByteReader reader, const Layout& layout, const std::set<u8>& programs) {
  std::vector<Patch> patches;
  const SnesSampleDirectory directory(reader, layout.spcDirAddress);
  std::set<u8> availablePrograms = programs;
  for (u16 candidate = 0; candidate < 0x100; ++candidate) {
    const u8 program = static_cast<u8>(candidate);
    const auto sample = directory.entry(program);
    if (sample && sample->stream && sample->startAddress >= layout.spcDirAddress) {
      availablePrograms.insert(program);
    }
  }
  for (const u8 program : availablePrograms) {
    const auto sample = directory.entry(program);
    if (!sample || !sample->stream || sample->startAddress < layout.spcDirAddress) {
      continue;
    }
    const u32 adsr1 = layout.adsr1TableAddress + program;
    const u32 adsr2 = layout.adsr2TableAddress + program;
    const u32 tuningHigh = layout.tuningHighTableAddress + program;
    const u32 tuningLow = layout.tuningLowTableAddress + program;
    patches.push_back(Patch{
        .program = program,
        .adsr1 = reader.u8At(adsr1),
        .adsr2 = reader.u8At(adsr2),
        .tuning = static_cast<s16>(reader.u8At(tuningLow) | (reader.u8At(tuningHigh) << 8)),
        .adsr1Source = reader.range(adsr1, 1),
        .adsr2Source = reader.range(adsr2, 1),
        .tuningHighSource = reader.range(tuningHigh, 1),
        .tuningLowSource = reader.range(tuningLow, 1),
    });
  }
  return patches;
}

[[nodiscard]] std::vector<u8> srcns(const std::vector<Patch>& patches) {
  std::vector<u8> result;
  result.reserve(patches.size());
  for (const Patch& patch : patches) {
    result.push_back(patch.program);
  }
  return result;
}

}  // namespace

std::optional<ScanSoundBankDraft> addSynth(ScanResultBuilder& builder, const Layout& layout,
                                           const std::set<u8>& programs, std::string_view displayName) {
  const ByteReader reader = builder.reader();
  const std::vector<Patch> patches = collectPatches(reader, layout, programs);
  if (patches.empty()) {
    return std::nullopt;
  }
  const SnesBrrCatalog catalog = readSnesBrrCatalog(reader, layout.spcDirAddress, srcns(patches));
  if (catalog.samples.empty()) {
    return std::nullopt;
  }

  auto bank = builder.soundBank(fmt::format("{} Instruments", displayName));
  auto& instruments = bank.instruments();
  auto& samplePool = bank.localSamples();
  const SnesBrrSampleRefs samples = addSnesBrrSamples(samplePool, reader, catalog);
  for (const Patch& patch : patches) {
    const auto sample = samples.findSrcn(patch.program);
    if (!sample) {
      continue;
    }
    const double semitones = patch.tuning / 256.0;
    const SourceRange range = patch.adsr1Source;
    auto entry = instruments.append(Instrument{
        .explicitAddress = InstrumentAddress{.bank = static_cast<u32>(patch.program >> 7),
                                             .program = static_cast<u32>(patch.program & 0x7f)},
        .identity = InstrumentIdentity{.domain = std::string(kInstrumentDomain), .key = patch.program},
        .name = fmt::format("Instrument {}", patch.program),
        .range = range,
    });
    const SourceAnnotationId root =
        entry.source(fmt::format("Instrument {}", patch.program), range, "prism-snes-instrument").id();
    entry.source("ADSR1", patch.adsr1Source, "prism-snes-adsr1").parent(root);
    entry.source("ADSR2", patch.adsr2Source, "prism-snes-adsr2").parent(root);
    entry.source("Coarse Tuning", patch.tuningHighSource, "prism-snes-tuning-high").parent(root);
    entry.source("Fine Tuning", patch.tuningLowSource, "prism-snes-tuning-low").parent(root);
    entry
        .region(*sample,
                Region{
                    .range = range,
                    .unityKey = 93.0 - semitones,
                    .envelope = driverEnvelope(patch.adsr1, patch.adsr2),
                })
        .source("Region", range, "prism-snes-region")
        .parent(root)
        .description(fmt::format("SRCN {}, tuning {:.4f} semitones", patch.program, semitones));
  }
  return bank;
}

}  // namespace vgmtrans::formats::prism_snes
