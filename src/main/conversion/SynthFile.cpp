/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "SynthFile.h"

#include <spdlog/fmt/fmt.h>
#include "VGMSamp.h"

//  **********************************************************************************
//  SynthFile - An intermediate class to lay out all of the the data necessary for Coll conversion
//				to DLS or SF2 formats.  Currently, the structure is identical to DLS.
//  **********************************************************************************

SynthFile::SynthFile(std::string synth_name)
    : m_name(std::move(synth_name)) {}

SynthFile::~SynthFile() {
  deleteVect(vInstrs);
  deleteVect(vWaves);
}

SynthInstr *SynthFile::addInstr(uint32_t bank, uint32_t instrNum, float reverb) {
  auto str = fmt::format("Instr bnk {} num {}", bank, instrNum);
  vInstrs.insert(vInstrs.end(), new SynthInstr(bank, instrNum, str, reverb));
  return vInstrs.back();
}

SynthInstr *SynthFile::addInstr(uint32_t bank, uint32_t instrNum, std::string name, float reverb) {
  vInstrs.insert(vInstrs.end(), new SynthInstr(bank, instrNum, std::move(name), reverb));
  return vInstrs.back();
}

SynthWave *SynthFile::addWave(uint16_t formatTag,
                              uint16_t channels,
                              int samplesPerSec,
                              int aveBytesPerSec,
                              uint16_t blockAlign,
                              uint16_t bitsPerSample,
                              uint32_t waveDataSize,
                              std::vector<uint8_t> waveData,
                              std::string name) {
  vWaves.insert(vWaves.end(),
                new SynthWave(formatTag,
                              channels,
                              samplesPerSec,
                              aveBytesPerSec,
                              blockAlign,
                              bitsPerSample,
                              waveDataSize,
                              std::move(waveData),
                              std::move(name)));
  return vWaves.back();
}

//  **********
//  SynthInstr
//  **********


SynthInstr::SynthInstr(uint32_t bank, uint32_t instrument, float reverb)
    : ulBank(bank), ulInstrument(instrument), reverb(reverb) {
  name = fmt::format("Instr bnk {} num {}", bank, instrument);
  //RiffFile::AlignName(name);
}

SynthInstr::SynthInstr(uint32_t bank, uint32_t instrument, std::string instrName, float reverb)
    : ulBank(bank), ulInstrument(instrument), name(std::move(instrName)), reverb(reverb)  {
  //RiffFile::AlignName(name);
}

SynthInstr::SynthInstr(uint32_t bank, uint32_t instrument, std::string instrName,
                       const std::vector<SynthRgn *>& listRgns, float reverb)
    : ulBank(bank), ulInstrument(instrument), name(std::move(instrName)), reverb(reverb)  {
  //RiffFile::AlignName(name);
  vRgns = listRgns;
}

SynthInstr::~SynthInstr() {
  deleteVect(vRgns);
}

SynthRgn *SynthInstr::addRgn() {
  vRgns.insert(vRgns.end(), new SynthRgn());
  return vRgns.back();
}

SynthRgn *SynthInstr::addRgn(const SynthRgn& rgn) {
  SynthRgn *newRgn = new SynthRgn();
  *newRgn = rgn;
  vRgns.insert(vRgns.end(), newRgn);
  return vRgns.back();
}

//  ********
//  SynthRgn
//  ********

SynthRgn::~SynthRgn() {
  delete sampinfo;
  delete art;
}

SynthArt *SynthRgn::addArt() {
  art = new SynthArt();
  return art;
}

SynthSampInfo *SynthRgn::addSampInfo() {
  sampinfo = new SynthSampInfo();
  return sampinfo;
}

void SynthRgn::setRanges(uint16_t keyLow, uint16_t keyHigh, uint16_t velLow, uint16_t velHigh) {
  usKeyLow = keyLow;
  usKeyHigh = keyHigh;
  usVelLow = velLow;
  usVelHigh = velHigh;
}

void SynthRgn::setWaveLinkInfo(uint16_t options, uint16_t phaseGroup, uint32_t theChannel, uint32_t theTableIndex) {
  fusOptions = options;
  usPhaseGroup = phaseGroup;
  channel = theChannel;
  tableIndex = theTableIndex;
}

void SynthRgn::setFineTune(int16_t semitones, int16_t cents) {
  coarseTuneSemitones = semitones;
  fineTuneCents = cents;
}

void SynthRgn::setAttenuationDb(double attenuation) {
  attenDb = attenuation;
}

//  ********
//  SynthArt
//  ********

SynthArt::~SynthArt() {
  //DeleteVect(vConnBlocks);
}

void SynthArt::addADSR(double attack, Transform atk_transform, double hold, double decay,
                       double sustain_level, double sustain, double release, Transform rls_transform) {
  this->attack_time = attack;
  this->attack_transform = atk_transform;
  this->hold_time = hold;
  this->decay_time = decay;
  this->sustain_lev = sustain_level;
  this->sustain_time = sustain;
  this->release_time = release;
  this->release_transform = rls_transform;
}

void SynthArt::addPan(double thePan) {
  this->pan = thePan;
}

//  *************
//  SynthSampInfo
//  *************

void SynthSampInfo::setLoopInfo(Loop &loop, VGMSamp *samp) {
  const int origFormatBytesPerSamp = samp->bps / 8;
  double compressionRatio = samp->compressionRatio();

  // If the sample loops, but the loop length is 0, then assume the length should
  // extend to the end of the sample.
  if (loop.loopStatus && loop.loopLength == 0)
    loop.loopLength = samp->dataLength - loop.loopStart;

  cSampleLoops = loop.loopStatus;
  ulLoopType = loop.loopType;
  ulLoopStart = (loop.loopStartMeasure == LM_BYTES)
                  ? static_cast<uint32_t>((loop.loopStart * compressionRatio) / origFormatBytesPerSamp)
                  : loop.loopStart;
  ulLoopLength = (loop.loopLengthMeasure == LM_BYTES)
                   ? static_cast<uint32_t>((loop.loopLength * compressionRatio) / origFormatBytesPerSamp)
                   : loop.loopLength;
}

void SynthSampInfo::setPitchInfo(uint16_t unityNote, short fineTune, double atten) {
  usUnityNote = unityNote;
  sFineTune = fineTune;
  attenuation = atten;
}

//  *********
//  SynthWave
//  *********

void SynthWave::convertTo16bit() {
  if (wBitsPerSample == 8) {
    wBitsPerSample = 16;
    wBlockAlign = 16 / 8 * wChannels;
    dwAveBytesPerSec *= 2;

    std::vector<uint8_t> newData(dataSize * 2u);
    for (uint32_t i = 0; i < dataSize; i++) {
      const int16_t sample = static_cast<int16_t>(data[i]) << 8;
      const uint16_t u = static_cast<uint16_t>(sample);
      newData[i * 2] = static_cast<uint8_t>(u & 0xFF);
      newData[i * 2 + 1] = static_cast<uint8_t>((u >> 8) & 0xFF);
    }
    data = std::move(newData);
    dataSize *= 2;
  }
}

SynthWave::~SynthWave() {
  delete sampinfo;
}

SynthSampInfo *SynthWave::addSampInfo() {
  sampinfo = new SynthSampInfo();
  return sampinfo;
}
