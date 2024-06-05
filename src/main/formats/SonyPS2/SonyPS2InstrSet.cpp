#include "SonyPS2InstrSet.h"
#include "PSXSPU.h"

using namespace std;

// ***************
// SonyPS2InstrSet
// ***************

SonyPS2InstrSet::SonyPS2InstrSet(RawFile *file, uint32_t offset)
    : VGMInstrSet(SonyPS2Format::name, file, offset, 0, "Sony PS2 InstrSet") {
  // disableAutoAddTracksAsRootChildren();
}

SonyPS2InstrSet::~SonyPS2InstrSet() {
}


bool SonyPS2InstrSet::GetHeaderInfo() {
  // VERSION CHUNK
  uint32_t curOffset = dwOffset;
  GetBytes(curOffset, 16, &versCk);
  VGMHeader *versCkHdr = addHeader(curOffset, versCk.chunkSize, "Version Chunk");
  versCkHdr->addSimpleChild(curOffset, 4, "Creator");
  versCkHdr->addSimpleChild(curOffset + 4, 4, "Type");
  versCkHdr->addSimpleChild(curOffset + 8, 4, "Chunk Size");
  versCkHdr->addSimpleChild(curOffset + 12, 2, "Reserved");
  versCkHdr->addSimpleChild(curOffset + 14, 1, "Major Version");
  versCkHdr->addSimpleChild(curOffset + 15, 1, "Minor Version");

  // HEADER CHUNK
  curOffset += versCk.chunkSize;
  GetBytes(curOffset, 64, &hdrCk);
  unLength = hdrCk.fileSize;

  VGMHeader *hdrCkHdr = addHeader(curOffset, hdrCk.chunkSize, "Header Chunk");
  hdrCkHdr->addSimpleChild(curOffset, 4, "Creator");
  hdrCkHdr->addSimpleChild(curOffset + 4, 4, "Type");
  hdrCkHdr->addSimpleChild(curOffset + 8, 4, "Chunk Size");
  hdrCkHdr->addSimpleChild(curOffset + 12, 4, "Entire Header Size");
  hdrCkHdr->addSimpleChild(curOffset + 16, 4, "Body Size");
  hdrCkHdr->addSimpleChild(curOffset + 20, 4, "Program Chunk Addr");
  hdrCkHdr->addSimpleChild(curOffset + 24, 4, "SampleSet Chunk Addr");
  hdrCkHdr->addSimpleChild(curOffset + 28, 4, "Sample Chunk Addr");
  hdrCkHdr->addSimpleChild(curOffset + 32, 4, "VAG Info Chunk Addr");
  //hdrCkHdr->addSimpleChild(curOffset+36, 4, "Sound Effect Timbre Chunk Addr");

  // PROGRAM CHUNK
  // this is handled in GetInstrPointers()

  // SAMPLESET CHUNK
  curOffset = dwOffset + hdrCk.samplesetChunkAddr;
  GetBytes(curOffset, 16, &sampSetCk);
  sampSetCk.sampleSetOffsetAddr = new uint32_t[sampSetCk.maxSampleSetNumber + 1];
  sampSetCk.sampleSetParam = new SampSetParam[sampSetCk.maxSampleSetNumber + 1];

  VGMHeader *sampSetCkHdr = addHeader(curOffset, sampSetCk.chunkSize, "SampleSet Chunk");
  sampSetCkHdr->addSimpleChild(curOffset, 4, "Creator");
  sampSetCkHdr->addSimpleChild(curOffset + 4, 4, "Type");
  sampSetCkHdr->addSimpleChild(curOffset + 8, 4, "Chunk Size");
  sampSetCkHdr->addSimpleChild(curOffset + 12, 4, "Max SampleSet Number");

  GetBytes(curOffset + 16, (sampSetCk.maxSampleSetNumber + 1) * sizeof(uint32_t), sampSetCk.sampleSetOffsetAddr);
  VGMHeader *sampSetParamOffsetHdr = sampSetCkHdr->addHeader(curOffset + 16,
                                                             (sampSetCk.maxSampleSetNumber + 1) * sizeof(uint32_t),
                                                             "SampleSet Param Offsets");
  VGMHeader *sampSetParamsHdr = sampSetCkHdr->addHeader(curOffset + 16 + (sampSetCk.maxSampleSetNumber + 1) * sizeof(uint32_t),
                              (sampSetCk.maxSampleSetNumber + 1) * sizeof(SampSetParam), "SampleSet Params");
  for (uint32_t i = 0; i <= sampSetCk.maxSampleSetNumber; i++) {
    sampSetParamOffsetHdr->addSimpleChild(curOffset + 16 + i * sizeof(uint32_t), 4, "Offset");
    if (sampSetCk.sampleSetOffsetAddr[i] == 0xFFFFFFFF)
      continue;
    GetBytes(curOffset + sampSetCk.sampleSetOffsetAddr[i], sizeof(uint8_t) * 4, sampSetCk.sampleSetParam + i);
    uint8_t nSamples = sampSetCk.sampleSetParam[i].nSample;
    sampSetCk.sampleSetParam[i].sampleIndex = new uint16_t[nSamples];
    GetBytes(curOffset + sampSetCk.sampleSetOffsetAddr[i] + sizeof(uint8_t) * 4, nSamples * sizeof(uint16_t),
             sampSetCk.sampleSetParam[i].sampleIndex);
    VGMHeader *sampSetParamHdr = sampSetParamsHdr->addHeader(curOffset + sampSetCk.sampleSetOffsetAddr[i],
                                                             sizeof(uint8_t) * 4 + nSamples * sizeof(uint16_t),
                                                             "SampleSet Param");
    sampSetParamHdr->addSimpleChild(curOffset + sampSetCk.sampleSetOffsetAddr[i], 1, "Vel Curve");
    sampSetParamHdr->addSimpleChild(curOffset + sampSetCk.sampleSetOffsetAddr[i] + 1, 1, "Vel Limit Low");
    sampSetParamHdr->addSimpleChild(curOffset + sampSetCk.sampleSetOffsetAddr[i] + 2, 1, "Vel Limit High");
    sampSetParamHdr->addSimpleChild(curOffset + sampSetCk.sampleSetOffsetAddr[i] + 3, 1, "Number of Samples");
    for (uint32_t j = 0; j < nSamples; j++)
      sampSetParamHdr->addSimpleChild(curOffset + sampSetCk.sampleSetOffsetAddr[i] + 4 + j * 2, 2, "Sample Index");
  }

  // SAMPLE CHUNK
  curOffset = dwOffset + hdrCk.sampleChunkAddr;
  GetBytes(curOffset, 16, &sampCk);
  sampCk.sampleOffsetAddr = new uint32_t[sampCk.maxSampleNumber + 1];
  sampCk.sampleParam = new SampleParam[sampCk.maxSampleNumber + 1];

  VGMHeader *sampCkHdr = addHeader(curOffset, sampCk.chunkSize, "Sample Chunk");
  sampCkHdr->addSimpleChild(curOffset, 4, "Creator");
  sampCkHdr->addSimpleChild(curOffset + 4, 4, "Type");
  sampCkHdr->addSimpleChild(curOffset + 8, 4, "Chunk Size");
  sampCkHdr->addSimpleChild(curOffset + 12, 4, "Max Sample Number");

  GetBytes(curOffset + 16, (sampCk.maxSampleNumber + 1) * sizeof(uint32_t), sampCk.sampleOffsetAddr);
  VGMHeader *sampleParamOffsetHdr = sampCkHdr->addHeader(curOffset + 16,
                                                         (sampCk.maxSampleNumber + 1) * sizeof(uint32_t),
                                                         "Sample Param Offsets");
  VGMHeader *sampleParamsHdr = sampCkHdr->addHeader(curOffset + 16 + (sampCk.maxSampleNumber + 1) * sizeof(uint32_t),
                                                    (sampCk.maxSampleNumber + 1) * sizeof(SampleParam),
                                                    "Sample Params");
  for (uint32_t i = 0; i <= sampCk.maxSampleNumber; i++) {
    sampleParamOffsetHdr->addSimpleChild(curOffset + 16 + i * sizeof(uint32_t), 4, "Offset");
    GetBytes(curOffset + sampCk.sampleOffsetAddr[i], sizeof(SampleParam), sampCk.sampleParam + i);
    VGMHeader *sampleParamHdr = sampleParamsHdr->addHeader(curOffset + sampCk.sampleOffsetAddr[i],
                                                           sizeof(SampleParam), "Sample Param");
    sampleParamHdr->addSimpleChild(curOffset + sampCk.sampleOffsetAddr[i], 2, "VAG Index");
    sampleParamHdr->addSimpleChild(curOffset + sampCk.sampleOffsetAddr[i] + 2, 1, "Vel Range Low");
    sampleParamHdr->addSimpleChild(curOffset + sampCk.sampleOffsetAddr[i] + 3, 1, "Vel Cross Fade");
    sampleParamHdr->addSimpleChild(curOffset + sampCk.sampleOffsetAddr[i] + 4, 1, "Vel Range High");
    sampleParamHdr->addSimpleChild(curOffset + sampCk.sampleOffsetAddr[i] + 5, 1, "Vel Follow Pitch");
    sampleParamHdr->addSimpleChild(curOffset + sampCk.sampleOffsetAddr[i] + 6, 1, "Vel Follow Pitch Center");
    sampleParamHdr->addSimpleChild(curOffset + sampCk.sampleOffsetAddr[i] + 7, 1, "Vel Follow Pitch Vel Curve");
    sampleParamHdr->addSimpleChild(curOffset + sampCk.sampleOffsetAddr[i] + 8, 1, "Vel Follow Amp");
    sampleParamHdr->addSimpleChild(curOffset + sampCk.sampleOffsetAddr[i] + 9, 1, "Vel Follow Amp Center");
    sampleParamHdr->addSimpleChild(curOffset + sampCk.sampleOffsetAddr[i] + 10, 1, "Vel Follow Amp Vel Curve");
    sampleParamHdr->addSimpleChild(curOffset + sampCk.sampleOffsetAddr[i] + 11, 1, "Sample Base Note");
    sampleParamHdr->addSimpleChild(curOffset + sampCk.sampleOffsetAddr[i] + 12, 1, "Sample Detune");
    sampleParamHdr->addSimpleChild(curOffset + sampCk.sampleOffsetAddr[i] + 13, 1, "Sample Panpot");
    sampleParamHdr->addSimpleChild(curOffset + sampCk.sampleOffsetAddr[i] + 14, 1, "Sample Group");
    sampleParamHdr->addSimpleChild(curOffset + sampCk.sampleOffsetAddr[i] + 15, 1, "Sample Priority");
    sampleParamHdr->addSimpleChild(curOffset + sampCk.sampleOffsetAddr[i] + 16, 1, "Sample Volume");
    sampleParamHdr->addSimpleChild(curOffset + sampCk.sampleOffsetAddr[i] + 17, 1, "Reserved");
    sampleParamHdr->addSimpleChild(curOffset + sampCk.sampleOffsetAddr[i] + 18, 2, "Sample ADSR1");
    sampleParamHdr->addSimpleChild(curOffset + sampCk.sampleOffsetAddr[i] + 20, 2, "Sample ADSR2");
    sampleParamHdr->addSimpleChild(curOffset + sampCk.sampleOffsetAddr[i] + 22, 1, "Key Follow Attack Rate");
    sampleParamHdr->addSimpleChild(curOffset + sampCk.sampleOffsetAddr[i] + 23, 1, "Key Follow Attack Rate Center");
    sampleParamHdr->addSimpleChild(curOffset + sampCk.sampleOffsetAddr[i] + 24, 1, "Key Follow Decay Rate");
    sampleParamHdr->addSimpleChild(curOffset + sampCk.sampleOffsetAddr[i] + 25, 1, "Key Follow Decay Rate Center");
    sampleParamHdr->addSimpleChild(curOffset + sampCk.sampleOffsetAddr[i] + 26, 1, "Key Follow Sustain Rate");
    sampleParamHdr->addSimpleChild(curOffset + sampCk.sampleOffsetAddr[i] + 27, 1, "Key Follow Sustain Rate Center");
    sampleParamHdr->addSimpleChild(curOffset + sampCk.sampleOffsetAddr[i] + 28, 1, "Key Follow Release Rate");
    sampleParamHdr->addSimpleChild(curOffset + sampCk.sampleOffsetAddr[i] + 29, 1, "Key Follow Release Rate Center");
    sampleParamHdr->addSimpleChild(curOffset + sampCk.sampleOffsetAddr[i] + 30, 1, "Key Follow Sustain Level");
    sampleParamHdr->addSimpleChild(curOffset + sampCk.sampleOffsetAddr[i] + 31, 1, "Key Follow Sustain Level Center");
    sampleParamHdr->addSimpleChild(curOffset + sampCk.sampleOffsetAddr[i] + 32, 2, "Sample Pitch LFO Delay");
    sampleParamHdr->addSimpleChild(curOffset + sampCk.sampleOffsetAddr[i] + 34, 2, "Sample Pitch LFO Fade");
    sampleParamHdr->addSimpleChild(curOffset + sampCk.sampleOffsetAddr[i] + 36, 2, "Sample Amp LFO Delay");
    sampleParamHdr->addSimpleChild(curOffset + sampCk.sampleOffsetAddr[i] + 38, 2, "Sample Amp LFO Fade");
    sampleParamHdr->addSimpleChild(curOffset + sampCk.sampleOffsetAddr[i] + 40, 1, "Sample LFO Attributes");
    sampleParamHdr->addSimpleChild(curOffset + sampCk.sampleOffsetAddr[i] + 41, 1, "Sample SPU Attributes");
  }

  // VAGInfo CHUNK
  curOffset = dwOffset + hdrCk.vagInfoChunkAddr;
  GetBytes(curOffset, 16, &vagInfoCk);
  vagInfoCk.vagInfoOffsetAddr = new uint32_t[vagInfoCk.maxVagInfoNumber + 1];
  vagInfoCk.vagInfoParam = new VAGInfoParam[vagInfoCk.maxVagInfoNumber + 1];

  VGMHeader *vagInfoCkHdr = addHeader(curOffset, vagInfoCk.chunkSize, "VAGInfo Chunk");
  vagInfoCkHdr->addSimpleChild(curOffset, 4, "Creator");
  vagInfoCkHdr->addSimpleChild(curOffset + 4, 4, "Type");
  vagInfoCkHdr->addSimpleChild(curOffset + 8, 4, "Chunk Size");
  vagInfoCkHdr->addSimpleChild(curOffset + 12, 4, "Max VAGInfo Number");

  GetBytes(curOffset + 16, (vagInfoCk.maxVagInfoNumber + 1) * sizeof(uint32_t), vagInfoCk.vagInfoOffsetAddr);
  VGMHeader *vagInfoParamOffsetHdr = vagInfoCkHdr->addHeader(curOffset + 16,
                                                             (vagInfoCk.maxVagInfoNumber + 1) * sizeof(uint32_t),
                                                             "VAGInfo Param Offsets");
  VGMHeader *vagInfoParamsHdr = vagInfoCkHdr->addHeader(curOffset + 16 + (vagInfoCk.maxVagInfoNumber + 1) * sizeof(uint32_t),
                                                  (vagInfoCk.maxVagInfoNumber + 1) * sizeof(VAGInfoParam),
                                                  "VAGInfo Params");
  for (uint32_t i = 0; i <= vagInfoCk.maxVagInfoNumber; i++) {
    vagInfoParamOffsetHdr->addSimpleChild(curOffset + 16 + i * sizeof(uint32_t), 4, "Offset");
    GetBytes(curOffset + vagInfoCk.vagInfoOffsetAddr[i], sizeof(VAGInfoParam), vagInfoCk.vagInfoParam + i);
    VGMHeader *vagInfoParamHdr = vagInfoParamsHdr->addHeader(curOffset + vagInfoCk.vagInfoOffsetAddr[i],
                                                             sizeof(VAGInfoParam), "VAGInfo Param");
    vagInfoParamHdr->addSimpleChild(curOffset + vagInfoCk.vagInfoOffsetAddr[i], 4, "VAG Offset Addr");
    vagInfoParamHdr->addSimpleChild(curOffset + vagInfoCk.vagInfoOffsetAddr[i] + 4, 2, "Sampling Rate");
    vagInfoParamHdr->addSimpleChild(curOffset + vagInfoCk.vagInfoOffsetAddr[i] + 6, 1, "Loop Flag");
    vagInfoParamHdr->addSimpleChild(curOffset + vagInfoCk.vagInfoOffsetAddr[i] + 7, 1, "Reserved");
  }
  return true;
}

