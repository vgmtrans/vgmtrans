/*
 * VGMTrans (c) 2002-2025
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "KonamiTMNT2Instr.h"
#include "KonamiTMNT2Format.h"
#include "KonamiAdpcm.h"
#include "KonamiTMNT2Definitions.h"
#include "VGMRgn.h"

#include <set>
#include <tuple>
#include <spdlog/fmt/fmt.h>

KonamiTMNT2SampleInstrSet::KonamiTMNT2SampleInstrSet(
  RawFile *file,
  u32 offset,
  u32 instrTableAddr,
  u32 drumTableAddr,
  const std::vector<konami_tmnt2_instr_info>& instrInfos,
  const std::vector<std::vector<konami_tmnt2_drum_info>>& drumTables,
  std::string name,
  KonamiTMNT2FormatVer fmtVer
)
  : VGMInstrSet(KonamiTMNT2Format::name, file, offset, 0, std::move(name)),
    m_instrTableAddr(instrTableAddr), m_drumTableAddr(drumTableAddr),
    m_instrInfos(instrInfos), m_drumTables(drumTables), m_fmtVer(fmtVer)
{
}


void KonamiTMNT2SampleInstrSet::addInstrInfoChildren(VGMItem* instrInfoItem, u32 off) {
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

bool KonamiTMNT2SampleInstrSet::parseInstrPointers() {
  disableAutoAddInstrumentsAsChildren();

  if (!parseMelodicInstrs())
    return false;
  if (!parseDrums())
    return false;

  return true;
}

bool KonamiTMNT2SampleInstrSet::parseMelodicInstrs() {
  auto instrTableItem = addChild(m_instrTableAddr, m_instrInfos.size() * 2, "Instrument Table");
  u16 minInstrOffset = -1;
  u16 maxInstrOffset = 0;
  for (int i = 0; i < m_instrInfos.size(); ++i) {
    if (m_instrInfos[i].start_hi >= 0x20) {
      instrTableItem->addChild(m_instrTableAddr + (i * 2), 2, "Bad Instr Pointer");
      continue;
    }
    minInstrOffset = std::min(minInstrOffset, readShort(m_instrTableAddr + i * 2));
    maxInstrOffset = std::max(maxInstrOffset, readShort(m_instrTableAddr + i * 2));
    instrTableItem->addChild(m_instrTableAddr + (i * 2), 2, fmt::format("Instrument {} Pointer", i));
  }

  auto instrInfosItem = addChild(
    minInstrOffset,
    (maxInstrOffset + sizeof(konami_tmnt2_instr_info)) - minInstrOffset,
    "Instruments"
  );
  int instrNum = 0;
  for (auto& instrInfo : m_instrInfos) {
    u32 offset = readShort(m_instrTableAddr + instrNum * 2);
    if (instrInfo.start_hi < 0x20) {     // Sanity check for TMNT2 mess
      auto instrInfoItem = instrInfosItem->addChild(
        offset,
        sizeof(konami_tmnt2_instr_info),
        fmt::format("Instrument {}", instrNum)
      );
      addInstrInfoChildren(instrInfoItem, offset);
    }

    std::string name = fmt::format("Instrument {}", instrNum);
    VGMInstr* instr = new VGMInstr(
      this,
      offset,
      sizeof(konami_tmnt2_instr_info),
      instrNum / 128,
      instrNum % 128,
      name
    );
    VGMRgn* rgn = new VGMRgn(instr, offset, sizeof(konami_tmnt2_instr_info));
    rgn->sampOffset = instrInfo.start();
    rgn->sampDataLength = (instrInfo.length_hi << 8) | instrInfo.length_lo;

    instr->addRgn(rgn);
    aInstrs.push_back(instr);
    instrNum += 1;
  }
  return true;
}

double k053260_pitch_cents(uint16_t pitch_word) {
  uint16_t P = pitch_word & 0x0FFF;
  double ratio = TIM2_COUNT / (4096.0 - (double)P);
  return 1200.0 * log2(ratio);      // cents relative to 31,960 Hz @ B3
}

bool KonamiTMNT2SampleInstrSet::parseDrums() {
  auto drumBankTableItem = addChild(m_drumTableAddr, m_drumTables.size() * 2, "Drum Bank Table");
  u16 minDrumOffset = -1;
  u16 maxDrumOffset = 0;
  for (int i = 0; i < m_drumTables.size(); ++i) {
    u16 ptrOffset = m_drumTableAddr + i * 2;
    u16 ptr = readShort(ptrOffset);
    minDrumOffset = std::min(minDrumOffset, ptr);
    maxDrumOffset = std::max(maxDrumOffset, ptr);
    drumBankTableItem->addChild(m_drumTableAddr + (i * 2), 2, fmt::format("Drum Bank {} Pointer", i));
  }

  auto drumBanksItem = addChild(
    minDrumOffset,
    (maxDrumOffset + sizeof(konami_tmnt2_drum_info)) - minDrumOffset,
    "Drum Banks"
  );
  auto drumsItem = addChild(
    0,
    unLength,
    "Drums"
  );

  VGMInstr* drumKit = new VGMInstr(this, dwOffset, unLength, 1, 0, "Drum Kit");

  minDrumOffset = -1;
  maxDrumOffset = 0;
  int drumNum = 0;
  for (u32 i = 0; i < m_drumTables.size(); ++i) {
    u16 drumBankTablePtr = readShort(m_drumTableAddr + i * 2);
    auto drumBankTable = m_drumTables[i];
    auto drumBankItem = drumBanksItem->addChild(drumBankTablePtr, drumBankTable.size() * 2, fmt::format("Drum Bank {}", i));
    for (int j = 0; j < drumBankTable.size(); ++j) {
      u32 ptrOffset = drumBankTablePtr + j * 2;
      u16 ptr = readShort(ptrOffset);
      minDrumOffset = std::min(minDrumOffset, ptr);
      maxDrumOffset = std::max(maxDrumOffset, ptr);
      drumBankItem->addChild(ptrOffset, 2, "Drum Pointer");

      u8 flagsByte = readByte(ptr + 3);
      std::string sampleTypeStr = (flagsByte & 0x10) ? "KADPCM" : "PCM 8";
      if (flagsByte & 0x08) {
        sampleTypeStr += " (Reverse)";
      }

      std::string name = j == 0 ? "Unreachable Drum" : fmt::format("Drum {}", drumNum);
      auto drumItem = drumsItem->addChild(ptr, sizeof(konami_tmnt2_drum_info), name);
      drumItem->addChild(ptr + 0, 2, "Pitch");
      drumItem->addChild(ptr + 2, 1, "Unknown");
      drumItem->addChild(ptr + 3, 1, fmt::format("Flags - Sample Type: {}", sampleTypeStr));
      drumItem->addChild(ptr + 4, 2, "Sample Length");
      drumItem->addChild(ptr + 6, 3, "Sample Address");
      drumItem->addChild(ptr + 9, 1, "Volume");
      drumItem->addChild(ptr + 10, 2, "Note Duration");
      drumItem->addChild(ptr + 12, 1, "Release Duration / Rate?");
      drumItem->addChild(ptr + 13, 1, "Pan");

      const konami_tmnt2_drum_info& drumInfo = m_drumTables[i][j];

      VGMRgn* rgn = new VGMRgn(drumKit, ptr, sizeof(konami_tmnt2_drum_info));
      rgn->sampOffset = drumInfo.start();
      rgn->sampDataLength = (drumInfo.length_hi << 8) | drumInfo.length_lo;
      u8 key = (i * 16) + j;
      rgn->keyLow = key;
      rgn->keyHigh = key;
      rgn->unityKey = key;

      double relativePitchCents = k053260_pitch_cents((drumInfo.pitch_hi << 8) + drumInfo.pitch_lo);
      rgn->coarseTune = relativePitchCents / 100;
      rgn->fineTune = static_cast<int>(relativePitchCents) % 100;

      drumKit->addRgn(rgn);
      drumNum += 1;
    }
  }
  aInstrs.emplace_back(drumKit);
  drumsItem->dwOffset = minDrumOffset;
  drumsItem->unLength = (maxDrumOffset + sizeof(konami_tmnt2_drum_info)) - minDrumOffset;
  return true;
}

KonamiTMNT2SampColl::KonamiTMNT2SampColl(
    RawFile* file,
    const std::vector<sample_info>& sampInfos,
    u32 offset,
    u32 length,
    std::string name)
    : VGMSampColl(KonamiTMNT2Format::name, file, offset, length, std::move(name)),
      m_sampInfos(sampInfos)
{
}

bool KonamiTMNT2SampColl::parseHeader() {
  return true;
}

bool KonamiTMNT2SampColl::parseSampleInfo() {
  int sampNum = 0;
  std::set<std::tuple<u32, u32, bool>> seenSamples;

  for (auto sampInfo : m_sampInfos) {
    u32 sampleOffset = sampInfo.offset;
    u32 sampleSize = sampInfo.length;

    if (sampInfo.isReverse) {
      sampleOffset = sampleOffset - sampleSize;
    }

    auto sampleKey = std::make_tuple(sampleOffset, sampleSize, sampInfo.isReverse);
    if (!seenSamples.insert(sampleKey).second) {
      continue;
    }

    auto name = fmt::format("Sample {:d}", sampNum++);
    VGMSamp* sample;
    if (sampleOffset + sampleSize > unLength) {
      sample = new EmptySamp(this);
    }
    else if (sampInfo.isAdpcm) {
      sample = new KonamiAdpcmSamp(
        this,
        sampleOffset,
        sampleSize,
        KonamiAdpcmChip::K053260,
        K053260_BASE_PCM_RATE,
        name
      );
      sample->setBPS(BPS::PCM16);
      samples.push_back(sample);
    } else {
      sample = addSamp(sampleOffset,
                           sampleSize,
                           sampleOffset,
                           sampleSize,
                           1,
                           BPS::PCM8,
                           K053260_BASE_PCM_RATE,
                           name);
      sample->setSignedness(Signedness::Signed);
    }
    sample->setLoopStatus(false);
    sample->unityKey = 0x3B;
    sample->setReverse(sampInfo.isReverse);
  }
  return true;
}
