#include "PSDSEStream.h"

#include "PSDSEFormat.h"
#include "RawFile.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

namespace {

constexpr uint32_t kSadbMagic = 0x73616462;
constexpr uint32_t kSadbHeaderSize = 0x80;
constexpr uint32_t kDspHeaderSize = 0x60;

uint32_t nibbleAddressToSample(uint32_t address) {
  const uint32_t frameSamples = (address / 16) * 14;
  const uint32_t frameNibble = address % 16;
  return frameSamples + (frameNibble > 2 ? frameNibble - 2 : 0);
}

}  // namespace

bool PSDSESADBHeader::read(const RawFile* file, uint32_t readOffset) {
  offset = readOffset;
  if (readOffset > file->size() || kSadbHeaderSize > file->size() - readOffset ||
      file->readWordBE(readOffset) != kSadbMagic) {
    return false;
  }

  fileLength = file->readWordBE(readOffset + 0x08);
  version = file->readShortBE(readOffset + 0x0c);
  fileId = file->readShortBE(readOffset + 0x0e);
  channels = file->readByte(readOffset + 0x32);
  interleave = file->readShortBE(readOffset + 0x3a);
  encodedDataEnd = file->readWordBE(readOffset + 0x40);
  encodedBytesPerChannel = file->readWordBE(readOffset + 0x44);
  dataOffset = file->readWordBE(readOffset + 0x48);
  const uint32_t dspHeadersLength = file->readWordBE(readOffset + 0x4c);
  sampleCount = file->readWordBE(readOffset + 0x50);
  sampleRate = file->readWordBE(readOffset + 0x5c);
  volume = file->readByte(readOffset + 0x60);
  pan = file->readByte(readOffset + 0x61);

  if (version != 0x0410 || (channels != 1 && channels != 2) || interleave != 0x10 ||
      fileLength < kSadbHeaderSize + channels * kDspHeaderSize || fileLength > file->size() - readOffset ||
      dataOffset < kSadbHeaderSize + channels * kDspHeaderSize || dataOffset > fileLength ||
      encodedDataEnd < dataOffset || encodedDataEnd > fileLength || dspHeadersLength != channels * kDspHeaderSize ||
      encodedBytesPerChannel != (encodedDataEnd - dataOffset) / channels ||
      encodedBytesPerChannel * channels != encodedDataEnd - dataOffset || sampleCount == 0 || sampleRate == 0 ||
      volume > 127 || pan > 127) {
    return false;
  }

  char name[17]{};
  file->readBytes(readOffset + 0x20, 16, name);
  internalName = name;
  if (internalName.empty()) {
    internalName = "SADB Stream";
  }

  for (uint8_t channelIndex = 0; channelIndex < channels; ++channelIndex) {
    const uint32_t headerOffset = readOffset + kSadbHeaderSize + channelIndex * kDspHeaderSize;
    PSDSEDspStreamChannel& dsp = channel[channelIndex];
    dsp.sampleCount = file->readWordBE(headerOffset);
    dsp.nibbleCount = file->readWordBE(headerOffset + 0x04);
    dsp.sampleRate = file->readWordBE(headerOffset + 0x08);
    dsp.loopFlag = file->readShortBE(headerOffset + 0x0c);
    const uint16_t format = file->readShortBE(headerOffset + 0x0e);
    dsp.loopStart = file->readWordBE(headerOffset + 0x10);
    dsp.loopEnd = file->readWordBE(headerOffset + 0x14);
    const uint32_t initialAddress = file->readWordBE(headerOffset + 0x18);
    for (size_t coefficient = 0; coefficient < dsp.coefficients.size(); ++coefficient) {
      dsp.coefficients[coefficient] =
          static_cast<int16_t>(file->readShortBE(headerOffset + 0x1c + coefficient * 2));
    }
    dsp.initialPredictorScale = file->readShortBE(headerOffset + 0x3e);
    dsp.initialHistory1 = static_cast<int16_t>(file->readShortBE(headerOffset + 0x40));
    dsp.initialHistory2 = static_cast<int16_t>(file->readShortBE(headerOffset + 0x42));

    const uint64_t frameCount = (static_cast<uint64_t>(dsp.nibbleCount) + 15) / 16;
    const uint64_t encodedLength = frameCount * 8;
    const uint64_t decodedCount =
        (dsp.nibbleCount / 16) * 14 + std::max<int32_t>(static_cast<int32_t>(dsp.nibbleCount % 16) - 2, 0);
    const uint32_t alignedEncodedLength = static_cast<uint32_t>((encodedLength + 15) & ~uint64_t{15});
    if (dsp.sampleCount != sampleCount || dsp.sampleRate != sampleRate || dsp.nibbleCount < 2 || dsp.loopFlag > 1 ||
        format != 0 || initialAddress != 2 || decodedCount != sampleCount ||
        encodedLength > std::numeric_limits<uint32_t>::max() || alignedEncodedLength != encodedBytesPerChannel ||
        (channelIndex != 0 && (dsp.loopFlag != channel[0].loopFlag || dsp.loopStart != channel[0].loopStart ||
                               dsp.loopEnd != channel[0].loopEnd))) {
      return false;
    }
  }

  return true;
}

