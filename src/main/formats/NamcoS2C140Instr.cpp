#include "pch.h"
#include "NamcoS2C140Instr.h"
#include "NamcoS2Format.h"
#include "NamcoS2Seq.h"
#include "VGMSamp.h"
#include "VGMRgn.h"
#include "ScaleConversion.h"
#include "RawFile.h"


// *********
// C140Artic
// *********

void C140Artic::Load(RawFile* file, uint32_t offset) {
  while (file->GetByte(offset) != 0) {
    C140ArticSection section = { file->GetByte(offset), file->GetByte(offset+1) };
    upwardSegments.push_back(section);
    offset += 2;
  }
  if (file->GetByte(offset+1) == 0xFF)
    return;
  offset += 2;
  while (file->GetByte(offset) != 0) {
    C140ArticSection section = { file->GetByte(offset), file->GetByte(offset+1) };
    downwardSegments.push_back(section);
    offset += 2;
  }
}

double C140Artic::attackTime() {
  auto level = (upwardSegments.front().level << 8);
  auto rate = envRateTable[upwardSegments.front().rate];
  return (level / rate) / (double)NAMCOS2_IRQ_HZ;
}

double C140Artic::decayTime(uint8_t hold) {

//  if (sustainLevel() <= 0.2) {
//  if (sustainLevel() > 0) {
//    if (upwardSegments.size() <= 1) {
//      return 0;
//    }
//
//    auto level = (upwardSegments.back().level << 8);
//    auto rate = envRateTable[upwardSegments.back().rate];
//    auto prevPeak = upwardSegments.front().level << 8;
//    return (0xFFFF / rate) / (double) NAMCOS2_IRQ_HZ;
//  }

//  if (hold) {
//
//    if (upwardSegments.size() <= 1) {
//      return 0;
//    }
//
//    // set the decay time to total descent time
//    auto prevPeak = 0xFFFF;
//    double descentTicks = 0;
//    for (int i = 1; i < upwardSegments.size(); i++) {
//      auto &segment = upwardSegments[i];
//      auto level = (segment.level << 8);
//      auto rate = envRateTable[segment.rate];
//      descentTicks += (prevPeak - level) / rate;
//      prevPeak = level;
//    }
//    return LinAmpDecayTimeToLinDBDecayTime(descentTicks / (double) NAMCOS2_IRQ_HZ, 0x800);
//  }

  auto level = (upwardSegments.back().level << 8);
  auto rate = envRateTable[upwardSegments.back().rate];
  auto prevPeak = 0xFFFF;//upwardSegments.front().level << 8;
  double descentTicks = (prevPeak - level) / rate;
  prevPeak = level;
  for (int i=0; i<downwardSegments.size(); i++) {
    auto& segment = downwardSegments[i];
    auto level = (segment.level << 8);
    auto rate = envRateTable[segment.rate];
    descentTicks += (prevPeak - level) / rate;
    prevPeak = level;
  }
  descentTicks += hold;
  return LinAmpDecayTimeToLinDBDecayTime(descentTicks / (double) NAMCOS2_IRQ_HZ, 0x800);
}

double C140Artic::sustainLevel() {

//  if (downwardSegments.size() > 0) {
//    return (downwardSegments.front().level << 8) / (double)0x10000;
//  }
//  auto level = (double)(upwardSegments.back().level << 8) / (double)0x10000;
//  if (level <= 0.3) {
//    return level;
//  }
//  else return 0;
//  return 0;

//  if (!hold) {
    return 0;
//  }
//  return (double)(upwardSegments.back().level << 8) / (double)0xFFFF;
}

double C140Artic::sustainTime() {
  return 0;
}

double C140Artic::releaseTime() {

//  if (!hold) {
    return 0;
//  }

//  if (downwardSegments.size() == 0) {
//    return 0;
//  }
//  auto prevPeak = 0xFFFF;
//  double descentTicks = 0;
//  for (int i=0; i<downwardSegments.size(); i++) {
//    auto& segment = downwardSegments[i];
//    auto level = (segment.level << 8);
//    auto rate = envRateTable[segment.rate];
//    descentTicks += (prevPeak - level) / rate;
//    prevPeak = level;
//  }
//  return LinAmpDecayTimeToLinDBDecayTime(descentTicks / (double) NAMCOS2_IRQ_HZ, 0x800);
}

