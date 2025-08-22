/**
 * VGMTrans (c) - 2002-2024
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#include <cmath>
#include "VGMSamp.h"
#include "VGMSampColl.h"
#include "Root.h"
#include "ScaleConversion.h"
#include "helper.h"

// *******
// VGMSamp
// *******

VGMSamp::VGMSamp(VGMSampColl *sampColl, uint32_t offset, uint32_t length, uint32_t dataOffset,
                 uint32_t dataLen, uint8_t nChannels, uint16_t bps, uint32_t rate,
                 std::string name)
    : VGMItem(sampColl->vgmFile(), offset, length, std::move(name), Type::Sample),
      dataOff(dataOffset), dataLength(dataLen), bps(bps), rate(rate), channels(nChannels),
      parSampColl(sampColl) {
}

double VGMSamp::compressionRatio() {
  return 1.0;
}

void VGMSamp::convertToStdWave(std::uint8_t* buf) {
  readBytes(dataOff, dataLength, buf);

  if (m_endianness == Endianness::Big && waveType == WT_PCM16) {
    // Convert big-endian 16-bit PCM to little-endian by swapping each pair.
    // Guard against odd lengths (shouldn't happen for PCM16, but safe).
    for (std::size_t i = 0; i + 1 < dataLength; i += 2) {
      std::swap(buf[i], buf[i + 1]);
    }
  }

  if (m_reverse) {
    switch (waveType) {
      case WT_PCM16: {
          // treat the buffer as an array of 16-bit little-endian words;
          //   reversing the *words* keeps each sample’s byte order intact
        auto samples = std::span<u16>(
            reinterpret_cast<u16*>(buf),      // first element
            dataLength / 2);            // # of 16-bit words
        std::reverse(samples.begin(), samples.end());
        break;
      }
      case WT_PCM8:
      default:
        std::reverse(buf, buf + dataLength);
        break;
    }
  }

  if (m_signedness == Signedness::Unsigned) {
    switch (waveType) {
      case WT_PCM8:
        // 0…255  →  −128…127
        for (std::size_t i = 0; i < dataLength; ++i)
          buf[i] ^= 0x80;
        break;

      case WT_PCM16: {
        // 0…65535 → −32768…32767
        auto samples = std::span<std::uint16_t>(
            reinterpret_cast<std::uint16_t*>(buf),
            dataLength / 2);
        for (auto& s : samples)
          s = static_cast<std::uint16_t>(s - 0x8000);
        break;
      }
    }
  }
}

void VGMSamp::setVolume(double volume) {
  m_attenDb = ampToDb(volume);
}

bool VGMSamp::onSaveAsWav() {
  std::string filepath = pRoot->UI_getSaveFilePath(ConvertToSafeFileName(name()), "wav");
  if (filepath.empty())
    return saveAsWav(filepath);
  return false;
}

bool VGMSamp::saveAsWav(const std::string &filepath) {
  uint32_t bufSize;
  if (this->ulUncompressedSize)
    bufSize = this->ulUncompressedSize;
  else
    bufSize = static_cast<uint32_t>(ceil(dataLength * compressionRatio()));

  std::vector<uint8_t> uncompSampBuf(bufSize);
  convertToStdWave(uncompSampBuf.data());

  uint16_t blockAlign = bps / 8 * channels;

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
  pushTypeOnVect<uint16_t>(waveBuf, bps);                // wBitsPerSample

  pushTypeOnVectBE<uint32_t>(waveBuf, 0x64617461);                            //"data"
  pushTypeOnVect<uint32_t>(waveBuf, bufSize);                                 // size
  waveBuf.insert(waveBuf.end(), uncompSampBuf.begin(), uncompSampBuf.end());  // Write the sample
  if (bufSize % 2)
    waveBuf.push_back(0);

  if (hasLoop) {
    const int origFormatBytesPerSamp = bps / 8;

    // If the sample loops, but the loop length is 0, then assume the length should
    // extend to the end of the sample.
    uint32_t loopLength = loop.loopLength;
    if (loop.loopStatus && loop.loopLength == 0) {
      loopLength = dataLength - loop.loopStart;
    }

    uint32_t loopStart =
        (loop.loopStartMeasure == LM_BYTES)
            ? static_cast<uint32_t>((loop.loopStart * compressionRatio()) / origFormatBytesPerSamp)
            : loop.loopStart;
    uint32_t loopLenInSamp =
        (loop.loopLengthMeasure == LM_BYTES)
            ? static_cast<uint32_t>((loopLength * compressionRatio()) / origFormatBytesPerSamp)
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
