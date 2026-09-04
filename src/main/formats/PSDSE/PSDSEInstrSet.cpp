#include "PSDSEInstrSet.h"
#include "PSDSEFormat.h"
#include "VGMSamp.h"
#include "VGMSampColl.h"
#include "VGMColl.h"
#include "RawFile.h"
#include "LogManager.h"
#include "ScaleConversion.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include "PSDSEEnvelopeCurve.h"
#include "PSDSESeq.h"

namespace {
double dsePitchLfoDepthCents(int32_t amplitude) {
  // [Pokemon Mystery Dungeon: Explorers of Sky]: The DSE driver shifts amplitude left by 10, then shifts waveform
  // output right by 8 before adding it to pitch in 1/256-semitone units.
  return std::abs(static_cast<double>(amplitude)) * 25.0 / 16.0;
}

double dseFullTriangleFrequencyHz(uint16_t periodMs) {
  // Full-triangle direction changes once per period, so one cycle is two
  // phase periods.
  return periodMs == 0 ? 0.0 : 1000.0 / (2.0 * periodMs);
}
}  // namespace

// ***************
// PSDSEInstrSet
// ***************

PSDSEInstrSet::PSDSEInstrSet(RawFile* file, const SWDLHeader& header)
    : VGMInstrSet("PSDSE", file, header.offset, 0, header.intName), m_header(header) {
}

bool PSDSEInstrSet::parseHeader() {
  uint32_t flen = PSDSE::readU32(rawFile(), offset() + 0x08, m_header.endianness);
  if (flen > rawFile()->size() - offset()) {
    flen = rawFile()->size() - offset();
  }
  setLength(flen);

  VGMHeader* header = addHeader(offset(), 0x50, "SWDL Header");
  header->addChild(offset(), 4, "Magic");
  // [Pokemon Mystery Dungeon: Explorers of Sky]: DseFile_CheckHeader skips +0x04 and +0x10.
  // [Line Attack Heroes]: SsdCheckDataHeader skips +0x04 and +0x10. Every audited file stores zeroes in both ranges.
  header->addChild(offset() + 0x04, 4, "Zero Padding");
  header->addChild(offset() + 0x08, 4, "File Length");
  header->addChild(offset() + 0x0C, 2, "Version");
  header->addChild(offset() + 0x0E, 2, "File ID");
  header->addChild(offset() + 0x10, 8, "Zero Padding");
  header->addChild(offset() + 0x18, 2, "Creation Year");
  header->addChild(offset() + 0x1A, 1, "Creation Month");
  header->addChild(offset() + 0x1B, 1, "Creation Day");
  header->addChild(offset() + 0x1C, 1, "Creation Hour");
  header->addChild(offset() + 0x1D, 1, "Creation Minute");
  header->addChild(offset() + 0x1E, 1, "Creation Second");
  header->addChild(offset() + 0x1F, 1, "Creation Centisecond");
  header->addChild(offset() + 0x20, 16, "File Name");
  // [Pokemon Mystery Dungeon: Explorers of Sky]: The bank loader skips these four authoring words.
  // [Luminous Arc]: Version 0x0402 banks store 0xffffff00 at +0x30, and the runtime does not consume these words.
  // [Pokemon Mystery Dungeon: Explorers of Sky]: Version 0x0415 banks generally store 0xaaaaaa00 at +0x30. The
  // remaining authoring values vary across shipped files and are not consumed by the runtime.
  header->addChild(offset() + 0x30, 4, "Version Authoring Marker");
  header->addChild(offset() + 0x34, 4, "Authoring Metadata 1");
  header->addChild(offset() + 0x38, 4, "Authoring Metadata 2");
  header->addChild(offset() + 0x3C, 4, "Authoring Metadata 3");

  if (m_header.version == 0x0402) {
    // [Luminous Arc]: The version 0x0402 driver reads byte-sized counts at +0x46 through +0x48.
    header->addChild(offset() + 0x40, 6, "Zero Padding");
    header->addChild(offset() + 0x46, 1, "Wave Slot Count");
    header->addChild(offset() + 0x47, 1, "Program Slot Count");
    header->addChild(offset() + 0x48, 1, "Keygroup Count");
    header->addChild(offset() + 0x49, 7, "0xFF Padding");
  } else {
    // [Pokemon Mystery Dungeon: Explorers of Sky]: DseSwd_LoadBank reads these fields. For storage kind 2, the low
    // halfword at +0x40 is a main waveform bank ID rather than an embedded PCMD byte count.
    if (m_header.sampleStorageKind == 2) {
      header->addChild(offset() + 0x40, 2, "Main Waveform Bank ID");
      header->addChild(offset() + 0x42, 2, "External PCMD Marker");
    } else {
      header->addChild(offset() + 0x40, 4, "PCMD Length");
    }
    header->addChild(offset() + 0x44, 2, "Zero Padding");
    header->addChild(offset() + 0x46, 2, "Wave Slot Count");
    header->addChild(offset() + 0x48, 2, "Program Slot Count");
    header->addChild(offset() + 0x4A, 1, "Keygroup Count");
    header->addChild(offset() + 0x4B, 1, "Sample Storage Kind");
    header->addChild(offset() + 0x4C, 4, "WAVI Length");
  }

  return true;
}

