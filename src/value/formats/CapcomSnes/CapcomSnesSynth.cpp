/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/CapcomSnes/CapcomSnes.h"

#include "value/base/RecordReader.h"
#include "value/platform/SnesSampleDirectory.h"
#include "value/synth/SnesDsp.h"
#include "value/synth/SynthMath.h"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <string>

namespace vgmtrans::formats::capcom_snes {

using namespace core;

namespace {

constexpr double kLfoStepHertz = 1000.0 / 16384.0;
constexpr double kVibratoBaseHertz = kLfoStepHertz;
constexpr double kVibratoMaxHertz = 255.0 * kLfoStepHertz;
constexpr double kTremoloBaseHertz = 2.0 * kLfoStepHertz;
constexpr double kTremoloMaxHertz = 510.0 * kLfoStepHertz;
constexpr s32 kTremoloHalfDepthCentibels = 484;

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

[[nodiscard]] std::vector<SynthGenerator> capcomInstrumentGenerators() {
  return {
      SynthGenerator{
          .destination = SynthDestination::VibratoRate,
          .amount = synthAmountFromHertz(kVibratoBaseHertz),
      },
      SynthGenerator{
          .destination = SynthDestination::TremoloRate,
          .amount = synthAmountFromHertz(kTremoloBaseHertz),
      },
  };
}

[[nodiscard]] std::vector<SynthModulator> capcomInstrumentModulators() {
  // Capcom drives vibrato/tremolo from sequence controllers. These default modulators
  // describe the maximum synth response; export-time scaling can narrow it to observed use.
  const s32 vibratoRange = synthAmountFromHertzRange(kVibratoBaseHertz, kVibratoMaxHertz);
  const s32 tremoloRange = synthAmountFromHertzRange(kTremoloBaseHertz, kTremoloMaxHertz);

  return {
      SynthModulator{
          .source = SynthSource::ChannelPressure,
          .destination = SynthDestination::VibratoDepth,
          .amount = 0,
      },
      SynthModulator{
          .destination = SynthDestination::VibratoDepth,
          .amount = 1200,
      },
      SynthModulator{
          .destination = SynthDestination::VibratoRate,
          .amount = vibratoRange,
      },
      SynthModulator{
          .destination = SynthDestination::TremoloRate,
          .amount = tremoloRange,
      },
      SynthModulator{
          .destination = SynthDestination::TremoloDepth,
          .amount = kTremoloHalfDepthCentibels,
      },
      SynthModulator{
          .destination = SynthDestination::VolumeAttenuation,
          .amount = kTremoloHalfDepthCentibels,
      },
  };
}

}  // namespace

std::vector<CapcomSnesInstrumentInfo> parseCapcomSnesInstrumentInfos(ByteReader reader, u32 instrumentTableAddress,
                                                                     u32 spcDirAddress) {
  // Instrument table length is inferred, not explicitly stored. Blank slots are skipped,
  // but the first impossible nonblank entry terminates discovery like legacy scanning.
  std::vector<CapcomSnesInstrumentInfo> instruments;
  const SnesSampleDirectory directory(reader, spcDirAddress);
  std::map<u8, std::optional<SnesSampleDirectoryEntry>> directoryEntries;

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

    auto [sampleEntry, inserted] = directoryEntries.try_emplace(*srcn);
    if (inserted) {
      sampleEntry->second = directory.entry(*srcn, false);
    }
    const auto& sample = sampleEntry->second;
    if (!row.ok() || *srcn >= 0x80 || (*adsr1 == 0 && *gain == 0) || !sample || !sample->loopAddressIsBlockAligned()) {
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
        .dirEntryAddress = static_cast<u32>(sample->entryRange.offset),
        .sampleStartAddress = sample->startAddress,
        .sampleLoopAddress = sample->loopAddress,
    };
    info.sourceFields.assign(row.fields().begin(), row.fields().end());
    instruments.push_back(std::move(info));
  }

  return instruments;
}

