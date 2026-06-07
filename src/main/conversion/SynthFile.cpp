/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "SynthFile.h"

#include "base/Types.h"
#include "VGMSamp.h"

#include <utility>

#include <spdlog/fmt/fmt.h>

//  **********************************************************************************
//  SynthFile - An intermediate class to lay out all of the the data necessary for Coll conversion
//				to DLS or SF2 formats.  Currently, the structure is identical to DLS.
//  **********************************************************************************

SynthFile::SynthFile(std::string name)
    : m_name(std::move(name)) {}

SynthFile::~SynthFile() = default;

SynthInstr *SynthFile::addInstr(u32 bank, u32 instrNum, float reverb) {
  auto str = fmt::format("Instr bnk {} num {}", bank, instrNum);
  auto instr = std::make_unique<SynthInstr>(bank, instrNum, str, reverb);
  auto *rawInstr = instr.get();
  sinkInstr(std::move(instr));
  return rawInstr;
}

SynthInstr *SynthFile::addInstr(u32 bank, u32 instrNum, std::string name, float reverb) {
  auto instr = std::make_unique<SynthInstr>(bank, instrNum, std::move(name), reverb);
  auto *rawInstr = instr.get();
  sinkInstr(std::move(instr));
  return rawInstr;
}

void SynthFile::sinkInstr(std::unique_ptr<SynthInstr>&& instr) {
  m_instrObservers.push_back(instr.get());
  m_instrs.push_back(std::move(instr));
}

std::vector<std::unique_ptr<SynthInstr>> SynthFile::releaseInstrs() {
  m_instrObservers.clear();
  return std::exchange(m_instrs, {});
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
  auto wave = std::make_unique<SynthWave>(formatTag,
                                          channels,
                                          samplesPerSec,
                                          aveBytesPerSec,
                                          blockAlign,
                                          bitsPerSample,
                                          waveDataSize,
                                          std::move(waveData),
                                          std::move(name));
  auto *rawWave = wave.get();
  sinkWave(std::move(wave));
  return rawWave;
}

void SynthFile::sinkWave(std::unique_ptr<SynthWave>&& wave) {
  m_waveObservers.push_back(wave.get());
  m_waves.push_back(std::move(wave));
}

