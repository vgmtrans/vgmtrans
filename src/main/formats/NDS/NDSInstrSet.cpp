/**
 * VGMTrans (c) - 2002-2024
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#include <cmath>
#include <numeric>
#include <string>
#include <vector>
#include <spdlog/fmt/fmt.h>
#include "Loop.h"
#include "NDSFormat.h"
#include "VGMSampColl.h"
#include "NDSInstrSet.h"
#include "VGMRgn.h"
#include "LogManager.h"

// INTR_FREQUENCY is the interval in seconds between updates to the vol for articulation.
// In the original software, this is done via an interrupt timer.
// The value was copied from the nocash docs.
// We can multiply the count  by this frequency to find the duration of the articulation phases with
// exact accuracy.
constexpr double INTR_FREQUENCY = 64 * 2728.0 / 33e6;

// ***********
// NDSInstrSet
// ***********

NDSInstrSet::NDSInstrSet(RawFile *file, uint32_t offset, uint32_t length, VGMSampColl *psg_samples,
                         std::string name)
    : VGMInstrSet(NDSFormat::name, file, offset, length, std::move(name)),
      m_psg_samples(psg_samples) {}

bool NDSInstrSet::parseInstrPointers() {
  uint32_t nInstruments = readWord(dwOffset + 0x38);
  VGMHeader *instrptrHdr = addHeader(dwOffset + 0x38, nInstruments * 4 + 4, "Instrument Pointers");

  for (uint32_t i = 0; i < nInstruments; i++) {
    uint32_t instrPtrOff = dwOffset + 0x3C + i * 4;
    uint32_t temp = readWord(instrPtrOff);
    if (temp == 0) {
      continue;
    }

    uint8_t instrType = temp & 0xFF;
    uint32_t pInstr = temp >> 8;
    aInstrs.push_back(new NDSInstr(this, pInstr + dwOffset, 0, 0, i, instrType));

    VGMHeader *hdr = instrptrHdr->addHeader(instrPtrOff, 4, "Pointer");
    hdr->addChild(instrPtrOff, 1, "Type");
    hdr->addChild(instrPtrOff + 1, 3, "Offset");
  }

  return true;
}

// ********
// NDSInstr
// ********

NDSInstr::NDSInstr(NDSInstrSet *instrSet, uint32_t offset, uint32_t length, uint32_t theBank,
                   uint32_t theInstrNum, uint8_t theInstrType)
    : VGMInstr(instrSet, offset, length, theBank, theInstrNum, "Instrument", 0), instrType(theInstrType) {
}

bool NDSInstr::loadInstr() {
  // All of the undefined case values below are used for tone or noise channels
  switch (instrType) {
    case 0x01: {
      setName("Single-Region Instrument");
      unLength = 10;

      VGMRgn *rgn = addRgn(dwOffset, 10, readShort(dwOffset));
      getSampCollPtr(rgn, readShort(dwOffset + 2));
      getArticData(rgn, dwOffset + 4);
      rgn->addChild(dwOffset + 2, 2, "Sample Collection Index");
      break;
    }

    case 0x02: {
      /* PSG Tone */
      uint8_t dutyCycle = readByte(dwOffset) & 0x07;
      std::string dutyCycles[8] = {"12.5%", "25%", "37.5%", "50%",
                                    "62.5%", "75%", "87.5%", "0%"};
      setName("PSG Wave (" + dutyCycles[dutyCycle] + ")");
      unLength = 10;

      VGMRgn *rgn = addRgn(dwOffset, 10, dutyCycle);
      getArticData(rgn, dwOffset + 4);

      rgn->sampCollPtr = static_cast<NDSInstrSet*>(parInstrSet)->m_psg_samples;
      /* We have to set this manually as all of our samples are generated at 440Hz (69 = A4) */
      rgn->setUnityKey(69);
      break;
    }

    case 0x03: {
      setName("PSG Noise");
      unLength = 10;

      /* The noise sample is the 8th in our PSG sample collection */
      VGMRgn *rgn = addRgn(dwOffset, 10, 8);
      getArticData(rgn, dwOffset + 4);

      rgn->sampCollPtr = static_cast<NDSInstrSet*>(parInstrSet)->m_psg_samples;
      rgn->setUnityKey(45);

      break;
    }

    case 0x10: {
      setName("Drumset");

      uint8_t lowKey = readByte(dwOffset);
      uint8_t highKey = readByte(dwOffset + 1);
      uint8_t nRgns = (highKey - lowKey) + 1;
      for (uint8_t i = 0; i < nRgns; i++) {
        u32 rgnOff = dwOffset + 2 + i * 12;
        VGMRgn *rgn = addRgn(rgnOff, 12, readShort(rgnOff + 2),
                             lowKey + i, lowKey + i);
        getSampCollPtr(rgn, readShort(rgnOff + 4));
        getArticData(rgn, rgnOff + 6);
        rgn->addChild(rgnOff + 2, 2, "Sample Num");
        rgn->addChild(rgnOff + 4, 2, "Sample Collection Index");
      }
      unLength = 2 + nRgns * 12;

      break;
    }

    case 0x11: {
      setName("Multi-Region Instrument");
      uint8_t keyRanges[8];
      uint8_t nRgns = 0;
      for (int i = 0; i < 8; i++) {
        keyRanges[i] = readByte(dwOffset + i);
        if (keyRanges[i] != 0) {
          nRgns++;
        } else {
          break;
        }
        addChild(dwOffset + i, 1, "Key Range");
      }

      for (int i = 0; i < nRgns; i++) {
        u32 rgnOff = dwOffset + 8 + i * 12;
        VGMRgn *rgn = addRgn(rgnOff, 12, readShort(rgnOff + 2),
                             (i == 0) ? 0 : keyRanges[i - 1] + 1, keyRanges[i]);
        getSampCollPtr(rgn, readShort(rgnOff + 4));
        getArticData(rgn, rgnOff + 6);
        addChild(rgnOff + 4, 2, "Sample Collection Index");
      }
      unLength = nRgns * 12 + 8;

      break;
    }
    default:
      break;
  }
  return true;
}

