/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "KonamiSnesInstr.h"

#include "base/Types.h"
#include "KonamiSnesSeq.h"
#include "KonamiSnesVibrato.h"
#include "SNESDSP.h"
#include "VGMColl.h"

#include <algorithm>

#include <spdlog/fmt/fmt.h>

namespace {
constexpr u8 kKonamiSnesPercussionNoteCount = 0x60;
constexpr u8 kKonamiSnesPercussionBaseNote = 0x3c;

struct PercussionHeader {
  u8 note;
  u32 offset;
};

constexpr bool usesLegacyInstrumentLayout(KonamiSnesVersion version) {
  return version >= KONAMISNES_V1 && version <= KONAMISNES_V3;
}

constexpr bool usesLegacyPanRange(KonamiSnesVersion version) {
  return version == KONAMISNES_V1 || version == KONAMISNES_V2;
}

constexpr u8 percussionPanLimit(KonamiSnesVersion version) {
  return usesLegacyPanRange(version) ? 0x14 : 0x28;
}

// Rewrites the shared vibrato modulators to the sequence-specific maxima collected on the first
// pass, while keeping the controller mapping itself identical across instrument loads.
void applyVibratoExportScaling(KonamiSnesInstrSet* instrSet, u8 maxDepth, u16 maxRateFactor) {
  const auto spec = konami_snes::vibrato::modulationSpec(instrSet->version, maxDepth, maxRateFactor);
  for (auto* instr : instrSet->exportInstrs()) {
    instr->updateStandardVibratoHandling(spec);
  }
}

int getPercussionKey(RawFile *file, u32 addrInstrHeader) {
  const s8 rawKey = file->readByte(addrInstrHeader + 1);
  const s8 tuning = file->readByte(addrInstrHeader + 2);
  return (tuning >= 0) ? rawKey : (rawKey - 1);
}

// Legacy percussion tables can be shorter than the nominal 0x60 slots. Clamp the scan
// to entries that still look like sane drum definitions so we do not run into SPC code.
bool isValidPercussionHeader(RawFile *file,
                             KonamiSnesVersion version,
                             u32 addrInstrHeader,
                             u32 spcDirAddr) {
  if (!KonamiSnesInstr::isValidHeader(file, version, addrInstrHeader, spcDirAddr, true)) {
    return false;
  }

  const bool legacyLayout = usesLegacyInstrumentLayout(version);
  const u8 pan = file->readByte(addrInstrHeader + (legacyLayout ? 6 : 5));
  const u8 vol = file->readByte(addrInstrHeader + (legacyLayout ? 7 : 6));
  return pan <= percussionPanLimit(version) && vol <= 0x7f;
}

std::vector<PercussionHeader> collectPercussionHeaders(RawFile *file,
                                                       KonamiSnesVersion version,
                                                       u32 tableOffset,
                                                       u32 spcDirAddr) {
  std::vector<PercussionHeader> headers;
  headers.reserve(kKonamiSnesPercussionNoteCount);
  const u32 instrItemSize = KonamiSnesInstr::expectedSize(version);
  for (u8 percussionNote = 0; percussionNote < kKonamiSnesPercussionNoteCount; percussionNote++) {
    const u32 addrInstrHeader = tableOffset + (instrItemSize * percussionNote);
    if (addrInstrHeader + instrItemSize > 0x10000) {
      break;
    }

    if (!isValidPercussionHeader(file, version, addrInstrHeader, spcDirAddr)) {
      const bool legacyLayout = usesLegacyInstrumentLayout(version);
      const u8 pan = file->readByte(addrInstrHeader + (legacyLayout ? 6 : 5));
      // Bad SRCNs can appear inside a real drum table, but once pan goes out of range
      // or the key on a bad header becomes implausible, we have likely hit non-table data.
      if (pan > percussionPanLimit(version)) {
        break;
      }
      const int key = getPercussionKey(file, addrInstrHeader);
      if (!KonamiSnesInstr::isValidHeader(file, version, addrInstrHeader, spcDirAddr, true) &&
          (key < -40 || key > 40)) {
        break;
      }
      continue;
    }

    headers.push_back({percussionNote, addrInstrHeader});
  }
  return headers;
}
}

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
                                       u32 offset,
                                       u32 bankedInstrOffset,
                                       u8 firstBankedInstr,
                                       u32 percInstrOffset,
                                       u32 spcDirAddr,
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
  auto addUsedSRCN = [this](u8 srcn) {
    if (std::find(usedSRCNs.begin(), usedSRCNs.end(), srcn) == usedSRCNs.end()) {
      usedSRCNs.push_back(srcn);
    }
  };
  const u32 instrItemSize = KonamiSnesInstr::expectedSize(version);

  for (int instr = 0; instr <= 0xff; instr++) {
    const u32 addrInstrHeader = (instr < firstBankedInstr)
                                         ? offset() + (instrItemSize * instr)
                                         : bankedInstrOffset + (instrItemSize * (instr - firstBankedInstr));
    if (addrInstrHeader + instrItemSize > 0x10000) {
      return false;
    }

    // The banked table is often partially filled, so use the lighter structural check to
    // decide where it ends and the stricter sample check to decide what is exportable.
    if (!KonamiSnesInstr::isValidHeader(this->rawFile(), version, addrInstrHeader, spcDirAddr, false)) {
      if (instr < firstBankedInstr) {
        continue;
      }
      break;
    }
    if (!KonamiSnesInstr::isValidHeader(this->rawFile(), version, addrInstrHeader, spcDirAddr, true)) {
      continue;
    }

    u8 srcn = readByte(addrInstrHeader);

    u32 offDirEnt = spcDirAddr + (srcn * 4);
    u16 addrSampStart = readShort(offDirEnt);
    if (addrSampStart < offDirEnt + 4) {
      continue;
    }

    addUsedSRCN(srcn);

    KonamiSnesInstr *newInstr = new KonamiSnesInstr(
      this, version, addrInstrHeader, instr >> 7, instr & 0x7f,
      spcDirAddr, false, fmt::format("Instrument {}", instr));
    aInstrs.push_back(newInstr);
  }

  const auto percussionHeaders = collectPercussionHeaders(this->rawFile(), version, percInstrOffset, spcDirAddr);
  for (const auto &header : percussionHeaders) {
    // The first byte of the percussion instr data is the sample index
    addUsedSRCN(readByte(header.offset));
  }
  if (!percussionHeaders.empty()) {
    auto *newInstr = new KonamiSnesInstr(this,
                                         version,
                                         percInstrOffset,
                                         DRUMKIT_PROGRAM >> 7,
                                         DRUMKIT_PROGRAM & 0x7f,
                                         spcDirAddr,
                                         true,
                                         "Percussion");
    aInstrs.push_back(newInstr);
  }

  if (aInstrs.empty()) {
    return false;
  }

  std::sort(usedSRCNs.begin(), usedSRCNs.end());
  SNESSampColl *newSampColl = new SNESSampColl(KonamiSnesFormat::name, this->rawFile(), spcDirAddr, usedSRCNs);
  if (!newSampColl->loadVGMFile()) {
    delete newSampColl;
    return false;
  }

  return true;
}

