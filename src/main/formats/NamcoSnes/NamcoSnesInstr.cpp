/*
* VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "util/types.h"
#include "NamcoSnesInstr.h"
#include <spdlog/fmt/fmt.h>
#include "SNESDSP.h"

// *****************
// NamcoSnesInstrSet
// *****************

NamcoSnesInstrSet::NamcoSnesInstrSet(RawFile *file,
                                     NamcoSnesVersion ver,
                                     u32 spcDirAddr,
                                     u16 addrTuningTable,
                                     const std::string &name) :
    VGMInstrSet(NamcoSnesFormat::name, file, addrTuningTable, 0, name), version(ver),
    spcDirAddr(spcDirAddr),
    addrTuningTable(addrTuningTable) {
}

NamcoSnesInstrSet::~NamcoSnesInstrSet() {
}

bool NamcoSnesInstrSet::parseHeader() {
  return true;
}

bool NamcoSnesInstrSet::parseInstrPointers() {
  u8 maxSampCount = 0x80;
  if (spcDirAddr < addrTuningTable) {
    u16 sampCountCandidate = (addrTuningTable - spcDirAddr) / 4;
    if (sampCountCandidate < maxSampCount) {
      maxSampCount = (u8) sampCountCandidate;
    }
  }

  usedSRCNs.clear();
  for (u8 srcn = 0; srcn < maxSampCount; srcn++) {
    u32 addrDIRentry = spcDirAddr + (srcn * 4);
    if (!SNESSampColl::isValidSampleDir(rawFile(), addrDIRentry, true)) {
      continue;
    }

    u16 addrSampStart = readShort(addrDIRentry);
    if (addrSampStart < spcDirAddr) {
      continue;
    }

    u32 ofsTuningEntry;
    ofsTuningEntry = addrTuningTable + (srcn * 2);
    if (ofsTuningEntry + 2 > 0x10000) {
      break;
    }

    u16 sampleRate = readShort(ofsTuningEntry);
    if (sampleRate == 0 || sampleRate == 0xffff) {
      continue;
    }

    usedSRCNs.push_back(srcn);

    NamcoSnesInstr *newInstr = new NamcoSnesInstr(this, version, srcn, spcDirAddr, ofsTuningEntry,
fmt::format("Instrument {}", srcn));
    aInstrs.push_back(newInstr);
  }

  if (aInstrs.size() == 0) {
    return false;
  }

  std::sort(usedSRCNs.begin(), usedSRCNs.end());
  SNESSampColl *newSampColl = new SNESSampColl(NamcoSnesFormat::name, this->rawFile(), spcDirAddr, usedSRCNs);
  if (!newSampColl->loadVGMFile()) {
    delete newSampColl;
    return false;
  }

  return true;
}

// **************
// NamcoSnesInstr
// **************

NamcoSnesInstr::NamcoSnesInstr(VGMInstrSet *instrSet,
                               NamcoSnesVersion ver,
                               u8 srcn,
                               u32 spcDirAddr,
                               u16 addrTuningEntry,
                               const std::string &name) :
    VGMInstr(instrSet, addrTuningEntry, 0, 0, srcn, name), version(ver),
    spcDirAddr(spcDirAddr),
    addrTuningEntry(addrTuningEntry) {
}

NamcoSnesInstr::~NamcoSnesInstr() {
}

bool NamcoSnesInstr::loadInstr() {
  u32 offDirEnt = spcDirAddr + (instrNum * 4);
  if (offDirEnt + 4 > 0x10000) {
    return false;
  }

  u16 addrSampStart = readShort(offDirEnt);

  NamcoSnesRgn *rgn = new NamcoSnesRgn(this, version, instrNum, spcDirAddr, addrTuningEntry);
  rgn->sampOffset = addrSampStart - spcDirAddr;
  addRgn(rgn);

  setGuessedLength();
  return true;
}

// ************
// NamcoSnesRgn
// ************

NamcoSnesRgn::NamcoSnesRgn(NamcoSnesInstr *instr,
                           NamcoSnesVersion ver,
                           u8 srcn,
                           u32 spcDirAddr,
                           u16 addrTuningEntry) :
    VGMRgn(instr, addrTuningEntry, 0),
    version(ver) {
  s16 pitch_scale = getShortBE(addrTuningEntry);

  const double pitch_fixer = 4032.0 / 4096.0;
  double fine_tuning;
  double coarse_tuning;
  fine_tuning = modf((log(pitch_scale * pitch_fixer / 256.0) / log(2.0)) * 12.0, &coarse_tuning);

  // normalize
  if (fine_tuning >= 0.5) {
    coarse_tuning += 1.0;
    fine_tuning -= 1.0;
  }
  else if (fine_tuning <= -0.5) {
    coarse_tuning -= 1.0;
    fine_tuning += 1.0;
  }

  addChild(addrTuningEntry, 2, "Sample Rate");
  unityKey = 71 - (int) coarse_tuning;
  fineTune = (s16) (fine_tuning * 100.0);

  u8 adsr1 = 0x8f;
  u8 adsr2 = 0xe0;
  u8 gain = 0;
  snesConvADSR<VGMRgn>(this, adsr1, adsr2, gain);

  setGuessedLength();
}

NamcoSnesRgn::~NamcoSnesRgn() {
}

bool NamcoSnesRgn::loadRgn() {
  return true;
}
