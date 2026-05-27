/*
* VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#include "base/types.h"
#include "NinSnesInstr.h"
#include "NinSnesSeq.h"
#include "NinSnesVibrato.h"
#include "SNESDSP.h"
#include "VGMColl.h"
#include <spdlog/fmt/fmt.h>

namespace {

constexpr u8 kNinSnesMidiKeyCorrection = 24;

struct NinSnesPitchTuning {
  int unityKey = 96;
  s16 fineTune = 0;
};

u32 getProgramNumber(const VGMInstr* instr) {
  return (instr->bank << 7) | (instr->instrNum & 0x7f);
}

bool usesIntelliTempDrumKitExport(NinSnesProfileId profileId) {
  const auto intelliMode = getNinSnesProfile(profileId).intelliMode;
  return intelliMode == NinSnesIntelliModeId::Ta || intelliMode == NinSnesIntelliModeId::Fe4;
}

void applyVibratoScaling(NinSnesInstrSet* instrSet, const VibratoModulationSpec& spec) {
  // Re-target the shared vibrato modulators to the maxima observed in the matched sequence.
  for (auto* instr : instrSet->exportInstrs()) {
    instr->updateStandardVibratoHandling(spec);
  }
}

VGMInstr* findInstrByProgram(const std::vector<VGMInstr*>& instrs, u32 progNum) {
  for (auto* instr : instrs) {
    if (getProgramNumber(instr) == progNum) {
      return instr;
    }
  }
  return nullptr;
}

u16 readPitchScale(const NinSnesProfile& profile, u8 pitchHigh, u8 pitchLow) {
  if (profile.instrumentLayout == NinSnesInstrumentLayoutId::Earlier5Byte) {
    return static_cast<u16>(static_cast<s8>(pitchHigh) * 256);
  }
  return static_cast<u16>((pitchHigh << 8) | pitchLow);
}

NinSnesPitchTuning calculatePitchTuning(u16 pitchScale) {
  const double pitchFixer = 4286.0 / 4096.0;
  double fineTuning;
  double coarseTuning;

  const bool wrapsPercussion = ((static_cast<u32>(0x0217) * pitchScale) >> 8) > 0x3FFF;
  if (wrapsPercussion) {
    pitchScale = static_cast<u16>(
        (((static_cast<u32>(0x0217) * pitchScale) >> 8) & 0x3FFF) * 256.0 / 0x0217);
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
      .fineTune = static_cast<s16>(fineTuning * 100.0),
  };
}

VGMRgn* cloneLegacyRgnForDrumKit(VGMInstr* instr,
                                 VGMRgn* sourceRgn,
                                 u8 noteIndex,
                                 s8 globalTranspose) {
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
                                    u8 drumKey,
                                    u8 playedNoteByte) {
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
                                u32 spcDirAddr,
                                const std::array<u8, 6>& regionData) {
  const auto& profile = getNinSnesProfile(profileId);
  const u8 srcn = regionData[0];
  const u8 adsr1 = regionData[1];
  const u8 adsr2 = regionData[2];
  const u8 gain = regionData[3];
  const u16 offDirEnt = spcDirAddr + (srcn * 4);

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
                                 u32 offset,
                                 u32 spcDirAddr,
                                 const std::string &name) :
    VGMInstrSet(NinSnesFormat::name, file, offset, 0, name),
    signature(NinSnesSignatureId::None),
    profileId(profile),
    konamiTuningTableAddress(0),
    konamiTuningTableSize(0),
    spcDirAddr(spcDirAddr) {
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
  const u16 instrCount = getNinSnesInstrumentSlotCount(profile);
  if (instrCount == 0) {
    return false;
  }

  usedSRCNs.clear();
  for (u16 instr = 0; instr < instrCount; instr++) {
    u32 instrItemSize = NinSnesInstr::expectedSize(profileId);
    u32 addrInstrHeader = offset() + (instrItemSize * instr);
    if (addrInstrHeader + instrItemSize > 0x10000) {
      return false;
    }

    if (isBlankNinSnesInstrumentSlot(profile, this->rawFile(), addrInstrHeader)) {
      continue;
    }

    u8 srcn = readByte(addrInstrHeader);

    u32 offDirEnt = spcDirAddr + (srcn * 4);
    if (offDirEnt + 4 > 0x10000) {
      break;
    }

    u16 addrSampStart = readShort(offDirEnt);
    u16 addrLoopStart = readShort(offDirEnt + 2);

    if ((addrSampStart == 0x0000 && addrLoopStart == 0x0000) ||
        (addrSampStart == 0xffff && addrLoopStart == 0xffff)) {
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

    std::vector<u8>::iterator itrSRCN = find(usedSRCNs.begin(), usedSRCNs.end(), srcn);
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
  const auto* seq = dynamic_cast<const NinSnesSeq*>(coll != nullptr ? coll->seq() : nullptr);
  if (seq == nullptr || seq->rawFile() != rawFile() || seq->profileId != profileId) {
    applyVibratoScaling(this, nin_snes::vibrato::modulationSpec());
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
      overrideInstr->addStandardVibratoHandling(nin_snes::vibrato::modulationSpec());
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
      drumKit->addStandardVibratoHandling(nin_snes::vibrato::modulationSpec());

      for (size_t slot = 0; slot < drumKitDef.slots.size(); slot++) {
        const auto& slotDef = drumKitDef.slots[slot];
        if (!slotDef.active) {
          continue;
        }

        VGMInstr* sourceInstr = findInstrByProgram(exportInstrs(), slotDef.sourceProgNum);
        if (sourceInstr == nullptr) {
          continue;
        }

        const u8 drumKey = static_cast<u8>(0x24 + slot);
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
  } else {
    const auto& percussionInstrNoteMap = seq->percussionInstrNoteMap();
    if (!percussionInstrNoteMap.empty()) {
      // Create the drumkit instrument for percussion note events.
      auto* drumKit = new VGMInstr(this, 0, 0, 127, 0, "Drum Kit");
      drumKit->addStandardVibratoHandling(nin_snes::vibrato::modulationSpec());
      for (const auto& [instrIndex, percussionDef] : percussionInstrNoteMap) {
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
          drumKit->addRgn(cloneLegacyRgnForDrumKit(drumKit,
                                                   sourceRgn,
                                                   percussionDef.noteIndex,
                                                   percussionDef.globalTranspose));
        }
      }

      if (drumKit->regions().empty()) {
        delete drumKit;
      } else {
        addTempInstr(drumKit);
      }
    }
  }

  applyVibratoScaling(this,
                            nin_snes::vibrato::modulationSpec(seq->maxVibratoDepthCents, seq->maxVibratoRateHz));
}

void NinSnesInstrSet::unuseColl() {
  applyVibratoScaling(this, nin_snes::vibrato::modulationSpec());
}

// *************
// NinSnesInstr
// *************

NinSnesInstr::NinSnesInstr(VGMInstrSet *instrSet,
                           NinSnesProfileId profile,
                           u32 offset,
                           u32 theBank,
                           u32 theInstrNum,
                           u32 spcDirAddr,
                           const std::string &name) :
    VGMInstr(instrSet, offset, NinSnesInstr::expectedSize(profile), theBank, theInstrNum, name),
    profileId(profile),
    konamiTuningTableAddress(0),
    konamiTuningTableSize(0),
    spcDirAddr(spcDirAddr) {
}

NinSnesInstr::~NinSnesInstr() {
}

bool NinSnesInstr::loadInstr() {
  u8 srcn = readByte(offset());
  u32 offDirEnt = spcDirAddr + (srcn * 4);
  if (offDirEnt + 4 > 0x10000) {
    return false;
  }

  addStandardVibratoHandling(nin_snes::vibrato::modulationSpec());

  u16 addrSampStart = readShort(offDirEnt);

  NinSnesRgn *rgn = new NinSnesRgn(this, profileId, offset(), konamiTuningTableAddress, konamiTuningTableSize);
  rgn->sampOffset = addrSampStart - spcDirAddr;
  addRgn(rgn);

  return true;
}

bool NinSnesInstr::isValidHeader(RawFile *file,
                                 NinSnesProfileId profileId,
                                 u32 addrInstrHeader,
                                 u32 spcDirAddr,
                                 bool validateSample) {
  return isValidNinSnesInstrumentHeader(
      getNinSnesProfile(profileId), file, addrInstrHeader, spcDirAddr, validateSample);
}

u32 NinSnesInstr::expectedSize(NinSnesProfileId profileId) {
  return getNinSnesInstrumentHeaderSize(getNinSnesProfile(profileId));
}

// ***********
// NinSnesRgn
// ***********

NinSnesRgn::NinSnesRgn(NinSnesInstr *instr,
                       NinSnesProfileId profileId,
                       u32 offset,
                       u16 konamiTuningTableAddress,
                       u8 konamiTuningTableSize) :
    VGMRgn(instr, offset, NinSnesInstr::expectedSize(profileId)) {
  const auto& profile = getNinSnesProfile(profileId);
  u8 srcn = readByte(offset);
  u8 adsr1 = readByte(offset + 1);
  u8 adsr2 = readByte(offset + 2);
  u8 gain = readByte(offset + 3);
  const u8 pitchHigh = readByte(offset + 4);
  const bool earlierLayout = profile.instrumentLayout == NinSnesInstrumentLayoutId::Earlier5Byte;
  const u8 pitchLow = earlierLayout ? 0 : readByte(offset + 5);
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
    u16 addrTuningTableCoarse = konamiTuningTableAddress;
    u16 addrTuningTableFine = konamiTuningTableAddress + konamiTuningTableSize;

    s8 coarse_tuning;
    u8 fine_tuning;
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
    fineTune = static_cast<s16>(fine_tune_real * 100.0);

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
