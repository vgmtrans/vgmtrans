#include "PSDSEInstrSet.h"
#include "PSDSEFormat.h"
#include "VGMSamp.h"
#include "VGMSampColl.h"
#include "VGMColl.h"
#include "RawFile.h"
#include "LogManager.h"

#include <spdlog/spdlog.h>

#include "PSDSEEnvelopeCurve.h"

// ***************
// PSDSEInstrSet
// ***************

PSDSEInstrSet::PSDSEInstrSet(RawFile *file, const SWDLHeader &header)
    : VGMInstrSet("PSDSE", file, header.offset, 0, header.intName), m_header(header) {
}

bool PSDSEInstrSet::parseHeader() {
  uint32_t flen = PSDSE::readU32(rawFile(), offset() + 0x08, m_header.endianness);
  if (flen > rawFile()->size() - offset()) {
    flen = rawFile()->size() - offset();
  }
  setLength(flen);

  return true;
}

bool PSDSEInstrSet::parseInstrPointers() {
  // If no prgi chunk, this file is a sample-only bank (like bgm.swd)
  // User confirmed these should NOT have implicit instruments created.
  // It effectively functions only as a sample library for other SWDs.
  if (m_header.prgiOffset == 0) {
    L_INFO("PSDSE: no prgi chunk (sample bank), skipping instrument creation");
    return true;
  }

  uint32_t chunkDataStart = m_header.prgiOffset + 0x10;
  uint32_t prgiChunkEnd = m_header.prgiOffset + 0x10 + m_header.prgiSize;

  L_DEBUG("PSDSE: parsing {} prgi slots at {:x}, prgi ends at {:x}", m_header.nbprgislots,
          chunkDataStart, prgiChunkEnd);

  uint32_t fileSize = rawFile()->size();

  for (int i = 0; i < m_header.nbprgislots; i++) {
    // Ensure we don't read beyond file bounds for the pointer table
    if (chunkDataStart + i * 2 + 2 > fileSize) {
      break;
    }

    uint16_t ptroffset =
        PSDSE::readU16(rawFile(), chunkDataStart + i * 2, m_header.endianness);
    if (ptroffset != 0) {
      uint32_t instrOffset = chunkDataStart + ptroffset;

      // Validate instrument offset is within prgi chunk bounds
      if (instrOffset >= prgiChunkEnd) {
        L_WARN("PSDSE: instr {} has invalid offset {:x} (prgi ends {:x}), skipping", i, instrOffset,
               prgiChunkEnd);
        continue;
      }

      L_DEBUG("PSDSE: found instr {} at {:x} (ptr {:x})", i, instrOffset, ptroffset);

      addInstr<PSDSEInstr>(this, instrOffset, 1, 0, i);
      // FluidSynth always addresses MIDI channel 10 through SF2 bank 128.
      // DSE can select any preset on that channel, so mirror every available
      // preset rather than assuming only program 127 is percussion.
      addInstr<PSDSEInstr>(this, instrOffset, 1, 128, i);
    }
  }

  L_INFO("PSDSE: total instruments found: {}", instrCount());

  return true;
}

// ***************
// PSDSESampColl
// ***************

PSDSESampColl::PSDSESampColl(const std::string &format, RawFile *rawfile, const SWDLHeader &header,
                             std::string theName)
    : VGMSampColl(format, rawfile, header.offset, 0, theName), m_header(header) {
}

bool PSDSESampColl::parseHeader() {
  setLength(m_header.fileLength);
  return true;
}

