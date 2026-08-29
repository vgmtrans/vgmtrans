/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/AsciiShuichiSnes/AsciiShuichiSnes.h"

#include "value/platform/SnesSampleDirectory.h"
#include "value/synth/SnesDsp.h"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace vgmtrans::formats::ascii_shuichi_snes {

using namespace core;

Envelope driverEnvelope(u8 adsr1, u8 adsr2, u8 gain) {
  Envelope envelope = snesDspEnvelope(adsr1, adsr2, gain);
  // Note expiry uses the patch's GAIN byte instead of KOF. The driver first
  // writes GAIN and then clears ADSR1's enable bit.
  const s16 releaseStart = static_cast<s16>(((adsr2 >> 5) << 8) | 0xff);
  if ((gain > 0 && gain < 0x80) || gain >= 0xc0) {
    // Direct nonzero and increasing GAIN modes never decay to silence.
    envelope.releaseSeconds = std::numeric_limits<double>::infinity();
  } else {
    envelope.releaseSeconds = snesDspGainEnvelopeSeconds(gain, releaseStart, 0);
  }
  return envelope;
}

namespace {

[[nodiscard]] std::vector<Patch> collectPatches(ByteReader reader, const Layout& layout,
                                                const std::set<u8>& programs) {
  std::vector<Patch> patches;
  const SnesSampleDirectory directory(reader, layout.spcDirAddress);
  for (const u8 program : programs) {
    // The loader performs two 8-bit ASLs, so programs 64..255 alias the same
    // 64 physical rows exactly as they do on the SPC700.
    const u32 row = layout.instrumentTableAddress + static_cast<u8>(program * 4u);
    if (!reader.has(row, 4)) {
      continue;
    }
    const u8 srcn = reader.u8At(row);
    const auto sample = directory.entry(srcn);
    const u32 tuning = layout.tuningTableAddress + srcn;
    if (!sample || !sample->stream || (sample->stream->loops && !sample->loopAddressIsBlockAligned()) ||
        !reader.has(tuning, 1)) {
      continue;
    }
    patches.push_back(Patch{
        .program = program,
        .srcn = srcn,
        .adsr1 = reader.u8At(row + 1),
        .adsr2 = reader.u8At(row + 2),
        .gain = reader.u8At(row + 3),
        .tuning = static_cast<s8>(reader.u8At(tuning)),
        .source = reader.range(row, 4),
        .tuningSource = reader.range(tuning, 1),
    });
  }
  return patches;
}

[[nodiscard]] std::vector<u8> referencedSrcns(const std::vector<Patch>& patches) {
  std::vector<u8> srcns;
  srcns.reserve(patches.size());
  for (const Patch& patch : patches) {
    srcns.push_back(patch.srcn);
  }
  return srcns;
}

[[nodiscard]] double unityKey(s8 tuning) {
  const double ratio = 1.0 + tuning / 4096.0;
  return 81.0 - 12.0 * std::log2(ratio);
}

}  // namespace

std::optional<ScanSoundBankDraft> addSynth(ScanResultBuilder& builder, const Layout& layout,
                                           const std::set<u8>& programs, std::string_view displayName) {
  const ByteReader reader = builder.reader();
  const std::vector<Patch> patches = collectPatches(reader, layout, programs);
  if (patches.empty()) {
    return std::nullopt;
  }
  const SnesBrrCatalog catalog = readSnesBrrCatalog(reader, layout.spcDirAddress, referencedSrcns(patches));
  if (catalog.samples.empty()) {
    return std::nullopt;
  }

  auto bank = builder.soundBank(fmt::format("{} Instruments", displayName));
  auto& instruments = bank.instruments();
  auto& samplePool = bank.localSamples();
  const SnesBrrSampleRefs samples = addSnesBrrSamples(samplePool, reader, catalog);

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
        entry.source(fmt::format("Instrument {}", patch.program), patch.source, "ascii-shuichi-snes-instrument").id();
    entry.source("Fine tuning", patch.tuningSource, "ascii-shuichi-snes-tuning")
        .parent(root)
        .description(fmt::format("SRCN {} tuning {:+d}/4096", patch.srcn, patch.tuning));
    entry
        .region(*sample,
                Region{
                    .range = patch.source,
                    .unityKey = unityKey(patch.tuning),
                    .envelope = driverEnvelope(patch.adsr1, patch.adsr2, patch.gain),
                })
        .source("Region", patch.source, "ascii-shuichi-snes-region")
        .description(fmt::format("SRCN {}, ADSR ${:02X} ${:02X}, release GAIN ${:02X}", patch.srcn, patch.adsr1,
                                 patch.adsr2, patch.gain));
  }
  return bank;
}

}  // namespace vgmtrans::formats::ascii_shuichi_snes
