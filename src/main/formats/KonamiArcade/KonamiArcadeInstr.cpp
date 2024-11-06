/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#include "KonamiArcadeInstr.h"
#include "KonamiAdpcm.h"
#include "KonamiArcadeFormat.h"
#include "LogManager.h"
#include "VGMRgn.h"
#include <spdlog/fmt/fmt.h>

// ********************
// KonamiArcadeInstrSet
// ********************

KonamiArcadeInstrSet::KonamiArcadeInstrSet(RawFile *file,
                                       uint32_t offset,
                                       std::string name)
    : VGMInstrSet(KonamiArcadeFormat::name, file, offset, 0, std::move(name)) {
}

bool KonamiArcadeInstrSet::parseInstrPointers() {

  u32 instrSampleTableOffset = readShort(dwOffset);
  u32 sfxSampleTableOffset = readShort(dwOffset+2);

  int sampNum = 0;
  for (u32 off = instrSampleTableOffset; off < sfxSampleTableOffset; off += sizeof(konami_mw_sample_info)) {
    std::string name = fmt::format("Instrument {}", sampNum);
    VGMInstr* instr = new VGMInstr(this, off, sizeof(konami_mw_sample_info), 0, sampNum, name);
    VGMRgn* rgn = new VGMRgn(instr, off);
    rgn->sampNum = sampNum++;
    instr->addRgn(rgn);
    aInstrs.push_back(instr);
  }
  return true;
}


// ********************
// KonamiArcadeSampColl
// ********************

KonamiArcadeSampColl::KonamiArcadeSampColl(
    RawFile* file,
    KonamiArcadeInstrSet* instrset,
    std::vector<konami_mw_sample_info>& sampInfos,
    u32 offset,
    u32 length,
    std::string name)
    : VGMSampColl(KonamiArcadeFormat::name, file, offset, length, std::move(name)),
      instrset(instrset), sampInfos(sampInfos) {
}

bool KonamiArcadeSampColl::parseHeader() {
  return true;
}

u32 KonamiArcadeSampColl::determineSampleSize(u32 startOffset, konami_mw_sample_info::sample_type type) {
  // Each sample type uses a slightly different sentinel value.
  // In practice, we find that each sample ends with multiple sample values. The smallest pattern
  // being ADPCM with 4 0x88 bytes in a row. For performance sake, we should be fine to
  // read two bytes at a time.
  u16 endMarker;
  switch (type) {
    case konami_mw_sample_info::sample_type::PCM_8:
      endMarker = 0x8080;
      break;
    case konami_mw_sample_info::sample_type::PCM_16:
      endMarker = 0x8000;
      break;
    case konami_mw_sample_info::sample_type::ADPCM:
      endMarker = 0x8888;
      break;
    default:
      L_ERROR("K054539 sample info has invalid type: {:d}. Expected 0, 4, or 8.", static_cast<int>(type));
      endMarker = 0x8080;
      break;
  }

  for (u32 off = startOffset; off < unLength + 2; off += 2) {
    if (readShort(off) == endMarker)
      return off - startOffset;
  }
  return unLength - startOffset;
}

bool KonamiArcadeSampColl::parseSampleInfo() {

  int sampNum = 0;
  for (auto sampInfo : sampInfos) {
    u32 sampleOffset = sampInfo.start_msb << 16 | sampInfo.start_mid << 8 | sampInfo.start_lsb;
    u32 sampleLoopOffset = sampInfo.loop_msb << 16 | sampInfo.loop_mid << 8 | sampInfo.loop_lsb;
    u32 sampleSize = determineSampleSize(sampleOffset, sampInfo.type);

    auto name = fmt::format("Sample {:d}", sampNum++);
    if (sampInfo.type == konami_mw_sample_info::sample_type::ADPCM) {
      auto sample = new K054539AdpcmSamp(this, sampleOffset, sampleSize, 16700, name);
      sample->setWaveType(WT_PCM16);
      sample->setLoopStatus(sampInfo.loops == 1);
      sample->setLoopOffset(sampleLoopOffset);
      // sample->unityKey = 0x3C;
      samples.push_back(sample);
    } else {
      u16 bps = sampInfo.type == konami_mw_sample_info::sample_type::PCM_8 ? 8 : 16;
      VGMSamp *sample = addSamp(sampleOffset,
                           sampleSize,
                           sampleOffset,
                           sampleSize,
                           1,
                           bps,
                           16700,
                           name);
      sample->setWaveType(bps == 8 ? WT_PCM8 : WT_PCM16);
      sample->setLoopStatus(sampInfo.loops == 1);
      sample->setLoopOffset(sampleLoopOffset);
    }
  }
  return true;
}