bool SonyPS2InstrSet::GetInstrPointers() {
  uint32_t curOffset = dwOffset + hdrCk.programChunkAddr;
  //Now we're at the Program chunk, which starts with the sig "SCEIProg" (in 32bit little endian)
  //read in the first 4 values.  The programs will be read within GetInstrPointers()
  GetBytes(curOffset, 16, &progCk);

  progCk.programOffsetAddr = new uint32_t[progCk.maxProgramNumber + 1];
  progCk.progParamBlock = new SonyPS2Instr::ProgParam[progCk.maxProgramNumber + 1];

  VGMHeader *progCkHdr = addHeader(curOffset, progCk.chunkSize, "Program Chunk");
  progCkHdr->addSimpleChild(curOffset, 4, "Creator");
  progCkHdr->addSimpleChild(curOffset + 4, 4, "Type");
  progCkHdr->addSimpleChild(curOffset + 8, 4, "Chunk Size");
  progCkHdr->addSimpleChild(curOffset + 12, 4, "Max Program Number");

  GetBytes(curOffset + 16, (progCk.maxProgramNumber + 1) * sizeof(uint32_t), progCk.programOffsetAddr);
  VGMHeader *progParamOffsetHdr = progCkHdr->addHeader(curOffset + 16,
                                                       (progCk.maxProgramNumber + 1) * sizeof(uint32_t),
                                                       "Program Param Offsets");
  VGMHeader *progParamsHdr = progCkHdr->addHeader(curOffset + 16 + (progCk.maxProgramNumber + 1) * sizeof(uint32_t),
                                                  0/*(progCk.maxProgramNumber+1)*sizeof(SonyPS2Instr::ProgParam)*/,
                                                  "Program Params");

  for (uint32_t i = 0; i <= progCk.maxProgramNumber; i++) {
    progParamOffsetHdr->addSimpleChild(curOffset + 16 + i * sizeof(uint32_t), 4, "Offset");
    if (progCk.programOffsetAddr[i] == 0xFFFFFFFF)
      continue;
    GetBytes(curOffset + progCk.programOffsetAddr[i], sizeof(SonyPS2Instr::ProgParam), progCk.progParamBlock + i);
    //VGMHeader* progParamHdr = progParamsHdr->addHeader(curOffset+progCk.programOffsetAddr[i],
    //	sizeof(SonyPS2Instr::ProgParam), "Program Param");

    SonyPS2Instr *instr = new SonyPS2Instr(this, curOffset + progCk.programOffsetAddr[i],
                                           sizeof(SonyPS2Instr::ProgParam), i / 128, i % 128);
    aInstrs.push_back(instr);

    instr->addSimpleChild(curOffset + progCk.programOffsetAddr[i], 4, "SplitBlock Addr");
    instr->addSimpleChild(curOffset + progCk.programOffsetAddr[i] + 4, 1, "Number of SplitBlocks");
    instr->addSimpleChild(curOffset + progCk.programOffsetAddr[i] + 5, 1, "Size of SplitBlock");
    instr->addSimpleChild(curOffset + progCk.programOffsetAddr[i] + 6, 1, "Program Volume");
    instr->addSimpleChild(curOffset + progCk.programOffsetAddr[i] + 7, 1, "Program Panpot");
    instr->addSimpleChild(curOffset + progCk.programOffsetAddr[i] + 8, 1, "Program Transpose");
    instr->addSimpleChild(curOffset + progCk.programOffsetAddr[i] + 9, 1, "Program Detune");
    instr->addSimpleChild(curOffset + progCk.programOffsetAddr[i] + 10, 1, "Key Follow Pan");
    instr->addSimpleChild(curOffset + progCk.programOffsetAddr[i] + 11, 1, "Key Follow Pan Center");
    instr->addSimpleChild(curOffset + progCk.programOffsetAddr[i] + 12, 1, "Program Attributes");
    instr->addSimpleChild(curOffset + progCk.programOffsetAddr[i] + 13, 1, "Reserved");
    instr->addSimpleChild(curOffset + progCk.programOffsetAddr[i] + 14, 1, "Program Pitch LFO Waveform");
    instr->addSimpleChild(curOffset + progCk.programOffsetAddr[i] + 15, 1, "Program Amp LFO Waveform");
    instr->addSimpleChild(curOffset + progCk.programOffsetAddr[i] + 16, 1, "Program Pitch LFO Start Phase");
    instr->addSimpleChild(curOffset + progCk.programOffsetAddr[i] + 17, 1, "Program Amp LFO Start Phase");
    instr->addSimpleChild(curOffset + progCk.programOffsetAddr[i] + 18, 1, "Program Pitch LFO Start Phase Random");
    instr->addSimpleChild(curOffset + progCk.programOffsetAddr[i] + 19, 1, "Program Amp LFO Start Phase Random");
    instr->addSimpleChild(curOffset + progCk.programOffsetAddr[i] + 20, 2, "Program Pitch LFO Cycle Period (msec)");
    instr->addSimpleChild(curOffset + progCk.programOffsetAddr[i] + 22, 2, "Program Amp LFO Cycle Period (msec)");
    instr->addSimpleChild(curOffset + progCk.programOffsetAddr[i] + 24, 2, "Program Pitch LFO Depth (+)");
    instr->addSimpleChild(curOffset + progCk.programOffsetAddr[i] + 26, 2, "Program Pitch LFO Depth (-)");
    instr->addSimpleChild(curOffset + progCk.programOffsetAddr[i] + 28, 2, "MIDI Pitch Modulation Max Amplitude (+)");
    instr->addSimpleChild(curOffset + progCk.programOffsetAddr[i] + 30, 2, "MIDI Pitch Modulation Max Amplitude (-)");
    instr->addSimpleChild(curOffset + progCk.programOffsetAddr[i] + 32, 1, "Program Amp LFO Depth (+)");
    instr->addSimpleChild(curOffset + progCk.programOffsetAddr[i] + 33, 1, "Program Amp LFO Depth (-)");
    instr->addSimpleChild(curOffset + progCk.programOffsetAddr[i] + 34, 1, "MIDI Amp Modulation Max Amplitude (+)");
    instr->addSimpleChild(curOffset + progCk.programOffsetAddr[i] + 35, 1, "MIDI Amp Modulation Max Amplitude (-)");

    assert(progCk.progParamBlock[i].sizeSplitBlock == 20);    //make sure the size of a split block is indeed 20
    uint8_t nSplits = progCk.progParamBlock[i].nSplit;
    instr->unLength += nSplits * sizeof(SonyPS2Instr::SplitBlock);
    uint32_t absSplitBlocksAddr = curOffset + progCk.programOffsetAddr[i] + progCk.progParamBlock[i].splitBlockAddr;
    instr->splitBlocks = new SonyPS2Instr::SplitBlock[nSplits];
    GetBytes(absSplitBlocksAddr, nSplits * sizeof(SonyPS2Instr::SplitBlock), instr->splitBlocks);
    VGMHeader *splitBlocksHdr = instr->addHeader(absSplitBlocksAddr,
                                                 nSplits * sizeof(SonyPS2Instr::SplitBlock), "Split Blocks");
    for (uint8_t j = 0; j < nSplits; j++) {
      uint32_t splitOff = absSplitBlocksAddr + j * sizeof(SonyPS2Instr::SplitBlock);
      VGMHeader *splitBlockHdr = splitBlocksHdr->addHeader(splitOff,
                                                           sizeof(SonyPS2Instr::SplitBlock), "Split Block");
      splitBlockHdr->addSimpleChild(splitOff, 2, "Sample Set Index");
      splitBlockHdr->addSimpleChild(splitOff + 2, 1, "Split Range Low");
      splitBlockHdr->addSimpleChild(splitOff + 3, 1, "Split Cross Fade");
      splitBlockHdr->addSimpleChild(splitOff + 4, 1, "Split Range High");
      splitBlockHdr->addSimpleChild(splitOff + 5, 1, "Split Number");
      splitBlockHdr->addSimpleChild(splitOff + 6, 2, "Split Bend Range Low");
      splitBlockHdr->addSimpleChild(splitOff + 8, 2, "Split Bend Range High");
      splitBlockHdr->addSimpleChild(splitOff + 10, 1, "Key Follow Pitch");
      splitBlockHdr->addSimpleChild(splitOff + 11, 1, "Key Follow Pitch Center");
      splitBlockHdr->addSimpleChild(splitOff + 12, 1, "Key Follow Amp");
      splitBlockHdr->addSimpleChild(splitOff + 13, 1, "Key Follow Amp Center");
      splitBlockHdr->addSimpleChild(splitOff + 14, 1, "Key Follow Pan");
      splitBlockHdr->addSimpleChild(splitOff + 15, 1, "Key Follow Pan Center");
      splitBlockHdr->addSimpleChild(splitOff + 16, 1, "Split Volume");
      splitBlockHdr->addSimpleChild(splitOff + 17, 1, "Split Panpot");
      splitBlockHdr->addSimpleChild(splitOff + 18, 1, "Split Transpose");
      splitBlockHdr->addSimpleChild(splitOff + 19, 1, "Split Detune");
    }
  }

  progParamsHdr->addChildren(aInstrs);

  uint32_t maxProgNum = progCk.maxProgramNumber;
  progParamsHdr->unLength = (curOffset + progCk.programOffsetAddr[maxProgNum]) + sizeof(SonyPS2Instr::ProgParam) +
      progCk.progParamBlock[maxProgNum].nSplit * sizeof(SonyPS2Instr::SplitBlock) - progParamsHdr->dwOffset;

  return true;
}


