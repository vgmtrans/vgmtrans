/*
 * VGMTrans (c) 2002-2014
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "Matcher.h"
#include "Root.h"
#include <regex>

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

// ****************
// FilegroupMatcher
// ****************

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