bool PSDSEInstrSet::parseInstrPointers() {
  // [Pokemon Mystery Dungeon: Explorers of Sky]: BGM.SWD has no prgi chunk and functions only as a sample library
  // for program banks, so it defines no implicit instruments.
  if (m_header.prgiOffset == 0) {
    L_INFO("PSDSE: no prgi chunk (sample bank), skipping instrument creation");
    return true;
  }

  uint32_t chunkDataStart = m_header.prgiOffset + 0x10;
  uint32_t prgiChunkEnd = m_header.prgiOffset + 0x10 + m_header.prgiSize;

  L_DEBUG("PSDSE: parsing {} prgi slots at {:x}, prgi ends at {:x}", m_header.nbprgislots, chunkDataStart,
          prgiChunkEnd);

  uint32_t fileSize = rawFile()->size();

  for (int i = 0; i < m_header.nbprgislots; i++) {
    // Ensure we don't read beyond file bounds for the pointer table
    if (chunkDataStart + i * 2 + 2 > fileSize) {
      break;
    }

    uint16_t ptroffset = PSDSE::readU16(rawFile(), chunkDataStart + i * 2, m_header.endianness);
    if (ptroffset != 0) {
      uint32_t instrOffset = chunkDataStart + ptroffset;

      // Validate instrument offset is within prgi chunk bounds
      if (instrOffset >= prgiChunkEnd) {
        L_WARN("PSDSE: instr {} has invalid offset {:x} (prgi ends {:x}), skipping", i, instrOffset, prgiChunkEnd);
        continue;
      }

      L_DEBUG("PSDSE: found instr {} at {:x} (ptr {:x})", i, instrOffset, ptroffset);

      addInstr<PSDSEInstr>(this, instrOffset, 1, m_header.id, i);
      // FluidSynth always addresses MIDI channel 10 through SF2 bank 128.
      // DSE can select any preset on that channel, so mirror every available
      // preset rather than assuming only program 127 is percussion.
      addInstr<PSDSEInstr>(this, instrOffset, 1, 128, i);
    }
  }

  L_INFO("PSDSE: total instruments found: {}", instrCount());

  return true;
}

void PSDSEInstrSet::useColl(const VGMColl* coll) {
  const auto* seq = dynamic_cast<const PSDSESeq*>(coll->seq());
  if (seq == nullptr || !seq->hasPatchReferences()) {
    return;
  }

  std::vector<VGMInstr*> usedInstruments;
  for (VGMInstr* instr : instrs()) {
    if (seq->referencesPatch(instr->bank, instr->instrNum)) {
      usedInstruments.push_back(instr);
    }
  }
  if (!usedInstruments.empty()) {
    // [Pokemon Fushigi no Dungeon: Ikuzo! Arashi no Boukendan]: SE.SWD contains more than 36,000 regions, while each
    // sequence selects only a subset of its programs. Exporting the complete bank exceeds the 16-bit SoundFont ibag
    // and igen indexes.
    setExportInstrs(std::move(usedInstruments));
  }
}

// ***************
// PSDSESampColl
// ***************

PSDSESampColl::PSDSESampColl(const std::string& format, RawFile* rawfile, const SWDLHeader& header, std::string theName)
    : VGMSampColl(format, rawfile, header.offset, 0, theName), m_header(header) {
}

bool PSDSESampColl::parseHeader() {
  setLength(m_header.fileLength);
  return true;
}

// From http://nocash.emubase.de/gbatek.htm#dssound
static const unsigned AdpcmTable[89] = {
    0x0007, 0x0008, 0x0009, 0x000A, 0x000B, 0x000C, 0x000D, 0x000E, 0x0010, 0x0011, 0x0013, 0x0015, 0x0017,
    0x0019, 0x001C, 0x001F, 0x0022, 0x0025, 0x0029, 0x002D, 0x0032, 0x0037, 0x003C, 0x0042, 0x0049, 0x0050,
    0x0058, 0x0061, 0x06B,  0x0076, 0x0082, 0x008F, 0x009D, 0x00AD, 0x00BE, 0x00D1, 0x00E6, 0x00FD, 0x0117,
    0x0133, 0x0151, 0x0173, 0x0198, 0x01C1, 0x01EE, 0x0220, 0x0256, 0x0292, 0x02D4, 0x031C, 0x036C, 0x03C3,
    0x0424, 0x048E, 0x0502, 0x0583, 0x0610, 0x06AB, 0x0756, 0x0812, 0x08E0, 0x09C3, 0x0ABD, 0x0BD0, 0x0CFF,
    0x0E4C, 0x0FBA, 0x114C, 0x1307, 0x14EE, 0x1706, 0x1954, 0x1BDC, 0x1EA5, 0x21B6, 0x2515, 0x28CA, 0x2CDF,
    0x315B, 0x364B, 0x3BB9, 0x41B2, 0x4844, 0x4F7E, 0x5771, 0x602F, 0x69CE, 0x7462, 0x7FFF};

static constexpr int IMA_IndexTable[9] = {-1, -1, -1, -1, 2, 4, 6, 8};

