/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "base/types.h"
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

SynthInstr *SynthFile::addInstr(u32 bank, u32 instrNum, float reverb) {
  auto str = fmt::format("Instr bnk {} num {}", bank, instrNum);
  vInstrs.insert(vInstrs.end(), new SynthInstr(bank, instrNum, str, reverb));
  return vInstrs.back();
}

SynthInstr *SynthFile::addInstr(u32 bank, u32 instrNum, std::string name, float reverb) {
  vInstrs.insert(vInstrs.end(), new SynthInstr(bank, instrNum, std::move(name), reverb));
  return vInstrs.back();
}

SynthWave *SynthFile::addWave(u16 formatTag,
                              u16 channels,
                              int samplesPerSec,
                              int aveBytesPerSec,
                              u16 blockAlign,
                              u16 bitsPerSample,
                              u32 waveDataSize,
                              std::vector<u8> waveData,
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


SynthInstr::SynthInstr(u32 bank, u32 instrument, float reverb)
    : ulBank(bank), ulInstrument(instrument), reverb(reverb) {
  name = fmt::format("Instr bnk {} num {}", bank, instrument);
  //RiffFile::AlignName(name);
}

SynthInstr::SynthInstr(u32 bank, u32 instrument, std::string instrName, float reverb)
    : ulBank(bank), ulInstrument(instrument), name(std::move(instrName)), reverb(reverb)  {
  //RiffFile::AlignName(name);
}

SynthInstr::SynthInstr(u32 bank, u32 instrument, std::string instrName,
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

void SynthInstr::addModulator(const SynthModulator& modulator) {
  m_modulators.push_back(modulator);
}

// Add a modulator using a specific ModSource. This will bypass the ModDest->ModSource mapping in ConversionOptions
void SynthInstr::addModulator(ModSource source, ModDest destination, ModAmount amount) {
  if (!amount.valid()) {
    return;
  }

  m_modulators.emplace_back(source, destination, amount.value());
}

// Add a modulator without specifying the ModSource. The ModDest->ModSource mapping in ConversionOptions will be used
void SynthInstr::addModulator(ModDest destination, ModAmount amount) {
  if (!amount.valid()) {
    return;
  }

  m_modulators.emplace_back(destination, amount.value());
}

void SynthInstr::addGenerator(const SynthGenerator& generator) {
  m_generators.push_back(generator);
}

void SynthInstr::addGenerator(ModDest destination, ModAmount amount) {
  if (!amount.valid()) {
    return;
  }

  m_generators.push_back({destination, amount.value()});
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

void SynthRgn::setRanges(u16 keyLow, u16 keyHigh, u16 velLow, u16 velHigh) {
  usKeyLow = keyLow;
  usKeyHigh = keyHigh;
  usVelLow = velLow;
  usVelHigh = velHigh;
}

void SynthRgn::setWaveLinkInfo(u16 options, u16 phaseGroup, u32 theChannel, u32 theTableIndex) {
  fusOptions = options;
  usPhaseGroup = phaseGroup;
  channel = theChannel;
  tableIndex = theTableIndex;
}

void SynthRgn::setFineTune(s16 semitones, s16 cents) {
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
  const int origFormatBytesPerSamp = samp->bytesPerSample();
  double compressionRatio = samp->compressionRatio();

  // If the sample loops, but the loop length is 0, then assume the length should
  // extend to the end of the sample.
  if (loop.loopStatus && loop.loopLength == 0)
    loop.loopLength = samp->dataLength - loop.loopStart;

  cSampleLoops = loop.loopStatus;
  ulLoopType = loop.loopType;
  ulLoopStart = (loop.loopStartMeasure == LM_BYTES)
                  ? static_cast<u32>((loop.loopStart * compressionRatio) / origFormatBytesPerSamp)
                  : loop.loopStart;
  ulLoopLength = (loop.loopLengthMeasure == LM_BYTES)
                   ? static_cast<u32>((loop.loopLength * compressionRatio) / origFormatBytesPerSamp)
                   : loop.loopLength;
}

void SynthSampInfo::setPitchInfo(u16 unityNote, short fineTune, double atten) {
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

    std::vector<u8> newData(dataSize * 2u);
    for (u32 i = 0; i < dataSize; i++) {
      const s16 sample = static_cast<s16>(data[i]) << 8;
      const u16 u = static_cast<u16>(sample);
      newData[i * 2] = static_cast<u8>(u & 0xFF);
      newData[i * 2 + 1] = static_cast<u8>((u >> 8) & 0xFF);
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
