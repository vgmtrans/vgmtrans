/**
 * VGMTrans (c) - 2002-2024
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#include <algorithm>
#include <cmath>
#include <utility>
#include "VGMSamp.h"
#include "VGMSampColl.h"
#include "Root.h"
#include "ScaleConversion.h"
#include "helper.h"

// *******
// VGMSamp
// *******

VGMSamp::VGMSamp(VGMSampColl *sampColl, uint32_t offset, uint32_t length, uint32_t dataOffset,
                 uint32_t dataLen, uint8_t nChannels, BPS bps, uint32_t rate,
                 std::string name)
    : VGMItem(sampColl->vgmFile(), offset, length, std::move(name), Type::Sample),
      dataOff(dataOffset), dataLength(dataLen), m_bps(bps), rate(rate), channels(nChannels),
      parSampColl(sampColl) {
}

double VGMSamp::compressionRatio() const {
  return 1.0;
}

std::vector<uint8_t> VGMSamp::decodeToNativePcm() {
  std::vector<uint8_t> src(dataLength);
  readBytes(dataOff, dataLength, src.data());
  return src;
}

std::vector<uint8_t> VGMSamp::toPcm(Signedness targetSignedness,
                                    Endianness targetEndianness,
                                    BPS targetBps) {
  std::vector<uint8_t> src = decodeToNativePcm();

  if (!m_reverse &&
      m_bps == targetBps &&
      m_signedness == targetSignedness &&
      m_endianness == targetEndianness) {
    return src;
  }

  const bool isSrc16 = (m_bps == BPS::PCM16);
  const bool isDst16 = (targetBps == BPS::PCM16);

  const std::size_t sampleCount = isSrc16 ? (src.size() / 2) : src.size();
  std::vector<uint8_t> out(sampleCount * (isDst16 ? 2u : 1u));

  auto write16 = [&](std::size_t oi, uint16_t v) {
    const std::size_t b = oi * 2;
    if (targetEndianness == Endianness::Big) {
      out[b + 0] = uint8_t(v >> 8);
      out[b + 1] = uint8_t(v);
    } else {
      out[b + 0] = uint8_t(v);
      out[b + 1] = uint8_t(v >> 8);
    }
  };

  auto readS16 = [&](std::size_t i) -> int32_t {
    if (isSrc16) {
      const std::size_t b = i * 2;
      const uint16_t lo = src[b + 0];
      const uint16_t hi = src[b + 1];
      const uint16_t u = (m_endianness == Endianness::Big)
                           ? uint16_t((lo << 8) | hi)
                           : uint16_t(lo | (hi << 8));
      return (m_signedness == Signedness::Unsigned)
               ? (int32_t(u) - 0x8000)
               : int32_t(int16_t(u));
    } else {
      const uint8_t u = src[i];
      const int32_t s8 = (m_signedness == Signedness::Unsigned)
                           ? (int32_t(u) - 128)
                           : int32_t(int8_t(u));
      return s8 * 256;
    }
  };

  for (std::size_t i = 0; i < sampleCount; ++i) {
    const std::size_t oi = m_reverse ? (sampleCount - 1 - i) : i;

    int32_t s16 = readS16(i);
    s16 = std::clamp(s16, -32768, 32767);

    if (isDst16) {
      const uint16_t u = (targetSignedness == Signedness::Unsigned)
                           ? uint16_t(s16 + 0x8000)
                           : uint16_t(int16_t(s16));
      write16(oi, u);
    } else {
      int32_t s8 = (s16 >> 8);                       // assumes arithmetic shift (typical)
      s8 = std::clamp(s8, -128, 127);
      out[oi] = (targetSignedness == Signedness::Unsigned)
                  ? uint8_t(s8 + 128)
                  : uint8_t(int8_t(s8));
    }
  }

  return out;
}

uint32_t VGMSamp::uncompressedSize() const {
  if (ulUncompressedSize)
    return ulUncompressedSize;
  return static_cast<uint32_t>(std::ceil(dataLength * compressionRatio()));
}

void VGMSamp::setVolume(double volume) {
  m_attenDb = ampToDb(volume);
}

bool VGMSamp::onSaveAsWav() {
  auto filepath = pRoot->UI_getSaveFilePath(makeSafeFileName(name()).string(), "wav");
  if (!filepath.empty())
    return saveAsWav(filepath);
  return false;
}

bool VGMSamp::saveAsWav(const std::filesystem::path &filepath) {
  uint32_t bufSize = uncompressedSize();

  std::vector<uint8_t> uncompSampBuf = toPcm(Signedness::Signed, Endianness::Little, m_bps);
  bufSize = static_cast<uint32_t>(uncompSampBuf.size());

  uint16_t blockAlign = bytesPerSample() * channels;

  bool hasLoop = (this->loop.loopStatus != -1 && this->loop.loopStatus != 0);

  std::vector<uint8_t> waveBuf;
  pushTypeOnVectBE<uint32_t>(waveBuf, 0x52494646);                                        //"RIFF"
  pushTypeOnVect<uint32_t>(waveBuf, 0x24 + ((bufSize + 1) & ~1) + (hasLoop ? 0x50 : 0));  // size

  // WriteLIST(waveBuf, 0x43564157, bufSize+24);			//write "WAVE" list
  pushTypeOnVectBE<uint32_t>(waveBuf, 0x57415645);       //"WAVE"
  pushTypeOnVectBE<uint32_t>(waveBuf, 0x666D7420);       //"fmt "
  pushTypeOnVect<uint32_t>(waveBuf, 16);                 // size
  pushTypeOnVect<uint16_t>(waveBuf, 1);                  // wFormatTag
  pushTypeOnVect<uint16_t>(waveBuf, channels);           // wChannels
  pushTypeOnVect<uint32_t>(waveBuf, rate);               // dwSamplesPerSec
  pushTypeOnVect<uint32_t>(waveBuf, rate * blockAlign);  // dwAveBytesPerSec
  pushTypeOnVect<uint16_t>(waveBuf, blockAlign);         // wBlockAlign
  pushTypeOnVect<uint16_t>(waveBuf, bpsInt());    // wBitsPerSample

  pushTypeOnVectBE<uint32_t>(waveBuf, 0x64617461);                            //"data"
  pushTypeOnVect<uint32_t>(waveBuf, bufSize);                                 // size
  waveBuf.insert(waveBuf.end(), uncompSampBuf.begin(), uncompSampBuf.end());  // Write the sample
  if (bufSize % 2)
    waveBuf.push_back(0);

  if (hasLoop) {
    // If the sample loops, but the loop length is 0, then assume the length should
    // extend to the end of the sample.
    uint32_t loopLength = loop.loopLength;
    if (loop.loopStatus && loop.loopLength == 0) {
      loopLength = dataLength - loop.loopStart;
    }

    uint32_t loopStart = (loop.loopStartMeasure == LM_BYTES)
            ? static_cast<uint32_t>((loop.loopStart * compressionRatio()) / bytesPerSample())
            : loop.loopStart;
    uint32_t loopLenInSamp = (loop.loopLengthMeasure == LM_BYTES)
            ? static_cast<uint32_t>((loopLength * compressionRatio()) / bytesPerSample())
            : loopLength;
    uint32_t loopEnd = loopStart + loopLenInSamp;

    pushTypeOnVectBE<uint32_t>(waveBuf, 0x736D706C);       //"smpl"
    pushTypeOnVect<uint32_t>(waveBuf, 0x50);               // size
    pushTypeOnVect<uint32_t>(waveBuf, 0);                  // manufacturer
    pushTypeOnVect<uint32_t>(waveBuf, 0);                  // product
    pushTypeOnVect<uint32_t>(waveBuf, 1000000000 / rate);  // sample period
    pushTypeOnVect<uint32_t>(waveBuf, 60);                 // MIDI uniti note (C5)
    pushTypeOnVect<uint32_t>(waveBuf, 0);                  // MIDI pitch fraction
    pushTypeOnVect<uint32_t>(waveBuf, 0);                  // SMPTE format
    pushTypeOnVect<uint32_t>(waveBuf, 0);                  // SMPTE offset
    pushTypeOnVect<uint32_t>(waveBuf, 1);                  // sample loops
    pushTypeOnVect<uint32_t>(waveBuf, 0);                  // sampler data
    pushTypeOnVect<uint32_t>(waveBuf, 0);                  // cue point ID
    pushTypeOnVect<uint32_t>(waveBuf, 0);                  // type (loop forward)
    pushTypeOnVect<uint32_t>(waveBuf, loopStart);          // start sample #
    pushTypeOnVect<uint32_t>(waveBuf, loopEnd);            // end sample #
    pushTypeOnVect<uint32_t>(waveBuf, 0);                  // fraction
    pushTypeOnVect<uint32_t>(waveBuf, 0);                  // playcount
  }

  return pRoot->UI_writeBufferToFile(filepath, &waveBuf[0], waveBuf.size());
}
