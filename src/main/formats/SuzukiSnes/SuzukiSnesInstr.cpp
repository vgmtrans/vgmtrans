/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "SuzukiSnesInstr.h"
#include "SNESDSP.h"
#include <spdlog/fmt/fmt.h>

// ******************
// SuzukiSnesInstrSet
// ******************

SuzukiSnesInstrSet::SuzukiSnesInstrSet(RawFile *file,
                                       SuzukiSnesVersion ver,
                                       uint32_t spcDirAddr,
                                       uint16_t addrSRCNTable,
                                       uint16_t addrVolumeTable,
                                       uint16_t addrADSRTable,
                                       uint16_t addrTuningTable,
                                       const std::string &name) :
    VGMInstrSet(SuzukiSnesFormat::name, file, addrSRCNTable, 0, name), version(ver),
    spcDirAddr(spcDirAddr),
    addrSRCNTable(addrSRCNTable),
    addrVolumeTable(addrVolumeTable),
    addrADSRTable(addrADSRTable),
    addrTuningTable(addrTuningTable) {
}

SuzukiSnesInstrSet::~SuzukiSnesInstrSet() {}

bool SuzukiSnesInstrSet::parseHeader() {
  return true;
}

bool SuzukiSnesInstrSet::parseInstrPointers() {
  usedSRCNs.clear();
  for (uint8_t instrNum = 0; instrNum <= 0x7f; instrNum++) {
    uint32_t ofsSRCNEntry = addrSRCNTable + instrNum;
    if (ofsSRCNEntry + 1 > 0x10000) {
      continue;
    }
    uint8_t srcn = readByte(ofsSRCNEntry);
    if (srcn >= 0x40) {
      continue;
    }

    uint32_t addrDIRentry = spcDirAddr + (srcn * 4);
    if (!SNESSampColl::isValidSampleDir(rawFile(), addrDIRentry, true)) {
      continue;
    }

    uint16_t addrSampStart = readShort(addrDIRentry);
    if (addrSampStart < spcDirAddr) {
      continue;
    }

    uint32_t ofsVolumeEntry = addrVolumeTable + srcn * 2;
    if (ofsVolumeEntry + 1 > 0x10000) {
      break;
    }

    uint32_t ofsADSREntry = addrADSRTable + srcn * 2;
    if (ofsADSREntry + 2 > 0x10000) {
      break;
    }

    if (readShort(ofsADSREntry) == 0x0000) {
      break;
    }

    uint32_t ofsTuningEntry = addrTuningTable + srcn * 2;
    if (ofsTuningEntry + 2 > 0x10000) {
      break;
    }

    if (readShort(ofsTuningEntry) == 0xffff) {
      continue;
    }

    usedSRCNs.push_back(srcn);

    SuzukiSnesInstr *newInstr = new SuzukiSnesInstr(
      this, version, instrNum, spcDirAddr, addrSRCNTable, addrVolumeTable, addrADSRTable,
      addrTuningTable, fmt::format("Instrument: {:#x}", srcn));
    aInstrs.push_back(newInstr);
  }
  if (aInstrs.size() == 0) {
    return false;
  }

  std::sort(usedSRCNs.begin(), usedSRCNs.end());
  SNESSampColl *newSampColl = new SNESSampColl(SuzukiSnesFormat::name, this->rawFile(), spcDirAddr, usedSRCNs);
  if (!newSampColl->loadVGMFile()) {
    delete newSampColl;
    return false;
  }

  return true;
}

// ***************
// SuzukiSnesInstr
// ***************

SuzukiSnesInstr::SuzukiSnesInstr(VGMInstrSet *instrSet,
                                 SuzukiSnesVersion ver,
                                 uint8_t instrNum,
                                 uint32_t spcDirAddr,
                                 uint16_t addrSRCNTable,
                                 uint16_t addrVolumeTable,
                                 uint16_t addrADSRTable,
                                 uint16_t addrTuningTable,
                                 const std::string &name) :
    VGMInstr(instrSet, addrSRCNTable, 0, 0, instrNum, name), version(ver),
    spcDirAddr(spcDirAddr),
    addrSRCNTable(addrSRCNTable),
    addrVolumeTable(addrVolumeTable),
    addrADSRTable(addrADSRTable),
    addrTuningTable(addrTuningTable) {
}

SuzukiSnesInstr::~SuzukiSnesInstr() {
}

bool SuzukiSnesInstr::loadInstr() {
  uint32_t ofsADSREntry = addrSRCNTable + instrNum;
  if (ofsADSREntry + 1 > 0x10000) {
    return false;
  }
  uint8_t srcn = readByte(ofsADSREntry);

  uint32_t offDirEnt = spcDirAddr + (srcn * 4);
  if (offDirEnt + 4 > 0x10000) {
    return false;
  }

  uint16_t addrSampStart = readShort(offDirEnt);

  SuzukiSnesRgn *rgn = new SuzukiSnesRgn(this,
                                         version,
                                         instrNum,
                                         spcDirAddr,
                                         addrSRCNTable,
                                         addrVolumeTable,
                                         addrADSRTable,
                                         addrTuningTable);
  rgn->sampOffset = addrSampStart - spcDirAddr;
  addRgn(rgn);

  setGuessedLength();
  return true;
}

// *************
// SuzukiSnesRgn
// *************

SuzukiSnesRgn::SuzukiSnesRgn(SuzukiSnesInstr *instr,
                             SuzukiSnesVersion ver,
                             uint8_t instrNum,
                             uint32_t spcDirAddr,
                             uint16_t addrSRCNTable,
                             uint16_t addrVolumeTable,
                             uint16_t addrADSRTable,
                             uint16_t addrTuningTable) :
    VGMRgn(instr, addrSRCNTable, 0),
    version(ver) {
  uint8_t srcn = readByte(addrSRCNTable + instrNum);
  uint8_t vol = readByte(addrVolumeTable + srcn * 2);
  uint8_t adsr1 = readByte(addrADSRTable + srcn * 2);
  uint8_t adsr2 = readByte(addrADSRTable + srcn * 2 + 1);
  uint8_t fine_tuning = readByte(addrTuningTable + srcn * 2);
  int8_t coarse_tuning = readByte(addrTuningTable + srcn * 2 + 1);

  addSampNum(srcn, addrSRCNTable + instrNum, 1);
  addChild(addrADSRTable + srcn * 2, 1, "ADSR1");
  addChild(addrADSRTable + srcn * 2 + 1, 1, "ADSR2");
  addFineTune((int16_t) (fine_tuning / 256.0 * 100.0), addrTuningTable + srcn * 2, 1);
  addUnityKey(69 - coarse_tuning, addrTuningTable + srcn * 2 + 1, 1);
  addVolume(vol / 256.0, addrVolumeTable + srcn * 2, 1);
  snesConvADSR<VGMRgn>(this, adsr1, adsr2, 0);

  setGuessedLength();
}

SuzukiSnesRgn::~SuzukiSnesRgn() {}

bool SuzukiSnesRgn::loadRgn() {
  return true;
}
