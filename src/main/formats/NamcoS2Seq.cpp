#include "pch.h"
#include "NamcoS2Seq.h"

DECLARE_FORMAT(NamcoS2);

//  **********
//  NamcoS2Seq
//  **********

#define MAX_TRACKS  24
#define SEQ_PPQN    72
//#define SEQ_PPQN    72

NamcoS2Seq::NamcoS2Seq(
    RawFile *file,
    uint32_t c140BankOff,
    uint32_t ym2151BankOff,
    uint8_t startingC140Index,
    uint32_t seqdataOffset,
    shared_ptr<InstrMap> instrMap,
    std::wstring newName)
    : VGMSeqNoTrks(NamcoS2Format::name, file, c140BankOff+seqdataOffset, newName),
      c140BankOffset(c140BankOff),
      ym2151BankOffset(ym2151BankOff),
      startingC140SectionIndex(startingC140Index),
      numC140Sections(0),
      numFMSections(0),
      c140InstrMap(instrMap)
{
  bAllowDiscontinuousTrackData = true;
  bUseLinearAmplitudeScale = true;

//  AlwaysWriteInitialTempo((NAMCOS2_IRQ_HZ * 60.0) / SEQ_PPQN);
}

NamcoS2Seq::~NamcoS2Seq(void) {
}

uint32_t NamcoS2Seq::instrKey(uint8_t samp, uint8_t artic, uint8_t hold) {
  return (samp << 16) + (artic << 8) + hold;
}

void NamcoS2Seq::AddC140ProgramChangeIfNeeded(ChannelState& chanState) {
  auto key = instrKey(chanState.samp, chanState.artic, chanState.hold);
  if (key == chanState.curInstrKey)
    return;

  uint32_t progNum;
  auto it = c140InstrMap->find(key);
  if (it != c140InstrMap->end()) {
    progNum = it->second;
  }
  else {
    uint32_t progNum = c140InstrMap->size();
    (*c140InstrMap)[key] = progNum;
  }
  uint8_t bank = progNum / 128;
  uint8_t instr = progNum % 128;

  AddBankSelectNoItem(bank);
  AddProgramChangeNoItem(instr, false);
  chanState.curInstrKey = key;
}



// For YM2151 Sections, the first byte of the section header will be 0x20. For the C140, it can be either 0x20 or 0x21.
// 0x20 means the section applies to channels 0-7. 0x21 means channels 8-15.
// The second byte is a bitfield indicating which channels in that range will actually be used by the section.

void NamcoS2Seq::LoadC140Section(uint8_t sectionIndex) {
  uint16_t relSeqOffset = GetShortBE(c140BankOffset + GetShortBE(c140BankOffset) + sectionIndex * sizeof(uint16_t));

  const uint32_t seqOffset = c140BankOffset + relSeqOffset;
  if (seqOffset < VGMSeq::dwOffset) {
    VGMSeq::dwOffset = seqOffset;
  }

  auto& state = c140SectionStates[numC140Sections];
  uint8_t chanRange = (GetByte(seqOffset) & 0x0F) * 8;
  state.startingTrack = chanRange;
  state.bankOffset = c140BankOffset;
  state.curOffset = seqOffset+2;
  state.active = true;
  state.time = GetTime();  // based on mame debug tests of burning force, this technically should be GetTime() + 1, not sure I want to make the change though.
  numC140Sections++;
}

void NamcoS2Seq::LoadFMSection(uint8_t sectionIndex) {
  uint16_t relSeqOffset  = GetShortBE(ym2151BankOffset + GetShortBE(ym2151BankOffset) + sectionIndex * sizeof(uint16_t));

  const uint32_t seqOffset = ym2151BankOffset + relSeqOffset;
  if (seqOffset < VGMSeq::dwOffset) {
    VGMSeq::dwOffset = seqOffset;
  }

  auto& state = fmSectionStates[numFMSections];
  state.startingTrack = 16;
  state.bankOffset = ym2151BankOffset;
  state.curOffset = seqOffset+2;
  state.active = true;
  state.time = GetTime();
  numFMSections++;
}

void NamcoS2Seq::ResetVars(void) {
  VGMSeqNoTrks::ResetVars();

  for (auto& state: c140SectionStates) {
    state.Reset();
  }
  for (auto& state: fmSectionStates) {
    state.Reset();
  }
  numC140Sections = 0;
  numFMSections = 0;

  LoadC140Section(startingC140SectionIndex);
}

bool NamcoS2Seq::GetHeaderInfo(void) {
//  SetPPQN(SEQ_PPQN);
  nNumTracks = MAX_TRACKS;

  SetEventsOffset(VGMSeq::dwOffset);
  return true;
}

bool NamcoS2Seq::ReadEvent(void) {

  bool shouldContinue = false;

  for (uint8_t i=0; i<numC140Sections; i++) {
    auto& state = c140SectionStates[i];

    if (!state.active)
      continue;

    SetTime(state.time);
    shouldContinue |= ReadC140Event(state);
    state.active = shouldContinue;
    state.time = GetTime();
  }

  for (int i=0; i<numFMSections; i++) {
    auto& state = fmSectionStates[i];

    if (!state.active)
      continue;

    SetTime(state.time);
    shouldContinue |= ReadFMEvent(state);
    state.active = shouldContinue;
    state.time = GetTime();
  }
  return shouldContinue;
}


