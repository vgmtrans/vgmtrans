#include <sstream>
#include <cmath>
#include <spdlog/fmt/fmt.h>
#include "VGMSamp.h"
#include "VGMRgn.h"
#include "ScaleConversion.h"
#include "CPS2Format.h"
#include "CPS2Instr.h"
#include "OkiAdpcm.h"

// ****************
// CPSArticTable
// ****************


CPSArticTable::CPSArticTable(RawFile *file, std::string name, uint32_t offset, uint32_t length)
    : VGMMiscFile(CPS2Format::name, file, offset, length, std::move(name)) {
}

CPSArticTable::~CPSArticTable() {
  if (artics)
    delete[] artics;
}

bool CPSArticTable::loadMain() {
  uint32_t off = dwOffset;
  for (int i = 0; off < dwOffset + unLength; i++, off += sizeof(qs_artic_info)) {
    uint32_t test1 = readWord(off);
    uint32_t test2 = readWord(off + 4);
    if ((test1 == 0 && test2 == 0) || (test1 == 0xFFFFFFFF && test2 == 0xFFFFFFFF))
      continue;

    auto name = fmt::format("Articulation {:d}", i);
    VGMItem *containerItem = new VGMItem(this, off, sizeof(qs_artic_info), name);
    containerItem->addChild(off, 1, "Attack Rate");
    containerItem->addChild(off + 1, 1, "Decay Rate");
    containerItem->addChild(off + 2, 1, "Sustain Level");
    containerItem->addChild(off + 3, 1, "Sustain Rate");
    containerItem->addChild(off + 4, 1, "Release Rate");
    containerItem->addChild(off + 5, 3, "Unknown");
    addChild(containerItem);
  }
  //unLength = off - dwOffset;
  int numArtics = unLength / sizeof(qs_artic_info);
  artics = new qs_artic_info[numArtics];
  readBytes(dwOffset, unLength, artics);

  return true;
}


// ******************
// CPSSampleInfoTable
// ******************

CPSSampleInfoTable::CPSSampleInfoTable(RawFile *file,
                                       std::string name,
                                       uint32_t offset,
                                       uint32_t length)
    : VGMMiscFile(CPS2Format::name, file, offset, length, std::move(name)) {
}

CPSSampleInfoTable::~CPSSampleInfoTable() {
  if (infos) {
    delete[] infos;
  }
}


CPS2SampleInfoTable::CPS2SampleInfoTable(RawFile *file,
                                         std::string name,
                                         uint32_t offset,
                                         uint32_t length)
    : CPSSampleInfoTable(file, std::move(name), offset, length) {
}

bool CPS2SampleInfoTable::loadMain() {
  uint32_t off = dwOffset;
  uint32_t test1 = 1, test2 = 1;
  if (unLength == 0)
    unLength = 0xFFFFFFFF - dwOffset;
  for (int i = 0; (test1 || test2) && ((test1 != 0xFFFFFFFF) || (test2 != 0xFFFFFFFF)) && off < dwOffset + unLength;
       i++, off += 8) {
    test1 = readWord(off + 8);
    test2 = readWord(off + 12);

    auto name = fmt::format("Sample Info: {:d}", i);

    VGMItem *containerItem = new VGMItem(this, off, sizeof(qs_samp_info_cps2), name);
    containerItem->addChild(off + 0, 1, "Bank");
    containerItem->addChild(off + 1, 2, "Offset");
    containerItem->addChild(off + 3, 2, "Loop Offset");
    containerItem->addChild(off + 5, 2, "End Offset");
    containerItem->addChild(off + 7, 1, "Unity Key");
    addChild(containerItem);
  }
  unLength = off - 8 - dwOffset;
  this->numSamples = unLength / 8;

  infos = new sample_info[numSamples];

  for (off = 0; off < numSamples * 8; off += 8) {

    uint8_t bank = readByte(dwOffset + off + 0);
    uint16_t start_addr = readShort(dwOffset + off + 1);
    uint16_t loop_offset= readShort(dwOffset + off + 3);
    uint16_t end_addr = readShort(dwOffset + off + 5);
    uint8_t unity_key = readByte(dwOffset + off + 7);

    sample_info& info = infos[off/8];
    info.start_addr = (bank << 16) | (start_addr);
    info.loop_offset =  (bank << 16) | loop_offset;
    info.end_addr =  static_cast<uint32_t>(end_addr) + (end_addr == 0 ? (bank + 1) << 16 : bank << 16);
    info.unity_key = unity_key;

    // D&D SOM has a sample with end_addr < start addr at index 290.
    if (info.start_addr > info.end_addr) {
      info.end_addr = info.start_addr;
    }
    if (info.loop_offset < info.start_addr ||
        info.loop_offset > info.end_addr) {
      info.loop_offset = info.end_addr;
    }
  }

  return true;
}


