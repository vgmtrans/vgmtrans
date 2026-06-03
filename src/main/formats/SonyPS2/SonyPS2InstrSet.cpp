#include "SonyPS2InstrSet.h"

#include "base/Types.h"
#include "PSXSPU.h"

using namespace std;

// ***************
// SonyPS2InstrSet
// ***************

SonyPS2InstrSet::SonyPS2InstrSet(RawFile *file, u32 offset)
    : VGMInstrSet(SonyPS2Format::name, file, offset, 0, "Sony PS2 InstrSet") {
  // Instruments are represented by the "Program Param" struct which are contained as a list within
  // the "Program Chunk". We want to disable the defualt VGMInstrSet behavior of adding instruments
  // as root children, instead we'll add them as children of the Program Chunk ourselves.
  disableAutoAddInstrumentsAsChildren();
}

SonyPS2InstrSet::~SonyPS2InstrSet() {
}


bool SonyPS2InstrSet::parseHeader() {
  // VERSION CHUNK
  u32 curOffset = offset();
  readBytes(curOffset, 16, &versCk);
  VGMHeader *versCkHdr = addHeader(curOffset, versCk.chunkSize, "Version Chunk");
  versCkHdr->addChild(curOffset, 4, "Creator");
  versCkHdr->addChild(curOffset + 4, 4, "Type");
  versCkHdr->addChild(curOffset + 8, 4, "Chunk Size");
  versCkHdr->addChild(curOffset + 12, 2, "Reserved");
  versCkHdr->addChild(curOffset + 14, 1, "Major Version");
  versCkHdr->addChild(curOffset + 15, 1, "Minor Version");

  // HEADER CHUNK
  curOffset += versCk.chunkSize;
  readBytes(curOffset, 64, &hdrCk);
  setLength(hdrCk.fileSize);

  VGMHeader *hdrCkHdr = addHeader(curOffset, hdrCk.chunkSize, "Header Chunk");
  hdrCkHdr->addChild(curOffset, 4, "Creator");
  hdrCkHdr->addChild(curOffset + 4, 4, "Type");
  hdrCkHdr->addChild(curOffset + 8, 4, "Chunk Size");
  hdrCkHdr->addChild(curOffset + 12, 4, "Entire Header Size");
  hdrCkHdr->addChild(curOffset + 16, 4, "Body Size");
  hdrCkHdr->addChild(curOffset + 20, 4, "Program Chunk Addr");
  hdrCkHdr->addChild(curOffset + 24, 4, "SampleSet Chunk Addr");
  hdrCkHdr->addChild(curOffset + 28, 4, "Sample Chunk Addr");
  hdrCkHdr->addChild(curOffset + 32, 4, "VAG Info Chunk Addr");
  //hdrCkHdr->addSimpleChild(curOffset+36, 4, "Sound Effect Timbre Chunk Addr");

  // PROGRAM CHUNK
  // this is handled in GetInstrPointers()

  // SAMPLESET CHUNK
  curOffset = offset() + hdrCk.samplesetChunkAddr;
  readBytes(curOffset, 16, &sampSetCk);
  sampSetCk.sampleSetOffsetAddr = new u32[sampSetCk.maxSampleSetNumber + 1];
  sampSetCk.sampleSetParam = new SampSetParam[sampSetCk.maxSampleSetNumber + 1];

  VGMHeader *sampSetCkHdr = addHeader(curOffset, sampSetCk.chunkSize, "SampleSet Chunk");
  sampSetCkHdr->addChild(curOffset, 4, "Creator");
  sampSetCkHdr->addChild(curOffset + 4, 4, "Type");
  sampSetCkHdr->addChild(curOffset + 8, 4, "Chunk Size");
  sampSetCkHdr->addChild(curOffset + 12, 4, "Max SampleSet Number");

  readBytes(curOffset + 16, (sampSetCk.maxSampleSetNumber + 1) * sizeof(u32), sampSetCk.sampleSetOffsetAddr);
  VGMHeader *sampSetParamOffsetHdr = sampSetCkHdr->addHeader(curOffset + 16,
                                                             (sampSetCk.maxSampleSetNumber + 1) * sizeof(u32),
                                                             "SampleSet Param Offsets");
  VGMHeader *sampSetParamsHdr = sampSetCkHdr->addHeader(curOffset + 16 + (sampSetCk.maxSampleSetNumber + 1) * sizeof(u32),
                              (sampSetCk.maxSampleSetNumber + 1) * sizeof(SampSetParam), "SampleSet Params");
  for (u32 i = 0; i <= sampSetCk.maxSampleSetNumber; i++) {
    sampSetParamOffsetHdr->addChild(curOffset + 16 + i * sizeof(u32), 4, "Offset");
    if (sampSetCk.sampleSetOffsetAddr[i] == 0xFFFFFFFF)
      continue;
    readBytes(curOffset + sampSetCk.sampleSetOffsetAddr[i], sizeof(u8) * 4, sampSetCk.sampleSetParam + i);
    u8 nSamples = sampSetCk.sampleSetParam[i].nSample;
    sampSetCk.sampleSetParam[i].sampleIndex = new u16[nSamples];
    readBytes(curOffset + sampSetCk.sampleSetOffsetAddr[i] + sizeof(u8) * 4, nSamples * sizeof(u16),
             sampSetCk.sampleSetParam[i].sampleIndex);
    VGMHeader *sampSetParamHdr = sampSetParamsHdr->addHeader(curOffset + sampSetCk.sampleSetOffsetAddr[i],
                                                             sizeof(u8) * 4 + nSamples * sizeof(u16),
                                                             "SampleSet Param");
    sampSetParamHdr->addChild(curOffset + sampSetCk.sampleSetOffsetAddr[i], 1, "Vel Curve");
    sampSetParamHdr->addChild(curOffset + sampSetCk.sampleSetOffsetAddr[i] + 1, 1, "Vel Limit Low");
    sampSetParamHdr->addChild(curOffset + sampSetCk.sampleSetOffsetAddr[i] + 2, 1, "Vel Limit High");
    sampSetParamHdr->addChild(curOffset + sampSetCk.sampleSetOffsetAddr[i] + 3, 1, "Number of Samples");
    for (u32 j = 0; j < nSamples; j++)
      sampSetParamHdr->addChild(curOffset + sampSetCk.sampleSetOffsetAddr[i] + 4 + j * 2, 2, "Sample Index");
  }

  // SAMPLE CHUNK
  curOffset = offset() + hdrCk.sampleChunkAddr;
  readBytes(curOffset, 16, &sampCk);
  sampCk.sampleOffsetAddr = new u32[sampCk.maxSampleNumber + 1];
  sampCk.sampleParam = new SampleParam[sampCk.maxSampleNumber + 1];

  VGMHeader *sampCkHdr = addHeader(curOffset, sampCk.chunkSize, "Sample Chunk");
  sampCkHdr->addChild(curOffset, 4, "Creator");
  sampCkHdr->addChild(curOffset + 4, 4, "Type");
  sampCkHdr->addChild(curOffset + 8, 4, "Chunk Size");
  sampCkHdr->addChild(curOffset + 12, 4, "Max Sample Number");

  readBytes(curOffset + 16, (sampCk.maxSampleNumber + 1) * sizeof(u32), sampCk.sampleOffsetAddr);
  VGMHeader *sampleParamOffsetHdr = sampCkHdr->addHeader(curOffset + 16,
                                                         (sampCk.maxSampleNumber + 1) * sizeof(u32),
                                                         "Sample Param Offsets");
  VGMHeader *sampleParamsHdr = sampCkHdr->addHeader(curOffset + 16 + (sampCk.maxSampleNumber + 1) * sizeof(u32),
                                                    (sampCk.maxSampleNumber + 1) * sizeof(SampleParam),
                                                    "Sample Params");
  for (u32 i = 0; i <= sampCk.maxSampleNumber; i++) {
    sampleParamOffsetHdr->addChild(curOffset + 16 + i * sizeof(u32), 4, "Offset");
    readBytes(curOffset + sampCk.sampleOffsetAddr[i], sizeof(SampleParam), sampCk.sampleParam + i);
    VGMHeader *sampleParamHdr = sampleParamsHdr->addHeader(curOffset + sampCk.sampleOffsetAddr[i],
                                                           sizeof(SampleParam), "Sample Param");
    sampleParamHdr->addChild(curOffset + sampCk.sampleOffsetAddr[i], 2, "VAG Index");
    sampleParamHdr->addChild(curOffset + sampCk.sampleOffsetAddr[i] + 2, 1, "Vel Range Low");
    sampleParamHdr->addChild(curOffset + sampCk.sampleOffsetAddr[i] + 3, 1, "Vel Cross Fade");
    sampleParamHdr->addChild(curOffset + sampCk.sampleOffsetAddr[i] + 4, 1, "Vel Range High");
    sampleParamHdr->addChild(curOffset + sampCk.sampleOffsetAddr[i] + 5, 1, "Vel Follow Pitch");
    sampleParamHdr->addChild(curOffset + sampCk.sampleOffsetAddr[i] + 6, 1, "Vel Follow Pitch Center");
    sampleParamHdr->addChild(curOffset + sampCk.sampleOffsetAddr[i] + 7, 1, "Vel Follow Pitch Vel Curve");
    sampleParamHdr->addChild(curOffset + sampCk.sampleOffsetAddr[i] + 8, 1, "Vel Follow Amp");
    sampleParamHdr->addChild(curOffset + sampCk.sampleOffsetAddr[i] + 9, 1, "Vel Follow Amp Center");
    sampleParamHdr->addChild(curOffset + sampCk.sampleOffsetAddr[i] + 10, 1, "Vel Follow Amp Vel Curve");
    sampleParamHdr->addChild(curOffset + sampCk.sampleOffsetAddr[i] + 11, 1, "Sample Base Note");
    sampleParamHdr->addChild(curOffset + sampCk.sampleOffsetAddr[i] + 12, 1, "Sample Detune");
    sampleParamHdr->addChild(curOffset + sampCk.sampleOffsetAddr[i] + 13, 1, "Sample Panpot");
    sampleParamHdr->addChild(curOffset + sampCk.sampleOffsetAddr[i] + 14, 1, "Sample Group");
    sampleParamHdr->addChild(curOffset + sampCk.sampleOffsetAddr[i] + 15, 1, "Sample Priority");
    sampleParamHdr->addChild(curOffset + sampCk.sampleOffsetAddr[i] + 16, 1, "Sample Volume");
    sampleParamHdr->addChild(curOffset + sampCk.sampleOffsetAddr[i] + 17, 1, "Reserved");
    sampleParamHdr->addChild(curOffset + sampCk.sampleOffsetAddr[i] + 18, 2, "Sample ADSR1");
    sampleParamHdr->addChild(curOffset + sampCk.sampleOffsetAddr[i] + 20, 2, "Sample ADSR2");
    sampleParamHdr->addChild(curOffset + sampCk.sampleOffsetAddr[i] + 22, 1, "Key Follow Attack Rate");
    sampleParamHdr->addChild(curOffset + sampCk.sampleOffsetAddr[i] + 23, 1, "Key Follow Attack Rate Center");
    sampleParamHdr->addChild(curOffset + sampCk.sampleOffsetAddr[i] + 24, 1, "Key Follow Decay Rate");
    sampleParamHdr->addChild(curOffset + sampCk.sampleOffsetAddr[i] + 25, 1, "Key Follow Decay Rate Center");
    sampleParamHdr->addChild(curOffset + sampCk.sampleOffsetAddr[i] + 26, 1, "Key Follow Sustain Rate");
    sampleParamHdr->addChild(curOffset + sampCk.sampleOffsetAddr[i] + 27, 1, "Key Follow Sustain Rate Center");
    sampleParamHdr->addChild(curOffset + sampCk.sampleOffsetAddr[i] + 28, 1, "Key Follow Release Rate");
    sampleParamHdr->addChild(curOffset + sampCk.sampleOffsetAddr[i] + 29, 1, "Key Follow Release Rate Center");
    sampleParamHdr->addChild(curOffset + sampCk.sampleOffsetAddr[i] + 30, 1, "Key Follow Sustain Level");
    sampleParamHdr->addChild(curOffset + sampCk.sampleOffsetAddr[i] + 31, 1, "Key Follow Sustain Level Center");
    sampleParamHdr->addChild(curOffset + sampCk.sampleOffsetAddr[i] + 32, 2, "Sample Pitch LFO Delay");
    sampleParamHdr->addChild(curOffset + sampCk.sampleOffsetAddr[i] + 34, 2, "Sample Pitch LFO Fade");
    sampleParamHdr->addChild(curOffset + sampCk.sampleOffsetAddr[i] + 36, 2, "Sample Amp LFO Delay");
    sampleParamHdr->addChild(curOffset + sampCk.sampleOffsetAddr[i] + 38, 2, "Sample Amp LFO Fade");
    sampleParamHdr->addChild(curOffset + sampCk.sampleOffsetAddr[i] + 40, 1, "Sample LFO Attributes");
    sampleParamHdr->addChild(curOffset + sampCk.sampleOffsetAddr[i] + 41, 1, "Sample SPU Attributes");
  }

  // VAGInfo CHUNK
  curOffset = offset() + hdrCk.vagInfoChunkAddr;
  readBytes(curOffset, 16, &vagInfoCk);
  vagInfoCk.vagInfoOffsetAddr = new u32[vagInfoCk.maxVagInfoNumber + 1];
  vagInfoCk.vagInfoParam = new VAGInfoParam[vagInfoCk.maxVagInfoNumber + 1];

  VGMHeader *vagInfoCkHdr = addHeader(curOffset, vagInfoCk.chunkSize, "VAGInfo Chunk");
  vagInfoCkHdr->addChild(curOffset, 4, "Creator");
  vagInfoCkHdr->addChild(curOffset + 4, 4, "Type");
  vagInfoCkHdr->addChild(curOffset + 8, 4, "Chunk Size");
  vagInfoCkHdr->addChild(curOffset + 12, 4, "Max VAGInfo Number");

  readBytes(curOffset + 16, (vagInfoCk.maxVagInfoNumber + 1) * sizeof(u32), vagInfoCk.vagInfoOffsetAddr);
  VGMHeader *vagInfoParamOffsetHdr = vagInfoCkHdr->addHeader(curOffset + 16,
                                                             (vagInfoCk.maxVagInfoNumber + 1) * sizeof(u32),
                                                             "VAGInfo Param Offsets");
  VGMHeader *vagInfoParamsHdr = vagInfoCkHdr->addHeader(curOffset + 16 + (vagInfoCk.maxVagInfoNumber + 1) * sizeof(u32),
                                                  (vagInfoCk.maxVagInfoNumber + 1) * sizeof(VAGInfoParam),
                                                  "VAGInfo Params");
  for (u32 i = 0; i <= vagInfoCk.maxVagInfoNumber; i++) {
    vagInfoParamOffsetHdr->addChild(curOffset + 16 + i * sizeof(u32), 4, "Offset");
    readBytes(curOffset + vagInfoCk.vagInfoOffsetAddr[i], sizeof(VAGInfoParam), vagInfoCk.vagInfoParam + i);
    VGMHeader *vagInfoParamHdr = vagInfoParamsHdr->addHeader(curOffset + vagInfoCk.vagInfoOffsetAddr[i],
                                                             sizeof(VAGInfoParam), "VAGInfo Param");
    vagInfoParamHdr->addChild(curOffset + vagInfoCk.vagInfoOffsetAddr[i], 4, "VAG Offset Addr");
    vagInfoParamHdr->addChild(curOffset + vagInfoCk.vagInfoOffsetAddr[i] + 4, 2, "Sampling Rate");
    vagInfoParamHdr->addChild(curOffset + vagInfoCk.vagInfoOffsetAddr[i] + 6, 1, "Loop Flag");
    vagInfoParamHdr->addChild(curOffset + vagInfoCk.vagInfoOffsetAddr[i] + 7, 1, "Reserved");
  }
  return true;
}