bool PSDSESampColl::parseSampleInfo() {
  uint32_t wavTableOffset = m_header.waviOffset + 0x10;

  int numSlots = m_header.nbwavislots;
  if (numSlots == 0) {
    numSlots = 128;  // fallback
  }
  initializeSampleSlots(numSlots);

  for (int i = 0; i < numSlots; i++) {
    uint16_t relativeOffset = PSDSE::readU16(rawFile(), wavTableOffset + i * 2, m_header.endianness);

    if (relativeOffset != 0) {
      uint32_t sampleInfoOffset = wavTableOffset + relativeOffset;

      uint16_t smplfmt = 0;
      uint32_t smplRate = 0;
      uint32_t smplPos = 0;
      uint32_t loopBeg = 0;
      uint32_t loopLen = 0;
      uint8_t loopFlag = 0;
      uint8_t rootKey = 60;
      uint8_t volume = 127;
      uint8_t pan = 0;
      uint32_t dspSampleCount = 0;
      uint32_t dspNibbleCount = 0;
      uint32_t dspLoopStart = 0;
      uint32_t dspLoopEnd = 0;
      std::array<int16_t, 16> dspCoefficients{};
      int16_t dspInitialHistory1 = 0;
      int16_t dspInitialHistory2 = 0;

      std::string sampleName = fmt::format("Sample {}", i);

      if (!m_header.intName.empty()) {
        if (m_header.nbwavislots == 1 || m_header.intName.find(fmt::format("{}", i)) != std::string::npos) {
          sampleName = m_header.intName;
        }
      }

      if (m_header.version == 0x0402) {
        // [Luminous Arc]: Version 0x0402 sample records use this field layout.
        rootKey = readByte(sampleInfoOffset + 0x04);
        volume = readByte(sampleInfoOffset + 0x06);
        pan = readByte(sampleInfoOffset + 0x07);
        smplfmt = PSDSE::readU16(rawFile(), sampleInfoOffset + 0x08, m_header.endianness);
        loopFlag = readByte(sampleInfoOffset + 0x11);
        smplRate = PSDSE::readU16(rawFile(), sampleInfoOffset + 0x12, m_header.endianness);
        smplPos = PSDSE::readU32(rawFile(), sampleInfoOffset + 0x14, m_header.endianness);
        loopBeg = PSDSE::readU32(rawFile(), sampleInfoOffset + 0x18, m_header.endianness);
        loopLen = PSDSE::readU32(rawFile(), sampleInfoOffset + 0x1C, m_header.endianness);
      } else {
        // [Pokemon Mystery Dungeon: Explorers of Sky]: Version 0x0415 sample records use this field layout.
        rootKey = readByte(sampleInfoOffset + 0x06);
        volume = readByte(sampleInfoOffset + 0x08);
        pan = readByte(sampleInfoOffset + 0x09);
        smplfmt = PSDSE::readU16(rawFile(), sampleInfoOffset + 0x12, m_header.endianness);
        loopFlag = readByte(sampleInfoOffset + 0x15);
        smplRate = PSDSE::readU32(rawFile(), sampleInfoOffset + 0x20, m_header.endianness);
        smplPos = PSDSE::readU32(rawFile(), sampleInfoOffset + 0x24, m_header.endianness);
        loopBeg = PSDSE::readU32(rawFile(), sampleInfoOffset + 0x28, m_header.endianness);
        loopLen = PSDSE::readU32(rawFile(), sampleInfoOffset + 0x2C, m_header.endianness);
      }

      // Calculate absolute data offset
      uint32_t dataOffset = 0;
      if (m_header.pcmdOffset != 0) {
        dataOffset = m_header.pcmdOffset + 0x10 + smplPos;
      } else {
        continue;
      }

      // [Line Attack Heroes]: Big-endian SWDB banks place a 0x60-byte Nintendo DSP-ADPCM header at each PCMD position.
      // [Pokemon Fushigi no Dungeon: Ikuzo! Arashi no Boukendan]: Retail SWDB banks use the same DSP-ADPCM layout.
      // NDS sample lengths instead use 32-bit words.
      uint32_t dataLength = (loopBeg + loopLen) * 4;

      BPS bps = BPS::PCM16;
      uint8_t waveType = PSDSESamp::PCM16;

      if (m_header.endianness == Endianness::Big && m_header.version == 0x0415 && smplfmt == 0x0100) {
        constexpr uint32_t dspHeaderSize = 0x60;
        if (dataOffset > rawFile()->size() || dspHeaderSize > rawFile()->size() - dataOffset) {
          continue;
        }

        dspSampleCount = rawFile()->readWordBE(dataOffset);
        dspNibbleCount = rawFile()->readWordBE(dataOffset + 0x04);
        const uint32_t dspSampleRate = rawFile()->readWordBE(dataOffset + 0x08);
        const uint16_t dspLoopFlag = rawFile()->readShortBE(dataOffset + 0x0C);
        const uint16_t dspFormat = rawFile()->readShortBE(dataOffset + 0x0E);
        dspLoopStart = rawFile()->readWordBE(dataOffset + 0x10);
        dspLoopEnd = rawFile()->readWordBE(dataOffset + 0x14);
        if (dspSampleCount == 0 || dspNibbleCount < 2 || dspLoopFlag > 1 || dspFormat != 0 ||
            dspSampleRate != smplRate) {
          continue;
        }

        const uint64_t frameCount = (static_cast<uint64_t>(dspNibbleCount) + 15) / 16;
        const uint64_t encodedLength = frameCount * 8;
        const uint64_t decodedSampleCount =
            (dspNibbleCount / 16) * 14 + std::max<int32_t>(static_cast<int32_t>(dspNibbleCount % 16) - 2, 0);
        if (encodedLength > std::numeric_limits<uint32_t>::max() || decodedSampleCount != dspSampleCount ||
            dspSampleCount > std::numeric_limits<uint32_t>::max() / sizeof(int16_t)) {
          continue;
        }
        loopFlag = static_cast<uint8_t>(dspLoopFlag);
        dataOffset += dspHeaderSize;
        dataLength = static_cast<uint32_t>(encodedLength);
        waveType = PSDSESamp::DSP_ADPCM;
        for (size_t coefficient = 0; coefficient < dspCoefficients.size(); ++coefficient) {
          dspCoefficients[coefficient] =
              static_cast<int16_t>(rawFile()->readShortBE(dataOffset - dspHeaderSize + 0x1C + coefficient * 2));
        }
        dspInitialHistory1 = static_cast<int16_t>(rawFile()->readShortBE(dataOffset - dspHeaderSize + 0x40));
        dspInitialHistory2 = static_cast<int16_t>(rawFile()->readShortBE(dataOffset - dspHeaderSize + 0x42));
      } else if (smplfmt == 0x0200) {
        waveType = PSDSESamp::IMA_ADPCM;
        dataOffset += 4;
        dataLength -= 4;  // Subtract header size from total length
      } else if (smplfmt == 0x0000) {
        // PCM8
        waveType = PSDSESamp::PCM8;
        bps = BPS::PCM8;
      }

      const uint32_t bankEnd = m_header.offset + m_header.fileLength;
      if (dataOffset >= bankEnd) {
        continue;
      }
      if (dataLength > bankEnd - dataOffset) {
        if (waveType == PSDSESamp::DSP_ADPCM) {
          continue;
        }
        dataLength = bankEnd - dataOffset;
      }

      PSDSESamp* samp = addSamp<PSDSESamp>(this, sampleInfoOffset, 64, dataOffset, dataLength, 1, bps, smplRate,
                                           waveType, sampleName);
      if (waveType == PSDSESamp::DSP_ADPCM) {
        samp->configureDspAdpcm(dspCoefficients, dspSampleCount, dspInitialHistory1, dspInitialHistory2);
      }
      mapSampleSlot(i, sampleCount() - 1);
      samp->unityKey = rootKey;
      samp->pan = pan;
      samp->setVolume(volume / 127.0);
      if (loopFlag) {
        samp->setLoopStatus(1);

        if (waveType == PSDSESamp::DSP_ADPCM) {
          const auto nibbleAddressToSample = [](uint32_t address) {
            const uint32_t frameSamples = (address / 16) * 14;
            const uint32_t frameNibble = address % 16;
            return frameSamples + (frameNibble > 2 ? frameNibble - 2 : 0);
          };
          const uint32_t loopStartSample = nibbleAddressToSample(dspLoopStart);
          const uint32_t loopEndSample = nibbleAddressToSample(dspLoopEnd) + 1;
          if (loopStartSample < dspSampleCount && loopEndSample > loopStartSample) {
            samp->setLoopStartMeasure(LM_SAMPLES);
            samp->setLoopLengthMeasure(LM_SAMPLES);
            samp->setLoopOffset(loopStartSample);
            samp->setLoopLength(std::min(loopEndSample, dspSampleCount) - loopStartSample);
          } else {
            samp->setLoopStatus(0);
          }
          samp->ulUncompressedSize = dspSampleCount * sizeof(int16_t);
        } else if (waveType == PSDSESamp::IMA_ADPCM) {
          samp->setLoopStartMeasure(LM_SAMPLES);
          samp->setLoopLengthMeasure(LM_SAMPLES);

          uint32_t loopStartSamples;
          if (loopBeg == 0) {
            loopStartSamples = 0;
          } else {
            loopStartSamples = 1 + (loopBeg - 1) * 8;
          }

          uint32_t loopLenSamples = loopLen * 8;
          uint32_t totalSamples = dataLength * 2 + 1;

          if (loopStartSamples + loopLenSamples > totalSamples) {
            loopLenSamples = totalSamples - loopStartSamples;
          }

          samp->setLoopOffset(loopStartSamples);
          samp->setLoopLength(loopLenSamples);
          samp->ulUncompressedSize = totalSamples * 2;
        } else {
          samp->setLoopStartMeasure(LM_BYTES);
          samp->setLoopLengthMeasure(LM_BYTES);
          samp->setLoopOffset(loopBeg * 4);
          samp->setLoopLength(loopLen * 4);
        }
      } else {
        samp->setLoopStatus(0);
      }
    }
  }

  return true;
}

