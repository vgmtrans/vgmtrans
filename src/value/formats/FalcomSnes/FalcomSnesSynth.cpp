/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/FalcomSnes/FalcomSnes.h"

#include "value/platform/SnesSampleDirectory.h"
#include "value/synth/SnesDsp.h"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace vgmtrans::formats::falcom_snes {

using namespace core;

namespace {

[[nodiscard]] double unityKey(u16 pitchScale) {
  // $1adf is the driver's top pitch-table entry. Together with the 8.8 patch
  // multiplier and its raw-note -> MIDI +24 mapping it gives this exact root.
  constexpr double kPitchTableC6 = 0x10be / 4096.0;
  return 96.0 - std::log2((pitchScale / 256.0) * kPitchTableC6) * 12.0;
}

}  // namespace

PatchTable parsePatches(ByteReader reader, const Layout& layout) {
  PatchTable result;
  for (u32 program = 0; program < result.size(); ++program) {
    result[program].program = static_cast<u8>(program);
    // MUL YA followed by MOV Y,A deliberately wraps the five-byte row offset.
    const u16 row = static_cast<u16>(layout.instrumentTableAddress + static_cast<u8>(program * 5u));
    if (reader.has(row, 5)) {
      result[program].adsr1 = reader.u8At(row);
      result[program].adsr2 = reader.u8At(row + 1);
      result[program].pitchScale = reader.be16(row + 3);
      result[program].source = reader.range(row, 5);
    }
  }

  // The loader maintains at most 32 dynamic DIR entries and terminates the
  // instrument-ID list with FF. The list index is also the live SRCN.
  for (u8 srcn = 0; srcn < 32 && reader.has(layout.instrumentSrcnMapAddress + srcn, 1); ++srcn) {
    const u32 address = layout.instrumentSrcnMapAddress + srcn;
    const u8 program = reader.u8At(address);
    if (program == 0xff) {
      break;
    }
    if (!result[program].srcn) {
      result[program].srcn = srcn;
      result[program].srcnSource = reader.range(address, 1);
    }
  }
  return result;
}

std::optional<ScanSoundBankDraft> addSynth(ScanResultBuilder& builder, const Layout& layout,
                                           const std::set<u8>& programs, const PatchTable& patchTable,
                                           std::string_view displayName) {
  const ByteReader reader = builder.reader();
  std::vector<Patch> patches;
  for (const u8 program : programs) {
    const Patch& patch = patchTable[program];
    if (patch.srcn && patch.pitchScale != 0) {
      patches.push_back(patch);
    }
  }
  if (patches.empty()) {
    return std::nullopt;
  }
  std::vector<u8> srcns;
  srcns.reserve(patches.size());
  for (const Patch& patch : patches) {
    srcns.push_back(*patch.srcn);
  }
  const SnesBrrCatalog catalog = readSnesBrrCatalog(reader, layout.spcDirAddress, srcns);
  if (catalog.samples.empty()) {
    return std::nullopt;
  }

  auto bank = builder.soundBank(fmt::format("{} Instruments", displayName));
  auto& instruments = bank.instruments();
  const SnesBrrSampleRefs sampleRefs = addSnesBrrSamples(bank.localSamples(), reader, catalog);
  for (const Patch& patch : patches) {
    const auto sample = sampleRefs.findSrcn(*patch.srcn);
    if (!sample) {
      continue;
    }
    auto instrument = instruments.append(Instrument{
        .explicitAddress = InstrumentAddress{.bank = 0, .program = patch.program},
        .identity = InstrumentIdentity{.domain = std::string(kInstrumentDomain), .key = patch.program},
        .name = fmt::format("Instrument {} (SRCN {})", patch.program, *patch.srcn),
        .range = patch.source,
    });
    const SourceAnnotationId root =
        instrument.source(fmt::format("Instrument {}", patch.program), patch.source, "falcom-snes-instrument").id();
    instrument.source("SRCN lookup", *patch.srcnSource, "falcom-snes-srcn")
        .parent(root)
        .description(fmt::format("instrument {} -> SRCN {}", patch.program, *patch.srcn));
    instrument
        .region(*sample,
                Region{
                    .range = patch.source,
                    .unityKey = unityKey(patch.pitchScale),
                    .envelope = snesDspEnvelope(static_cast<u8>(patch.adsr1 | 0x80), patch.adsr2, 0),
                })
        .source("Region", patch.source, "falcom-snes-region")
        .description(fmt::format("SRCN {}, 8.8 pitch scale ${:04X}", *patch.srcn, patch.pitchScale));
  }
  return bank;
}

}  // namespace vgmtrans::formats::falcom_snes
