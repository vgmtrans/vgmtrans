/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/synth/SampleDecoder.h"

#include "value/synth/PsxAdpcm.h"

#include <algorithm>
#include <array>
#include <cmath>
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

[[nodiscard]] std::vector<s16> synthesizeLfsrNoisePcm16(u32 sampleCount, u16 lfsrSeed = 0x7fff, u16 lfsrTap = 0x6000) {
  // PSG noise is not sample data in ROM. Emit a deterministic loopable waveform so synth
  // exporters have a concrete sample to reference.
  std::vector<s16> samples(sampleCount);
  if (samples.empty()) {
    return samples;
  }

  u16 value = lfsrSeed;
  samples[0] = 0x7fff;
  for (u32 i = 1; i < sampleCount; ++i) {
    const bool carry = (value & 0x0001) != 0;
    value >>= 1;
    if (carry) {
      samples[i] = -0x7fff;
      value ^= lfsrTap;
    } else {
      samples[i] = 0x7fff;
    }
  }
  return samples;
}

[[nodiscard]] std::vector<s16> synthesizeBandLimitedPulsePcm16(double dutyCycle, u32 sampleRate, u32 sampleCount,
                                                               double baseFrequencyHz = 440.0) {
  // PSG pulse instruments likewise have no source PCM. Band-limiting the generated
  // waveform avoids the harsh aliasing that a naive square wave would introduce.
  std::vector<s16> samples(sampleCount);
  if (samples.empty() || sampleRate == 0 || baseFrequencyHz <= 0.0) {
    return samples;
  }

  std::vector<double> coefficients = {dutyCycle - 0.5};
  int harmonic = 1;
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

[[nodiscard]] std::optional<DecodedSample> decodeGbaBdpcm(const Sample& sample, std::span<const u8> sourceBytes) {
  if (sample.encodedData.offset > sourceBytes.size() ||
      sample.encodedData.size > sourceBytes.size() - sample.encodedData.offset) {
    return std::nullopt;
  }

  static constexpr std::array<s8, 16> deltas{
      0, 1, 4, 9, 16, 25, 36, 49, -64, -49, -36, -25, -16, -9, -4, -1,
  };
  const auto encoded = sourceBytes.subspan(sample.encodedData.offset, sample.encodedData.size);
  const u32 requestedSamples = sample.codecParameter;
  DecodedSample decoded{.sampleRate = sample.sampleRate, .channels = 1, .loop = sample.loop};
  decoded.pcm.reserve(requestedSamples);

  for (size_t block = 0; block + 33 <= encoded.size() && decoded.pcm.size() < requestedSamples; block += 33) {
    s8 value = static_cast<s8>(encoded[block]);
    decoded.pcm.push_back(static_cast<s16>(value) << 8);
    for (size_t byte = 1; byte < 33 && decoded.pcm.size() < requestedSamples; ++byte) {
      const u8 packed = encoded[block + byte];
      // The first byte contributes only its low nibble. Every later byte is
      // decoded high-nibble first, then low-nibble.
      if (byte != 1) {
        value = static_cast<s8>(value + deltas[packed >> 4]);
        decoded.pcm.push_back(static_cast<s16>(value) << 8);
        if (decoded.pcm.size() >= requestedSamples) {
          break;
        }
      }
      value = static_cast<s8>(value + deltas[packed & 0x0f]);
      decoded.pcm.push_back(static_cast<s16>(value) << 8);
    }
  }
  decoded.pcm.resize(requestedSamples, 0);
  if (sample.reverse) {
    std::ranges::reverse(decoded.pcm);
  }
  return decoded;
}

[[nodiscard]] std::optional<DecodedSample> decodeGbaPsg(const Sample& sample) {
  constexpr std::array<double, 4> duties{0.125, 0.25, 0.5, 0.75};
  const size_t guard = sample.loop.enabled ? sample.loop.start : 0;
  const u64 sampleCountWithGuards = static_cast<u64>(guard) * 2 + sample.loop.length;
  if (sampleCountWithGuards > std::numeric_limits<u32>::max() || guard > sample.loop.length) {
    return std::nullopt;
  }
  DecodedSample decoded{.sampleRate = sample.sampleRate, .channels = 1, .loop = sample.loop};
  std::vector<s16> period;
  const bool noise = sample.codecParameter == 4 || sample.codecParameter == 5;
  if (noise && sample.loop.length == std::numeric_limits<u32>::max()) {
    return std::nullopt;
  }
  if (sample.codecParameter == 4) {
    period = synthesizeLfsrNoisePcm16(sample.loop.length + 1);
    period.erase(period.begin());
  } else if (sample.codecParameter == 5) {
    period = synthesizeLfsrNoisePcm16(sample.loop.length + 1, 0x7f, 0x60);
    period.erase(period.begin());
  } else {
    period = synthesizeBandLimitedPulsePcm16(duties[sample.codecParameter & 3], sample.sampleRate, sample.loop.length);
  }
  decoded.pcm.reserve(static_cast<size_t>(sampleCountWithGuards));
  decoded.pcm.insert(decoded.pcm.end(), period.end() - static_cast<std::ptrdiff_t>(guard), period.end());
  decoded.pcm.insert(decoded.pcm.end(), period.begin(), period.end());
  decoded.pcm.insert(decoded.pcm.end(), period.begin(), period.begin() + static_cast<std::ptrdiff_t>(guard));
  return decoded;
}

[[nodiscard]] std::optional<DecodedSample> decodeGbaPsgWave(const Sample& sample, std::span<const u8> sourceBytes) {
  if (sample.encodedData.offset > sourceBytes.size() || sample.encodedData.size != 16 ||
      sample.encodedData.size > sourceBytes.size() - sample.encodedData.offset) {
    return std::nullopt;
  }
  const auto encoded = sourceBytes.subspan(sample.encodedData.offset, sample.encodedData.size);
  DecodedSample decoded{.sampleRate = sample.sampleRate, .channels = 1, .loop = sample.loop};
  std::array<s16, 32> wave{};
  size_t index = 0;
  for (const u8 packed : encoded) {
    // The GBA wave channel converts each unsigned nibble to 2 * (n - 8).
    // Scaling that exact asymmetric -16..14 DAC range to PCM16 preserves both
    // its zero point and its amplitude relative to the other PSG channels.
    wave[index++] = static_cast<s16>((static_cast<s32>(packed >> 4) - 8) << 12);
    wave[index++] = static_cast<s16>((static_cast<s32>(packed & 0x0f) - 8) << 12);
  }
  const size_t guard = sample.loop.enabled ? sample.loop.start : 0;
  if (guard > wave.size() || (sample.loop.enabled && sample.loop.length != wave.size())) {
    return std::nullopt;
  }
  decoded.pcm.reserve(wave.size() + guard * 2);
  decoded.pcm.insert(decoded.pcm.end(), wave.end() - static_cast<std::ptrdiff_t>(guard), wave.end());
  decoded.pcm.insert(decoded.pcm.end(), wave.begin(), wave.end());
  decoded.pcm.insert(decoded.pcm.end(), wave.begin(), wave.begin() + static_cast<std::ptrdiff_t>(guard));
  return decoded;
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
    case AudioCodec::NdsImaAdpcm:
      return decodeNdsImaAdpcm(sample, sourceBytes);
    case AudioCodec::NdsPsg:
      return decodeNdsPsg(sample, sourceBytes);
    case AudioCodec::GbaBdpcm:
      return decodeGbaBdpcm(sample, sourceBytes);
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