// ************
// PSDSESamp
// ************

PSDSESamp::PSDSESamp(VGMSampColl* sampColl, uint32_t offset, uint32_t length, uint32_t dataOffset, uint32_t dataLength,
                     uint8_t nChannels, BPS bps, uint32_t theRate, uint8_t theWaveType, std::string name)
    : VGMSamp(sampColl, offset, length, dataOffset, dataLength, nChannels, bps, theRate, std::move(name)),
      waveType(theWaveType) {
}

double PSDSESamp::compressionRatio() const {
  if (waveType == IMA_ADPCM || waveType == DSP_ADPCM) {
    return 4.0;
  }
  return 1.0;
}

std::vector<uint8_t> PSDSESamp::decodeToNativePcm() {
  if (waveType == IMA_ADPCM) {
    return decodeImaAdpcm();
  }
  if (waveType == DSP_ADPCM) {
    return decodeDspAdpcm();
  }
  return VGMSamp::decodeToNativePcm();
}

void PSDSESamp::configureDspAdpcm(std::array<int16_t, 16> coefficients, uint32_t sampleCount, int16_t initialHistory1,
                                  int16_t initialHistory2) {
  m_dspCoefficients = coefficients;
  m_dspSampleCount = sampleCount;
  m_dspInitialHistory1 = initialHistory1;
  m_dspInitialHistory2 = initialHistory2;
  ulUncompressedSize = sampleCount * sizeof(int16_t);
}

std::vector<uint8_t> PSDSESamp::decodeDspAdpcm() {
  std::vector<uint8_t> decoded(static_cast<size_t>(m_dspSampleCount) * sizeof(int16_t));
  int32_t history1 = m_dspInitialHistory1;
  int32_t history2 = m_dspInitialHistory2;
  uint32_t outputOffset = 0;
  uint32_t inputOffset = 0;

  while (outputOffset < m_dspSampleCount && inputOffset + 8 <= dataLength) {
    const uint8_t frameHeader = readByte(dataOff + inputOffset++);
    const uint8_t predictor = frameHeader >> 4;
    const uint8_t scale = frameHeader & 0x0F;
    if (predictor >= 8) {
      return {};
    }

    const int32_t coefficient1 = m_dspCoefficients[predictor * 2];
    const int32_t coefficient2 = m_dspCoefficients[predictor * 2 + 1];
    for (size_t frameSample = 0; frameSample < 14 && outputOffset < m_dspSampleCount; ++frameSample) {
      const uint8_t packed = readByte(dataOff + inputOffset + frameSample / 2);
      int32_t nibble = (frameSample & 1) == 0 ? packed >> 4 : packed & 0x0F;
      if (nibble >= 8) {
        nibble -= 16;
      }

      const int64_t residual = static_cast<int64_t>(nibble) * (int64_t{1} << scale) * 2048;
      const int64_t predicted =
          static_cast<int64_t>(coefficient1) * history1 + static_cast<int64_t>(coefficient2) * history2;
      const int32_t sample =
          static_cast<int32_t>(std::clamp<int64_t>((residual + predicted + 1024) >> 11, -32768, 32767));
      const size_t byteOffset = static_cast<size_t>(outputOffset++) * sizeof(int16_t);
      decoded[byteOffset] = static_cast<uint8_t>(sample & 0xFF);
      decoded[byteOffset + 1] = static_cast<uint8_t>((sample >> 8) & 0xFF);
      history2 = history1;
      history1 = sample;
    }
    inputOffset += 7;
  }

  if (outputOffset != m_dspSampleCount) {
    return {};
  }
  return decoded;
}

