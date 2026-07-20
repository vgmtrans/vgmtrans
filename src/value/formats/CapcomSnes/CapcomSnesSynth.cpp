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
  Tuning tuning;
  u8 rootKey = 96;
  s16 fineTuneCents = 0;
};

// Converts Capcom's pitch scale into the root key and fine tuning used by the
// shared instrument model.
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
      .tuning = Tuning{.cents = (rootKey - baseUnityKey) * 100 + fine},
      .rootKey = static_cast<u8>(std::clamp(rootKey, 0, 127)),
      .fineTuneCents = static_cast<s16>(fine),
  };
}

// Converts Capcom's ADSR and gain bytes into the shared envelope model.
[[nodiscard]] Envelope capcomInstrumentEnvelope(u8 adsr1, u8 adsr2, u8 gain) {
  Envelope envelope = (adsr1 & 0x80) != 0 ? snesDspEnvelope(adsr1, adsr2, gain) : Envelope{};

  const u8 sustainLevel = adsr2 >> 5;
  const auto releaseStartEnvelopeLevel = static_cast<s16>((sustainLevel << 8) | 0xff);
  const double releaseSeconds = snesDspGainEnvelopeSeconds(gain, releaseStartEnvelopeLevel, 0);
  envelope.release = static_cast<u32>(std::lround(std::max(0.0, releaseSeconds) * 1'000'000.0));
  envelope.releaseSeconds = releaseSeconds;
  return envelope;
}

}  // namespace

// Reads consecutive six-byte instrument rows. Blank slots are skipped, and the
// first unusable nonblank row marks the end of the table.
std::vector<CapcomSnesInstrumentInfo> parseCapcomSnesInstrumentInfos(ByteReader reader, u32 instrumentTableAddress,
                                                                     u32 spcDirAddress) {
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
    // The table is contiguous; the first impossible nonblank header ends discovery.
    if (*srcn >= 0x80 || (*adsr1 == 0 && *gain == 0) || !sample || !sample->loopAddressIsBlockAligned()) {
      break;
    }

    instruments.push_back(CapcomSnesInstrumentInfo{
        .index = instrumentIndex,
        .address = address,
        .srcn = *srcn,
        .adsr1 = *adsr1,
        .adsr2 = *adsr2,
        .gain = *gain,
        .pitchScale = *pitchScale,
        .sourceFields = row.takeFields(),
    });
  }

  return instruments;
}

// Builds Capcom's instruments and samples together, then links every instrument
// region to its matching sample.
bool addCapcomSnesSynth(ScanResultBuilder& builder, ScanInstrumentSetRef instrumentSet,
                        ScanSampleCollectionRef sampleCollection, u32 instrumentTableAddress, u32 spcDirAddress,
                        std::string_view displayName) {
  const ByteReader reader = builder.reader();
  const auto instrumentInfos = parseCapcomSnesInstrumentInfos(reader, instrumentTableAddress, spcDirAddress);
  std::vector<u8> referencedSrcns;
  referencedSrcns.reserve(instrumentInfos.size());
  for (const auto& info : instrumentInfos) {
    referencedSrcns.push_back(info.srcn);
  }
  const auto samples = readSnesBrrCatalog(reader, spcDirAddress, referencedSrcns);
  if (samples.samples.empty()) {
    return false;
  }

  const u32 rootOffset = instrumentInfos.front().address;
  const u32 rootSize = (instrumentInfos.back().address + 6) - rootOffset;
  const SourceAnnotationId root = builder.sourceMap()
                                      .table("Instrument Table", reader.range(rootOffset, rootSize))
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
    const SourceRange range = reader.range(info.address, 6);
    const std::string name = fmt::format("Instrument {}", info.index);
    const SampleRef sampleRef = builder.sampleRef(sampleCollection, *sampleIndex);
    const ObjectRef sampleObject = ObjectRefs::sample(sampleCollection.id, *sampleIndex);

    instruments.push_back(Instrument{
        .identity =
            InstrumentIdentity{
                .domain = std::string(kCapcomSnesInstrumentDomain),
                .key = info.index,
            },
        .name = name,
        .range = range,
        .regions = {Region{
            .sample = sampleRef,
            .range = range,
            .tuning = pitch.tuning,
            .rootKey = pitch.rootKey,
            .fineTuneCents = pitch.fineTuneCents,
            .envelope = capcomInstrumentEnvelope(info.adsr1, info.adsr2, info.gain),
        }},
        .modulation = kCapcomModulation,
    });
    auto annotation = builder.sourceMap()
                          .row(name, range)
                          .role(SourceRole::Instrument)
                          .kind("capcom-snes-instrument")
                          .owner(ObjectRefs::instrument(instrumentSet.id, info.index))
                          .derived("instrument", info.index)
                          .fields(info.sourceFields)
                          .parent(root);
    annotation.link(SourceLinkRole::UsesSample, SourceTarget{sampleObject});
    auto envelopeAnnotation = builder.sourceMap()
                                  .annotation(SourceRole::DataBlock, "ADSR/Gain", reader.range(info.address + 1, 3))
                                  .kind("capcom-snes-adsr-gain")
                                  .parent(annotation.id())
                                  .outline(SourceOutlinePolicy::Show);
    for (const auto& field : info.sourceFields) {
      if (field.name == "adsr1" || field.name == "adsr2" || field.name == "gain") {
        envelopeAnnotation.field(field.name, field.range, field.value, field.display);
      }
    }
    builder.sourceMap()
        .annotation(SourceRole::Region, "Region", range)
        .kind("capcom-snes-region")
        .parent(annotation.id())
        .description(fmt::format("Sample {}", *sampleIndex))
        .link(SourceLinkRole::UsesSample, SourceTarget{sampleObject});
  }

  builder.instrumentSet(instrumentSet, fmt::format("{} Instruments", displayName), reader.range(rootOffset, rootSize))
      .instruments(std::move(instruments));
  builder.sampleCollection(sampleCollection, fmt::format("{} Samples", displayName), samples.directoryRange)
      .samples(buildSnesBrrSampleCollection(reader, samples, sampleCollection.id, builder.sourceMap()));
  return true;
}

}  // namespace vgmtrans::formats::capcom_snes
