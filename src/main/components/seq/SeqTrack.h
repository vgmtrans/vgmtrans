/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include <sstream>
#include <unordered_set>
#include <vector>
#include "VGMItem.h"
#include "VGMSeq.h"
#include <spdlog/common.h>
#include "LogManager.h"
#include "SynthType.h"

class VGMSeq;
class SeqEvent;
class MidiTrack;

enum ReadMode : uint8_t;

enum class LevelController : uint8_t {
  Volume,
  Expression,
  MasterVolume,
};

enum class Resolution : uint8_t {
  SevenBit,
  FourteenBit,
};

struct LoopState {
  uint32_t endOffset;
  uint32_t remainingCount;

  bool operator==(const LoopState &other) const = default;
};

struct ControlFlowState {
  uint32_t offset;
  std::vector<uint32_t> returnStack;
  std::vector<LoopState> loopStack;

  bool operator==(const ControlFlowState &other) const = default;
};

struct ControlFlowStateHasher {
  static inline void hashCombine(std::size_t& seed, std::size_t v) noexcept {
    // 64-bit and 32-bit friendly “golden ratio” constant
    constexpr std::size_t k = (sizeof(std::size_t) == 8)
      ? 0x9e3779b97f4a7c15ull
      : 0x9e3779b9ul;
    seed ^= v + k + (seed << 6) + (seed >> 2);
  }

  std::size_t operator()(const ControlFlowState& s) const noexcept {
    std::size_t seed = std::hash<uint32_t>{}(s.offset);

    // Mix return stack (length + elements)
    hashCombine(seed, s.returnStack.size());
    for (uint32_t v : s.returnStack)
      hashCombine(seed, v);

    // Mix loop stack (length + each loop’s fields)
    hashCombine(seed, s.loopStack.size());
    for (const auto& lp : s.loopStack) {
      hashCombine(seed, lp.endOffset);
      hashCombine(seed, lp.remainingCount);
    }
    return seed;
  }
};

class SeqTrack : public VGMItem {
 public:
  SeqTrack(VGMSeq *parentSeqFile, uint32_t offset = 0, uint32_t length = 0, std::string name = "Track");

  [[nodiscard]] bool usesLinearAmplitudeScale() const {
    return m_useLinearAmpScale || parentSeq->usesLinearAmplitudeScale();
  }
  void setUseLinearAmplitudeScale(bool set) { m_useLinearAmpScale = set; }

  virtual bool loadTrackInit(int trackNum, MidiTrack *preparedMidiTrack);
  virtual void loadTrackMainLoop(uint32_t stopOffset, int32_t stopTime);

protected:
  virtual void resetVars();
  void resetVisitedAddresses();

  virtual void setChannelAndGroupFromTrkNum(int theTrackNum);
  virtual void addInitialMidiEvents(int trackNum);
  virtual bool readEvent();
  virtual void onTickBegin() {}
  virtual void onTickEnd() {}

  uint32_t readVarLen(uint32_t &offset) const;
  uint32_t getTime() const;
  virtual void setTime(uint32_t newTime);
  virtual void addTime(uint32_t delta);

  virtual bool isOffsetUsed(uint32_t offset);

  virtual bool onEvent(uint32_t offset, uint32_t length);
  virtual void addEvent(SeqEvent *pSeqEvent);

  bool shouldTrackControlFlowState() const;
  void addControlFlowState(uint32_t destinationOffset);
  bool checkControlStateForInfiniteLoop(u32 offset);
  void pushReturnOffset(uint32_t returnOffset);
  bool popReturnOffset(uint32_t &returnOffset);

 private:
  void addControllerSlide(u32 dur, u16 &prevVal, u16 targVal, uint8_t (*scalerFunc)(uint8_t), void (MidiTrack::*insertFunc)(uint8_t, uint8_t, uint32_t)) const;
  double applyLevelCorrection(double level, LevelController controller) const;
  void addLevelNoItem(double level, LevelController controller, Resolution res, int absTime = -1);

