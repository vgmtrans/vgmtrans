/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "util/types.h"
#include "AsciiShuichiSnesInstr.h"
#include <spdlog/fmt/fmt.h>
#include "SNESDSP.h"
#include "AsciiShuichiSnesFormat.h"
#include "LogManager.h"

// ************************
// AsciiShuichiSnesInstrSet
// ************************

AsciiShuichiSnesInstrSet::AsciiShuichiSnesInstrSet(RawFile *file, u32 offset,
                                                   u32 fineTuningTableAddress,
                                                   u32 spcDirAddress, const std::string &name)
    :
    VGMInstrSet(AsciiShuichiSnesFormat::name, file, offset, 0, name),
    fineTuningTableAddress(fineTuningTableAddress),
    spcDirAddress(spcDirAddress) {
}

bool AsciiShuichiSnesInstrSet::parseHeader() {
  return true;
}

bool AsciiShuichiSnesInstrSet::parseInstrPointers() {
  usedSRCNs.clear();
  for (int instr = 0; instr < 0x40; instr++) {
    const u32 instrHeaderAddress = offset() + (4 * instr);
    if (instrHeaderAddress + 4 > 0x10000) {
      return false;
    }

    if (!AsciiShuichiSnesInstr::isValidHeader(this->rawFile(), instrHeaderAddress, spcDirAddress)) {
      continue;
    }

    const u8 srcn = readByte(instrHeaderAddress);
    auto itrSRCN = std::ranges::find(usedSRCNs, srcn);
    if (itrSRCN == usedSRCNs.end()) {
      usedSRCNs.push_back(srcn);
    }

    const auto newInstr = new AsciiShuichiSnesInstr(
      this, instrHeaderAddress, instr >> 7, instr & 0x7f, spcDirAddress, fineTuningTableAddress,
      fmt::format("Instrument {}", instr));
    aInstrs.push_back(newInstr);
  }
  if (aInstrs.empty()) {
    return false;
  }

  std::ranges::sort(usedSRCNs);
  auto newSampColl = new SNESSampColl(AsciiShuichiSnesFormat::name, this->rawFile(), spcDirAddress, usedSRCNs);
  if (!newSampColl->loadVGMFile()) {
    delete newSampColl;
    return false;
  }

  return true;
}

// *********************
// AsciiShuichiSnesInstr
// *********************

AsciiShuichiSnesInstr::AsciiShuichiSnesInstr(VGMInstrSet *instrSet,
                                 u32 offset,
                                 u32 theBank,
                                 u32 theInstrNum,
                                 u32 spcDirAddress,
                                 u32 fineTuningTableAddress,
                                 const std::string &name) :
    VGMInstr(instrSet, offset, 6, theBank, theInstrNum, name),
    spcDirAddress(spcDirAddress),
    fineTuningTableAddress(fineTuningTableAddress) {
}

bool AsciiShuichiSnesInstr::loadInstr() {
  const u8 srcn = readByte(offset());
  const u32 dirEntryOffset = spcDirAddress + (srcn * 4);
  if (dirEntryOffset + 4 > 0x10000) {
    return false;
  }

  const u16 sampleStartAddress = readShort(dirEntryOffset);

  if (fineTuningTableAddress + srcn > 0x10000) {
    return false;
  }
  const auto spcFineTuning = static_cast<s8>(readByte(fineTuningTableAddress + srcn));

  const auto rgn = new AsciiShuichiSnesRgn(this, offset(), spcFineTuning);
  rgn->sampOffset = sampleStartAddress - spcDirAddress;
  addRgn(rgn);

  return true;
}

bool AsciiShuichiSnesInstr::isValidHeader(const RawFile *file, u32 instrHeaderAddress, u32 spcDirAddress) {
  if (instrHeaderAddress + 4 > 0x10000) {
    return false;
  }

  const u8 srcn = file->readByte(instrHeaderAddress);
  const u8 adsr1 = file->readByte(instrHeaderAddress + 1);
  const u8 gain = file->readByte(instrHeaderAddress + 3);

  if (srcn >= 0x80 || (adsr1 == 0 && gain == 0)) {
    return false;
  }

  const u32 dirEntryAddress = spcDirAddress + (srcn * 4);
  if (!SNESSampColl::isValidSampleDir(file, dirEntryAddress, false)) {
    return false;
  }

  const u16 srcAddress = file->readShort(dirEntryAddress);
  const u16 loopStartAddress = file->readShort(dirEntryAddress + 2);
  if (srcAddress > loopStartAddress || (loopStartAddress - srcAddress) % 9 != 0) {
    return false;
  }

  return true;
}

// *******************
// AsciiShuichiSnesRgn
// *******************

AsciiShuichiSnesRgn::AsciiShuichiSnesRgn(AsciiShuichiSnesInstr *instr, u32 offset, s8 spcFineTuning) :
    VGMRgn(instr, offset, 4) {
  const u8 srcn = readByte(offset);
  const u8 adsr1 = readByte(offset + 1);
  const u8 adsr2 = readByte(offset + 2);
  const u8 gain = readByte(offset + 3);

  addSampNum(srcn, offset, 1);
  addChild(offset + 1, 1, "ADSR1");
  addChild(offset + 2, 1, "ADSR2");
  addChild(offset + 3, 1, "GAIN");

  // pitchTable[57] is 0x1000 then
  setUnityKey(57 + 24);
  setFineTune(static_cast<s16>(pitchScaleToCents(1.0 + (spcFineTuning / 4096.0))));

  snesConvADSR<VGMRgn>(this, adsr1, adsr2, gain);

  const u8 sl = (adsr2 >> 5);
  emulateSDSPGAIN(gain, static_cast<s16>((sl << 8) | 0xff), 0, nullptr, &release_time);
}

bool AsciiShuichiSnesRgn::loadRgn() {
  return true;
}
