#include "pch.h"
#include "HudsonSnesInstr.h"
#include "SNESDSP.h"

// ******************
// HudsonSnesInstrSet
// ******************

HudsonSnesInstrSet::HudsonSnesInstrSet(RawFile *file,
                                       HudsonSnesVersion ver,
                                       uint32_t offset,
                                       uint32_t length,
                                       uint32_t spcDirAddr,
                                       uint32_t addrSampTuningTable,
                                       const std::wstring &name) :
    VGMInstrSet(HudsonSnesFormat::name, file, offset, length, name), version(ver),
    spcDirAddr(spcDirAddr),
    addrSampTuningTable(addrSampTuningTable) {
}

HudsonSnesInstrSet::~HudsonSnesInstrSet() {
}

bool HudsonSnesInstrSet::GetHeaderInfo() {
  return true;
}

bool HudsonSnesInstrSet::GetInstrPointers() {
  usedSRCNs.clear();
  for (uint8_t instrNum = 0; instrNum < unLength / 4; instrNum++) {
    uint32_t ofsInstrEntry = dwOffset + (instrNum * 4);
    if (ofsInstrEntry + 4 > 0x10000) {
      break;
    }
    uint8_t srcn = GetByte(ofsInstrEntry);

    uint32_t addrDIRentry = spcDirAddr + (srcn * 4);
    if (!SNESSampColl::IsValidSampleDir(rawfile, addrDIRentry, true)) {
      continue;
    }

    uint32_t ofsTuningEnt = addrSampTuningTable + srcn * 4;
    if (ofsTuningEnt + 4 > 0x10000) {
      continue;
    }

    usedSRCNs.push_back(srcn);

    std::wostringstream instrName;
    instrName << L"Instrument " << instrNum;
    HudsonSnesInstr *newInstr =
        new HudsonSnesInstr(this, version, ofsInstrEntry, instrNum, spcDirAddr, addrSampTuningTable, instrName.str());
    aInstrs.push_back(newInstr);
  }
  if (aInstrs.size() == 0) {
    return false;
  }

  std::sort(usedSRCNs.begin(), usedSRCNs.end());
  SNESSampColl *newSampColl = new SNESSampColl(HudsonSnesFormat::name, this->rawfile, spcDirAddr, usedSRCNs);
  if (!newSampColl->LoadVGMFile()) {
    delete newSampColl;
    return false;
  }

  return true;
}

// ***************
// HudsonSnesInstr
// ***************

HudsonSnesInstr::HudsonSnesInstr(VGMInstrSet *instrSet,
                                 HudsonSnesVersion ver,
                                 uint32_t offset,
                                 uint8_t instrNum,
                                 uint32_t spcDirAddr,
                                 uint32_t addrSampTuningTable,
                                 const std::wstring &name) :
    VGMInstr(instrSet, offset, 4, 0, instrNum, name), version(ver),
    spcDirAddr(spcDirAddr),
    addrSampTuningTable(addrSampTuningTable) {
}

HudsonSnesInstr::~HudsonSnesInstr() {
}

bool HudsonSnesInstr::LoadInstr() {
  uint8_t srcn = GetByte(dwOffset);

  uint32_t offDirEnt = spcDirAddr + (srcn * 4);
  if (offDirEnt + 4 > 0x10000) {
    return false;
  }

  uint16_t addrSampStart = GetShort(offDirEnt);

  uint32_t ofsTuningEnt = addrSampTuningTable + srcn * 4;
  if (ofsTuningEnt + 4 > 0x10000) {
    return false;
  }

  HudsonSnesRgn *rgn = new HudsonSnesRgn(this, version, dwOffset, spcDirAddr, ofsTuningEnt);
  rgn->sampOffset = addrSampStart - spcDirAddr;
  aRgns.push_back(rgn);

  return true;
}

// *************
// HudsonSnesRgn
// *************

HudsonSnesRgn::HudsonSnesRgn(HudsonSnesInstr *instr,
                             HudsonSnesVersion ver,
                             uint32_t offset,
                             uint32_t spcDirAddr,
                             uint32_t addrTuningEntry) :
    VGMRgn(instr, offset, 4),
    version(ver) {
  uint8_t srcn = GetByte(dwOffset);
  uint8_t adsr1 = GetByte(dwOffset + 1);
  uint8_t adsr2 = GetByte(dwOffset + 2);
  uint8_t gain = GetByte(dwOffset + 3);

  AddSimpleItem(dwOffset, 1, L"SRCN");
  AddSimpleItem(dwOffset + 1, 1, L"ADSR(1)");
  AddSimpleItem(dwOffset + 2, 1, L"ADSR(2)");
  AddSimpleItem(dwOffset + 3, 1, L"GAIN");

  double pitch_scale = GetShortBE(addrTuningEntry) / 256.0;
  double fine_tuning;
  double coarse_tuning;
  fine_tuning = modf((log(pitch_scale) / log(2.0)) * 12.0, &coarse_tuning);

  // normalize
  if (fine_tuning >= 0.5) {
    coarse_tuning += 1.0;
    fine_tuning -= 1.0;
  }
  else if (fine_tuning <= -0.5) {
    coarse_tuning -= 1.0;
    fine_tuning += 1.0;
  }

  int8_t coarse_tuning_byte = GetByte(addrTuningEntry + 2);
  int8_t fine_tuning_byte = GetByte(addrTuningEntry + 3);

  unityKey = 71 - (int) (coarse_tuning) - coarse_tuning_byte;
  fineTune = (int16_t) (fine_tuning + (fine_tuning_byte / 256.0) * 100.0);

  AddSimpleItem(addrTuningEntry, 2, L"Pitch Multiplier");
  AddSimpleItem(addrTuningEntry + 2, 1, L"Coarse Tune");
  AddSimpleItem(addrTuningEntry + 3, 1, L"Fine Tune");
  SNESConvADSR<VGMRgn>(this, adsr1, adsr2, gain);
}

HudsonSnesRgn::~HudsonSnesRgn() {
}

bool HudsonSnesRgn::LoadRgn() {
  return true;
}