// ************
// SonyPS2Instr
// ************


SonyPS2Instr::SonyPS2Instr(VGMInstrSet *instrSet,
                           uint32_t offset,
                           uint32_t length,
                           uint32_t theBank,
                           uint32_t theInstrNum)
    : VGMInstr(instrSet, offset, length, theBank, theInstrNum, "Program Param"),
      splitBlocks(0) {
}

SonyPS2Instr::~SonyPS2Instr() {
  if (splitBlocks)
    delete[] splitBlocks;
}


bool SonyPS2Instr::LoadInstr() {
  SonyPS2InstrSet *instrset = (SonyPS2InstrSet *) parInstrSet;
  SonyPS2InstrSet::ProgCk &progCk = instrset->progCk;
  SonyPS2InstrSet::SampSetCk &sampSetCk = instrset->sampSetCk;
  SonyPS2InstrSet::SampCk &sampCk = instrset->sampCk;
  SonyPS2InstrSet::VAGInfoCk &vagInfoCk = instrset->vagInfoCk;
  ProgParam &progParam = progCk.progParamBlock[this->instrNum];
  uint8_t nSplits = progParam.nSplit;
  for (uint8_t i = 0; i < nSplits; i++) {
    SplitBlock &splitblock = this->splitBlocks[i];
    SonyPS2InstrSet::SampSetParam &sampSetParam = sampSetCk.sampleSetParam[splitblock.sampleSetIndex];

    for (uint8_t j = 0; j < sampSetParam.nSample; j++) {
      SonyPS2InstrSet::SampleParam &sampParam = sampCk.sampleParam[sampSetParam.sampleIndex[j]];
      SonyPS2InstrSet::VAGInfoParam &vagInfoParam = vagInfoCk.vagInfoParam[sampParam.VagIndex];
      // WE ARE ASSUMING THE VAG SAMPLES ARE STORED CONSECUTIVELY
      int sampNum = sampParam.VagIndex;
      uint8_t noteLow = splitblock.splitRangeLow;
      uint8_t noteHigh = splitblock.splitRangeHigh;
      if (noteHigh < noteLow)
        noteHigh = 0x7F;
      uint8_t sampSetVelLow = Convert7bitPercentVolValToStdMidiVal(sampSetParam.velLimitLow);
      uint8_t sampSetVelHigh = Convert7bitPercentVolValToStdMidiVal(sampSetParam.velLimitHigh);
      uint8_t velLow = Convert7bitPercentVolValToStdMidiVal(sampParam.velRangeLow);//sampSetParam.velLimitLow;
      uint8_t velHigh = Convert7bitPercentVolValToStdMidiVal(sampParam.velRangeHigh);//sampSetParam.velLimitHigh;
      if (velLow < sampSetVelLow)
        velLow = sampSetVelLow;
      if (velHigh > sampSetVelHigh)
        velHigh = sampSetVelHigh;

      uint8_t unityNote = sampParam.sampleBaseNote;
      VGMRgn *rgn = this->AddRgn(0, 0, sampNum, noteLow, noteHigh, velLow, velHigh);
      rgn->unityKey = unityNote;
      //rgn->SetPan(sampParam.samplePanpot);
      int16_t pan = 0x40 + ConvertPanVal(sampParam.samplePanpot) + ConvertPanVal(progParam.progPanpot) +
          ConvertPanVal(splitblock.splitPanpot);
      if (pan > 0x7F) pan = 0x7F;
      if (pan < 0) pan = 0;
      //double realPan = (pan-0x40)* (1.0/(double)0x40);
      rgn->SetPan((uint8_t) pan);
      rgn->SetFineTune(splitblock.splitTranspose * 100 + splitblock.splitDetune);

      long vol = progParam.progVolume * splitblock.splitVolume * sampParam.sampleVolume;
      //we divide the above value by 127^3 to get the percent vol it represents.  Then we convert it to DB units.
      //0xA0000 = 1db in the DLS lScale val for atten (dls1 specs p30)
      double percentvol = vol / (double) (127 * 127 * 127);
      rgn->SetVolume(percentvol);
      PSXConvADSR(rgn, sampParam.sampleAdsr1, sampParam.sampleAdsr2, true);
      //splitblock->splitRangeLow
    }
  }
  return true;
}