// *****************
// NamcoS2ArticTable
// *****************

NamcoS2ArticTable::NamcoS2ArticTable(RawFile *file, uint32_t offset, uint32_t length, uint32_t bankOffset)
    : VGMMiscFile(NamcoS2Format::name, file, offset, length, L"Namco S2 Artic Table"),
      bankOffset(bankOffset) {
}

NamcoS2ArticTable::~NamcoS2ArticTable(void) {
//  if (artics)
//    delete[] artics;
}

bool NamcoS2ArticTable::LoadMain() {
  uint32_t off = dwOffset;

  // Assume first pointer points to end of table
  uint16_t ptrTableLength = GetShortBE(dwOffset) + bankOffset -  dwOffset;

  for (uint16_t i = 0; i*2 < ptrTableLength; i++) {
    C140Artic artic;
    artic.Load(this->rawfile, bankOffset + GetShortBE(dwOffset+i*2));
    artics.push_back(artic);
  }

//  for (int i = 0; off < dwOffset + unLength; i++, off += C140_ARTIC_SIZE) {
//
//    wostringstream name;
//    name << L"Articulation " << i;
//    VGMContainerItem *containerItem = new VGMContainerItem(this, off, C140_ARTIC_SIZE, name.str());
//    containerItem->AddSimpleItem(off, 1, L"Attack Rate");
//    containerItem->AddSimpleItem(off + 1, 1, L"Decay Rate");
//    containerItem->AddSimpleItem(off + 2, 1, L"Sustain Level");
//    containerItem->AddSimpleItem(off + 3, 1, L"Sustain Rate");
//    containerItem->AddSimpleItem(off + 4, 1, L"Release Rate");
//    containerItem->AddSimpleItem(off + 5, 3, L"Unknown");
//    this->AddItem(containerItem);
//  }
  //unLength = off - dwOffset;

  return true;
}




// *******************
// NamcoS2C140InstrSet
// *******************

NamcoS2C140InstrSet::NamcoS2C140InstrSet(
    RawFile *file,
    uint32_t offset,
    NamcoS2SampColl* sampColl,
    NamcoS2ArticTable* articTable,
    shared_ptr<InstrMap> instrMap,
    const std::wstring &name
) :
    VGMInstrSet(NamcoS2Format::name, file, offset, 0, name),
    sampColl(sampColl),
    articTable(articTable),
    instrMap(instrMap)
{

}

NamcoS2C140InstrSet::~NamcoS2C140InstrSet() {
}

bool NamcoS2C140InstrSet::GetHeaderInfo() {
  return true;
}

bool NamcoS2C140InstrSet::GetInstrPointers() {

  for(auto it = instrMap->begin(); it != instrMap->end(); ++it) {
    auto key = it->first;

    uint8_t s = (key >> 16) & 0xFF;
    uint8_t a = (key >> 8) & 0xFF;
    uint8_t h = key & 0xFF;

    uint8_t bank = it->second / 128;
    uint8_t prog = it->second % 128;

    auto& artic = articTable->artics[a];
    auto instr = new VGMInstr(this, dwOffset, 0, bank, prog);
    auto rgn = new VGMRgn(instr, 0);
    rgn->keyLow = 0x0;
    rgn->keyHigh = 0x7F;
    rgn->sampNum = s;
    rgn->attack_time = artic.attackTime();
//    rgn->hold_time = h / (double)NAMCOS2_IRQ_HZ;
    rgn->decay_time = 20.0;//artic.decayTime(h);
    rgn->sustain_level = artic.sustainLevel();
    rgn->sustain_time = artic.sustainTime();
    rgn->release_time = artic.releaseTime();

    printf("a: %f h: %f d: %f sl: %f\n", rgn->attack_time, rgn->hold_time, rgn->decay_time, rgn->sustain_level);

    instr->AddRgn(rgn);
    aInstrs.push_back(instr);
  }

  return true;
}


// ****************
// NamcoS2C140Instr
// ****************

