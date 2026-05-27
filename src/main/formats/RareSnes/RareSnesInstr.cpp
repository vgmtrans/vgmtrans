/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "base/Types.h"
#include "RareSnesInstr.h"
#include "SNESDSP.h"
#include "RareSnesFormat.h"
#include <spdlog/fmt/fmt.h>

// ****************
// RareSnesInstrSet
// ****************

RareSnesInstrSet::RareSnesInstrSet(RawFile *file, u32 offset, u32 spcDirAddr, const std::string &name) :
    VGMInstrSet(RareSnesFormat::name, file, offset, 0, name),
    spcDirAddr(spcDirAddr),
    maxSRCNValue(255) {
  Initialize();
}

RareSnesInstrSet::RareSnesInstrSet(RawFile *file,
                                   u32 offset,
                                   u32 spcDirAddr,
                                   const std::map<u8, s8> &instrUnityKeyHints,
                                   const std::map<u8, s16> &instrPitchHints,
                                   const std::map<u8, u16> &instrADSRHints,
                                   const std::string &name) :
    VGMInstrSet(RareSnesFormat::name, file, offset, 0, name),
    spcDirAddr(spcDirAddr),
    maxSRCNValue(255),
    instrUnityKeyHints(instrUnityKeyHints),
    instrPitchHints(instrPitchHints),
    instrADSRHints(instrADSRHints) {
  Initialize();
}

RareSnesInstrSet::~RareSnesInstrSet() {}

void RareSnesInstrSet::Initialize() {
  for (u32 srcn = 0; srcn < 256; srcn++) {
    u32 offDirEnt = spcDirAddr + (srcn * 4);
    if (offDirEnt + 4 > 0x10000) {
      maxSRCNValue = srcn - 1;
      break;
    }

    if (readShort(offDirEnt) == 0) {
      maxSRCNValue = srcn - 1;
      break;
    }
  }

  setLength(0x100);
  if (offset() + length() > rawFile()->size()) {
    setLength(rawFile()->size() - offset());
  }
  ScanAvailableInstruments();
}

void RareSnesInstrSet::ScanAvailableInstruments() {
  availInstruments.clear();

  bool firstZero = true;
  for (u32 inst = 0; inst < length(); inst++) {
    u8 srcn = readByte(offset() + inst);

    if (srcn == 0) {
      if (firstZero) {
        firstZero = false;
      }
      else {
        continue;
      }
    }

    u32 offDirEnt = spcDirAddr + (srcn * 4);
    if (offDirEnt + 4 > 0x10000) {
      continue;
    }
    if (srcn > maxSRCNValue) {
      continue;
    }

    if (!SNESSampColl::isValidSampleDir(rawFile(), offDirEnt, true)) {
      continue;
    }

    u16 addrSampStart = readShort(offDirEnt);
    u16 addrSampLoop = readShort(offDirEnt + 2);
    // not in DIR table
    if (addrSampStart < spcDirAddr + (inst * 4)) {
      continue;
    }
    // address 0 is probably legit, but it should not be used
    if (addrSampStart == 0) {
      continue;
    }
    // Rare engine does not break the following rule... perhaps
    if (addrSampStart < spcDirAddr) {
      continue;
    }

    availInstruments.push_back(inst);
  }
}

bool RareSnesInstrSet::parseHeader() {
  return true;
}

