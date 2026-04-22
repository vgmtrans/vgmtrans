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

struct NinSnesPitchTuning {
  int unityKey = 96;
  int16_t fineTune = 0;
};

uint32_t getProgramNumber(const VGMInstr* instr) {
  return (instr->bank << 7) | (instr->instrNum & 0x7f);
}

bool usesIntelliTempDrumKitExport(NinSnesProfileId profileId) {
  const auto intelliMode = getNinSnesProfile(profileId).intelliMode;
  return intelliMode == NinSnesIntelliModeId::Ta || intelliMode == NinSnesIntelliModeId::Fe4;
}

VGMInstr* findInstrByProgram(const std::vector<VGMInstr*>& instrs, uint32_t progNum) {
  for (auto* instr : instrs) {
    if (getProgramNumber(instr) == progNum) {
      return instr;
    }
  }
  return nullptr;
}

uint16_t readPitchScale(const NinSnesProfile& profile, uint8_t pitchHigh, uint8_t pitchLow) {
  if (profile.instrumentLayout == NinSnesInstrumentLayoutId::Earlier5Byte) {
    return static_cast<uint16_t>(static_cast<int8_t>(pitchHigh) * 256);
  }
  return static_cast<uint16_t>((pitchHigh << 8) | pitchLow);
}

NinSnesPitchTuning calculatePitchTuning(uint16_t pitchScale) {
  const double pitchFixer = 4286.0 / 4096.0;
  double fineTuning;
  double coarseTuning;

  const bool wrapsPercussion = (((uint32_t)0x0217 * pitchScale) >> 8) > 0x3FFF;
  if (wrapsPercussion) {
    pitchScale = ((((uint32_t)0x0217 * pitchScale) >> 8) & 0x3FFF) * 256.0 / 0x0217;
  }

  fineTuning = modf((log(pitchScale * pitchFixer / 256.0) / log(2.0)) * 12.0, &coarseTuning);

  if (fineTuning >= 0.5) {
    coarseTuning += 1.0;
    fineTuning -= 1.0;
  }
  else if (fineTuning <= -0.5) {
    coarseTuning -= 1.0;
    fineTuning += 1.0;
  }

  return {
      .unityKey = 96 - static_cast<int>(coarseTuning),
      .fineTune = static_cast<int16_t>(fineTuning * 100.0),
  };
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
                                NinSnesProfileId profileId,
                                uint32_t spcDirAddr,
                                const std::array<uint8_t, 6>& regionData) {
  const auto& profile = getNinSnesProfile(profileId);
  const uint8_t srcn = regionData[0];
  const uint8_t adsr1 = regionData[1];
  const uint8_t adsr2 = regionData[2];
  const uint8_t gain = regionData[3];
  const uint16_t offDirEnt = spcDirAddr + (srcn * 4);

  if (offDirEnt + 4 > 0x10000 || !SNESSampColl::isValidSampleDir(rawFile, offDirEnt, true)) {
    return nullptr;
  }

  const auto tuning = calculatePitchTuning(readPitchScale(profile, regionData[4], regionData[5]));

  auto* rgn = new VGMRgn(instr, 0, NinSnesInstr::expectedSize(profileId));
  rgn->sampOffset = rawFile->readShort(offDirEnt) - spcDirAddr;
  rgn->sampNum = srcn;
  rgn->unityKey = tuning.unityKey;
  rgn->fineTune = tuning.fineTune;
  snesConvADSR<VGMRgn>(rgn, adsr1, adsr2, gain);
  return rgn;
}

}  // namespace

// ****************
// NinSnesInstrSet
// ****************

NinSnesInstrSet::NinSnesInstrSet(RawFile *file,
                                 NinSnesProfileId profile,
                                 uint32_t offset,
                                 uint32_t spcDirAddr,
                                 const std::string &name) :
    VGMInstrSet(NinSnesFormat::name, file, offset, 0, name),
    signature(NinSnesSignatureId::None),
    profileId(profile),
    spcDirAddr(spcDirAddr),
    konamiTuningTableAddress(0),
    konamiTuningTableSize(0) {
}

NinSnesInstrSet::NinSnesInstrSet(RawFile* file, const NinSnesScanResult& scanResult)
    : NinSnesInstrSet(file,
                      scanResult.profile,
                      scanResult.instrTableAddr,
                      scanResult.spcDirAddr,
                      "NinSnesInstrSet") {
  signature = scanResult.signature;
  profileId = scanResult.profile;
  konamiTuningTableAddress = scanResult.konamiTuningTableAddress;
  konamiTuningTableSize = scanResult.konamiTuningTableSize;
}

NinSnesInstrSet::~NinSnesInstrSet() {
}

bool NinSnesInstrSet::parseHeader() {
  return true;
}

