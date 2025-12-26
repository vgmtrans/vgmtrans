/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#include "KonamiArcadeInstr.h"

#include "K054539.h"
#include "KonamiAdpcm.h"
#include "LogManager.h"
#include "VGMRgn.h"
#include "KonamiArcadeDefinitions.h"
#include "KonamiArcadeFormat.h"

#include <spdlog/fmt/fmt.h>

// The driver doesn't define an envelope at the instrument level. Instead, it sets release time
// via sequence events, which are currently unhandled. Adding artificial release times generally
// makes things sound better.
const double instrReleaseTime = 0.5;
const double drumReleaseTime = 0.7;

// ********************
// KonamiArcadeInstrSet
// ********************

KonamiArcadeInstrSet::KonamiArcadeInstrSet(RawFile *file,
                                           u32 offset,
                                           std::string name,
                                           u32 drumTableOffset,
                                           u32 drumSampleTableOffset,
                                           KonamiArcadeFormatVer fmtVer)
    : VGMInstrSet(KonamiArcadeFormat::name, file, offset, 0, std::move(name)),
      m_drumTableOffset(drumTableOffset), m_drumSampleTableOffset(drumSampleTableOffset),
      m_fmtVer(fmtVer) {
}

void KonamiArcadeInstrSet::addSampleInfoChildren(VGMItem* sampInfoItem, u32 off) {
  sampInfoItem->addChild(off, 3, "Loop Offset");
  sampInfoItem->addChild(off + 3, 3, "Sample Offset");
  std::string sampleTypeStr;
  u8 flagsByte = readByte(off + 6);
  switch (flagsByte & 0xc) {
    case static_cast<int>(k054539_sample_type::PCM_8):
      sampleTypeStr = "PCM 8";
      break;
    case static_cast<int>(k054539_sample_type::PCM_16):
      sampleTypeStr = "PCM 16";
      break;
    case static_cast<int>(k054539_sample_type::ADPCM):
      sampleTypeStr = "ADPCM";
      break;
  }
  if (flagsByte & 0x20) {
    sampleTypeStr += " (Reverse)";
  }
  sampInfoItem->addChild(off + 6, 1, fmt::format("Sample Type: {}", sampleTypeStr));
  sampInfoItem->addChild(off + 7, 1, fmt::format("Loops: {}", readByte(off + 7) > 0 ? "True" : "False"));
  sampInfoItem->addChild(off + 8, 1, "Attenuation");
}

bool KonamiArcadeInstrSet::parseInstrPointers() {

  u32 instrSampleTableOffset = m_fmtVer == MysticWarrior ? readShort(dwOffset) : readWordBE(dwOffset);
  u32 sfxSampleTableOffset = m_fmtVer == MysticWarrior ? readShort(dwOffset + 2) : readWordBE(dwOffset + 4);

  disableAutoAddInstrumentsAsChildren();

  auto instrSampInfos = addChild(instrSampleTableOffset, sfxSampleTableOffset - instrSampleTableOffset,
                                      "Instrument Sample Infos");
  int sampNum = 0;
  for (u32 off = instrSampleTableOffset; off < sfxSampleTableOffset; off += sizeof(konami_mw_sample_info)) {
    int bank = sampNum > 127 ? 1 : 0;
    int instrNum = sampNum > 127 ? sampNum - 128 : sampNum;
    std::string name = fmt::format("Instrument {} Bank {}", instrNum, bank);
    VGMInstr* instr = new VGMInstr(this, off, sizeof(konami_mw_sample_info), bank, instrNum, name);
    VGMRgn* rgn = new VGMRgn(instr, off, sizeof(konami_mw_sample_info));
    rgn->sampNum = sampNum;
    rgn->release_time = instrReleaseTime;
    instr->addRgn(rgn);

    auto instrSampInfoItem = instrSampInfos->addChild(off, sizeof(konami_mw_sample_info),
                                      fmt::format("Instrument Sample Info {}", sampNum));
    addSampleInfoChildren(instrSampInfoItem, off);
    sampNum++;

    aInstrs.push_back(instr);
  }

  if (m_drumTableOffset == 0 || m_drumSampleTableOffset == 0) {
    return true;
  }

  auto drumSampInfos = addChild(m_drumSampleTableOffset, m_drumTableOffset - m_drumSampleTableOffset,
                                      "Drum Sample Infos");
  sampNum = 0;
  for (u32 off = m_drumSampleTableOffset; off < m_drumTableOffset; off += sizeof(konami_mw_sample_info)) {
    auto drumSampInfoItem = drumSampInfos->addChild(off, sizeof(konami_mw_sample_info),
                                                    fmt::format("Drum Sample Info {}", sampNum++));
    addSampleInfoChildren(drumSampInfoItem, off);
  }

  // Load drum table
  int numMelodicInstrs = aInstrs.size();
  readBytes(m_drumTableOffset, sizeof(m_drums), &m_drums);
  VGMInstr* drumInstr = new VGMInstr(
    this,
    m_drumTableOffset,
    sizeof(m_drums),
    2,
    0,
    "Drum Kit"
  );
  std::vector<VGMInstr *> aDrumKit;
  for (int i = 0; i < sizeof(m_drums) / sizeof(drum); ++i) {
    drum& d = m_drums[i];
    u32 off = m_drumTableOffset + i * sizeof(drum);
    int sampNum = numMelodicInstrs + d.samp_num;

    if (d.unity_key >= 0x60) {
      break;
    }

    VGMRgn* rgn = new VGMRgn(drumInstr, off, sizeof(drum), fmt::format("Region {:d}", i));
    // The driver offsets notes up 2 octaves relative to midi note values.
    rgn->keyLow = i + 24;
    rgn->keyHigh = i + 24;
    int unityKey = (i + 24) + (0x2A - d.unity_key);
    rgn->sampNum = sampNum;
    rgn->unityKey = unityKey;
    rgn->release_time = drumReleaseTime;
    rgn->setVolume(volTable[d.attenuation]);

    rgn->addChild(off, 1, "Sample Number");
    rgn->addChild(off + 1, 1, "Unity Key");
    rgn->addChild(off + 2, 1, "Pitch Bend");
    rgn->addChild(off + 3, 1, "Pan");
    rgn->addChild(off + 4, 2, "Unknown");
    rgn->addChild(off + 6, 1, "Default Duration");
    rgn->addChild(off + 7, 1, "Attenuation");
    drumInstr->addRgn(rgn);
  }
  aInstrs.push_back(drumInstr);
  aDrumKit.push_back(drumInstr);
  addChildren(aDrumKit);

  return true;
}


