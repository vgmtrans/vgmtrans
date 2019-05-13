/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
 #include "pch.h"
#include "AkaoSnesInstr.h"
#include "SNESDSP.h"

// ****************
// AkaoSnesInstrSet
// ****************

AkaoSnesInstrSet::AkaoSnesInstrSet(RawFile *file,
                                   AkaoSnesVersion ver,
                                   uint32_t spcDirAddr,
                                   uint16_t addrTuningTable,
                                   uint16_t addrADSRTable,
                                   uint16_t addrDrumKitTable,
                                   const std::wstring &name) :
    VGMInstrSet(AkaoSnesFormat::name, file, addrTuningTable, 0, name), version(ver),
    spcDirAddr(spcDirAddr),
    addrTuningTable(addrTuningTable),
    addrADSRTable(addrADSRTable),
    addrDrumKitTable(addrDrumKitTable) {
}

AkaoSnesInstrSet::~AkaoSnesInstrSet() {
}

bool AkaoSnesInstrSet::GetHeaderInfo() {
  return true;
}

bool AkaoSnesInstrSet::GetInstrPointers() {
  usedSRCNs.clear();
  uint8_t srcn_max = (version == AKAOSNES_V1) ? 0x7f : 0x3f;
  for (uint8_t srcn = 0; srcn <= srcn_max; srcn++) {
    uint32_t addrDIRentry = spcDirAddr + (srcn * 4);
    if (!SNESSampColl::IsValidSampleDir(rawfile, addrDIRentry, true)) {
      continue;
    }

    uint16_t addrSampStart = GetShort(addrDIRentry);
    uint16_t instrumentMinOffset;
    
    if (version == AKAOSNES_V4) {
      // Some instruments can be placed before the DIR table. At least a few
      // songs from Chrono Trigger ("World Revolution", "Last Battle") will do
      // this.
      instrumentMinOffset = 0x200;
    }
    else {
      instrumentMinOffset = spcDirAddr;
    }
    if (addrSampStart < instrumentMinOffset) {
      continue;
    }

    uint32_t ofsTuningEntry;
    if (version == AKAOSNES_V1 || version == AKAOSNES_V2) {
      ofsTuningEntry = addrTuningTable + srcn;
    }
    else {
      ofsTuningEntry = addrTuningTable + srcn * 2;
    }

    if (ofsTuningEntry + 2 > 0x10000) {
      break;
    }

    if (version != AKAOSNES_V1) {
      uint32_t ofsADSREntry = addrADSRTable + srcn * 2;
      if (ofsADSREntry + 2 > 0x10000) {
        break;
      }

      if (GetShort(ofsADSREntry) == 0x0000) {
        break;
      }
    }

    usedSRCNs.push_back(srcn);

    std::wostringstream instrName;
    instrName << L"Instrument " << srcn;
    AkaoSnesInstr *newInstr = new AkaoSnesInstr(this, version, srcn, spcDirAddr, addrTuningTable, addrADSRTable, instrName.str());
    aInstrs.push_back(newInstr);
  }
  if (aInstrs.size() == 0) {
    return false;
  }

  if (addrDrumKitTable) {
    // One percussion instrument covers all percussion sounds
    AkaoSnesDrumKit *newDrumKitInstr = new AkaoSnesDrumKit(this, version, DRUMKIT_PROGRAM, spcDirAddr, addrTuningTable, addrADSRTable, addrDrumKitTable, L"Drum Kit");
    aInstrs.push_back(newDrumKitInstr);
  }

  std::sort(usedSRCNs.begin(), usedSRCNs.end());
  SNESSampColl *newSampColl = new SNESSampColl(AkaoSnesFormat::name, this->rawfile, spcDirAddr, usedSRCNs);
  if (!newSampColl->LoadVGMFile()) {
    delete newSampColl;
    return false;
  }

  return true;
}

// *************
// AkaoSnesInstr
// *************

