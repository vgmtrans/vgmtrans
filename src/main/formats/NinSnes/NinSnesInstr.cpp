/*
* VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#include "NinSnesInstr.h"
#include "NinSnesSeq.h"
#include "SNESDSP.h"
#include "VGMColl.h"
#include <spdlog/fmt/fmt.h>

namespace {

constexpr uint8_t kNinSnesMidiKeyCorrection = 24;

uint32_t getProgramNumber(const VGMInstr* instr) {
  return (instr->bank << 7) | (instr->instrNum & 0x7f);
}

VGMInstr* findInstrByProgram(const std::vector<VGMInstr*>& instrs, uint32_t progNum) {
  for (auto* instr : instrs) {
    if (getProgramNumber(instr) == progNum) {
      return instr;
    }
  }
  return nullptr;
}

VGMRgn* cloneLegacyRgnForDrumKit(VGMInstr* instr,
                                 VGMRgn* sourceRgn,
                                 uint8_t noteIndex,
                                 int8_t globalTranspose) {
  auto* newRgn = new VGMRgn(instr,
                            sourceRgn->offset(),
                            sourceRgn->length(),
                            noteIndex,
                            noteIndex,
                            sourceRgn->velLow,
                            sourceRgn->velHigh,
                            sourceRgn->sampNum);

  newRgn->sampOffset = sourceRgn->sampOffset;
  newRgn->unityKey = sourceRgn->unityKey + (noteIndex - 0x3C) - globalTranspose;
  newRgn->fineTune = sourceRgn->fineTune;
  newRgn->attack_time = sourceRgn->attack_time;
  newRgn->decay_time = sourceRgn->decay_time;
  newRgn->sustain_level = sourceRgn->sustain_level;
  newRgn->sustain_time = sourceRgn->sustain_time;
  newRgn->release_time = sourceRgn->release_time;
  return newRgn;
}

VGMRgn* cloneIntelliTARgnForDrumKit(VGMInstr* instr,
                                    VGMRgn* sourceRgn,
                                    uint8_t drumKey,
                                    uint8_t playedNoteByte) {
  auto* newRgn = new VGMRgn(instr,
                            sourceRgn->offset(),
                            sourceRgn->length(),
                            drumKey,
                            drumKey,
                            sourceRgn->velLow,
                            sourceRgn->velHigh,
                            sourceRgn->sampNum);

  newRgn->sampOffset = sourceRgn->sampOffset;
  newRgn->sampDataLength = sourceRgn->sampDataLength;
  newRgn->unityKey =
      sourceRgn->unityKey + drumKey - ((playedNoteByte & 0x7f) + kNinSnesMidiKeyCorrection);
  newRgn->coarseTune = sourceRgn->coarseTune;
  newRgn->fineTune = sourceRgn->fineTune;
  newRgn->loop = sourceRgn->loop;
  newRgn->pan = sourceRgn->pan;
  newRgn->setAttenuation(sourceRgn->attenDb());
  newRgn->attack_time = sourceRgn->attack_time;
  newRgn->decay_time = sourceRgn->decay_time;
  newRgn->sustain_level = sourceRgn->sustain_level;
  newRgn->sustain_time = sourceRgn->sustain_time;
  newRgn->release_time = sourceRgn->release_time;
  newRgn->setLfoVibFreqHz(sourceRgn->lfoVibFreqHz());
  newRgn->setLfoVibDepthCents(sourceRgn->lfoVibDepthCents());
  newRgn->setLfoVibDelaySeconds(sourceRgn->lfoVibDelaySeconds());
  return newRgn;
}

VGMRgn* createRgnFromHeaderData(VGMInstr* instr,
                                RawFile* rawFile,
                                NinSnesVersion version,
                                uint32_t spcDirAddr,
                                const std::array<uint8_t, 6>& regionData) {
  const uint8_t srcn = regionData[0];
  const uint8_t adsr1 = regionData[1];
  const uint8_t adsr2 = regionData[2];
  const uint8_t gain = regionData[3];
  const uint16_t offDirEnt = spcDirAddr + (srcn * 4);

  if (offDirEnt + 4 > 0x10000 || !SNESSampColl::isValidSampleDir(rawFile, offDirEnt, true)) {
    return nullptr;
  }

  uint16_t pitch_scale = 0;
  if (version == NINSNES_EARLIER) {
    pitch_scale = static_cast<uint16_t>(static_cast<int8_t>(regionData[4]) * 256);
  }
  else {
    pitch_scale = static_cast<uint16_t>((regionData[4] << 8) | regionData[5]);
  }

  const double pitch_fixer = 4286.0 / 4096.0;
  double fine_tuning;
  double coarse_tuning;

  bool wrapsPercussion = (((uint32_t) 0x0217 * pitch_scale) >> 8) > 0x3FFF;
  if (wrapsPercussion) {
    pitch_scale = ((((uint32_t) 0x0217 * pitch_scale) >> 8) & 0x3FFF) * 256.0 / 0x0217;
  }

  fine_tuning = modf((log(pitch_scale * pitch_fixer / 256.0) / log(2.0)) * 12.0, &coarse_tuning);

  if (fine_tuning >= 0.5) {
    coarse_tuning += 1.0;
    fine_tuning -= 1.0;
  }
  else if (fine_tuning <= -0.5) {
    coarse_tuning -= 1.0;
    fine_tuning += 1.0;
  }

  auto* rgn = new VGMRgn(instr, 0, NinSnesInstr::expectedSize(version));
  rgn->sampOffset = rawFile->readShort(offDirEnt) - spcDirAddr;
  rgn->sampNum = srcn;
  rgn->unityKey = 96 - static_cast<int>(coarse_tuning);
  rgn->fineTune = static_cast<int16_t>(fine_tuning * 100.0);
  snesConvADSR<VGMRgn>(rgn, adsr1, adsr2, gain);
  return rgn;
}

}  // namespace

// ****************
// NinSnesInstrSet
// ****************

NinSnesInstrSet::NinSnesInstrSet(RawFile *file,
                                 NinSnesVersion ver,
                                 uint32_t offset,
                                 uint32_t spcDirAddr,
                                 const std::string &name) :
    VGMInstrSet(NinSnesFormat::name, file, offset, 0, name), version(ver),
    spcDirAddr(spcDirAddr),
    konamiTuningTableAddress(0),
    konamiTuningTableSize(0) {
}

NinSnesInstrSet::~NinSnesInstrSet() {
}

bool NinSnesInstrSet::parseHeader() {
  return true;
}

bool NinSnesInstrSet::parseInstrPointers() {
  uint8_t instr_max = 0x7f;
  if (version == NINSNES_HUMAN) {
    instr_max = 0x200 / NinSnesInstr::expectedSize(version);
  }

  usedSRCNs.clear();
  for (int instr = 0; instr <= instr_max; instr++) {
    uint32_t instrItemSize = NinSnesInstr::expectedSize(version);
    uint32_t addrInstrHeader = offset() + (instrItemSize * instr);
    if (addrInstrHeader + instrItemSize > 0x10000) {
      return false;
    }

    // skip blank slot
    if (version != NINSNES_EARLIER) {
      // Kirby Super Star
      if (readByte(addrInstrHeader) == 0xff && readByte(addrInstrHeader + 1) == 0xff
        && readByte(addrInstrHeader + 2) == 0xff &&
        readByte(addrInstrHeader + 3) == 0xff && readByte(addrInstrHeader + 4) == 0xff
        && readByte(addrInstrHeader + 5) == 0xff) {
        continue;
      }

      // Bubsy in Claws Encounters of the Furred Kind
      if (readByte(addrInstrHeader) == 0 && readByte(addrInstrHeader + 1) == 0
        && readByte(addrInstrHeader + 2) == 0 &&
        readByte(addrInstrHeader + 3) == 0 && readByte(addrInstrHeader + 4) == 0
        && readByte(addrInstrHeader + 5) == 0) {
        continue;
      }
    }

    uint8_t srcn = readByte(addrInstrHeader);

    uint32_t offDirEnt = spcDirAddr + (srcn * 4);
    if (offDirEnt + 4 > 0x10000) {
      break;
    }

    uint16_t addrSampStart = readShort(offDirEnt);
    uint16_t addrLoopStart = readShort(offDirEnt + 2);

    if (addrSampStart == 0x0000 && addrLoopStart == 0x0000 ||
        addrSampStart == 0xffff && addrLoopStart == 0xffff) {
      // example: Lemmings - Stage Clear (00 00 00 00)
      // example: Yoshi's Island - Bowser (ff ff ff ff)
      continue;
    }
    if (!NinSnesInstr::isValidHeader(this->rawFile(), version, addrInstrHeader, spcDirAddr, false)) {
      break;
    }
    if (!NinSnesInstr::isValidHeader(this->rawFile(), version, addrInstrHeader, spcDirAddr, true)) {
      continue;
    }

    if (version == NINSNES_EARLIER || version == NINSNES_STANDARD) {
      if (addrSampStart < offDirEnt + 4) {
        continue;
      }
    }

    std::vector<uint8_t>::iterator itrSRCN = find(usedSRCNs.begin(), usedSRCNs.end(), srcn);
    if (itrSRCN == usedSRCNs.end()) {
      usedSRCNs.push_back(srcn);
    }

    NinSnesInstr *newInstr = new NinSnesInstr(
      this, version, addrInstrHeader, instr >> 7, instr & 0x7f,
      spcDirAddr, fmt::format("Instrument {}", instr));
    newInstr->konamiTuningTableAddress = konamiTuningTableAddress;
    newInstr->konamiTuningTableSize = konamiTuningTableSize;
    aInstrs.push_back(newInstr);
  }
  if (aInstrs.size() == 0) {
    return false;
  }

  std::sort(usedSRCNs.begin(), usedSRCNs.end());
  SNESSampColl *newSampColl = nullptr;
  if (version == NINSNES_INTELLI_TA) {
    newSampColl = new SNESSampColl(NinSnesFormat::name, this->rawFile(), spcDirAddr, 0x80);
  }
  else {
    newSampColl = new SNESSampColl(NinSnesFormat::name, this->rawFile(), spcDirAddr, usedSRCNs);
  }
  if (!newSampColl->loadVGMFile()) {
    delete newSampColl;
    return false;
  }

  return true;
}

void NinSnesInstrSet::useColl(const VGMColl* coll) {
  if (coll->seq() == nullptr) {
    return;
  }

  const auto* seq = dynamic_cast<const NinSnesSeq*>(coll->seq());
  if (seq == nullptr || seq->rawFile() != rawFile() || seq->version != version) {
    return;
  }

  if (seq->version == NINSNES_INTELLI_TA || seq->version == NINSNES_INTELLI_FE4) {
    for (const auto& overrideDef : seq->intelliTAInstrumentOverrides()) {
      auto* overrideInstr = new VGMInstr(
          this,
          0,
          NinSnesInstr::expectedSize(version),
          overrideDef.progNum >> 7,
          overrideDef.progNum & 0x7f,
          fmt::format("Instrument {:d} (Overwrite)", overrideDef.logicalInstrIndex));
      auto* rgn =
          createRgnFromHeaderData(overrideInstr, rawFile(), version, spcDirAddr, overrideDef.regionData);
      if (rgn == nullptr) {
        delete overrideInstr;
        continue;
      }
      overrideInstr->addRgn(rgn);
      addTempInstr(overrideInstr);
    }

    for (const auto& drumKitDef : seq->intelliTADrumKitDefs()) {
      auto* drumKit = new VGMInstr(
          this, 0, 0, 127, drumKitDef.program, fmt::format("Drum Kit {:d}", drumKitDef.program));

      for (size_t slot = 0; slot < drumKitDef.slots.size(); slot++) {
        const auto& slotDef = drumKitDef.slots[slot];
        if (!slotDef.active) {
          continue;
        }

        VGMInstr* sourceInstr = findInstrByProgram(exportInstrs(), slotDef.sourceProgNum);
        if (sourceInstr == nullptr) {
          continue;
        }

        const uint8_t drumKey = static_cast<uint8_t>(0x24 + slot);
        for (auto* sourceRgn : sourceInstr->regions()) {
          drumKit->addRgn(
              cloneIntelliTARgnForDrumKit(drumKit, sourceRgn, drumKey, slotDef.playedNoteByte));
        }
      }

      if (drumKit->regions().empty()) {
        delete drumKit;
        continue;
      }

      addTempInstr(drumKit);
    }

    return;
  }

  const auto& percussionInstrNoteMap = seq->percussionInstrNoteMap();
  if (percussionInstrNoteMap.empty()) {
    return;
  }

  // Create the drumkit instrument for percussion note events
  auto* drumKit = new VGMInstr(this, 0, 0, 127, 0, "Drum Kit");
  for (const auto& [instrIndex, percussionDef] : percussionInstrNoteMap) {
    // Get the referenced instrument by checking its instrument number
    VGMInstr* sourceInstr = nullptr;
    for (auto* instr : aInstrs) {
      if (instr->instrNum == instrIndex) {
        sourceInstr = instr;
        break;
      }
    }
    if (sourceInstr == nullptr) {
      continue;
    }

    for (auto* sourceRgn : sourceInstr->regions()) {
      drumKit->addRgn(
          cloneLegacyRgnForDrumKit(drumKit, sourceRgn, percussionDef.noteIndex, percussionDef.globalTranspose));
    }
  }

  if (drumKit->regions().empty()) {
    delete drumKit;
    return;
  }

  addTempInstr(drumKit);
}

// *************
// NinSnesInstr
// *************

NinSnesInstr::NinSnesInstr(VGMInstrSet *instrSet,
                           NinSnesVersion ver,
                           uint32_t offset,
                           uint32_t theBank,
                           uint32_t theInstrNum,
                           uint32_t spcDirAddr,
                           const std::string &name) :
    VGMInstr(instrSet, offset, NinSnesInstr::expectedSize(ver), theBank, theInstrNum, name), version(ver),
    spcDirAddr(spcDirAddr),
    konamiTuningTableAddress(0),
    konamiTuningTableSize(0) {
}

NinSnesInstr::~NinSnesInstr() {
}

bool NinSnesInstr::loadInstr() {
  uint8_t srcn = readByte(offset());
  uint32_t offDirEnt = spcDirAddr + (srcn * 4);
  if (offDirEnt + 4 > 0x10000) {
    return false;
  }

  uint16_t addrSampStart = readShort(offDirEnt);

  NinSnesRgn *rgn = new NinSnesRgn(this, version, offset(), konamiTuningTableAddress, konamiTuningTableSize);
  rgn->sampOffset = addrSampStart - spcDirAddr;
  addRgn(rgn);

  return true;
}

bool NinSnesInstr::isValidHeader(RawFile *file,
                                 NinSnesVersion version,
                                 uint32_t addrInstrHeader,
                                 uint32_t spcDirAddr,
                                 bool validateSample) {
  
  auto instrItemSize = NinSnesInstr::expectedSize(version);

  if (addrInstrHeader + instrItemSize > 0x10000) {
    return false;
  }

  std::vector<uint8_t> instrHeader(instrItemSize);
  file->readBytes(addrInstrHeader, instrItemSize, instrHeader.data());

  if (std::all_of(instrHeader.cbegin(), instrHeader.cend(),
                  [](const uint8_t b) { return b == 0x00 || b == 0xFF; })) {
    return false;
  }

  const uint8_t &srcn = instrHeader[0];
  const uint8_t &adsr1 = instrHeader[1];
  const uint8_t &adsr2 = instrHeader[2];
  const uint8_t &gain = instrHeader[3];

  if (srcn >= 0x80 || (adsr1 == 0 && gain == 0)) {
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

uint32_t NinSnesInstr::expectedSize(NinSnesVersion version) {
  if (version == NINSNES_EARLIER) {
    return 5;
  }
  else {
    return 6;
  }
}

// ***********
// NinSnesRgn
// ***********

NinSnesRgn::NinSnesRgn(NinSnesInstr *instr,
                       NinSnesVersion ver,
                       uint32_t offset,
                       uint16_t konamiTuningTableAddress,
                       uint8_t konamiTuningTableSize) :
    VGMRgn(instr, offset, NinSnesInstr::expectedSize(ver)), version(ver) {
  uint8_t srcn = readByte(offset);
  uint8_t adsr1 = readByte(offset + 1);
  uint8_t adsr2 = readByte(offset + 2);
  uint8_t gain = readByte(offset + 3);
  uint16_t pitch_scale;
  if (version == NINSNES_EARLIER) {
    pitch_scale = (int8_t) readByte(offset + 4) * 256;
  }
  else {
    pitch_scale = getShortBE(offset + 4);
  }

  const double pitch_fixer = 4286.0 / 4096.0;
  double fine_tuning;
  double coarse_tuning;

  // Very large NinSnes pitch multipliers can wrap the SNES DSP's 14-bit pitch instead of acting
  // like a normal transposition. We have only observed this on percussion instruments (ex. SimCity
  // Metropolis), which the driver plays at fixed note 0x24 (base pitch 0x0217). When that happens,
  // assume percussion usage and replace the raw multiplier with the wrapped pitch actually heard at
  // 0x24, expressed again as an equivalent scale factor for SF2/DLS unity key / fine-tune calculation.
  bool wrapsPercussion = (((uint32_t)0x0217 * pitch_scale) >> 8) > 0x3FFF;
  if (wrapsPercussion) {
    pitch_scale = ((((uint32_t)0x0217 * pitch_scale) >> 8) & 0x3FFF) * 256.0 / 0x0217;
  }

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

  addSampNum(srcn, offset, 1);
  addChild(offset + 1, 1, "ADSR1");
  addChild(offset + 2, 1, "ADSR2");
  addChild(offset + 3, 1, "GAIN");
  if (version == NINSNES_EARLIER) {
    addUnityKey(96 - (int) (coarse_tuning), offset + 4, 1);
    addFineTune((int16_t) (fine_tuning * 100.0), offset + 4, 1);
  }
  else if (version == NINSNES_KONAMI && konamiTuningTableAddress != 0) {
    uint16_t addrTuningTableCoarse = konamiTuningTableAddress;
    uint16_t addrTuningTableFine = konamiTuningTableAddress + konamiTuningTableSize;

    int8_t coarse_tuning;
    uint8_t fine_tuning;
    if (srcn < konamiTuningTableSize) {
      coarse_tuning = readByte(addrTuningTableCoarse + srcn);
      fine_tuning = readByte(addrTuningTableFine + srcn);
    }
    else {
      coarse_tuning = 0;
      fine_tuning = 0;
    }

    double fine_tune_real = fine_tuning / 256.0;
    fine_tune_real += log(4045.0 / 4096.0) / log(2) * 12; // -21.691 cents

    unityKey = 71 - coarse_tuning;
    fineTune = (int16_t) (fine_tune_real * 100.0);

    addChild(offset + 4, 2, "Tuning (Unused)");
  }
  else {
    addUnityKey(96 - (int) (coarse_tuning), offset + 4, 1);
    addFineTune((int16_t) (fine_tuning * 100.0), offset + 5, 1);
  }
  snesConvADSR<VGMRgn>(this, adsr1, adsr2, gain);
}

NinSnesRgn::~NinSnesRgn() {}

bool NinSnesRgn::loadRgn() {
  return true;
}