PSDSESADBSampColl::PSDSESADBSampColl(RawFile* file, const PSDSESADBHeader& header)
    : VGMSampColl(PSDSEFormat::name, file, header.offset, header.fileLength, header.internalName), m_header(header) {
}

bool PSDSESADBSampColl::parseHeader() {
  setLength(m_header.fileLength);
  VGMHeader* header = addHeader(offset(), kSadbHeaderSize, "SADB Header");
  header->addChild(offset(), 4, "Magic");
  header->addChild(offset() + 0x04, 4, "Zero Padding");
  header->addChild(offset() + 0x08, 4, "File Length");
  header->addChild(offset() + 0x0c, 2, "Version");
  header->addChild(offset() + 0x0e, 2, "File ID");
  header->addChild(offset() + 0x10, 8, "Zero Padding");
  header->addChild(offset() + 0x18, 8, "Creation Time");
  header->addChild(offset() + 0x20, 16, "File Name");
  header->addChild(offset() + 0x30, 1, "Codec");
  header->addChild(offset() + 0x31, 1, "Loop Flag");
  header->addChild(offset() + 0x32, 1, "Channel Count");
  header->addChild(offset() + 0x33, 1, "Zero Padding");
  header->addChild(offset() + 0x34, 2, "Authoring Metadata");
  header->addChild(offset() + 0x36, 1, "Encoded Bits per Sample");
  header->addChild(offset() + 0x37, 1, "Decoded Samples per Frame");
  header->addChild(offset() + 0x38, 2, "Stream Version");
  header->addChild(offset() + 0x3a, 2, "Interleave Size");
  header->addChild(offset() + 0x3c, 4, "Zero Padding");
  header->addChild(offset() + 0x40, 4, "Encoded Data End");
  header->addChild(offset() + 0x44, 4, "Encoded Bytes per Channel");
  header->addChild(offset() + 0x48, 4, "Encoded Data Offset");
  header->addChild(offset() + 0x4c, 4, "DSP Headers Length");
  header->addChild(offset() + 0x50, 4, "Sample Count");
  header->addChild(offset() + 0x54, 4, "Loop Start Byte Offset");
  header->addChild(offset() + 0x58, 4, "Loop End Byte Offset");
  header->addChild(offset() + 0x5c, 4, "Sample Rate");
  header->addChild(offset() + 0x60, 1, "Volume");
  header->addChild(offset() + 0x61, 1, "Pan");
  // [Disaster: Day of Crisis]: Header byte +0x62 is 8 for 1,852 streams, 2 for eight music streams, and 0 for one
  // jingle, so its observed values do not establish a priority ordering.
  // [Shiren the Wanderer]: Every audited stream stores 8 at +0x62, providing no independent semantic distinction.
  header->addChild(offset() + 0x62, 1, "Playback Parameter");
  header->addChild(offset() + 0x63, 1, "Zero Padding");
  header->addChild(offset() + 0x64, 0x1c, "Authoring Playback Metadata");

  for (uint8_t channelIndex = 0; channelIndex < m_header.channels; ++channelIndex) {
    const uint32_t channelOffset = offset() + kSadbHeaderSize + channelIndex * kDspHeaderSize;
    VGMHeader* dsp = addHeader(channelOffset, kDspHeaderSize, "Nintendo DSP Header");
    dsp->addChild(channelOffset, 4, "Sample Count");
    dsp->addChild(channelOffset + 0x04, 4, "Nibble Count");
    dsp->addChild(channelOffset + 0x08, 4, "Sample Rate");
    dsp->addChild(channelOffset + 0x0c, 2, "Loop Flag");
    dsp->addChild(channelOffset + 0x0e, 2, "Format");
    dsp->addChild(channelOffset + 0x10, 4, "Loop Start Nibble");
    dsp->addChild(channelOffset + 0x14, 4, "Loop End Nibble");
    dsp->addChild(channelOffset + 0x18, 4, "Initial Nibble");
    dsp->addChild(channelOffset + 0x1c, 0x20, "ADPCM Coefficients");
    dsp->addChild(channelOffset + 0x3c, 2, "Gain");
    dsp->addChild(channelOffset + 0x3e, 2, "Initial Predictor and Scale");
    dsp->addChild(channelOffset + 0x40, 2, "Initial History 1");
    dsp->addChild(channelOffset + 0x42, 2, "Initial History 2");
    dsp->addChild(channelOffset + 0x44, 2, "Loop Predictor and Scale");
    dsp->addChild(channelOffset + 0x46, 2, "Loop History 1");
    dsp->addChild(channelOffset + 0x48, 2, "Loop History 2");
    dsp->addChild(channelOffset + 0x4a, 0x16, "Encoder Metadata");
  }
  return true;
}