CPS3SampleInfoTable::CPS3SampleInfoTable(RawFile *file,
                                         std::string name,
                                         uint32_t offset,
                                         uint32_t length)
    : CPSSampleInfoTable(file, std::move(name), offset, length) {
}

//CPS3SampleInfoTable::~CPS3SampleInfoTable(void) {
//  if (infos)
//    delete[] infos;
//}

bool CPS3SampleInfoTable::loadMain() {

  this->numSamples = unLength / 16;

  infos = new sample_info[numSamples];

  int i=0;
  for (uint32_t off = dwOffset; off < dwOffset + unLength; off += 16) {

    auto name = fmt::format("Sample Info: {:d}", i);

    VGMItem *containerItem = new VGMItem(this, off, 16, name);
    containerItem->addChild(off + 0, 4, "Offset");
    containerItem->addChild(off + 4, 4, "Loop Offset");
    containerItem->addChild(off + 8, 4, "End Offset");
    containerItem->addChild(off + 12, 4, "Unity Key");
    addChild(containerItem);

    sample_info& info = infos[i];
    info.start_addr = readWordBE(off + 0);
    info.loop_offset = readWordBE(off + 4);
    info.end_addr = readWordBE(off + 8);
    info.unity_key = static_cast<uint8_t>(readWordBE(off + 12));

    i++;
  }
  return true;
}

// ***********
// CPS2InstrSet
// ***********

CPS2InstrSet::CPS2InstrSet(RawFile *file,
                         CPSFormatVer version,
                         uint32_t offset,
                         int numInstrBanks,
                         CPSSampleInfoTable *theSampInfoTable,
                         CPSArticTable *theArticTable,
                         std::string name)
    : VGMInstrSet(CPS2Format::name, file, offset, 0, std::move(name)),
      fmt_version(version),
      num_instr_banks(numInstrBanks),
      sampInfoTable(theSampInfoTable),
      articTable(theArticTable) {
}

bool CPS2InstrSet::parseHeader() {
  return true;
}

