/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "Matcher.h"

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
    auto iterator = std::find(seqs.begin(), seqs.end(), seq);
    if (iterator != seqs.end()) {
        seqs.erase(iterator);
    }
    return true;
}

bool FilegroupMatcher::OnCloseInstrSet(VGMInstrSet *instrset) {
    auto iterator =
        std::find(instrsets.begin(), instrsets.end(), instrset);
    if (iterator != instrsets.end()) {
        instrsets.erase(iterator);
    }
    return true;
}

bool FilegroupMatcher::OnCloseSampColl(VGMSampColl *sampcoll) {
    auto iterator =
        std::find(sampcolls.begin(), sampcolls.end(), sampcoll);
    if (iterator != sampcolls.end()) {
        sampcolls.erase(iterator);
    }
    return true;
}

void FilegroupMatcher::LookForMatch() {
    if (instrsets.size() == 1 && sampcolls.size() == 1) {
        if (!seqs.empty()) {
            for (auto seq : seqs) {
                VGMInstrSet *instrset = instrsets.front();
                VGMSampColl *sampcoll = sampcolls.front();
                VGMColl *coll = fmt->NewCollection();
                coll->SetName(seq->GetName());
                coll->UseSeq(seq);
                coll->AddInstrSet(instrset);
                coll->AddSampColl(sampcoll);
                if (!coll->Load()) {
                    delete coll;
                }
            }
        } else {
            VGMInstrSet *instrset = instrsets.front();
            VGMSampColl *sampcoll = sampcolls.front();
            VGMColl *coll = fmt->NewCollection();
            coll->SetName(instrset->GetName());
            coll->UseSeq(nullptr);
            coll->AddInstrSet(instrset);
            coll->AddSampColl(sampcoll);
            if (!coll->Load()) {
                delete coll;
            }
        }
        seqs.clear();
        instrsets.clear();
        sampcolls.clear();
    }
}
