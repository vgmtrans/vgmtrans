/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/formats/CapcomSnes/CapcomSnesSynth.h"

#include "value/synth/SnesDsp.h"
#include "value/synth/SynthMath.h"
#include "formats/CapcomSnes/CapcomSnesConstants.h"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <string>

namespace vgmtrans::formats::capcom_snes {

using namespace core;

namespace {

struct InstrumentPitch {
  Tuning aggregate;
  u8 rootKey = 96;
  s16 fineTuneCents = 0;
};

[[nodiscard]] u32 sampleLength(ByteReader reader, u32 startAddress, bool& loop) {
  u32 offset = startAddress;
  while (true) {
    if (!reader.has(offset, 9)) {
      return 0;
    }

    const u8 flag = reader.u8At(offset);
    offset += 9;
    if ((flag & 1) != 0) {
      // BRR end blocks carry the loop flag in bit 1 of the same header byte.
      loop = (flag & 2) != 0;
      break;
    }
  }
  return offset - startAddress;
}

[[nodiscard]] bool sampleDirIsValid(ByteReader reader, u32 dirEntryAddress, bool validateSample) {
  if (!reader.has(dirEntryAddress, 4)) {
    return false;
  }

  const u16 sampleStart = reader.le16(dirEntryAddress);
  const u16 sampleLoop = reader.le16(dirEntryAddress + 2);
  if (sampleLoop < sampleStart || !reader.has(sampleStart, 10)) {
    return false;
  }

  if (validateSample) {
    // Fast table probing can skip BRR walking; committed instruments validate the encoded stream.
    bool loops = false;
    const u32 length = sampleLength(reader, sampleStart, loops);
    if (length == 0) {
      return false;
    }
    if (loops && sampleLoop >= sampleStart + length) {
      return false;
    }
  }

  return true;
}

[[nodiscard]] bool blankInstrumentSlot(ByteReader reader, u32 address) {
  if (!reader.has(address, 6)) {
    return false;
  }

  for (u32 offset = address; offset < address + 6; ++offset) {
    if (reader.u8At(offset) != 0 && reader.u8At(offset) != 0xff) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool instrumentHeaderIsValid(ByteReader reader, u32 address, u32 spcDirAddress, bool validateSample) {
  // A plausible instrument header must reference a valid SRCN and carry usable ADSR/gain
  // data. Full validation additionally walks the BRR stream.
  if (!reader.has(address, 6)) {
    return false;
  }

  const u8 srcn = reader.u8At(address);
  const u8 adsr1 = reader.u8At(address + 1);
  const u8 gain = reader.u8At(address + 3);
  if (srcn >= 0x80 || (adsr1 == 0 && gain == 0)) {
    return false;
  }

  const u32 dirEntryAddress = spcDirAddress + srcn * 4;
  if (!sampleDirIsValid(reader, dirEntryAddress, validateSample)) {
    return false;
  }

  const u16 sampleStart = reader.le16(dirEntryAddress);
  const u16 sampleLoop = reader.le16(dirEntryAddress + 2);
  return sampleStart <= sampleLoop && ((sampleLoop - sampleStart) % 9) == 0;
}

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
          .amount = synthAmountFromHertz(::capcom_snes::kVibratoBaseHz),
      },
      SynthGenerator{
          .destination = SynthDestination::TremoloRate,
          .amount = synthAmountFromHertz(::capcom_snes::kTremoloBaseHz),
      },
  };
}

[[nodiscard]] std::vector<SynthModulator> capcomInstrumentModulators() {
  // Capcom drives vibrato/tremolo from sequence controllers. These default modulators
  // describe the maximum synth response; export-time scaling can narrow it to observed use.
  const s32 vibratoRange = synthAmountFromHertzRange(::capcom_snes::kVibratoBaseHz, ::capcom_snes::kVibratoMaxHz);
  const s32 tremoloRange = synthAmountFromHertzRange(::capcom_snes::kTremoloBaseHz, ::capcom_snes::kTremoloMaxHz);

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
          .amount = static_cast<s32>(::capcom_snes::kTremoloHalfDepthCentibels),
      },
      SynthModulator{
          .destination = SynthDestination::VolumeAttenuation,
          .amount = static_cast<s32>(::capcom_snes::kTremoloHalfDepthCentibels),
      },
  };
}

}  // namespace

std::vector<CapcomSnesInstrumentInfo> parseCapcomSnesInstrumentInfos(ByteReader reader, u32 instrumentTableAddress,
                                                                     u32 spcDirAddress) {
  // Instrument table length is inferred, not explicitly stored. Blank slots are skipped,
  // but the first impossible nonblank entry terminates discovery like legacy scanning.
  std::vector<CapcomSnesInstrumentInfo> instruments;

  for (u32 instrumentIndex = 0; instrumentIndex <= 0xff; ++instrumentIndex) {
    const u32 address = instrumentTableAddress + instrumentIndex * 6;
    if (!reader.has(address, 6)) {
      break;
    }

    if (blankInstrumentSlot(reader, address)) {
      continue;
    }
    if (!instrumentHeaderIsValid(reader, address, spcDirAddress, false)) {
      // The table is contiguous; the first impossible nonblank header ends discovery.
      break;
    }
    if (!instrumentHeaderIsValid(reader, address, spcDirAddress, true)) {
      continue;
    }

    instruments.push_back(CapcomSnesInstrumentInfo{
        .index = instrumentIndex,
        .address = address,
        .srcn = reader.u8At(address),
        .adsr1 = reader.u8At(address + 1),
        .adsr2 = reader.u8At(address + 2),
        .gain = reader.u8At(address + 3),
        .pitchScale = static_cast<s16>(reader.be16(address + 4)),
    });
  }

  return instruments;
}

