/*
 * VGMTrans (c) 2002-2025
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "SegSatInstrSet.h"

#include "ScaleConversion.h"
#include "VGMSamp.h"
#include <spdlog/fmt/fmt.h>

struct GainsDB { double left_dB, right_dB; };

static const float SDLT[8] = {1000000.0f,36.0f,30.0f,24.0f,18.0f,12.0f,6.0f,0.0f};

struct PanLinAmp { double lPan, rPan; };

constexpr PanLinAmp panToAmp(u8 pan) {
  int iPAN = pan;
  float SegaDB = 0.0f;
  float PAN;
  float LPAN,RPAN;

  SegaDB=0;
  if (iPAN & 0x1) SegaDB -= 3.0f;
  if (iPAN & 0x2) SegaDB -= 6.0f;
  if (iPAN & 0x4) SegaDB -= 12.0f;
  if (iPAN & 0x8) SegaDB -= 24.0f;

  if ((iPAN & 0xf) == 0xf) PAN = 0.0;
  else PAN=powf(10.0f, SegaDB / 20.0f);

  if (iPAN < 0x10) {
    LPAN = PAN;
    RPAN = 1.0;
  }
  else {
    RPAN = PAN;
    LPAN = 1.0;
  }

  return { LPAN, RPAN };
}


// **************
// SegSatInstrSet
// **************

SegSatInstrSet::SegSatInstrSet(RawFile* file, uint32_t offset, int numInstrs, const std::string& name) :
    VGMInstrSet(SegSatFormat::name, file, offset, 0, name), m_numInstrs(numInstrs) {
  sampColl = new VGMSampColl(SegSatFormat::name, file, this, offset);
}

bool SegSatInstrSet::parseHeader() {
  addChild(dwOffset + 0, 2, "Mixer Tables Pointer");
  addChild(dwOffset + 2, 2, "Velocity Tables Pointer");
  addChild(dwOffset + 4, 2, "PEG Tables Pointer");
  addChild(dwOffset + 6, 2, "PLFO Tables Pointer");

  u32 mixerTablesOffset = readShortBE(dwOffset) + dwOffset;
  u32 vlTablesOffset = readShortBE(dwOffset + 2) + dwOffset;
  u32 pegTablesOffset = readShortBE(dwOffset + 4) + dwOffset;
  u32 plfoTablesOffset = readShortBE(dwOffset + 6) + dwOffset;
  u32 firstInstrOffset = readShortBE(dwOffset + 8) + dwOffset;

  // Parse Mixer Tables
  u32 offset = mixerTablesOffset;
  int i = 0;
  do {
    SegSatMixerTable mixerTable;
    readBytes(offset, 18, &mixerTable);
    m_mixerTables.push_back(mixerTable);

    auto tableItem = addChild(offset, 18, fmt::format("Mixer Table {:d}", i));
    for (int j = 0; j < 18; ++j) {
      tableItem->addChild(offset + j, 1, fmt::format("Channel {:d}  Effect Send/Pan", j));
    }
    offset += 18;
    ++i;
  } while (offset < vlTablesOffset);

  // Parse Velocity Level Tables
  offset = vlTablesOffset;
  i = 0;
  do {
    SegSatVLTable vlTable;
    readBytes(offset, 10, &vlTable);
    m_vlTables.push_back(vlTable);

    auto tableItem = addChild(offset, 10, fmt::format("VL Table {:d}", i));
    tableItem->addChild(offset, 1, "Rate 0");
    tableItem->addChild(offset + 1, 1, "Point 0");
    tableItem->addChild(offset + 2, 1, "Level 0");
    tableItem->addChild(offset + 3, 1, "Rate 1");
    tableItem->addChild(offset + 4, 1, "Point 1");
    tableItem->addChild(offset + 5, 1, "Level 1");
    tableItem->addChild(offset + 6, 1, "Rate 2");
    tableItem->addChild(offset + 7, 1, "Point 2");
    tableItem->addChild(offset + 8, 1, "Level 2");
    tableItem->addChild(offset + 9, 1, "Rate 3");

    offset += 10;
    ++i;
  } while (offset < pegTablesOffset);

  // Parse PLFO Tables
  offset = plfoTablesOffset;
  i = 0;
  do {
    SegSatPlfoTable plfoTable;
    readBytes(offset, 4, &plfoTable);
    m_plfoTables.push_back(plfoTable);

    auto tableItem = addChild(offset, 4, fmt::format("PLFO Table {:d}", i));
    tableItem->addChild(offset, 1, "Delay");
    tableItem->addChild(offset + 1, 1, "Frequency");
    tableItem->addChild(offset + 2, 1, "Amplitude");
    tableItem->addChild(offset + 3, 1, "Fade Time");

    offset += 4;
    ++i;
  } while (offset < firstInstrOffset);

  return true;
}

bool SegSatInstrSet::parseInstrPointers() {
  size_t off = dwOffset + 8;
  auto instrList = addChild(off, m_numInstrs * 2, "Instrument Pointers");
  for (int i = 0; i < m_numInstrs; i++) {
    u32 instrOff = rawFile()->getBE<u16>(off + (i * 2)) + dwOffset;
    u8 numRgns = rawFile()->readByte(instrOff + 2) + 1;
    size_t instrSize = 4 + numRgns * 0x20;
    auto name = fmt::format("Instrument {:d}", i);
    aInstrs.push_back(new SegSatInstr(this, instrOff, instrSize, 0, i, name));
    instrList->addChild(off + (i * 2), 2, fmt::format("Instrument {:d} Pointer", i));
  }

  return true;
}


// ***********
// SegSatInstr
// ***********

SegSatInstr::SegSatInstr(SegSatInstrSet* set, size_t offset, size_t length, u32 bank, u32 number, const std::string &name)
    : VGMInstr(set, offset, length, bank, number, name) {
}

bool SegSatInstr::loadInstr() {
  addChild(dwOffset, 1, "Pitchbend Range");
  addChild(dwOffset+1, 1, "Portamento");
  addChild(dwOffset+2, 1, "Region Count");
  addChild(dwOffset+3, 1, "Volume Bias");
  m_pitchBendRange = rawFile()->readByte(dwOffset);
  m_portamento = rawFile()->readByte(dwOffset + 1);
  u8 numRgns = rawFile()->readByte(dwOffset + 2) + 1;
  m_volBias = rawFile()->readByte(dwOffset + 3);

  auto sampColl = parInstrSet->sampColl;
  for (int i = 0; i < numRgns; ++i) {
    // Add region
    auto name = fmt::format("Region {:d}", i);
    auto rgn = new SegSatRgn(this, dwOffset + 4 + (i * 0x20), name);
    addRgn(rgn);

    // Add sample
    u32 sampLength = rgn->sampleLoopEnd();
    u8 bps = rgn->sampleType() == SegSatRgn::SampleType::PCM16 ? 16 : 8;
    auto instrSet = static_cast<SegSatInstrSet*>(parInstrSet);
    // check if a sample at the offset was already added
    bool inserted = instrSet->sampleOffsets.insert(rgn->sampOffset).second;

    u32 sampOffset = rgn->sampOffset;// & (bps == 16 ? ~1 : ~0);
    if (inserted) {
      auto sample = sampColl->addSamp(
       sampOffset,
       sampLength,
       sampOffset,
       sampLength,
       1,
       bps,
       44100,
       fmt::format("Sample: 0x{:X}",  rgn->sampOffset)
      );
      sample->setWaveType(bps == 16 ? WT_PCM16 : WT_PCM8);
      sample->setSignedness(Signedness::Signed);
      sample->loop = rgn->loop;
      sample->setEndianness(Endianness::Big);
      // TODO: We should only reverse the looped portion of the sample
      if (rgn->loopType() == SegSatRgn::LoopType::Reverse)
        sample->setReverse(true);

      size_t newLength = sampOffset + sampLength - instrSet->dwOffset;
      if (sampColl->unLength < newLength) {
        sampColl->unLength = newLength;
      }
      if (instrSet->unLength < newLength) {
        instrSet->unLength = newLength;
      }
    }
  }
  return true;
}

// *********
// SegSatRgn
// *********

SegSatRgn::SegSatRgn(SegSatInstr* instr, uint32_t offset, const std::string& name) :
  VGMRgn(instr, offset, 0x20, name) {
  addKeyLow(readByte(offset), offset, 1);
  addKeyHigh(readByte(offset+1), offset+1, 1);

  u8 pitchFlags = readByte(offset+2);
  m_enablePeg = pitchFlags >> 7;
  m_enablePlfo = (pitchFlags >> 6) & 1;
  addChild(offset + 2, 1, "Enable PEG / PLFO Flags");


  u8 loopFlag = (readByte(offset + 3) >> 5) & 3;
  m_loopType = static_cast<LoopType>(loopFlag);
  m_sampleType = static_cast<SampleType>((readByte(offset + 3) >> 4) & 1);

  u8 bytesPerSamp = m_sampleType == SampleType::PCM16 ? 2 : 1;

  u32 instrSetOffset = parInstr->parInstrSet->dwOffset;
  sampOffset = (getWordBE(offset + 2) & 0x7FFFF);
  if (m_sampleType == SampleType::PCM16)
    sampOffset = sampOffset & ~1;
  sampOffset += instrSetOffset;

  addChild(offset + 3, 3, "Sample Offset, Sample Type, Loop Type");
  m_sampLoopStart = getShortBE(offset + 6) * bytesPerSamp;
  m_sampLoopEnd = getShortBE(offset + 8) * bytesPerSamp;
  setLoopInfo(m_loopType == LoopType::Off ? 0 : 1, m_sampLoopStart, m_sampLoopEnd - m_sampLoopStart);
  addChild(offset + 6, 2, "Loop Start");
  addChild(offset + 8, 2, "Loop End");

  u16 adsr1 = getShortBE(offset + 10);
  m_attackRate = adsr1 & 0x1F;
  m_decayRate1 = (adsr1 >> 6) & 0x1F;
  m_decayRate2 = (adsr1 >> 11) & 0x1F;
  addADSRValue(offset + 10, 2, "Attack Rate, Decay Rate, Sustain Rate");
  u16 adsr2 = getShortBE(offset + 12);
  m_releaseRate = adsr2 & 0x1F;
  m_decayLevel = (adsr2 >> 5) & 0x1F;
  m_keyRateScale = (adsr2 >> 10) & 0x1F;
  addADSRValue(offset + 12, 2, "Release Rate, Sustain Level, Key Rate Scale");

  // Calculate envelope values in seconds
  // MAME starts the envelope at 0x17F. The ARTimes table represents time from 0 to max (0x3FF).
  // Effectively, this scales down AR time to 62.5%
  attack_time = (ARTimes[m_attackRate * 2] / 1000.0) * 0.625;
  attack_time *= 0.625;
  decay_time = DRTimes[m_decayRate1 * 2] / 1000.0;
  sustain_level = (31 - m_decayLevel) / 31.0;

  release_time = DRTimes[m_releaseRate * 2] / 1000.0;
  double decay2Time = DRTimes[m_decayRate2 * 2] / 1000.0;

  if ((decay_time < 0.5 && sustain_level > 0.7) && decay2Time < 88600) {
    decay_time = decay2Time;
    sustain_level = 0;
  }

  u8 enableModFlags = readByte(offset + 14);
  m_enableLfoModulation = (enableModFlags >> 7) & 1;
  m_enablePegModulation = (enableModFlags >> 6) & 1;
  m_enablePlfoModulation = (enableModFlags >> 5) & 1;
  addChild(offset + 14, 1, "Enable PEG Modulation / PLFO Modulation Flags");

  m_totalLevel = readByte(offset + 15);
  addChild(offset + 15, 1, "Total Level");

  m_enableTotalLevelModulation = (readByte(offset + 18) >> 2) & 1;
  addChild(offset + 18, 1, "Enable Total Level Modulation");

  u8 lfoResetFreqAndWave = readByte(offset + 20);
  m_lfoReset = (lfoResetFreqAndWave & 0x80) > 0;
  m_lfoFreq = (lfoResetFreqAndWave >> 2) & 0x1F;
  u8 lfoWave1 = (lfoResetFreqAndWave) & 1;
  u8 lfoWave2 = (readByte(offset + 21) >> 3) & 3;

  // The pitch LFO wave selection is split across bits non-contiguous bits. Very weird.
  if (lfoWave1 == 1)
    m_pitchLfoWave = LFOWave::Square;
  else if (lfoWave2 == 2)
    m_pitchLfoWave = LFOWave::Triangle;
  else if (lfoWave2 == 3)
    m_pitchLfoWave = LFOWave::Noise;
  else
    m_pitchLfoWave = LFOWave::SawTooth;
  addChild(offset + 20, 1, "LFO Frequency, LFO Reset, Pitch LFO Wave");

  u8 lfoPitchDepthAmpWaveAmpDepth = readByte(offset + 21);
  m_pitchLfoDepth = (lfoPitchDepthAmpWaveAmpDepth >> 5) & 7;
  u8 ampLfoWave = (lfoPitchDepthAmpWaveAmpDepth >> 3) & 3;
  m_ampLfoWave = static_cast<LFOWave>(ampLfoWave);
  m_ampLfoDepth = lfoPitchDepthAmpWaveAmpDepth & 0x7;

  addChild(offset + 21, 1, "Pitch LFO Depth, Amp LFO Depth, Amp LFO Wave");
  addChild(offset + 23, 1, "Effect Select, Effect Send");

  u8 disdl_dipan = readByte(offset + 24);
  m_directLevel = (disdl_dipan >> 5) & 0x7;
  m_directPan = disdl_dipan & 0x1F;
  addChild(offset + 24, 1, "Direct Level, Direct Pan");

  addUnityKey(readByte(offset + 25), offset + 0x19, 1);
  s8 fineTuneByte = static_cast<s8>(readByte(offset + 26));
  s16 fineTuneCents = (fineTuneByte / 128.0) * 50;
  addFineTune(fineTuneCents, offset + 26, 1);

  m_vlTableIndex = readByte(offset + 29);
  addChild(offset + 29, 1, "Velocity Table Index");

  m_PegIndex = readByte(offset + 30);
  m_PlfoIndex = readByte(offset + 31);

  addChild(offset + 30, 1, "PEG Index");
  addChild(offset + 31, 1, "PLFO Index");

  // Calculate attenuation, using TL, DISDL, and PAN
  double sdlDb = SDLT[m_directLevel];
  double tlDb = tlToDB(m_totalLevel);
  PanLinAmp pan = panToAmp(m_directPan);

  double volScale;
  double panPerc = convertVolumeBalanceToStdMidiPercentPan(pan.lPan, pan.rPan, &volScale);
  const double fiftyFiftyComp = 3.0103;
  double finalAtten = sdlDb + tlDb + ampToDb(volScale) + fiftyFiftyComp;
  setAttenuation(finalAtten);
  this->pan = panPerc;
}
