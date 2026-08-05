/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "ValueTestSupport.h"

#include "value/synth/SnesGaussianFilter.h"

#include <cmath>

namespace {

constexpr double kPi = 3.14159265358979323846;

[[nodiscard]] double filteredToneGain(double normalizedFrequency) {
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
  applySnesGaussianResponseFilter(sample);

  double inputPower = 0.0;
  double outputPower = 0.0;
  for (size_t i = 64; i < sampleCount; ++i) {
    inputPower += static_cast<double>(source[i]) * source[i];
    outputPower += static_cast<double>(sample.pcm[i]) * sample.pcm[i];
  }
  return std::sqrt(outputPower / inputPower);
}

void snesGaussianFilterMatchesHardwareTone() {
  DecodedSample constant{
      .sampleRate = 32000,
      .pcm = std::vector<s16>(128, 12345),
  };
  applySnesGaussianResponseFilter(constant);
  expect(std::ranges::all_of(constant.pcm, [](s16 value) { return value == 12345; }),
         "SNES Gaussian response filtering should preserve DC gain");

  const double halfNyquistDb = 20.0 * std::log10(filteredToneGain(0.5));
  const double nyquistDb = 20.0 * std::log10(filteredToneGain(1.0));
  expect(std::abs(halfNyquistDb + 3.974) < 0.03,
         "SNES Gaussian response filtering should match the coherent hardware response at half Nyquist");
  expect(std::abs(nyquistDb + 17.269) < 0.25,
         "SNES Gaussian response filtering should retain the hardware's residual Nyquist response");
}

void snesGaussianFilterWrapsLoopHistory() {
  DecodedSample sample{
      .sampleRate = 32000,
      .pcm = std::vector<s16>(68),
      .loop = Loop{.enabled = true, .start = 4, .length = 64},
  };
  sample.pcm[67] = 10000;

  applySnesGaussianResponseFilter(sample);

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
      .metadata = AssetMetadata{.id = AssetId{1}, .format = "SnesProbe", .name = "Samples"},
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
  const auto filtered = prepareSynthData(
      SynthExportInput{.sampleCollections = collections, .sampleFiltering = SampleFilteringPolicy::SnesDspLowPass},
      sources);

  expect(decoded && unfiltered.samples.size() == 1 && unfiltered.samples.front().decoded.pcm == decoded->pcm,
         "disabling sample filtering should retain decoded PCM");
  expect(filtered.samples.size() == 1 && filtered.samples.front().decoded.pcm != decoded->pcm,
         "an explicit SNES low-pass should filter samples regardless of codec");

  FormatRegistry formats;
  formats.add(testFormat(FormatModule{
      .name = "SnesProbe",
      .preferredSampleFilter = SampleFilter::SnesDspLowPass,
      .scan = scanProbeSequence,
  }));
  const auto automatic = prepareSynthData(
      SynthExportInput{
          .sampleCollections = collections,
          .formats = &formats,
          .sampleFiltering = SampleFilteringPolicy::FormatPreferred,
      },
      sources);
  expect(automatic.samples.size() == 1 && automatic.samples.front().decoded.pcm == filtered.samples.front().decoded.pcm,
         "automatic sample filtering should use the owning format's recommendation");
}

}  // namespace

void runSnesGaussianFilterTests() {
  snesGaussianFilterMatchesHardwareTone();
  snesGaussianFilterWrapsLoopHistory();
  synthSampleFilteringHonorsPolicyAndFormat();
}