int8_t SonyPS2Instr::ConvertPanVal(uint8_t panVal) {
  // Actually, it may be C1 is center, but i don't care to fix that right now since
  // I have yet to see an occurence of >0x7F pan
  if (panVal > 0x7F)        //if it's > 0x7F, 0x80 == right, 0xBF center 0xFF left
    return (int8_t) 0x40 - (int8_t) (panVal - 0x7F);
  else
    return (int8_t) panVal - (int8_t) 0x40;
}


// ***************
// SonyPS2SampColl
// ***************

SonyPS2SampColl::SonyPS2SampColl(RawFile *rawfile, uint32_t offset, uint32_t length)
    : VGMSampColl(SonyPS2Format::name, rawfile, offset, length) {
  this->LoadOnInstrMatch();
  pRoot->AddVGMFile(this);
}

bool SonyPS2SampColl::GetSampleInfo() {
  SonyPS2InstrSet *instrset = (SonyPS2InstrSet *) parInstrSet;
  if (!instrset)
    return false;
  SonyPS2InstrSet::VAGInfoCk &vagInfoCk = instrset->vagInfoCk;
  uint32_t numVagInfos = vagInfoCk.maxVagInfoNumber + 1;
  for (uint32_t i = 0; i < numVagInfos; i++) {
    // Get offset, length, and samplerate from VAGInfo Param
    SonyPS2InstrSet::VAGInfoParam &vagInfoParam = vagInfoCk.vagInfoParam[i];
    uint32_t offset = vagInfoParam.vagOffsetAddr;
    uint32_t length = (i == numVagInfos - 1) ? this->rawFile()->size() - offset :
                      vagInfoCk.vagInfoParam[i + 1].vagOffsetAddr - offset;

    // We need to perform a hackish check to make sure the last ADPCM block was not
    // 0x00 07 77 77 77 etc.  If this is the case, we should ignore that block and treat the
    // previous block as the last.  It is just a strange quirk found among PS1/PS2 games
    // It should be safe to just check test the first 4 bytes as these values would be inconceivable
    // in any other case  (especially as bit 4 of second byte signifies the loop start point).
    uint32_t testWord = this->GetWordBE(offset + length - 16);
    if (testWord == 0x00077777)
      length -= 16;

    uint16_t sampleRate = vagInfoParam.vagSampleRate;

    auto name = fmt::format("Sample {}", samples.size());
    PSXSamp *samp = new PSXSamp(this, offset, length, offset, length, 1,
      16, sampleRate, name, true);
    samples.push_back(samp);

    // Determine loop information from VAGInfo Param
    if (vagInfoParam.vagAttribute == SCEHD_VAG_1SHOT) {
      samp->SetLoopOnConversion(false);
      samp->SetLoopStatus(0);
    }
    else if (vagInfoParam.vagAttribute == SCEHD_VAG_LOOP)
      samp->SetLoopOnConversion(true);
  }

  return true;
}