bool SonyPS2InstrSet::parseInstrPointers() {
  u32 curOffset = offset() + hdrCk.programChunkAddr;
  //Now we're at the Program chunk, which starts with the sig "SCEIProg" (in 32bit little endian)
  //read in the first 4 values.  The programs will be read within GetInstrPointers()
  readBytes(curOffset, 16, &progCk);

  progCk.programOffsetAddr = new u32[progCk.maxProgramNumber + 1];
  progCk.progParamBlock = new SonyPS2Instr::ProgParam[progCk.maxProgramNumber + 1];

  VGMHeader *progCkHdr = addHeader(curOffset, progCk.chunkSize, "Program Chunk");
  progCkHdr->addChild(curOffset, 4, "Creator");
  progCkHdr->addChild(curOffset + 4, 4, "Type");
  progCkHdr->addChild(curOffset + 8, 4, "Chunk Size");
  progCkHdr->addChild(curOffset + 12, 4, "Max Program Number");

  readBytes(curOffset + 16, (progCk.maxProgramNumber + 1) * sizeof(u32), progCk.programOffsetAddr);
  VGMHeader *progParamOffsetHdr = progCkHdr->addHeader(curOffset + 16,
                                                       (progCk.maxProgramNumber + 1) * sizeof(u32),
                                                       "Program Param Offsets");
  VGMHeader *progParamsHdr = progCkHdr->addHeader(curOffset + 16 + (progCk.maxProgramNumber + 1) * sizeof(u32),
                                                  0/*(progCk.maxProgramNumber+1)*sizeof(SonyPS2Instr::ProgParam)*/,
                                                  "Program Params");

  for (u32 i = 0; i <= progCk.maxProgramNumber; i++) {
    progParamOffsetHdr->addChild(curOffset + 16 + i * sizeof(u32), 4, "Offset");
    if (progCk.programOffsetAddr[i] == 0xFFFFFFFF)
      continue;
    readBytes(curOffset + progCk.programOffsetAddr[i], sizeof(SonyPS2Instr::ProgParam), progCk.progParamBlock + i);
    //VGMHeader* progParamHdr = progParamsHdr->addHeader(curOffset+progCk.programOffsetAddr[i],
    //	sizeof(SonyPS2Instr::ProgParam), "Program Param");

    SonyPS2Instr *instr = new SonyPS2Instr(this, curOffset + progCk.programOffsetAddr[i],
                                           sizeof(SonyPS2Instr::ProgParam), i / 128, i % 128);
    aInstrs.push_back(instr);

    instr->addChild(curOffset + progCk.programOffsetAddr[i], 4, "SplitBlock Addr");
    instr->addChild(curOffset + progCk.programOffsetAddr[i] + 4, 1, "Number of SplitBlocks");
    instr->addChild(curOffset + progCk.programOffsetAddr[i] + 5, 1, "Size of SplitBlock");
    instr->addChild(curOffset + progCk.programOffsetAddr[i] + 6, 1, "Program Volume");
    instr->addChild(curOffset + progCk.programOffsetAddr[i] + 7, 1, "Program Panpot");
    instr->addChild(curOffset + progCk.programOffsetAddr[i] + 8, 1, "Program Transpose");
    instr->addChild(curOffset + progCk.programOffsetAddr[i] + 9, 1, "Program Detune");
    instr->addChild(curOffset + progCk.programOffsetAddr[i] + 10, 1, "Key Follow Pan");
    instr->addChild(curOffset + progCk.programOffsetAddr[i] + 11, 1, "Key Follow Pan Center");
    instr->addChild(curOffset + progCk.programOffsetAddr[i] + 12, 1, "Program Attributes");
    instr->addChild(curOffset + progCk.programOffsetAddr[i] + 13, 1, "Reserved");
    instr->addChild(curOffset + progCk.programOffsetAddr[i] + 14, 1, "Program Pitch LFO Waveform");
    instr->addChild(curOffset + progCk.programOffsetAddr[i] + 15, 1, "Program Amp LFO Waveform");
    instr->addChild(curOffset + progCk.programOffsetAddr[i] + 16, 1, "Program Pitch LFO Start Phase");
    instr->addChild(curOffset + progCk.programOffsetAddr[i] + 17, 1, "Program Amp LFO Start Phase");
    instr->addChild(curOffset + progCk.programOffsetAddr[i] + 18, 1, "Program Pitch LFO Start Phase Random");
    instr->addChild(curOffset + progCk.programOffsetAddr[i] + 19, 1, "Program Amp LFO Start Phase Random");
    instr->addChild(curOffset + progCk.programOffsetAddr[i] + 20, 2, "Program Pitch LFO Cycle Period (msec)");
    instr->addChild(curOffset + progCk.programOffsetAddr[i] + 22, 2, "Program Amp LFO Cycle Period (msec)");
    instr->addChild(curOffset + progCk.programOffsetAddr[i] + 24, 2, "Program Pitch LFO Depth (+)");
    instr->addChild(curOffset + progCk.programOffsetAddr[i] + 26, 2, "Program Pitch LFO Depth (-)");
    instr->addChild(curOffset + progCk.programOffsetAddr[i] + 28, 2, "MIDI Pitch Modulation Max Amplitude (+)");
    instr->addChild(curOffset + progCk.programOffsetAddr[i] + 30, 2, "MIDI Pitch Modulation Max Amplitude (-)");
    instr->addChild(curOffset + progCk.programOffsetAddr[i] + 32, 1, "Program Amp LFO Depth (+)");
    instr->addChild(curOffset + progCk.programOffsetAddr[i] + 33, 1, "Program Amp LFO Depth (-)");
    instr->addChild(curOffset + progCk.programOffsetAddr[i] + 34, 1, "MIDI Amp Modulation Max Amplitude (+)");
    instr->addChild(curOffset + progCk.programOffsetAddr[i] + 35, 1, "MIDI Amp Modulation Max Amplitude (-)");

    assert(progCk.progParamBlock[i].sizeSplitBlock == 20);    //make sure the size of a split block is indeed 20
    u8 nSplits = progCk.progParamBlock[i].nSplit;
    instr->setLength(instr->length() + nSplits * sizeof(SonyPS2Instr::SplitBlock));
    u32 absSplitBlocksAddr = curOffset + progCk.programOffsetAddr[i] + progCk.progParamBlock[i].splitBlockAddr;
    instr->splitBlocks = new SonyPS2Instr::SplitBlock[nSplits];
    readBytes(absSplitBlocksAddr, nSplits * sizeof(SonyPS2Instr::SplitBlock), instr->splitBlocks);
    VGMHeader *splitBlocksHdr = instr->addHeader(absSplitBlocksAddr,
                                                 nSplits * sizeof(SonyPS2Instr::SplitBlock), "Split Blocks");
    for (u8 j = 0; j < nSplits; j++) {
      u32 splitOff = absSplitBlocksAddr + j * sizeof(SonyPS2Instr::SplitBlock);
      VGMHeader *splitBlockHdr = splitBlocksHdr->addHeader(splitOff,
                                                           sizeof(SonyPS2Instr::SplitBlock), "Split Block");
      splitBlockHdr->addChild(splitOff, 2, "Sample Set Index");
      splitBlockHdr->addChild(splitOff + 2, 1, "Split Range Low");
      splitBlockHdr->addChild(splitOff + 3, 1, "Split Cross Fade");
      splitBlockHdr->addChild(splitOff + 4, 1, "Split Range High");
      splitBlockHdr->addChild(splitOff + 5, 1, "Split Number");
      splitBlockHdr->addChild(splitOff + 6, 2, "Split Bend Range Low");
      splitBlockHdr->addChild(splitOff + 8, 2, "Split Bend Range High");
      splitBlockHdr->addChild(splitOff + 10, 1, "Key Follow Pitch");
      splitBlockHdr->addChild(splitOff + 11, 1, "Key Follow Pitch Center");
      splitBlockHdr->addChild(splitOff + 12, 1, "Key Follow Amp");
      splitBlockHdr->addChild(splitOff + 13, 1, "Key Follow Amp Center");
      splitBlockHdr->addChild(splitOff + 14, 1, "Key Follow Pan");
      splitBlockHdr->addChild(splitOff + 15, 1, "Key Follow Pan Center");
      splitBlockHdr->addChild(splitOff + 16, 1, "Split Volume");
      splitBlockHdr->addChild(splitOff + 17, 1, "Split Panpot");
      splitBlockHdr->addChild(splitOff + 18, 1, "Split Transpose");
      splitBlockHdr->addChild(splitOff + 19, 1, "Split Detune");
    }
  }

  progParamsHdr->addChildren(aInstrs);

  u32 maxProgNum = progCk.maxProgramNumber;
  progParamsHdr->setLength((curOffset + progCk.programOffsetAddr[maxProgNum]) + sizeof(SonyPS2Instr::ProgParam) +
      progCk.progParamBlock[maxProgNum].nSplit * sizeof(SonyPS2Instr::SplitBlock) - progParamsHdr->offset());

  return true;
}