 public:
  void addGenericEvent(uint32_t offset, uint32_t length, const std::string &sEventName, const std::string &sEventDesc, Type type);
  void addSetOctave(uint32_t offset, uint32_t length, uint8_t newOctave, const std::string &sEventName = "Set Octave");
  void addIncrementOctave(uint32_t offset, uint32_t length, const std::string &sEventName = "Increment Octave");    // 1,Sep.2009 revise
  void addDecrementOctave(uint32_t offset, uint32_t length, const std::string &sEventName = "Decrement Octave");    // 1,Sep.2009 revise
  void addRest(uint32_t offset, uint32_t length, uint32_t restTime, const std::string &sEventName = "Rest");
  void addHold(uint32_t offset, uint32_t length, const std::string &sEventName = "Hold");
  void addUnknown(uint32_t offset, uint32_t length, const std::string &sEventName = "Unknown Event", const std::string &sEventDesc = "");

  void addNoteOn(uint32_t offset, uint32_t length, int8_t key, int8_t vel, const std::string &sEventName = "Note On");
  void addNoteOnNoItem(int8_t key, int8_t vel);
  void addPercNoteOn(uint32_t offset, uint32_t length, int8_t key, int8_t vel, const std::string &sEventName = "Percussion Note On");
  void addPercNoteOnNoItem(int8_t key, int8_t vel);
  void insertNoteOn(uint32_t offset, uint32_t length, int8_t key, int8_t vel, uint32_t absTime, const std::string &sEventName = "Note On");

  void addNoteOff(uint32_t offset, uint32_t length, int8_t key, const std::string &sEventName = "Note Off");
  void addNoteOffNoItem(int8_t key);
  void addPercNoteOff(uint32_t offset, uint32_t length, int8_t key, const std::string &sEventName = "Percussion Note Off");
  void addPercNoteOffNoItem(int8_t key);
  void insertNoteOff(uint32_t offset, uint32_t length, int8_t key, uint32_t absTime, const std::string &sEventName = "Note Off");
  void insertNoteOffNoItem(int8_t key, uint32_t absTime) const;

  void addNoteByDur(uint32_t offset, uint32_t length, int8_t key, int8_t vel, uint32_t dur, const std::string &sEventName = "Note with Duration");
  void addNoteByDurNoItem(int8_t key, int8_t vel, uint32_t dur);
  void addNoteByDur_Extend(uint32_t offset, uint32_t length, int8_t key, int8_t vel, uint32_t dur, const std::string &sEventName = "Note with Duration (Extended)");
  void addNoteByDurNoItem_Extend(int8_t key, int8_t vel, uint32_t dur);
  void addPercNoteByDur(uint32_t offset, uint32_t length, int8_t key, int8_t vel, uint32_t dur, const std::string &sEventName = "Percussion Note with Duration");
  void addPercNoteByDurNoItem(int8_t key, int8_t vel, uint32_t dur);
  void insertNoteByDur(uint32_t offset, uint32_t length, int8_t key, int8_t vel, uint32_t dur, uint32_t absTime, const std::string &sEventName = "Note On With Duration");
  void insertNoteByDurNoItem(int8_t key, int8_t vel, uint32_t dur, uint32_t absTime);

