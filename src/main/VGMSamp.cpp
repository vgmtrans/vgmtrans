/**
 * VGMTrans (c) - 2002-2021
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#include "pch.h"
#include "VGMSamp.h"
#include "VGMSampColl.h"
#include "Root.h"

// *******
// VGMSamp
// *******

DECLARE_MENU(VGMSamp)

VGMSamp::VGMSamp(VGMSampColl *sampColl, uint32_t offset, uint32_t length, uint32_t dataOffset,
                 uint32_t dataLen, uint8_t nChannels, uint16_t theBPS, uint32_t theRate,
                 string theName)
    : VGMItem(sampColl->vgmfile, offset, length, theName), dataOff(dataOffset), dataLength(dataLen),
      bps(theBPS), rate(theRate), channels(nChannels), parSampColl(sampColl) {
}

double VGMSamp::GetCompressionRatio() {
  return 1.0;
}

void VGMSamp::ConvertToStdWave(uint8_t *buf) {
  switch (waveType) {
    case WT_PCM8:
      GetBytes(dataOff, dataLength, buf);
      /* Need to transform to unsigned (despite what the type imples), as standard WAVE uses PCM8
       * unsigned */
      for (unsigned int i = 0; i < dataLength; i++)
        buf[i] ^= 0x80;
      break;
    case WT_PCM16:
    default:
      /* Nothing to do here, PCM16 is signed */
      GetBytes(dataOff, dataLength, buf);
      break;
  }
}

bool VGMSamp::OnSaveAsWav() {
  string filepath = pRoot->UI_GetSaveFilePath(ConvertToSafeFileName(name), "wav");
  if (filepath.empty())
    return SaveAsWav(filepath);
  return false;
}

bool VGMSamp::SaveAsWav(const std::string &filepath) {
  uint32_t bufSize;
  if (this->ulUncompressedSize)
    bufSize = this->ulUncompressedSize;
  else
    bufSize = (uint32_t)ceil((double)dataLength * GetCompressionRatio());

  std::vector<uint8_t> uncompSampBuf(
      bufSize);                           
  ConvertToStdWave(uncompSampBuf.data());

  uint16_t blockAlign = bps / 8 * channels;

  bool hasLoop = (this->loop.loopStatus != -1 && this->loop.loopStatus != 0);

  std::vector<uint8_t> waveBuf;
  PushTypeOnVectBE<uint32_t>(waveBuf, 0x52494646);                                        //"RIFF"
  PushTypeOnVect<uint32_t>(waveBuf, 0x24 + ((bufSize + 1) & ~1) + (hasLoop ? 0x50 : 0));  // size

  // WriteLIST(waveBuf, 0x43564157, bufSize+24);			//write "WAVE" list
  PushTypeOnVectBE<uint32_t>(waveBuf, 0x57415645);       //"WAVE"
  PushTypeOnVectBE<uint32_t>(waveBuf, 0x666D7420);       //"fmt "
  PushTypeOnVect<uint32_t>(waveBuf, 16);                 // size
  PushTypeOnVect<uint16_t>(waveBuf, 1);                  // wFormatTag
  PushTypeOnVect<uint16_t>(waveBuf, channels);           // wChannels
  PushTypeOnVect<uint32_t>(waveBuf, rate);               // dwSamplesPerSec
  PushTypeOnVect<uint32_t>(waveBuf, rate * blockAlign);  // dwAveBytesPerSec
  PushTypeOnVect<uint16_t>(waveBuf, blockAlign);         // wBlockAlign
  PushTypeOnVect<uint16_t>(waveBuf, bps);                // wBitsPerSample

  PushTypeOnVectBE<uint32_t>(waveBuf, 0x64617461);                            //"data"
  PushTypeOnVect<uint32_t>(waveBuf, bufSize);                                 // size
  waveBuf.insert(waveBuf.end(), uncompSampBuf.begin(), uncompSampBuf.end());  // Write the sample
  if (bufSize % 2)
    waveBuf.push_back(0);

  if (hasLoop) {
    const int origFormatBytesPerSamp = bps / 8;
    double compressionRatio = GetCompressionRatio();

    // If the sample loops, but the loop length is 0, then assume the length should
    // extend to the end of the sample.
    uint32_t loopLength = loop.loopLength;
    if (loop.loopStatus && loop.loopLength == 0) {
      loopLength = dataLength - loop.loopStart;
    }

    uint32_t loopStart =
        (loop.loopStartMeasure == LM_BYTES)
            ? (uint32_t)((loop.loopStart * compressionRatio) / origFormatBytesPerSamp)
            : loop.loopStart;
    uint32_t loopLenInSamp =
        (loop.loopLengthMeasure == LM_BYTES)
            ? (uint32_t)((loopLength * compressionRatio) / origFormatBytesPerSamp)
            : loopLength;
    uint32_t loopEnd = loopStart + loopLenInSamp;

    PushTypeOnVectBE<uint32_t>(waveBuf, 0x736D706C);       //"smpl"
    PushTypeOnVect<uint32_t>(waveBuf, 0x50);               // size
    PushTypeOnVect<uint32_t>(waveBuf, 0);                  // manufacturer
    PushTypeOnVect<uint32_t>(waveBuf, 0);                  // product
    PushTypeOnVect<uint32_t>(waveBuf, 1000000000 / rate);  // sample period
    PushTypeOnVect<uint32_t>(waveBuf, 60);                 // MIDI uniti note (C5)
    PushTypeOnVect<uint32_t>(waveBuf, 0);                  // MIDI pitch fraction
    PushTypeOnVect<uint32_t>(waveBuf, 0);                  // SMPTE format
    PushTypeOnVect<uint32_t>(waveBuf, 0);                  // SMPTE offset
    PushTypeOnVect<uint32_t>(waveBuf, 1);                  // sample loops
    PushTypeOnVect<uint32_t>(waveBuf, 0);                  // sampler data
    PushTypeOnVect<uint32_t>(waveBuf, 0);                  // cue point ID
    PushTypeOnVect<uint32_t>(waveBuf, 0);                  // type (loop forward)
    PushTypeOnVect<uint32_t>(waveBuf, loopStart);          // start sample #
    PushTypeOnVect<uint32_t>(waveBuf, loopEnd);            // end sample #
    PushTypeOnVect<uint32_t>(waveBuf, 0);                  // fraction
    PushTypeOnVect<uint32_t>(waveBuf, 0);                  // playcount
  }

  return pRoot->UI_WriteBufferToFile(filepath, &waveBuf[0], (uint32_t)waveBuf.size());
}