void NDSInstr::getSampCollPtr(VGMRgn *rgn, int waNum) const {
  rgn->sampCollPtr = static_cast<NDSInstrSet*>(parInstrSet)->sampCollWAList[waNum];
}

void NDSInstr::getArticData(VGMRgn *rgn, uint32_t offset) const {
  uint8_t realAttack;
  long realSustainLev;
  const uint8_t AttackTimeTable[] = {0x00, 0x01, 0x05, 0x0E, 0x1A, 0x26, 0x33, 0x3F, 0x49, 0x54,
                                     0x5C, 0x64, 0x6D, 0x74, 0x7B, 0x7F, 0x84, 0x89, 0x8F};

  const uint16_t sustainLevTable[] = {
      0xFD2D, 0xFD2E, 0xFD2F, 0xFD75, 0xFDA7, 0xFDCE, 0xFDEE, 0xFE09, 0xFE20, 0xFE34, 0xFE46,
      0xFE57, 0xFE66, 0xFE74, 0xFE81, 0xFE8D, 0xFE98, 0xFEA3, 0xFEAD, 0xFEB6, 0xFEBF, 0xFEC7,
      0xFECF, 0xFED7, 0xFEDF, 0xFEE6, 0xFEEC, 0xFEF3, 0xFEF9, 0xFEFF, 0xFF05, 0xFF0B, 0xFF11,
      0xFF16, 0xFF1B, 0xFF20, 0xFF25, 0xFF2A, 0xFF2E, 0xFF33, 0xFF37, 0xFF3C, 0xFF40, 0xFF44,
      0xFF48, 0xFF4C, 0xFF50, 0xFF53, 0xFF57, 0xFF5B, 0xFF5E, 0xFF62, 0xFF65, 0xFF68, 0xFF6B,
      0xFF6F, 0xFF72, 0xFF75, 0xFF78, 0xFF7B, 0xFF7E, 0xFF81, 0xFF83, 0xFF86, 0xFF89, 0xFF8C,
      0xFF8E, 0xFF91, 0xFF93, 0xFF96, 0xFF99, 0xFF9B, 0xFF9D, 0xFFA0, 0xFFA2, 0xFFA5, 0xFFA7,
      0xFFA9, 0xFFAB, 0xFFAE, 0xFFB0, 0xFFB2, 0xFFB4, 0xFFB6, 0xFFB8, 0xFFBA, 0xFFBC, 0xFFBE,
      0xFFC0, 0xFFC2, 0xFFC4, 0xFFC6, 0xFFC8, 0xFFCA, 0xFFCC, 0xFFCE, 0xFFCF, 0xFFD1, 0xFFD3,
      0xFFD5, 0xFFD6, 0xFFD8, 0xFFDA, 0xFFDC, 0xFFDD, 0xFFDF, 0xFFE1, 0xFFE2, 0xFFE4, 0xFFE5,
      0xFFE7, 0xFFE9, 0xFFEA, 0xFFEC, 0xFFED, 0xFFEF, 0xFFF0, 0xFFF2, 0xFFF3, 0xFFF5, 0xFFF6,
      0xFFF8, 0xFFF9, 0xFFFA, 0xFFFC, 0xFFFD, 0xFFFF, 0x0000};


  rgn->addUnityKey(readByte(offset), offset, 1);
  uint8_t AttackTime = readByte(offset + 1);
  uint8_t DecayTime = readByte(offset + 2);
  uint8_t SustainLev = readByte(offset + 3);
  uint8_t ReleaseTime = readByte(offset + 4);
  uint8_t Pan = readByte(offset + 5);

  rgn->addADSRValue(offset + 1, 1, "Attack Rate");
  rgn->addADSRValue(offset + 2, 1, "Decay Rate");
  rgn->addADSRValue(offset + 3, 1, "Sustain Level");
  rgn->addADSRValue(offset + 4, 1, "Release Time");
  rgn->addChild(offset + 5, 1, "Pan");

  if (AttackTime >= 0x6D)
    realAttack = AttackTimeTable[0x7F - AttackTime];
  else
    realAttack = 0xFF - AttackTime;

  short realDecay = getFallingRate(DecayTime);
  short realRelease = getFallingRate(ReleaseTime);

  int count = 0;
  for (long i = 0x16980; i != 0; i = (i * realAttack) >> 8)
    count++;
  rgn->attack_time = count * INTR_FREQUENCY;

  if (SustainLev == 0x7F)
    realSustainLev = 0;
  else
    realSustainLev = (0x10000 - sustainLevTable[SustainLev]) << 7;
  if (DecayTime == 0x7F)
    rgn->decay_time = 0.001;
  else {
    count = 0x16980 / realDecay;  // realSustainLev / realDecay;
    rgn->decay_time = count * INTR_FREQUENCY;
  }

  if (realSustainLev == 0)
    rgn->sustain_level = 1.0;
  else
    // rgn->sustain_level = 20 * log10 ((92544.0-realSustainLev) / 92544.0);
    rgn->sustain_level = static_cast<double>(0x16980 - realSustainLev) / 0x16980;

  // we express release rate as time from maximum volume, not sustain level
  count = 0x16980 / realRelease;
  rgn->release_time = count * INTR_FREQUENCY;

  if (Pan == 0)
    rgn->pan = 0;
  else if (Pan == 127)
    rgn->pan = 1.0;
  else if (Pan == 64)
    rgn->pan = 0.5;
  else
    rgn->pan = static_cast<double>(Pan) / 127;
}

