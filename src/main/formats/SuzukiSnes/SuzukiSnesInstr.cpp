/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "SuzukiSnesInstr.h"
#include "SuzukiSnesSeq.h"
#include "SNESDSP.h"
#include "VGMColl.h"
#include <spdlog/fmt/fmt.h>

namespace {
constexpr uint8_t kSuzukiSnesSrcnCount = 0x40;
constexpr uint16_t kSuzukiSnesSd3DrumKitOffset = 16;
}

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

bool SuzukiSnesInstrSet::parseHeader() {
  return true;
}

bool SuzukiSnesInstrSet::parseInstrPointers() {
  for (uint8_t instrNum = 0; instrNum <= 0x7f; instrNum++) {
    uint32_t ofsSRCNEntry = addrSRCNTable + instrNum;
    if (ofsSRCNEntry + 1 > 0x10000) {
      continue;
    }
    uint8_t srcn = readByte(ofsSRCNEntry);
    if (srcn >= kSuzukiSnesSrcnCount) {
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

    SuzukiSnesInstr *newInstr = new SuzukiSnesInstr(
      this, version, instrNum, spcDirAddr, addrSRCNTable, addrVolumeTable, addrADSRTable,
      addrTuningTable, fmt::format("Instrument: {:#x}", srcn));
    aInstrs.push_back(newInstr);
  }
  if (aInstrs.size() == 0) {
    return false;
  }

  // Load all valid Suzuki sample slots so conversion-time drumkits can reuse the base sample collection.
  SNESSampColl *newSampColl = new SNESSampColl(
      SuzukiSnesFormat::name, this->rawFile(), spcDirAddr, kSuzukiSnesSrcnCount);
  if (!newSampColl->loadVGMFile()) {
    delete newSampColl;
    return false;
  }

  return true;
}

void SuzukiSnesInstrSet::useColl(const VGMColl* coll) {
  if (coll->seq() == nullptr) {
    return;
  }

  const auto* seq = dynamic_cast<const SuzukiSnesSeq*>(coll->seq());
  if (seq == nullptr || seq->rawFile() != rawFile() || seq->version != version) {
    return;
  }

  uint16_t addrDrumKitTable = static_cast<uint16_t>(seq->offset());
  if (version == SUZUKISNES_SD3) {
    addrDrumKitTable += kSuzukiSnesSd3DrumKitOffset;
  }
  if (readByte(addrDrumKitTable) >= 0x80) {
    return;
  }

  auto* drumKit = new SuzukiSnesDrumKit(this, version, DRUMKIT_PROGRAM, spcDirAddr, addrSRCNTable,
                                        addrTuningTable, addrADSRTable, addrDrumKitTable, "Drum Kit");
  if (!drumKit->loadInstr() || drumKit->regions().empty()) {
    delete drumKit;
    return;
  }

  addTempInstr(drumKit);
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

  SuzukiSnesRgn *rgn = new SuzukiSnesRgn(this, version, addrSRCNTable);
  rgn->sampOffset = addrSampStart - spcDirAddr;
  rgn->initializeNonPercussionRegion(instrNum, addrVolumeTable);
  rgn->initializeCommonRegion(srcn, spcDirAddr, addrADSRTable, addrTuningTable);
  rgn->loadRgn();
  addRgn(rgn);

  setGuessedLength();
  return true;
}

// *****************
// SuzukiSnesDrumKit
// *****************

SuzukiSnesDrumKit::SuzukiSnesDrumKit(VGMInstrSet *instrSet,
                                     SuzukiSnesVersion ver,
                                     uint32_t programNum,
                                     uint32_t spcDirAddr,
                                     uint16_t addrSRCNTable,
                                     uint16_t addrTuningTable,
                                     uint16_t addrADSRTable,
                                     uint16_t addrDrumKitTable,
                                     const std::string &name) :
  VGMInstr(instrSet, addrDrumKitTable, 0, programNum >> 7, programNum & 0x7F, name), version(ver),
  spcDirAddr(spcDirAddr),
  addrSRCNTable(addrSRCNTable),
  addrTuningTable(addrTuningTable),
  addrADSRTable(addrADSRTable),
  addrDrumKitTable(addrDrumKitTable) {
}

bool SuzukiSnesDrumKit::loadInstr() {
  uint16_t addr = addrDrumKitTable;

  for (uint16_t i = addr; readByte(i) < 0x80; i += 5) {
    SuzukiSnesDrumKitRgn *rgn = new SuzukiSnesDrumKitRgn(this, version, addrDrumKitTable);

    if (!rgn->initializePercussionRegion(i, spcDirAddr, addrSRCNTable, addrADSRTable, addrTuningTable)) {
      delete rgn;
      continue;
    }
    if (!rgn->loadRgn()) {
      delete rgn;
      return false;
    }

    uint32_t rgnOffDirEnt = spcDirAddr + (rgn->sampNum * 4);
    if (rgnOffDirEnt + 4 > 0x10000) {
      delete rgn;
      return false;
    }

    uint16_t addrSampStart = readShort(rgnOffDirEnt);

    rgn->sampOffset = addrSampStart - spcDirAddr;

    addRgn(rgn);
  }

  setGuessedLength();
  return true;
}


// *************
// SuzukiSnesRgn
// *************

SuzukiSnesRgn::SuzukiSnesRgn(VGMInstr *instr,
                             SuzukiSnesVersion ver,
                             uint16_t addrSRCNTable) :
    VGMRgn(instr, addrSRCNTable, 0),
    version(ver) {
}

bool SuzukiSnesRgn::initializeNonPercussionRegion(uint8_t instrNum, uint16_t addrVolumeTable) {
  uint16_t addrSRCNTable = offset();
  uint8_t srcn = readByte(addrSRCNTable + instrNum);
  uint8_t vol = readByte(addrVolumeTable + srcn * 2);
  addSampNum(srcn, addrSRCNTable + instrNum, 1);
  addVolume(vol / 256.0, addrVolumeTable + srcn * 2, 1);
  return true;
}

bool SuzukiSnesRgn::initializeCommonRegion(uint8_t srcn,
                                           uint32_t /*spcDirAddr*/,
                                           uint16_t addrADSRTable,
                                           uint16_t addrTuningTable) {
  uint8_t adsr1 = readByte(addrADSRTable + srcn * 2);
  uint8_t adsr2 = readByte(addrADSRTable + srcn * 2 + 1);
  uint8_t fine_tuning = readByte(addrTuningTable + srcn * 2);
  int8_t coarse_tuning = readByte(addrTuningTable + srcn * 2 + 1);

  addChild(addrADSRTable + srcn * 2, 1, "ADSR1");
  addChild(addrADSRTable + srcn * 2 + 1, 1, "ADSR2");
  addFineTune((int16_t) (fine_tuning / 256.0 * 100.0), addrTuningTable + srcn * 2, 1);
  addUnityKey(69 - coarse_tuning, addrTuningTable + srcn * 2 + 1, 1);
  snesConvADSR<VGMRgn>(this, adsr1, adsr2, 0);

  return true;
}


SuzukiSnesRgn::~SuzukiSnesRgn() {}

bool SuzukiSnesRgn::loadRgn() {
  setGuessedLength();

  return true;
}

// ********************
// SuzukiSnesDrumKitRgn
// ********************

SuzukiSnesDrumKitRgn::SuzukiSnesDrumKitRgn(SuzukiSnesDrumKit *instr,
                                           SuzukiSnesVersion ver,
                                           uint16_t addrDrumKitTable) :
  SuzukiSnesRgn(instr, ver, addrDrumKitTable) {}

bool SuzukiSnesDrumKitRgn::initializePercussionRegion(uint16_t noteOffset,
                                                      uint32_t spcDirAddr,
                                                      uint16_t addrSRCNTable,
                                                      uint16_t addrADSRTable,
                                                      uint16_t addrTuningTable)
{
  uint16_t instrOffset = noteOffset + 1;
  uint16_t keyOffset = noteOffset + 2;
  uint16_t volumeOffset = noteOffset + 3;
  uint16_t panOffset = noteOffset + 4;

  uint8_t instrNum = readByte(instrOffset);
  addChild(instrOffset, 1, "Instrument");

  uint8_t srcn = readByte(addrSRCNTable + instrNum);
  initializeCommonRegion(srcn, spcDirAddr, addrADSRTable, addrTuningTable);

  uint8_t percussionIndex = readByte(noteOffset);
  setName(fmt::format("Drum {}", percussionIndex));
  keyLow = keyHigh = percussionIndex + KEY_BIAS;

  // sampNum is absolute key to play
  unityKey = (unityKey + KEY_BIAS) - readByte(keyOffset) + percussionIndex;

  addSampNum(srcn, addrSRCNTable + instrNum, 1);
  addChild(keyOffset, 1, "Key");

  addVolume(readByte(volumeOffset) / 256.0, volumeOffset, 1);

  uint8_t panValue = readByte(panOffset);
  if (panValue < 0x80) {
    addPan(panValue, panOffset);
  }

  return true;
}

SuzukiSnesDrumKitRgn::~SuzukiSnesDrumKitRgn() {}