NamcoS2C140Instr::NamcoS2C140Instr(VGMInstrSet *instrSet,
                                   uint32_t offset,
                                   uint32_t length,
                                   uint32_t theBank,
                                   uint32_t theInstrNum,
                                   const std::wstring &name) :
    VGMInstr(instrSet, offset, length, theBank, theInstrNum, name) {
}

NamcoS2C140Instr::~NamcoS2C140Instr() {
}

bool NamcoS2C140Instr::LoadInstr() {

  auto rgn = new VGMRgn(this, dwOffset, 0);
//  uint16_t addrSampStart = GetShort(offDirEnt);

//  CapcomSnesRgn *rgn = new CapcomSnesRgn(this, dwOffset);
//  rgn->sampOffset = addrSampStart - spcDirAddr;
//  aRgns.push_back(rgn);

  return true;
}

// **************
// C140SampleInfo
// **************

bool C140SampleInfo::loops() {
  return mode & 0x10 != 0;
}

uint32_t C140SampleInfo::realStartAddr() {
  return getAddr(start_addr, bank);
}

uint32_t C140SampleInfo::realEndAddr() {
  return getAddr(end_addr, bank);
}

uint32_t C140SampleInfo::realLoopAddr() {
  return getAddr(loop_addr, bank);
}

uint32_t C140SampleInfo::getAddr(uint32_t adrs, uint32_t bank) {
  adrs=(bank<<16)+adrs;
  return ((adrs&0x200000)>>2)|(adrs&0x7ffff);
}

// **********************
// NamcoS2SampleInfoTable
// **********************

NamcoS2SampleInfoTable::NamcoS2SampleInfoTable(RawFile *file, uint32_t offset, uint32_t samplesFileLength)
    : VGMMiscFile(NamcoS2Format::name, file, offset, 0, L"Namco S2 Sample Table"),
      samplesFileLength(samplesFileLength)
{
}

NamcoS2SampleInfoTable::~NamcoS2SampleInfoTable(void) {
}

bool NamcoS2SampleInfoTable::LoadMain() {


//  for (int i = 0; (test1 || test2) && ((test1 != 0xFFFFFFFF) || (test2 != 0xFFFFFFFF)) && off < dwOffset + unLength;
//       i++, off += 8) {
//    test1 = GetWord(off + 8);
//    test2 = GetWord(off + 12);
//
//    wostringstream name;
//    name << L"Sample Info " << i;
//
//    VGMContainerItem *containerItem = new VGMContainerItem(this, off, sizeof(qs_samp_info_cps2), name.str());
//    containerItem->AddSimpleItem(off + 0, 1, L"Bank");
//    containerItem->AddSimpleItem(off + 1, 2, L"Offset");
//    containerItem->AddSimpleItem(off + 3, 2, L"Loop Offset");
//    containerItem->AddSimpleItem(off + 5, 2, L"End Offset");
//    containerItem->AddSimpleItem(off + 7, 1, L"Unity Key");
//    this->AddItem(containerItem);
//  }


  uint32_t off = 0;
  while (true) {

    auto info = C140SampleInfo();
    info.bank = GetByte(dwOffset + off + 0);
    info.mode = GetByte(dwOffset + off + 1);
    info.start_addr = GetShortBE(dwOffset + off + 2);
    info.end_addr = GetShortBE(dwOffset + off + 4);
    info.loop_addr = GetShortBE(dwOffset + off + 6);
    info.freq = GetShortBE(dwOffset + off + 8);

    if (info.start_addr > info.end_addr ||
        (info.loops() && ((info.loop_addr > info.end_addr) || (info.start_addr > info.loop_addr))) ||
        info.realEndAddr() >= samplesFileLength ||
        (info.mode & 0xE0) != 0 ) {
      break;
    }

    infos.push_back(info);
    off += C140_SAMPINFO_SIZE;
  }
  unLength = off;

  return true;
}


// ***************
// NamcoS2SampColl
// ***************

NamcoS2SampColl::NamcoS2SampColl(RawFile *file,
                         NamcoS2SampleInfoTable *sampinfotable,
                         uint32_t offset,
                         uint32_t length,
                         wstring name)
    : VGMSampColl(NamcoS2Format::name, file, offset, length, name),
      sampInfoTable(sampinfotable) {

}


