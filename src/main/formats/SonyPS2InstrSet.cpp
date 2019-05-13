/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
 #include "pch.h"
#include "SonyPS2InstrSet.h"
#include "PSXSPU.h"

using namespace std;

// ***************
// SonyPS2InstrSet
// ***************

SonyPS2InstrSet::SonyPS2InstrSet(RawFile *file, uint32_t offset)
    : VGMInstrSet(SonyPS2Format::name, file, offset) {
}

SonyPS2InstrSet::~SonyPS2InstrSet(void) {
}


bool SonyPS2InstrSet::GetHeaderInfo() {
  name = L"Sony PS2 InstrSet";

  // VERSION CHUNK
  uint32_t curOffset = dwOffset;
  GetBytes(curOffset, 16, &versCk);
  VGMHeader *versCkHdr = AddHeader(curOffset, versCk.chunkSize, L"Version Chunk");
  versCkHdr->AddSimpleItem(curOffset, 4, L"Creator");
  versCkHdr->AddSimpleItem(curOffset + 4, 4, L"Type");
  versCkHdr->AddSimpleItem(curOffset + 8, 4, L"Chunk Size");
  versCkHdr->AddSimpleItem(curOffset + 12, 2, L"Reserved");
  versCkHdr->AddSimpleItem(curOffset + 14, 1, L"Major Version");
  versCkHdr->AddSimpleItem(curOffset + 15, 1, L"Minor Version");

  // HEADER CHUNK
  curOffset += versCk.chunkSize;
  GetBytes(curOffset, 64, &hdrCk);
  unLength = hdrCk.fileSize;

  VGMHeader *hdrCkHdr = AddHeader(curOffset, hdrCk.chunkSize, L"Header Chunk");
  hdrCkHdr->AddSimpleItem(curOffset, 4, L"Creator");
  hdrCkHdr->AddSimpleItem(curOffset + 4, 4, L"Type");
  hdrCkHdr->AddSimpleItem(curOffset + 8, 4, L"Chunk Size");
  hdrCkHdr->AddSimpleItem(curOffset + 12, 4, L"Entire Header Size");
  hdrCkHdr->AddSimpleItem(curOffset + 16, 4, L"Body Size");
  hdrCkHdr->AddSimpleItem(curOffset + 20, 4, L"Program Chunk Addr");
  hdrCkHdr->AddSimpleItem(curOffset + 24, 4, L"SampleSet Chunk Addr");
  hdrCkHdr->AddSimpleItem(curOffset + 28, 4, L"Sample Chunk Addr");
  hdrCkHdr->AddSimpleItem(curOffset + 32, 4, L"VAG Info Chunk Addr");
  //hdrCkHdr->AddSimpleItem(curOffset+36, 4, L"Sound Effect Timbre Chunk Addr");

  // PROGRAM CHUNK
  // this is handled in GetInstrPointers()

  // SAMPLESET CHUNK
  curOffset = dwOffset + hdrCk.samplesetChunkAddr;
  GetBytes(curOffset, 16, &sampSetCk);
  sampSetCk.sampleSetOffsetAddr = new uint32_t[sampSetCk.maxSampleSetNumber + 1];
  sampSetCk.sampleSetParam = new SampSetParam[sampSetCk.maxSampleSetNumber + 1];

  VGMHeader *sampSetCkHdr = AddHeader(curOffset, sampSetCk.chunkSize, L"SampleSet Chunk");
  sampSetCkHdr->AddSimpleItem(curOffset, 4, L"Creator");
  sampSetCkHdr->AddSimpleItem(curOffset + 4, 4, L"Type");
  sampSetCkHdr->AddSimpleItem(curOffset + 8, 4, L"Chunk Size");
  sampSetCkHdr->AddSimpleItem(curOffset + 12, 4, L"Max SampleSet Number");

  GetBytes(curOffset + 16, (sampSetCk.maxSampleSetNumber + 1) * sizeof(uint32_t), sampSetCk.sampleSetOffsetAddr);
  VGMHeader *sampSetParamOffsetHdr = sampSetCkHdr->AddHeader(curOffset + 16,
                                                             (sampSetCk.maxSampleSetNumber + 1) * sizeof(uint32_t),
                                                             L"SampleSet Param Offsets");
  VGMHeader *sampSetParamsHdr = sampSetCkHdr->AddHeader(curOffset + 16 + (sampSetCk.maxSampleSetNumber + 1) * sizeof(uint32_t),
                              (sampSetCk.maxSampleSetNumber + 1) * sizeof(SampSetParam), L"SampleSet Params");
  for (uint32_t i = 0; i <= sampSetCk.maxSampleSetNumber; i++) {
    sampSetParamOffsetHdr->AddSimpleItem(curOffset + 16 + i * sizeof(uint32_t), 4, L"Offset");
    if (sampSetCk.sampleSetOffsetAddr[i] == 0xFFFFFFFF)
      continue;
    GetBytes(curOffset + sampSetCk.sampleSetOffsetAddr[i], sizeof(uint8_t) * 4, sampSetCk.sampleSetParam + i);
    uint8_t nSamples = sampSetCk.sampleSetParam[i].nSample;
    sampSetCk.sampleSetParam[i].sampleIndex = new uint16_t[nSamples];
    GetBytes(curOffset + sampSetCk.sampleSetOffsetAddr[i] + sizeof(uint8_t) * 4, nSamples * sizeof(uint16_t),
             sampSetCk.sampleSetParam[i].sampleIndex);
    VGMHeader *sampSetParamHdr = sampSetParamsHdr->AddHeader(curOffset + sampSetCk.sampleSetOffsetAddr[i],
                                                             sizeof(uint8_t) * 4 + nSamples * sizeof(uint16_t),
                                                             L"SampleSet Param");
    sampSetParamHdr->AddSimpleItem(curOffset + sampSetCk.sampleSetOffsetAddr[i], 1, L"Vel Curve");
    sampSetParamHdr->AddSimpleItem(curOffset + sampSetCk.sampleSetOffsetAddr[i] + 1, 1, L"Vel Limit Low");
    sampSetParamHdr->AddSimpleItem(curOffset + sampSetCk.sampleSetOffsetAddr[i] + 2, 1, L"Vel Limit High");
    sampSetParamHdr->AddSimpleItem(curOffset + sampSetCk.sampleSetOffsetAddr[i] + 3, 1, L"Number of Samples");
    for (uint32_t j = 0; j < nSamples; j++)
      sampSetParamHdr->AddSimpleItem(curOffset + sampSetCk.sampleSetOffsetAddr[i] + 4 + j * 2, 2, L"Sample Index");
  }

  // SAMPLE CHUNK
  curOffset = dwOffset + hdrCk.sampleChunkAddr;
  GetBytes(curOffset, 16, &sampCk);
  sampCk.sampleOffsetAddr = new uint32_t[sampCk.maxSampleNumber + 1];
  sampCk.sampleParam = new SampleParam[sampCk.maxSampleNumber + 1];

  VGMHeader *sampCkHdr = AddHeader(curOffset, sampCk.chunkSize, L"Sample Chunk");
  sampCkHdr->AddSimpleItem(curOffset, 4, L"Creator");
  sampCkHdr->AddSimpleItem(curOffset + 4, 4, L"Type");
  sampCkHdr->AddSimpleItem(curOffset + 8, 4, L"Chunk Size");
  sampCkHdr->AddSimpleItem(curOffset + 12, 4, L"Max Sample Number");

  GetBytes(curOffset + 16, (sampCk.maxSampleNumber + 1) * sizeof(uint32_t), sampCk.sampleOffsetAddr);
  VGMHeader *sampleParamOffsetHdr = sampCkHdr->AddHeader(curOffset + 16,
                                                         (sampCk.maxSampleNumber + 1) * sizeof(uint32_t),
                                                         L"Sample Param Offsets");
  VGMHeader *sampleParamsHdr = sampCkHdr->AddHeader(curOffset + 16 + (sampCk.maxSampleNumber + 1) * sizeof(uint32_t),
                                                    (sampCk.maxSampleNumber + 1) * sizeof(SampleParam),
                                                    L"Sample Params");
  for (uint32_t i = 0; i <= sampCk.maxSampleNumber; i++) {
    sampleParamOffsetHdr->AddSimpleItem(curOffset + 16 + i * sizeof(uint32_t), 4, L"Offset");
    GetBytes(curOffset + sampCk.sampleOffsetAddr[i], sizeof(SampleParam), sampCk.sampleParam + i);
    VGMHeader *sampleParamHdr = sampleParamsHdr->AddHeader(curOffset + sampCk.sampleOffsetAddr[i],
                                                           sizeof(SampleParam), L"Sample Param");
    sampleParamHdr->AddSimpleItem(curOffset + sampCk.sampleOffsetAddr[i], 2, L"VAG Index");
    sampleParamHdr->AddSimpleItem(curOffset + sampCk.sampleOffsetAddr[i] + 2, 1, L"Vel Range Low");
    sampleParamHdr->AddSimpleItem(curOffset + sampCk.sampleOffsetAddr[i] + 3, 1, L"Vel Cross Fade");
    sampleParamHdr->AddSimpleItem(curOffset + sampCk.sampleOffsetAddr[i] + 4, 1, L"Vel Range High");
    sampleParamHdr->AddSimpleItem(curOffset + sampCk.sampleOffsetAddr[i] + 5, 1, L"Vel Follow Pitch");
    sampleParamHdr->AddSimpleItem(curOffset + sampCk.sampleOffsetAddr[i] + 6, 1, L"Vel Follow Pitch Center");
    sampleParamHdr->AddSimpleItem(curOffset + sampCk.sampleOffsetAddr[i] + 7, 1, L"Vel Follow Pitch Vel Curve");
    sampleParamHdr->AddSimpleItem(curOffset + sampCk.sampleOffsetAddr[i] + 8, 1, L"Vel Follow Amp");
    sampleParamHdr->AddSimpleItem(curOffset + sampCk.sampleOffsetAddr[i] + 9, 1, L"Vel Follow Amp Center");
    sampleParamHdr->AddSimpleItem(curOffset + sampCk.sampleOffsetAddr[i] + 10, 1, L"Vel Follow Amp Vel Curve");
    sampleParamHdr->AddSimpleItem(curOffset + sampCk.sampleOffsetAddr[i] + 11, 1, L"Sample Base Note");
    sampleParamHdr->AddSimpleItem(curOffset + sampCk.sampleOffsetAddr[i] + 12, 1, L"Sample Detune");
    sampleParamHdr->AddSimpleItem(curOffset + sampCk.sampleOffsetAddr[i] + 13, 1, L"Sample Panpot");
    sampleParamHdr->AddSimpleItem(curOffset + sampCk.sampleOffsetAddr[i] + 14, 1, L"Sample Group");
    sampleParamHdr->AddSimpleItem(curOffset + sampCk.sampleOffsetAddr[i] + 15, 1, L"Sample Priority");
    sampleParamHdr->AddSimpleItem(curOffset + sampCk.sampleOffsetAddr[i] + 16, 1, L"Sample Volume");
    sampleParamHdr->AddSimpleItem(curOffset + sampCk.sampleOffsetAddr[i] + 17, 1, L"Reserved");
    sampleParamHdr->AddSimpleItem(curOffset + sampCk.sampleOffsetAddr[i] + 18, 2, L"Sample ADSR1");
    sampleParamHdr->AddSimpleItem(curOffset + sampCk.sampleOffsetAddr[i] + 20, 2, L"Sample ADSR2");
    sampleParamHdr->AddSimpleItem(curOffset + sampCk.sampleOffsetAddr[i] + 22, 1, L"Key Follow Attack Rate");
    sampleParamHdr->AddSimpleItem(curOffset + sampCk.sampleOffsetAddr[i] + 23, 1, L"Key Follow Attack Rate Center");
    sampleParamHdr->AddSimpleItem(curOffset + sampCk.sampleOffsetAddr[i] + 24, 1, L"Key Follow Decay Rate");
    sampleParamHdr->AddSimpleItem(curOffset + sampCk.sampleOffsetAddr[i] + 25, 1, L"Key Follow Decay Rate Center");
    sampleParamHdr->AddSimpleItem(curOffset + sampCk.sampleOffsetAddr[i] + 26, 1, L"Key Follow Sustain Rate");
    sampleParamHdr->AddSimpleItem(curOffset + sampCk.sampleOffsetAddr[i] + 27, 1, L"Key Follow Sustain Rate Center");
    sampleParamHdr->AddSimpleItem(curOffset + sampCk.sampleOffsetAddr[i] + 28, 1, L"Key Follow Release Rate");
    sampleParamHdr->AddSimpleItem(curOffset + sampCk.sampleOffsetAddr[i] + 29, 1, L"Key Follow Release Rate Center");
    sampleParamHdr->AddSimpleItem(curOffset + sampCk.sampleOffsetAddr[i] + 30, 1, L"Key Follow Sustain Level");
    sampleParamHdr->AddSimpleItem(curOffset + sampCk.sampleOffsetAddr[i] + 31, 1, L"Key Follow Sustain Level Center");
    sampleParamHdr->AddSimpleItem(curOffset + sampCk.sampleOffsetAddr[i] + 32, 2, L"Sample Pitch LFO Delay");
    sampleParamHdr->AddSimpleItem(curOffset + sampCk.sampleOffsetAddr[i] + 34, 2, L"Sample Pitch LFO Fade");
    sampleParamHdr->AddSimpleItem(curOffset + sampCk.sampleOffsetAddr[i] + 36, 2, L"Sample Amp LFO Delay");
    sampleParamHdr->AddSimpleItem(curOffset + sampCk.sampleOffsetAddr[i] + 38, 2, L"Sample Amp LFO Fade");
    sampleParamHdr->AddSimpleItem(curOffset + sampCk.sampleOffsetAddr[i] + 40, 1, L"Sample LFO Attributes");
    sampleParamHdr->AddSimpleItem(curOffset + sampCk.sampleOffsetAddr[i] + 41, 1, L"Sample SPU Attributes");
  }

  // VAGInfo CHUNK
  curOffset = dwOffset + hdrCk.vagInfoChunkAddr;
  GetBytes(curOffset, 16, &vagInfoCk);
  vagInfoCk.vagInfoOffsetAddr = new uint32_t[vagInfoCk.maxVagInfoNumber + 1];
  vagInfoCk.vagInfoParam = new VAGInfoParam[vagInfoCk.maxVagInfoNumber + 1];

  VGMHeader *vagInfoCkHdr = AddHeader(curOffset, vagInfoCk.chunkSize, L"VAGInfo Chunk");
  vagInfoCkHdr->AddSimpleItem(curOffset, 4, L"Creator");
  vagInfoCkHdr->AddSimpleItem(curOffset + 4, 4, L"Type");
  vagInfoCkHdr->AddSimpleItem(curOffset + 8, 4, L"Chunk Size");
  vagInfoCkHdr->AddSimpleItem(curOffset + 12, 4, L"Max VAGInfo Number");

  GetBytes(curOffset + 16, (vagInfoCk.maxVagInfoNumber + 1) * sizeof(uint32_t), vagInfoCk.vagInfoOffsetAddr);
  VGMHeader *vagInfoParamOffsetHdr = vagInfoCkHdr->AddHeader(curOffset + 16,
                                                             (vagInfoCk.maxVagInfoNumber + 1) * sizeof(uint32_t),
                                                             L"VAGInfo Param Offsets");
  VGMHeader *vagInfoParamsHdr = vagInfoCkHdr->AddHeader(curOffset + 16 + (vagInfoCk.maxVagInfoNumber + 1) * sizeof(uint32_t),
                                                  (vagInfoCk.maxVagInfoNumber + 1) * sizeof(VAGInfoParam),
                                                  L"VAGInfo Params");
  for (uint32_t i = 0; i <= vagInfoCk.maxVagInfoNumber; i++) {
    vagInfoParamOffsetHdr->AddSimpleItem(curOffset + 16 + i * sizeof(uint32_t), 4, L"Offset");
    GetBytes(curOffset + vagInfoCk.vagInfoOffsetAddr[i], sizeof(VAGInfoParam), vagInfoCk.vagInfoParam + i);
    VGMHeader *vagInfoParamHdr = vagInfoParamsHdr->AddHeader(curOffset + vagInfoCk.vagInfoOffsetAddr[i],
                                                             sizeof(VAGInfoParam), L"VAGInfo Param");
    vagInfoParamHdr->AddSimpleItem(curOffset + vagInfoCk.vagInfoOffsetAddr[i], 4, L"VAG Offset Addr");
    vagInfoParamHdr->AddSimpleItem(curOffset + vagInfoCk.vagInfoOffsetAddr[i] + 4, 2, L"Sampling Rate");
    vagInfoParamHdr->AddSimpleItem(curOffset + vagInfoCk.vagInfoOffsetAddr[i] + 6, 1, L"Loop Flag");
    vagInfoParamHdr->AddSimpleItem(curOffset + vagInfoCk.vagInfoOffsetAddr[i] + 7, 1, L"Reserved");
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

  VGMHeader *progCkHdr = AddHeader(curOffset, progCk.chunkSize, L"Program Chunk");
  progCkHdr->AddSimpleItem(curOffset, 4, L"Creator");
  progCkHdr->AddSimpleItem(curOffset + 4, 4, L"Type");
  progCkHdr->AddSimpleItem(curOffset + 8, 4, L"Chunk Size");
  progCkHdr->AddSimpleItem(curOffset + 12, 4, L"Max Program Number");

  GetBytes(curOffset + 16, (progCk.maxProgramNumber + 1) * sizeof(uint32_t), progCk.programOffsetAddr);
  VGMHeader *progParamOffsetHdr = progCkHdr->AddHeader(curOffset + 16,
                                                       (progCk.maxProgramNumber + 1) * sizeof(uint32_t),
                                                       L"Program Param Offsets");
  VGMHeader *progParamsHdr = progCkHdr->AddHeader(curOffset + 16 + (progCk.maxProgramNumber + 1) * sizeof(uint32_t),
                                                  0/*(progCk.maxProgramNumber+1)*sizeof(SonyPS2Instr::ProgParam)*/,
                                                  L"Program Params");

  this->RemoveContainer(aInstrs);            //Remove the instrument vector as a contained item of the VGMInstr, instead
  progParamsHdr->AddContainer(aInstrs);    //it will be the contained item of the "Program Params" item.  Thus showing
  //up in the treeview appropriately

  for (uint32_t i = 0; i <= progCk.maxProgramNumber; i++) {
    progParamOffsetHdr->AddSimpleItem(curOffset + 16 + i * sizeof(uint32_t), 4, L"Offset");
    if (progCk.programOffsetAddr[i] == 0xFFFFFFFF)
      continue;
    GetBytes(curOffset + progCk.programOffsetAddr[i], sizeof(SonyPS2Instr::ProgParam), progCk.progParamBlock + i);
    //VGMHeader* progParamHdr = progParamsHdr->AddHeader(curOffset+progCk.programOffsetAddr[i],
    //	sizeof(SonyPS2Instr::ProgParam), L"Program Param");

    SonyPS2Instr *instr = new SonyPS2Instr(this, curOffset + progCk.programOffsetAddr[i],
                                           sizeof(SonyPS2Instr::ProgParam), i / 128, i % 128);
    aInstrs.push_back(instr);

    instr->AddSimpleItem(curOffset + progCk.programOffsetAddr[i], 4, L"SplitBlock Addr");
    instr->AddSimpleItem(curOffset + progCk.programOffsetAddr[i] + 4, 1, L"Number of SplitBlocks");
    instr->AddSimpleItem(curOffset + progCk.programOffsetAddr[i] + 5, 1, L"Size of SplitBlock");
    instr->AddSimpleItem(curOffset + progCk.programOffsetAddr[i] + 6, 1, L"Program Volume");
    instr->AddSimpleItem(curOffset + progCk.programOffsetAddr[i] + 7, 1, L"Program Panpot");
    instr->AddSimpleItem(curOffset + progCk.programOffsetAddr[i] + 8, 1, L"Program Transpose");
    instr->AddSimpleItem(curOffset + progCk.programOffsetAddr[i] + 9, 1, L"Program Detune");
    instr->AddSimpleItem(curOffset + progCk.programOffsetAddr[i] + 10, 1, L"Key Follow Pan");
    instr->AddSimpleItem(curOffset + progCk.programOffsetAddr[i] + 11, 1, L"Key Follow Pan Center");
    instr->AddSimpleItem(curOffset + progCk.programOffsetAddr[i] + 12, 1, L"Program Attributes");
    instr->AddSimpleItem(curOffset + progCk.programOffsetAddr[i] + 13, 1, L"Reserved");
    instr->AddSimpleItem(curOffset + progCk.programOffsetAddr[i] + 14, 1, L"Program Pitch LFO Waveform");
    instr->AddSimpleItem(curOffset + progCk.programOffsetAddr[i] + 15, 1, L"Program Amp LFO Waveform");
    instr->AddSimpleItem(curOffset + progCk.programOffsetAddr[i] + 16, 1, L"Program Pitch LFO Start Phase");
    instr->AddSimpleItem(curOffset + progCk.programOffsetAddr[i] + 17, 1, L"Program Amp LFO Start Phase");
    instr->AddSimpleItem(curOffset + progCk.programOffsetAddr[i] + 18, 1, L"Program Pitch LFO Start Phase Random");
    instr->AddSimpleItem(curOffset + progCk.programOffsetAddr[i] + 19, 1, L"Program Amp LFO Start Phase Random");
    instr->AddSimpleItem(curOffset + progCk.programOffsetAddr[i] + 20, 2, L"Program Pitch LFO Cycle Period (msec)");
    instr->AddSimpleItem(curOffset + progCk.programOffsetAddr[i] + 22, 2, L"Program Amp LFO Cycle Period (msec)");
    instr->AddSimpleItem(curOffset + progCk.programOffsetAddr[i] + 24, 2, L"Program Pitch LFO Depth (+)");
    instr->AddSimpleItem(curOffset + progCk.programOffsetAddr[i] + 26, 2, L"Program Pitch LFO Depth (-)");
    instr->AddSimpleItem(curOffset + progCk.programOffsetAddr[i] + 28, 2, L"MIDI Pitch Modulation Max Amplitude (+)");
    instr->AddSimpleItem(curOffset + progCk.programOffsetAddr[i] + 30, 2, L"MIDI Pitch Modulation Max Amplitude (-)");
    instr->AddSimpleItem(curOffset + progCk.programOffsetAddr[i] + 32, 1, L"Program Amp LFO Depth (+)");
    instr->AddSimpleItem(curOffset + progCk.programOffsetAddr[i] + 33, 1, L"Program Amp LFO Depth (-)");
    instr->AddSimpleItem(curOffset + progCk.programOffsetAddr[i] + 34, 1, L"MIDI Amp Modulation Max Amplitude (+)");
    instr->AddSimpleItem(curOffset + progCk.programOffsetAddr[i] + 35, 1, L"MIDI Amp Modulation Max Amplitude (-)");

    assert(progCk.progParamBlock[i].sizeSplitBlock == 20);    //make sure the size of a split block is indeed 20
    uint8_t nSplits = progCk.progParamBlock[i].nSplit;
    instr->unLength += nSplits * sizeof(SonyPS2Instr::SplitBlock);
    uint32_t absSplitBlocksAddr = curOffset + progCk.programOffsetAddr[i] + progCk.progParamBlock[i].splitBlockAddr;
    instr->splitBlocks = new SonyPS2Instr::SplitBlock[nSplits];
    GetBytes(absSplitBlocksAddr, nSplits * sizeof(SonyPS2Instr::SplitBlock), instr->splitBlocks);
    VGMHeader *splitBlocksHdr = instr->AddHeader(absSplitBlocksAddr,
                                                 nSplits * sizeof(SonyPS2Instr::SplitBlock), L"Split Blocks");
    for (uint8_t j = 0; j < nSplits; j++) {
      uint32_t splitOff = absSplitBlocksAddr + j * sizeof(SonyPS2Instr::SplitBlock);
      VGMHeader *splitBlockHdr = splitBlocksHdr->AddHeader(splitOff,
                                                           sizeof(SonyPS2Instr::SplitBlock), L"Split Block");
      splitBlockHdr->AddSimpleItem(splitOff, 2, L"Sample Set Index");
      splitBlockHdr->AddSimpleItem(splitOff + 2, 1, L"Split Range Low");
      splitBlockHdr->AddSimpleItem(splitOff + 3, 1, L"Split Cross Fade");
      splitBlockHdr->AddSimpleItem(splitOff + 4, 1, L"Split Range High");
      splitBlockHdr->AddSimpleItem(splitOff + 5, 1, L"Split Number");
      splitBlockHdr->AddSimpleItem(splitOff + 6, 2, L"Split Bend Range Low");
      splitBlockHdr->AddSimpleItem(splitOff + 8, 2, L"Split Bend Range High");
      splitBlockHdr->AddSimpleItem(splitOff + 10, 1, L"Key Follow Pitch");
      splitBlockHdr->AddSimpleItem(splitOff + 11, 1, L"Key Follow Pitch Center");
      splitBlockHdr->AddSimpleItem(splitOff + 12, 1, L"Key Follow Amp");
      splitBlockHdr->AddSimpleItem(splitOff + 13, 1, L"Key Follow Amp Center");
      splitBlockHdr->AddSimpleItem(splitOff + 14, 1, L"Key Follow Pan");
      splitBlockHdr->AddSimpleItem(splitOff + 15, 1, L"Key Follow Pan Center");
      splitBlockHdr->AddSimpleItem(splitOff + 16, 1, L"Split Volume");
      splitBlockHdr->AddSimpleItem(splitOff + 17, 1, L"Split Panpot");
      splitBlockHdr->AddSimpleItem(splitOff + 18, 1, L"Split Transpose");
      splitBlockHdr->AddSimpleItem(splitOff + 19, 1, L"Split Detune");
    }
  }

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
    : VGMInstr(instrSet, offset, length, theBank, theInstrNum, L"Program Param"),
      splitBlocks(0) {
  RemoveContainer(aRgns);
}

SonyPS2Instr::~SonyPS2Instr(void) {
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
    uint32_t length = (i == numVagInfos - 1) ? this->rawfile->size() - offset :
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

    wostringstream name;
    name << L"Sample " << samples.size();
    PSXSamp *samp = new PSXSamp(this, offset, length, offset, length, 1, 16, sampleRate, name.str(), true);
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