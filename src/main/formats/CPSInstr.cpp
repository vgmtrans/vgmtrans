#include "pch.h"
#include "VGMSamp.h"
#include "VGMRgn.h"
#include "ScaleConversion.h"
#include "CPSFormat.h"
#include "CPSInstr.h"

using namespace std;

// ****************
// CPSArticTable
// ****************


CPSArticTable::CPSArticTable(RawFile *file, std::wstring &name, uint32_t offset, uint32_t length)
    : VGMMiscFile(CPSFormat::name, file, offset, length, name) {
}

CPSArticTable::~CPSArticTable(void) {
  if (artics)
    delete[] artics;
}

bool CPSArticTable::LoadMain() {
  uint32_t off = dwOffset;
  uint32_t test1 = 1, test2 = 1;
  //for (int i = 0; (test1 || test2)  && ((test1 != 0xFFFFFFFF) || (test2 != 0xFFFFFFFF)); i++, off += sizeof(qs_samp_info_cps2) )
  for (int i = 0; off < dwOffset + unLength; i++, off += sizeof(qs_artic_info)) {
    test1 = GetWord(off);
    test2 = GetWord(off + 4);
    if ((test1 == 0 && test2 == 0) || (test1 == 0xFFFFFFFF && test2 == 0xFFFFFFFF))
      continue;


    wostringstream name;
    name << L"Articulation " << i;
    VGMContainerItem *containerItem = new VGMContainerItem(this, off, sizeof(qs_artic_info), name.str());
    containerItem->AddSimpleItem(off, 1, L"Attack Rate");
    containerItem->AddSimpleItem(off + 1, 1, L"Decay Rate");
    containerItem->AddSimpleItem(off + 2, 1, L"Sustain Level");
    containerItem->AddSimpleItem(off + 3, 1, L"Sustain Rate");
    containerItem->AddSimpleItem(off + 4, 1, L"Release Rate");
    containerItem->AddSimpleItem(off + 5, 3, L"Unknown");
    this->AddItem(containerItem);
  }
  //unLength = off - dwOffset;
  int numArtics = unLength / sizeof(qs_artic_info);
  artics = new qs_artic_info[numArtics];
  GetBytes(dwOffset, unLength, artics);

  return true;
}


// ******************
// CPSSampleInfoTable
// ******************

CPSSampleInfoTable::CPSSampleInfoTable(RawFile *file,
                                       wstring &name,
                                       uint32_t offset,
                                       uint32_t length)
    : VGMMiscFile(CPSFormat::name, file, offset, length, name) {
}

CPSSampleInfoTable::~CPSSampleInfoTable(void) {
  if (infos) {
    delete[] infos;
  }
}


CPS2SampleInfoTable::CPS2SampleInfoTable(RawFile *file,
                                         wstring &name,
                                         uint32_t offset,
                                         uint32_t length)
    : CPSSampleInfoTable(file, name, offset, length) {
}

