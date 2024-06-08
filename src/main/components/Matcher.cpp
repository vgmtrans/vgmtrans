/*
 * VGMTrans (c) 2002-2014
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "Matcher.h"
#include "Root.h"

using namespace std;

Matcher::Matcher(Format *format) {
  fmt = format;
}

bool Matcher::onNewFile(std::variant<VGMSeq *, VGMInstrSet *, VGMSampColl *, VGMMiscFile *> file) {
  if(auto seq = std::get_if<VGMSeq *>(&file)) {
    onNewSeq(*seq);
  } else if(auto instr = std::get_if<VGMInstrSet *>(&file)) {
    onNewInstrSet(*instr);
  } else if(auto sampcoll = std::get_if<VGMSampColl *>(&file)) {
    onNewSampColl(*sampcoll);
}

  return false;
}

bool Matcher::onCloseFile(std::variant<VGMSeq *, VGMInstrSet *, VGMSampColl *, VGMMiscFile *> file) {
  if(auto seq = std::get_if<VGMSeq *>(&file)) {
    onCloseSeq(*seq);
  } else if(auto instr = std::get_if<VGMInstrSet *>(&file)) {
    onCloseInstrSet(*instr);
  } else if(auto sampcoll = std::get_if<VGMSampColl *>(&file)) {
    onCloseSampColl(*sampcoll);
  }
  return false;
}

bool Matcher::makeCollectionsForFile(VGMFile* /*file*/) {
  return false;
}

// ****************
// FilegroupMatcher
// ****************

FilegroupMatcher::FilegroupMatcher(Format *format) : Matcher(format) {}

bool FilegroupMatcher::onNewSeq(VGMSeq *seq) {
  seqs.push_back(seq);
  lookForMatch();
  return true;
}

bool FilegroupMatcher::onNewInstrSet(VGMInstrSet *instrset) {
  instrsets.push_back(instrset);
  lookForMatch();
  return true;
}

bool FilegroupMatcher::onNewSampColl(VGMSampColl *sampcoll) {
  sampcolls.push_back(sampcoll);
  lookForMatch();
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

void FilegroupMatcher::makeCollection(VGMInstrSet *instrset, VGMSampColl *sampcoll) {
  if (seqs.size() >= 1) {
    for(VGMSeq *seq : seqs) {
      VGMColl *coll = fmt->newCollection();
      coll->setName(seq->name());
      coll->useSeq(seq);
      coll->addInstrSet(instrset);
      coll->addSampColl(sampcoll);
      if (!coll->load()) {
        delete coll;
      }
    }
  }
  else {
    VGMColl *coll = fmt->newCollection();
    coll->setName(instrset->name());
    coll->useSeq(nullptr);
    coll->addInstrSet(instrset);
    coll->addSampColl(sampcoll);
    if (!coll->load()) {
      delete coll;
    }
  }
}

void FilegroupMatcher::lookForMatch() {

  if (g_isCliMode) {
    // in CLI mode, do not update collections dynamically
    return;
  }

  if (instrsets.size() == 1 && sampcolls.size() == 1) {
    VGMInstrSet* instrset = instrsets.front();
    VGMSampColl* sampcoll = sampcolls.front();
    makeCollection(instrset, sampcoll);
    seqs.clear();
    instrsets.clear();
    sampcolls.clear();
  }
}

bool FilegroupMatcher::makeCollectionsForFile(VGMFile* /*file*/) {

  if (instrsets.size() >= 1 && sampcolls.size() >= 1) {

    // make one VGMCollection for each sequence

    // NOTE: this only uses the first instrument set & sample collection
    // VGMInstrSet *instrset = instrsets.front();
    // VGMSampColl *sampcoll = sampcolls.front();
    // MakeCollection(instrset, sampcoll);

    // alternatively, we can combine all instrument sets & sample collections
    // into the same VGMCollection
    for(VGMSeq *seq : seqs) {
      VGMColl *coll = fmt->newCollection();
      coll->setName(seq->name());
      coll->useSeq(seq);
      for (VGMInstrSet* instrset : instrsets) {
        for (VGMSampColl* sampcoll : sampcolls) {
          coll->addInstrSet(instrset);
          coll->addSampColl(sampcoll);
        }
      }
      if (!coll->load()) {
        delete coll;
      }
    }
  }

  seqs.clear();
  instrsets.clear();
  sampcolls.clear();
  return true;
}