bool RareSnesInstrSet::parseInstrPointers() {
  // make a loopup table for duplicated instruments,
  // to give them appropriate instrument metadata
  // (it's a workaround for wrong instrument tuning)
  std::map<u16, u8> addrToInstrLookups;
  for (auto itr = instrUnityKeyHints.begin(); itr != instrUnityKeyHints.end(); ++itr) {
    u8 inst = (*itr).first;
    u8 srcn = readByte(offset() + inst);

    u32 offDirEnt = spcDirAddr + (srcn * 4);
    if (addrToInstrLookups.count(offDirEnt) == 0) {
      addrToInstrLookups[offDirEnt] = inst;
    }
  }

  for (std::vector<u8>::iterator itr = availInstruments.begin(); itr != availInstruments.end(); ++itr) {
    u8 inst = (*itr);
    u8 srcn = readByte(offset() + inst);

    u8 inst_remapped = inst;
    u32 offDirEnt = spcDirAddr + (srcn * 4);
    if (addrToInstrLookups.count(offDirEnt) != 0) {
      inst_remapped = addrToInstrLookups[offDirEnt];
    }

    s8 transpose = 0;
    std::map<u8, s8>::iterator itrKey;
    itrKey = this->instrUnityKeyHints.find(inst_remapped);
    if (itrKey != instrUnityKeyHints.end()) {
      transpose = itrKey->second;
    }

    s16 pitch = 0;
    std::map<u8, s16>::iterator itrPitch;
    itrPitch = this->instrPitchHints.find(inst_remapped);
    if (itrPitch != instrPitchHints.end()) {
      pitch = itrPitch->second;
    }

    u16 adsr = 0x8FE0;
    std::map<u8, u16>::iterator itrADSR;
    itrADSR = this->instrADSRHints.find(inst_remapped);
    if (itrADSR != instrADSRHints.end()) {
      adsr = itrADSR->second;
    }

    RareSnesInstr *newInstr = new RareSnesInstr(
      this, offset() + inst, inst >> 7, inst & 0x7f, spcDirAddr, transpose,
      pitch, adsr, fmt::format("Instrument: {:#x}", inst));
    aInstrs.push_back(newInstr);
  }
  return aInstrs.size() != 0;
}

const std::vector<u8> &RareSnesInstrSet::getAvailableInstruments() {
  return availInstruments;
}

// *************
// RareSnesInstr
// *************

RareSnesInstr::RareSnesInstr(VGMInstrSet *instrSet,
                             u32 offset,
                             u32 theBank,
                             u32 theInstrNum,
                             u32 spcDirAddr,
                             s8 transpose,
                             s16 pitch,
                             u16 adsr,
                             const std::string &name) :
    VGMInstr(instrSet, offset, 1, theBank, theInstrNum, name),
    spcDirAddr(spcDirAddr),
    transpose(transpose),
    pitch(pitch),
    adsr(adsr) {
}

RareSnesInstr::~RareSnesInstr() {
}

bool RareSnesInstr::loadInstr() {
  u8 srcn = readByte(offset());
  u32 offDirEnt = spcDirAddr + (srcn * 4);
  if (offDirEnt + 4 > 0x10000) {
    return false;
  }

  u16 addrSampStart = readShort(offDirEnt);

  RareSnesRgn *rgn = new RareSnesRgn(this, offset(), transpose, pitch, adsr);
  rgn->sampOffset = addrSampStart - spcDirAddr;
  addRgn(rgn);
  return true;
}

// ***********
// RareSnesRgn
// ***********

RareSnesRgn::RareSnesRgn(RareSnesInstr *instr, u32 offset, s8 transpose, s16 pitch, u16 adsr) :
    VGMRgn(instr, offset, 1),
    transpose(transpose),
    pitch(pitch),
    adsr(adsr) {
  // normalize (it is needed especially since SF2 pitch correction is signed 8-bit)
  s16 pitchKeyShift = (pitch / 100);
  s8 realTranspose = transpose + pitchKeyShift;
  s16 realPitch = pitch - (pitchKeyShift * 100);

  // NOTE_PITCH_TABLE[73] == 0x1000
  // 0x80 + (73 - 36) = 0xA5
  setUnityKey(36 + 36 - realTranspose);
  setFineTune(realPitch);
  snesConvADSR<VGMRgn>(this, adsr >> 8, adsr & 0xff, 0);
}

RareSnesRgn::~RareSnesRgn() {}

bool RareSnesRgn::loadRgn() {
  return true;
}
