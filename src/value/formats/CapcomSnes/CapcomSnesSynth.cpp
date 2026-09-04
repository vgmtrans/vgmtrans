/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/CapcomSnes/CapcomSnes.h"

#include "value/base/RecordReader.h"
#include "value/platform/SnesSampleDirectory.h"
#include "value/synth/SnesDsp.h"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace vgmtrans::formats::capcom_snes {

using namespace core;

namespace {

// Converts Capcom's pitch scale into an exact sample unity key.
[[nodiscard]] double capcomInstrumentUnityKey(s16 pitchScale) {
  constexpr int baseUnityKey = 96;
  constexpr double referencePitch = 0x10b0 / 4096.0;

  // Legacy export models Capcom pitch scale as root-key displacement plus fine tuning.
  const double ratio = pitchScale != 0 ? (static_cast<double>(pitchScale) / 256.0) * referencePitch : 1.0;
  const double semitones = 12.0 * std::log2(ratio);
  int coarse = static_cast<int>(std::lround(semitones));
  int fine = static_cast<int>(std::lround((semitones - coarse) * 100.0));
  if (fine >= 50) {
    ++coarse;
    fine -= 100;
  } else if (fine < -50) {
    --coarse;
    fine += 100;
  }

  const int rootKey = baseUnityKey - coarse;
  return std::clamp(rootKey, 0, 127) - (fine / 100.0);
}

// Converts Capcom's ADSR and gain bytes into the shared envelope model.
[[nodiscard]] Envelope capcomInstrumentEnvelope(u8 adsr1, u8 adsr2, u8 gain) {
  Envelope envelope = (adsr1 & 0x80) != 0 ? snesDspEnvelope(adsr1, adsr2, gain) : Envelope{};

  const u8 sustainLevel = adsr2 >> 5;
  const auto releaseStartEnvelopeLevel = static_cast<s16>((sustainLevel << 8) | 0xff);
  const double releaseSeconds = snesDspGainEnvelopeSeconds(gain, releaseStartEnvelopeLevel, 0);
  envelope.releaseSeconds = releaseSeconds;
  return envelope;
}

}  // namespace

// Reads consecutive six-byte instrument entries. Blank slots are skipped, and
// the first unusable nonblank entry marks the end of the table.
std::vector<CapcomSnesInstrumentInfo> parseCapcomSnesInstrumentInfos(ByteReader reader, u32 instrumentTableAddress,
                                                                     u32 spcDirAddress) {
  std::vector<CapcomSnesInstrumentInfo> instruments;
  const SnesSampleDirectory directory(reader, spcDirAddress);

  for (u32 instrumentIndex = 0; instrumentIndex <= 0xff; ++instrumentIndex) {
    const u32 address = instrumentTableAddress + instrumentIndex * 6;
    if (!reader.has(address, 6)) {
      break;
    }

    RecordReader record(reader, address, address + 6);
    const auto srcn = record.u8("srcn", SourceValueDisplay::Hex);
    const auto adsr1 = record.u8("adsr1", SourceValueDisplay::Hex);
    const auto adsr2 = record.u8("adsr2", SourceValueDisplay::Hex);
    const auto gain = record.u8("gain", SourceValueDisplay::Hex);
    const auto pitchScale = record.s16be("pitch_scale");
    const bool blank = std::ranges::all_of(record.bytes(), [](u8 byte) { return byte == 0 || byte == 0xff; });
    if (blank) {
      continue;
    }

    const auto sample = directory.entry(*srcn, false);
    // The table is contiguous; the first impossible nonblank header ends discovery.
    if (*srcn >= 0x80 || (*adsr1 == 0 && *gain == 0) || !sample || !sample->loopAddressIsBlockAligned()) {
      break;
    }

    instruments.push_back(CapcomSnesInstrumentInfo{
        .index = instrumentIndex,
        .srcn = *srcn,
        .adsr1 = *adsr1,
        .adsr2 = *adsr2,
        .gain = *gain,
        .pitchScale = *pitchScale,
        .source = std::move(record).finish(),
    });
  }

  return instruments;
}

// Builds Capcom's instruments and samples together, then links every instrument
// region to its matching sample.
std::optional<ScanSoundBankDraft> addCapcomSnesSynth(ScanResultBuilder& builder, u32 instrumentTableAddress,
                                                     u32 spcDirAddress, std::string_view displayName) {
  const ByteReader reader = builder.reader();
  const auto instrumentInfos = parseCapcomSnesInstrumentInfos(reader, instrumentTableAddress, spcDirAddress);
  std::vector<u8> referencedSrcns;
  referencedSrcns.reserve(instrumentInfos.size());
  for (const auto& info : instrumentInfos) {
    referencedSrcns.push_back(info.srcn);
  }
  const auto sampleCatalog = readSnesBrrCatalog(reader, spcDirAddress, referencedSrcns);
  if (sampleCatalog.samples.empty()) {
    return std::nullopt;
  }

  const u32 rootOffset = static_cast<u32>(instrumentInfos.front().source.range.offset);
  const u32 rootSize = static_cast<u32>(instrumentInfos.back().source.range.endOffset() - rootOffset);
  const SourceRange instrumentTableRange = reader.range(rootOffset, rootSize);
  auto bank = builder.soundBank(fmt::format("{} Instruments", displayName));
  auto& instruments = bank.instruments();
  const auto sampleRefs = addSnesBrrSamples(bank.localSamples(), reader, sampleCatalog);

  instruments.include(instrumentTableRange);
  const SourceAnnotationId root =
      instruments.source(SourceRole::Table, "Instrument Table", instrumentTableRange, "capcom-snes-instrument-table")
          .id();

  for (const auto& info : instrumentInfos) {
    const auto sample = sampleRefs.findSrcn(info.srcn);
    if (!sample) {
      continue;
    }
    const double unityKey = capcomInstrumentUnityKey(info.pitchScale);
    const SourceRange range = info.source.range;
    const std::string name = fmt::format("Instrument {}", info.index);

    auto instrument = instruments.add(info.index, Instrument{
                                                      .identity =
                                                          InstrumentIdentity{
                                                              .domain = std::string(kCapcomSnesInstrumentDomain),
                                                              .key = info.index,
                                                          },
                                                      .name = name,
                                                  });
    auto annotation =
        instrument.source(name, info.source, "capcom-snes-instrument").derived("instrument", info.index).parent(root);

    instrument
        .region(*sample,
                Region{
                    .unityKey = unityKey,
                    .envelope = capcomInstrumentEnvelope(info.adsr1, info.adsr2, info.gain),
                })
        .source("Region", range, "capcom-snes-region")
        .description(fmt::format("Sample {}", sample->index()));

    auto envelopeAnnotation = builder.sourceMap()
                                  .annotation(SourceRole::DataBlock, "ADSR/Gain", reader.range(range.offset + 1, 3))
                                  .kind("capcom-snes-adsr-gain")
                                  .parent(annotation.id())
                                  .outline(SourceOutlinePolicy::Show);
    for (const auto& field : info.source.fields) {
      if (field.name == "adsr1" || field.name == "adsr2" || field.name == "gain") {
        envelopeAnnotation.field(field.name, field.range, field.value, field.display);
      }
    }
  }

  return bank;
}

}  // namespace vgmtrans::formats::capcom_snes
