/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/base/Source.h"
#include "value/synth/PsxAdpcm.h"
#include "value/synth/PsxSpu.h"
#include "value/synth/SynthBuilder.h"

#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

using namespace vgmtrans::core;

namespace {

void expect(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void psxAdsrRegistersAndEnvelopeAreSharedValues() {
  expect(composePsxAdsr1(1, 0x7f, 0x0f, 0x0f) == 0xffff, "PSX ADSR1 composition should preserve every native field");
  expect(composePsxAdsr2(1, 1, 0x7f, 1, 0x1f) == 0xdfff,
         "PSX ADSR2 composition should preserve every defined native field and leave its reserved bit clear");

  const u16 adsr1 = composePsxAdsr1(0, 0x70, 0x08, 0x08);
  const u16 adsr2 = composePsxAdsr2(0, 1, 0x60, 0, 0x10);
  const Envelope ps1 = psxSpuEnvelope(adsr1, adsr2, PsxSpuGeneration::Ps1);
  const Envelope ps2 = psxSpuEnvelope(adsr1, adsr2, PsxSpuGeneration::Ps2);
  expect(ps1.attackSeconds && ps1.decaySeconds && ps1.secondDecaySeconds && ps1.releaseSeconds && ps1.sustainAmplitude,
         "PSX ADSR conversion should retain precise envelope values");
  expect(*ps1.attackSeconds > 0.0 && *ps1.secondDecaySeconds > 0.0 && *ps1.releaseSeconds > 0.0 &&
             *ps1.sustainAmplitude > 0.0 && *ps1.sustainAmplitude <= 1.0,
         "PSX ADSR conversion should populate canonical physical envelope units");
  expect(ps2.attackSeconds && std::abs((*ps2.attackSeconds / *ps1.attackSeconds) -
                                       (static_cast<double>(kPs1SpuSampleRate) / kPs2SpuSampleRate)) < 1e-9,
         "PS2 ADSR timing should use the SPU2 sample rate without changing the register model");
}

void psxAdpcmInspectionFindsStreamAndLoopBoundaries() {
  SourceStore sources;
  std::vector<u8> bytes(kPsxAdpcmBlockBytes * 3);
  bytes[kPsxAdpcmBlockBytes + 1] = 4;
  bytes[kPsxAdpcmBlockBytes * 2 + 1] = 3;
  const SourceId source = sources.add(SourceFile{.name = "psx-adpcm.probe"}, std::move(bytes));
  const ByteReader reader = sources.reader(source);

  const auto stream = inspectPsxAdpcmStream(reader, 0, static_cast<u32>(reader.size()));
  expect(stream && stream->encodedData == reader.range(0, kPsxAdpcmBlockBytes * 3),
         "PSX ADPCM inspection should retain the exact encoded stream range");
  expect(stream->loop.enabled && stream->loop.start == kPsxAdpcmFramesPerBlock &&
             stream->loop.length == kPsxAdpcmFramesPerBlock * 2,
         "PSX ADPCM loop flags should convert block positions to decoded frames");
  expect(psxAdpcmDecodedFrames(kPsxAdpcmBlockBytes * 3) == kPsxAdpcmFramesPerBlock * 3 &&
             psxAdpcmDecodedOffset(kPsxAdpcmBlockBytes * 2) == kPsxAdpcmFramesPerBlock * 2,
         "PSX ADPCM frame helpers should use the native sixteen-byte block size");
  expect(!inspectPsxAdpcmStream(reader, 0, kPsxAdpcmBlockBytes - 1),
         "PSX ADPCM inspection should reject a range without one complete block");
}

void psxAdpcmCatalogBoundsStreamsAndKeepsSampleReferences() {
  SourceStore sources;
  constexpr u32 sampleBase = 8;
  std::vector<u8> bytes(sampleBase + 52);
  // The first sample has no end flag. The second loops over two blocks;
  // the final offset points to an incomplete block.
  bytes[sampleBase + 16 + 1] = 4;
  bytes[sampleBase + 32 + 1] = 3;
  const SourceId source = sources.add(SourceFile{.name = "psx-bank.probe"}, std::move(bytes));
  const ByteReader reader = sources.reader(source);
  const auto streams = inspectPsxAdpcmStreams(reader, sampleBase, {0, 16, 48}, sampleBase + 52);
  expect(streams.size() == 2 && streams.at(0).encodedData == reader.range(sampleBase, 16) &&
             streams.at(16).encodedData == reader.range(sampleBase + 16, 32),
         "sample offsets should bound unterminated streams and omit incomplete final blocks");
  expect(streams.at(16).loop == Loop{.enabled = true, .start = 0, .length = 56},
         "loop positions should remain relative to each sample");

  for (const bool attachParent : {false, true}) {
    SourceMapBuilder sourceMap;
    const AssetId asset{1};
    SamplePoolBuilder samples(asset, &sourceMap);
    const SourceAnnotationId parent =
        attachParent ? samples.source(SourceRole::SamplePool, "Sample Data", reader.range(sampleBase, 52)).id()
                     : SourceAnnotationId{};
    addPsxAdpcmSamples(samples, streams, kPs2SpuSampleRate, parent);
    const auto first = samples.find(0);
    const auto second = samples.find(16);
    expect(first && first->owner() == asset && first->index() == 0 && second && second->owner() == asset &&
               second->index() == 1 && !samples.find(48),
           "sample references should resolve relative source offsets after rejected streams are omitted");
    const auto built = std::move(samples).finish();
    const auto annotations = sourceMap.finish();
    for (u32 index = 0; index < built.value.samples.size(); ++index) {
      const auto& sample = built.value.samples[index];
      expect(sample.name == "Sample " + std::to_string(index) && sample.codec == AudioCodec::PsxAdpcm &&
                 sample.sampleRate == kPs2SpuSampleRate && sample.channels == 1 &&
                 sample.loop == streams.at(index * 16).loop,
             "catalog samples should preserve stream loops and use the requested hardware rate");
      const auto owned = annotations.ownedBy(ObjectRefs::sample(asset, index));
      expect(owned.size() == 1, "each sample should retain one owned source annotation");
      const auto& annotation = annotations.get(owned.front());
      expect(annotation.range == sample.encodedData && annotation.kind == "psx-adpcm-sample" &&
                 (attachParent ? annotation.parent == parent : !annotation.parent),
             "sample annotations should preserve their payload and optional parent");
    }
  }
}

}  // namespace

void runValuePsxTests() {
  psxAdsrRegistersAndEnvelopeAreSharedValues();
  psxAdpcmInspectionFindsStreamAndLoopBoundaries();
  psxAdpcmCatalogBoundsStreamsAndKeepsSampleReferences();
}