void KonamiSnesInstrSet::useColl(const VGMColl* coll) {
  u8 maxVibratoDepth = konami_snes::kDefaultVibratoMaxDepth;
  u16 maxVibratoRateFactor = konami_snes::vibrato::defaultMaxRateFactor(version);

  if (coll != nullptr && coll->seq() != nullptr) {
    const auto* seq = dynamic_cast<const KonamiSnesSeq*>(coll->seq());
    if (seq != nullptr && seq->rawFile() == rawFile() && seq->version == version) {
      maxVibratoDepth = seq->maxVibratoDepth;
      maxVibratoRateFactor = seq->maxVibratoRateFactor;
    }
  }

  applyVibratoExportScaling(this, maxVibratoDepth, maxVibratoRateFactor);
}

void KonamiSnesInstrSet::unuseColl() {
  applyVibratoExportScaling(this,
                            konami_snes::kDefaultVibratoMaxDepth,
                            konami_snes::vibrato::defaultMaxRateFactor(version));
}

// ***************
// KonamiSnesInstr
// ***************

KonamiSnesInstr::KonamiSnesInstr(VGMInstrSet *instrSet,
                                 KonamiSnesVersion ver,
                                 u32 offset,
                                 u32 theBank,
                                 u32 theInstrNum,
                                 u32 spcDirAddr,
                                 bool percussion,
                                 const std::string &name) :
    VGMInstr(instrSet, offset, KonamiSnesInstr::expectedSize(ver), theBank, theInstrNum, name),
    version(ver),
    spcDirAddr(spcDirAddr),
    percussion(percussion) {
}

KonamiSnesInstr::~KonamiSnesInstr() {
}

