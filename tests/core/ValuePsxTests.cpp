/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/base/Source.h"
#include "value/synth/PsxAdpcm.h"
#include "value/synth/PsxSpu.h"

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

}  // namespace

void runValuePsxTests() {
  psxAdsrRegistersAndEnvelopeAreSharedValues();
  psxAdpcmInspectionFindsStreamAndLoopBoundaries();
}