bool CPS2InstrSet::parseInstrPointers() {
  // Load the instr_info tables.

  if (fmt_version <= VER_115) {
    // In these versions, there are no instr_info_table pointers, it's hard-coded into the assembly code.
    // There are two possible instr_info banks stored next to each other with 256 entries in each,
    // unlike higher versions, where the number of instr_infos in each bank is variable and less than 0x7F.

    //dwOffset is the offset to the instr_info_table

    for (uint32_t bank = 0; bank < num_instr_banks; bank++)
      for (uint32_t i = 0; i < 256; i++) {
        auto name = fmt::format("Instrument: bank {:d}  num {:d}", bank * 256, i);
        aInstrs.push_back(new CPS2Instr(this,
                                       dwOffset + i * 8 + (bank * 256 * 8),
                                       8,
                                       (bank * 2) + (i / 128),
                                       i % 128,
                                       name));
      }
  }
  else {
    uint8_t instr_info_length = sizeof(qs_prog_info_ver_130);
    if (fmt_version < VER_130 || fmt_version == VER_200 || fmt_version == VER_201B) {
      instr_info_length = sizeof(qs_prog_info_ver_103);        //1.16 (Xmen vs SF) is like this
    }
    else if (fmt_version == VER_CPS3) {
      instr_info_length = 12;     // qs_prog_info_ver_cps3
    }

    std::vector<uint32_t> instr_table_ptrs;
    for (unsigned int i = 0; i < num_instr_banks; i++) {
      if (fmt_version == VER_CPS3) {
        instr_table_ptrs.push_back(readWordBE(dwOffset + i * 4));    //get the instr table ptrs
      }
      else {
        instr_table_ptrs.push_back(readShort(dwOffset + i * 2));    //get the instr table ptrs
      }
    }
    int totalInstrs = 0;
    for (uint8_t i = 0; i < instr_table_ptrs.size(); i++) {

      if (fmt_version == VER_CPS3) {
        uint8_t bank = i;
        uint32_t bankOff = instr_table_ptrs[bank] - 0x6000000;

        auto pointersName = fmt::format("Bank {:d} Instrument Pointers", bank);
        auto instrPointersItem = new VGMItem(vgmFile(), bankOff, 128*2, pointersName, CLR_HEADER);

        // For each bank, iterate over all instr ptrs and create instruments
        for (uint8_t j = 0; j < 128; j++) {
          uint16_t instrPtrOffset = readShortBE(bankOff + (j*2));
          uint32_t instrPtr = instrPtrOffset + bankOff;
          // We are not guaranteed a 0xFFFF separator sequence between instruments, so instead we
          // calculate length of each using the next instr pointer if possible.
          uint32_t nextInstrPtrOffset = readShortBE(bankOff + ((j+1)*2));
          uint32_t instrLength = nextInstrPtrOffset == 0 || j == 127 ? 0 : nextInstrPtrOffset - instrPtrOffset;
          if (instrPtrOffset == 0) {
            continue;
          }
          std::ostringstream pointerStream;
          pointerStream << "Instrument Pointer " << j;
          instrPointersItem->addChild(bankOff + (j*2), 2, pointerStream.str());

          auto name = fmt::format("Instrument: {:d}  bank: {:d}", j, bank);
          aInstrs.push_back(new CPS2Instr(this, instrPtr, instrLength, bank*2, j, name));
        }
        addChild(instrPointersItem);

        continue;
      }

      int k = 0;
      uint16_t endOffset;
      // The following is actually incorrect.  There is a max of 256 instruments per bank
      if (i + 1 < instr_table_ptrs.size() && instr_table_ptrs[i] < instr_table_ptrs[i+1])
        endOffset = instr_table_ptrs[i + 1];
      else
        endOffset = instr_table_ptrs[i] + instr_info_length * 256;//4*0x7F;

      for (int j = instr_table_ptrs[i]; j < endOffset; j += instr_info_length, k++) {
        if (readShort(j) == 0 && readByte(j + 2) == 0 && i != 0)
          break;

        auto name = fmt::format("Instrument {:d}", k);
        aInstrs.push_back(new CPS2Instr(this, j, instr_info_length, (i * 2) + (k / 128), (k % 128), name));
      }
      totalInstrs += k;
    }
  }
  return true;
}


// ***********
// CPS2Instr
// ***********

CPS2Instr::CPS2Instr(VGMInstrSet *instrSet,
                     uint32_t offset,
                     uint32_t length,
                     uint32_t theBank,
                     uint32_t theInstrNum,
                     std::string name)
    : VGMInstr(instrSet, offset, length, theBank, theInstrNum, std::move(name)) {
}

