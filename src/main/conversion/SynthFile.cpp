/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "SynthFile.h"

#include <fmt/format.h>
#include "VGMSamp.h"

//  **********************************************************************************
//  SynthFile - An intermediate class to lay out all of the the data necessary for Coll conversion
//				to DLS or SF2 formats.  Currently, the structure is identical to
//DLS.
//  **********************************************************************************

SynthFile::SynthFile(std::string synth_name) : name(synth_name) {}

SynthFile::~SynthFile() {
    DeleteVect(vInstrs);
    DeleteVect(vWaves);
}

SynthInstr *SynthFile::AddInstr(uint32_t bank, uint32_t instrNum) {
    auto str = fmt::format("Instr bnk {} num {}", bank, instrNum);
    vInstrs.insert(vInstrs.end(), new SynthInstr(bank, instrNum, str));
    return vInstrs.back();
}

SynthInstr *SynthFile::AddInstr(uint32_t bank, uint32_t instrNum, std::string name) {
    vInstrs.insert(vInstrs.end(), new SynthInstr(bank, instrNum, name));
    return vInstrs.back();
}

void SynthFile::DeleteInstr(uint32_t bank, uint32_t instrNum) {}

SynthWave *SynthFile::AddWave(uint16_t formatTag, uint16_t channels, int samplesPerSec,
                              int aveBytesPerSec, uint16_t blockAlign, uint16_t bitsPerSample,
                              uint32_t waveDataSize, unsigned char *waveData, std::string name) {
    vWaves.insert(vWaves.end(),
                  new SynthWave(formatTag, channels, samplesPerSec, aveBytesPerSec, blockAlign,
                                bitsPerSample, waveDataSize, waveData, name));
    return vWaves.back();
}

//  **********
//  SynthInstr
//  **********

SynthInstr::SynthInstr(uint32_t bank, uint32_t instrument)
    : ulBank(bank), ulInstrument(instrument) {
    name = fmt::format("Instr bnk {} num {}", bank, instrument);
    // RiffFile::AlignName(name);
}

SynthInstr::SynthInstr(uint32_t bank, uint32_t instrument, std::string instrName)
    : ulBank(bank), ulInstrument(instrument), name(instrName) {
    // RiffFile::AlignName(name);
}

SynthInstr::SynthInstr(uint32_t bank, uint32_t instrument, std::string instrName,
                       std::vector<SynthRgn *> listRgns)
    : ulBank(bank), ulInstrument(instrument), name(instrName) {
    // RiffFile::AlignName(name);
    vRgns = listRgns;
}

SynthInstr::~SynthInstr() {
    DeleteVect(vRgns);
}

SynthRgn *SynthInstr::AddRgn(void) {
    vRgns.insert(vRgns.end(), new SynthRgn());
    return vRgns.back();
}

SynthRgn *SynthInstr::AddRgn(SynthRgn rgn) {
    SynthRgn *newRgn = new SynthRgn();
    *newRgn = rgn;
    vRgns.insert(vRgns.end(), newRgn);
    return vRgns.back();
}

//  ********
//  SynthRgn
//  ********

SynthRgn::~SynthRgn(void) {
    if (sampinfo)
        delete sampinfo;
    if (art)
        delete art;
}

SynthArt *SynthRgn::AddArt(void) {
    art = new SynthArt();
    return art;
}

SynthSampInfo *SynthRgn::AddSampInfo(void) {
    sampinfo = new SynthSampInfo();
    return sampinfo;
}

void SynthRgn::SetRanges(uint16_t keyLow, uint16_t keyHigh, uint16_t velLow, uint16_t velHigh) {
    usKeyLow = keyLow;
    usKeyHigh = keyHigh;
    usVelLow = velLow;
    usVelHigh = velHigh;
}

void SynthRgn::SetWaveLinkInfo(uint16_t options, uint16_t phaseGroup, uint32_t theChannel,
                               uint32_t theTableIndex) {
    fusOptions = options;
    usPhaseGroup = phaseGroup;
    channel = theChannel;
    tableIndex = theTableIndex;
}

//  ********
//  SynthArt
//  ********

SynthArt::~SynthArt() {
    // DeleteVect(vConnBlocks);
}

void SynthArt::AddADSR(double attack, Transform atk_transform, double decay, double sustain_level,
                       double sustain, double release, Transform rls_transform) {
    this->attack_time = attack;
    this->attack_transform = atk_transform;
    this->decay_time = decay;
    this->sustain_lev = sustain_level;
    this->sustain_time = sustain;
    this->release_time = release;
    this->release_transform = rls_transform;
}

void SynthArt::AddPan(double thePan) {
    this->pan = thePan;
}

//  *************
//  SynthSampInfo
//  *************

void SynthSampInfo::SetLoopInfo(Loop &loop, VGMSamp *samp) {
    const int origFormatBytesPerSamp = samp->bps / 8;
    double compressionRatio = samp->GetCompressionRatio();

    // If the sample loops, but the loop length is 0, then assume the length should
    // extend to the end of the sample.
    if (loop.loopStatus && loop.loopLength == 0)
        loop.loopLength = samp->dataLength - loop.loopStart;

    cSampleLoops = loop.loopStatus;
    ulLoopType = loop.loopType;
    ulLoopStart = (loop.loopStartMeasure == LM_BYTES)
                      ? (uint32_t)((loop.loopStart * compressionRatio) / origFormatBytesPerSamp)
                      : loop.loopStart;
    ulLoopLength = (loop.loopLengthMeasure == LM_BYTES)
                       ? (uint32_t)((loop.loopLength * compressionRatio) / origFormatBytesPerSamp)
                       : loop.loopLength;
}

void SynthSampInfo::SetPitchInfo(uint16_t unityNote, short fineTune, double atten) {
    usUnityNote = unityNote;
    sFineTune = fineTune;
    attenuation = atten;
}

//  *********
//  SynthWave
//  *********

void SynthWave::ConvertTo16bitSigned() {
    if (wBitsPerSample == 8) {
        this->wBitsPerSample = 16;
        this->wBlockAlign = 16 / 8 * this->wChannels;
        this->dwAveBytesPerSec *= 2;

        int16_t *newData = new int16_t[this->dataSize];
        for (unsigned int i = 0; i < this->dataSize; i++)
            newData[i] = ((int16_t)this->data[i] - 128) << 8;
        delete[] this->data;
        this->data = (uint8_t *)newData;
        this->dataSize *= 2;
    }
}

SynthWave::~SynthWave() {
    delete sampinfo;
    delete[] data;
}

SynthSampInfo *SynthWave::AddSampInfo(void) {
    sampinfo = new SynthSampInfo();
    return sampinfo;
}
