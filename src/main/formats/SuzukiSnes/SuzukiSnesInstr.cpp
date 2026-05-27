/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "Types.h"
#include "SuzukiSnesInstr.h"
#include "SuzukiSnesSeq.h"
#include "SNESDSP.h"
#include "VGMColl.h"
#include <spdlog/fmt/fmt.h>

namespace {
constexpr u8 kSuzukiSnesSrcnCount = 0x40;
constexpr u16 kSuzukiSnesSd3DrumKitOffset = 16;
}

// ******************
// SuzukiSnesInstrSet
// ******************

SuzukiSnesInstrSet::SuzukiSnesInstrSet(RawFile *file,
                                       SuzukiSnesVersion ver,
                                       u32 spcDirAddr,
                                       u16 addrSRCNTable,
                                       u16 addrVolumeTable,
                                       u16 addrADSRTable,
                                       u16 addrTuningTable,
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
  for (u8 instrNum = 0; instrNum <= 0x7f; instrNum++) {
    u32 ofsSRCNEntry = addrSRCNTable + instrNum;
    if (ofsSRCNEntry + 1 > 0x10000) {
      continue;
    }
    u8 srcn = readByte(ofsSRCNEntry);
    if (srcn >= kSuzukiSnesSrcnCount) {
      continue;
    }

    u32 addrDIRentry = spcDirAddr + (srcn * 4);
    if (!SNESSampColl::isValidSampleDir(rawFile(), addrDIRentry, true)) {
      continue;
    }

    u16 addrSampStart = readShort(addrDIRentry);
    if (addrSampStart < spcDirAddr) {
      continue;
    }

    u32 ofsVolumeEntry = addrVolumeTable + srcn * 2;
    if (ofsVolumeEntry + 1 > 0x10000) {
      break;
    }

    u32 ofsADSREntry = addrADSRTable + srcn * 2;
    if (ofsADSREntry + 2 > 0x10000) {
      break;
    }

    if (readShort(ofsADSREntry) == 0x0000) {
      break;
    }

    u32 ofsTuningEntry = addrTuningTable + srcn * 2;
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

  u16 addrDrumKitTable = static_cast<u16>(seq->offset());
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
                                 u8 instrNum,
                                 u32 spcDirAddr,
                                 u16 addrSRCNTable,
                                 u16 addrVolumeTable,
                                 u16 addrADSRTable,
                                 u16 addrTuningTable,
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
  u32 ofsADSREntry = addrSRCNTable + instrNum;
  if (ofsADSREntry + 1 > 0x10000) {
    return false;
  }
  u8 srcn = readByte(ofsADSREntry);

  u32 offDirEnt = spcDirAddr + (srcn * 4);
  if (offDirEnt + 4 > 0x10000) {
    return false;
  }

  u16 addrSampStart = readShort(offDirEnt);

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
                                     u32 programNum,
                                     u32 spcDirAddr,
                                     u16 addrSRCNTable,
                                     u16 addrTuningTable,
                                     u16 addrADSRTable,
                                     u16 addrDrumKitTable,
                                     const std::string &name) :
  VGMInstr(instrSet, addrDrumKitTable, 0, programNum >> 7, programNum & 0x7F, name), version(ver),
  spcDirAddr(spcDirAddr),
  addrSRCNTable(addrSRCNTable),
  addrTuningTable(addrTuningTable),
  addrADSRTable(addrADSRTable),
  addrDrumKitTable(addrDrumKitTable) {
}

bool SuzukiSnesDrumKit::loadInstr() {
  u16 addr = addrDrumKitTable;

  for (u16 i = addr; readByte(i) < 0x80; i += 5) {
    SuzukiSnesDrumKitRgn *rgn = new SuzukiSnesDrumKitRgn(this, version, addrDrumKitTable);

    if (!rgn->initializePercussionRegion(i, spcDirAddr, addrSRCNTable, addrADSRTable, addrTuningTable)) {
      delete rgn;
      continue;
    }
    if (!rgn->loadRgn()) {
      delete rgn;
      return false;
    }

    u32 rgnOffDirEnt = spcDirAddr + (rgn->sampNum * 4);
    if (rgnOffDirEnt + 4 > 0x10000) {
      delete rgn;
      return false;
    }

    u16 addrSampStart = readShort(rgnOffDirEnt);

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
                             u16 addrSRCNTable) :
    VGMRgn(instr, addrSRCNTable, 0),
    version(ver) {
}

bool SuzukiSnesRgn::initializeNonPercussionRegion(u8 instrNum, u16 addrVolumeTable) {
  u16 addrSRCNTable = offset();
  u8 srcn = readByte(addrSRCNTable + instrNum);
  u8 vol = readByte(addrVolumeTable + srcn * 2);
  addSampNum(srcn, addrSRCNTable + instrNum, 1);
  addVolume(vol / 256.0, addrVolumeTable + srcn * 2, 1);
  return true;
}

bool SuzukiSnesRgn::initializeCommonRegion(u8 srcn,
                                           u32 /*spcDirAddr*/,
                                           u16 addrADSRTable,
                                           u16 addrTuningTable) {
  u8 adsr1 = readByte(addrADSRTable + srcn * 2);
  u8 adsr2 = readByte(addrADSRTable + srcn * 2 + 1);
  u8 fine_tuning = readByte(addrTuningTable + srcn * 2);
  s8 coarse_tuning = readByte(addrTuningTable + srcn * 2 + 1);

  addChild(addrADSRTable + srcn * 2, 1, "ADSR1");
  addChild(addrADSRTable + srcn * 2 + 1, 1, "ADSR2");
  addFineTune((s16) (fine_tuning / 256.0 * 100.0), addrTuningTable + srcn * 2, 1);
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
                                           u16 addrDrumKitTable) :
  SuzukiSnesRgn(instr, ver, addrDrumKitTable) {}

bool SuzukiSnesDrumKitRgn::initializePercussionRegion(u16 noteOffset,
                                                      u32 spcDirAddr,
                                                      u16 addrSRCNTable,
                                                      u16 addrADSRTable,
                                                      u16 addrTuningTable)
{
  u16 instrOffset = noteOffset + 1;
  u16 keyOffset = noteOffset + 2;
  u16 volumeOffset = noteOffset + 3;
  u16 panOffset = noteOffset + 4;

  u8 instrNum = readByte(instrOffset);
  addChild(instrOffset, 1, "Instrument");

  u8 srcn = readByte(addrSRCNTable + instrNum);
  initializeCommonRegion(srcn, spcDirAddr, addrADSRTable, addrTuningTable);

  u8 percussionIndex = readByte(noteOffset);
  setName(fmt::format("Drum {}", percussionIndex));
  keyLow = keyHigh = percussionIndex + KEY_BIAS;

  // sampNum is absolute key to play
  unityKey = (unityKey + KEY_BIAS) - readByte(keyOffset) + percussionIndex;

  addSampNum(srcn, addrSRCNTable + instrNum, 1);
  addChild(keyOffset, 1, "Key");

  addVolume(readByte(volumeOffset) / 256.0, volumeOffset, 1);

  u8 panValue = readByte(panOffset);
  if (panValue < 0x80) {
    addPan(panValue, panOffset);
  }

  return true;
}

SuzukiSnesDrumKitRgn::~SuzukiSnesDrumKitRgn() {}
