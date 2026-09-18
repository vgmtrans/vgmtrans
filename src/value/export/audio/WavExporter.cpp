/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "value/export/audio/WavExporter.h"
#include "value/export/BinaryWriter.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace vgmtrans::core {

namespace {

[[nodiscard]] u16 validChannels(u8 channels) {
  return std::max<u8>(channels, 1);
}

}  // namespace

std::vector<u8> encodePcm16Wav(const DecodedSample& sample) {
  // WAV export is intentionally minimal: decoded samples are already interleaved PCM16,
  // so this only writes a canonical RIFF/WAVE header and the sample payload.
  constexpr u16 bitsPerSample = 16;
  constexpr u16 bytesPerSample = bitsPerSample / 8;
  constexpr u32 riffHeaderBytesAfterSize = 36;

  if (sample.pcm.size() > std::numeric_limits<u32>::max() / bytesPerSample) {
    throw std::overflow_error("PCM sample is too large to export as RIFF/WAVE");
  }

  const u16 channels = validChannels(sample.channels);
  const u32 sampleRate = sample.sampleRate == 0 ? 32000 : sample.sampleRate;
  const u32 dataBytes = static_cast<u32>(sample.pcm.size() * bytesPerSample);
  if (dataBytes > std::numeric_limits<u32>::max() - riffHeaderBytesAfterSize) {
    throw std::overflow_error("PCM sample is too large to export as RIFF/WAVE");
  }

  const u16 blockAlign = static_cast<u16>(channels * bytesPerSample);
  if (sampleRate > std::numeric_limits<u32>::max() / blockAlign) {
    throw std::overflow_error("PCM sample rate is too large to export as RIFF/WAVE");
  }
  const u32 byteRate = sampleRate * blockAlign;

  std::vector<u8> bytes;
  bytes.reserve(44 + dataBytes);
  writeAscii(bytes, "RIFF");
  writeLe32(bytes, riffHeaderBytesAfterSize + dataBytes);
  writeAscii(bytes, "WAVE");
  writeAscii(bytes, "fmt ");
  writeLe32(bytes, 16);
  writeLe16(bytes, 1);
  writeLe16(bytes, channels);
  writeLe32(bytes, sampleRate);
  writeLe32(bytes, byteRate);
  writeLe16(bytes, blockAlign);
  writeLe16(bytes, bitsPerSample);
  writeAscii(bytes, "data");
  writeLe32(bytes, dataBytes);

  for (const s16 sampleValue : sample.pcm) {
    writeLe16(bytes, static_cast<u16>(sampleValue));
  }

  return bytes;
}

}  // namespace vgmtrans::core