std::vector<std::unique_ptr<SynthWave>> SynthFile::releaseWaves() {
  m_waveObservers.clear();
  return std::exchange(m_waves, {});
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

SynthInstr::~SynthInstr() = default;

SynthRgn *SynthInstr::addRgn() {
  auto rgn = std::make_unique<SynthRgn>();
  auto *rawRgn = rgn.get();
  sinkRgn(std::move(rgn));
  return rawRgn;
}

SynthRgn *SynthInstr::addRgn(const SynthRgn& rgn) {
  auto newRgn = std::make_unique<SynthRgn>(rgn);
  auto *rawRgn = newRgn.get();
  sinkRgn(std::move(newRgn));
  return rawRgn;
}

void SynthInstr::sinkRgn(std::unique_ptr<SynthRgn>&& rgn) {
  m_regionObservers.push_back(rgn.get());
  m_regions.push_back(std::move(rgn));
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

SynthRgn::SynthRgn(const SynthRgn& other)
    : usKeyLow(other.usKeyLow),
      usKeyHigh(other.usKeyHigh),
      usVelLow(other.usVelLow),
      usVelHigh(other.usVelHigh),
      fusOptions(other.fusOptions),
      usPhaseGroup(other.usPhaseGroup),
      channel(other.channel),
      tableIndex(other.tableIndex),
      coarseTuneSemitones(other.coarseTuneSemitones),
      fineTuneCents(other.fineTuneCents),
      attenDb(other.attenDb),
      m_lfoVibFreqHz(other.m_lfoVibFreqHz),
      m_lfoVibDepthCents(other.m_lfoVibDepthCents),
      m_lfoVibDelaySeconds(other.m_lfoVibDelaySeconds) {
  if (other.sampinfo) {
    m_sampinfo = std::make_unique<SynthSampInfo>(*other.sampinfo);
    sampinfo = m_sampinfo.get();
  }
  if (other.art) {
    m_art = std::make_unique<SynthArt>(*other.art);
    art = m_art.get();
  }
}

SynthRgn::SynthRgn(SynthRgn&& other) noexcept
    : usKeyLow(other.usKeyLow),
      usKeyHigh(other.usKeyHigh),
      usVelLow(other.usVelLow),
      usVelHigh(other.usVelHigh),
      fusOptions(other.fusOptions),
      usPhaseGroup(other.usPhaseGroup),
      channel(other.channel),
      tableIndex(other.tableIndex),
      coarseTuneSemitones(other.coarseTuneSemitones),
      fineTuneCents(other.fineTuneCents),
      attenDb(other.attenDb),
      m_sampinfo(std::move(other.m_sampinfo)),
      m_art(std::move(other.m_art)),
      m_lfoVibFreqHz(other.m_lfoVibFreqHz),
      m_lfoVibDepthCents(other.m_lfoVibDepthCents),
      m_lfoVibDelaySeconds(other.m_lfoVibDelaySeconds) {
  sampinfo = m_sampinfo.get();
  art = m_art.get();
  other.sampinfo = nullptr;
  other.art = nullptr;
}

SynthRgn& SynthRgn::operator=(const SynthRgn& other) {
  if (this == &other) {
    return *this;
  }

  usKeyLow = other.usKeyLow;
  usKeyHigh = other.usKeyHigh;
  usVelLow = other.usVelLow;
  usVelHigh = other.usVelHigh;
  fusOptions = other.fusOptions;
  usPhaseGroup = other.usPhaseGroup;
  channel = other.channel;
  tableIndex = other.tableIndex;
  coarseTuneSemitones = other.coarseTuneSemitones;
  fineTuneCents = other.fineTuneCents;
  attenDb = other.attenDb;
  m_lfoVibFreqHz = other.m_lfoVibFreqHz;
  m_lfoVibDepthCents = other.m_lfoVibDepthCents;
  m_lfoVibDelaySeconds = other.m_lfoVibDelaySeconds;

  m_sampinfo = other.sampinfo ? std::make_unique<SynthSampInfo>(*other.sampinfo) : nullptr;
  sampinfo = m_sampinfo.get();
  m_art = other.art ? std::make_unique<SynthArt>(*other.art) : nullptr;
  art = m_art.get();

  return *this;
}

SynthRgn& SynthRgn::operator=(SynthRgn&& other) noexcept {
  if (this == &other) {
    return *this;
  }

  usKeyLow = other.usKeyLow;
  usKeyHigh = other.usKeyHigh;
  usVelLow = other.usVelLow;
  usVelHigh = other.usVelHigh;
  fusOptions = other.fusOptions;
  usPhaseGroup = other.usPhaseGroup;
  channel = other.channel;
  tableIndex = other.tableIndex;
  coarseTuneSemitones = other.coarseTuneSemitones;
  fineTuneCents = other.fineTuneCents;
  attenDb = other.attenDb;
  m_lfoVibFreqHz = other.m_lfoVibFreqHz;
  m_lfoVibDepthCents = other.m_lfoVibDepthCents;
  m_lfoVibDelaySeconds = other.m_lfoVibDelaySeconds;
  m_sampinfo = std::move(other.m_sampinfo);
  m_art = std::move(other.m_art);
  sampinfo = m_sampinfo.get();
  art = m_art.get();
  other.sampinfo = nullptr;
  other.art = nullptr;

  return *this;
}

SynthArt *SynthRgn::addArt() {
  m_art = std::make_unique<SynthArt>();
  art = m_art.get();
  return art;
}

SynthSampInfo *SynthRgn::addSampInfo() {
  m_sampinfo = std::make_unique<SynthSampInfo>();
  sampinfo = m_sampinfo.get();
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

SynthSampInfo *SynthWave::addSampInfo() {
  m_sampinfo = std::make_unique<SynthSampInfo>();
  sampinfo = m_sampinfo.get();
  return sampinfo;
}
