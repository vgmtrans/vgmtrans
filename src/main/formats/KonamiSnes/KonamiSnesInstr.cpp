/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "KonamiSnesInstr.h"
#include "SNESDSP.h"
#include <spdlog/fmt/fmt.h>

// ******************
// KonamiSnesInstrSet
// ******************

// Actually, this engine has 3 instrument tables:
//     - Common samples
//     - Switchable samples
//     - Percussive samples
// KonamiSnesInstrSet tries to load all these samples to merge them into a single DLS.
KonamiSnesInstrSet::KonamiSnesInstrSet(RawFile *file,
                                       KonamiSnesVersion ver,
                                       uint32_t offset,
                                       uint32_t bankedInstrOffset,
                                       uint8_t firstBankedInstr,
                                       uint32_t percInstrOffset,
                                       uint32_t spcDirAddr,
                                       const std::string &name) :
    VGMInstrSet(KonamiSnesFormat::name, file, offset, 0, name), version(ver),
    bankedInstrOffset(bankedInstrOffset),
    firstBankedInstr(firstBankedInstr),
    percInstrOffset(percInstrOffset),
    spcDirAddr(spcDirAddr) {
}

KonamiSnesInstrSet::~KonamiSnesInstrSet() {
}

bool KonamiSnesInstrSet::parseHeader() {
  return true;
}

bool KonamiSnesInstrSet::parseInstrPointers() {
  usedSRCNs.clear();
  for (int instr = 0; instr <= 0xff; instr++) {
    uint32_t instrItemSize = KonamiSnesInstr::expectedSize(version);

    uint32_t addrInstrHeader;
    if (instr < firstBankedInstr) {
      // common samples
      addrInstrHeader = dwOffset + (instrItemSize * instr);
    }
    else {
      // switchable samples
      addrInstrHeader = bankedInstrOffset + (instrItemSize * (instr - firstBankedInstr));
    }
    if (addrInstrHeader + instrItemSize > 0x10000) {
      return false;
    }

    if (!KonamiSnesInstr::isValidHeader(this->rawFile(), version, addrInstrHeader, spcDirAddr, false)) {
      if (instr < firstBankedInstr) {
        continue;
      }
      else {
        break;
      }
    }
    if (!KonamiSnesInstr::isValidHeader(this->rawFile(), version, addrInstrHeader, spcDirAddr, true)) {
      continue;
    }

    uint8_t srcn = readByte(addrInstrHeader);

    uint32_t offDirEnt = spcDirAddr + (srcn * 4);
    uint16_t addrSampStart = readShort(offDirEnt);
    if (addrSampStart < offDirEnt + 4) {
      continue;
    }

    std::vector<uint8_t>::iterator itrSRCN = find(usedSRCNs.begin(), usedSRCNs.end(), srcn);
    if (itrSRCN == usedSRCNs.end()) {
      usedSRCNs.push_back(srcn);
    }

    KonamiSnesInstr *newInstr = new KonamiSnesInstr(
      this, version, addrInstrHeader, instr >> 7, instr & 0x7f,
      spcDirAddr, false, fmt::format("Instrument {}", instr));
    aInstrs.push_back(newInstr);
  }
  if (aInstrs.size() == 0) {
    return false;
  }

  // percussive samples
  KonamiSnesInstr
      *newInstr = new KonamiSnesInstr(this, version, percInstrOffset, 127, 0, spcDirAddr, true, "Percussions");
  aInstrs.push_back(newInstr);

  std::sort(usedSRCNs.begin(), usedSRCNs.end());
  SNESSampColl *newSampColl = new SNESSampColl(KonamiSnesFormat::name, this->rawFile(), spcDirAddr, usedSRCNs);
  if (!newSampColl->loadVGMFile()) {
    delete newSampColl;
    return false;
  }

  return true;
}

// ***************
// KonamiSnesInstr
// ***************

KonamiSnesInstr::KonamiSnesInstr(VGMInstrSet *instrSet,
                                 KonamiSnesVersion ver,
                                 uint32_t offset,
                                 uint32_t theBank,
                                 uint32_t theInstrNum,
                                 uint32_t spcDirAddr,
                                 bool percussion,
                                 const std::string &name) :
    VGMInstr(instrSet, offset, KonamiSnesInstr::expectedSize(ver), theBank, theInstrNum, name),
    spcDirAddr(spcDirAddr),
    percussion(percussion) {
}

KonamiSnesInstr::~KonamiSnesInstr() {
}