bool CPS2SampleInfoTable::LoadMain() {
  uint32_t off = dwOffset;
  uint32_t test1 = 1, test2 = 1;
  if (unLength == 0)
    unLength = 0xFFFFFFFF - dwOffset;
  for (int i = 0; (test1 || test2) && ((test1 != 0xFFFFFFFF) || (test2 != 0xFFFFFFFF)) && off < dwOffset + unLength;
       i++, off += 8) {
    test1 = GetWord(off + 8);
    test2 = GetWord(off + 12);

    wostringstream name;
    name << L"Sample Info " << i;

    VGMContainerItem *containerItem = new VGMContainerItem(this, off, sizeof(qs_samp_info_cps2), name.str());
    containerItem->AddSimpleItem(off + 0, 1, L"Bank");
    containerItem->AddSimpleItem(off + 1, 2, L"Offset");
    containerItem->AddSimpleItem(off + 3, 2, L"Loop Offset");
    containerItem->AddSimpleItem(off + 5, 2, L"End Offset");
    containerItem->AddSimpleItem(off + 7, 1, L"Unity Key");
    this->AddItem(containerItem);
  }
  unLength = off - 8 - dwOffset;
  this->numSamples = unLength / 8;

  infos = new sample_info[numSamples];

  for (uint32_t off = 0; off < numSamples * 8; off += 8) {

    uint8_t bank = GetByte(dwOffset + off + 0);
    uint16_t start_addr = GetShort(dwOffset + off + 1);
    uint16_t loop_offset= GetShort(dwOffset + off + 3);
    uint16_t end_addr = GetShort(dwOffset + off + 5);
    uint8_t unity_key = GetByte(dwOffset + off + 7);

    sample_info& info = infos[off/8];
    info.start_addr = (bank << 16) | (start_addr);
    info.loop_offset =  (bank << 16) | loop_offset;
    info.end_addr =  (uint32_t)end_addr + ((end_addr == 0) ? (bank + 1) << 16 : bank << 16);
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
                                         wstring &name,
                                         uint32_t offset,
                                         uint32_t length)
    : CPSSampleInfoTable(file, name, offset, length) {
}

//CPS3SampleInfoTable::~CPS3SampleInfoTable(void) {
//  if (infos)
//    delete[] infos;
//}

bool CPS3SampleInfoTable::LoadMain() {

  this->numSamples = unLength / 16;

  infos = new sample_info[numSamples];

  int i=0;
  for (uint32_t off = dwOffset; off < dwOffset + unLength; off += 16) {

    wostringstream name;
    name << L"Sample Info " << i;

    VGMContainerItem *containerItem = new VGMContainerItem(this, off, 16, name.str());
    containerItem->AddSimpleItem(off + 0, 4, L"Offset");
    containerItem->AddSimpleItem(off + 4, 4, L"Loop Offset");
    containerItem->AddSimpleItem(off + 8, 4, L"End Offset");
    containerItem->AddSimpleItem(off + 12, 4, L"Unity Key");
    this->AddItem(containerItem);

    sample_info& info = infos[i];
    info.start_addr = GetWordBE(off + 0);
    info.loop_offset = GetWordBE(off + 4);
    info.end_addr = GetWordBE(off + 8);
    info.unity_key = (uint8_t)GetWordBE(off + 12);

    i++;
  }
  return true;
}

// **************
// CPSInstrSet
// **************

CPSInstrSet::CPSInstrSet(RawFile *file,
                         CPSFormatVer version,
                         uint32_t offset,
                         int numInstrBanks,
                         CPSSampleInfoTable *theSampInfoTable,
                         CPSArticTable *theArticTable,
                         wstring &name)
    : VGMInstrSet(CPSFormat::name, file, offset, 0, name),
      fmt_version(version),
      num_instr_banks(numInstrBanks),
      sampInfoTable(theSampInfoTable),
      articTable(theArticTable) {
}

CPSInstrSet::~CPSInstrSet(void) {
}


bool CPSInstrSet::GetHeaderInfo() {
  return true;
}

bool CPSInstrSet::GetInstrPointers() {
  // Load the instr_info tables.

  if (fmt_version <= VER_115) {
    // In these versions, there are no instr_info_table pointers, it's hard-coded into the assembly code.
    // There are two possible instr_info banks stored next to each other with 256 entries in each,
    // unlike higher versions, where the number of instr_infos in each bank is variable and less than 0x7F.

    //dwOffset is the offset to the instr_info_table

    for (uint32_t bank = 0; bank < num_instr_banks; bank++)
      for (uint32_t i = 0; i < 256; i++) {
        std::wostringstream ss;
        ss << L"Instrument " << bank * 256 << i;
        wstring name = ss.str();
        aInstrs.push_back(new CPSInstr(this,
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

    vector<uint32_t> instr_table_ptrs;
    for (unsigned int i = 0; i < num_instr_banks; i++) {
      if (fmt_version == VER_CPS3) {
        instr_table_ptrs.push_back(GetWordBE(dwOffset + i * 4));    //get the instr table ptrs
      }
      else {
        instr_table_ptrs.push_back(GetShort(dwOffset + i * 2));    //get the instr table ptrs
      }
    }
    int totalInstrs = 0;
    for (uint8_t i = 0; i < instr_table_ptrs.size(); i++) {

      if (fmt_version == VER_CPS3) {
        uint8_t bank = i;
        uint32_t bankOff = instr_table_ptrs[bank] - 0x6000000;

        std::wostringstream pointersStream;
        pointersStream << L"Bank " << bank << " Instrument Pointers";
        auto instrPointersItem = new VGMContainerItem(this->vgmfile, bankOff, 128*2, pointersStream.str(), CLR_HEADER);

        // For each bank, iterate over all instr ptrs and create instruments
        for (uint8_t j = 0; j < 128; j++) {
          uint16_t instrPtrOffset = GetShortBE(bankOff + (j*2));
          uint32_t instrPtr = instrPtrOffset + bankOff;
          if (instrPtrOffset == 0) {
            continue;
          }
          std::wostringstream pointerStream;
          pointerStream << L"Instrument Pointer " << j;
          instrPointersItem->AddSimpleItem(bankOff + (j*2), 2, pointerStream.str());

          std::wostringstream ss;
          ss << L"Instrument " << j << " bank " << bank;
          wstring name = ss.str();
          //            aInstrs.push_back(new CPSInstr(this, instrPtr, 0, (uint32_t)(bank * 2) + (i / 128), (uint32_t)(instrNum % 128), name));
          aInstrs.push_back(new CPSInstr(this, instrPtr, 0, bank*2, j, name));

          instrNum++;
        }
        this->AddItem(instrPointersItem);

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
        if (GetShort(j) == 0 && GetByte(j + 2) == 0 && i != 0)
          break;

        std::wostringstream ss;
        ss << L"Instrument " << totalInstrs << k;
        wstring name = ss.str();
        aInstrs.push_back(new CPSInstr(this, j, instr_info_length, (i * 2) + (k / 128), (k % 128), name));
      }
      totalInstrs += k;
    }
  }
  return true;
}


// ***********
// CPSInstr
// ***********

CPSInstr::CPSInstr(VGMInstrSet *instrSet,
                   uint32_t offset,
                   uint32_t length,
                   uint32_t theBank,
                   uint32_t theInstrNum,
                   wstring &name)
    : VGMInstr(instrSet, offset, length, theBank, theInstrNum, name) {
}

CPSInstr::~CPSInstr(void) {
}


bool CPSInstr::LoadInstr() {
  vector<VGMRgn*> rgns;
  const CPSFormatVer formatVersion = GetFormatVer();
  if (formatVersion < VER_103) {
    VGMRgn* rgn = new VGMRgn(this, dwOffset, unLength, L"Instrument Info ver < 1.03");
    rgns.push_back(rgn);
    rgn->AddSimpleItem(this->dwOffset,     1, L"Sample Info Index");
    rgn->AddSimpleItem(this->dwOffset + 1, 1, L"Unknown / Ignored");
    rgn->AddSimpleItem(this->dwOffset + 2, 1, L"Attack Rate");
    rgn->AddSimpleItem(this->dwOffset + 3, 1, L"Decay Rate");
    rgn->AddSimpleItem(this->dwOffset + 4, 1, L"Sustain Level");
    rgn->AddSimpleItem(this->dwOffset + 5, 1, L"Sustain Rate");
    rgn->AddSimpleItem(this->dwOffset + 6, 1, L"Release Rate");
    rgn->AddSimpleItem(this->dwOffset + 7, 1, L"Unknown");

    qs_prog_info_ver_101 progInfo;
    GetBytes(dwOffset, sizeof(qs_prog_info_ver_101), &progInfo);
    rgn->sampNum = progInfo.sample_index;
    this->attack_rate = progInfo.attack_rate;
    this->decay_rate = progInfo.decay_rate;
    this->sustain_level = progInfo.sustain_level;
    this->sustain_rate = progInfo.sustain_rate;
    this->release_rate = progInfo.release_rate;
  }
  else if (formatVersion < VER_130 || formatVersion == VER_200 || formatVersion == VER_201B) {
    VGMRgn* rgn = new VGMRgn(this, dwOffset, unLength, L"Instrument Info ver < 1.30");
    rgns.push_back(rgn);
    qs_prog_info_ver_103 progInfo;
    GetBytes(dwOffset, sizeof(qs_prog_info_ver_103), &progInfo);

    rgn->AddSampNum(progInfo.sample_index, this->dwOffset, 2);
    rgn->AddFineTune( (int16_t)((progInfo.fine_tune / 256.0) * 100), this->dwOffset + 2, 1);
    rgn->AddSimpleItem(this->dwOffset + 3, 1, L"Attack Rate");
    rgn->AddSimpleItem(this->dwOffset + 4, 1, L"Decay Rate");
    rgn->AddSimpleItem(this->dwOffset + 5, 1, L"Sustain Level");
    rgn->AddSimpleItem(this->dwOffset + 6, 1, L"Sustain Rate");
    rgn->AddSimpleItem(this->dwOffset + 7, 1, L"Release Rate");

    this->attack_rate = progInfo.attack_rate;
    this->decay_rate = progInfo.decay_rate;
    this->sustain_level = progInfo.sustain_level;
    this->sustain_rate = progInfo.sustain_rate;
    this->release_rate = progInfo.release_rate;
  }
  else if (formatVersion == VER_CPS3) {

    uint8_t prevKeyHigh = 0;
    uint32_t off;
    for (off = dwOffset; GetShort(off) != 0xFFFF; off += 12) {
      VGMRgn* rgn = new VGMRgn(this, off, 12, L"Instrument Info ver CPS3");
      rgns.push_back(rgn);

      qs_prog_info_ver_cps3 progInfo;
      GetBytes(off, sizeof(qs_prog_info_ver_cps3), &progInfo);

      //    rgn->AddFineTune( (int16_t)((progInfo.fine_tune / 256.0) * 100), this->dwOffset + 2, 1);
      rgn->AddKeyHigh(progInfo.key_high, off + 0, 1);
      rgn->keyLow = prevKeyHigh + (uint8_t)1;
      prevKeyHigh = progInfo.key_high;
      rgn->AddSampNum(progInfo.sample_index, off+5, 1);
      rgn->AddSimpleItem(off + 7, 1, L"Attack Rate");
      rgn->AddSimpleItem(off + 8, 1, L"Decay Rate");
      rgn->AddSimpleItem(off + 9, 1, L"Sustain Level");
      rgn->AddSimpleItem(off + 10, 1, L"Sustain Rate");
      rgn->AddSimpleItem(off + 11, 1, L"Release Rate");


      this->attack_rate = progInfo.attack_rate;
      this->decay_rate = progInfo.decay_rate;
      this->sustain_level = progInfo.sustain_level;
      this->sustain_rate = progInfo.sustain_rate;
      this->release_rate = progInfo.release_rate;
    }
    unLength = off - dwOffset;
  }
  else {
    VGMRgn* rgn = new VGMRgn(this, dwOffset, unLength, L"Instrument Info ver >= 1.30");
    rgns.push_back(rgn);
    qs_prog_info_ver_130 progInfo;
    GetBytes(dwOffset, sizeof(qs_prog_info_ver_130), &progInfo);

    rgn->AddSimpleItem(this->dwOffset,     2, L"Sample Info Index");
    rgn->AddSimpleItem(this->dwOffset + 2, 1, L"Unknown");
    rgn->AddSimpleItem(this->dwOffset + 3, 1, L"Articulation Index");

    CPSArticTable *articTable = ((CPSInstrSet *) this->parInstrSet)->articTable;
    qs_artic_info *artic = &articTable->artics[progInfo.artic_index];
    rgn->sampNum = progInfo.sample_index;
    this->attack_rate = artic->attack_rate;
    this->decay_rate = artic->decay_rate;
    this->sustain_level = artic->sustain_level;
    this->sustain_rate = artic->sustain_rate;
    this->release_rate = artic->release_rate;

  }

  for (int i=0; i<rgns.size(); i++) {
    VGMRgn *rgn = rgns[i];

    // Fix for Dungeons and Dragons Shadow Over Mystara
    if (rgn->sampNum >= 0x8000)
      rgn->sampNum -= 0x8000;

    // if the sample doesn't exist, set it to the first sample
    if (rgn->sampNum >= ((CPSInstrSet *) parInstrSet)->sampInfoTable->numSamples)
      rgn->sampNum = 0;

    uint16_t Ar = attack_rate_table[this->attack_rate];
    uint16_t Dr = decay_rate_table[this->decay_rate];
    uint16_t Sl = linear_table[this->sustain_level];
    uint16_t Sr = decay_rate_table[this->sustain_rate];
    uint16_t Rr = decay_rate_table[this->release_rate];

    long ticks = 0;

    // The rate values are all measured from max to min, as the SF2 and DLS specs call for.
    //  In the actual code, the envelope level starts at different points
    // Attack rate 134A in sfa2
    ticks = Ar ? 0xFFFF / Ar : 0;
    rgn->attack_time = (Ar == 0xFFFF) ? 0 : ticks * QSOUND_TICK_FREQ;

    // Decay rate 1365 in sfa2
    //  Let's check if we should substitute the sustain rate for the decay rate.
    //  Why?  SF2 and DLS do not have a sustain rate.  If the decay rate here is practically
    //  unnoticeable because the sustain level is extremely high - meaning decay time is
    //  super short - then let's set the sustain level to 0 and substitute the sustain
    //  rate into the decay rate,  achieving the same effect as sustain rate.

    if (this->sustain_level >= 0x7E && this->sustain_rate > 0 && this->decay_rate > 1) {
      //for a better approximation, we count the ticks to get from original Dr to original Sl
      ticks = (long) ceil((0xFFFF - Sl) / (double) Dr);
      ticks += (long) ceil(Sl / (double) Sr);
      rgn->decay_time = ticks * QSOUND_TICK_FREQ;
      Sl = 0;
    } else {
      ticks = Dr ? (0xFFFF / Dr) : 0;
      rgn->decay_time = (Dr == 0xFFFF) ? 0 : ticks * QSOUND_TICK_FREQ;
    }
    rgn->decay_time = LinAmpDecayTimeToLinDBDecayTime(rgn->decay_time, 0x800);

    // Sustain level
    //    if the Decay rate is 0, then the sustain level is effectively max
    //    some articulations have Dr = 0, Sl = 0 (ex: mmatrix).  Dr 0 means the decay phase never ends
    //    and Sl value is irrelevant because the envelope stays at max, but we can't set an infinite decay phase in sf2 or dls
    if (Dr <= 1)
      rgn->sustain_level = 1.0;
    else
      rgn->sustain_level = Sl / (double) 0xFFFF;

    // Sustain rate 138D in sfa2
    ticks = Sr ? 0xFFFF / Sr : 0;
    rgn->sustain_time = (Sr == 0xFFFF) ? 0 : ticks * QSOUND_TICK_FREQ;
    rgn->sustain_time = LinAmpDecayTimeToLinDBDecayTime(rgn->sustain_time, 0x800);

    ticks = Rr ? 0xFFFF / Rr : 0;
    rgn->release_time = (Rr == 0xFFFF) ? 0 : ticks * QSOUND_TICK_FREQ;
    rgn->release_time = LinAmpDecayTimeToLinDBDecayTime(rgn->release_time, 0x800);

    if (rgn->sampNum == 0xFFFF || rgn->sampNum >= ((CPSInstrSet *) parInstrSet)->sampInfoTable->numSamples)
      rgn->sampNum = 0;

    rgn->SetUnityKey(((CPSInstrSet *) parInstrSet)->sampInfoTable->infos[rgn->sampNum].unity_key);
    aRgns.push_back(rgn);
  }
  return true;
}


// **************
// CPSSampColl
// **************

CPSSampColl::CPSSampColl(RawFile *file,
                         CPSInstrSet *theinstrset,
                         CPSSampleInfoTable *sampinfotable,
                         uint32_t offset,
                         uint32_t length,
                         wstring name)
    : VGMSampColl(CPSFormat::name, file, offset, length, name),
      instrset(theinstrset),
      sampInfoTable(sampinfotable) {

}


bool CPSSampColl::GetHeaderInfo() {
  unLength = this->rawfile->size();
  return true;
}

bool CPSSampColl::GetSampleInfo() {

  uint32_t numSamples = instrset->sampInfoTable->numSamples;
  uint32_t baseOffset;

  for (uint32_t i = 0; i < numSamples; i++) {
    wostringstream name;
    name << L"Sample " << i;

    sample_info& sampInfo = sampInfoTable->infos[i];
    // Base address correction for Strider2, which maps qsound start address to 200000h.
    // We assume first bank listed is base address.
    if (i == 0)
      baseOffset = sampInfo.start_addr & 0xFF0000;
    uint32_t sampOffset = sampInfo.start_addr - baseOffset;
    uint32_t sampLength = sampInfo.end_addr - sampInfo.start_addr;

    // Sanity check for mshvsf, which has samples of sizez 2. Fluidsynth does not like this (perhaps violates sf2 spec?)
    if (sampLength <= 2) {
      sampLength = 100;
    }

    uint32_t relativeLoopOffset = sampInfo.loop_offset - sampOffset;

    if (relativeLoopOffset > (uint32_t) sampLength)
      relativeLoopOffset = sampLength;
    if (sampLength == 0 || sampOffset > unLength)
      break;

    uint32_t frequency = instrset->fmt_version == VER_CPS3 ? 37000 : 24000;
    VGMSamp *newSamp = AddSamp(sampOffset, sampLength, sampOffset, sampLength, 1, 8, frequency, name.str());
    newSamp->SetWaveType(WT_PCM8);
    if (sampLength - relativeLoopOffset < 40)
      newSamp->SetLoopStatus(false);
    else {
      newSamp->SetLoopStatus(true);
      newSamp->SetLoopOffset(relativeLoopOffset);
      newSamp->SetLoopLength(sampLength - relativeLoopOffset);
    }
    newSamp->unityKey = sampInfo.unity_key;
  }
  return true;
}