uint16_t NDSInstr::getFallingRate(uint8_t DecayTime) const {
  uint32_t realDecay;
  if (DecayTime == 0x7F)
    realDecay = 0xFFFF;
  else if (DecayTime == 0x7E)
    realDecay = 0x3C00;
  else if (DecayTime < 0x32) {
    realDecay = DecayTime * 2;
    ++realDecay;
    realDecay &= 0xFFFF;
  } else {
    realDecay = 0x1E00;
    DecayTime = 0x7E - DecayTime;
    realDecay /= DecayTime;  // there is a whole subroutine that seems to resolve simply to this.  I
                             // have tested all cases
    realDecay &= 0xFFFF;
  }
  return static_cast<uint16_t>(realDecay);
}

// ***********
// NDSWaveArch
// ***********

NDSWaveArch::NDSWaveArch(RawFile *file, uint32_t offset, uint32_t length, std::string name)
    : VGMSampColl(NDSFormat::name, file, offset, length, std::move(name)) {
}

bool NDSWaveArch::parseHeader() {
  unLength = readWord(dwOffset + 8);
  return true;
}

bool NDSWaveArch::parseSampleInfo() {
  uint32_t nSamples = readWord(dwOffset + 0x38);
  for (uint32_t i = 0; i < nSamples; i++) {
    uint32_t pSample = readWord(dwOffset + 0x3C + i * 4) + dwOffset;
    int nChannels = 1;
    uint8_t waveType = readByte(pSample);
    bool bLoops = (readByte(pSample + 1) != 0);
    uint16_t rate = readShort(pSample + 2);
    BPS bps;
    // uint8_t multiplier;
    switch (waveType) {
      case NDSSamp::PCM8:
        bps = BPS::PCM8;
        break;
      case NDSSamp::PCM16:
        bps = BPS::PCM16;
        break;
      case NDSSamp::IMA_ADPCM:
        bps = BPS::PCM16;
        break;
      default:
        L_ERROR("Parsed invalid wave type: {}", waveType);
        bps = BPS::PCM16;
        break;
    }
    uint32_t loopOff =
        (readShort(pSample + 6)) *
        4;  //*multiplier; //represents loop point in words, excluding header supposedly
    uint32_t nonLoopLength =
        readShort(pSample + 8) * 4;  // if IMA-ADPCM, subtract one for the ADPCM header

    uint32_t dataStart, dataLength;
    if (waveType == NDSSamp::IMA_ADPCM) {
      dataStart = pSample + 0x10;
      dataLength = loopOff + nonLoopLength - 4;
    } else {
      dataStart = pSample + 0xC;
      dataLength = loopOff + nonLoopLength;
    }

    auto name = fmt::format("Sample {}", samples.size());
    NDSSamp *samp = new NDSSamp(this, pSample, dataStart + dataLength - pSample, dataStart,
                                dataLength, nChannels, bps, rate, waveType, name);

    if (waveType == NDSSamp::IMA_ADPCM) {
      samp->setLoopStartMeasure(LM_SAMPLES);
      samp->setLoopLengthMeasure(LM_SAMPLES);
      loopOff *= 2;               // now it's in samples
      loopOff = loopOff - 8 + 1;  // exclude the header's sample.  not exactly sure why 8.
      nonLoopLength = (dataLength * 2 + 1) - loopOff;
      samp->ulUncompressedSize = (nonLoopLength + loopOff) * 2;
    }

    samp->setLoopStatus(bLoops);
    samp->setLoopOffset(loopOff);
    samp->setLoopLength(nonLoopLength);
    samples.push_back(samp);
  }
  return true;
}