// Helper macros for ADPCM
#define IMAMax(samp) (samp > 32767) ? 32767 : samp
#define IMAMin(samp) (samp < -32768) ? -32768 : samp
#define IMAIndexMinMax(index, min, max) (index > max) ? max : ((index < min) ? min : index)

void PSDSESamp::process_nibble(unsigned char data4bit, int& Index, int& Pcm16bit) {
  int Diff = AdpcmTable[Index] / 8;
  if (data4bit & 1) {
    Diff = Diff + AdpcmTable[Index] / 4;
  }
  if (data4bit & 2) {
    Diff = Diff + AdpcmTable[Index] / 2;
  }
  if (data4bit & 4) {
    Diff = Diff + AdpcmTable[Index] / 1;
  }

  if ((data4bit & 8) == 0) {
    Pcm16bit = IMAMax(Pcm16bit + Diff);
  }
  if ((data4bit & 8) == 8) {
    Pcm16bit = IMAMin(Pcm16bit - Diff);
  }
  Index = IMAIndexMinMax(Index + IMA_IndexTable[data4bit & 7], 0, 88);
}

void PSDSESamp::clamp_step_index(int& stepIndex) {
  if (stepIndex < 0) {
    stepIndex = 0;
  }
  if (stepIndex > 88) {
    stepIndex = 88;
  }
}

void PSDSESamp::clamp_sample(int& decompSample) {
  if (decompSample < -32768) {
    decompSample = -32768;
  }
  if (decompSample > 32767) {
    decompSample = 32767;
  }
}

std::vector<uint8_t> PSDSESamp::decodeImaAdpcm() {
  // dataLength is the size of the compressed data (after header).
  // Each byte of compressed data contains 2 nibbles, each nibble produces 1 sample.
  // Header provides 1 initial sample.
  // Total samples = dataLength * 2 + 1.
  const uint32_t sampleCount = dataLength * 2 + 1;
  std::vector<uint8_t> samples(sampleCount * sizeof(int16_t));
  auto* output = reinterpret_cast<int16_t*>(samples.data());
  uint32_t destOff = 0;

  // The header (initial PCM and index) is located 4 bytes *before* dataOff.
  // dataOff was adjusted in parseSampleInfo to point to the actual compressed nibbles.
  uint32_t sampHeader = getWord(dataOff - 4);
  int decompSample = static_cast<int16_t>(sampHeader & 0xFFFF);  // Initial PCM sample
  int stepIndex = (sampHeader >> 16) & 0x7F;                     // Initial step index (7-bit)

  uint32_t curOffset = dataOff;

  // The first sample is the initial PCM sample from the header.
  output[destOff++] = static_cast<int16_t>(decompSample);

  uint8_t compByte;
  // dataLength is bytes of compressed data.
  while (curOffset < dataOff + dataLength) {
    compByte = readByte(curOffset++);

    // Process lower nibble first
    process_nibble(compByte & 0x0F, stepIndex, decompSample);
    output[destOff++] = static_cast<int16_t>(decompSample);

    // Process upper nibble
    process_nibble((compByte & 0xF0) >> 4, stepIndex, decompSample);
    output[destOff++] = static_cast<int16_t>(decompSample);
  }

  return samples;
}

// ************
// PSDSEInstr
// ************

PSDSEInstr::PSDSEInstr(VGMInstrSet* instr_set, uint32_t offset, uint32_t length, uint32_t bank, uint32_t instr_num)
    : VGMInstr(instr_set, offset, length, bank, instr_num, "Instrument") {
}

bool PSDSEInstr::loadInstr() {
  auto* pInstrSet = static_cast<PSDSEInstrSet*>(parInstrSet);
  uint32_t splitsOffset = 0;
  uint32_t lfoTableOffset = 0;
  uint16_t nbsplits = 0;
  uint8_t nblfos = 0;

  // [Pokemon Mystery Dungeon: Explorers of Sky]: DseVoice_PlayNote multiplies this program value with note velocity
  // and split volume before the mixer applies square-law gain.
  volume = readByte(offset() + 0x04);
  addChild(offset() + 0x04, 1, "Program Volume");

  if (pInstrSet->m_header.version == 0x0402) {
    // [Luminous Arc]: Version 0x0402 program records use this field layout.
    // 0x00 ID (1)
    // 0x01 nbsplits (1)
    // 0xD0 SplitsTbl
    nbsplits = readByte(offset() + 0x01);
    nblfos = readByte(offset() + 0x0B);
    splitsOffset = offset() + 0xD0;
    lfoTableOffset = offset() + 0x90;
  } else {
    // v415 structure
    nbsplits = PSDSE::readU16(rawFile(), offset() + 0x02, pInstrSet->m_header.endianness);
    nblfos = readByte(offset() + 0x0B);
    splitsOffset = offset() + 0x10 + (nblfos * 16) + 16;
    lfoTableOffset = offset() + 0x10;
  }

  setLength(splitsOffset + (nbsplits * 48) - offset());

  for (int i = 0; i < nblfos; i++) {
    // [Pokemon Mystery Dungeon: Explorers of Sky]: The DSE driver reads this 16-byte LFO structure.
    // [Line Attack Heroes]: SWDB banks store the structure fields in container byte order, unlike mixed-order
    // sequence operands.
    uint32_t currLfo = lfoTableOffset + (i * 16);
    PSDSELFO lfo;
    lfo.mode = readByte(currLfo + 0x01);
    lfo.destination = readByte(currLfo + 0x02);
    lfo.waveform = readByte(currLfo + 0x03);
    lfo.amplitude = static_cast<int32_t>(PSDSE::readU32(rawFile(), currLfo + 0x04, pInstrSet->m_header.endianness));
    lfo.periodMs = PSDSE::readU16(rawFile(), currLfo + 0x08, pInstrSet->m_header.endianness);
    lfo.delayMs = PSDSE::readU16(rawFile(), currLfo + 0x0A, pInstrSet->m_header.endianness);
    lfo.fadeMs = PSDSE::readU16(rawFile(), currLfo + 0x0C, pInstrSet->m_header.endianness);
    lfos.push_back(lfo);
  }

  for (int i = 0; i < nbsplits; i++) {
    uint32_t splitOff = splitsOffset + i * 48;
    PSDSERgn* rgn = addRgn<PSDSERgn>(this, splitOff);
    rgn->loadRgn();
  }

  return true;
}

