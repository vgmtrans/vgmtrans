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

const InstrumentModulation kCapcomModulation{
    .vibrato =
        VibratoSpec{
            .maxDepthCents = 1200.0,
            .rateHertz = {kCapcomSnesLfoStepHertz, 255.0 * kCapcomSnesLfoStepHertz},
        },
    .tremolo =
        TremoloSpec{
            .maxDepthDb = kCapcomSnesTremoloHalfDepthCentibels / 10.0,
            .rateHertz = {2.0 * kCapcomSnesLfoStepHertz, 510.0 * kCapcomSnesLfoStepHertz},
            .gainMode = TremoloGainMode::NoBoost,
        },
};

struct InstrumentPitch {
  Tuning aggregate;
  u8 rootKey = 96;
  s16 fineTuneCents = 0;
};

[[nodiscard]] InstrumentPitch capcomInstrumentPitch(s16 pitchScale) {
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
  return InstrumentPitch{
      .aggregate = Tuning{.cents = (rootKey - baseUnityKey) * 100 + fine},
      .rootKey = static_cast<u8>(std::clamp(rootKey, 0, 127)),
      .fineTuneCents = static_cast<s16>(fine),
  };
}

[[nodiscard]] Envelope capcomInstrumentEnvelope(u8 adsr1, u8 adsr2, u8 gain) {
  Envelope envelope;
  if ((adsr1 & 0x80) != 0) {
    envelope = snesDspEnvelope(adsr1, adsr2, gain);
  }

  const u8 sustainLevel = adsr2 >> 5;
  const double releaseSeconds = snesDspGainEnvelopeSeconds(gain, static_cast<s16>((sustainLevel << 8) | 0xff), 0);
  envelope.release = static_cast<u32>(std::lround(std::max(0.0, releaseSeconds) * 1'000'000.0));
  envelope.releaseSeconds = releaseSeconds;
  return envelope;
}

}  // namespace

std::vector<CapcomSnesInstrumentInfo> parseCapcomSnesInstrumentInfos(ByteReader reader, u32 instrumentTableAddress,
                                                                     u32 spcDirAddress) {
  // Instrument table length is inferred, not explicitly stored. Blank slots are skipped,
  // but the first impossible nonblank entry terminates discovery like legacy scanning.
  std::vector<CapcomSnesInstrumentInfo> instruments;
  const SnesSampleDirectory directory(reader, spcDirAddress);

  for (u32 instrumentIndex = 0; instrumentIndex <= 0xff; ++instrumentIndex) {
    const u32 address = instrumentTableAddress + instrumentIndex * 6;
    if (!reader.has(address, 6)) {
      break;
    }

    RecordReader row(reader, address, address + 6);
    const auto srcn = row.u8("srcn", SourceValueDisplay::Hex);
    const auto adsr1 = row.u8("adsr1", SourceValueDisplay::Hex);
    const auto adsr2 = row.u8("adsr2", SourceValueDisplay::Hex);
    const auto gain = row.u8("gain", SourceValueDisplay::Hex);
    const auto pitchScale = row.s16be("pitch_scale");
    const bool blank = std::ranges::all_of(row.bytes(), [](u8 byte) { return byte == 0 || byte == 0xff; });
    if (blank) {
      continue;
    }

    const auto sample = directory.entry(*srcn, false);
    if (*srcn >= 0x80 || (*adsr1 == 0 && *gain == 0) || !sample || !sample->loopAddressIsBlockAligned()) {
      // The table is contiguous; the first impossible nonblank header ends discovery.
      break;
    }

    CapcomSnesInstrumentInfo info{
        .index = instrumentIndex,
        .address = address,
        .srcn = *srcn,
        .adsr1 = *adsr1,
        .adsr2 = *adsr2,
        .gain = *gain,
        .pitchScale = *pitchScale,
    };
    info.sourceFields.assign(row.fields().begin(), row.fields().end());
    instruments.push_back(std::move(info));
  }

  return instruments;
}

