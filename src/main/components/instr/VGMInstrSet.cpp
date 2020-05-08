/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "VGMInstrSet.h"
#include "VGMSampColl.h"
#include "VGMRgn.h"
#include "VGMColl.h"
#include "Root.h"
#include "Format.h"

#include <fmt/format.h>

#include <utility>

// ***********
// VGMInstrSet
// ***********

VGMInstrSet::VGMInstrSet(const std::string &format,
                         RawFile *file, uint32_t offset, uint32_t length, std::string name,
                         VGMSampColl *theSampColl)
    : VGMFile(format, file, offset, length, std::move(name)),
      sampColl(theSampColl) {
    AddContainer<VGMInstr>(aInstrs);
}

VGMInstrSet::~VGMInstrSet() {
    DeleteVect<VGMInstr>(aInstrs);
    delete sampColl;
}

VGMInstr *VGMInstrSet::AddInstr(uint32_t offset, uint32_t length, unsigned long bank,
                                unsigned long instrNum, const std::string &instrName) {
    VGMInstr *instr =
        new VGMInstr(this, offset, length, bank, instrNum,
                     instrName.empty() ? fmt::format("Instrument {}", aInstrs.size()) : instrName);
    aInstrs.push_back(instr);
    return instr;
}

bool VGMInstrSet::LoadVGMFile() {
    bool val = Load();
    if (!val) {
        return false;
    }

    if (auto fmt = GetFormat(); fmt) {
        fmt->OnNewFile(std::variant<VGMSeq *, VGMInstrSet *, VGMSampColl *, VGMMiscFile *>(this));
    }

    return val;
}


bool VGMInstrSet::Load() {
    if (!GetHeaderInfo())
        return false;
    if (!GetInstrPointers())
        return false;
    if (!LoadInstrs())
        return false;

    if (aInstrs.size() == 0)
        return false;

    if (unLength == 0) {
        SetGuessedLength();
    }

    if (sampColl != NULL) {
        if (!sampColl->Load()) {
            L_WARN("Failed to load VGMSampColl");
        }
    }

    rawfile->AddContainedVGMFile(std::make_shared<std::variant<VGMSeq *, VGMInstrSet *, VGMSampColl *, VGMMiscFile *>>(this));
    pRoot->AddVGMFile(this);
    return true;
}

bool VGMInstrSet::GetHeaderInfo() {
    return true;
}

bool VGMInstrSet::GetInstrPointers() {
    return true;
}

bool VGMInstrSet::LoadInstrs() {
    size_t nInstrs = aInstrs.size();
    for (size_t i = 0; i < nInstrs; i++) {
        if (!aInstrs[i]->LoadInstr())
            return false;
    }
    return true;
}

namespace conversion {
bool SaveAsDLS(const VGMInstrSet &set, const std::string &filepath) {
    DLSFile dlsfile;
    bool dlsCreationSucceeded = false;

    if (set.assocColls.empty()) {
        return false;
    }

    dlsCreationSucceeded = set.assocColls.front()->CreateDLSFile(dlsfile);

    if (dlsCreationSucceeded) {
        return dlsfile.SaveDLSFile(filepath);
    }
    return false;
}

bool SaveAsSF2(const VGMInstrSet &set, const std::string &filepath) {
    if (set.assocColls.empty()) {
        return false;
    }

    if (auto sf2file = set.assocColls.front()->CreateSF2File(); sf2file) {
        bool bResult = sf2file->SaveSF2File(filepath);
        delete sf2file;
        return bResult;
    }

    return false;
}
}  // namespace conversion

// ********
// VGMInstr
// ********

VGMInstr::VGMInstr(VGMInstrSet *instrSet, uint32_t offset, uint32_t length, uint32_t theBank,
                   uint32_t theInstrNum, const std::string &name)
    : VGMContainerItem(instrSet, offset, length, name),
      parInstrSet(instrSet),
      bank(theBank),
      instrNum(theInstrNum) {
    AddContainer<VGMRgn>(aRgns);
}

VGMInstr::~VGMInstr() {
    DeleteVect<VGMRgn>(aRgns);
}

void VGMInstr::SetBank(uint32_t bankNum) {
    bank = bankNum;
}

void VGMInstr::SetInstrNum(uint32_t theInstrNum) {
    instrNum = theInstrNum;
}

VGMRgn *VGMInstr::AddRgn(VGMRgn *rgn) {
    aRgns.push_back(rgn);
    return rgn;
}

VGMRgn *VGMInstr::AddRgn(uint32_t offset, uint32_t length, int sampNum, uint8_t keyLow,
                         uint8_t keyHigh, uint8_t velLow, uint8_t velHigh) {
    VGMRgn *newRgn = new VGMRgn(this, offset, length, keyLow, keyHigh, velLow, velHigh, sampNum);
    aRgns.push_back(newRgn);
    return newRgn;
}

bool VGMInstr::LoadInstr() {
    return true;
}