std::vector<CapcomSnesSampleInfo> parseCapcomSnesSampleInfos(ByteReader reader,
                                                             const std::vector<CapcomSnesInstrumentInfo>& instruments) {
  std::vector<const CapcomSnesInstrumentInfo*> representatives;
  representatives.reserve(instruments.size());
  for (const auto& instrument : instruments) {
    const auto duplicate =
        std::ranges::find_if(representatives, [&](const auto* existing) { return existing->srcn == instrument.srcn; });
    if (duplicate == representatives.end()) {
      representatives.push_back(&instrument);
    }
  }
  std::ranges::sort(representatives, {}, [](const auto* instrument) { return instrument->srcn; });

  // Multiple instruments can point at the same SRCN; samples are emitted once.
  std::vector<CapcomSnesSampleInfo> samples;
  samples.reserve(representatives.size());
  for (const auto* instrument : representatives) {
    const auto stream = inspectSnesBrrStream(reader, instrument->sampleStartAddress);
    if (!stream || (stream->loops && instrument->sampleLoopAddress >= stream->encodedData.endOffset())) {
      continue;
    }
    samples.push_back(CapcomSnesSampleInfo{
        .srcn = instrument->srcn,
        .dirEntryAddress = instrument->dirEntryAddress,
        .startAddress = instrument->sampleStartAddress,
        .loopAddress = instrument->sampleLoopAddress,
        .encodedLength = static_cast<u32>(stream->encodedData.size),
        .loops = stream->loops,
    });
  }

  return samples;
}

SampleCollectionAsset parseCapcomSnesSamples(const ScanInput& input, AssetId sampleCollectionId,
                                             const std::vector<CapcomSnesSampleInfo>& sampleInfos,
                                             std::string_view displayName, SourceMapBuilder* sourceMap) {
  u32 rootOffset = 0;
  u32 rootSize = 0;
  if (!sampleInfos.empty()) {
    rootOffset = sampleInfos.front().dirEntryAddress;
    const u32 lastEnd = sampleInfos.back().dirEntryAddress + 4;
    rootSize = lastEnd - rootOffset;
  }

  SourceAnnotationId root;
  if (sourceMap != nullptr) {
    root = sourceMap->table("Sample DIR", input.reader.range(rootOffset, rootSize))
               .kind("snes-sample-dir")
               .owner(ObjectRefs::asset(sampleCollectionId))
               .id();
  }

  SampleCollection collection;
  collection.samples.reserve(sampleInfos.size());
  for (u32 sampleIndex = 0; sampleIndex < sampleInfos.size(); ++sampleIndex) {
    const auto& sampleInfo = sampleInfos[sampleIndex];
    // SNES BRR decodes 9-byte blocks into 16 PCM frames.
    const u32 loopStart = sampleInfo.loopAddress >= sampleInfo.startAddress
                              ? ((sampleInfo.loopAddress - sampleInfo.startAddress) / 9) * 16
                              : 0;
    const u32 decodedLength = (sampleInfo.encodedLength / 9) * 16;
    const u32 lastBlockAddress = sampleInfo.encodedLength >= 9 ? sampleInfo.startAddress + sampleInfo.encodedLength - 9
                                                               : sampleInfo.startAddress;
    const bool loopEnabled = sampleInfo.loops && sampleInfo.loopAddress >= sampleInfo.startAddress &&
                             sampleInfo.loopAddress <= lastBlockAddress;
    collection.samples.push_back(Sample{
        .name = fmt::format("Sample {}", static_cast<unsigned>(sampleInfo.srcn)),
        .codec = AudioCodec::SnesBrr,
        .encodedData = input.reader.range(sampleInfo.startAddress, sampleInfo.encodedLength),
        .sampleRate = 32000,
        .channels = 1,
        .bitsPerSample = 16,
        .loop =
            Loop{
                .enabled = loopEnabled,
                .start = loopStart,
                .length = loopEnabled && decodedLength >= loopStart ? decodedLength - loopStart : 0,
            },
    });
    if (sourceMap != nullptr) {
      auto row =
          sourceMap
              ->row(fmt::format("Sample {} DIR Entry", static_cast<unsigned>(sampleInfo.srcn)),
                    input.reader.range(sampleInfo.dirEntryAddress, 4))
              .role(SourceRole::Sample)
              .kind("snes-sample-dir-entry")
              .owner(ObjectRefs::sample(sampleCollectionId, sampleIndex))
              .field("start", input.reader.range(sampleInfo.dirEntryAddress, 2), sampleInfo.startAddress,
                     SourceValueDisplay::Address)
              .field("loop", input.reader.range(sampleInfo.dirEntryAddress + 2, 2), sampleInfo.loopAddress,
                     SourceValueDisplay::Address)
              .link(SourceLinkRole::PointsTo,
                    SourceTarget{input.reader.range(sampleInfo.startAddress, sampleInfo.encodedLength)}, "BRR data");
      if (root.valid()) {
        row.parent(root);
      }
      sourceMap
          ->section(fmt::format("Sample {} BRR Data", static_cast<unsigned>(sampleInfo.srcn)),
                    input.reader.range(sampleInfo.startAddress, sampleInfo.encodedLength))
          .role(SourceRole::Payload)
          .kind("snes-brr-payload")
          .owner(ObjectRefs::sample(sampleCollectionId, sampleIndex))
          .parent(row.id());
    }
  }

  return SampleCollectionAsset{
      .metadata =
          AssetMetadata{
              .id = sampleCollectionId,
              .format = "CapcomSnes",
              .name = fmt::format("{} Samples", displayName),
              .range = input.reader.range(rootOffset, rootSize),
          },
      .samples = std::move(collection),
  };
}