AkaoSnesInstr::AkaoSnesInstr(VGMInstrSet *instrSet,
                             AkaoSnesVersion ver,
                             uint8_t srcn,
                             uint32_t spcDirAddr,
                             uint16_t addrTuningTable,
                             uint16_t addrADSRTable,
                             const std::wstring &name) :
    VGMInstr(instrSet, addrTuningTable, 0, 0, srcn, name), version(ver),
    spcDirAddr(spcDirAddr),
    addrTuningTable(addrTuningTable),
    addrADSRTable(addrADSRTable) {
}

AkaoSnesInstr::~AkaoSnesInstr() {
}

bool AkaoSnesInstr::LoadInstr() {
  uint32_t offDirEnt = spcDirAddr + (instrNum * 4);
  if (offDirEnt + 4 > 0x10000) {
    return false;
  }

  uint16_t addrSampStart = GetShort(offDirEnt);

  AkaoSnesRgn *rgn = new AkaoSnesRgn(this, version, addrTuningTable);
  rgn->sampOffset = addrSampStart - spcDirAddr;
  if (!rgn->InitializeRegion(instrNum, spcDirAddr, addrADSRTable) || !rgn->LoadRgn()) {
    delete rgn;
    return false;
  }
  aRgns.push_back(rgn);

  SetGuessedLength();
  return true;
}

// *************
// AkaoSnesDrumKit
// *************

AkaoSnesDrumKit::AkaoSnesDrumKit(VGMInstrSet *instrSet,
                                 AkaoSnesVersion ver,
                                 uint32_t programNum,
                                 uint32_t spcDirAddr,
                                 uint16_t addrTuningTable,
                                 uint16_t addrADSRTable,
                                 uint16_t addrDrumKitTable,
                                 const std::wstring &name) :
  VGMInstr(instrSet, addrTuningTable, 0, ((programNum >> 6) & 0x7F00) | ((programNum >> 7) & 0x7F), programNum & 0xFF, name), version(ver),
  spcDirAddr(spcDirAddr),
  addrTuningTable(addrTuningTable),
  addrADSRTable(addrADSRTable),
  addrDrumKitTable(addrDrumKitTable) {
}

AkaoSnesDrumKit::~AkaoSnesDrumKit() {
}

bool AkaoSnesDrumKit::LoadInstr() {
  uint32_t offDirEnt = spcDirAddr + (instrNum * 4);
  if (offDirEnt + 4 > 0x10000) {
    return false;
  }

  uint16_t addrSampStart = GetShort(offDirEnt);

  uint8_t NOTE_DUR_TABLE_SIZE;
  switch (version) {
  case AKAOSNES_V1:
  case AKAOSNES_V2:
  case AKAOSNES_V3:
    NOTE_DUR_TABLE_SIZE = 15;
    break;

  default:
    NOTE_DUR_TABLE_SIZE = 14;
    break;
  }

  // A new region for every instrument
  for (uint8_t i = 0; i < NOTE_DUR_TABLE_SIZE; ++i) {
    AkaoSnesDrumKitRgn *rgn = new AkaoSnesDrumKitRgn(this, version, addrTuningTable);

    if (!rgn->InitializePercussionRegion(i, spcDirAddr, addrADSRTable, addrDrumKitTable)) {
      delete rgn;
      continue;
    }
    if (!rgn->LoadRgn()) {
      delete rgn;
      return false;
    }

    uint32_t offDirEnt = spcDirAddr + (rgn->sampNum * 4);
    if (offDirEnt + 4 > 0x10000) {
      delete rgn;
      return false;
    }

    uint16_t addrSampStart = GetShort(offDirEnt);

    rgn->sampOffset = addrSampStart - spcDirAddr;

    aRgns.push_back(rgn);
  }

  SetGuessedLength();
  return true;
}

// ***********
// AkaoSnesRgn
// ***********