// ************
// SonyPS2Instr
// ************


SonyPS2Instr::SonyPS2Instr(VGMInstrSet *instrSet,
                           u32 offset,
                           u32 length,
                           u32 bank,
                           u32 instrNum)
    : VGMInstr(instrSet, offset, length, bank, instrNum, "Program Param") {
  // The regions we create do not line up with any data structure in the format, so we will not add
  // them as children. The format uses "split blocks" to define key regions, but it layers velocity
  // regions on top of them in a separate "sample set params" data structure. As such, split blocks
  // do not map 1:1 with our concept of regions: if a split block links to a sample set param with
  // multiple velocity zones, we'll need to make a region for each.
  disableAutoAddRegionsAsChildren();
}

SonyPS2Instr::~SonyPS2Instr() {
  if (splitBlocks)
    delete[] splitBlocks;

  // Since we do not add regions as children, which ~VGMItem() deletes for us,
  // we must delete the regions vector ourselves
  deleteRegions();
}

bool SonyPS2Instr::loadInstr() {
  SonyPS2InstrSet *instrset = (SonyPS2InstrSet *) parInstrSet;
  SonyPS2InstrSet::ProgCk &progCk = instrset->progCk;
  SonyPS2InstrSet::SampSetCk &sampSetCk = instrset->sampSetCk;
  SonyPS2InstrSet::SampCk &sampCk = instrset->sampCk;
  ProgParam &progParam = progCk.progParamBlock[this->instrNum];
  u8 nSplits = progParam.nSplit;
  for (u8 i = 0; i < nSplits; i++) {
    SplitBlock &splitblock = this->splitBlocks[i];
    SonyPS2InstrSet::SampSetParam &sampSetParam = sampSetCk.sampleSetParam[splitblock.sampleSetIndex];

    for (u8 j = 0; j < sampSetParam.nSample; j++) {
      SonyPS2InstrSet::SampleParam &sampParam = sampCk.sampleParam[sampSetParam.sampleIndex[j]];
      // sampParam.VagIndex indexes vagInfoCk.vagInfoParam; regions currently use the index directly.
      // WE ARE ASSUMING THE VAG SAMPLES ARE STORED CONSECUTIVELY
      int sampNum = sampParam.VagIndex;
      u8 noteLow = splitblock.splitRangeLow;
      u8 noteHigh = splitblock.splitRangeHigh;
      if (noteHigh < noteLow)
        noteHigh = 0x7F;
      u8 sampSetVelLow = convert7bitPercentAmpToStdMidiVal(sampSetParam.velLimitLow);
      u8 sampSetVelHigh = convert7bitPercentAmpToStdMidiVal(sampSetParam.velLimitHigh);
      u8 velLow = convert7bitPercentAmpToStdMidiVal(sampParam.velRangeLow);//sampSetParam.velLimitLow;
      u8 velHigh = convert7bitPercentAmpToStdMidiVal(sampParam.velRangeHigh);//sampSetParam.velLimitHigh;
      if (velLow < sampSetVelLow)
        velLow = sampSetVelLow;
      if (velHigh > sampSetVelHigh)
        velHigh = sampSetVelHigh;

      u8 unityNote = sampParam.sampleBaseNote;
      VGMRgn *rgn = this->addRgn(0, 0, sampNum, noteLow, noteHigh, velLow, velHigh);
      rgn->unityKey = unityNote;
      //rgn->SetPan(sampParam.samplePanpot);
      s16 pan = 0x40 + convertPanValue(sampParam.samplePanpot) + convertPanValue(progParam.progPanpot) +
          convertPanValue(splitblock.splitPanpot);
      if (pan > 0x7F) pan = 0x7F;
      if (pan < 0) pan = 0;
      //double realPan = (pan-0x40)* (1.0/(double)0x40);
      rgn->setPan((u8) pan);
      rgn->setFineTune(splitblock.splitTranspose * 100 + splitblock.splitDetune);

      long vol = progParam.progVolume * splitblock.splitVolume * sampParam.sampleVolume;
      //we divide the above value by 127^3 to get the percent vol it represents.  Then we convert it to DB units.
      //0xA0000 = 1db in the DLS lScale val for atten (dls1 specs p30)
      double percentvol = vol / (double) (127 * 127 * 127);
      rgn->setVolume(percentvol);
      psxConvADSR(rgn, sampParam.sampleAdsr1, sampParam.sampleAdsr2, true);
      //splitblock->splitRangeLow
    }
  }
  return true;
}