bool KonamiSnesInstr::loadInstr() {
  addStandardVibratoHandling(konami_snes::vibrato::modulationSpec(version));

  if (percussion) {
    const auto percussionHeaders = collectPercussionHeaders(rawFile(), version, offset(), spcDirAddr);
    for (const auto &header : percussionHeaders) {
      const u8 srcn = readByte(header.offset);
      const u32 offDirEnt = spcDirAddr + (srcn * 4);
      const u16 addrSampStart = readShort(offDirEnt);

      auto *rgn = new KonamiSnesRgn(this, version, header.offset, true, header.note);
      rgn->sampOffset = addrSampStart - spcDirAddr;
      if (!rgn->loadRgn()) {
        delete rgn;
        return false;
      }

      addRgn(rgn);
    }
    setGuessedLength();
    return !percussionHeaders.empty() && !regions().empty();
  }

  u8 srcn = readByte(offset());
  u32 offDirEnt = spcDirAddr + (srcn * 4);
  if (offDirEnt + 4 > 0x10000) {
    return false;
  }

  u16 addrSampStart = readShort(offDirEnt);

  KonamiSnesRgn *rgn = new KonamiSnesRgn(this, version, offset(), percussion);
  rgn->sampOffset = addrSampStart - spcDirAddr;
  addRgn(rgn);

  setGuessedLength();
  return true;
}

bool KonamiSnesInstr::isValidHeader(RawFile *file,
                                    KonamiSnesVersion version,
                                    u32 addrInstrHeader,
                                    u32 spcDirAddr,
                                    bool validateSample) {
  size_t instrItemSize = KonamiSnesInstr::expectedSize(version);

  if (addrInstrHeader + instrItemSize > 0x10000) {
    return false;
  }

  u8 srcn = file->readByte(addrInstrHeader);
  if (srcn == 0xff) // SRCN:FF is false-positive in 99.999999% of cases
  {
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

u32 KonamiSnesInstr::expectedSize(KonamiSnesVersion version) {
  return usesLegacyInstrumentLayout(version) ? 8 : 7;
}

// *************
// KonamiSnesRgn
// *************

KonamiSnesRgn::KonamiSnesRgn(KonamiSnesInstr *instr,
                             KonamiSnesVersion ver,
                             u32 offset,
                             bool percussion,
                             u8 percussionNote) :
    VGMRgn(instr, offset, KonamiSnesInstr::expectedSize(ver)) {
  const bool legacyLayout = usesLegacyInstrumentLayout(ver);
  const u8 srcn = readByte(offset);
  const s8 raw_key = readByte(offset + 1);
  const s8 tuning = readByte(offset + 2);
  const u8 adsr1 = readByte(offset + 3);
  const u8 adsr2 = readByte(offset + 4);
  const u8 gain = legacyLayout ? readByte(offset + 5) : adsr2;
  const u32 panOffset = legacyLayout ? offset + 6 : offset + 5;
  const u32 volOffset = legacyLayout ? offset + 7 : offset + 6;
  const u8 vol = readByte(volOffset);

  const s8 key = (tuning >= 0) ? raw_key : (raw_key - 1);
  const s16 full_tuning = static_cast<s16>((static_cast<u8>(key) << 8) | static_cast<u8>(tuning));

  const bool use_adsr = ((adsr1 & 0x80) != 0);

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
  int unityKey = 72 - static_cast<int>(coarse_tuning);
  if (percussion) {
    keyLow = percussionNote;
    keyHigh = percussionNote;
    // Percussion notes always force SPC note $3c after loading the per-note instrument,
    // so move the exported drumkit region's unity key by the slot index delta.
    unityKey += static_cast<int>(percussionNote) - kKonamiSnesPercussionBaseNote;
  }
  addUnityKey(static_cast<u8>(std::clamp(unityKey, 0, 127)), offset + 1, 1);
  addFineTune((s16) (fine_tuning * 100.0), offset + 2, 1);
  addChild(offset + 3, 1, "ADSR1");
  if (legacyLayout) {
    addChild(offset + 4, 1, "ADSR2");
    addChild(offset + 5, 1, "GAIN");
  }
  else {
    addChild(offset + 4, 1, use_adsr ? "ADSR2" : "GAIN");
  }
  addChild(panOffset, 1, "Pan");
  // volume is *decreased* by final volume value
  // so it is impossible to convert it in 100% accuracy
  // the following value 72.0 is chosen as a "average channel volume level (before pan processing)"
  addVolume(std::max(1.0 - (vol / 72.0), 0.0), volOffset);
  snesConvADSR<VGMRgn>(this, adsr1, adsr2, gain);
}

KonamiSnesRgn::~KonamiSnesRgn() {}

bool KonamiSnesRgn::loadRgn() {
  return true;
}