NDSPSG::NDSPSG(RawFile *file) : VGMSampColl(NDSFormat::name, file, 0, 0, "NDS PSG samples") {
}

bool NDSPSG::parseSampleInfo() {
  /* 8 waves + noise */
  for (uint8_t i = 0; i <= 8; i++) {
    samples.push_back(new NDSPSGSamp(this, i));
  }

  return true;
}

// *******
// NDSSamp
// *******

NDSSamp::NDSSamp(VGMSampColl *sampColl, uint32_t offset, uint32_t length, uint32_t dataOffset,
                 uint32_t dataLen, uint8_t nChannels, BPS bps, uint32_t theRate,
                 uint8_t theWaveType, std::string name)
    : VGMSamp(sampColl, offset, length, dataOffset, dataLen, nChannels, bps, theRate, std::move(name)),
      waveType(theWaveType) {
}

double NDSSamp::compressionRatio() const {
  if (waveType == IMA_ADPCM) {
    return 4.0;
  }

  return 1.0;
}

std::vector<uint8_t> NDSSamp::decodeToNativePcm() {
  if (waveType == IMA_ADPCM) {
    return decodeImaAdpcm();
  }

  return VGMSamp::decodeToNativePcm();
}

// From nocash's site: The NDS data consist of a 32bit header, followed by 4bit values (so each byte
// contains two values, the first value in the lower 4bits, the second in upper 4 bits). The 32bit
// header contains initial values:
//
//  Bit0-15   Initial PCM16 Value (Pcm16bit = -7FFFh..+7FFF) (not -8000h)
//  Bit16-22  Initial Table Index Value (Index = 0..88)
//  Bit23-31  Not used (zero)

