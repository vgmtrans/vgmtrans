/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#include "util/types.h"
#include "CapcomSnesInstr.h"
#include <spdlog/fmt/fmt.h>
#include "SNESDSP.h"
#include "CapcomSnesDefinitions.h"
#include "CapcomSnesFormat.h"

// ****************
// CapcomSnesInstrSet
// ****************

CapcomSnesInstrSet::CapcomSnesInstrSet(RawFile *file, u32 offset, u32 spcDirAddr, const std::string &name) :
    VGMInstrSet(CapcomSnesFormat::name, file, offset, 0, name),
    spcDirAddr(spcDirAddr) {
}

CapcomSnesInstrSet::~CapcomSnesInstrSet() {
}

bool CapcomSnesInstrSet::parseHeader() {
  return true;
}

bool CapcomSnesInstrSet::parseInstrPointers() {
  usedSRCNs.clear();
  for (int instr = 0; instr <= 0xff; instr++) {
    u32 addrInstrHeader = offset() + (6 * instr);
    if (addrInstrHeader + 6 > 0x10000) {
      return false;
    }

    // skip blank slot (Mega Man X2, Super Street Fighter II Turbo)
    bool empty_garbage = true;
    for (u32 off = addrInstrHeader; off < addrInstrHeader + 6; off++) {
      const u8 v = readByte(off);
      if (v != 0 && v != 0xff) {
        empty_garbage = false;
        break;
      }
    }
    if (empty_garbage) {
      continue;
    }

    if (!CapcomSnesInstr::isValidHeader(this->rawFile(), addrInstrHeader, spcDirAddr, false)) {
      break;
    }
    if (!CapcomSnesInstr::isValidHeader(this->rawFile(), addrInstrHeader, spcDirAddr, true)) {
      continue;
    }

    u8 srcn = readByte(addrInstrHeader);
    std::vector<u8>::iterator itrSRCN = std::ranges::find(usedSRCNs, srcn);
    if (itrSRCN == usedSRCNs.end()) {
      usedSRCNs.push_back(srcn);
    }

    CapcomSnesInstr *newInstr = new CapcomSnesInstr(
      this, addrInstrHeader, instr >> 7, instr & 0x7f, spcDirAddr,
      fmt::format("Instrument {}", instr));
    aInstrs.push_back(newInstr);
  }
  if (aInstrs.size() == 0) {
    return false;
  }

  std::ranges::sort(usedSRCNs);
  SNESSampColl *newSampColl = new SNESSampColl(CapcomSnesFormat::name, this->rawFile(), spcDirAddr, usedSRCNs);
  if (!newSampColl->loadVGMFile()) {
    delete newSampColl;
    return false;
  }

  return true;
}

// *************
// CapcomSnesInstr
// *************

CapcomSnesInstr::CapcomSnesInstr(VGMInstrSet *instrSet,
                                 u32 offset,
                                 u32 theBank,
                                 u32 theInstrNum,
                                 u32 spcDirAddr,
                                 const std::string &name) :
    VGMInstr(instrSet, offset, 6, theBank, theInstrNum, name), spcDirAddr(spcDirAddr) {
}

CapcomSnesInstr::~CapcomSnesInstr() {}

bool CapcomSnesInstr::loadInstr() {
  u8 srcn = readByte(offset());
  u32 offDirEnt = spcDirAddr + (srcn * 4);
  if (offDirEnt + 4 > 0x10000) {
    return false;
  }

  // Vibrato has a full +/- octave range of 1200 cents (max depth value evaluates to 127/128 of range, just like SF2)
  // Vibrato frequency is a unipolar sweep from one Capcom LFO step up to 255 steps, so we use the
  // step frequency as the global base and let the configured vibrato frequency source span the full range in Hz.
  // Tremolo runs at twice the base vibrato rate and only attenuates, never boosts.

  addStandardVibratoHandling(capcom_snes::vibratoModulationSpec());
  addStandardTremoloHandling(capcom_snes::tremoloModulationSpec());

  u16 addrSampStart = readShort(offDirEnt);

  CapcomSnesRgn *rgn = new CapcomSnesRgn(this, offset());
  rgn->sampOffset = addrSampStart - spcDirAddr;
  addRgn(rgn);

  return true;
}

bool CapcomSnesInstr::isValidHeader(RawFile *file, u32 addrInstrHeader, u32 spcDirAddr, bool validateSample) {
  if (addrInstrHeader + 6 > 0x10000) {
    return false;
  }

  u8 srcn = file->readByte(addrInstrHeader);
  u8 adsr1 = file->readByte(addrInstrHeader + 1);
//  u8 adsr2 = file->GetByte(addrInstrHeader + 2);
  u8 gain = file->readByte(addrInstrHeader + 3);
//  s16 pitch_scale = file->GetShortBE(addrInstrHeader + 4);

  if (srcn >= 0x80 || (adsr1 == 0 && gain == 0)) {
    return false;
  }

  u32 addrDIRentry = spcDirAddr + (srcn * 4);
  if (!SNESSampColl::isValidSampleDir(file, addrDIRentry, validateSample)) {
    return false;
  }

  u16 srcAddr = file->readShort(addrDIRentry);
  u16 loopStartAddr = file->readShort(addrDIRentry + 2);
  if (srcAddr > loopStartAddr || (loopStartAddr - srcAddr) % 9 != 0) {
    return false;
  }

  return true;
}

// ***********
// CapcomSnesRgn
// ***********

CapcomSnesRgn::CapcomSnesRgn(CapcomSnesInstr *instr, u32 offset) :
    VGMRgn(instr, offset, 6) {
  u8 srcn = readByte(offset);
  u8 adsr1 = readByte(offset + 1);
  u8 adsr2 = readByte(offset + 2);
  u8 gain = readByte(offset + 3);
  s16 pitchScale = getShortBE(offset + 4);

  addSampNum(srcn, offset, 1);
  addChild(offset + 1, 1, "ADSR1");
  addChild(offset + 2, 1, "ADSR2");
  addChild(offset + 3, 1, "GAIN");

  constexpr int baseUnityKey = 96;
  // MMX pitch-table C is 0x10BE, but the driver writes 0x10B0 for that neutral C after its low-nibble
  // pitch quantization. That's a -5.66 cent difference. Using 0x10BE would give pitches that are on avg more sharp.
  constexpr double referencePitch = 0x10B0 / 4096.0;

  const double ratio = pitchScale != 0 ? (static_cast<double>(pitchScale) / 256.0) * referencePitch : 1.0;
  const double semitones = 12.0 * std::log2(ratio);
  int coarse = static_cast<int>(std::lround(semitones));
  int fine = static_cast<int>(std::lround((semitones - coarse) * 100.0));

  if (fine >= 50) {
    ++coarse;
    fine -= 100;
  } else if (fine < -50) {
    --coarse;
    fine += 100;
  }

  addUnityKey(baseUnityKey - coarse, offset + 4, 1);
  addFineTune(static_cast<s16>(fine), offset + 5, 1);
  snesConvADSR<VGMRgn>(this, adsr1, adsr2, gain);

  u8 sl = (adsr2 >> 5);
  emulateSDSPGAIN(gain, (sl << 8) | 0xff, 0, nullptr, &release_time);
}

CapcomSnesRgn::~CapcomSnesRgn() {
}

bool CapcomSnesRgn::loadRgn() {
  return true;
}
