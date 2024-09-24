/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
*/

#include "FilegroupMatcher.h"
#include "Format.h"
#include "VGMSeq.h"
#include "VGMInstrSet.h"
#include "VGMSampColl.h"
#include "VGMColl.h"
#include <regex>

FilegroupMatcher::FilegroupMatcher(Format *format) : Matcher(format) {}


void FilegroupMatcher::onFinishedScan(RawFile* rawfile) {
  // xsflib files are loaded recursively with xsf files. We want to scan the xsflib and the xsf
  // files together as if they're one file, so ignore this callback and wait for the xsf to finish.
  if (std::regex_match(rawfile->extension(), std::regex(R"(\w*sf\w?lib$)")))
    return;

  lookForMatch();

  seqs.clear();
  instrsets.clear();
  sampcolls.clear();
}

bool FilegroupMatcher::onNewSeq(VGMSeq *seq) {
  seqs.push_back(seq);
  return true;
}

bool FilegroupMatcher::onNewInstrSet(VGMInstrSet *instrset) {
  instrsets.push_back(instrset);
  return true;
}

bool FilegroupMatcher::onNewSampColl(VGMSampColl *sampcoll) {
  sampcolls.push_back(sampcoll);
  return true;
}

bool FilegroupMatcher::onCloseSeq(VGMSeq *seq) {
  auto iterator = std::ranges::find(seqs, seq);
  if (iterator != seqs.end()) {
    seqs.erase(iterator);
  }
  return true;
}

bool FilegroupMatcher::onCloseInstrSet(VGMInstrSet *instrset) {
  auto iterator = std::ranges::find(instrsets, instrset);
  if (iterator != instrsets.end()) {
    instrsets.erase(iterator);
  }
  return true;
}

bool FilegroupMatcher::onCloseSampColl(VGMSampColl *sampcoll) {
  auto iterator = std::ranges::find(sampcolls, sampcoll);
  if (iterator != sampcolls.end()) {
    sampcolls.erase(iterator);
  }
  return true;
}

void FilegroupMatcher::lookForMatch() {
  while (!seqs.empty() && !instrsets.empty()) {
    // If there's only 1 of any collection type left, match to largest size.
    if (seqs.size() == 1 && sampcolls.size() > 1) {
      sampcolls.sort([](const VGMSampColl* a, const VGMSampColl* b) {
        return a->size() > b->size();
      });
    }
    else if (seqs.size() == 1 && instrsets.size() > 1) {
      instrsets.sort([](const VGMInstrSet* a, const VGMInstrSet* b) {
        return a->size() > b->size();
      });
    }
    else if (instrsets.size() == 1 && seqs.size() > 1) {
      seqs.sort([](const VGMSeq* a, const VGMSeq* b) {
        return a->size() > b->size();
      });
    }

    VGMSeq* seq = seqs.front();
    seqs.pop_front();
    VGMInstrSet* instrset = instrsets.front();
    instrsets.pop_front();
    VGMSampColl* sampcoll = nullptr;
    if (instrset->sampColl == nullptr) {
      if (sampcolls.empty()) {
        continue;
      }
      sampcoll = sampcolls.front();
      sampcolls.pop_front();
    }

    VGMColl *coll = fmt->newCollection();
    coll->setName(seq->name());
    coll->useSeq(seq);
    coll->addInstrSet(instrset);
    if (sampcoll != nullptr) {
      coll->addSampColl(sampcoll);
    }
    if (!coll->load()) {
      delete coll;
    }
  }
}
