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

bool Matcher::OnNewFile(std::variant<VGMSeq *, VGMInstrSet *, VGMSampColl *, VGMMiscFile *> file) {
  if(auto seq = std::get_if<VGMSeq *>(&file)) {
    OnNewSeq(*seq);
  } else if(auto instr = std::get_if<VGMInstrSet *>(&file)) {
    OnNewInstrSet(*instr);
  } else if(auto sampcoll = std::get_if<VGMSampColl *>(&file)) {
    OnNewSampColl(*sampcoll);
}

  return false;
}

bool Matcher::OnCloseFile(std::variant<VGMSeq *, VGMInstrSet *, VGMSampColl *, VGMMiscFile *> file) {
  if(auto seq = std::get_if<VGMSeq *>(&file)) {
    OnCloseSeq(*seq);
  } else if(auto instr = std::get_if<VGMInstrSet *>(&file)) {
    OnCloseInstrSet(*instr);
  } else if(auto sampcoll = std::get_if<VGMSampColl *>(&file)) {
    OnCloseSampColl(*sampcoll);
  }
  return false;
}

bool Matcher::MakeCollectionsForFile(VGMFile* /*file*/) {
  return false;
}

// ****************
// FilegroupMatcher
// ****************

FilegroupMatcher::FilegroupMatcher(Format *format) : Matcher(format) {}

bool FilegroupMatcher::OnNewSeq(VGMSeq *seq) {
  seqs.push_back(seq);
  LookForMatch();
  return true;
}

bool FilegroupMatcher::OnNewInstrSet(VGMInstrSet *instrset) {
  instrsets.push_back(instrset);
  LookForMatch();
  return true;
}

bool FilegroupMatcher::OnNewSampColl(VGMSampColl *sampcoll) {
  sampcolls.push_back(sampcoll);
  LookForMatch();
  return true;
}

bool FilegroupMatcher::OnCloseSeq(VGMSeq *seq) {
  auto iterator = std::ranges::find(seqs, seq);
  if (iterator != seqs.end()) {
    seqs.erase(iterator);
  }
  return true;
}

bool FilegroupMatcher::OnCloseInstrSet(VGMInstrSet *instrset) {
  auto iterator = std::ranges::find(instrsets, instrset);
  if (iterator != instrsets.end()) {
    instrsets.erase(iterator);
  }
  return true;
}

bool FilegroupMatcher::OnCloseSampColl(VGMSampColl *sampcoll) {
  auto iterator = std::ranges::find(sampcolls, sampcoll);
  if (iterator != sampcolls.end()) {
    sampcolls.erase(iterator);
  }
  return true;
}

void FilegroupMatcher::MakeCollection(VGMInstrSet *instrset, VGMSampColl *sampcoll) {
  if (seqs.size() >= 1) {
    for(VGMSeq *seq : seqs) {
      VGMColl *coll = fmt->NewCollection();
      coll->SetName(seq->GetName());
      coll->UseSeq(seq);
      coll->AddInstrSet(instrset);
      coll->AddSampColl(sampcoll);
      if (!coll->Load()) {
        delete coll;
      }
    }
  }
  else {
    VGMColl *coll = fmt->NewCollection();
    coll->SetName(instrset->GetName());
    coll->UseSeq(nullptr);
    coll->AddInstrSet(instrset);
    coll->AddSampColl(sampcoll);
    if (!coll->Load()) {
      delete coll;
    }
  }
}

void FilegroupMatcher::LookForMatch() {

  if (g_isCliMode) {
    // in CLI mode, do not update collections dynamically
    return;
  }

  if (instrsets.size() == 1 && sampcolls.size() == 1) {
    VGMInstrSet* instrset = instrsets.front();
    VGMSampColl* sampcoll = sampcolls.front();
    MakeCollection(instrset, sampcoll);
    seqs.clear();
    instrsets.clear();
    sampcolls.clear();
  }
}

bool FilegroupMatcher::MakeCollectionsForFile(VGMFile* /*file*/) {

  if (instrsets.size() >= 1 && sampcolls.size() >= 1) {

    // make one VGMCollection for each sequence

    // NOTE: this only uses the first instrument set & sample collection
    // VGMInstrSet *instrset = instrsets.front();
    // VGMSampColl *sampcoll = sampcolls.front();
    // MakeCollection(instrset, sampcoll);

    // alternatively, we can combine all instrument sets & sample collections
    // into the same VGMCollection
    for(VGMSeq *seq : seqs) {
      VGMColl *coll = fmt->NewCollection();
      coll->SetName(seq->GetName());
      coll->UseSeq(seq);
      for (VGMInstrSet* instrset : instrsets) {
        for (VGMSampColl* sampcoll : sampcolls) {
          coll->AddInstrSet(instrset);
          coll->AddSampColl(sampcoll);
        }
      }
      if (!coll->Load()) {
        delete coll;
      }
    }
  }

  seqs.clear();
  instrsets.clear();
  sampcolls.clear();
  return true;
}