// From http://nocash.emubase.de/gbatek.htm#dssound
static const unsigned AdpcmTable[89] = {
    0x0007, 0x0008, 0x0009, 0x000A, 0x000B, 0x000C, 0x000D, 0x000E, 0x0010, 0x0011, 0x0013, 0x0015,
    0x0017, 0x0019, 0x001C, 0x001F, 0x0022, 0x0025, 0x0029, 0x002D, 0x0032, 0x0037, 0x003C, 0x0042,
    0x0049, 0x0050, 0x0058, 0x0061, 0x06B,  0x0076, 0x0082, 0x008F, 0x009D, 0x00AD, 0x00BE, 0x00D1,
    0x00E6, 0x00FD, 0x0117, 0x0133, 0x0151, 0x0173, 0x0198, 0x01C1, 0x01EE, 0x0220, 0x0256, 0x0292,
    0x02D4, 0x031C, 0x036C, 0x03C3, 0x0424, 0x048E, 0x0502, 0x0583, 0x0610, 0x06AB, 0x0756, 0x0812,
    0x08E0, 0x09C3, 0x0ABD, 0x0BD0, 0x0CFF, 0x0E4C, 0x0FBA, 0x114C, 0x1307, 0x14EE, 0x1706, 0x1954,
    0x1BDC, 0x1EA5, 0x21B6, 0x2515, 0x28CA, 0x2CDF, 0x315B, 0x364B, 0x3BB9, 0x41B2, 0x4844, 0x4F7E,
    0x5771, 0x602F, 0x69CE, 0x7462, 0x7FFF};

static constexpr int IMA_IndexTable[9] = {-1, -1, -1, -1, 2, 4, 6, 8};

// Dummy Sample Class for empty slots
class PSDSEEmptySamp : public VGMSamp {
public:
  PSDSEEmptySamp(VGMSampColl *sampColl)
      : VGMSamp(sampColl, 0, 1, 0, 32, 1, BPS::PCM16, 22050, "Empty") {
    setLoopStatus(0);
    setLoopLength(32);
    setLoopOffset(0);
  }

protected:
  std::vector<uint8_t> decodeToNativePcm() override { return std::vector<uint8_t>(dataLength, 0); }
};

