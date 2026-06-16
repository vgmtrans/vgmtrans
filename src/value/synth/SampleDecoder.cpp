/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/synth/SampleDecoder.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <numeric>
#include <optional>
#include <stdexcept>

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
  for (const u8 value : encoded) {
    decoded.pcm.push_back(static_cast<s16>(static_cast<s8>(value)) << 8);
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
  for (size_t offset = 0; offset + 1 < encoded.size(); offset += 2) {
    decoded.pcm.push_back(static_cast<s16>(le16(encoded, offset)));
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

}  // namespace

SampleDecoderRegistry SampleDecoderRegistry::withDefaultDecoders() {
  SampleDecoderRegistry registry;
  registry.add(SampleDecoder{.codec = AudioCodec::PcmS8, .decode = decodePcmS8});
  registry.add(SampleDecoder{.codec = AudioCodec::PcmS16, .decode = decodePcmS16});
  registry.add(SampleDecoder{.codec = AudioCodec::SnesBrr, .decode = decodeSnesBrr});
  registry.add(SampleDecoder{.codec = AudioCodec::NdsImaAdpcm, .decode = decodeNdsImaAdpcm});
  registry.add(SampleDecoder{.codec = AudioCodec::NdsPsg, .decode = decodeNdsPsg});
  return registry;
}

void SampleDecoderRegistry::add(SampleDecoder decoder) {
  if (decoder.codec == AudioCodec::Unknown || decoder.decode == nullptr) {
    throw std::invalid_argument("Cannot register an incomplete SampleDecoder");
  }
  decoders_.push_back(decoder);
}

std::optional<DecodedSample> SampleDecoderRegistry::decode(const Sample& sample,
                                                           std::span<const u8> sourceBytes) const {
  const auto decoder =
      std::ranges::find_if(decoders_, [&sample](const auto& candidate) { return candidate.codec == sample.codec; });
  if (decoder == decoders_.end()) {
    return std::nullopt;
  }
  return decoder->decode(sample, sourceBytes);
}

}  // namespace vgmtrans::core