// As far as I can tell, the NDS IMA-ADPCM format has one difference from standard IMA-ADPCM:
// it clamps min (and max?) sample values differently (see below).  I really don't know how much of
// a difference it makes, but this implementation is, to my knowledge, the proper way of doing
// things for NDS.
std::vector<uint8_t> NDSSamp::decodeImaAdpcm() {
  const uint32_t sampleCount = uncompressedSize() / sizeof(int16_t);
  std::vector<uint8_t> samples(sampleCount * sizeof(int16_t));
  auto *output = reinterpret_cast<int16_t*>(samples.data());
  uint32_t destOff = 0;
  uint32_t sampHeader = getWord(dataOff - 4);
  int decompSample = sampHeader & 0xFFFF;
  int stepIndex = (sampHeader >> 16) & 0x7F;

  uint32_t curOffset = dataOff;
  output[destOff++] = (int16_t)decompSample;

  uint8_t compByte;
  while (curOffset < dataOff + dataLength) {
    compByte = readByte(curOffset++);
    process_nibble(compByte, stepIndex, decompSample);
    output[destOff++] = (int16_t)decompSample;
    process_nibble((compByte & 0xF0) >> 4, stepIndex, decompSample);
    output[destOff++] = (int16_t)decompSample;
  }

  return samples;
}

// I'm copying nocash's IMA-ADPCM conversion method verbatim.  Big thanks to him.
// Info is at http://nocash.emubase.de/gbatek.htm#dssound and the algorithm is described as follows:
//
// The NDS data consist of a 32bit header, followed by 4bit values (so each byte contains two
// values, the first value in the lower 4bits, the second in upper 4 bits). The 32bit header
// contains initial values:
//
//  Bit0-15   Initial PCM16 Value (Pcm16bit = -7FFFh..+7FFF) (not -8000h)
//  Bit16-22  Initial Table Index Value (Index = 0..88)
//  Bit23-31  Not used (zero)
//
// In theory, the 4bit values are decoded into PCM16 values, as such:
//
//  Diff = ((Data4bit AND 7)*2+1)*AdpcmTable[Index]/8      ;see rounding-error
//  IF (Data4bit AND 8)=0 THEN Pcm16bit = Max(Pcm16bit+Diff,+7FFFh)
//  IF (Data4bit AND 8)=8 THEN Pcm16bit = Min(Pcm16bit-Diff,-7FFFh)
//  Index = MinMax (Index+IndexTable[Data4bit AND 7],0,88)
//
// In practice, the first line works like so (with rounding-error):
//
//  Diff = AdpcmTable[Index]/8
//  IF (data4bit AND 1) THEN Diff = Diff + AdpcmTable[Index]/4
//  IF (data4bit AND 2) THEN Diff = Diff + AdpcmTable[Index]/2
//  IF (data4bit AND 4) THEN Diff = Diff + AdpcmTable[Index]/1
//
// And, a note on the second/third lines (with clipping-error):
//
//  Max(+7FFFh) leaves -8000h unclipped (can happen if initial PCM16 was -8000h)
//  Min(-7FFFh) clips -8000h to -7FFFh (possibly unlike windows .WAV files?)