// ************
// PSDSERgn
// ************

PSDSERgn::PSDSERgn(VGMInstr* instr, uint32_t offset) : VGMRgn(instr, offset, 48, "Region") {
}

bool PSDSERgn::loadRgn() {
  auto* pInstrSet = static_cast<PSDSEInstrSet*>(parInstr->parInstrSet);

  uint8_t lowKey, hiKey, lowVel, hiVel;
  uint16_t smplID;
  uint8_t ftune;
  int8_t ctune = 0;
  uint8_t rootKey, vol, pan;
  uint8_t attack, decay, sustain, release;
  uint8_t envon = 0, envmult = 0, attackBegin = 0, hold = 0, decay2 = 0;

  if (pInstrSet->m_header.version == 0x0402) {
    // [Luminous Arc]: Version 0x0402 split records use this field layout.
    // 0x04 lowkey
    // 0x05 hikey
    // 0x08 lovel
    // 0x09 hivel
    // 0x11 SmplID (1 byte!)
    // 0x12 ftune
    // 0x13 ctune
    // 0x14 rootkey
    // 0x16 smplvol
    // 0x17 smplpan
    // 0x28 atkvol
    // 0x29 attack
    // 0x2A decay
    // 0x2B sustain
    // 0x2E release

    addChild(offset() + 0x00, 2, "ID");
    addChild(offset() + 0x02, 1, "Bend Sensitivity");
    addChild(offset() + 0x03, 1, "Unused Split Flag");
    lowKey = readByte(offset() + 0x04);
    addChild(offset() + 0x04, 1, "Low Key");
    hiKey = readByte(offset() + 0x05);
    addChild(offset() + 0x05, 1, "High Key");
    // [Luminous Arc]: The version 0x0402 split selector reads +0x04/+0x05 and +0x08/+0x09. The banks mirror those
    // ranges at +0x06/+0x07 and +0x0a/+0x0b, but the retail driver does not read the mirrors.
    addChild(offset() + 0x06, 1, "Mirror Low Key (Unused)");
    addChild(offset() + 0x07, 1, "Mirror High Key (Unused)");
    lowVel = readByte(offset() + 0x08);
    addChild(offset() + 0x08, 1, "Low Velocity");
    hiVel = readByte(offset() + 0x09);
    addChild(offset() + 0x09, 1, "High Velocity");
    addChild(offset() + 0x0A, 1, "Mirror Low Velocity (Unused)");
    addChild(offset() + 0x0B, 1, "Mirror High Velocity (Unused)");
    addChild(offset() + 0x0C, 5, "Padding");

    smplID = readByte(offset() + 0x11);
    addChild(offset() + 0x11, 1, "Sample ID");

    ftune = readByte(offset() + 0x12);
    addChild(offset() + 0x12, 1, "Fine Tune");
    ctune = readByte(offset() + 0x13);
    addChild(offset() + 0x13, 1, "Coarse Tune");
    rootKey = readByte(offset() + 0x14);
    addChild(offset() + 0x14, 1, "Root Key");
    addChild(offset() + 0x15, 1, "Key Transpose");

    vol = readByte(offset() + 0x16);
    addChild(offset() + 0x16, 1, "Sample Volume");
    pan = readByte(offset() + 0x17);
    addChild(offset() + 0x17, 1, "Sample Pan");
    addChild(offset() + 0x18, 1, "Key Group ID");
    addChild(offset() + 0x19, 5, "Unused Sample Metadata");
    envon = readByte(offset() + 0x1E);
    addChild(offset() + 0x1E, 1, "Envelope On");
    envmult = readByte(offset() + 0x1F);
    addChild(offset() + 0x1F, 1, "Envelope Multiplier");
    // [Luminous Arc]: Split records store the invariant metadata constants 0x0301 and 0xffffff03 at +0x22 and +0x24,
    // matching version 0x0415 records. The ADSR implementation does not read these constants.
    addChild(offset() + 0x20, 2, "Unused Envelope Metadata 1");
    addChild(offset() + 0x22, 2, "Unused Envelope Metadata 2");
    addChild(offset() + 0x24, 4, "Unused Envelope Metadata 3");
    attackBegin = readByte(offset() + 0x28);
    addChild(offset() + 0x28, 1, "Attack Begin");

    attack = readByte(offset() + 0x29);
    addChild(offset() + 0x29, 1, "Attack Time");
    decay = readByte(offset() + 0x2A);
    addChild(offset() + 0x2A, 1, "Decay Time");
    sustain = readByte(offset() + 0x2B);
    addChild(offset() + 0x2B, 1, "Sustain Level");
    hold = readByte(offset() + 0x2C);
    addChild(offset() + 0x2C, 1, "Hold Time");
    decay2 = readByte(offset() + 0x2D);
    addChild(offset() + 0x2D, 1, "Sustain Time");
    release = readByte(offset() + 0x2E);
    addChild(offset() + 0x2E, 1, "Release Time");
    addChild(offset() + 0x2F, 1, "Unused Envelope Metadata 4");
  } else {
    // v415 split entry is 48 bytes
    // 0x00 ID (2)
    // 0x02 bndrng (1)
    // 0x03 unk (1)
    // 0x04 lowkey (1)
    // 0x05 hikey (1)
    // 0x06 lowkey2 (1)
    // 0x07 hikey2 (1)
    // 0x08 lovel (1)
    // 0x09 hivel (1)
    // 0x0A lovel2 (1)
    // 0x0B hivel2 (1)
    // 0x0C padding (4)
    // 0x10 padding (2)
    // 0x12 SmplID (2) - Index in wavi table
    // 0x14 ftune (1) - uint8
    // 0x15 ctune (1) - int8
    // 0x16 rootkey (1) - int8
    // 0x17 ktps (1) - int8 (Key Transpose)
    // 0x18 smplvol (1) - int8
    // 0x19 smplpan (1) - int8
    // 0x1A kgrpid (1)
    // ...

    addChild(offset() + 0x00, 2, "ID");
    addChild(offset() + 0x02, 1, "Bend Sensitivity");
    addChild(offset() + 0x03, 1, "Unused Split Flag");
    lowKey = readByte(offset() + 0x04);
    addChild(offset() + 0x04, 1, "Low Key");
    hiKey = readByte(offset() + 0x05);
    addChild(offset() + 0x05, 1, "High Key");
    // [Luminous Arc]: The version 0x0402 driver selects splits with +0x04/+0x05 and +0x08/+0x09 only.
    // [Fushigi no Dungeon: Fuurai no Shiren 5: Fortune Tower to Unmei no Dice]: The ARM Thumb driver uses the same
    // fields and does not read the mirror ranges or split flag. The same selector occurs in 31 inspected ARM builds.
    addChild(offset() + 0x06, 1, "Mirror Low Key (Unused)");
    addChild(offset() + 0x07, 1, "Mirror High Key (Unused)");
    lowVel = readByte(offset() + 0x08);
    addChild(offset() + 0x08, 1, "Low Velocity");
    hiVel = readByte(offset() + 0x09);
    addChild(offset() + 0x09, 1, "High Velocity");
    addChild(offset() + 0x0A, 1, "Mirror Low Velocity (Unused)");
    addChild(offset() + 0x0B, 1, "Mirror High Velocity (Unused)");
    addChild(offset() + 0x0C, 4, "Padding (0x0C)");
    addChild(offset() + 0x10, 2, "Padding (0x10)");

    smplID = PSDSE::readU16(rawFile(), offset() + 0x12, pInstrSet->m_header.endianness);
    addChild(offset() + 0x12, 2, "Sample ID");

    ftune = readByte(offset() + 0x14);
    ctune = readByte(offset() + 0x15);
    addChild(offset() + 0x14, 1, "Fine Tune");
    addChild(offset() + 0x15, 1, "Coarse Tune");

    rootKey = readByte(offset() + 0x16);
    addChild(offset() + 0x16, 1, "Root Key");
    addChild(offset() + 0x17, 1, "Key Transpose");

    vol = readByte(offset() + 0x18);
    addChild(offset() + 0x18, 1, "Sample Volume");
    pan = readByte(offset() + 0x19);
    addChild(offset() + 0x19, 1, "Sample Pan");
    addChild(offset() + 0x1A, 1, "Key Group ID");
    addChild(offset() + 0x1B, 5, "Unused Sample Metadata");

    // Envelope flags/multipliers
    envon = readByte(offset() + 0x20);
    addChild(offset() + 0x20, 1, "Envelope On");
    envmult = readByte(offset() + 0x21);
    addChild(offset() + 0x21, 1, "Envelope Multiplier");
    // [Pokemon Mystery Dungeon: Explorers of Sky]: The driver copies the complete envelope block but does not read
    // these metadata constants or the trailing 0xff byte.
    addChild(offset() + 0x22, 2, "Unused Envelope Metadata 1");
    addChild(offset() + 0x24, 4, "Unused Envelope Metadata 2");

    // ADSR: 0x29=attack, 0x2A=decay, 0x2B=sustain, 0x2E=release.
    // Plus 0x2C=hold and 0x2D=decay2 (fade while the note remains held).
    attackBegin = readByte(offset() + 0x28);
    addChild(offset() + 0x28, 1, "Attack Begin");
    attack = readByte(offset() + 0x29);
    addChild(offset() + 0x29, 1, "Attack Time");
    decay = readByte(offset() + 0x2A);
    addChild(offset() + 0x2A, 1, "Decay Time");
    sustain = readByte(offset() + 0x2B);
    addChild(offset() + 0x2B, 1, "Sustain Level");
    hold = readByte(offset() + 0x2C);
    addChild(offset() + 0x2C, 1, "Hold Time");
    decay2 = readByte(offset() + 0x2D);
    addChild(offset() + 0x2D, 1, "Sustain Time");
    release = readByte(offset() + 0x2E);
    addChild(offset() + 0x2E, 1, "Release Time");
    addChild(offset() + 0x2F, 1, "Unused Envelope Metadata 3");
  }

  setLength(48);
  setRanges(lowKey, hiKey, lowVel, hiVel);
  addUnityKey(rootKey, (pInstrSet->m_header.version == 0x415) ? offset() + 0x16 : offset() + 0x14);

  setSampNum(smplID);

  // [Pokemon Mystery Dungeon: Explorers of Sky]: Version 0x0415 loads fine and coarse tune as one signed
  // little-endian halfword in 1/256-semitone units, then adds key transpose times 256.
  // [Luminous Arc]: Version 0x0402 uses the same tuning calculation.
  // The SF2 root key already represents key transpose, and the SF2 sample preserves its source rate. Retain the
  // residual tune after removing the DSE driver's 32728.5 Hz reference adjustment.
  {
    const uint16_t packedTune =
        static_cast<uint16_t>(ftune) | (static_cast<uint16_t>(static_cast<uint8_t>(ctune)) << 8);
    const int16_t rawTune = static_cast<int16_t>(packedTune);
    const int rawTuneCents = static_cast<int>(std::lround(rawTune * 100.0 / 256.0));
    int baseTuneCents = 0;
    const uint32_t waviTableOffset = pInstrSet->m_header.waviOffset + 0x10;
    const uint32_t pointerOffset = waviTableOffset + static_cast<uint32_t>(smplID) * 2;
    if (pInstrSet->m_header.waviOffset != 0 && smplID < pInstrSet->m_header.nbwavislots &&
        pointerOffset + 2 <= rawFile()->size()) {
      const uint16_t relativeOffset = PSDSE::readU16(rawFile(), pointerOffset, pInstrSet->m_header.endianness);
      if (relativeOffset != 0) {
        const uint32_t sampleInfoOffset = waviTableOffset + relativeOffset;
        const uint32_t sampleRate =
            pInstrSet->m_header.version == 0x0402
                ? PSDSE::readU16(rawFile(), sampleInfoOffset + 0x12, pInstrSet->m_header.endianness)
                : PSDSE::readU32(rawFile(), sampleInfoOffset + 0x20, pInstrSet->m_header.endianness);
        if (sampleRate != 0) {
          baseTuneCents = static_cast<int>(std::lround(1200.0 * std::log2(static_cast<double>(sampleRate) / 32728.5)));
        }
      }
    }

    const int tuneCents = rawTuneCents - baseTuneCents;
    const int coarseTune = tuneCents / 100;
    const int fineTune = tuneCents - coarseTune * 100;
    const uint32_t coarseTuneOffset = offset() + (pInstrSet->m_header.version == 0x0402 ? 0x13 : 0x15);
    const uint32_t fineTuneOffset = offset() + (pInstrSet->m_header.version == 0x0402 ? 0x12 : 0x14);
    if (coarseTune != 0) {
      addCoarseTune(coarseTune, coarseTuneOffset);
    }
    if (fineTune != 0) {
      addFineTune(fineTune, fineTuneOffset);
    }
  }
  if (vol == 0) {
    setAttenuation(144.0);
  } else {
    const auto* instr = static_cast<PSDSEInstr*>(parInstr);
    double voiceVolume = (vol / 127.0) * (instr->volume / 127.0);
    voiceVolume *= voiceVolume;
    setVolume(voiceVolume);
  }

  setPan(pan);

  auto* instr = static_cast<PSDSEInstr*>(parInstr);
  for (const auto& lfo : instr->lfos) {
    // [Pokemon Mystery Dungeon: Explorers of Sky]: The DSE waveform table defines a full triangle cycle as two phase
    // periods. SF2 can represent only triangle vibrato, while DLS approximates it with a fixed sine LFO; square, saw,
    // and noise waveforms are left unexported.
    if (lfo.mode != 0 && lfo.destination == 0x01 && lfo.waveform == 0x03 && lfo.amplitude != 0 && lfo.periodMs != 0) {
      setLfoVibDelaySeconds(lfo.delayMs / 1000.0);
      setLfoVibFreqHz(dseFullTriangleFrequencyHz(lfo.periodMs));
      setLfoVibDepthCents(dsePitchLfoDepthCents(lfo.amplitude));
    }
  }

  auto dseTimeToSeconds = [envmult](uint8_t val) -> double { return PSDSEEnvelope::convertToSeconds(val, envmult); };

  // [Pokemon Mystery Dungeon: Explorers of Sky]: The version 0x0415 driver runs attack, hold, decay-to-sustain, and a
  // timed sustain-to-zero phase in sequence; 0x7f denotes an infinite phase, and the mixer squares envelope level.
  // SF2 and DLS expose AHDSR, so export combines the two decay phases into one linear-dB decay rate.
  if (envon == 0) {
    this->attack_time = 0.0;
    this->hold_time = 0.0;
    this->decay_time = 0.0;
    this->sustain_level = 1.0;
    this->release_time = 0.0;
    return true;
  }

  const double attackBeginControl = attackBegin / 127.0;
  const double attackBeginLevel = attackBeginControl * attackBeginControl;
  if (attack == 0x7F) {
    // [Pokemon Mystery Dungeon: Explorers of Sky]: An infinite attack remains at attack_begin and never reaches hold
    // or decay.
    this->attack_time = 0.0;
    this->hold_time = 0.0;
    this->decay_time = 0.0;
    this->sustain_level = attackBeginLevel;
    this->release_time = release == 0x7F ? 1.0e9 : dseTimeToSeconds(release);
    return true;
  }

  // [Pokemon Mystery Dungeon: Explorers of Sky]: A finite nonzero attack_begin sets the phase start level. SF2 and
  // DLS cannot represent that start level, so the exported phase retains its duration and target.
  this->attack_time = dseTimeToSeconds(attack);
  this->hold_time = hold == 0x7F ? 1.0e9 : dseTimeToSeconds(hold);

  const double controlSustain = sustain / 127.0;
  const double audibleSustain = controlSustain * controlSustain;
  const auto linearPhaseToSf2Rate = [](double duration, double startLevel, double endLevel) -> double {
    const double amplitudeDistance = startLevel - endLevel;
    if (duration <= 0.0 || amplitudeDistance <= 0.0) {
      return 0.0;
    }
    return linearAmpDecayTimeToLinDBDecayTime(duration / amplitudeDistance);
  };

  if (decay == 0x7F) {
    // [Pokemon Mystery Dungeon: Explorers of Sky]: The state machine never advances past a full-level infinite
    // decay.
    this->decay_time = 0.0;
    this->sustain_level = 1.0;
  } else {
    double firstRate = linearPhaseToSf2Rate(dseTimeToSeconds(decay), 1.0, audibleSustain);
    const bool finiteSustainFade = decay2 != 0 && decay2 != 0x7F;

    if (finiteSustainFade && audibleSustain > 0.0) {
      const double secondRate = linearPhaseToSf2Rate(dseTimeToSeconds(decay2), audibleSustain, 0.0);
      const double sustainAttenuation = ampToDb(audibleSustain, 100.0) / 100.0;
      this->decay_time = firstRate * sustainAttenuation + secondRate * (1.0 - sustainAttenuation);
      this->sustain_level = 0.0;
    } else if (finiteSustainFade) {
      this->decay_time = firstRate;
      this->sustain_level = 0.0;
    } else {
      this->decay_time = firstRate;
      this->sustain_level = audibleSustain;
    }
  }

  // [Pokemon Mystery Dungeon: Explorers of Sky]: Release is a fixed-duration phase. SF2 and DLS use a different
  // curve, so the exported release retains the game duration and zero endpoint.
  this->release_time = release == 0x7F ? 1.0e9 : dseTimeToSeconds(release);

  return true;
}