  void makePrevDurNoteEnd() const;
  void makePrevDurNoteEnd(uint32_t absTime) const;
  void limitPrevDurNoteEnd() const;
  void limitPrevDurNoteEnd(uint32_t absTime) const;
  void addVol(uint32_t offset, uint32_t length, uint8_t vol, const std::string &sEventName = "Volume");
  void addVolNoItem(uint8_t vol);
  void addVol(u32 offset, u32 length, double volPercent, Resolution res, const std::string &sEventName = "Volume");
  void addVolSlide(uint32_t offset, uint32_t length, uint32_t dur, uint8_t targVol, const std::string &sEventName = "Volume Slide");
  void insertVol(uint32_t offset, uint32_t length, uint8_t vol, uint32_t absTime, const std::string &sEventName = "Volume");
  void addExpression(uint32_t offset, uint32_t length, uint8_t level, const std::string &sEventName = "Expression");
  void addExpression(u32 offset, u32 length, double levelPercent, Resolution res, const std::string &sEventName = "Expression");
  void addExpressionNoItem(uint8_t level);
  void addExpressionSlide(uint32_t offset, uint32_t length, uint32_t dur, uint8_t targExpr, const std::string &sEventName = "Expression Slide");
  void insertExpression(uint32_t offset, uint32_t length, uint8_t level, uint32_t absTime, const std::string &sEventName = "Expression");
  void insertExpressionNoItem(uint8_t level, uint32_t absTime);
  void addMasterVol(uint32_t offset, uint32_t length, uint8_t vol, const std::string &sEventName = "Master Volume");
  void addMasterVolNoItem(uint8_t newVol);
  void addMastVolSlide(uint32_t offset, uint32_t length, uint32_t dur, uint8_t targVol, const std::string &sEventName = "Master Volume Slide");

  void addPan(uint32_t offset, uint32_t length, uint8_t pan, const std::string &sEventName = "Pan");
  void addPanNoItem(uint8_t pan);
  void addPanSlide(uint32_t offset, uint32_t length, uint32_t dur, uint8_t targPan, const std::string &sEventName = "Pan Slide");
  void insertPan(uint32_t offset, uint32_t length, uint8_t pan, uint32_t absTime, const std::string &sEventName = "Pan");
  void addReverb(uint32_t offset, uint32_t length, uint8_t reverb, const std::string &sEventName = "Reverb");
  void addReverbNoItem(uint8_t reverb);
  void addMonoNoItem() const;
  void insertReverb(uint32_t offset, uint32_t length, uint8_t reverb, uint32_t absTime, const std::string &sEventName = "Reverb");
  void addPitchBend(uint32_t offset, uint32_t length, int16_t bend, const std::string &sEventName = "Pitch Bend");
  void addPitchBendAsPercent(uint32_t offset, uint32_t length, double percent, const std::string &sEventName = "Pitch Bend");
  void addPitchBendRange(uint32_t offset, uint32_t length, uint16_t cents, const std::string &sEventName = "Pitch Bend Range");
  void addPitchBendRangeNoItem(uint16_t cents) const;
  void addFineTuning(uint32_t offset, uint32_t length, double cents, const std::string &sEventName = "Fine Tuning");
  void addFineTuningNoItem(double cents);
  void addCoarseTuning(uint32_t offset, uint32_t length, double semitones, const std::string &sEventName = "Coarse Tuning");
  void addCoarseTuningNoItem(double semitones);
  void addModulationDepthRange(uint32_t offset, uint32_t length, double semitones, const std::string &sEventName = "Modulation Depth Range");
  void addModulationDepthRangeNoItem(double semitones) const;
  void addTranspose(uint32_t offset, uint32_t length, int8_t transpose, const std::string &sEventName = "Transpose");
  void addPitchBendMidiFormat(uint32_t offset, uint32_t length, uint8_t lo, uint8_t hi, const std::string &sEventName = "Pitch Bend");
  void addModulation(uint32_t offset, uint32_t length, uint8_t depth, const std::string &sEventName = "Modulation Depth");
  void addModulationNoItem(uint8_t depth);
  void insertModulation(uint32_t offset, uint32_t length, uint8_t depth, uint32_t absTime, const std::string &sEventName = "Modulation Depth");
  void addBreath(uint32_t offset, uint32_t length, uint8_t depth, const std::string &sEventName = "Breath Depth");
  void addBreathNoItem(uint8_t depth);
  void insertBreath(uint32_t offset, uint32_t length, uint8_t depth, uint32_t absTime, const std::string &sEventName = "Breath Depth");