InstrumentSetAsset parseCapcomSnesInstrumentSet(const ScanInput& input, ScanResultBuilder& builder,
                                                AssetId instrumentSetId, ScanSampleCollectionRef sampleCollection,
                                                const std::vector<CapcomSnesInstrumentInfo>& instrumentInfos,
                                                const std::vector<CapcomSnesSampleInfo>& sampleInfos,
                                                std::string_view displayName) {
  // Instruments refer to samples by SRCN, while exported regions need flat sample indexes.
  // Build both SRCN and start-address maps so duplicate BRR data stays canonical.
  std::map<u32, u32> sampleIndexByStartAddress;
  std::map<u8, u32> sampleIndexBySrcn;
  for (u32 index = 0; index < sampleInfos.size(); ++index) {
    // Canonicalize duplicate sample starts so shared BRR data exports as one sample.
    const auto [canonical, _] = sampleIndexByStartAddress.emplace(sampleInfos[index].startAddress, index);
    sampleIndexBySrcn[sampleInfos[index].srcn] = canonical->second;
  }

  u32 rootOffset = instrumentInfos.empty() ? 0 : instrumentInfos.front().address;
  u32 rootSize = instrumentInfos.empty() ? 0 : (instrumentInfos.back().address + 6) - rootOffset;
  const SourceAnnotationId root = builder.sourceMap()
                                      .table("Instrument Table", input.reader.range(rootOffset, rootSize))
                                      .kind("capcom-snes-instrument-table")
                                      .owner(ObjectRefs::asset(instrumentSetId))
                                      .id();

  std::vector<Instrument> instruments;
  instruments.reserve(instrumentInfos.size());
  for (const auto& info : instrumentInfos) {
    const auto sampleIndex = sampleIndexBySrcn.find(info.srcn);
    if (sampleIndex == sampleIndexBySrcn.end()) {
      continue;
    }
    const auto pitch = capcomInstrumentPitch(info.pitchScale);

    Instrument instrument{
        .bank = info.index >> 7,
        .program = info.index & 0x7f,
        .name = fmt::format("Instrument {}", info.index),
        .range = input.reader.range(info.address, 6),
    };
    instrument.regions.push_back(Region{
        .sample = builder.sampleRef(sampleCollection, sampleIndex->second),
        .range = input.reader.range(info.address, 6),
        .tuning = pitch.aggregate,
        .rootKey = pitch.rootKey,
        .fineTuneCents = pitch.fineTuneCents,
        .envelope = capcomInstrumentEnvelope(info.adsr1, info.adsr2, info.gain),
    });
    instrument.generators = capcomInstrumentGenerators();
    instrument.modulators = capcomInstrumentModulators();

    instruments.push_back(std::move(instrument));
    auto annotation = builder.sourceMap()
                          .row(fmt::format("Instrument {}", info.index), input.reader.range(info.address, 6))
                          .role(SourceRole::Instrument)
                          .kind("capcom-snes-instrument")
                          .owner(ObjectRefs::instrument(instrumentSetId, info.index))
                          .derived("bank", info.index >> 7)
                          .derived("program", info.index & 0x7f);
    for (const auto& field : info.sourceFields) {
      annotation.field(field.name, field.range, field.value, field.display);
    }
    if (root.valid()) {
      annotation.parent(root);
    }
    annotation.link(SourceLinkRole::UsesSample,
                    SourceTarget{ObjectRefs::sample(sampleCollection.id, sampleIndex->second)});
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
        .description(fmt::format("Sample {}", sampleIndex->second))
        .link(SourceLinkRole::UsesSample, SourceTarget{ObjectRefs::sample(sampleCollection.id, sampleIndex->second)});
  }

  return InstrumentSetAsset{
      .metadata =
          AssetMetadata{
              .id = instrumentSetId,
              .format = "CapcomSnes",
              .name = fmt::format("{} Instruments", displayName),
              .range = input.reader.range(rootOffset, rootSize),
          },
      .instruments = std::move(instruments),
  };
}

}  // namespace vgmtrans::formats::capcom_snes
