#pragma once
#include "VGMSeqNoTrks.h"
#include "SeqTrack.h"
#include "NamcoS2Format.h"

#define NAMCOS2_IRQ_HZ 120

typedef map<uint32_t, uint32_t> InstrMap;

enum NamcoS2FMEventType {
  S2FM_OPC_00_NOP = 0,
  S2FM_OPC_01_MASTERVOL,
  S2FM_OPC_02_SET_DELTA_MULTIPLE,
  S2FM_OPC_03_SET_DELTA,
  S2FM_OPC_04_CALL,
  S2FM_OPC_05_RETURN,
  S2FM_OPC_06_NOTE,
  S2FM_OPC_07_PROG_CHANGE,
  S2FM_OPC_08_VOLUME,
  S2FM_OPC_09_JUMP,
  S2FM_OPC_0A_LOOP,
  S2FM_OPC_0B_LOOP_BREAK,
  S2FM_OPC_0C_PITCHBEND_HI,
  S2FM_OPC_0D_PITCHBEND_LO,
  S2FM_OPC_0E,
  S2FM_OPC_0F,
  S2FM_OPC_10,
  S2FM_OPC_11_DURATION,
  S2FM_OPC_12,
  S2FM_OPC_13,
  S2FM_OPC_14,
  S2FM_OPC_15_LFO,
  S2FM_OPC_16_RLENABLE_FEEDBACK_CONNECTION,
  S2FM_OPC_17_NOP,
  S2FM_OPC_18_NOP,
  S2FM_OPC_19,
  S2FM_OPC_1A_NOP,
  S2FM_OPC_1B_NOP,
  S2FM_OPC_1C,
  S2FM_OPC_1D_LOAD_FM_SECTION,
  S2FM_OPC_1E_LOAD_C140_SECTION,
  S2FM_OPC_1F_NOP,
  S2FM_OPC_20_NOP,
  S2FM_OPC_21_NOP,
  S2FM_OPC_22
};

enum NamcoS2C140EventType {
  S2C140_OPC_00_NOP = 0,
  S2C140_OPC_01_MASTERVOL,
  S2C140_OPC_02_SET_DELTA_MULTIPLE,
  S2C140_OPC_03_SET_DELTA,
  S2C140_OPC_04_CALL,
  S2C140_OPC_05_RETURN,
  S2C140_OPC_06_NOTE,
  S2C140_OPC_07_PROG_CHANGE,
  S2C140_OPC_08_VOLUME,
  S2C140_OPC_09_JUMP,
  S2C140_OPC_0A_LOOP,
  S2C140_OPC_0B_LOOP_BREAK,
  S2C140_OPC_0C_PITCHBEND_HI,
  S2C140_OPC_0D_PITCHBEND_LO,
  S2C140_OPC_0E,
  S2C140_OPC_0F,
  S2C140_OPC_10,
  S2C140_OPC_11_DURATION,
  S2C140_OPC_12,
  S2C140_OPC_13_ARTICULATION,
  S2C140_OPC_14,
  S2C140_OPC_15_NOP,
  S2C140_OPC_16_PAN,
  S2C140_OPC_17,
  S2C140_OPC_18_NOP,
  S2C140_OPC_19,
  S2C140_OPC_1A_NOP,
  S2C140_OPC_1B,
  S2C140_OPC_1C,
  S2C140_OPC_1D_LOAD_FM_SECTION,
  S2C140_OPC_1E_LOAD_C140_SECTION,
  S2C140_OPC_1F,
  S2C140_OPC_20_NOP,
  S2C140_OPC_21_NOP,
  S2C140_OPC_22
};

// Namco System 2 songs are composed of - what I'm terming - sections.
// A section is essentially a trackless, multichannel sequence (think SMF Type 0).
// The YM2151 (FM) and C140 use separate sections.
// A section covers a range of up to 8 channels. Multiple sections can be loaded at once
// each covering a unique set of channels.
// The weird thing is that sections can (and do) load other sections via sequence events.
// What I'm finding is that, for a song to be played, a single c140 section is queued
// into shared memory by the maincpu, then that section loads all other sections in addition
// to defining sequence events for its own channel range.

struct ChannelState {
 public:
  void Reset(void) {
    prevKey = 0;
    volume = 0;
    transpose = 0;

    samp = 0;
    artic = 0;
    hold = 0;
    curInstrKey = 0xFFFFFFFF;

    noteIsPlaying = false;
  }
  uint8_t prevKey;
  uint8_t volume;
  int8_t transpose;

  uint8_t samp;
  uint8_t artic;
  uint8_t hold;
  uint32_t curInstrKey;

  bool noteIsPlaying;
};


struct SectionState {
  SectionState() : active(false), curChannel(0), curOffset(0) {}
 public:
  void Reset(void) {
    for (auto& chanState: chanStates) {
      chanState.Reset();
    }
    active = false;
    startingTrack = 0;
    bankOffset = 0;
    curChannel = 0;
    curOffset = 0;
    time = 0;
    delta = 0;
    deltaMultiplier = 0;
    memset(databyteList, 0,  sizeof(databyteList));
    callIndex = 0;
    memset(callOffsets, 0,  sizeof(callOffsets));
    loopIndex = 0;
    memset(loopCounts, 0,  sizeof(loopCounts));
    memset(loopOffsets, 0,  sizeof(loopOffsets));
    loopBreakIndex = 0;
    memset(loopBreakCounts, 0,  sizeof(loopBreakCounts));
    memset(loopBreakOffsets, 0,  sizeof(loopBreakOffsets));
  }

  bool active;
  ChannelState chanStates[8];
  uint8_t startingTrack;
  uint32_t bankOffset;
  uint16_t curChannel;
  uint32_t curOffset;
  uint32_t time;
  uint8_t delta;
  uint8_t deltaMultiplier;
  uint8_t databyteList[8];
  uint8_t callIndex;
  uint32_t callOffsets[8];
  uint8_t loopIndex;
  uint8_t loopCounts[10];
  uint32_t loopOffsets[10];
  uint8_t loopBreakIndex;
  uint8_t loopBreakCounts[10];
  uint32_t loopBreakOffsets[10];
};

class NamcoS2Seq
    : public VGMSeqNoTrks {
 public:
  NamcoS2Seq(RawFile *file,
             uint32_t c140BankOffset,
             uint32_t ym2151BankOffset,
             uint8_t startingC140Index,
             uint32_t seqdataOffset,
             shared_ptr<InstrMap> instrMap,
             std::wstring newName = L"Namco S2 Seq");
  virtual ~NamcoS2Seq(void);

  virtual bool GetHeaderInfo(void);
  virtual void ResetVars(void);
  virtual bool ReadEvent(void);
//  virtual bool PostLoad(void);

 private:
  uint8_t startingC140SectionIndex;
  uint32_t c140BankOffset;
  uint32_t ym2151BankOffset;
  uint8_t numC140Sections;
  uint8_t numFMSections;
  SectionState fmSectionStates[8];
  SectionState c140SectionStates[8];
  shared_ptr<InstrMap> c140InstrMap;;

  void LoadC140Section(uint8_t sectionIndex);
  void LoadFMSection(uint8_t sectionIndex);

  bool ReadFMEvent(SectionState& state);
  bool ReadC140Event(SectionState& state);

  uint32_t instrKey(uint8_t samp, uint8_t artic, uint8_t hold);
  void AddC140ProgramChangeIfNeeded(ChannelState& chanState);
};