  void addSustainEvent(uint32_t offset, uint32_t length, uint8_t depth, const std::string &sEventName = "Sustain");
  void insertSustainEvent(uint32_t offset, uint32_t length, uint8_t depth, uint32_t absTime, const std::string &sEventName = "Sustain");
  void addPortamento(uint32_t offset, uint32_t length, bool bOn, const std::string &sEventName = "Portamento");
  void addPortamentoNoItem(bool bOn) const;
  void insertPortamento(uint32_t offset, uint32_t length, bool bOn, uint32_t absTime, const std::string &sEventName = "Portamento");
  void insertPortamentoNoItem(bool bOn, uint32_t absTime) const;
  void addPortamentoTime(uint32_t offset, uint32_t length, uint8_t time, const std::string &sEventName = "Portamento Time");
  void addPortamentoTimeNoItem(uint8_t time) const;
  void insertPortamentoTime(uint32_t offset, uint32_t length, uint8_t time, uint32_t absTime, const std::string &sEventName = "Portamento Time");
  void insertPortamentoTimeNoItem(uint8_t time, uint32_t absTime) const;
  void addPortamentoTime14Bit(uint32_t offset, uint32_t length, uint16_t time, const std::string &sEventName = "Portamento Time");
  void addPortamentoTime14BitNoItem(uint16_t time) const;
  void insertPortamentoTime14Bit(uint32_t offset, uint32_t length, uint16_t time, uint32_t absTime, const std::string &sEventName);
  void insertPortamentoTime14BitNoItem(uint16_t time, uint32_t absTime) const;
  void addPortamentoControlNoItem(uint8_t key) const;
  void insertPortamentoControlNoItem(uint8_t key, uint32_t absTime) const;
  void addProgramChange(uint32_t offset, uint32_t length, uint32_t progNum, const std::string &sEventName = "Program Change");
  void addProgramChange(uint32_t offset, uint32_t length, uint32_t progNum, uint8_t chan, const std::string &sEventName = "Program Change");
  void addProgramChange(uint32_t offset, uint32_t length, uint32_t progNum, bool requireBank, const std::string &sEventName = "Program Change");
  void addProgramChange(uint32_t offset, uint32_t length, uint32_t progNum, bool requireBank, uint8_t chan, const std::string &sEventName = "Program Change");
  void addProgramChangeNoItem(uint32_t progNum, bool requireBank) const;
  void addBankSelect(uint32_t offset, uint32_t length, uint8_t bank, const std::string& sEventName = "Bank Select");
  void addBankSelectNoItem(uint8_t bank) const;
  void addTempo(uint32_t offset, uint32_t length, uint32_t microsPerQuarter, const std::string &sEventName = "Tempo");
  void addTempoNoItem(uint32_t microsPerQuarter) const;
  void insertTempo(uint32_t offset, uint32_t length, uint32_t microsPerQuarter, uint32_t absTime, const std::string &sEventName = "Tempo");
  void insertTempoNoItem(uint32_t microsPerQuarter, uint32_t absTime) const;
  void addTempoSlide(uint32_t offset, uint32_t length, uint32_t dur, uint32_t targMicrosPerQuarter, const std::string &sEventName = "Tempo Slide");
  void addTempoBPM(uint32_t offset, uint32_t length, double bpm, const std::string &sEventName = "Tempo");
  void addTempoBPMNoItem(double bpm) const;
  void addTempoBPMSlide(uint32_t offset, uint32_t length, uint32_t dur, double targBPM, const std::string &sEventName = "Tempo Slide");
  void addTimeSig(uint32_t offset, uint32_t length, uint8_t numer, uint8_t denom, uint8_t ticksPerQuarter, const std::string &sEventName = "Time Signature");
  void addTimeSigNoItem(uint8_t numer, uint8_t denom, uint8_t ticksPerQuarter) const;
  void insertTimeSig(uint32_t offset, uint32_t length, uint8_t numer, uint8_t denom, uint8_t ticksPerQuarter, uint32_t absTime, const std::string &sEventName = "Time Signature");
  void addEndOfTrack(uint32_t offset, uint32_t length, const std::string &sEventName = "Track End");
  void addEndOfTrackNoItem();
  void addControllerEventNoItem(uint8_t controllerType, uint8_t controllerValue) const;