bool KonamiSnesInstr::loadInstr() {
  // TODO: percussive samples
  if (percussion) {
    return true;
  }

  uint8_t srcn = readByte(dwOffset);
  uint32_t offDirEnt = spcDirAddr + (srcn * 4);
  if (offDirEnt + 4 > 0x10000) {
    return false;
  }

  uint16_t addrSampStart = readShort(offDirEnt);

  KonamiSnesRgn *rgn = new KonamiSnesRgn(this, version, dwOffset, percussion);
  rgn->sampOffset = addrSampStart - spcDirAddr;
  addRgn(rgn);

  return true;
}

bool KonamiSnesInstr::isValidHeader(RawFile *file,
                                    KonamiSnesVersion version,
                                    uint32_t addrInstrHeader,
                                    uint32_t spcDirAddr,
                                    bool validateSample) {
  size_t instrItemSize = KonamiSnesInstr::expectedSize(version);

  if (addrInstrHeader + instrItemSize > 0x10000) {
    return false;
  }

  uint8_t srcn = file->readByte(addrInstrHeader);
  int16_t pitch_scale = file->readShortBE(addrInstrHeader + 1);
  uint8_t adsr1 = file->readByte(addrInstrHeader + 3);
  uint8_t adsr2 = file->readByte(addrInstrHeader + 4);

  if (srcn == 0xff) // SRCN:FF is false-positive in 99.999999% of cases
  {
    return false;
  }

  uint32_t addrDIRentry = spcDirAddr + (srcn * 4);
  if (!SNESSampColl::isValidSampleDir(file, addrDIRentry, validateSample)) {
    return false;
  }

  uint16_t srcAddr = file->readShort(addrDIRentry);
  uint16_t loopStartAddr = file->readShort(addrDIRentry + 2);
  if (srcAddr > loopStartAddr || (loopStartAddr - srcAddr) % 9 != 0) {
    return false;
  }

  return true;
}

uint32_t KonamiSnesInstr::expectedSize(KonamiSnesVersion version) {
  if (version == KONAMISNES_V1 || version == KONAMISNES_V2 || version == KONAMISNES_V3) {
    return 8;
  } else {
    return 7;
  }
}

// *************
// KonamiSnesRgn
// *************

KonamiSnesRgn::KonamiSnesRgn(KonamiSnesInstr *instr, KonamiSnesVersion ver, uint32_t offset, bool percussion) :
    VGMRgn(instr, offset, KonamiSnesInstr::expectedSize(ver)) {
  // TODO: percussive samples

  uint8_t srcn = readByte(offset);
  int8_t raw_key = readByte(offset + 1);
  int8_t tuning = readByte(offset + 2);
  uint8_t adsr1 = readByte(offset + 3);
  uint8_t adsr2 = readByte(offset + 4);
  uint8_t pan = readByte(offset + 5);
  uint8_t vol = readByte(offset + 6);

  const int8_t key = (tuning >= 0) ? raw_key : (raw_key - 1);
  const int16_t full_tuning = static_cast<int16_t>((static_cast<uint8_t>(key) << 8) | static_cast<uint8_t>(tuning));

  uint8_t gain = adsr2;
  bool use_adsr = ((adsr1 & 0x80) != 0);

  double fine_tuning;
  double coarse_tuning;
  const double pitch_fixer = log(4286.0 / 4096.0) / log(2) * 12;  // from pitch table ($10be vs $1000)
  fine_tuning = modf((full_tuning / 256.0) + pitch_fixer, &coarse_tuning);

  // normalize
  if (fine_tuning >= 0.5) {
    coarse_tuning += 1.0;
    fine_tuning -= 1.0;
  }
  else if (fine_tuning <= -0.5) {
    coarse_tuning -= 1.0;
    fine_tuning += 1.0;
  }

  addSampNum(srcn, offset, 1);
  addUnityKey(72 - (int) (coarse_tuning), offset + 1, 1);
  addFineTune((int16_t) (fine_tuning * 100.0), offset + 2, 1);
  addChild(offset + 3, 1, "ADSR1");
  addChild(offset + 4, 1, use_adsr ? "ADSR2" : "GAIN");
  addChild(offset + 5, 1, "Pan");
  // volume is *decreased* by final volume value
  // so it is impossible to convert it in 100% accuracy
  // the following value 72.0 is chosen as a "average channel volume level (before pan processing)"
  addVolume(std::max(1.0 - (vol / 72.0), 0.0), offset + 6);
  snesConvADSR<VGMRgn>(this, adsr1, adsr2, gain);
}

KonamiSnesRgn::~KonamiSnesRgn() {}

bool KonamiSnesRgn::loadRgn() {
  return true;
}
