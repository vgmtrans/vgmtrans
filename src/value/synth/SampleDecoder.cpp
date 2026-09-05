/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/synth/SampleDecoder.h"

#include "value/synth/PsxAdpcm.h"
#include "value/synth/SnesDsp.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <iterator>
#include <limits>
#include <numeric>
#include <optional>

namespace vgmtrans::core {

namespace {

constexpr unsigned kNdsAdpcmTable[89] = {
    0x0007, 0x0008, 0x0009, 0x000A, 0x000B, 0x000C, 0x000D, 0x000E, 0x0010, 0x0011, 0x0013, 0x0015, 0x0017,
    0x0019, 0x001C, 0x001F, 0x0022, 0x0025, 0x0029, 0x002D, 0x0032, 0x0037, 0x003C, 0x0042, 0x0049, 0x0050,
    0x0058, 0x0061, 0x006B, 0x0076, 0x0082, 0x008F, 0x009D, 0x00AD, 0x00BE, 0x00D1, 0x00E6, 0x00FD, 0x0117,
    0x0133, 0x0151, 0x0173, 0x0198, 0x01C1, 0x01EE, 0x0220, 0x0256, 0x0292, 0x02D4, 0x031C, 0x036C, 0x03C3,
    0x0424, 0x048E, 0x0502, 0x0583, 0x0610, 0x06AB, 0x0756, 0x0812, 0x08E0, 0x09C3, 0x0ABD, 0x0BD0, 0x0CFF,
    0x0E4C, 0x0FBA, 0x114C, 0x1307, 0x14EE, 0x1706, 0x1954, 0x1BDC, 0x1EA5, 0x21B6, 0x2515, 0x28CA, 0x2CDF,
    0x315B, 0x364B, 0x3BB9, 0x41B2, 0x4844, 0x4F7E, 0x5771, 0x602F, 0x69CE, 0x7462, 0x7FFF};

constexpr int kNdsImaIndexTable[9] = {-1, -1, -1, -1, 2, 4, 6, 8};
constexpr std::array<s16, 49> kOkiStepTable = {
    16,  17,  19,  21,  23,  25,  28,  31,  34,  37,  41,   45,   50,   55,   60,   66,  73,
    80,  88,  97,  107, 118, 130, 143, 157, 173, 190, 209,  230,  253,  279,  307,  337, 371,
    408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552,
};
constexpr std::array<s8, 8> kOkiIndexShift = {-1, -1, -1, -1, 2, 4, 6, 8};
constexpr double kPi = 3.14159265358979323846264338327950288;
s32 clipSigned15(s32 x) {
  return (x & 16384) ? (x | ~16383) : (x & 16383);
}

s32 clampSigned16(s32 x) {
  return std::clamp<s32>(x, -32768, 32767);
}

void decodeBrrBlock(std::span<s16, 16> output, u8 header, std::span<const u8, 8> payload, s32& previous1,
                    s32& previous2) {
  // SNES BRR packs sixteen 4-bit deltas per block. The two previous decoded samples are
  // part of the predictor state and must carry across block boundaries.
  const auto range = static_cast<u8>((header & 0xf0) >> 4);
  const auto filter = static_cast<u8>((header & 0x0c) >> 2);
  const bool validHeader = range < 0x0d;

  s32 s1 = previous1;
  s32 s2 = previous2;

  for (size_t i = 0; i < payload.size(); ++i) {
    s8 sample1 = static_cast<s8>(payload[i]);
    s8 sample2 = static_cast<s8>(sample1 << 4);
    sample1 >>= 4;
    sample2 >>= 4;

    for (int nibble = 0; nibble < 2; ++nibble) {
      s32 out = nibble != 0 ? sample2 : sample1;
      out = validHeader ? ((out << range) >> 1) : (out & ~0x7ff);

      switch (filter) {
        case 1:
          out += s1 + ((-s1) >> 4);
          break;
        case 2:
          out += (s1 << 1) + ((-((s1 << 1) + s1)) >> 5) - s2 + (s2 >> 4);
          break;
        case 3:
          out += (s1 << 1) + ((-(s1 + (s1 << 2) + (s1 << 3))) >> 6) - s2 + (((s2 << 1) + s2) >> 4);
          break;
        default:
          break;
      }

      out = clipSigned15(clampSigned16(out));
      s2 = s1;
      s1 = out;
      output[i * 2 + nibble] = static_cast<s16>(out << 1);
    }
  }

  previous1 = s1;
  previous2 = s2;
}

[[nodiscard]] bool rangeIsValid(const Sample& sample, std::span<const u8> sourceBytes) {
  const auto offset = sample.encodedData.offset;
  const auto size = sample.encodedData.size;
  return offset <= sourceBytes.size() && size <= sourceBytes.size() - offset;
}

[[nodiscard]] u16 le16(std::span<const u8> bytes, size_t offset) {
  return static_cast<u16>(bytes[offset] | (bytes[offset + 1] << 8));
}

void processNdsImaNibble(u8 data4Bit, int& index, int& pcm16) {
  // Nintendo DS ADPCM is IMA-style but uses the console's step/index tables and stores
  // the initial PCM/index immediately before the encoded payload.
  int diff = static_cast<int>(kNdsAdpcmTable[index] / 8);
  if ((data4Bit & 1) != 0) {
    diff += static_cast<int>(kNdsAdpcmTable[index] / 4);
  }
  if ((data4Bit & 2) != 0) {
    diff += static_cast<int>(kNdsAdpcmTable[index] / 2);
  }
  if ((data4Bit & 4) != 0) {
    diff += static_cast<int>(kNdsAdpcmTable[index]);
  }

  if ((data4Bit & 8) == 0) {
    pcm16 = pcm16 > 0x7fff - diff ? 0x7fff : pcm16 + diff;
  } else {
    pcm16 = pcm16 < -0x7fff + diff ? -0x7fff : pcm16 - diff;
  }
  index = std::clamp(index + kNdsImaIndexTable[data4Bit & 7], 0, 88);
}

void decodePsxAdpcmBlock(std::span<s16, kPsxAdpcmFramesPerBlock> output, std::span<const u8, kPsxAdpcmBlockBytes> block,
                         s32& previous1, s32& previous2) {
  static constexpr s16 kCoef[5][2] = {
      {0, 0}, {60, 0}, {115, -52}, {98, -55}, {122, -60},
  };

  const u8 shift = std::min<u8>(block[0] & 0x0f, 12);
  const u8 filter = std::min<u8>((block[0] & 0xf0) >> 4, 4);
  const s16 coef0 = kCoef[filter][0];
  const s16 coef1 = kCoef[filter][1];

  s32 s1 = previous1;
  s32 s2 = previous2;
  for (size_t i = 0; i < output.size(); ++i) {
    const u8 byte = block[2 + (i >> 1)];
    const u8 nibble = (i & 1u) == 0 ? (byte & 0x0f) : (byte >> 4);
    const s8 signedNibble = static_cast<s8>(nibble << 4) >> 4;
    s32 sample = (static_cast<s32>(signedNibble) << 12) >> shift;
    sample += ((coef0 * s1 + coef1 * s2) >> 6);
    sample = std::clamp<s32>(sample, -32768, 32767);
    output[i] = static_cast<s16>(sample);
    s2 = s1;
    s1 = sample;
  }

  previous1 = s1;
  previous2 = s2;
}

[[nodiscard]] double ndsPsgDutyCycle(u32 dutyIndex) {
  switch (dutyIndex & 7u) {
    case 7:
      return 0.0;
    case 0:
      return 0.125;
    case 1:
      return 0.25;
    case 2:
      return 0.375;
    case 3:
      return 0.5;
    case 4:
      return 0.625;
    case 5:
      return 0.75;
    case 6:
      return 0.875;
  }
  return 0.5;
}

[[nodiscard]] std::vector<s16> synthesizeLfsrNoisePcm16(u32 sampleCount, u16 lfsrSeed = 0x7fff, u16 lfsrTap = 0x6000,
                                                        s16 amplitude = 0x7fff) {
  // PSG noise is not sample data in ROM. Emit a deterministic loopable waveform so synth
  // exporters have a concrete sample to reference.
  std::vector<s16> samples(sampleCount);
  if (samples.empty()) {
    return samples;
  }

  u16 value = lfsrSeed;
  samples[0] = amplitude;
  for (u32 i = 1; i < sampleCount; ++i) {
    const bool carry = (value & 0x0001) != 0;
    value >>= 1;
    if (carry) {
      samples[i] = static_cast<s16>(-amplitude);
      value ^= lfsrTap;
    } else {
      samples[i] = amplitude;
    }
  }
  return samples;
}

// Creates a playable sample for a Nintendo DS pulse channel. dutyCycle says
// how much of each cycle is high, baseFrequencyHz sets the pitch, and
// sampleRate and sampleCount describe the generated sample.
[[nodiscard]] std::vector<s16> synthesizeBandLimitedPulsePcm16(double dutyCycle, u32 sampleRate, u32 sampleCount,
                                                               double baseFrequencyHz = 440.0) {
  std::vector<s16> samples(sampleCount);
  if (samples.empty() || sampleRate == 0 || baseFrequencyHz <= 0.0) {
    return samples;
  }

  std::vector<double> coefficients = {dutyCycle - 0.5};
  int harmonic = 1;
  // A pulse contains increasingly high multiples of its base pitch. Keep only
  // those below half the sample rate; faster ones would become false tones.
  const u32 maxHarmonics = static_cast<u32>(sampleRate / (baseFrequencyHz * 2.0));
  std::generate_n(std::back_inserter(coefficients), maxHarmonics, [dutyCycle, &harmonic]() {
    const double value = std::sin(harmonic * dutyCycle * kPi) * 2.0 / (harmonic * kPi);
    ++harmonic;
    return value;
  });

  const double scale = baseFrequencyHz * kPi * 2.0 / static_cast<double>(sampleRate);
  for (u32 i = 0; i < sampleCount; ++i) {
    int counter = 0;
    const double value = std::accumulate(coefficients.begin(), coefficients.end(), 0.0,
                                         [i, scale, &counter](double sum, double coefficient) {
                                           sum += coefficient * std::cos(counter++ * scale * i);
                                           return sum;
                                         });
    samples[i] = static_cast<s16>(std::clamp(std::round(value * 0x7fff * 2.0), -32768.0, 32767.0));
  }
  return samples;
}

// Creates a playable sample loop from one cycle of a GBA hardware waveform.
// Each value in steps is the output level held during one equal slice of that
// cycle. MP2k passes eight values for a pulse channel—1, 2, 4, or 6 high slices
// followed by low slices—or 32 values for a programmable wave. sampleCount
// sets the length of the new loop.
[[nodiscard]] std::vector<s16> synthesizeBandLimitedStepPcm16(std::span<const s16> steps, u32 sampleCount) {
  std::vector<s16> samples(sampleCount);
  if (steps.empty() || sampleCount == 0) {
    return samples;
  }

  // Rebuild the shape by layering waves that repeat 1, 2, 3, ... times per
  // loop. Stop before a repetition is reduced to only two output samples,
  // which is too little to reproduce it reliably.
  const u32 harmonics = (sampleCount - 1) / 2;
  std::vector<std::complex<double>> coefficients(harmonics + 1);
  coefficients[0] = std::accumulate(steps.begin(), steps.end(), 0.0) / steps.size();
  for (u32 harmonic = 1; harmonic <= harmonics; ++harmonic) {
    std::complex<double> sum{};
    for (u32 step = 0; step < steps.size(); ++step) {
      const double phase = -2.0 * kPi * harmonic * (step + 0.5) / steps.size();
      sum += static_cast<double>(steps[step]) * std::polar(1.0, phase);
    }
    coefficients[harmonic] = sum * (std::sin(kPi * harmonic / steps.size()) / (kPi * harmonic));
  }

  for (u32 i = 0; i < sampleCount; ++i) {
    double value = coefficients[0].real();
    for (u32 harmonic = 1; harmonic <= harmonics; ++harmonic) {
      value += 2.0 * (coefficients[harmonic] * std::polar(1.0, 2.0 * kPi * harmonic * i / sampleCount)).real();
    }
    samples[i] = static_cast<s16>(std::clamp(std::round(value), -32768.0, 32767.0));
  }
  return samples;
}

[[nodiscard]] std::optional<DecodedSample> guardedLoopSample(const Sample& sample, std::vector<s16> period) {
  const size_t guard = sample.loop.enabled ? sample.loop.start : 0;
  const u64 outputSize = static_cast<u64>(guard) * 2 + period.size();
  if (period.empty() || guard > period.size() || outputSize > std::numeric_limits<u32>::max()) {
    return std::nullopt;
  }
  DecodedSample decoded{.sampleRate = sample.sampleRate, .channels = 1, .loop = sample.loop};
  decoded.pcm.reserve(static_cast<size_t>(outputSize));
  decoded.pcm.insert(decoded.pcm.end(), period.end() - static_cast<std::ptrdiff_t>(guard), period.end());
  decoded.pcm.insert(decoded.pcm.end(), period.begin(), period.end());
  decoded.pcm.insert(decoded.pcm.end(), period.begin(), period.begin() + static_cast<std::ptrdiff_t>(guard));
  return decoded;
}

[[nodiscard]] std::optional<DecodedSample> decodePcmS8(const Sample& sample, std::span<const u8> sourceBytes) {
  if (!rangeIsValid(sample, sourceBytes)) {
    return std::nullopt;
  }

  const auto encoded = sourceBytes.subspan(sample.encodedData.offset, sample.encodedData.size);
  DecodedSample decoded{
      .sampleRate = sample.sampleRate,
      .channels = sample.channels,
      .loop = sample.loop,
  };
  decoded.pcm.reserve(encoded.size());
  for (size_t index = 0; index < encoded.size(); ++index) {
    const size_t sourceIndex = sample.reverse ? encoded.size() - 1 - index : index;
    decoded.pcm.push_back(static_cast<s16>(static_cast<s8>(encoded[sourceIndex])) << 8);
  }
  return decoded;
}

[[nodiscard]] std::optional<DecodedSample> decodePcmS16(const Sample& sample, std::span<const u8> sourceBytes) {
  if (!rangeIsValid(sample, sourceBytes)) {
    return std::nullopt;
  }

  const auto encoded = sourceBytes.subspan(sample.encodedData.offset, sample.encodedData.size);
  DecodedSample decoded{
      .sampleRate = sample.sampleRate,
      .channels = sample.channels,
      .loop = sample.loop,
  };
  decoded.pcm.reserve(encoded.size() / 2);
  const size_t sampleCount = encoded.size() / 2;
  for (size_t index = 0; index < sampleCount; ++index) {
    const size_t sourceIndex = sample.reverse ? sampleCount - 1 - index : index;
    const size_t offset = sourceIndex * 2;
    const u16 value = sample.bigEndian
                          ? static_cast<u16>((static_cast<u16>(encoded[offset]) << 8) | encoded[offset + 1])
                          : le16(encoded, offset);
    decoded.pcm.push_back(static_cast<s16>(value));
  }
  return decoded;
}

[[nodiscard]] std::optional<DecodedSample> decodeKonamiDeltaPcm(const Sample& sample, std::span<const u8> sourceBytes,
                                                                const std::array<s32, 16>& deltas) {
  if (!rangeIsValid(sample, sourceBytes)) {
    return std::nullopt;
  }
  const auto encoded = sourceBytes.subspan(sample.encodedData.offset, sample.encodedData.size);
  DecodedSample decoded{
      .sampleRate = sample.sampleRate,
      .channels = sample.channels,
      .loop = sample.loop,
  };
  decoded.pcm.reserve(encoded.size() * 2);
  s32 previous = 0;
  auto emit = [&](u8 nibble) {
    previous = std::clamp<s32>(previous + deltas[nibble & 0x0f], -32768, 32767);
    decoded.pcm.push_back(static_cast<s16>(previous));
  };
  for (size_t index = 0; index < encoded.size(); ++index) {
    const size_t sourceIndex = sample.reverse ? encoded.size() - 1 - index : index;
    const u8 value = encoded[sourceIndex];
    emit(value & 0x0f);
    emit(value >> 4);
  }
  return decoded;
}

[[nodiscard]] std::optional<DecodedSample> decodeKonamiK054539Adpcm(const Sample& sample,
                                                                    std::span<const u8> sourceBytes) {
  static constexpr std::array<s32, 16> deltas{
      0, 256, 512, 1024, 2048, 4096, 8192, 16384, 0, -16384, -8192, -4096, -2048, -1024, -512, -256,
  };
  return decodeKonamiDeltaPcm(sample, sourceBytes, deltas);
}

[[nodiscard]] std::optional<DecodedSample> decodeKonamiK053260Adpcm(const Sample& sample,
                                                                    std::span<const u8> sourceBytes) {
  // K053260 PPCM differs from K054539 DPCM only at nibble 8: it is the
  // largest negative delta rather than a zero delta.
  static constexpr std::array<s32, 16> deltas{
      0, 256, 512, 1024, 2048, 4096, 8192, 16384, -32768, -16384, -8192, -4096, -2048, -1024, -512, -256,
  };
  return decodeKonamiDeltaPcm(sample, sourceBytes, deltas);
}

[[nodiscard]] std::optional<DecodedSample> decodeOkiAdpcm(const Sample& sample, std::span<const u8> sourceBytes) {
  if (!rangeIsValid(sample, sourceBytes)) {
    return std::nullopt;
  }

  const auto encoded = sourceBytes.subspan(sample.encodedData.offset, sample.encodedData.size);
  DecodedSample decoded{
      .sampleRate = sample.sampleRate,
      .channels = sample.channels,
      .loop = sample.loop,
  };
  if (encoded.empty() && sample.codecParameter != 0) {
    decoded.pcm.assign(sample.codecParameter, 0);
    return decoded;
  }
  decoded.pcm.reserve(encoded.size() * 2);

  s32 signal = 0;
  s32 stepIndex = 0;
  auto emit = [&](u8 nibble) {
    const s32 step = kOkiStepTable[stepIndex];
    s32 difference = step / 8;
    if ((nibble & 1) != 0) {
      difference += step / 4;
    }
    if ((nibble & 2) != 0) {
      difference += step / 2;
    }
    if ((nibble & 4) != 0) {
      difference += step;
    }
    signal += (nibble & 8) != 0 ? -difference : difference;
    signal = std::clamp<s32>(signal, -2048, 2047);
    stepIndex = std::clamp<s32>(stepIndex + kOkiIndexShift[nibble & 7], 0, 48);

    // The MSM6295 path used by CPS1 scales the 12-bit decoder output by 11
    // before presenting PCM. Keeping that conversion here preserves the
    // hardware level without target-specific negative attenuation.
    decoded.pcm.push_back(static_cast<s16>(std::clamp<s32>(signal * 11, -32768, 32767)));
  };
  for (const u8 value : encoded) {
    emit(value >> 4);
    emit(value & 0x0f);
  }
  return decoded;
}

[[nodiscard]] std::optional<DecodedSample> decodeNdsImaAdpcm(const Sample& sample, std::span<const u8> sourceBytes) {
  if (!rangeIsValid(sample, sourceBytes) || sample.encodedData.offset < 4) {
    return std::nullopt;
  }

  // Sample::encodedData starts at the ADPCM nibble stream. SWAV stores the predictor header in
  // the four bytes immediately before it.
  const auto encoded = sourceBytes.subspan(sample.encodedData.offset, sample.encodedData.size);
  const u32 headerOffset = static_cast<u32>(sample.encodedData.offset - 4);
  const u32 header =
      static_cast<u32>(le16(sourceBytes, headerOffset)) | (static_cast<u32>(le16(sourceBytes, headerOffset + 2)) << 16);
  int pcm16 = static_cast<s16>(header & 0xffff);
  int index = static_cast<int>((header >> 16) & 0x7f);
  if (index >= static_cast<int>(std::size(kNdsAdpcmTable))) {
    return std::nullopt;
  }

  DecodedSample decoded{
      .sampleRate = sample.sampleRate,
      .channels = sample.channels,
      .loop = sample.loop,
  };
  decoded.pcm.reserve(encoded.size() * 2 + 1);
  decoded.pcm.push_back(static_cast<s16>(pcm16));
  for (const u8 byte : encoded) {
    processNdsImaNibble(byte & 0x0f, index, pcm16);
    decoded.pcm.push_back(static_cast<s16>(pcm16));
    processNdsImaNibble((byte & 0xf0) >> 4, index, pcm16);
    decoded.pcm.push_back(static_cast<s16>(pcm16));
  }
  return decoded;
}

[[nodiscard]] std::optional<DecodedSample> decodeNdsPsg(const Sample& sample, std::span<const u8>) {
  const u32 sampleCount = sample.loop.length != 0 ? sample.loop.length : 32768;
  DecodedSample decoded{
      .sampleRate = sample.sampleRate == 0 ? 32768 : sample.sampleRate,
      .channels = sample.channels == 0 ? static_cast<u8>(1) : sample.channels,
      .loop = sample.loop,
  };
  decoded.pcm = sample.codecParameter == 8 ? synthesizeLfsrNoisePcm16(sampleCount)
                                           : synthesizeBandLimitedPulsePcm16(ndsPsgDutyCycle(sample.codecParameter),
                                                                             decoded.sampleRate, sampleCount);
  return decoded;
}

[[nodiscard]] std::optional<DecodedSample> decodeSnesDspNoise(const Sample& sample) {
  const u32 sampleCount = sample.loop.length == 0 ? kSnesDspNoiseSampleCount : sample.loop.length;
  DecodedSample decoded{
      .sampleRate = sample.sampleRate == 0 ? kSnesDspSampleRate : sample.sampleRate,
      .channels = 1,
      .loop = sample.loop,
  };
  decoded.pcm = synthesizeSnesDspNoisePcm16(static_cast<u8>(sample.codecParameter), sampleCount);
  return decoded;
}

[[nodiscard]] std::optional<DecodedSample> decodePsxAdpcm(const Sample& sample, std::span<const u8> sourceBytes) {
  if (!rangeIsValid(sample, sourceBytes)) {
    return std::nullopt;
  }

  const auto encoded = sourceBytes.subspan(sample.encodedData.offset, sample.encodedData.size);
  DecodedSample decoded{
      .sampleRate = sample.sampleRate,
      .channels = sample.channels,
      .loop = sample.loop,
  };
  decoded.pcm.reserve((encoded.size() / kPsxAdpcmBlockBytes) * kPsxAdpcmFramesPerBlock);

  s32 previous1 = 0;
  s32 previous2 = 0;
  for (size_t offset = 0; offset + kPsxAdpcmBlockBytes <= encoded.size(); offset += kPsxAdpcmBlockBytes) {
    const auto outputOffset = decoded.pcm.size();
    decoded.pcm.resize(outputOffset + kPsxAdpcmFramesPerBlock);
    decodePsxAdpcmBlock(
        std::span<s16, kPsxAdpcmFramesPerBlock>(decoded.pcm.data() + outputOffset, kPsxAdpcmFramesPerBlock),
        std::span<const u8, kPsxAdpcmBlockBytes>(encoded.data() + offset, kPsxAdpcmBlockBytes), previous1, previous2);
  }
  return decoded;
}

[[nodiscard]] std::optional<DecodedSample> decodeSnesBrr(const Sample& sample, std::span<const u8> sourceBytes) {
  const auto offset = sample.encodedData.offset;
  const auto size = sample.encodedData.size;
  if (offset > sourceBytes.size() || size > sourceBytes.size() - offset) {
    return std::nullopt;
  }

  const auto encoded = sourceBytes.subspan(offset, size);
  DecodedSample decoded{
      .sampleRate = sample.sampleRate,
      .channels = sample.channels,
      .loop = sample.loop,
  };

  s32 previous1 = 0;
  s32 previous2 = 0;
  for (size_t blockOffset = 0; blockOffset + 9 <= encoded.size(); blockOffset += 9) {
    // BRR block bit 0 marks the terminal block. Some scanners already trim encodedData,
    // but respecting the flag here keeps the decoder robust for larger source ranges.
    const auto header = encoded[blockOffset];
    const auto payload = encoded.subspan(blockOffset + 1, 8);
    const auto outputOffset = decoded.pcm.size();
    decoded.pcm.resize(outputOffset + 16);
    decodeBrrBlock(std::span<s16, 16>(decoded.pcm.data() + outputOffset, 16), header,
                   std::span<const u8, 8>(payload.data(), 8), previous1, previous2);

    if ((header & 0x01) != 0) {
      break;
    }
  }

  return decoded;
}

[[nodiscard]] std::vector<s16> decodeGbaBdpcm(std::span<const u8> encoded, u32 sampleCount) {
  static constexpr std::array<s8, 16> deltas{
      0, 1, 4, 9, 16, 25, 36, 49, -64, -49, -36, -25, -16, -9, -4, -1,
  };
  std::vector<s16> decoded;
  decoded.reserve(sampleCount);

  for (size_t block = 0; block + 33 <= encoded.size() && decoded.size() < sampleCount; block += 33) {
    s8 value = static_cast<s8>(encoded[block]);
    decoded.push_back(static_cast<s16>(value) << 8);
    for (size_t byte = 1; byte < 33 && decoded.size() < sampleCount; ++byte) {
      const u8 packed = encoded[block + byte];
      // The first byte contributes only its low nibble. Every later byte is
      // decoded high-nibble first, then low-nibble.
      if (byte != 1) {
        value = static_cast<s8>(value + deltas[packed >> 4]);
        decoded.push_back(static_cast<s16>(value) << 8);
        if (decoded.size() >= sampleCount) {
          break;
        }
      }
      value = static_cast<s8>(value + deltas[packed & 0x0f]);
      decoded.push_back(static_cast<s16>(value) << 8);
    }
  }
  decoded.resize(sampleCount, 0);
  return decoded;
}

[[nodiscard]] std::optional<DecodedSample> decodeGbaDirectSound(const Sample& sample, std::span<const u8> sourceBytes) {
  if (!rangeIsValid(sample, sourceBytes) || sample.encodedData.size < 16 || sample.sampleRate == 0 ||
      sample.loop.length == 0) {
    return std::nullopt;
  }
  const auto header = sourceBytes.subspan(sample.encodedData.offset, sample.encodedData.size);
  const bool compressed = le16(header, 0) != 0;
  const u64 totalSamples = static_cast<u64>(sample.loop.start) + sample.loop.length;
  if (totalSamples > std::numeric_limits<u32>::max()) {
    return std::nullopt;
  }
  const u32 sampleCount = static_cast<u32>(totalSamples);

  const auto body = header.subspan(16);
  std::vector<s16> source(sampleCount);
  if (compressed) {
    source = decodeGbaBdpcm(body, sampleCount);
  } else {
    std::ranges::transform(body.first(std::min<size_t>(body.size(), sampleCount)), source.begin(),
                           [](u8 value) { return static_cast<s16>(static_cast<s8>(value)) << 8; });
  }
  if (sample.reverse) {
    std::ranges::reverse(source);
  }
  const u32 mixerRate = static_cast<u32>(sample.codecParameter >> 32);
  if (mixerRate == 0) {
    return std::nullopt;
  }
  constexpr u64 unit = u64{1} << 23;
  const u64 step = static_cast<u32>(sample.codecParameter) == 0 ? unit : static_cast<u32>(sample.codecParameter);
  const auto ceilDivide = [](u64 value, u64 divisor) { return (value + divisor - 1) / divisor; };
  const u64 mixerFrames = ceilDivide(totalSamples * unit, step);
  const u64 outputFrames = ceilDivide(mixerFrames * sample.sampleRate, mixerRate);
  if (outputFrames > std::numeric_limits<u32>::max()) {
    return std::nullopt;
  }

  DecodedSample decoded{.sampleRate = sample.sampleRate, .channels = 1};
  decoded.pcm.reserve(static_cast<size_t>(outputFrames));
  for (u64 frame = 0; frame < outputFrames; ++frame) {
    const u64 position = (frame * mixerRate / sample.sampleRate) * step;
    const u32 index = static_cast<u32>(position >> 23);
    const u32 next = index + 1 < sampleCount ? index + 1 : sample.loop.enabled ? sample.loop.start : index;
    const s32 first = source[index] >> 8;
    const s32 difference = (source[next] >> 8) - first;
    const s32 fraction = static_cast<s32>(position & (unit - 1));
    decoded.pcm.push_back(static_cast<s16>((first + ((fraction * difference) >> 23)) << 8));
  }
  if (sample.loop.enabled) {
    const u64 mixerLoopStart = ceilDivide(static_cast<u64>(sample.loop.start) * unit, step);
    decoded.loop.enabled = true;
    decoded.loop.start = static_cast<u32>(ceilDivide(mixerLoopStart * sample.sampleRate, mixerRate));
    decoded.loop.length = static_cast<u32>(decoded.pcm.size()) - decoded.loop.start;
  }
  return decoded;
}

[[nodiscard]] std::optional<DecodedSample> decodeGbaPsg(const Sample& sample) {
  // Each GBA pulse cycle has eight equal slices. Its four duty settings keep
  // the output high for 1, 2, 4, or 6 of them.
  constexpr std::array<u8, 4> highSteps{1, 2, 4, 6};
  if (sample.loop.length == 0) {
    return std::nullopt;
  }
  std::vector<s16> period;
  if (sample.codecParameter >= 4) {
    if (sample.codecParameter > 5 || sample.loop.length == std::numeric_limits<u32>::max()) {
      return std::nullopt;
    }
    const bool shortWidth = sample.codecParameter == 5;
    period = synthesizeLfsrNoisePcm16(sample.loop.length + 1, shortWidth ? 0x7f : 0x7fff, shortWidth ? 0x60 : 0x6000,
                                      0x4000);
    period.erase(period.begin());
  } else {
    const u32 high = highSteps[sample.codecParameter & 3];
    // The analog output blocks the duty-dependent DC component.
    std::array<s16, 8> steps{};
    steps.fill(static_cast<s16>(-static_cast<s32>(high) * 4096));
    std::fill_n(steps.begin(), high, static_cast<s16>((8 - high) * 4096));
    period = synthesizeBandLimitedStepPcm16(steps, sample.loop.length);
  }
  return guardedLoopSample(sample, std::move(period));
}

[[nodiscard]] std::optional<DecodedSample> decodeGbaPsgWave(const Sample& sample, std::span<const u8> sourceBytes) {
  if (!rangeIsValid(sample, sourceBytes) || sample.encodedData.size != 16 || sample.loop.length == 0) {
    return std::nullopt;
  }
  const auto encoded = sourceBytes.subspan(sample.encodedData.offset, sample.encodedData.size);
  std::array<s16, 32> wave{};
  const s32 sum = std::accumulate(encoded.begin(), encoded.end(), s32{0},
                                  [](s32 value, u8 packed) { return value + (packed >> 4) + (packed & 0x0f); });
  size_t index = 0;
  for (const u8 packed : encoded) {
    // The hardware DAC emits n / 16; its analog high-pass removes the cycle's
    // DC component. This integer form preserves that scale exactly in PCM16.
    wave[index++] = static_cast<s16>(((packed >> 4) * 32 - sum) * 64);
    wave[index++] = static_cast<s16>(((packed & 0x0f) * 32 - sum) * 64);
  }
  return guardedLoopSample(sample, synthesizeBandLimitedStepPcm16(wave, sample.loop.length));
}

}  // namespace

std::optional<DecodedSample> decodeSample(const Sample& sample, std::span<const u8> sourceBytes) {
  switch (sample.codec) {
    case AudioCodec::PcmS8:
      return decodePcmS8(sample, sourceBytes);
    case AudioCodec::PcmS16:
      return decodePcmS16(sample, sourceBytes);
    case AudioCodec::SnesBrr:
      return decodeSnesBrr(sample, sourceBytes);
    case AudioCodec::SnesDspNoise:
      return decodeSnesDspNoise(sample);
    case AudioCodec::NdsImaAdpcm:
      return decodeNdsImaAdpcm(sample, sourceBytes);
    case AudioCodec::NdsPsg:
      return decodeNdsPsg(sample, sourceBytes);
    case AudioCodec::GbaDirectSound:
      return decodeGbaDirectSound(sample, sourceBytes);
    case AudioCodec::GbaPsg:
      return decodeGbaPsg(sample);
    case AudioCodec::GbaPsgWave:
      return decodeGbaPsgWave(sample, sourceBytes);
    case AudioCodec::PsxAdpcm:
      return decodePsxAdpcm(sample, sourceBytes);
    case AudioCodec::KonamiK053260Adpcm:
      return decodeKonamiK053260Adpcm(sample, sourceBytes);
    case AudioCodec::KonamiK054539Adpcm:
      return decodeKonamiK054539Adpcm(sample, sourceBytes);
    case AudioCodec::OkiAdpcm:
      return decodeOkiAdpcm(sample, sourceBytes);
    case AudioCodec::Unknown:
      return std::nullopt;
  }
  return std::nullopt;
}

}  // namespace vgmtrans::core