bool CPS2Instr::loadInstr() {
  std::vector<VGMRgn*> rgns;
  const CPSFormatVer formatVer = formatVersion();
  if (formatVer < VER_103) {
    VGMRgn* rgn = new VGMRgn(this, dwOffset, unLength);
    rgns.push_back(rgn);
    rgn->addChild(this->dwOffset,     1, "Sample Info Index");
    rgn->addChild(this->dwOffset + 1, 1, "Unknown / Ignored");
    rgn->addChild(this->dwOffset + 2, 1, "Attack Rate");
    rgn->addChild(this->dwOffset + 3, 1, "Decay Rate");
    rgn->addChild(this->dwOffset + 4, 1, "Sustain Level");
    rgn->addChild(this->dwOffset + 5, 1, "Sustain Rate");
    rgn->addChild(this->dwOffset + 6, 1, "Release Rate");
    rgn->addChild(this->dwOffset + 7, 1, "Unknown");

    qs_prog_info_ver_101 progInfo;
    readBytes(dwOffset, sizeof(qs_prog_info_ver_101), &progInfo);
    rgn->sampNum = progInfo.sample_index;
    this->attack_rate = progInfo.attack_rate;
    this->decay_rate = progInfo.decay_rate;
    this->sustain_level = progInfo.sustain_level;
    this->sustain_rate = progInfo.sustain_rate;
    this->release_rate = progInfo.release_rate;
  }
  else if (formatVer < VER_130 || formatVer == VER_200 || formatVer == VER_201B) {
    VGMRgn* rgn = new VGMRgn(this, dwOffset, unLength);
    rgns.push_back(rgn);
    qs_prog_info_ver_103 progInfo;
    readBytes(dwOffset, sizeof(qs_prog_info_ver_103), &progInfo);

    rgn->addSampNum(progInfo.sample_index, this->dwOffset, 2);
    rgn->addFineTune( static_cast<int16_t>((progInfo.fine_tune / 256.0) * 100), this->dwOffset + 2, 1);
    rgn->addChild(this->dwOffset + 3, 1, "Attack Rate");
    rgn->addChild(this->dwOffset + 4, 1, "Decay Rate");
    rgn->addChild(this->dwOffset + 5, 1, "Sustain Level");
    rgn->addChild(this->dwOffset + 6, 1, "Sustain Rate");
    rgn->addChild(this->dwOffset + 7, 1, "Release Rate");

    this->attack_rate = progInfo.attack_rate;
    this->decay_rate = progInfo.decay_rate;
    this->sustain_level = progInfo.sustain_level;
    this->sustain_rate = progInfo.sustain_rate;
    this->release_rate = progInfo.release_rate;
  }
  else if (formatVer == VER_CPS3) {

    uint8_t prevKeyHigh = 0;
    uint32_t off;
    for (off = dwOffset; readShort(off) != 0xFFFF; off += 12) {
      if (unLength != 0 && off >= dwOffset + unLength)
        break;

      VGMRgn* rgn = new VGMRgn(this, off, 12);
      rgns.push_back(rgn);

      qs_prog_info_ver_cps3 progInfo;
      readBytes(off, sizeof(qs_prog_info_ver_cps3), &progInfo);

      //    rgn->AddFineTune( (int16_t)((progInfo.fine_tune / 256.0) * 100), this->dwOffset + 2, 1);
      rgn->addKeyHigh(progInfo.key_high, off + 0, 1);
      rgn->keyLow = prevKeyHigh + 1;
      prevKeyHigh = progInfo.key_high;

      // When the region pan value != -1, the CPS3 driver completely overrides the track state pan
      // (otherwise it's ignored). SF2 and DLS don't do this; they combine region pan with track pan.
      uint8_t pan = progInfo.pan_override == -1 ? 64 : progInfo.pan_override;
      rgn->addPan(pan, off+1, 1, "Pan Override");

      auto volume_percent = ((64 + progInfo.volume_adjustment) & 0x7F) / 64.0;
      rgn->addVolume(volume_percent, off+2, 1);
      rgn->addUnknown(off+3, 1);
      rgn->addSampNum((progInfo.sample_index_hi << 8) + progInfo.sample_index_lo, off+4, 2);

      auto fine_tune_cents = static_cast<int16_t>(std::lround((progInfo.fine_tune / 128.0) * 100));
      rgn->addFineTune(fine_tune_cents, off+6, 1);

      rgn->addChild(off + 7, 1, "Attack Rate");
      rgn->addChild(off + 8, 1, "Decay Rate");
      rgn->addChild(off + 9, 1, "Sustain Level");
      rgn->addChild(off + 10, 1, "Sustain Rate");
      rgn->addChild(off + 11, 1, "Release Rate");


      this->attack_rate = progInfo.attack_rate;
      this->decay_rate = progInfo.decay_rate;
      this->sustain_level = progInfo.sustain_level;
      this->sustain_rate = progInfo.sustain_rate;
      this->release_rate = progInfo.release_rate;
    }
    unLength = off - dwOffset;
  }
  else {
    VGMRgn* rgn = new VGMRgn(this, dwOffset, unLength, "Region");
    rgns.push_back(rgn);
    qs_prog_info_ver_130 progInfo;
    readBytes(dwOffset, sizeof(qs_prog_info_ver_130), &progInfo);

    rgn->addChild(this->dwOffset,     2, "Sample Info Index");
    rgn->addFineTune( static_cast<int16_t>((progInfo.fine_tune / 256.0) * 100), this->dwOffset + 2, 1);
    rgn->addChild(this->dwOffset + 3, 1, "Articulation Index");

    const CPSArticTable* articTable = static_cast<CPS2InstrSet*>(this->parInstrSet)->articTable;
    const qs_artic_info* artic = &articTable->artics[progInfo.artic_index];
    rgn->sampNum = progInfo.sample_index;
    this->attack_rate = artic->attack_rate;
    this->decay_rate = artic->decay_rate;
    this->sustain_level = artic->sustain_level;
    this->sustain_rate = artic->sustain_rate;
    this->release_rate = artic->release_rate;
  }

  for (auto rgn : rgns) {
    // Fix for Dungeons and Dragons Shadow Over Mystara
    if (rgn->sampNum >= 0x8000)
      rgn->sampNum -= 0x8000;

    // if the sample doesn't exist, set it to the first sample
    if (rgn->sampNum >= static_cast<CPS2InstrSet*>(parInstrSet)->sampInfoTable->numSamples)
      rgn->sampNum = 0;

    uint16_t Ar = attack_rate_table[this->attack_rate];
    uint16_t Dr = decay_rate_table[this->decay_rate];
    uint16_t Sl = sustain_level_table[this->sustain_level];
    uint16_t Sr = decay_rate_table[this->sustain_rate];
    uint16_t Rr = decay_rate_table[this->release_rate];

    const double UPDATE_RATE_IN_HZ = formatVer == VER_CPS3 ? CPS3_DRIVER_RATE_HZ : CPS2_DRIVER_RATE_HZ;
    // The rate values are all measured from max to min, as the SF2 and DLS specs call for.
    //  In the actual code, the envelope level starts at different points
    // Attack rate 134A in sfa2
    long ticks = Ar ? 0xFFFF / Ar : 0;
    rgn->attack_time = (Ar == 0xFFFF) ? 0 : ticks / UPDATE_RATE_IN_HZ;

    // Decay rate 1365 in sfa2
    //  Let's check if we should substitute the sustain rate for the decay rate.
    //  Why?  SF2 and DLS do not have a sustain rate.  If the decay rate here is practically
    //  unnoticeable because the sustain level is extremely high - meaning decay time is
    //  super short - then let's set the sustain level to 0 and substitute the sustain
    //  rate into the decay rate,  achieving the same effect as sustain rate.

    if (this->sustain_level >= 0x7E && this->sustain_rate > 0 && this->decay_rate > 1) {
      //for a better approximation, we count the ticks to get from original Dr to original Sl
      ticks = static_cast<long>(ceil((0xFFFF - Sl)) / static_cast<double>(Dr));
      ticks += static_cast<long>(ceil(Sl / static_cast<double>(Sr)));
      rgn->decay_time = ticks / UPDATE_RATE_IN_HZ;
      Sl = 0;
    } else {
      ticks = Dr ? (0xFFFF / Dr) : 0;
      rgn->decay_time = (Dr == 0xFFFF) ? 0 : ticks / UPDATE_RATE_IN_HZ;
    }
    rgn->decay_time = linearAmpDecayTimeToLinDBDecayTime(rgn->decay_time, 0xFFFF / 8);

    // Sustain level
    //    if the Decay rate is 0, then the sustain level is effectively max
    //    some articulations have Dr = 0, Sl = 0 (ex: mmatrix).  Dr 0 means the decay phase never ends
    //    and Sl value is irrelevant because the envelope stays at max, but we can't set an infinite decay phase in sf2 or dls
    if (Dr <= 1)
      rgn->sustain_level = 1.0;
    else
      rgn->sustain_level = Sl / static_cast<double>(0xFFFF);

    // Sustain rate 138D in sfa2
    ticks = Sr ? 0xFFFF / Sr : 0;
    rgn->sustain_time = (Sr == 0xFFFF) ? 0 : ticks / UPDATE_RATE_IN_HZ;
    rgn->sustain_time = linearAmpDecayTimeToLinDBDecayTime(rgn->sustain_time, 0xFFFF / 8);

    ticks = Rr ? 0xFFFF / Rr : 0xFFFF;
    rgn->release_time = (Rr == 0xFFFF) ? 0 : ticks / UPDATE_RATE_IN_HZ;
    rgn->release_time = linearAmpDecayTimeToLinDBDecayTime(rgn->release_time, 0xFFFF / 8);

    if (rgn->sampNum == 0xFFFF || rgn->sampNum >= (dynamic_cast<CPS2InstrSet*>(parInstrSet))->sampInfoTable->numSamples)
      rgn->sampNum = 0;

    rgn->setUnityKey((dynamic_cast<CPS2InstrSet*>(parInstrSet))->sampInfoTable->infos[rgn->sampNum].unity_key);
    addRgn(rgn);
  }
  return true;
}