bool addCapcomSnesSynth(const ScanInput& input, ScanResultBuilder& builder, ScanInstrumentSetRef instrumentSet,
                        ScanSampleCollectionRef sampleCollection, u32 instrumentTableAddress, u32 spcDirAddress,
                        std::string_view displayName) {
  const auto instrumentInfos = parseCapcomSnesInstrumentInfos(input.reader, instrumentTableAddress, spcDirAddress);
  std::vector<u8> referencedSrcns;
  referencedSrcns.reserve(instrumentInfos.size());
  for (const auto& info : instrumentInfos) {
    referencedSrcns.push_back(info.srcn);
  }
  const auto samples = readSnesBrrCatalog(input.reader, spcDirAddress, referencedSrcns);
  if (instrumentInfos.empty() || samples.samples.empty()) {
    return false;
  }

  const u32 rootOffset = instrumentInfos.front().address;
  const u32 rootSize = (instrumentInfos.back().address + 6) - rootOffset;
  const SourceAnnotationId root = builder.sourceMap()
                                      .table("Instrument Table", input.reader.range(rootOffset, rootSize))
                                      .kind("capcom-snes-instrument-table")
                                      .owner(ObjectRefs::asset(instrumentSet.id))
                                      .id();

  std::vector<Instrument> instruments;
  instruments.reserve(instrumentInfos.size());
  for (const auto& info : instrumentInfos) {
    const auto sampleIndex = samples.canonicalIndex(info.srcn);
    if (!sampleIndex) {
      continue;
    }
    const auto pitch = capcomInstrumentPitch(info.pitchScale);

    instruments.push_back(Instrument{
        .identity =
            InstrumentIdentity{
                .domain = std::string(kCapcomSnesInstrumentDomain),
                .key = info.index,
            },
        .name = fmt::format("Instrument {}", info.index),
        .range = input.reader.range(info.address, 6),
        .regions = {Region{
            .sample = builder.sampleRef(sampleCollection, *sampleIndex),
            .range = input.reader.range(info.address, 6),
            .tuning = pitch.aggregate,
            .rootKey = pitch.rootKey,
            .fineTuneCents = pitch.fineTuneCents,
            .envelope = capcomInstrumentEnvelope(info.adsr1, info.adsr2, info.gain),
        }},
        .modulation = kCapcomModulation,
    });
    auto annotation = builder.sourceMap()
                          .row(fmt::format("Instrument {}", info.index), input.reader.range(info.address, 6))
                          .role(SourceRole::Instrument)
                          .kind("capcom-snes-instrument")
                          .owner(ObjectRefs::instrument(instrumentSet.id, info.index))
                          .derived("instrument", info.index)
                          .fields(info.sourceFields)
                          .parent(root);
    annotation.link(SourceLinkRole::UsesSample, SourceTarget{ObjectRefs::sample(sampleCollection.id, *sampleIndex)});
    auto envelopeAnnotation =
        builder.sourceMap()
            .annotation(SourceRole::DataBlock, "ADSR/Gain", input.reader.range(info.address + 1, 3))
            .kind("capcom-snes-adsr-gain")
            .parent(annotation.id())
            .outline(SourceOutlinePolicy::Show);
    for (const auto& field : info.sourceFields) {
      if (field.name == "adsr1" || field.name == "adsr2" || field.name == "gain") {
        envelopeAnnotation.field(field.name, field.range, field.value, field.display);
      }
    }
    builder.sourceMap()
        .annotation(SourceRole::Region, "Region", input.reader.range(info.address, 6))
        .kind("capcom-snes-region")
        .parent(annotation.id())
        .description(fmt::format("Sample {}", *sampleIndex))
        .link(SourceLinkRole::UsesSample, SourceTarget{ObjectRefs::sample(sampleCollection.id, *sampleIndex)});
  }

  builder
      .instrumentSet(instrumentSet, fmt::format("{} Instruments", displayName),
                     input.reader.range(rootOffset, rootSize))
      .instruments(std::move(instruments));
  builder.sampleCollection(sampleCollection, fmt::format("{} Samples", displayName), samples.directoryRange)
      .samples(buildSnesBrrSampleCollection(input.reader, samples, sampleCollection.id, builder.sourceMap()));
  return true;
}

}  // namespace vgmtrans::formats::capcom_snes
