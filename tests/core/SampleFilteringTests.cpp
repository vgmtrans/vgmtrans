/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "ValueTestSupport.h"

#include "value/formats/Akao/Akao.h"
#include "value/formats/SuzukiPS1/SuzukiPS1.h"
#include "value/synth/SampleFiltering.h"

#include <cmath>

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

  const double snesHalfNyquistDb =
      20.0 * std::log10(filteredToneGain(SampleFilter::SnesDspLowPass, 0.5));
  const double snesNyquistDb = 20.0 * std::log10(filteredToneGain(SampleFilter::SnesDspLowPass, 1.0));
  expect(std::abs(snesHalfNyquistDb + 3.974) < 0.03,
         "SNES Gaussian response filtering should match the coherent hardware response at half Nyquist");
  expect(std::abs(snesNyquistDb + 17.269) < 0.25,
         "SNES Gaussian response filtering should retain the hardware's residual Nyquist response");

  const double psxHalfNyquistDb =
      20.0 * std::log10(filteredToneGain(SampleFilter::PsxSpuLowPass, 0.5));
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
  SampleCollectionAsset collection{
      .metadata = AssetMetadata{.id = AssetId{1}, .format = "PsxProbe", .name = "Samples"},
      .samples =
          SampleCollection{
              .samples = {Sample{
                  .codec = AudioCodec::PcmS16,
                  .encodedData = SourceRange{.source = source, .offset = 0, .size = 128},
                  .sampleRate = 32000,
              }},
          },
  };
  const std::array<const SampleCollectionAsset*, 1> collections{&collection};
  const auto decoded = decodeSample(collection.samples.samples.front(), sources.bytes(source));
  const auto unfiltered = prepareSynthData(
      SynthExportInput{.sampleCollections = collections, .sampleFiltering = SampleFilteringPolicy::None}, sources);
  const auto snesFiltered = prepareSynthData(
      SynthExportInput{.sampleCollections = collections, .sampleFiltering = SampleFilteringPolicy::SnesDspLowPass},
      sources);
  const auto psxFiltered = prepareSynthData(
      SynthExportInput{.sampleCollections = collections, .sampleFiltering = SampleFilteringPolicy::PsxSpuLowPass},
      sources);

  expect(decoded && unfiltered.samples.size() == 1 && unfiltered.samples.front().decoded.pcm == decoded->pcm,
         "disabling sample filtering should retain decoded PCM");
  expect(snesFiltered.samples.size() == 1 && psxFiltered.samples.size() == 1 &&
             snesFiltered.samples.front().decoded.pcm != decoded->pcm &&
             psxFiltered.samples.front().decoded.pcm != decoded->pcm,
         "explicit hardware low-pass filters should process samples regardless of codec");
  expect(snesFiltered.samples.front().decoded.pcm != psxFiltered.samples.front().decoded.pcm,
         "SNES and PlayStation filtering should retain their distinct hardware responses");

  FormatRegistry formats;
  formats.add(FormatModule{
      .name = "PsxProbe",
      .preferredSampleFilter = SampleFilter::PsxSpuLowPass,
      .scan = scanProbeSequence,
  });
  const auto automatic = prepareSynthData(
      SynthExportInput{
          .sampleCollections = collections,
          .formats = &formats,
          .sampleFiltering = SampleFilteringPolicy::FormatPreferred,
      },
      sources);
  expect(automatic.samples.size() == 1 &&
             automatic.samples.front().decoded.pcm == psxFiltered.samples.front().decoded.pcm,
         "automatic sample filtering should use the owning format's recommendation");
}

void psxFormatsPreferSpuFiltering() {
  const auto akao = vgmtrans::formats::akao::akaoModule();
  const auto suzuki = vgmtrans::formats::suzuki_ps1::suzukiPs1Module();
  expect(akao.preferredSampleFilter == SampleFilter::PsxSpuLowPass &&
             suzuki.preferredSampleFilter == SampleFilter::PsxSpuLowPass,
         "PlayStation formats should recommend the SPU response filter");
}

}  // namespace

void runSampleFilteringTests() {
  sampleFiltersMatchHardwareTone();
  sampleFilteringWrapsLoopHistory();
  synthSampleFilteringHonorsPolicyAndFormat();
  psxFormatsPreferSpuFiltering();
}