#define IMAMax(samp) (samp > 0x7FFF) ? ((short)0x7FFF) : samp
#define IMAMin(samp) (samp < -0x7FFF) ? ((short)-0x7FFF) : samp
#define IMAIndexMinMax(index, min, max) (index > max) ? max : ((index < min) ? min : index)

void NDSSamp::process_nibble(unsigned char data4bit, int &Index, int &Pcm16bit) {
  // int Diff = ((Data4bit & 7)*2+1)*AdpcmTable[Index]/8;
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

void NDSSamp::clamp_step_index(int &stepIndex) {
  if (stepIndex < 0)
    stepIndex = 0;
  if (stepIndex > 88)
    stepIndex = 88;
}

void NDSSamp::clamp_sample(int &decompSample) {
  if (decompSample < -32768)
    decompSample = -32768;
  if (decompSample > 32767)
    decompSample = 32767;
}

NDSPSGSamp::NDSPSGSamp(VGMSampColl *sampcoll, uint8_t duty_cycle) : VGMSamp(sampcoll) {
  switch (duty_cycle) {
    case 7: {
      m_duty_cycle = 0;
      break;
    }
    case 0: {
      m_duty_cycle = 0.125;
      break;
    }
    case 1: {
      m_duty_cycle = 0.25;
      break;
    }
    case 2: {
      m_duty_cycle = 0.375;
      break;
    }
    case 3: {
      m_duty_cycle = 0.5;
      break;
    }
    case 4: {
      m_duty_cycle = 0.625;
      break;
    }
    case 5: {
      m_duty_cycle = 0.75;
      break;
    }
    case 6: {
      m_duty_cycle = 0.875;
      break;
    }
    default: {
      /* We don't care about the rest */
      break;
    }
  }

  setNumChannels(1);
  /* This is the NDS mixer frequency */
  setRate(32768);
  setBPS(BPS::PCM16);

  setLoopStatus(true);

  setLoopOffset(0);
  setLoopLength(32768);
  setLoopStartMeasure(LM_SAMPLES);
  setLoopLengthMeasure(LM_SAMPLES);
  ulUncompressedSize = 32768 * bytesPerSample();

  setName("PSG_duty_" + std::to_string(duty_cycle));
}

std::vector<uint8_t> NDSPSGSamp::decodeToNativePcm() {
  const uint32_t sampleCount = uncompressedSize() / sizeof(int16_t);
  std::vector<uint8_t> samples(sampleCount * sizeof(int16_t));
  auto *output = reinterpret_cast<int16_t*>(samples.data());

  /* Noise mode */
  if (m_duty_cycle == -1) {
    int16_t value = 0x7FFF;
    output[0] = 0x7FFF;
    for (int i = 1, len = loopLength(); i < len; i++) {
      bool carry = value & 0x0001;
      value >>= 1;
      if (carry) {
        output[i] = -0x7FFF;
        value ^= 0x6000;
      } else {
        output[i] = 0x7FFF;
      }
    }
  } else {
    /*
     * PSG wave mode
     * It's band limited so it should sound nice!
     */

    /* Generate Fourier coefficients */
    std::vector<double> coefficients = {m_duty_cycle - 0.5};
    {
      int i = 1;
      std::generate_n(std::back_inserter(coefficients), rate / (440 * 2),
                      [duty_cycle = m_duty_cycle, &i]() {
                        double val = sin(i * duty_cycle * M_PI) * 2 / (i * M_PI);
                        i++;

                        return val;
                      });
    }

    /* Generate audio */
    double scale = 440 * M_PI * 2 / rate;
    for (int i = 0, len = loopLength(); i < len; i++) {
      int counter = 0;
      double value = std::accumulate(std::begin(coefficients), std::end(coefficients), 0.0,
                                     [i, scale, &counter](double sum, double coef) {
                                       sum += coef * cos(counter++ * scale * i);
                                       return sum;
                                     });

      /* We have to go from F64 to S16 */
      int16_t out_value = static_cast<int16_t>(std::round(value * 0x7FFF));
      output[i] = out_value;
    }
  }

  return samples;
}