bool NinSnesInstrSet::parseInstrPointers() {
  const auto& profile = getNinSnesProfile(profileId);
  const uint16_t instrCount = getNinSnesInstrumentSlotCount(profile);
  if (instrCount == 0) {
    return false;
  }

  usedSRCNs.clear();
  for (uint16_t instr = 0; instr < instrCount; instr++) {
    uint32_t instrItemSize = NinSnesInstr::expectedSize(profileId);
    uint32_t addrInstrHeader = offset() + (instrItemSize * instr);
    if (addrInstrHeader + instrItemSize > 0x10000) {
      return false;
    }

    if (isBlankNinSnesInstrumentSlot(profile, this->rawFile(), addrInstrHeader)) {
      continue;
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
    if (!NinSnesInstr::isValidHeader(this->rawFile(), profileId, addrInstrHeader, spcDirAddr, false)) {
      break;
    }
    if (!NinSnesInstr::isValidHeader(this->rawFile(), profileId, addrInstrHeader, spcDirAddr, true)) {
      continue;
    }

    if (requiresNinSnesSampleStartAfterDirEntry(profile)) {
      if (addrSampStart < offDirEnt + 4) {
        continue;
      }
    }

    std::vector<uint8_t>::iterator itrSRCN = find(usedSRCNs.begin(), usedSRCNs.end(), srcn);
    if (itrSRCN == usedSRCNs.end()) {
      usedSRCNs.push_back(srcn);
    }

    NinSnesInstr *newInstr = new NinSnesInstr(
      this, profileId, addrInstrHeader, instr >> 7, instr & 0x7f,
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
  if (loadsFullNinSnesSampleDirectory(profile)) {
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
  if (seq == nullptr || seq->rawFile() != rawFile() || seq->profileId != profileId) {
    return;
  }

  if (usesIntelliTempDrumKitExport(seq->profileId)) {
    for (const auto& overrideDef : seq->intelliTAInstrumentOverrides()) {
      auto* overrideInstr = new VGMInstr(
          this,
          0,
          NinSnesInstr::expectedSize(profileId),
          overrideDef.progNum >> 7,
          overrideDef.progNum & 0x7f,
          fmt::format("Instrument {:d} (Overwrite)", overrideDef.logicalInstrIndex));
      auto* rgn =
          createRgnFromHeaderData(overrideInstr, rawFile(), profileId, spcDirAddr, overrideDef.regionData);
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
                           NinSnesProfileId profile,
                           uint32_t offset,
                           uint32_t theBank,
                           uint32_t theInstrNum,
                           uint32_t spcDirAddr,
                           const std::string &name) :
    VGMInstr(instrSet, offset, NinSnesInstr::expectedSize(profile), theBank, theInstrNum, name), profileId(profile),
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

  NinSnesRgn *rgn = new NinSnesRgn(this, profileId, offset(), konamiTuningTableAddress, konamiTuningTableSize);
  rgn->sampOffset = addrSampStart - spcDirAddr;
  addRgn(rgn);

  return true;
}

bool NinSnesInstr::isValidHeader(RawFile *file,
                                 NinSnesProfileId profileId,
                                 uint32_t addrInstrHeader,
                                 uint32_t spcDirAddr,
                                 bool validateSample) {
  return isValidNinSnesInstrumentHeader(
      getNinSnesProfile(profileId), file, addrInstrHeader, spcDirAddr, validateSample);
}

uint32_t NinSnesInstr::expectedSize(NinSnesProfileId profileId) {
  return getNinSnesInstrumentHeaderSize(getNinSnesProfile(profileId));
}

// ***********
// NinSnesRgn
// ***********

NinSnesRgn::NinSnesRgn(NinSnesInstr *instr,
                       NinSnesProfileId profileId,
                       uint32_t offset,
                       uint16_t konamiTuningTableAddress,
                       uint8_t konamiTuningTableSize) :
    VGMRgn(instr, offset, NinSnesInstr::expectedSize(profileId)) {
  const auto& profile = getNinSnesProfile(profileId);
  uint8_t srcn = readByte(offset);
  uint8_t adsr1 = readByte(offset + 1);
  uint8_t adsr2 = readByte(offset + 2);
  uint8_t gain = readByte(offset + 3);
  const uint8_t pitchHigh = readByte(offset + 4);
  const bool earlierLayout = profile.instrumentLayout == NinSnesInstrumentLayoutId::Earlier5Byte;
  const uint8_t pitchLow = earlierLayout ? 0 : readByte(offset + 5);
  const auto tuning = calculatePitchTuning(readPitchScale(profile, pitchHigh, pitchLow));

  addSampNum(srcn, offset, 1);
  addChild(offset + 1, 1, "ADSR1");
  addChild(offset + 2, 1, "ADSR2");
  addChild(offset + 3, 1, "GAIN");
  if (earlierLayout) {
    addUnityKey(tuning.unityKey, offset + 4, 1);
    addFineTune(tuning.fineTune, offset + 4, 1);
  }
  else if (profile.instrumentLayout == NinSnesInstrumentLayoutId::KonamiTuningTable &&
           konamiTuningTableAddress != 0) {
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
    addUnityKey(tuning.unityKey, offset + 4, 1);
    addFineTune(tuning.fineTune, offset + 5, 1);
  }
  snesConvADSR<VGMRgn>(this, adsr1, adsr2, gain);
}

NinSnesRgn::~NinSnesRgn() {}

bool NinSnesRgn::loadRgn() {
  return true;
}