// **************
// CPS2SampColl
// **************

CPS2SampColl::CPS2SampColl(RawFile *file, CPS2InstrSet *theinstrset,
                         CPSSampleInfoTable *sampinfotable,
                         uint32_t offset,
                         uint32_t length,
                         std::string name)
    : VGMSampColl(CPS2Format::name, file, offset, length, std::move(name)),
      instrset(theinstrset),
      sampInfoTable(sampinfotable) {

}


bool CPS2SampColl::parseHeader() {
  unLength = static_cast<uint32_t>(this->rawFile()->size());
  return true;
}

bool CPS2SampColl::parseSampleInfo() {

  uint32_t numSamples = instrset->sampInfoTable->numSamples;
  uint32_t baseOffset = 0;

  for (uint32_t i = 0; i < numSamples; i++) {

    auto& [start_addr, loop_offset, end_addr, unity_key] = sampInfoTable->infos[i];
    // Base address correction for Strider2, which maps qsound start address to 200000h.
    // We assume first bank listed is base address.
    if (i == 0)
      baseOffset = start_addr & 0xFF0000;
    uint32_t sampOffset = start_addr - baseOffset;
    uint32_t sampLength = end_addr - start_addr;

    // Sanity check
    if (sampOffset > unLength)
      break;

    // Sanity check for mshvsf, which has samples of sizez 2. Fluidsynth does not like this (perhaps violates sf2 spec?)
    if (sampLength <= 2) {
      sampLength = 100;
    }

    uint32_t relativeLoopOffset = loop_offset - sampOffset;

    if (relativeLoopOffset > sampLength)
      relativeLoopOffset = sampLength;


    // CPS3 frequency is (42954545 hz / 3) / 384) ~= 37287
    // CPS2 frequency is (60 MHz / 2 / 1248) ~= 24038
    uint32_t frequency = instrset->fmt_version == VER_CPS3 ? 37287 : 24038;
    VGMSamp *newSamp = addSamp(sampOffset,
                               sampLength,
                               sampOffset,
                               sampLength,
                               1,
                               8,
                               frequency,
                               fmt::format("Sample {:d}", i));
    newSamp->setWaveType(WT_PCM8);
    if (sampLength - relativeLoopOffset < 40)
      newSamp->setLoopStatus(false);
    else {
      newSamp->setLoopStatus(true);
      newSamp->setLoopOffset(relativeLoopOffset);
      newSamp->setLoopLength(sampLength - relativeLoopOffset);
    }
    newSamp->unityKey = unity_key;
  }
  return true;
}