// ********************
// KonamiArcadeSampColl
// ********************

KonamiArcadeSampColl::KonamiArcadeSampColl(
    RawFile* file,
    KonamiArcadeInstrSet* instrset,
    const std::vector<konami_mw_sample_info>& sampInfos,
    u32 offset,
    u32 length,
    std::string name)
    : VGMSampColl(KonamiArcadeFormat::name, file, offset, length, std::move(name)),
      instrset(instrset), sampInfos(sampInfos) {
}

bool KonamiArcadeSampColl::parseHeader() {
  return true;
}

bool KonamiArcadeSampColl::parseSampleInfo() {

  int sampNum = 0;
  for (auto sampInfo : sampInfos) {
    u32 sampleOffset = sampInfo.start_msb << 16 | sampInfo.start_mid << 8 | sampInfo.start_lsb;
    u32 sampleLoopOffset = sampInfo.loop_msb << 16 | sampInfo.loop_mid << 8 | sampInfo.loop_lsb;
    s32 relativeLoopOffset = sampleLoopOffset - sampleOffset;
    u32 sampleSize = determineK054539SampleSize(this, sampleOffset, sampInfo.type(), sampInfo.reverse());
    if (sampInfo.reverse()) {
      sampleOffset = sampleOffset - sampleSize;
      relativeLoopOffset = -relativeLoopOffset;
    }

    auto name = fmt::format("Sample {:d}", sampNum++);
    VGMSamp* sample;
    if (sampInfo.type() == k054539_sample_type::ADPCM) {
      sample = new KonamiAdpcmSamp(
        this,
        sampleOffset,
        sampleSize,
        KonamiAdpcmChip::K054539,
        24000,
        name
      );
      sample->setWaveType(WT_PCM16);
      samples.push_back(sample);
    } else {
      u16 bps = sampInfo.type() == k054539_sample_type::PCM_8 ? 8 : 16;
      sample = addSamp(sampleOffset,
                           sampleSize,
                           sampleOffset,
                           sampleSize,
                           1,
                           bps,
                           24000,
                           name);
      sample->setWaveType(bps == 8 ? WT_PCM8 : WT_PCM16);
    }
    sample->setLoopStatus(sampInfo.loops == 1);
    sample->setLoopOffset(relativeLoopOffset);
    sample->unityKey = 0x3C + 6;
    sample->setVolume(volTable[sampInfo.attenuation]);
    sample->setReverse(sampInfo.reverse());
  }
  return true;
}