bool PSDSESADBSampColl::parseSampleInfo() {
  auto* sample = addSamp<PSDSESADBSamp>(this, m_header);
  sample->setVolume(m_header.volume / 127.0);
  sample->pan = m_header.pan;
  if (m_header.channel[0].loopFlag != 0) {
    const uint32_t loopStart = nibbleAddressToSample(m_header.channel[0].loopStart);
    const uint32_t loopEnd = nibbleAddressToSample(m_header.channel[0].loopEnd) + 1;
    if (loopStart < m_header.sampleCount && loopEnd > loopStart) {
      sample->setLoopStatus(1);
      sample->setLoopStartMeasure(LM_SAMPLES);
      sample->setLoopLengthMeasure(LM_SAMPLES);
      sample->setLoopOffset(loopStart);
      sample->setLoopLength(std::min(loopEnd, m_header.sampleCount) - loopStart);
    }
  }
  return true;
}

PSDSESADBSamp::PSDSESADBSamp(VGMSampColl* sampColl, const PSDSESADBHeader& header)
    : VGMSamp(sampColl, header.offset + kSadbHeaderSize, header.channels * kDspHeaderSize,
              header.offset + header.dataOffset, header.encodedBytesPerChannel * header.channels, header.channels,
              BPS::PCM16, header.sampleRate, header.internalName),
      m_header(header) {
  ulUncompressedSize = header.sampleCount * header.channels * sizeof(int16_t);
}

double PSDSESADBSamp::compressionRatio() const {
  return 3.5;
}

uint8_t PSDSESADBSamp::encodedByte(uint8_t channel, uint32_t channelOffset) const {
  const uint32_t block = channelOffset / m_header.interleave;
  const uint32_t withinBlock = channelOffset % m_header.interleave;
  const uint32_t physicalOffset =
      block * m_header.interleave * m_header.channels + channel * m_header.interleave + withinBlock;
  return readByte(dataOff + physicalOffset);
}

std::vector<uint8_t> PSDSESADBSamp::decodeToNativePcm() {
  const size_t outputSamples = static_cast<size_t>(m_header.sampleCount) * m_header.channels;
  if (outputSamples > std::numeric_limits<size_t>::max() / sizeof(int16_t)) {
    return {};
  }
  std::vector<uint8_t> decoded(outputSamples * sizeof(int16_t));

  for (uint8_t channelIndex = 0; channelIndex < m_header.channels; ++channelIndex) {
    const PSDSEDspStreamChannel& channel = m_header.channel[channelIndex];
    int32_t history1 = channel.initialHistory1;
    int32_t history2 = channel.initialHistory2;
    uint32_t outputOffset = 0;
    uint32_t inputOffset = 0;

    while (outputOffset < channel.sampleCount && inputOffset + 8 <= m_header.encodedBytesPerChannel) {
      const uint8_t frameHeader = encodedByte(channelIndex, inputOffset++);
      const uint8_t predictor = frameHeader >> 4;
      const uint8_t scale = frameHeader & 0x0f;
      if (predictor >= 8) {
        return {};
      }

      const int32_t coefficient1 = channel.coefficients[predictor * 2];
      const int32_t coefficient2 = channel.coefficients[predictor * 2 + 1];
      for (uint32_t frameSample = 0; frameSample < 14 && outputOffset < channel.sampleCount; ++frameSample) {
        const uint8_t packed = encodedByte(channelIndex, inputOffset + frameSample / 2);
        int32_t nibble = (frameSample & 1) == 0 ? packed >> 4 : packed & 0x0f;
        if (nibble >= 8) {
          nibble -= 16;
        }

        const int64_t residual = static_cast<int64_t>(nibble) * (int64_t{1} << scale) * 2048;
        const int64_t predicted =
            static_cast<int64_t>(coefficient1) * history1 + static_cast<int64_t>(coefficient2) * history2;
        const int32_t pcm =
            static_cast<int32_t>(std::clamp<int64_t>((residual + predicted + 1024) >> 11, -32768, 32767));
        const size_t sampleIndex = static_cast<size_t>(outputOffset++) * m_header.channels + channelIndex;
        decoded[sampleIndex * 2] = static_cast<uint8_t>(pcm & 0xff);
        decoded[sampleIndex * 2 + 1] = static_cast<uint8_t>((pcm >> 8) & 0xff);
        history2 = history1;
        history1 = pcm;
      }
      inputOffset += 7;
    }

    if (outputOffset != channel.sampleCount) {
      return {};
    }
  }

  return decoded;
}
