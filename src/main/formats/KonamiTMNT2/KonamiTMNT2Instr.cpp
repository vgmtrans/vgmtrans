#include "KonamiTMNT2Instr.h"
#include "KonamiTMNT2Format.h"
#include "KonamiAdpcm.h"
#include "KonamiTMNT2Definitions.h"
#include "VGMRgn.h"

#include <spdlog/fmt/fmt.h>

KonamiTMNT2InstrSet::KonamiTMNT2InstrSet(
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


void KonamiTMNT2InstrSet::addInstrInfoChildren(VGMItem* sampInfoItem, u32 off) {
  std::string sampleTypeStr;
  u8 flagsByte = readByte(off);
  sampleTypeStr = (flagsByte & 0x10) ? "KADPCM" : "PCM 8";

  // if (flagsByte & 0x20) {
    // sampleTypeStr += " (Reverse)";
  // }
  sampInfoItem->addChild(off + 0, 1, fmt::format("Flags - Sample Type: {}", sampleTypeStr));

  sampInfoItem->addChild(off + 1, 2, "Sample Length");
  sampInfoItem->addChild(off + 3, 3, "Sample Offset");
  sampInfoItem->addChild(off + 6, 1, "Volume");
  sampInfoItem->addChild(off + 7, 3, "Unknown");
}

bool KonamiTMNT2InstrSet::parseInstrPointers() {
  disableAutoAddInstrumentsAsChildren();

  if (!parseMelodicInstrs())
    return false;
  if (!parseDrums())
    return false;

  return true;
}

bool KonamiTMNT2InstrSet::parseMelodicInstrs() {
  auto instrTableItem = addChild(m_instrTableAddr, m_instrInfos.size() * 2, "Instrument Table");
  u16 minInstrOffset = -1;
  u16 maxInstrOffset = 0;
  for (int i = 0; i < m_instrInfos.size(); ++i) {
    if (m_instrInfos[i].start_msb >= 0x20) {
      instrTableItem->addChild(m_instrTableAddr + (i * 2), 2, "Bad Instr Pointer");
      continue;
    }
    minInstrOffset = std::min(minInstrOffset, readShort(m_instrTableAddr + i * 2));
    maxInstrOffset = std::max(maxInstrOffset, readShort(m_instrTableAddr + i * 2));
    instrTableItem->addChild(m_instrTableAddr + (i * 2), 2, fmt::format("Instr Pointer {}", i));
  }

  auto instrInfosItem = addChild(
    minInstrOffset,
    (maxInstrOffset + sizeof(konami_tmnt2_instr_info)) - minInstrOffset,
    "Instruments"
  );
  int instrNum = 0;
  for (auto& instrInfo : m_instrInfos) {
    u32 offset = readShort(m_instrTableAddr + instrNum * 2);
    if (instrInfo.start_msb < 0x20) {     // Sanity check for TMNT2 mess
      auto instrInfoItem = instrInfosItem->addChild(
        offset,
        sizeof(konami_tmnt2_instr_info),
        fmt::format("Instrument {}", instrNum)
      );
      addInstrInfoChildren(instrInfoItem, offset);
    }

    std::string name = fmt::format("Instrument {}", instrNum);
    VGMInstr* instr = new VGMInstr(this, offset, sizeof(konami_tmnt2_instr_info), 0, instrNum, name);
    VGMRgn* rgn = new VGMRgn(instr, offset, sizeof(konami_tmnt2_instr_info));
    rgn->sampOffset = instrInfo.start();
    // rgn->sampNum = sampNum;
    // rgn->release_time = 0;
    instr->addRgn(rgn);
    aInstrs.push_back(instr);
    instrNum += 1;
  }
  return true;
}

bool KonamiTMNT2InstrSet::parseDrums() {
  auto drumOctaveTableItem = addChild(m_drumTableAddr, m_drumTables.size() * 2, "Drum Octave Table");
  u16 minDrumOffset = -1;
  u16 maxDrumOffset = 0;
  for (int i = 0; i < m_drumTables.size(); ++i) {
    u16 ptrOffset = m_drumTableAddr + i * 2;
    u16 ptr = readShort(ptrOffset);
    minDrumOffset = std::min(minDrumOffset, ptr);
    maxDrumOffset = std::max(maxDrumOffset, ptr);
    drumOctaveTableItem->addChild(m_drumTableAddr + (i * 2), 2, fmt::format("Drum Table Pointer {}", i));
  }

  auto drumTablesItem = addChild(
    minDrumOffset,
    (maxDrumOffset + sizeof(konami_tmnt2_drum_info)) - minDrumOffset,
    "Drum Octaves"
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
    u16 drumOctaveTablePtr = readShort(m_drumTableAddr + i * 2);
    auto drumOctaveTable = m_drumTables[i];
    // for (int j = 0; j < drumOctaveTable.size(); ++j) {
    auto drumOctaveItem = drumTablesItem->addChild(drumOctaveTablePtr, drumOctaveTable.size() * 2, fmt::format("Drum Octave {}", i));
    for (int j = 0; j < drumOctaveTable.size(); ++j) {
      u32 ptrOffset = drumOctaveTablePtr + j * 2;
      u16 ptr = readShort(ptrOffset);
      minDrumOffset = std::min(minDrumOffset, ptr);
      maxDrumOffset = std::max(maxDrumOffset, ptr);
      drumOctaveItem->addChild(ptrOffset, 2, "Drum Pointer");

      std::string name = j == 0 ? "Unreachable Drum" : fmt::format("Drum {}", drumNum);
      drumsItem->addChild(ptr, sizeof(konami_tmnt2_drum_info), name);

      const konami_tmnt2_drum_info& drumInfo = m_drumTables[i][j];
      // std::string name = fmt::format("Drum {}", drumNum);
      // VGMInstr* instr = new VGMInstr(this, offset, sizeof(konami_tmnt2_instr_info), 0, instrNum, name);
      VGMRgn* rgn = new VGMRgn(drumKit, ptr, sizeof(konami_tmnt2_drum_info));
      rgn->sampOffset = drumInfo.start();
      u8 key = (i * 16) + j;
      rgn->keyLow = key;
      rgn->keyHigh = key;
      rgn->unityKey = key;
      // rgn->sampNum = sampNum;
      // rgn->release_time = 0;
      drumKit->addRgn(rgn);
      drumNum += 1;
    }
  }
  aInstrs.emplace_back(drumKit);
  drumsItem->dwOffset = minDrumOffset;
  drumsItem->unLength = (maxDrumOffset + sizeof(konami_tmnt2_drum_info)) - minDrumOffset;


  // for (auto drumTable : m_drumTables) {
    // addChild(m_drumTableAddr, 2, fmt::format("Drum Octave));
  // }

  // if (m_drumTableOffset == 0 || m_drumSampleTableOffset == 0) {
  //   return true;
  // }
  //
  // auto drumSampInfos = addChild(m_drumSampleTableOffset, m_drumTableOffset - m_drumSampleTableOffset,
  //                                     "Drum Sample Infos");
  // sampNum = 0;
  // for (u32 off = m_drumSampleTableOffset; off < m_drumTableOffset; off += sizeof(konami_mw_sample_info)) {
  //   auto drumSampInfoItem = drumSampInfos->addChild(off, sizeof(konami_mw_sample_info),
  //                                                   fmt::format("Drum Sample Info {}", sampNum++));
  //   addSampleInfoChildren(drumSampInfoItem, off);
  // }
  //
  // // Load drum table
  // int numMelodicInstrs = aInstrs.size();
  // readBytes(m_drumTableOffset, sizeof(m_drums), &m_drums);
  // VGMInstr* drumInstr = new VGMInstr(
  //   this,
  //   m_drumTableOffset,
  //   sizeof(m_drums),
  //   2,
  //   0,
  //   "Drum Kit"
  // );
  // std::vector<VGMInstr *> aDrumKit;
  // for (int i = 0; i < sizeof(m_drums) / sizeof(drum); ++i) {
  //   drum& d = m_drums[i];
  //   u32 off = m_drumTableOffset + i * sizeof(drum);
  //   int sampNum = numMelodicInstrs + d.samp_num;
  //
  //   if (d.unity_key >= 0x60) {
  //     break;
  //   }
  //
  //   VGMRgn* rgn = new VGMRgn(drumInstr, off, sizeof(drum), fmt::format("Region {:d}", i));
  //   // The driver offsets notes up 2 octaves relative to midi note values.
  //   rgn->keyLow = i + 24;
  //   rgn->keyHigh = i + 24;
  //   int unityKey = (i + 24) + (0x2A - d.unity_key);
  //   rgn->sampNum = sampNum;
  //   rgn->unityKey = unityKey;
  //   rgn->release_time = drumReleaseTime;
  //   rgn->setVolume(volTable[d.attenuation]);
  //
  //   rgn->addChild(off, 1, "Sample Number");
  //   rgn->addChild(off + 1, 1, "Unity Key");
  //   rgn->addChild(off + 2, 1, "Pitch Bend");
  //   rgn->addChild(off + 3, 1, "Pan");
  //   rgn->addChild(off + 4, 2, "Unknown");
  //   rgn->addChild(off + 6, 1, "Default Duration");
  //   rgn->addChild(off + 7, 1, "Attenuation");
  //   drumInstr->addRgn(rgn);
  // }
  // aInstrs.push_back(drumInstr);
  // aDrumKit.push_back(drumInstr);
  // addChildren(aDrumKit);

  return true;
}


KonamiTMNT2SampColl::KonamiTMNT2SampColl(
    RawFile* file,
    KonamiTMNT2InstrSet* instrset,
    const std::vector<konami_tmnt2_instr_info>& instrInfos,
    const std::vector<std::vector<konami_tmnt2_drum_info>>& drumTables,
    u32 offset,
    u32 length,
    std::string name)
    : VGMSampColl(KonamiTMNT2Format::name, file, offset, length, std::move(name)),
      instrset(instrset), instrInfos(instrInfos), drumTables(drumTables)
{
}

bool KonamiTMNT2SampColl::parseHeader() {
  return true;
}

bool KonamiTMNT2SampColl::parseSampleInfo() {
  int sampNum = 0;

  std::vector<konami_tmnt2_instr_info> flatDrumInfos;
  for (auto const& inner : drumTables) {
    for (auto const& drumInfo : inner) {
      konami_tmnt2_instr_info drumInstr = {
        drumInfo.flags, drumInfo.length_lo, drumInfo.length_hi,
        drumInfo.start_lo, drumInfo.start_mid, drumInfo.start_hi,
        drumInfo.volume, drumInfo.unknown2, drumInfo.unknown3, drumInfo.unknown4
      };
      flatDrumInfos.emplace_back(drumInstr);
    }
  }

  std::vector<konami_tmnt2_instr_info> allInstrInfos;
  allInstrInfos.reserve(instrInfos.size() + flatDrumInfos.size());
  allInstrInfos.insert(allInstrInfos.end(), instrInfos.begin(), instrInfos.end());
  allInstrInfos.insert(allInstrInfos.end(), flatDrumInfos.begin(), flatDrumInfos.end());

  for (auto instrInfo : allInstrInfos) {
    u32 sampleOffset = instrInfo.start_msb << 16 | instrInfo.start_mid << 8 | instrInfo.start_lsb;
    u32 sampleSize = instrInfo.length_msb << 8 | instrInfo.length_lsb;

    // if (sampInfo.reverse()) {
    // sampleOffset = sampleOffset - sampleSize;
    // relativeLoopOffset = -relativeLoopOffset;
    // }

    auto name = fmt::format("Sample {:d}", sampNum++);
    // auto name = fmt::format("Sample {:d}", sampNum++);
    VGMSamp* sample;
    // if (sampInfo.type() == konami_tmnt2_instr_info::sample_type::ADPCM) {
    if (sampleOffset + sampleSize > unLength) {
      sample = new EmptySamp(this);
    }
    else if (instrInfo.isAdpcm()) {
      sample = new KonamiAdpcmSamp(
        this,
        sampleOffset,
        sampleSize,
        KonamiAdpcmChip::K053260,
        K053260_BASE_PCM_RATE,
        name
      );
      sample->setWaveType(WT_PCM16);
      samples.push_back(sample);
    } else {
      // u16 bps = instrInfo.type() == konami_tmnt2_instr_info::sample_type::PCM_8 ? 8 : 16;
      sample = addSamp(sampleOffset,
                           sampleSize,
                           sampleOffset,
                           sampleSize,
                           1,
                           8,
                           K053260_BASE_PCM_RATE,
                           name);
      sample->setWaveType(WT_PCM8);
      sample->setSignedness(Signedness::Signed);
    }
    // sample->setLoopStatus(sampInfo.loops == 1);
    sample->setLoopStatus(false);
    // sample->setLoopOffset(relativeLoopOffset);
    // sample->unityKey = 0x3C + 6;
    sample->unityKey = 0x3B;
    // sample->setVolume(volTable[sampInfo.attenuation]);
    // sample->setReverse(sampInfo.reverse());
  }
  return true;
}