/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "HOSASeq.h"
#include "HOSAInstr.h"
#include "VGMColl.h"
#include "PSXSPU.h"
#include "ScannerManager.h"
#include "VGMMiscFile.h"

namespace vgmtrans::scanners {
ScannerRegistration<HOSAScanner> s_hosa("HOSA");
}

#define SRCH_BUF_SIZE 0x20000

void HOSAScanner::scan(RawFile *file, void *info) {
  HOSASeq *seq = searchForHOSASeq(file);
  if (seq == nullptr) {
    return;
  }

  // Scan for PSXSampColls if we haven't already
  std::vector<PSXSampColl *> sampcolls = file->containedVGMFilesOfType<PSXSampColl>();
  if (sampcolls.empty()) {
    sampcolls = PSXSampColl::searchForPSXADPCMs(file, HOSAFormat::name);
  }

  PSXSampColl *sampcoll = nullptr;
  HOSAInstrSet *instrset = nullptr;
  for (size_t i = 0; i < sampcolls.size(); i++) {
    instrset = searchForHOSAInstrSet(file, sampcolls[i]);
    if (instrset != nullptr) {
      sampcoll = sampcolls[i];
      break;
    }
  }

  for (size_t i = 0; i < sampcolls.size(); i++) {
    if (sampcolls[i] != sampcoll) {
      pRoot->removeVGMFile(sampcolls[i]);
    }
  }

  if (instrset == nullptr) {
    return;
  }

  VGMColl *coll = new VGMColl(seq->name());
  coll->useSeq(seq);
  coll->addInstrSet(instrset);
  coll->addSampColl(sampcoll);
  if (!coll->load()) {
    delete coll;
  }

  return;
}

HOSASeq *HOSAScanner::searchForHOSASeq(RawFile *file) {
  std::string name = file->tag.hasTitle() ? file->tag.title : file->stem();

  size_t nFileLength = file->size();
  for (uint32_t i = 0; i + 4 < nFileLength; i++) {
    // Signature must match
    if (file->readWordBE(i) != 0x484F5341 || file->readByte(i + 4) != 'V')  //"HOSAV"
      continue;
    // Number of tracks must not exceed 24 (I'm pretty sure)
    if (file->readByte(i + 6) > 24)
      continue;
    // First track pointer must != 0
    uint16_t firstTrkPtr = file->readShort(i + 0x50);
    if (firstTrkPtr == 0)
      continue;
    // First track pointer must be > second track pointer (if more than one track)
    if (firstTrkPtr >= file->readShort(i + 0x54) && firstTrkPtr != 0x54)
      continue;

    HOSASeq *seq = new HOSASeq(file, i, name);
    if (!seq->loadVGMFile()) {
      delete seq;
      return nullptr;
    }
    return seq;
  }
  return nullptr;
}

// This Scanner is quite imperfect.  It compares the offsets of the sample collection against the
// sample offsets in the region data, assuming that samples will be referenced consecutively.
#define MIN_NUM_SAMPLES_COMPARE 5
#define MIN_SAMPLES_MATCH 4
HOSAInstrSet *HOSAScanner::searchForHOSAInstrSet(RawFile *file, const PSXSampColl *sampcoll) {
  int numSamples = static_cast<int>(sampcoll->samples.size());
  if (numSamples < MIN_NUM_SAMPLES_COMPARE) {
    return nullptr;
  }

  uint32_t *sampOffsets = new uint32_t[numSamples];
  for (int i = 0; i < numSamples; i++)
    sampOffsets[i] = sampcoll->samples[i]->offset() - sampcoll->offset();

  size_t nFileLength = file->size();
  for (uint32_t i = 0x20; i + 0x14 < nFileLength; i++) {
    if (recursiveRgnCompare(file, i, 0, numSamples, 0, sampOffsets)) {
      for (; i >= 0x20; i -= 4) {
        if ((file->readWord(i + 4) != 0) || (file->readWord(i) != 0))
          continue;

        HOSAInstrSet *instrset = new HOSAInstrSet(file, i);
        if (!instrset->loadVGMFile()) {
          delete instrset;
          delete[] sampOffsets;
          return nullptr;
        }
        delete[] sampOffsets;
        return instrset;
      }
    }
  }
  delete[] sampOffsets;
  return nullptr;
}

bool HOSAScanner::recursiveRgnCompare(RawFile *file,
                                      int i,
                                      int sampNum,
                                      int numSamples,
                                      int numFinds,
                                      uint32_t *sampOffsets) {
  if (i < 0 || static_cast<uint32_t>(i + 0x14) >= file->size())
    return false;
  if (sampNum >= numSamples - 1)
    return (numFinds >= MIN_SAMPLES_MATCH);
  // i+0 would be sample pointer of next region of same instr
  // i+4 would be sample pointer of first region in new instr
  uint32_t word1 = file->readWord(i);
  uint32_t word2 = file->readWord(i + 4);
  uint32_t sampOffset = sampOffsets[sampNum];
  if (word1 == sampOffset)
    return recursiveRgnCompare(file, i + 0x10, sampNum + 1, numSamples, numFinds + 1, sampOffsets);
  else if (word1 == 0)
    return recursiveRgnCompare(file, i + 0x10, sampNum + 1, numSamples, numFinds, sampOffsets);
  else if (word2 == sampOffset)
    return recursiveRgnCompare(file, i + 4 + 0x10, sampNum + 1, numSamples, numFinds + 1, sampOffsets);
  else if (word2 == 0)
    return recursiveRgnCompare(file, i + 4 + 0x10, sampNum + 1, numSamples, numFinds, sampOffsets);
  else
    return (numFinds >= MIN_SAMPLES_MATCH);
}
