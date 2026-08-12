/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/HeartBeatSnes/HeartBeatSnes.h"

#include "value/platform/SnesSampleDirectory.h"
#include "value/synth/SnesDsp.h"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace vgmtrans::formats::heartbeat_snes {

using namespace core;

namespace {

struct Patch {
  u8 program = 0;
  u8 sampleIndex = 0;
  u8 srcn = 0;
  u8 adsr1 = 0;
  u8 adsr2 = 0;
  u16 pitchScale = 0;
  SourceRange source;
  SourceRange srcnSource;
};

[[nodiscard]] double unityKey(Version version, u16 pitchScale) {
  // The multiply routine at DQ3 $1175 treats byte 4 as the integer part and
  // byte 5 as the fraction. Its ARAM comments label those bytes backwards.
  // The two shipped drivers also use slightly different C6 pitch entries.
  const double pitchTableCorrection = (version == Version::DragonQuest3 ? 0x10d4 : 0x10be) / 4096.0;
  return 72.0 - std::log2((pitchScale / 256.0) * pitchTableCorrection) * 12.0;
}

[[nodiscard]] Envelope driverEnvelope(u8 adsr1, u8 adsr2) {
  Envelope envelope = snesDspEnvelope(static_cast<u8>(adsr1 | 0x80), adsr2, 0);
  // Gate expiry changes ADSR2's sustain-rate field rather than issuing KOF.
  // Instrument selection initializes the gated rate from the ordinary SR.
  envelope.releaseSeconds = snesDspAdsrSustainSeconds(adsr2 & 0x1f);
  return envelope;
}

[[nodiscard]] std::vector<Patch> collectPatches(ByteReader reader, const Layout& layout,
                                                const SequenceRecipes& recipes) {
  std::vector<Patch> patches;
  for (const u8 program : recipes.programs) {
    // The SPC700 multiplication keeps only the low byte before indexing the
    // song-relative table.
    const u16 row = static_cast<u16>(layout.instrumentTableAddress + static_cast<u8>(program * 6u));
    if (!reader.has(row, 6)) {
      continue;
    }
    const u8 sampleIndex = static_cast<u8>(reader.u8At(row) + layout.songIndex * 0x10u);
    const u16 srcnAddress = static_cast<u16>(layout.srcnTableAddress + sampleIndex);
    if (!reader.has(srcnAddress, 1)) {
      continue;
    }
    const u8 srcn = reader.u8At(srcnAddress);
    const u16 pitchScale = reader.be16(row + 4);
    const auto directory =
        readSnesSampleDirectoryEntry(reader, static_cast<u16>(layout.spcDirAddress + srcn * 4u), true);
    if (pitchScale == 0 || !directory || !directory->stream || !directory->loopAddressIsBlockAligned()) {
      continue;
    }
    patches.push_back(Patch{
        .program = program,
        .sampleIndex = sampleIndex,
        .srcn = srcn,
        .adsr1 = reader.u8At(row + 1),
        .adsr2 = reader.u8At(row + 2),
        .pitchScale = pitchScale,
        .source = reader.range(row, 6),
        .srcnSource = reader.range(srcnAddress, 1),
    });
  }
  return patches;
}

[[nodiscard]] std::vector<u8> referencedSrcns(const std::vector<Patch>& patches) {
  std::vector<u8> result;
  for (const Patch& patch : patches) {
    result.push_back(patch.srcn);
  }
  std::ranges::sort(result);
  result.erase(std::ranges::unique(result).begin(), result.end());
  return result;
}

}  // namespace

std::optional<ScanSynthRefs> addSynth(ScanResultBuilder& builder, const Layout& layout, const SequenceRecipes& recipes,
                                      std::string_view displayName) {
  const ByteReader reader = builder.reader();
  const std::vector<Patch> patches = collectPatches(reader, layout, recipes);
  if (patches.empty()) {
    return std::nullopt;
  }
  const SnesBrrCatalog catalog = readSnesBrrCatalog(reader, layout.spcDirAddress, referencedSrcns(patches));
  if (catalog.samples.empty()) {
    return std::nullopt;
  }

  auto instruments = builder.instrumentSet(fmt::format("{} Instruments", displayName));
  auto sampleCollection = builder.sampleCollection(fmt::format("{} Samples", displayName));
  const SnesBrrSampleRefs samples = addSnesBrrSamples(sampleCollection.builder(), reader, catalog);

  for (const Patch& patch : patches) {
    const auto sample = samples.findSrcn(patch.srcn);
    if (!sample) {
      continue;
    }
    auto entry = instruments.append(Instrument{
        .explicitAddress = InstrumentAddress{.bank = 0, .program = patch.program},
        .identity = InstrumentIdentity{.domain = std::string(kInstrumentDomain), .key = patch.program},
        .name = fmt::format("Instrument {} (SRCN {})", patch.program, patch.srcn),
        .range = patch.source,
    });
    const SourceAnnotationId root =
        entry.source(fmt::format("Instrument {}", patch.program), patch.source, "heartbeat-snes-instrument").id();
    entry.source("SRCN lookup", patch.srcnSource, "heartbeat-snes-srcn")
        .parent(root)
        .description(fmt::format("sample index {} -> SRCN {}", patch.sampleIndex, patch.srcn));
    entry
        .region(*sample,
                Region{
                    .range = patch.source,
                    .unityKey = unityKey(layout.version, patch.pitchScale),
                    .envelope = driverEnvelope(patch.adsr1, patch.adsr2),
                })
        .source("Region", patch.source, "heartbeat-snes-region")
        .description(fmt::format("SRCN {}, scale ${:04X}", patch.srcn, patch.pitchScale));
  }

  if (instruments.builder().empty()) {
    return std::nullopt;
  }
  return ScanSynthRefs{.instruments = instruments.ref(), .samples = sampleCollection.ref()};
}

}  // namespace vgmtrans::formats::heartbeat_snes