std::vector<CapcomSnesSampleInfo> parseCapcomSnesSampleInfos(ByteReader reader, u32 spcDirAddress,
                                                             const std::vector<CapcomSnesInstrumentInfo>& instruments) {
  std::vector<u8> srcns;
  srcns.reserve(instruments.size());
  for (const auto& instrument : instruments) {
    if (std::ranges::find(srcns, instrument.srcn) == srcns.end()) {
      srcns.push_back(instrument.srcn);
    }
  }
  std::ranges::sort(srcns);

  // Multiple instruments can point at the same SRCN; samples are emitted once.
  std::vector<CapcomSnesSampleInfo> samples;
  samples.reserve(srcns.size());
  for (const u8 srcn : srcns) {
    const u32 dirEntryAddress = spcDirAddress + srcn * 4;
    if (!sampleDirIsValid(reader, dirEntryAddress, true)) {
      continue;
    }

    const u16 start = reader.le16(dirEntryAddress);
    const u16 loop = reader.le16(dirEntryAddress + 2);
    bool loops = false;
    const u32 length = sampleLength(reader, start, loops);
    samples.push_back(CapcomSnesSampleInfo{
        .srcn = srcn,
        .dirEntryAddress = dirEntryAddress,
        .startAddress = start,
        .loopAddress = loop,
        .encodedLength = length,
        .loops = loops,
    });
  }

  return samples;
}

SampleCollectionAsset parseCapcomSnesSamples(const ScanInput& input, AssetId sampleCollectionId,
                                             const std::vector<CapcomSnesSampleInfo>& sampleInfos,
                                             std::string_view displayName) {
  ItemTree items;
  u32 rootOffset = 0;
  u32 rootSize = 0;
  if (!sampleInfos.empty()) {
    rootOffset = sampleInfos.front().dirEntryAddress;
    const u32 lastEnd = sampleInfos.back().dirEntryAddress + 4;
    rootSize = lastEnd - rootOffset;
  }

  ItemTreeBuilder itemBuilder(items, input.ids);
  const auto root = itemBuilder.add(std::nullopt, ItemKind::SampleCollection, "snes-sample-dir", "Sample DIR",
                                    input.reader.range(rootOffset, rootSize));

  SampleCollection collection;
  collection.samples.reserve(sampleInfos.size());
  for (const auto& sampleInfo : sampleInfos) {
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

    static_cast<void>(itemBuilder.add(root, ItemKind::Sample, "snes-brr-sample",
                                      fmt::format("Sample {}", static_cast<unsigned>(sampleInfo.srcn)),
                                      input.reader.range(sampleInfo.startAddress, sampleInfo.encodedLength),
                                      fmt::format("DIR entry ${:04X}", sampleInfo.dirEntryAddress)));
  }

  return SampleCollectionAsset{
      .metadata =
          AssetMetadata{
              .id = sampleCollectionId,
              .format = "CapcomSnes",
              .name = fmt::format("{} Samples", displayName),
              .range = input.reader.range(rootOffset, rootSize),
              .items = std::move(items),
          },
      .samples = std::move(collection),
  };
}

InstrumentSetAsset parseCapcomSnesInstrumentSet(const ScanInput& input, AssetId instrumentSetId,
                                                AssetId sampleCollectionId,
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

  ItemTree items;
  u32 rootOffset = instrumentInfos.empty() ? 0 : instrumentInfos.front().address;
  u32 rootSize = instrumentInfos.empty() ? 0 : (instrumentInfos.back().address + 6) - rootOffset;
  ItemTreeBuilder itemBuilder(items, input.ids);
  const auto root = itemBuilder.add(std::nullopt, ItemKind::InstrumentSet, "capcom-snes-instrument-table",
                                    "Instrument Table", input.reader.range(rootOffset, rootSize));

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
        .sample =
            SampleRef{
                .collection = sampleCollectionId,
                .index = sampleIndex->second,
            },
        .range = input.reader.range(info.address, 6),
        .tuning = pitch.aggregate,
        .rootKey = pitch.rootKey,
        .fineTuneCents = pitch.fineTuneCents,
        .envelope = capcomInstrumentEnvelope(info.adsr1, info.adsr2, info.gain),
    });
    instrument.generators = capcomInstrumentGenerators();
    instrument.modulators = capcomInstrumentModulators();

    instruments.push_back(std::move(instrument));
    const auto instrumentItem =
        itemBuilder.add(root, ItemKind::Instrument, "capcom-snes-instrument", fmt::format("Instrument {}", info.index),
                        input.reader.range(info.address, 6), fmt::format("SRCN {}", static_cast<unsigned>(info.srcn)));
    static_cast<void>(itemBuilder.add(instrumentItem, ItemKind::Region, "capcom-snes-region", "Region",
                                      input.reader.range(info.address, 6),
                                      fmt::format("Sample {}", sampleIndex->second)));
  }

  return InstrumentSetAsset{
      .metadata =
          AssetMetadata{
              .id = instrumentSetId,
              .format = "CapcomSnes",
              .name = fmt::format("{} Instruments", displayName),
              .range = input.reader.range(rootOffset, rootSize),
              .items = std::move(items),
          },
      .instruments = std::move(instruments),
  };
}

}  // namespace vgmtrans::formats::capcom_snes