bool NamcoS2SampColl::GetHeaderInfo() {
  unLength = this->rawfile->size();
  return true;
}

bool NamcoS2SampColl::GetSampleInfo() {
  for (uint32_t i = 0; i < sampInfoTable->infos.size(); i++) {
    auto& info = sampInfoTable->infos[i];

    // NOTE: burning force driver has logic to transform bank if byte at shared mem addr 7013 & 0x10 is set. Code at E112
    const uint32_t offset = info.realStartAddr();//FindSample(info.start_addr, info.bank, 0);
    const uint32_t endOffset = info.realEndAddr();//FindSample(info.end_addr, info.bank, 0);
    const uint32_t loopOffset = info.realLoopAddr();//FindSample(info.loop_addr, info.bank, 0);
    const uint32_t sampLength = endOffset - offset;

//    printf("sample offset: %X\n", offset);

    wostringstream name;
    name << L"Sample " << i;

    const uint32_t frequency = (49152000 / 384 / 6); // 21,333.33Hz
    VGMSamp *samp = AddSamp(offset, sampLength, offset, sampLength, 1, 8, frequency, name.str());
    samp->SetWaveType(WT_PCM8);

    if ((info.mode & 0x10) != 0) {
      samp->SetLoopStatus(true);
      samp->SetLoopOffset(loopOffset - offset);
      samp->SetLoopLength(endOffset - loopOffset);
    }
    else {
      samp->SetLoopStatus(false);
    }

    double f = (double)info.freq;
//    double cents = 17.3123 * log(0.00000194694*f);
//    double cents = 1731.23*log(0.000194694*f);
//    double cents = -(((12 * (-log(f) + log(5120)))/(log(f))) * 100) - 100;

    // 0x12F0 * 5 is (still unclear to me why) the c140 hardware freq value that seems to play note at normal
    // 21333Hz rate. In the driver, final freq value is determined by a keycode freq table lookup
    // times the freq value in the samp info, right shifted 8 (divide by 256).
    // If we take (0x12F0 * 5) >> 8 and divide by the keycode freq table val for middle c
    // (we'll use middle c for unity key), the result is 5120.
    // Now using 5120 as the freq for fineTune of 0, we calculate fineTune using the equation,
    // fn = f0 * (a)^n where a == (2)^(1/12), solving for n.
    // see: http://www.phy.mtu.edu/~suits/NoteFreqCalcs.html
    int16_t cents = (int16_t)round(-100 * ((12 * log(5120 / f)) / log(2)));
    samp->fineTune = (int16_t)(cents % 100);
    samp->unityKey = (uint8_t)(0x3C - (cents / 100));
//    if (f == 0x1BC8) {
//      samp->fineTune = -1200;
//    }
    printf("freq: %X   finetune: %d cents\n", info.freq, (int16_t)cents);


//    samp->unityKey = 0x3C;

//    samples.push_back(samp);
  }
  return true;
}


// NOTE: Code taken from MAME c140.cpp
//   find_sample: compute the actual address of a sample given it's
//   address and banking registers, as well as the board type.
//uint32_t NamcoS2SampColl::FindSample(uint32_t adrs, uint32_t bank, uint8_t voice)
//{
//  uint32_t newadr = 0;
//
////  static const int16_t asic219banks[4] = { 0x1f7, 0x1f1, 0x1f3, 0x1f5 };
//
//  adrs=(bank<<16)+adrs;
//
//  newadr = ((adrs&0x200000)>>2)|(adrs&0x7ffff);
//
////  switch (m_banking_type)
////  {
////    case C140_TYPE::SYSTEM2:
//      // System 2 banking
////      newadr = ((adrs&0x200000)>>2)|(adrs&0x7ffff);
////      break;
//
////    case C140_TYPE::SYSTEM21:
////      // System 21 banking.
////      // similar to System 2's.
////      newadr = ((adrs&0x300000)>>1)+(adrs&0x7ffff);
////      break;
////
////    case C140_TYPE::ASIC219:
////      // ASIC219's banking is fairly simple
////      newadr = ((m_REG[asic219banks[voice/4]]&0x3) * 0x20000) + adrs;
////      break;
////  }
//
//  return (newadr);
//}