  void addGlobalTranspose(uint32_t offset, uint32_t length, int8_t semitones, const std::string &sEventName = "Global Transpose");
  void addMarker(uint32_t offset, uint32_t length, const std::string &markername, uint8_t databyte1, uint8_t databyte2, const std::string &sEventName, int8_t priority = 0, Type type = Type::Misc);
  void addMarkerNoItem(const std::string &markername, uint8_t databyte1, uint8_t databyte2, int8_t priority) const;
  void insertMarkerNoItem(uint32_t absTime, const std::string &markername, uint8_t databyte1, uint8_t databyte2, int8_t priority) const;

  bool addLoopForever(uint32_t offset, uint32_t length, const std::string &sEventName = "Loop Forever");
  bool addJump(u32 offset, u32 length, u32 destination, const std::string &sEventName = "Jump");
  bool addCall(u32 offset, u32 length, u32 destination, u32 returnOffset, const std::string &sEventName = "Call");
  bool addReturn(u32 offset, u32 length, const std::string &sEventName = "Return");

 public:
  uint32_t dwStartOffset;
  ReadMode readMode;        //state variable that determines behavior for all methods.  Are we adding UI items or converting to MIDI?

  VGMSeq *parentSeq;
  MidiTrack *pMidiTrack;

  int channel;
  int channelGroup;
  bool active;            //indicates whether a VGMSeq is loading this track
  long totalTicks;
  int infiniteLoops;

  SynthType synthType = SynthType::SoundFont;

 protected:
  bool bMonophonic;
  long deltaTime;            //delta time, an interval to the next event (ticks)
  uint8_t prevKey;
  uint8_t prevVel;
  uint8_t octave;
  u16 vol;
  u16 expression;
  u16 mastVol;
  double panVolumeCorrectionRate; // as percentage of original volume (default: 1.0)
  u16 prevPan;
  uint8_t prevReverb;
  int8_t transpose;
  double fineTuningCents;
  double coarseTuningSemitones;
  uint32_t curOffset;
  bool bInLoop;
  int8_t cDrumNote;            //-1 signals do not use drumNote, otherwise,

  //Table Related Variables
  int8_t cKeyCorrection;    //steps to offset the key by

  //SETTINGS
  bool bDetermineTrackLengthEventByEvent;
  bool bWriteGenericEventAsTextEvent;
  bool m_useLinearAmpScale = false;

  std::unordered_set<uint32_t> visitedAddresses;
  uint32_t visitedAddressMax;

  std::vector<uint32_t> returnOffsets;
  std::vector<LoopState> loopStack;
  std::unordered_set<ControlFlowState, ControlFlowStateHasher> visitedControlFlowStates;
};

template<typename... Args>
static std::string logEvent(uint8_t statusByte, spdlog::level::level_enum level = spdlog::level::err,
  std::string title = "Event", Args... args) {

  std::ostringstream description;
  description <<  title << ": 0x" << std::hex << std::setfill('0') << std::setw(2)
              << std::uppercase << static_cast<unsigned>(statusByte) << std::dec << std::setfill(' ') << std::setw(0);

  // Use a fold expression to process each argument
  int arg_idx = 1;
  ((description << "  Arg" << arg_idx++ << ": " << args), ...);

  auto str = description.str();
  L_LOG(level, str);
  return str;
}

template<typename... Args>
static std::string describeUnknownEvent(uint8_t statusByte, Args... args) {
  return logEvent(statusByte, spdlog::level::off, "Unknown Event", args...);
}

template<typename... Args>
static std::string describeUnknownSubevent(uint8_t statusByte, Args... args) {
  return logEvent(statusByte, spdlog::level::off, "Unknown Subevent", args...);
}
