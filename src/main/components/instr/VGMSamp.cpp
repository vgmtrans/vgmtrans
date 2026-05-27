/**
 * VGMTrans (c) - 2002-2024
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#include "util/types.h"
#include <algorithm>
#include <cmath>
#include <utility>
#include "VGMSamp.h"
#include "VGMSampColl.h"
#include "Root.h"
#include "ScaleConversion.h"
#include "helper.h"
#include "util/path.h"

// *******
// VGMSamp
// *******

VGMSamp::VGMSamp(VGMSampColl *sampColl, u32 offset, u32 length, u32 dataOffset,
                 u32 dataLen, u8 nChannels, BPS bps, u32 rate,
                 std::string name)
    : VGMItem(sampColl->vgmFile(), offset, length, std::move(name), Type::Sample),
      dataOff(dataOffset), dataLength(dataLen), m_bps(bps), rate(rate), channels(nChannels),
      parSampColl(sampColl) {
}

double VGMSamp::compressionRatio() const {
  return 1.0;
}

std::vector<u8> VGMSamp::decodeToNativePcm() {
  std::vector<u8> src(dataLength);
  readBytes(dataOff, dataLength, src.data());
  return src;
}

std::vector<u8> VGMSamp::toPcm(Signedness targetSignedness,
                                    Endianness targetEndianness,
                                    BPS targetBps) {
  std::vector<u8> src = decodeToNativePcm();

  if (!m_reverse &&
      m_bps == targetBps &&
      m_signedness == targetSignedness &&
      m_endianness == targetEndianness) {
    return src;
  }

  const bool isSrc16 = (m_bps == BPS::PCM16);
  const bool isDst16 = (targetBps == BPS::PCM16);

  const std::size_t sampleCount = isSrc16 ? (src.size() / 2) : src.size();
  std::vector<u8> out(sampleCount * (isDst16 ? 2u : 1u));

  auto write16 = [&](std::size_t oi, u16 v) {
    const std::size_t b = oi * 2;
    if (targetEndianness == Endianness::Big) {
      out[b + 0] = u8(v >> 8);
      out[b + 1] = u8(v);
    } else {
      out[b + 0] = u8(v);
      out[b + 1] = u8(v >> 8);
    }
  };

  auto readS16 = [&](std::size_t i) -> s32 {
    if (isSrc16) {
      const std::size_t b = i * 2;
      const u16 lo = src[b + 0];
      const u16 hi = src[b + 1];
      const u16 u = (m_endianness == Endianness::Big)
                           ? u16((lo << 8) | hi)
                           : u16(lo | (hi << 8));
      return (m_signedness == Signedness::Unsigned)
               ? (s32(u) - 0x8000)
               : s32(s16(u));
    } else {
      const u8 u = src[i];
      const s32 sample8 = (m_signedness == Signedness::Unsigned)
                           ? (s32(u) - 128)
                           : s32(s8(u));
      return sample8 * 256;
    }
  };

  for (std::size_t i = 0; i < sampleCount; ++i) {
    const std::size_t oi = m_reverse ? (sampleCount - 1 - i) : i;

    s32 sample16 = readS16(i);
    sample16 = std::clamp(sample16, -32768, 32767);

    if (isDst16) {
      const u16 u = (targetSignedness == Signedness::Unsigned)
                           ? u16(sample16 + 0x8000)
                           : u16(s16(sample16));
      write16(oi, u);
    } else {
      s32 sample8 = (sample16 >> 8);                       // assumes arithmetic shift (typical)
      sample8 = std::clamp(sample8, -128, 127);
      out[oi] = (targetSignedness == Signedness::Unsigned)
                  ? u8(sample8 + 128)
                  : u8(s8(sample8));
    }
  }

  return out;
}

u32 VGMSamp::uncompressedSize() const {
  if (ulUncompressedSize)
    return ulUncompressedSize;
  return static_cast<u32>(std::ceil(dataLength * compressionRatio()));
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
  u32 bufSize = uncompressedSize();

  std::vector<u8> uncompSampBuf = toPcm(Signedness::Signed, Endianness::Little, m_bps);
  bufSize = static_cast<u32>(uncompSampBuf.size());

  u16 blockAlign = bytesPerSample() * channels;

  bool hasLoop = (this->loop.loopStatus != -1 && this->loop.loopStatus != 0);

  std::vector<u8> waveBuf;
  pushTypeOnVectBE<u32>(waveBuf, 0x52494646);                                        //"RIFF"
  pushTypeOnVect<u32>(waveBuf, 0x24 + ((bufSize + 1) & ~1) + (hasLoop ? 0x50 : 0));  // size

  // WriteLIST(waveBuf, 0x43564157, bufSize+24);			//write "WAVE" list
  pushTypeOnVectBE<u32>(waveBuf, 0x57415645);       //"WAVE"
  pushTypeOnVectBE<u32>(waveBuf, 0x666D7420);       //"fmt "
  pushTypeOnVect<u32>(waveBuf, 16);                 // size
  pushTypeOnVect<u16>(waveBuf, 1);                  // wFormatTag
  pushTypeOnVect<u16>(waveBuf, channels);           // wChannels
  pushTypeOnVect<u32>(waveBuf, rate);               // dwSamplesPerSec
  pushTypeOnVect<u32>(waveBuf, rate * blockAlign);  // dwAveBytesPerSec
  pushTypeOnVect<u16>(waveBuf, blockAlign);         // wBlockAlign
  pushTypeOnVect<u16>(waveBuf, bitsPerSample());    // wBitsPerSample

  pushTypeOnVectBE<u32>(waveBuf, 0x64617461);                            //"data"
  pushTypeOnVect<u32>(waveBuf, bufSize);                                 // size
  waveBuf.insert(waveBuf.end(), uncompSampBuf.begin(), uncompSampBuf.end());  // Write the sample
  if (bufSize % 2)
    waveBuf.push_back(0);

  if (hasLoop) {
    // If the sample loops, but the loop length is 0, then assume the length should
    // extend to the end of the sample.
    u32 loopLength = loop.loopLength;
    if (loop.loopStatus && loop.loopLength == 0) {
      loopLength = dataLength - loop.loopStart;
    }

    u32 loopStart = (loop.loopStartMeasure == LM_BYTES)
            ? static_cast<u32>((loop.loopStart * compressionRatio()) / bytesPerSample())
            : loop.loopStart;
    u32 loopLenInSamp = (loop.loopLengthMeasure == LM_BYTES)
            ? static_cast<u32>((loopLength * compressionRatio()) / bytesPerSample())
            : loopLength;
    u32 loopEnd = loopStart + loopLenInSamp;

    pushTypeOnVectBE<u32>(waveBuf, 0x736D706C);       //"smpl"
    pushTypeOnVect<u32>(waveBuf, 0x50);               // size
    pushTypeOnVect<u32>(waveBuf, 0);                  // manufacturer
    pushTypeOnVect<u32>(waveBuf, 0);                  // product
    pushTypeOnVect<u32>(waveBuf, 1000000000 / rate);  // sample period
    pushTypeOnVect<u32>(waveBuf, 60);                 // MIDI uniti note (C5)
    pushTypeOnVect<u32>(waveBuf, 0);                  // MIDI pitch fraction
    pushTypeOnVect<u32>(waveBuf, 0);                  // SMPTE format
    pushTypeOnVect<u32>(waveBuf, 0);                  // SMPTE offset
    pushTypeOnVect<u32>(waveBuf, 1);                  // sample loops
    pushTypeOnVect<u32>(waveBuf, 0);                  // sampler data
    pushTypeOnVect<u32>(waveBuf, 0);                  // cue point ID
    pushTypeOnVect<u32>(waveBuf, 0);                  // type (loop forward)
    pushTypeOnVect<u32>(waveBuf, loopStart);          // start sample #
    pushTypeOnVect<u32>(waveBuf, loopEnd);            // end sample #
    pushTypeOnVect<u32>(waveBuf, 0);                  // fraction
    pushTypeOnVect<u32>(waveBuf, 0);                  // playcount
  }

  return pRoot->UI_writeBufferToFile(filepath, &waveBuf[0], waveBuf.size());
}
