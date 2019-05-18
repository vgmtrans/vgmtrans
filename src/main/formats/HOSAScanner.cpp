/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
 





#include "HOSASeq.h"
#include "HOSAInstr.h"
#include "PSXSPU.h"

#define SRCH_BUF_SIZE 0x20000

HOSAScanner::HOSAScanner(void) {
}

HOSAScanner::~HOSAScanner(void) {
}

void HOSAScanner::Scan(RawFile *file, void *info) {
  HOSASeq *seq = SearchForHOSASeq(file);
  if (seq == NULL) {
    return;
  }

  std::vector<PSXSampColl *> sampcolls = PSXSampColl::SearchForPSXADPCMs(file, HOSAFormat::name);

  PSXSampColl *sampcoll = NULL;
  HOSAInstrSet *instrset = NULL;
  for (size_t i = 0; i < sampcolls.size(); i++) {
    instrset = SearchForHOSAInstrSet(file, sampcolls[i]);
    if (instrset != NULL) {
      sampcoll = sampcolls[i];
      break;
    }
  }

  for (size_t i = 0; i < sampcolls.size(); i++) {
    if (sampcolls[i] != sampcoll) {
      pRoot->RemoveVGMFile(sampcolls[i]);
    }
  }

  if (instrset == NULL) {
    return;
  }

  VGMColl *coll = new VGMColl(*seq->GetName());
  coll->UseSeq(seq);
  coll->AddInstrSet(instrset);
  coll->AddSampColl(sampcoll);
  if (!coll->Load()) {
    delete coll;
  }

  return;
}

HOSASeq *HOSAScanner::SearchForHOSASeq(RawFile *file) {
  std::wstring name = file->tag.HasTitle() ? file->tag.title : RawFile::removeExtFromPath(file->GetFileName());

  uint32_t nFileLength = file->size();
  for (uint32_t i = 0; i + 4 < nFileLength; i++) {
    // Signature must match
    if (file->GetWordBE(i) != 0x484F5341 || file->GetByte(i + 4) != 'V')        //"HOSAV"
      continue;
    // Number of tracks must not exceed 24 (I'm pretty sure)
    if (file->GetByte(i + 6) > 24)
      continue;
    // First track pointer must != 0
    uint16_t firstTrkPtr = file->GetShort(i + 0x50);
    if (firstTrkPtr == 0)
      continue;
    // First track pointer must be > second track pointer (if more than one track)
    if (firstTrkPtr >= file->GetShort(i + 0x54) && firstTrkPtr != 0x54)
      continue;

    HOSASeq *seq = new HOSASeq(file, i, name);
    if (!seq->LoadVGMFile()) {
      delete seq;
      return NULL;
    }
    return seq;
  }
  return NULL;
}

// This Scanner is quite imperfect.  It compares the offsets of the sample collection against the sample
// offsets in the region data, assuming that samples will be referenced consecutively.
#define MIN_NUM_SAMPLES_COMPARE 5
#define MIN_SAMPLES_MATCH 4
HOSAInstrSet *HOSAScanner::SearchForHOSAInstrSet(RawFile *file, PSXSampColl *sampcoll) {
  size_t numSamples = sampcoll->samples.size();
  if (numSamples < MIN_NUM_SAMPLES_COMPARE) {
    return NULL;
  }

  uint32_t *sampOffsets = new uint32_t[numSamples];
  for (unsigned int i = 0; i < numSamples; i++)
    sampOffsets[i] = sampcoll->samples[i]->dwOffset - sampcoll->dwOffset;

  uint32_t nFileLength = file->size();
  for (uint32_t i = 0x20; i + 0x14 < nFileLength; i++) {
    if (RecursiveRgnCompare(file, i, 0, (int) numSamples, 0, sampOffsets)) {
      for (; i >= 0x20; i -= 4) {
        if ((file->GetWord(i + 4) != 0) || (file->GetWord(i) != 0))
          continue;

        HOSAInstrSet *instrset = new HOSAInstrSet(file, i);
        if (!instrset->LoadVGMFile()) {
          delete instrset;
          delete[] sampOffsets;
          return NULL;
        }
        delete[] sampOffsets;
        return instrset;
      }
    }
  }
  delete[] sampOffsets;
  return NULL;
}

bool HOSAScanner::RecursiveRgnCompare(RawFile *file,
                                      int i,
                                      int sampNum,
                                      int numSamples,
                                      int numFinds,
                                      uint32_t *sampOffsets) {
  if (i < 0 || (uint32_t) (i + 0x14) >= file->size())
    return false;
  if (sampNum >= numSamples - 1)
    return (numFinds >= MIN_SAMPLES_MATCH);
  // i+0 would be sample pointer of next region of same instr
  // i+4 would be sample pointer of first region in new instr
  uint32_t word1 = file->GetWord(i);
  uint32_t word2 = file->GetWord(i + 4);
  uint32_t sampOffset = sampOffsets[sampNum];
  if (word1 == sampOffset)
    return RecursiveRgnCompare(file, i + 0x10, sampNum + 1, numSamples, numFinds + 1, sampOffsets);
  else if (word1 == 0)
    return RecursiveRgnCompare(file, i + 0x10, sampNum + 1, numSamples, numFinds, sampOffsets);
  else if (word2 == sampOffset)
    return RecursiveRgnCompare(file, i + 4 + 0x10, sampNum + 1, numSamples, numFinds + 1, sampOffsets);
  else if (word2 == 0)
    return RecursiveRgnCompare(file, i + 4 + 0x10, sampNum + 1, numSamples, numFinds, sampOffsets);
  else
    return (numFinds >= MIN_SAMPLES_MATCH);
}