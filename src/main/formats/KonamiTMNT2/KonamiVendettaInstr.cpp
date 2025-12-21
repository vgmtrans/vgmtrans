/*
* VGMTrans (c) 2002-2025
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "KonamiVendettaInstr.h"
#include "KonamiTMNT2Instr.h"
#include "KonamiTMNT2Format.h"
#include "KonamiAdpcm.h"
#include "LogManager.h"
#include "VGMRgn.h"

KonamiVendettaSampleInstrSet::KonamiVendettaSampleInstrSet(
  RawFile *file,
  u32 offset,
  u32 instrTableOffsetYM2151,
  u32 instrTableOffsetK053260,
  u32 sampInfoTableOffset,
  u32 drumBanksOffset,
  std::map<u16, int> drumOffsetToIdx,
  const std::vector<konami_vendetta_instr_k053260>& instrs,
  const std::vector<konami_vendetta_sample_info>& sampInfos,
  const std::vector<konami_vendetta_drum_info>& drumInfos,
  std::string name,
  KonamiTMNT2FormatVer fmtVer
  ) : VGMInstrSet(KonamiTMNT2Format::name, file, offset, 0, std::move(name)),
      m_instrTableOffsetYM2151(instrTableOffsetYM2151),
      m_instrTableOffsetK053260(instrTableOffsetK053260),
      m_sampInfoTableOffset(sampInfoTableOffset),
      m_drumBanksOffset(drumBanksOffset),
      m_drumOffsetToIdx(drumOffsetToIdx),
      m_instrsK053260(instrs),
      m_sampInfos(sampInfos),
      m_drumInfos(drumInfos),
      m_fmtVer(fmtVer)
{
}


void KonamiVendettaSampleInstrSet::addInstrInfoChildren(VGMItem* instrInfoItem, u32 off) {
  std::string sampleTypeStr;
  u8 flagsByte = readByte(off);
  sampleTypeStr = (flagsByte & 0x10) ? "KADPCM" : "PCM 8";

  if (flagsByte & 0x08) {
    sampleTypeStr += " (Reverse)";
  }
  instrInfoItem->addChild(off + 0, 1, fmt::format("Flags - Sample Type: {}", sampleTypeStr));

  instrInfoItem->addChild(off + 1, 2, "Sample Length");
  instrInfoItem->addChild(off + 3, 3, "Sample Offset");
  instrInfoItem->addChild(off + 6, 1, "Volume");
  instrInfoItem->addChild(off + 7, 1, "Note Duration (fractional)");
  instrInfoItem->addChild(off + 8, 1, "Release Duration / Rate?");
  instrInfoItem->addChild(off + 9, 1, "Pan");
}

bool KonamiVendettaSampleInstrSet::parseInstrPointers() {
  disableAutoAddInstrumentsAsChildren();

  if (!parseMelodicInstrs())
    return false;
  if (!parseDrums())
    return false;

  return true;
}

bool KonamiVendettaSampleInstrSet::parseMelodicInstrs() {
  auto instrTableItem = addChild(m_instrTableOffsetK053260, m_instrsK053260.size() * 2, "Instrument Table");
  u16 minInstrOffset = -1;
  u16 maxInstrOffset = 0;
  for (int i = 0; i < m_instrsK053260.size(); ++i) {
    minInstrOffset = std::min(minInstrOffset, readShort(m_instrTableOffsetK053260 + i * 2));
    maxInstrOffset = std::max(maxInstrOffset, readShort(m_instrTableOffsetK053260 + i * 2));
    instrTableItem->addChild(m_instrTableOffsetK053260 + (i * 2), 2, fmt::format("Instr Pointer {}", i));
  }

  auto instrInfosItem = addChild(
    minInstrOffset,
    (maxInstrOffset + sizeof(konami_vendetta_instr_k053260)) - minInstrOffset,
    "Instruments"
  );
  int instrNum = 0;
  for (auto& instrInfo : m_instrsK053260) {
    u32 offset = readShort(m_instrTableOffsetK053260 + instrNum * 2);
    // if (instrInfo.start_hi < 0x20) {     // Sanity check for TMNT2 mess
    //   auto instrInfoItem = instrInfosItem->addChild(
    //     offset,
    //     sizeof(konami_tmnt2_instr_info),
    //     fmt::format("Instrument {}", instrNum)
    //   );
    //   addInstrInfoChildren(instrInfoItem, offset);
    // }
    if (instrInfo.samp_info_idx >= m_sampInfos.size()) {
      instrNum++;
      L_WARN("Invalid sample info index: {:d}", instrInfo.samp_info_idx);
      continue;
    }
    auto sampInfo = m_sampInfos[instrInfo.samp_info_idx];

    std::string name = fmt::format("Instrument {}", instrNum);
    VGMInstr* instr = new VGMInstr(
      this,
      offset,
      sizeof(konami_vendetta_instr_k053260),
      instrNum / 128,
      instrNum % 128,
      name
    );
    VGMRgn* rgn = new VGMRgn(instr, offset, sizeof(konami_vendetta_instr_k053260));
    rgn->sampOffset = sampInfo.start();
    rgn->sampDataLength = sampInfo.length();
    // rgn->setVolume((instrInfo.volume & 0x7F) / 127.0);

    instr->addRgn(rgn);
    aInstrs.push_back(instr);
    instrNum += 1;
  }
  return true;
}

bool KonamiVendettaSampleInstrSet::parseDrums() {
  if (m_drumInfos.size() == 0)
    return false;
  u16 drumsOffset = m_drumOffsetToIdx.begin()->first - 3;
  int numDrumTables = (drumsOffset - m_drumBanksOffset) / 0x20;

  auto drumBanksItem = addChild(m_drumBanksOffset, numDrumTables * 0x20, "Drum Banks");
  u16 minDrumOffset = -1;
  u16 maxDrumOffset = 0;

  // for (int i = 0; i < m_drumTables.size(); ++i) {
  //   u16 ptrOffset = m_drumTableAddr + i * 2;
  //   u16 ptr = readShort(ptrOffset);
  //   minDrumOffset = std::min(minDrumOffset, ptr);
  //   maxDrumOffset = std::max(maxDrumOffset, ptr);
  //   drumOctaveTableItem->addChild(m_drumTableAddr + (i * 2), 2, fmt::format("Drum Table Pointer {}", i));
  // }

  // auto drumTablesItem = addChild(
  //   minDrumOffset,
  //   (maxDrumOffset + sizeof(konami_tmnt2_drum_info)) - minDrumOffset,
  //   "Drum Octaves"
  // );
  auto drumsItem = addChild(
    0,
    unLength,
    "Drums"
  );

  VGMInstr* drumKit = new VGMInstr(this, dwOffset, unLength, 1, 0, "Drum Kit");

  minDrumOffset = -1;
  maxDrumOffset = 0;
  int drumNum = 0;
  for (u32 i = 0; i < numDrumTables; ++i) {
    u16 drumBankPtr = m_drumBanksOffset + (i * 0x20);
    // auto drumOctaveTable = m_drumTables[i];
    auto drumOctaveItem = drumBanksItem->addChild(drumBankPtr, 0x20, fmt::format("Drum Bank {}", i));
    for (int j = 0; j < 16; ++j) {
      u32 ptrOffset = drumBankPtr + j * 2;
      u16 ptr = readShort(ptrOffset);

      if (ptr == 0)
        continue;

      // minDrumOffset = std::min(minDrumOffset, ptr);
      // maxDrumOffset = std::max(maxDrumOffset, ptr);
      drumOctaveItem->addChild(ptrOffset, 2, "Drum Pointer");

      // u8 flagsByte = readByte(ptr + 3);
      // std::string sampleTypeStr = (flagsByte & 0x10) ? "KADPCM" : "PCM 8";
      // if (flagsByte & 0x08) {
      //   sampleTypeStr += " (Reverse)";
      // }

      // std::string name = j == 0 ? "Unreachable Drum" : fmt::format("Drum {}", drumNum);
      // auto drumItem = drumsItem->addChild(ptr, sizeof(konami_tmnt2_drum_info), name);
      // drumItem->addChild(ptr + 0, 2, "Pitch");
      // drumItem->addChild(ptr + 2, 1, "Unknown");
      // drumItem->addChild(ptr + 3, 1, fmt::format("Flags - Sample Type: {}", sampleTypeStr));
      // drumItem->addChild(ptr + 4, 2, "Sample Length");
      // drumItem->addChild(ptr + 6, 3, "Sample Address");
      // drumItem->addChild(ptr + 9, 1, "Volume");
      // drumItem->addChild(ptr + 10, 2, "Note Duration");
      // drumItem->addChild(ptr + 12, 1, "Release Duration / Rate?");
      // drumItem->addChild(ptr + 13, 1, "Pan");
      int drumIdx = m_drumOffsetToIdx[ptr];
      const konami_vendetta_drum_info& drumInfo = m_drumInfos[drumIdx];
      m_drumKeyMap[(i * 16) + j] = drumInfo;

      VGMRgn* rgn = new VGMRgn(drumKit, ptr - 3, 3);//sizeof(konami_tmnt2_drum_info));
      auto sampInfoIdx = drumInfo.instr.samp_info_idx;
      auto sampInfo = m_sampInfos[sampInfoIdx];
      rgn->sampOffset = sampInfo.start();
      rgn->sampDataLength = sampInfo.length();
      u8 key = (i * 16) + j;      // drum tables use base 1
      rgn->keyLow = key;
      rgn->keyHigh = key;
      rgn->unityKey = key;

      s16 pitch = drumInfo.pitch != -1 ? drumInfo.pitch : (sampInfo.pitch_hi << 8) + sampInfo.pitch_lo;
      double relativePitchCents = k053260_pitch_cents(pitch);
      rgn->coarseTune = relativePitchCents / 100;
      rgn->fineTune = static_cast<int>(relativePitchCents) % 100;
      // rgn->setVolume((drumInfo.volume & 0x7F) / 127.0);

      drumKit->addRgn(rgn);
      drumNum += 1;
    }
  }
  aInstrs.emplace_back(drumKit);
  drumsItem->dwOffset = drumsOffset;
  drumsItem->unLength = m_instrTableOffsetYM2151 - drumsOffset;
  // drumsItem->dwOffset = minDrumOffset;
  // drumsItem->unLength = (maxDrumOffset + sizeof(konami_tmnt2_drum_info)) - minDrumOffset;
  return true;
}