AkaoSnesRgn::AkaoSnesRgn(VGMInstr *instr,
                         AkaoSnesVersion ver,
                         uint16_t addrTuningTable) :
    VGMRgn(instr, addrTuningTable, 0),
    version(ver) {
}

bool AkaoSnesRgn::InitializeRegion(uint8_t srcn,
                                   uint32_t spcDirAddr,
                                   uint16_t addrADSRTable)
{
  uint16_t addrTuningTable = dwOffset;
  uint8_t adsr1;
  uint8_t adsr2;
  if (version == AKAOSNES_V1) {
    adsr1 = 0xff;
    adsr2 = 0xe0;
  }
  else {
    adsr1 = GetByte(addrADSRTable + srcn * 2);
    adsr2 = GetByte(addrADSRTable + srcn * 2 + 1);

    AddSimpleItem(addrADSRTable + srcn * 2, 1, L"ADSR1");
    AddSimpleItem(addrADSRTable + srcn * 2 + 1, 1, L"ADSR2");
  }

  uint8_t tuning1;
  uint8_t tuning2;
  if (version == AKAOSNES_V1 || version == AKAOSNES_V2) {
    tuning1 = GetByte(addrTuningTable + srcn);
    tuning2 = 0;

    AddSimpleItem(addrTuningTable + srcn, 1, L"Tuning");
  }
  else {
    tuning1 = GetByte(addrTuningTable + srcn * 2);
    tuning2 = GetByte(addrTuningTable + srcn * 2 + 1);

    AddSimpleItem(addrTuningTable + srcn * 2, 2, L"Tuning");
  }

  double pitch_scale;
  if (tuning1 <= 0x7f) {
    pitch_scale = 1.0 + (tuning1 / 256.0);
  }
  else {
    pitch_scale = tuning1 / 256.0;
  }
  pitch_scale += tuning2 / 65536.0;

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

  sampNum = srcn;
  unityKey = 69 - (int) (coarse_tuning);
  fineTune = (int16_t) (fine_tuning * 100.0);
  SNESConvADSR<VGMRgn>(this, adsr1, adsr2, 0xa0);

  return true;
}

AkaoSnesRgn::~AkaoSnesRgn() {
}

bool AkaoSnesRgn::LoadRgn() {
  SetGuessedLength();

  return true;
}

// ***********
// AkaoSnesDrumKitRgn
// ***********

AkaoSnesDrumKitRgn::AkaoSnesDrumKitRgn(AkaoSnesDrumKit *instr,
                                       AkaoSnesVersion ver,
                                       uint16_t addrTuningTable) :
  AkaoSnesRgn(instr, ver, addrTuningTable) {
}

bool AkaoSnesDrumKitRgn::InitializePercussionRegion(uint8_t percussionIndex,
                                                       uint32_t spcDirAddr,
                                                       uint16_t addrADSRTable,
                                                       uint16_t addrDrumKitTable)
{
  wostringstream newName;
  
  newName << L"Drum " << percussionIndex;
  name = newName.str();

  uint32_t srcnOffset = addrDrumKitTable + percussionIndex * 3;
  uint32_t keyOffset = srcnOffset + 1;
  uint32_t panOffset = srcnOffset + 2;

  uint8_t instrumentIndex = GetByte(srcnOffset);

  if (instrumentIndex == 0 || instrumentIndex == 0xFF || !InitializeRegion(instrumentIndex, spcDirAddr, addrADSRTable)) {
    return false;
  }

  keyLow = keyHigh = percussionIndex + KEY_BIAS;

  // sampNum is absolute key to play
  unityKey = (unityKey + KEY_BIAS) - GetByte(keyOffset) + percussionIndex;
  
  AddSampNum(sampNum, srcnOffset);
  AddSimpleItem(keyOffset, 1, L"Key");

  uint8_t panValue = GetByte(panOffset);
  if (panValue < 0x80) {
    AddPan(panValue, panOffset);
  }

  return true;
}

AkaoSnesDrumKitRgn::~AkaoSnesDrumKitRgn() {
}
