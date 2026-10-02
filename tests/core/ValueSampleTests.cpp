/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "../TestSupport.h"

#include "value/export/synth/SynthExportData.h"
#include "value/formats/Akao/Akao.h"
#include "value/formats/SuzukiPS1/SuzukiPS1.h"
#include "value/synth/SampleDecoder.h"
#include "value/synth/SampleFiltering.h"
#include "value/synth/SnesDsp.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

using namespace vgmtrans::core;

namespace {

constexpr double kPi = 3.14159265358979323846;

[[nodiscard]] double filteredToneGain(SampleFilter filter, double normalizedFrequency) {
  constexpr size_t sampleCount = 4096;
  constexpr double amplitude = 20000.0;
  DecodedSample sample{
      .sampleRate = 32000,
      .pcm = std::vector<s16>(sampleCount),
  };
  for (size_t i = 0; i < sampleCount; ++i) {
    sample.pcm[i] = static_cast<s16>(std::lround(amplitude * std::cos(kPi * normalizedFrequency * i)));
  }

  const auto source = sample.pcm;
  applySampleFilter(sample, filter);

  double inputPower = 0.0;
  double outputPower = 0.0;
  for (size_t i = 64; i < sampleCount; ++i) {
    inputPower += static_cast<double>(source[i]) * source[i];
    outputPower += static_cast<double>(sample.pcm[i]) * sample.pcm[i];
  }
  return std::sqrt(outputPower / inputPower);
}

void snesBrrDecoderProducesPcm() {
  const std::vector<u8> sourceBytes{0x01, 0, 0, 0, 0, 0, 0, 0, 0};
  const Sample sample{
      .name = "zero",
      .codec = AudioCodec::SnesBrr,
      .encodedData = SourceRange{.source = SourceId{0}, .offset = 0, .size = sourceBytes.size()},
      .sampleRate = 32000,
  };

  const auto decoded = decodeSample(sample, sourceBytes);
  expect(decoded.has_value(), "BRR decoder should decode a valid sample");
  expect(decoded->sampleRate == 32000, "decoded sample should preserve sample rate");
  expect(decoded->pcm.size() == 16, "one BRR block should decode to 16 samples");
  expect(std::ranges::all_of(decoded->pcm, [](s16 sample) { return sample == 0; }),
         "zero BRR block should decode to silence");

  const Sample invalidRange = Sample{
      .name = "invalid",
      .codec = AudioCodec::SnesBrr,
      .encodedData = SourceRange{.source = SourceId{0}, .offset = 8, .size = 9},
  };
  expect(!decodeSample(invalidRange, sourceBytes).has_value(), "BRR decoder should reject invalid source ranges");
}

void snesDspNoiseDecoderMatchesHardwareSequence() {
  Sample sample{
      .codec = AudioCodec::SnesDspNoise,
      .sampleRate = kSnesDspSampleRate,
      .loop = Loop{.enabled = true, .length = 4},
      .codecParameter = 31,
  };
  const auto fastest = decodeSample(sample, {});
  sample.codecParameter = 30;
  const auto halfRate = decodeSample(sample, {});

  expect(fastest && fastest->pcm == std::vector<s16>({16384, 8192, 4096, 2048}) && halfRate &&
             halfRate->pcm == std::vector<s16>({-32768, 16384, 16384, 8192}),
         "SNES noise should use the DSP's LFSR values and FLG counter periods");
}

void snesGainEvaluationHandlesLongIntervals() {
  const double large = std::numeric_limits<double>::max();
  for (u8 mode = 4; mode < 8; ++mode) {
    for (u8 rate = 0; rate < 32; ++rate) {
      const auto gain = static_cast<u8>((mode << 5) | rate);
      const s16 expected = rate == 0 ? 0x321 : (mode < 6 ? 0 : 0x7ff);
      expect(snesDspGainEnvelopeValue(gain, 0x321, large) == expected,
             "long finite GAIN intervals must reach the endpoint or retain a stopped counter's value");
    }
  }
  expect(snesDspGainEnvelopeValue(0x9f, 0x7ff, 0.5 / kSnesDspSampleRate) == 0x7ff &&
             snesDspGainEnvelopeValue(0x9f, 0x7ff, 1.0 / kSnesDspSampleRate) == 0x7df,
         "GAIN evaluation must still wait for a complete hardware counter period before stepping");
}

void ndsImaAdpcmDecoderValidatesItsPredictorHeader() {
  Sample sample{
      .name = "adpcm",
      .codec = AudioCodec::NdsImaAdpcm,
      .encodedData = SourceRange{.source = SourceId{0}, .offset = 0, .size = 5},
      .sampleRate = 32768,
  };

  const std::vector<u8> validMaxIndex{0x00, 0x00, 0x58, 0x00, 0x00};
  const auto decoded = decodeSample(sample, validMaxIndex);
  expect(decoded && decoded->pcm == std::vector<s16>({0, 4095, 7819}),
         "NDS IMA ADPCM decoder should accept initial predictor index 88");

  const std::vector<u8> invalidIndex{0x00, 0x00, 0x59, 0x00, 0x00};
  expect(!decodeSample(sample, invalidIndex).has_value(),
         "NDS IMA ADPCM decoder should reject initial predictor indexes outside the step table");
  sample.encodedData.size = 3;
  expect(!decodeSample(sample, validMaxIndex), "the encoded range must contain the whole predictor header");
  sample.encodedData.size = 4;
  const auto predictorOnly = decodeSample(sample, validMaxIndex);
  expect(predictorOnly && predictorOnly->pcm == std::vector<s16>{0},
         "a predictor-only stream should emit its initial PCM value");
}

void pcm16DecoderHonorsExplicitByteOrder() {
  const Sample littleEndian{
      .codec = AudioCodec::PcmS16,
      .encodedData = SourceRange{.source = SourceId{0}, .offset = 0, .size = 4},
  };
  Sample bigEndian = littleEndian;
  bigEndian.bigEndian = true;

  const std::vector<u8> bytes{0x12, 0x34, 0xfe, 0xdc};
  const auto little = decodeSample(littleEndian, bytes);
  const auto big = decodeSample(bigEndian, bytes);
  expect(little && little->pcm == std::vector<s16>({0x3412, -8962}),
         "PCM16 should retain the default little-endian decoding");
  expect(big && big->pcm == std::vector<s16>({0x1234, -292}),
         "PCM16 should honor source-declared big-endian byte order");
}

void sampleFiltersMatchHardwareTone() {
  for (const SampleFilter filter : {SampleFilter::SnesDspLowPass, SampleFilter::PsxSpuLowPass}) {
    DecodedSample constant{
        .sampleRate = 32000,
        .pcm = std::vector<s16>(128, 12345),
    };
    applySampleFilter(constant, filter);
    expect(std::ranges::all_of(constant.pcm, [](s16 value) { return value == 12345; }),
           "hardware response filtering should preserve DC gain");
  }

  const double snesHalfNyquistDb = 20.0 * std::log10(filteredToneGain(SampleFilter::SnesDspLowPass, 0.5));
  const double snesNyquistDb = 20.0 * std::log10(filteredToneGain(SampleFilter::SnesDspLowPass, 1.0));
  expect(std::abs(snesHalfNyquistDb + 3.974) < 0.03,
         "SNES Gaussian response filtering should match the coherent hardware response at half Nyquist");
  expect(std::abs(snesNyquistDb + 17.269) < 0.25,
         "SNES Gaussian response filtering should retain the hardware's residual Nyquist response");

  const double psxHalfNyquistDb = 20.0 * std::log10(filteredToneGain(SampleFilter::PsxSpuLowPass, 0.5));
  const double psxNyquistDb = 20.0 * std::log10(filteredToneGain(SampleFilter::PsxSpuLowPass, 1.0));
  expect(std::abs(psxHalfNyquistDb + 3.257) < 0.03,
         "PlayStation SPU response filtering should match the coherent hardware response at half Nyquist");
  expect(std::abs(psxNyquistDb + 13.833) < 0.2,
         "PlayStation SPU response filtering should retain the hardware's residual Nyquist response");
}

void sampleFilteringWrapsLoopHistory() {
  DecodedSample sample{
      .sampleRate = 32000,
      .pcm = std::vector<s16>(68),
      .loop = Loop{.enabled = true, .start = 4, .length = 64},
  };
  sample.pcm[67] = 10000;

  applySampleFilter(sample, SampleFilter::SnesDspLowPass);

  expect(std::abs(sample.pcm[4] - 4168) <= 1,
         "the first filtered loop frame should use history from the end of the loop");
}

void synthSampleFilteringHonorsPolicyAndFormat() {
  std::vector<u8> encoded;
  for (size_t i = 0; i < 64; ++i) {
    const u16 value = static_cast<u16>(i % 2 == 0 ? 12000 : -12000);
    encoded.push_back(static_cast<u8>(value));
    encoded.push_back(static_cast<u8>(value >> 8));
  }

  SourceStore sources;
  const SourceId source = sources.add(SourceFile{.name = "alternating.pcm"}, std::move(encoded));
  SamplePoolAsset collection{
      .metadata = AssetMetadata{.id = AssetId{1}, .format = "PsxProbe", .name = "Samples"},
      .pool =
          SamplePool{
              .samples = {Sample{
                  .codec = AudioCodec::PcmS16,
                  .encodedData = SourceRange{.source = source, .offset = 0, .size = 128},
                  .sampleRate = 32000,
              }},
          },
  };
  const std::array<const SamplePoolAsset*, 1> collections{&collection};
  const auto decoded = decodeSample(collection.pool.samples.front(), sources.bytes(source));
  const auto unfiltered = prepareSynthData(
      SynthExportInput{.samplePools = collections, .sampleFiltering = SampleFilteringPolicy::None}, sources);
  const auto snesFiltered = prepareSynthData(
      SynthExportInput{.samplePools = collections, .sampleFiltering = SampleFilteringPolicy::SnesDspLowPass}, sources);
  const auto psxFiltered = prepareSynthData(
      SynthExportInput{.samplePools = collections, .sampleFiltering = SampleFilteringPolicy::PsxSpuLowPass}, sources);

  expect(decoded && unfiltered.samples.size() == 1 && unfiltered.samples.front().decoded.pcm == decoded->pcm,
         "disabling sample filtering should retain decoded PCM");
  expect(snesFiltered.samples.size() == 1 && psxFiltered.samples.size() == 1 &&
             snesFiltered.samples.front().decoded.pcm != decoded->pcm &&
             psxFiltered.samples.front().decoded.pcm != decoded->pcm,
         "explicit hardware low-pass filters should process samples regardless of codec");
  expect(snesFiltered.samples.front().decoded.pcm != psxFiltered.samples.front().decoded.pcm,
         "SNES and PlayStation filtering should retain their distinct hardware responses");

  collection.pool.preferredFilter = SampleFilter::PsxSpuLowPass;
  const auto automatic = prepareSynthData(
      SynthExportInput{
          .samplePools = collections,
          .sampleFiltering = SampleFilteringPolicy::FormatPreferred,
      },
      sources);
  expect(
      automatic.samples.size() == 1 && automatic.samples.front().decoded.pcm == psxFiltered.samples.front().decoded.pcm,
      "automatic sample filtering should use the owning format's recommendation");

  collection.pool.samples.front() = Sample{
      .codec = AudioCodec::SnesDspNoise,
      .encodedData = SourceRange{.source = source},
      .sampleRate = kSnesDspSampleRate,
      .loop = Loop{.enabled = true, .length = 16},
      .codecParameter = 31,
  };
  const auto noise = prepareSynthData(
      SynthExportInput{.samplePools = collections, .sampleFiltering = SampleFilteringPolicy::None}, sources);
  const auto requestedNoiseFilter = prepareSynthData(
      SynthExportInput{.samplePools = collections, .sampleFiltering = SampleFilteringPolicy::SnesDspLowPass}, sources);
  expect(noise.samples.front().decoded.pcm == requestedNoiseFilter.samples.front().decoded.pcm,
         "DSP noise should bypass sample-response filtering even when explicitly requested");
}

void psxFormatsPreferSpuFiltering() {
  const auto akao = vgmtrans::formats::akao::akaoModule();
  const auto suzuki = vgmtrans::formats::suzuki_ps1::suzukiPs1Module();
  expect(akao.preferredSampleFilter == SampleFilter::PsxSpuLowPass &&
             suzuki.preferredSampleFilter == SampleFilter::PsxSpuLowPass,
         "PlayStation formats should recommend the SPU response filter");
}

}  // namespace

void runValueSampleTests() {
  snesBrrDecoderProducesPcm();
  snesDspNoiseDecoderMatchesHardwareSequence();
  snesGainEvaluationHandlesLongIntervals();
  ndsImaAdpcmDecoderValidatesItsPredictorHeader();
  pcm16DecoderHonorsExplicitByteOrder();
  sampleFiltersMatchHardwareTone();
  sampleFilteringWrapsLoopHistory();
  synthSampleFilteringHonorsPolicyAndFormat();
  psxFormatsPreferSpuFiltering();
}
