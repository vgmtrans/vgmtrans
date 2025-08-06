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

  if (!seqs.empty() && !instrsets.empty())
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
  // 1. Sort all three containers by descending file‑offset
  auto byOffsetDesc = [](auto* a, auto* b) { return a->dwOffset > b->dwOffset; };
  seqs.sort(byOffsetDesc);
  instrsets.sort(byOffsetDesc);
  sampcolls.sort(byOffsetDesc);

  // An instrument set plus an optional extra sample collection to add to the VGMColl
  struct InstrAssoc {
    VGMInstrSet* instrSet;
    VGMSampColl* sampColl;
  };

  std::vector<InstrAssoc> instrAssocs;
  instrAssocs.reserve(instrsets.size());

  bool forcePairSingle = (instrsets.size() == 1 && sampcolls.size() == 1);
  VGMSampColl* lastValidSamp = nullptr;

  // 2. Build the list of usable (instrset, extraSamp) associations
  for (VGMInstrSet* instr : instrsets) {
    VGMSampColl* extraSamp = nullptr;

    if (instr->sampColl == nullptr) {
      if (forcePairSingle) {                     // exactly one‑and‑one: pair regardless
        extraSamp = sampcolls.front();
        sampcolls.clear();
        lastValidSamp = extraSamp;
      } else {
        // look for an unused compatible sample collection
        for (auto scIt = sampcolls.begin(); scIt != sampcolls.end(); ++scIt) {
          VGMSampColl* sampleColl = *scIt;
          if (instr->isViableSampCollMatch(sampleColl)) {
            extraSamp = sampleColl;
            lastValidSamp = sampleColl;
            sampcolls.erase(scIt);         // consume it
            break;
          }
        }
        // none left → reuse the last compatible one if still valid
        if (!extraSamp && lastValidSamp && instr->isViableSampCollMatch(lastValidSamp)) {
          extraSamp = lastValidSamp;
        }
      }
    } else {
      extraSamp = instr->sampColl;                // use the existing sample collection
    }

    // keep only instrsets that actually have a sample collection to work with
    if (extraSamp != nullptr) {
      instrAssocs.push_back({instr, extraSamp});
    }
  }

  if (instrAssocs.empty()) {
    return;                                        // nothing left to match
  }

  // 3. For every sequence, build a VGMColl with the next instrument association
  auto assocIt = instrAssocs.begin();
  for (VGMSeq* seq : seqs) {
    const InstrAssoc& assoc = *assocIt;

    VGMColl* coll = fmt->newCollection();
    coll->setName(seq->name());
    coll->useSeq(seq);
    coll->addInstrSet(assoc.instrSet);
    if (assoc.sampColl && assoc.instrSet->sampColl == nullptr) {
      coll->addSampColl(assoc.sampColl);
    }

    if (!coll->load()) {
      delete coll;
    }

    // advance through the association list; keep using the last one once we run out
    if (std::next(assocIt) != instrAssocs.end()) {
      ++assocIt;
    }
  }
}