bool PSDSESampColl::parseSampleInfo() {
  uint32_t wavTableOffset = m_header.waviOffset + 0x10;

  int numSlots = m_header.nbwavislots;
  if (numSlots == 0) {
    numSlots = 128;  // fallback
  }

  for (int i = 0; i < numSlots; i++) {
    uint16_t relativeOffset =
        PSDSE::readU16(rawFile(), wavTableOffset + i * 2, m_header.endianness);

    if (relativeOffset == 0) {
      addSamp<PSDSEEmptySamp>(this);
    } else {
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

      std::string sampleName = "Sample " + std::to_string(i);

      if (!m_header.intName.empty()) {
        if (m_header.nbwavislots == 1 ||
            m_header.intName.find(std::to_string(i)) != std::string::npos) {
          sampleName = m_header.intName;
        }
      }

      if (m_header.version == 0x0402) {
        // v402 structure (shorter offsets)
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
        // v415 structure (longer offsets, confirmed for PMD Sky)
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

      // Total size of the sample data block, including header if present
      uint32_t dataLength = (loopBeg + loopLen) * 4;

      BPS bps = BPS::PCM16;
      uint8_t waveType = PSDSESamp::PCM16;

      if (smplfmt == 0x0200) {
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
        dataLength = bankEnd - dataOffset;
      }

      PSDSESamp *samp = addSamp<PSDSESamp>(
          this, sampleInfoOffset, 64, dataOffset, dataLength, 1, bps, smplRate, waveType,
          sampleName);
      samp->unityKey = rootKey;
      samp->pan = pan;
      samp->setVolume(volume / 127.0);
      if (loopFlag) {
        samp->setLoopStatus(1);

        if (waveType == PSDSESamp::IMA_ADPCM) {
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

PSDSESamp::PSDSESamp(VGMSampColl *sampColl, uint32_t offset, uint32_t length, uint32_t dataOffset,
                     uint32_t dataLength, uint8_t nChannels, BPS bps, uint32_t theRate,
                     uint8_t theWaveType, std::string name)
    : VGMSamp(sampColl, offset, length, dataOffset, dataLength, nChannels, bps, theRate,
              std::move(name)),
      waveType(theWaveType) {
}

double PSDSESamp::compressionRatio() const {
  if (waveType == IMA_ADPCM) {
    return 4.0;
  }
  return 1.0;
}

std::vector<uint8_t> PSDSESamp::decodeToNativePcm() {
  if (waveType == IMA_ADPCM) {
    return decodeImaAdpcm();
  }
  return VGMSamp::decodeToNativePcm();
}

// Helper macros for ADPCM
#define IMAMax(samp) (samp > 32767) ? 32767 : samp
#define IMAMin(samp) (samp < -32768) ? -32768 : samp
#define IMAIndexMinMax(index, min, max) (index > max) ? max : ((index < min) ? min : index)

void PSDSESamp::process_nibble(unsigned char data4bit, int &Index, int &Pcm16bit) {
  int Diff = AdpcmTable[Index] / 8;
  if (data4bit & 1)
    Diff = Diff + AdpcmTable[Index] / 4;
  if (data4bit & 2)
    Diff = Diff + AdpcmTable[Index] / 2;
  if (data4bit & 4)
    Diff = Diff + AdpcmTable[Index] / 1;

  if ((data4bit & 8) == 0)
    Pcm16bit = IMAMax(Pcm16bit + Diff);
  if ((data4bit & 8) == 8)
    Pcm16bit = IMAMin(Pcm16bit - Diff);
  Index = IMAIndexMinMax(Index + IMA_IndexTable[data4bit & 7], 0, 88);
}

void PSDSESamp::clamp_step_index(int &stepIndex) {
  if (stepIndex < 0)
    stepIndex = 0;
  if (stepIndex > 88)
    stepIndex = 88;
}

void PSDSESamp::clamp_sample(int &decompSample) {
  if (decompSample < -32768)
    decompSample = -32768;
  if (decompSample > 32767)
    decompSample = 32767;
}

std::vector<uint8_t> PSDSESamp::decodeImaAdpcm() {
  // dataLength is the size of the compressed data (after header).
  // Each byte of compressed data contains 2 nibbles, each nibble produces 1 sample.
  // Header provides 1 initial sample.
  // Total samples = dataLength * 2 + 1.
  const uint32_t sampleCount = dataLength * 2 + 1;
  std::vector<uint8_t> samples(sampleCount * sizeof(int16_t));
  auto *output = reinterpret_cast<int16_t *>(samples.data());
  uint32_t destOff = 0;

  // The header (initial PCM and index) is located 4 bytes *before* dataOff.
  // dataOff was adjusted in parseSampleInfo to point to the actual compressed nibbles.
  uint32_t sampHeader = getWord(dataOff - 4);
  int decompSample = (int16_t)(sampHeader & 0xFFFF);  // Initial PCM sample
  int stepIndex = (sampHeader >> 16) & 0x7F;          // Initial step index (7-bit)

  uint32_t curOffset = dataOff;

  // The first sample is the initial PCM sample from the header.
  output[destOff++] = (int16_t)decompSample;

  uint8_t compByte;
  // dataLength is bytes of compressed data.
  while (curOffset < dataOff + dataLength) {
    compByte = readByte(curOffset++);

    // Process lower nibble first
    process_nibble(compByte & 0x0F, stepIndex, decompSample);
    output[destOff++] = (int16_t)decompSample;

    // Process upper nibble
    process_nibble((compByte & 0xF0) >> 4, stepIndex, decompSample);
    output[destOff++] = (int16_t)decompSample;
  }

  return samples;
}

// ************
// PSDSEInstr
// ************

PSDSEInstr::PSDSEInstr(VGMInstrSet *instr_set, uint32_t offset, uint32_t length, uint32_t bank,
                       uint32_t instr_num)
    : VGMInstr(instr_set, offset, length, bank, instr_num, "Instrument") {
}

bool PSDSEInstr::loadInstr() {
  auto *pInstrSet = static_cast<PSDSEInstrSet *>(parInstrSet);
  uint32_t splitsOffset = 0;
  uint32_t lfoTableOffset = 0;
  uint16_t nbsplits = 0;
  uint8_t nblfos = 0;

  if (pInstrSet->m_header.version == 0x0402) {
    // v402 structure
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
    uint32_t currLfo = lfoTableOffset + (i * 16);
    PSDSELFO lfo;
    lfo.enabled = readByte(currLfo + 0x01);
    lfo.dest = readByte(currLfo + 0x02);
    lfo.wshape = readByte(currLfo + 0x03);
    lfo.rate = PSDSE::readU16(rawFile(), currLfo + 0x04, pInstrSet->m_header.endianness);
    lfo.depth = PSDSE::readU16(rawFile(), currLfo + 0x08, pInstrSet->m_header.endianness);
    lfo.delay = PSDSE::readU16(rawFile(), currLfo + 0x0A, pInstrSet->m_header.endianness);
    lfo.fade = static_cast<int16_t>(
        PSDSE::readU16(rawFile(), currLfo + 0x0C, pInstrSet->m_header.endianness));
    lfos.push_back(lfo);
  }

  for (int i = 0; i < nbsplits; i++) {
    uint32_t splitOff = splitsOffset + i * 48;
    PSDSERgn *rgn = addRgn<PSDSERgn>(this, splitOff);
    rgn->loadRgn();
  }

  return true;
}

// ************
// PSDSERgn
// ************

PSDSERgn::PSDSERgn(VGMInstr *instr, uint32_t offset) : VGMRgn(instr, offset, 48, "Region") {
}

bool PSDSERgn::loadRgn() {
  auto *pInstrSet = static_cast<PSDSEInstrSet *>(parInstr->parInstrSet);

  uint8_t lowKey, hiKey, lowVel, hiVel;
  uint16_t smplID;
  uint8_t ftune;
  int8_t ctune = 0, ktps = 0;
  uint8_t rootKey, vol, pan;
  uint8_t attack, decay, sustain, release;
  uint8_t envon = 0, envmult = 0, hold = 0, decay2 = 0;

  if (pInstrSet->m_header.version == 0x0402) {
    // v402 split entry (48 bytes)
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

    addChild(offset() + 0x00, 4, "Unknown 0x00");
    lowKey = readByte(offset() + 0x04);
    addChild(offset() + 0x04, 1, "Low Key");
    hiKey = readByte(offset() + 0x05);
    addChild(offset() + 0x05, 1, "High Key");
    addChild(offset() + 0x06, 2, "Unknown 0x06");
    lowVel = readByte(offset() + 0x08);
    addChild(offset() + 0x08, 1, "Low Velocity");
    hiVel = readByte(offset() + 0x09);
    addChild(offset() + 0x09, 1, "High Velocity");
    addChild(offset() + 0x0A, 7, "Unknown 0x0A");

    smplID = readByte(offset() + 0x11);
    addChild(offset() + 0x11, 1, "Sample ID");

    ftune = readByte(offset() + 0x12);
    addChild(offset() + 0x12, 1, "Fine Tune");
    ctune = readByte(offset() + 0x13);
    addChild(offset() + 0x13, 1, "Coarse Tune");
    rootKey = readByte(offset() + 0x14);
    addChild(offset() + 0x15, 1, "Unknown 0x15");

    vol = readByte(offset() + 0x16);
    addChild(offset() + 0x16, 1, "Sample Volume");
    pan = readByte(offset() + 0x17);
    addChild(offset() + 0x17, 1, "Sample Pan");
    addChild(offset() + 0x18, 17, "Unknown 0x18");

    attack = readByte(offset() + 0x29);
    addChild(offset() + 0x29, 1, "Attack Time");
    decay = readByte(offset() + 0x2A);
    addChild(offset() + 0x2A, 1, "Decay Time");
    sustain = readByte(offset() + 0x2B);
    addChild(offset() + 0x2B, 1, "Sustain Level");
    addChild(offset() + 0x2C, 2, "Unknown 0x2C");
    release = readByte(offset() + 0x2E);
    addChild(offset() + 0x2E, 1, "Release Time");
    addChild(offset() + 0x2F, 1, "Unknown 0x2F");
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
    addChild(offset() + 0x02, 1, "Pitch Bend Range");
    addChild(offset() + 0x03, 1, "Unknown 0x03");
    lowKey = readByte(offset() + 0x04);
    addChild(offset() + 0x04, 1, "Low Key");
    hiKey = readByte(offset() + 0x05);
    addChild(offset() + 0x05, 1, "High Key");
    addChild(offset() + 0x06, 1, "Low Key 2");
    addChild(offset() + 0x07, 1, "High Key 2");
    lowVel = readByte(offset() + 0x08);
    addChild(offset() + 0x08, 1, "Low Velocity");
    hiVel = readByte(offset() + 0x09);
    addChild(offset() + 0x09, 1, "High Velocity");
    addChild(offset() + 0x0A, 1, "Low Velocity 2");
    addChild(offset() + 0x0B, 1, "High Velocity 2");
    addChild(offset() + 0x0C, 4, "Padding (0x0C)");
    addChild(offset() + 0x10, 2, "Padding (0x10)");

    smplID = PSDSE::readU16(rawFile(), offset() + 0x12, pInstrSet->m_header.endianness);
    addChild(offset() + 0x12, 2, "Sample ID");

    ftune = readByte(offset() + 0x14);
    ctune = readByte(offset() + 0x15);

    ktps = readByte(offset() + 0x17);
    rootKey = readByte(offset() + 0x16);

    vol = readByte(offset() + 0x18);
    addChild(offset() + 0x18, 1, "Sample Volume");
    pan = readByte(offset() + 0x19);
    addChild(offset() + 0x19, 1, "Sample Pan");
    addChild(offset() + 0x1A, 1, "Key Group ID");
    addChild(offset() + 0x1B, 5, "Unknown 0x1B");

    // Envelope flags/multipliers
    envon = readByte(offset() + 0x20);
    addChild(offset() + 0x20, 1, "Envelope On");
    envmult = readByte(offset() + 0x21);
    addChild(offset() + 0x21, 1, "Envelope Multiplier");
    addChild(offset() + 0x22, 7, "Unknown 0x22");

    // ADSR: 0x29=attack, 0x2A=decay, 0x2B=sustain, 0x2E=release.
    // Plus 0x2C=hold and 0x2D=decay2 (fade while the note remains held).
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
    addChild(offset() + 0x2F, 1, "Unknown 0x2F");
  }

  setLength(48);
  setRanges(lowKey, hiKey, lowVel, hiVel);
  addUnityKey(rootKey, (pInstrSet->m_header.version == 0x415) ? offset() + 0x16 : offset() + 0x14);

  setSampNum(smplID);

  // DSE tuning contains the pitch adjustment that expresses a sample's playback
  // rate relative to the engine's 32728.5 Hz reference. SF2 already expresses
  // that adjustment in the sample-rate field, so retain only the residual region
  // tuning. ktps is derived metadata (60 - rootKey), not an audible transpose.

  if (pInstrSet->m_header.version == 0x415) {
    const int rawTuneCents = static_cast<int>(ctune) * 100 +
                             static_cast<int>(std::lround(ftune / 255.0 * 100.0));
    int baseTuneCents = 0;
    const uint32_t waviTableOffset = pInstrSet->m_header.waviOffset + 0x10;
    const uint32_t pointerOffset = waviTableOffset + static_cast<uint32_t>(smplID) * 2;
    if (pInstrSet->m_header.waviOffset != 0 && smplID < pInstrSet->m_header.nbwavislots &&
        pointerOffset + 2 <= rawFile()->size()) {
      const uint16_t relativeOffset =
          PSDSE::readU16(rawFile(), pointerOffset, pInstrSet->m_header.endianness);
      if (relativeOffset != 0) {
        const uint32_t sampleInfoOffset = waviTableOffset + relativeOffset;
        const uint32_t sampleRate = pInstrSet->m_header.version == 0x0402
                                        ? PSDSE::readU16(rawFile(), sampleInfoOffset + 0x12,
                                                        pInstrSet->m_header.endianness)
                                        : PSDSE::readU32(rawFile(), sampleInfoOffset + 0x20,
                                                        pInstrSet->m_header.endianness);
        if (sampleRate != 0) {
          baseTuneCents = static_cast<int>(
              std::lround(1200.0 * std::log2(static_cast<double>(sampleRate) / 32728.5)));
        }
      }
    }

    const int tuneCents = rawTuneCents - baseTuneCents;
    const int coarseTune = tuneCents / 100;
    const int fineTune = tuneCents - coarseTune * 100;
    if (coarseTune != 0) {
      addCoarseTune(coarseTune, offset() + 0x15);
    }
    if (fineTune != 0) {
      addFineTune(fineTune, offset() + 0x14);
    }
  }
  (void)ktps;

  (void)envon;

  if (vol == 0) {
    setAttenuation(144.0);
  } else {
    // NDS mixer volume is a linear amplitude multiplier.
    setVolume(vol / 127.0);
  }

  setPan(pan);

  if (pInstrSet->m_header.version == 0x0402 || pInstrSet->m_header.version == 0x0415) {
    auto *instr = static_cast<PSDSEInstr *>(parInstr);
    for (const auto &lfo : instr->lfos) {
      if (lfo.enabled != 0 && lfo.dest == 0x01) {  // Pitch / Vibrato
        setLfoVibDelaySeconds(lfo.delay / 1000.0);

        // Pitch LFO entries encode pitch depth in the field conventionally
        // called rate, while the depth field is frequency in 1/16 Hz units.
        // For example, PMD's (10, 90) string vibrato is 10 cents at 5.625 Hz.
        // PSDSE LFOs have a 'wshape' parameter (Square, Triangle, etc.), but
        // VGMRgn currently maps purely to "Vibrato LFO" which has format-specific behaviors:
        // - SF2: Fixed Triangle wave.
        // - DLS: Default Sine wave (Level 1).
        // We cannot easily enforce the PSDSE wave shape on the output formats.

        setLfoVibFreqHz(lfo.depth / 16.0);
        setLfoVibDepthCents(lfo.rate);
      }
    }
  }

  auto dseTimeToSeconds = [envmult](uint8_t val) -> double {
    return PSDSEEnvelope::convertToSeconds(val, envmult);
  };

  this->attack_time = dseTimeToSeconds(attack);
  this->hold_time = dseTimeToSeconds(hold);

  // Decay Time
  // "decay2" (at offset 0x2D) is often used as a second decay phase or "fade out" while holding.
  // If decay2 is active (nonzero and not 0x7F), the instrument decays to silence (Sustain Level =
  // 0). The total decay time becomes decay1 + decay2.

  bool decayToSilence = false;
  if (decay != 0 && decay2 != 0 && decay2 != 0x7F) {
    decayToSilence = true;
    if (decay == 0x7F) {
      this->decay_time = dseTimeToSeconds(decay2);
    } else if (sustain == 0) {
      this->decay_time = dseTimeToSeconds(decay);
    } else {
      this->decay_time = dseTimeToSeconds(decay) + dseTimeToSeconds(decay2);
    }
  } else if (decay != 0) {
    this->decay_time = dseTimeToSeconds(decay);
  } else if (decay2 != 0x7F) {
    decayToSilence = true;
    this->decay_time = dseTimeToSeconds(decay2);
  } else {
    this->decay_time = 0.0;
  }

  if (decayToSilence) {
    this->sustain_level = 0.0;
  } else {
    this->sustain_level = sustain / 127.0;
  }

  this->release_time = dseTimeToSeconds(release);

  return true;
}