s8 SonyPS2Instr::convertPanValue(u8 panVal) {
  // Actually, it may be C1 is center, but i don't care to fix that right now since
  // I have yet to see an occurence of >0x7F pan
  if (panVal > 0x7F)        //if it's > 0x7F, 0x80 == right, 0xBF center 0xFF left
    return (s8) 0x40 - (s8) (panVal - 0x7F);
  else
    return (s8) panVal - (s8) 0x40;
}


// ***************
// SonyPS2SampColl
// ***************

SonyPS2SampColl::SonyPS2SampColl(RawFile *rawfile, u32 offset, u32 length)
    : VGMSampColl(SonyPS2Format::name, rawfile, offset, length) {
  this->setShouldLoadOnInstrSetMatch(true);
  pRoot->addVGMFile(this);
}

bool SonyPS2SampColl::parseSampleInfo() {
  SonyPS2InstrSet *instrset = (SonyPS2InstrSet *) parInstrSet;
  if (!instrset)
    return false;
  SonyPS2InstrSet::VAGInfoCk &vagInfoCk = instrset->vagInfoCk;
  u32 numVagInfos = vagInfoCk.maxVagInfoNumber + 1;
  for (u32 i = 0; i < numVagInfos; i++) {
    // Get offset, length, and samplerate from VAGInfo Param
    SonyPS2InstrSet::VAGInfoParam &vagInfoParam = vagInfoCk.vagInfoParam[i];
    u32 offset = vagInfoParam.vagOffsetAddr;
    u32 length = (i == numVagInfos - 1) ? this->rawFile()->size() - offset :
                      vagInfoCk.vagInfoParam[i + 1].vagOffsetAddr - offset;

    // We need to perform a hackish check to make sure the last ADPCM block was not
    // 0x00 07 77 77 77 etc.  If this is the case, we should ignore that block and treat the
    // previous block as the last.  It is just a strange quirk found among PS1/PS2 games
    // It should be safe to just check test the first 4 bytes as these values would be inconceivable
    // in any other case  (especially as bit 4 of second byte signifies the loop start point).
    u32 testWord = this->readWordBE(offset + length - 16);
    if (testWord == 0x00077777)
      length -= 16;

    u16 sampleRate = vagInfoParam.vagSampleRate;

    auto name = fmt::format("Sample {}", samples.size());
    PSXSamp *samp = new PSXSamp(this, offset, length, offset, length, 1,
      BPS::PCM16, sampleRate, name, true);
    samples.push_back(samp);

    // Determine loop information from VAGInfo Param
    if (vagInfoParam.vagAttribute == SCEHD_VAG_1SHOT) {
      samp->SetLoopOnConversion(false);
      samp->setLoopStatus(0);
    }
    else if (vagInfoParam.vagAttribute == SCEHD_VAG_LOOP)
      samp->SetLoopOnConversion(true);
  }

  return true;
}
