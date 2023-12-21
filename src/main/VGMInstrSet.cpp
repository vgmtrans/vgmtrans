#include "pch.h"
#include "VGMInstrSet.h"
#include "VGMSampColl.h"
#include "VGMRgn.h"
#include "VGMColl.h"
#include "Root.h"

DECLARE_MENU(VGMInstrSet)

using namespace std;

// ***********
// VGMInstrSet
// ***********

VGMInstrSet::VGMInstrSet(const string &format,/*FmtID fmtID,*/
                         RawFile *file,
                         uint32_t offset,
                         uint32_t length,
                         string name,
                         VGMSampColl *theSampColl)
    : VGMFile(FILETYPE_INSTRSET, /*fmtID,*/format, file, offset, length, name), sampColl(theSampColl), allowEmptyInstrs(false) {
  AddContainer<VGMInstr>(aInstrs);
}

VGMInstrSet::~VGMInstrSet() {
  DeleteVect<VGMInstr>(aInstrs);
  delete sampColl;
}


VGMInstr *VGMInstrSet::AddInstr(uint32_t offset, uint32_t length, unsigned long bank,
                                unsigned long instrNum, const string &instrName) {
  ostringstream name;
  if (instrName == "")
    name << "Instrument " << aInstrs.size();
  else
    name << instrName;

  VGMInstr *instr = new VGMInstr(this, offset, length, bank, instrNum, name.str());
  aInstrs.push_back(instr);
  return instr;
}

bool VGMInstrSet::Load() {
  if (!GetHeaderInfo())
    return false;
  if (!GetInstrPointers())
    return false;
  if (!LoadInstrs())
    return false;

  if (!allowEmptyInstrs && aInstrs.empty())
    return false;

  if (unLength == 0) {
    SetGuessedLength();
  }

  if (sampColl != NULL) {
    if (!sampColl->Load()) {
      pRoot->AddLogItem(new LogItem("Failed to load VGMSampColl.", LOG_LEVEL_ERR, "VGMInstrSet"));
    }
  }

  LoadLocalData();
  UseLocalData();
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


bool VGMInstrSet::OnSaveAsDLS(void) {
  string filepath = pRoot->UI_GetSaveFilePath(ConvertToSafeFileName(name), "dls");
  if (filepath.length() != 0) {
    return SaveAsDLS(filepath.c_str());
  }
  return false;
}

bool VGMInstrSet::OnSaveAsSF2(void) {
  string filepath = pRoot->UI_GetSaveFilePath(ConvertToSafeFileName(name), "sf2");
  if (filepath.length() != 0) {
    return SaveAsSF2(filepath);
  }
  return false;
}


bool VGMInstrSet::SaveAsDLS(const std::string &filepath) {
  DLSFile dlsfile;
  bool dlsCreationSucceeded = false;

  if (!assocColls.empty()) {
    dlsCreationSucceeded = assocColls.front()->CreateDLSFile(dlsfile);
  } else {
    std::ostringstream message;
    message << name
            << ": "
               "Instrument sets that are not part of a collection cannot be "
               "converted to DLS at the moment.";
    pRoot->AddLogItem(new LogItem(message.str(), LOG_LEVEL_ERR, "VGMInstrSet"));
    return false;
  }

  if (dlsCreationSucceeded) {
    return dlsfile.SaveDLSFile(filepath);
  }
  return false;
}

bool VGMInstrSet::SaveAsSF2(const std::string &filepath) {
  SF2File *sf2file = NULL;

  if (!assocColls.empty()) {
    sf2file = assocColls.front()->CreateSF2File();
  } else {
    std::ostringstream message;
    message << name
            << ": "
               "Instrument sets that are not part of a collection cannot be "
               "converted to SF2 at the moment.";
    pRoot->AddLogItem(new LogItem(message.str(), LOG_LEVEL_ERR, "VGMInstrSet"));
    return false;
  }

  if (sf2file != NULL) {
    bool bResult = sf2file->SaveSF2File(filepath);
    delete sf2file;
    return bResult;
  }
  return false;
}


// ********
// VGMInstr
// ********

VGMInstr::VGMInstr(VGMInstrSet *instrSet, uint32_t offset, uint32_t length, uint32_t theBank,
                   uint32_t theInstrNum, const string &name, float reverb)
    : VGMContainerItem(instrSet, offset, length, name),
      parInstrSet(instrSet),
      bank(theBank),
      reverb(reverb),
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

VGMRgn *VGMInstr::AddRgn(uint32_t offset, uint32_t length, int sampNum, uint8_t keyLow, uint8_t keyHigh,
                         uint8_t velLow, uint8_t velHigh) {
  VGMRgn *newRgn = new VGMRgn(this, offset, length, keyLow, keyHigh, velLow, velHigh, sampNum);
  aRgns.push_back(newRgn);
  return newRgn;
}


bool VGMInstr::LoadInstr() {
  return true;
}

