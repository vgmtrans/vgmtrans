#include "KonamiTMNT2Instr.h"
#include "KonamiTMNT2Format.h"
#include "KonamiAdpcm.h"
#include "KonamiTMNT2Definitions.h"
#include "VGMRgn.h"

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


void KonamiTMNT2SampleInstrSet::addInstrInfoChildren(VGMItem* sampInfoItem, u32 off) {
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
    // rgn->sampNum = sampNum;
    // rgn->release_time = 0;
    instr->addRgn(rgn);
    aInstrs.push_back(instr);
    instrNum += 1;
  }
  return true;
}

double k053260_pitch_cents(uint16_t pitch_word) {
  uint16_t P = pitch_word & 0x0FFF;
  double ratio = 112.0 / (4096.0 - (double)P);
  return 1200.0 * log2(ratio);      // cents relative to 31,960 Hz @ B3
}

bool KonamiTMNT2SampleInstrSet::parseDrums() {
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
      auto drumItem = drumsItem->addChild(ptr, sizeof(konami_tmnt2_drum_info), name);
      drumItem->addChild(ptr + 0, 2, "Pitch");
      drumItem->addChild(ptr + 2, 1, "Unknown");
      drumItem->addChild(ptr + 3, 1, "Flags");
      drumItem->addChild(ptr + 4, 2, "Sample Length");
      drumItem->addChild(ptr + 6, 3, "Sample Address");
      drumItem->addChild(ptr + 9, 1, "Volume");
      drumItem->addChild(ptr + 10, 4, "Unknown");


      const konami_tmnt2_drum_info& drumInfo = m_drumTables[i][j];

      double relativePitchCents = k053260_pitch_cents((drumInfo.pitch_hi << 8) + drumInfo.pitch_lo);
      // std::string name = fmt::format("Drum {}", drumNum);
      // VGMInstr* instr = new VGMInstr(this, offset, sizeof(konami_tmnt2_instr_info), 0, instrNum, name);
      VGMRgn* rgn = new VGMRgn(drumKit, ptr, sizeof(konami_tmnt2_drum_info));
      rgn->sampOffset = drumInfo.start();
      u8 key = (i * 16) + j;
      rgn->keyLow = key;
      rgn->keyHigh = key;
      rgn->unityKey = key;
      rgn->coarseTune = relativePitchCents / 100;
      rgn->fineTune = static_cast<int>(relativePitchCents) % 100;
      // rgn->sampNum = sampNum;
      // rgn->release_time = 0;
      drumKit->addRgn(rgn);
      drumNum += 1;
    }
  }
  aInstrs.emplace_back(drumKit);
  drumsItem->dwOffset = minDrumOffset;
  drumsItem->unLength = (maxDrumOffset + sizeof(konami_tmnt2_drum_info)) - minDrumOffset;
  return true;
}


 // OPMData convertToOPMData(u8 masterVol, const std::string& name) const {
 //    bool enableLFO = (LFO_ENABLE_AND_WF & 0x80) != 0;
 //    // LFO
 //    OPMData::LFO lfo{};
 //    if (enableLFO) {
 //      lfo.LFRQ = LFRQ;
 //      lfo.AMD = AMD;
 //      lfo.PMD = PMD;
 //      lfo.WF = (LFO_ENABLE_AND_WF >> 5) & 0b11;
 //      lfo.NFRQ = 0;  // the driver doesn't define noise frequency
 //    }
 //
 //    // CH
 //    OPMData::CH ch{};
 //    ch.PAN = 0b11000000; // the driver always sets R/L, ie PAN, to 0xC0 (sf2ce 0xDC0)
 //    ch.FL = (FL_CON >> 3) & 0b111;
 //    ch.CON = FL_CON & 0b111;
 //    ch.AMS = enableLFO ? PMS_AMS & 0b11 : 0;
 //    ch.PMS = enableLFO ? (PMS_AMS >> 4) & 0b1111 : 0;
 //    ch.SLOT_MASK = SLOT_MASK;
 //    ch.NE = 0;
 //
 //    // OP
 //    uint8_t CON_limits[4] = { 7, 5, 4, 0 };
 //    OPMData::OP op[4];
 //    for (int i = 0; i < 4; i ++) {
 //      auto conLimit = CON_limits[i];
 //      auto& opx = op[i];
 //      opx.AR = KS_AR[i] & 0b11111;
 //      opx.D1R = AMSEN_D1R[i] & 0b11111;
 //      opx.D2R = DT2_D2R[i] & 0b11111;
 //      opx.RR = D1L_RR[i] & 0b1111;
 //      opx.D1L = D1L_RR[i] >> 4;
 //      if (ch.CON < conLimit) {
 //        u8 atten = volToAttenuation(volData[i].vol);
 //        opx.TL = (atten + volData[i].extra_atten) & 0x7F;
 //      } else {
 //        u8 masterVolumeAtten = 0x7F - masterVol;
 //        u8 atten = volToAttenuation(volData[i].vol);
 //        u32 finalAtten = (atten + masterVolumeAtten) + volData[i].extra_atten;
 //        opx.TL = std::min(finalAtten, 0x7FU);
 //      }
 //      opx.KS = KS_AR[i] >> 6;
 //      opx.MUL = DT1_MUL[i] & 0b1111;
 //      opx.DT1 = (DT1_MUL[i] >> 4) & 0b111;
 //      opx.DT2 = DT2_D2R[i] >> 6;
 //      opx.AMS_EN = AMSEN_D1R[i] & 0b10000000;
 //    }
 //
 //    return {name, lfo, ch, { op[0], op[1], op[2], op[3] }};
 //  }
 //
 //  std::string toOPMString(uint8_t masterVol, const std::string& name, int num) const {
 //    std::ostringstream ss;
 //
 //    // Generate the OPM data string first
 //    OPMData opmData = convertToOPMData(masterVol, name);
 //    ss << opmData.toOPMString(num);
 //
 //    // Add supplementary data
 //    ss << "\nCPS:";
 //    uint8_t enableLfo = LFO_ENABLE_AND_WF >> 7;
 //    uint8_t resetLfo = (LFO_ENABLE_AND_WF >> 1) & 1;
 //    ss << " " << +enableLfo << " " <<  +resetLfo;
 //    for (int i = 0; i < 4; i ++) {
 //      ss << " " << +volData[i].key_scale << " " << +volData[i].extra_atten;
 //    }
 //    ss << "\n";
 //    return ss.str();
 //  }



KonamiTMNT2SampColl::KonamiTMNT2SampColl(
    RawFile* file,
    KonamiTMNT2SampleInstrSet* instrset,
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