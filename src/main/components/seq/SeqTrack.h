/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "Types.h"

#include <cstdint>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include "VGMItem.h"
#include "VGMSeq.h"
#include "Modulation.h"
#include <spdlog/common.h>
#include "LogManager.h"
#include "SynthType.h"

class VGMSeq;
class SeqEvent;
class MidiTrack;
class SeqSynthLfoAutomation;
template <typename ValueType>
struct SeqMotionPlan;
template <typename ValueType>
struct SeqMotionTick;
template <typename PitchType>
class SeqPitchBendAutomation;

enum ReadMode : u8;

enum class LevelController : u8 {
  Volume,
  Expression,
  MasterVolume,
};

enum class Resolution : u8 {
  SevenBit,
  FourteenBit,
};

struct LoopState {
  u32 endOffset;
  u32 remainingCount;

  bool operator==(const LoopState &other) const = default;
};

struct ControlFlowState {
  u32 offset;
  std::vector<u32> returnStack;
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
    std::size_t seed = std::hash<u32>{}(s.offset);

    // Mix return stack (length + elements)
    hashCombine(seed, s.returnStack.size());
    for (u32 v : s.returnStack)
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
  SeqTrack(VGMSeq *parentSeqFile, u32 offset = 0, u32 length = 0, std::string name = "Track");

  [[nodiscard]] bool usesLinearAmplitudeScale() const {
    return m_useLinearAmpScale || parentSeq->usesLinearAmplitudeScale();
  }
  void setUseLinearAmplitudeScale(bool set) { m_useLinearAmpScale = set; }

  virtual bool loadTrackInit(int trackNum, MidiTrack *preparedMidiTrack);
  virtual void loadTrackMainLoop(u32 stopOffset, s32 stopTime);

protected:
  virtual void resetVars();
  void resetVisitedAddresses();
  void resetSegmentVars();
  void loadTrackSegmentInit(u32 segmentOffset, u32 segmentLength, bool segmentActive,
                            u32 initialOffset);

  virtual void setChannelAndGroupFromTrkNum(int theTrackNum);
  virtual void addInitialMidiEvents(int trackNum);
  virtual bool readEvent();
  virtual void onTickBegin() {}
  virtual void onTickEnd() {}

  u32 readVarLen(u32 &offset) const;
  u32 getTime() const;
  virtual void setTime(u32 newTime);
  virtual void addTime(u32 delta);

  virtual bool isOffsetUsed(u32 offset);

  virtual bool onEvent(u32 offset, u32 length);
  virtual SeqEvent* addEvent(SeqEvent *pSeqEvent);
  // Some parsers emit SeqEvents under a display track that differs from the parser track.
  void setSeqEventTarget(SeqTrack* target, bool emitEvents = true) {
    m_seqEventTarget = target;
    m_emitSeqEvents = emitEvents;
  }
  void clearSeqEventTarget() { setSeqEventTarget(nullptr); }
  SeqTrack* seqEventTarget() { return m_seqEventTarget != nullptr ? m_seqEventTarget : this; }

  template <typename EventType, typename... Args>
  void recordSeqEvent(bool isNewOffset, u32 startTick, Args&&... args) {
    recordDurSeqEvent<EventType>(isNewOffset, startTick, 0, std::forward<Args>(args)...);
  }

  template <typename EventType, typename... Args>
  SeqEventTimeIndex::Index recordDurSeqEvent(bool isNewOffset,
                                             u32 startTick,
                                             u32 duration,
                                             Args&&... args) {
    if (readMode == READMODE_ADD_TO_UI) {
      if (isNewOffset && m_emitSeqEvents) {
        auto* target = seqEventTarget();
        auto* event = new EventType(target, std::forward<Args>(args)...);
        event->channel = static_cast<u8>(channel);
        target->addEvent(event);
      }
      return SeqEventTimeIndex::kInvalidIndex;
    }
    if (readMode == READMODE_CONVERT_TO_MIDI) {
      if (SeqEvent* existing =
              seqEventTarget()->findSeqEventAtOffset(m_lastEventOffset, m_lastEventLength)) {
        return parentSeq->timedEventIndex().addEvent(existing, startTick, duration);
      }
    }
    return SeqEventTimeIndex::kInvalidIndex;
  }

  bool shouldTrackControlFlowState() const;
  void addControlFlowState(u32 destinationOffset);
  bool checkControlStateForInfiniteLoop(u32 offset);
  void pushReturnOffset(u32 returnOffset);
  bool popReturnOffset(u32 &returnOffset);
  SeqEvent* findSeqEventAtOffset(u32 offset, u32 length);

  template <typename PitchType>
  SeqMotionTick<PitchType> advancePitchBendAutomation(SeqPitchBendAutomation<PitchType>& automation);
  template <typename PitchType>
  bool beginPitchBendAutomation(SeqPitchBendAutomation<PitchType>& automation,
                                const SeqMotionPlan<PitchType>& motion, u16 rangeCents,
                                u16 defaultRangeCents,
                                bool applyInitialBend = false);
  template <typename PitchType>
  bool setPitchBendAutomationRange(SeqPitchBendAutomation<PitchType>& automation, u16 cents);
  template <typename PitchType>
  bool setPitchBendAutomationBend(SeqPitchBendAutomation<PitchType>& automation, s16 bend);
  template <typename PitchType>
  bool applyPitchBendAutomation(SeqPitchBendAutomation<PitchType>& automation);
  template <typename PitchType>
  void resetPitchBendAutomation(SeqPitchBendAutomation<PitchType>& automation, u16 defaultRangeCents);
  bool emitVibratoDepth(SeqSynthLfoAutomation& automation, u8 depth, bool force = false);
  bool emitTremoloDepth(SeqSynthLfoAutomation& automation, u8 depth, bool force = false);
  template <typename ConvertDepth>
  SeqMotionTick<s32> advanceVibratoDepthFade(SeqSynthLfoAutomation& automation,
                                                 u8 fractionalBits,
                                                 ConvertDepth&& convertDepth);
  template <typename ConvertDepth>
  SeqMotionTick<s32> advanceTremoloDepthFade(SeqSynthLfoAutomation& automation,
                                                 u8 fractionalBits,
                                                 ConvertDepth&& convertDepth);

private:
  void addControllerSlide(u32 dur, u16 &prevVal, u16 targVal, u8 (*scalerFunc)(u8), void (MidiTrack::*insertFunc)(u8, u8, u32)) const;
  void addForModSourceNoItem(ModSource source, u8 value) const;
  void addForModDestNoItem(ModDest destination, u8 value) const;
  void addLfoModulationEvent(ModDest destination,
                             u32 offset,
                             u32 length,
                             u8 value,
                             const std::string& eventName,
                             Type type);
  double applyPanVolumeCorrection(double level, LevelController controller) const;
  void addLevelNoItem(double level, LevelController controller, Resolution res, int absTime = -1);
  void reapplyStoredLevelNoItem(LevelController controller, int absTime = -1);
  void purgePrevDurEvents(u32 absTime);
  void clearPrevDurEvents();
  void trackActiveNoteIndex(s8 key, SeqEventTimeIndex::Index idx);
  void endActiveNoteIndex(s8 key, u32 endTick);

 public:
  SeqEvent* addGenericEvent(u32 offset, u32 length, const std::string &sEventName, const std::string &sEventDesc, Type type);
  void addSetOctave(u32 offset, u32 length, u8 newOctave, const std::string &sEventName = "Set Octave");
  void addIncrementOctave(u32 offset, u32 length, const std::string &sEventName = "Increment Octave");    // 1,Sep.2009 revise
  void addDecrementOctave(u32 offset, u32 length, const std::string &sEventName = "Decrement Octave");    // 1,Sep.2009 revise
  void addRest(u32 offset, u32 length, u32 restTime, const std::string &sEventName = "Rest");
  void addTie(u32 offset, u32 length, u32 duration,
              const std::string &sEventName = "Tie", const std::string &sEventDesc = "");
  void addUnknown(u32 offset, u32 length, const std::string &sEventName = "Unknown Event", const std::string &sEventDesc = "");

  void addNoteOn(u32 offset, u32 length, s8 key, s8 vel, const std::string &sEventName = "Note On");
  void addNoteOnNoItem(s8 key, s8 vel);
  void addPercNoteOn(u32 offset, u32 length, s8 key, s8 vel, const std::string &sEventName = "Percussion Note On");
  void addPercNoteOnNoItem(s8 key, s8 vel);
  void insertNoteOn(u32 offset, u32 length, s8 key, s8 vel, u32 absTime, const std::string &sEventName = "Note On");

  void addNoteOff(u32 offset, u32 length, s8 key, const std::string &sEventName = "Note Off");
  void addNoteOffNoItem(s8 key);
  void addPercNoteOff(u32 offset, u32 length, s8 key, const std::string &sEventName = "Percussion Note Off");
  void addPercNoteOffNoItem(s8 key);
  void insertNoteOff(u32 offset, u32 length, s8 key, u32 absTime, const std::string &sEventName = "Note Off");
  void insertNoteOffNoItem(s8 key, u32 absTime) const;

  void addNoteByDur(u32 offset, u32 length, s8 key, s8 vel, u32 dur, const std::string &sEventName = "Note with Duration");
  void addNoteByDurNoItem(s8 key, s8 vel, u32 dur);
  void addNoteByDur_Extend(u32 offset, u32 length, s8 key, s8 vel, u32 dur, const std::string &sEventName = "Note with Duration (Extended)");
  void addNoteByDurNoItem_Extend(s8 key, s8 vel, u32 dur);
  void addPercNoteByDur(u32 offset, u32 length, s8 key, s8 vel, u32 dur, const std::string &sEventName = "Percussion Note with Duration");
  void addPercNoteByDurNoItem(s8 key, s8 vel, u32 dur);
  void insertNoteByDur(u32 offset, u32 length, s8 key, s8 vel, u32 dur, u32 absTime, const std::string &sEventName = "Note On With Duration");
  void insertNoteByDurNoItem(s8 key, s8 vel, u32 dur, u32 absTime);

  void makePrevDurNoteEnd() const;
  void makePrevDurNoteEnd(u32 absTime) const;
  void limitPrevDurNoteEnd() const;
  void limitPrevDurNoteEnd(u32 absTime) const;
  void addVol(u32 offset, u32 length, u8 vol, const std::string &sEventName = "Volume");
  void addVolNoItem(u8 vol);
  void addVol(u32 offset, u32 length, double volPercent, Resolution res, const std::string &sEventName = "Volume");
  void addVolSlide(u32 offset, u32 length, u32 dur, u8 targVol, const std::string &sEventName = "Volume Slide");
  void insertVol(u32 offset, u32 length, u8 vol, u32 absTime, const std::string &sEventName = "Volume");
  void addExpression(u32 offset, u32 length, u8 level, const std::string &sEventName = "Expression");
  void addExpression(u32 offset, u32 length, double levelPercent, Resolution res, const std::string &sEventName = "Expression");
  void addExpressionNoItem(u8 level);
  void addExpressionSlide(u32 offset, u32 length, u32 dur, u8 targExpr, const std::string &sEventName = "Expression Slide");
  void insertExpression(u32 offset, u32 length, u8 level, u32 absTime, const std::string &sEventName = "Expression");
  void insertExpressionNoItem(u8 level, u32 absTime);
  void addMasterVol(u32 offset, u32 length, u8 vol, const std::string &sEventName = "Master Volume");
  void addMasterVol(u32 offset, u32 length, double volPercent, Resolution res, const std::string &sEventName = "Master Volume");
  void addMasterVolNoItem(u8 newVol);
  void addMastVolSlide(u32 offset, u32 length, u32 dur, u8 targVol, const std::string &sEventName = "Master Volume Slide");

  void addPan(u32 offset, u32 length, u8 pan, const std::string &sEventName = "Pan");
  void addPanNoItem(u8 pan);
  void addPanSlide(u32 offset, u32 length, u32 dur, u8 targPan, const std::string &sEventName = "Pan Slide");
  void insertPan(u32 offset, u32 length, u8 pan, u32 absTime, const std::string &sEventName = "Pan");
  void addReverb(u32 offset, u32 length, u8 reverb, const std::string &sEventName = "Reverb");
  void addReverbNoItem(u8 reverb);
  void addMonoNoItem() const;
  void insertReverb(u32 offset, u32 length, u8 reverb, u32 absTime, const std::string &sEventName = "Reverb");
  void addPitchBend(u32 offset, u32 length, s16 bend, const std::string &sEventName = "Pitch Bend");
  void addPitchBendAsPercent(u32 offset, u32 length, double percent, const std::string &sEventName = "Pitch Bend");
  void addPitchBendNoItem(s16 bend) const;
  void addPitchBendRange(u32 offset, u32 length, u16 cents, const std::string &sEventName = "Pitch Bend Range");
  void addPitchBendRangeNoItem(u16 cents) const;
  void addChannelPressure(u32 offset, u32 length, u8 pressure, const std::string &sEventName = "Channel Pressure");
  void addChannelPressureNoItem(u8 pressure);
  void insertChannelPressure(u32 offset, u32 length, u8 pressure, u32 absTime, const std::string &sEventName = "Channel Pressure");
  void addFineTuning(u32 offset, u32 length, double cents, const std::string &sEventName = "Fine Tuning");
  void addFineTuningNoItem(double cents);
  void addCoarseTuning(u32 offset, u32 length, double semitones, const std::string &sEventName = "Coarse Tuning");
  void addCoarseTuningNoItem(double semitones);
  void addModulationDepthRange(u32 offset, u32 length, double semitones, const std::string &sEventName = "Modulation Depth Range");
  void addModulationDepthRangeNoItem(double semitones) const;
  void addTranspose(u32 offset, u32 length, s8 transpose, const std::string &sEventName = "Transpose");
  void addPitchBendMidiFormat(u32 offset, u32 length, u8 lo, u8 hi, const std::string &sEventName = "Pitch Bend");
  void addModulation(u32 offset, u32 length, u8 depth, const std::string &sEventName = "Modulation Depth");
  void addModulationNoItem(u8 depth);
  void insertModulation(u32 offset, u32 length, u8 depth, u32 absTime, const std::string &sEventName = "Modulation Depth");
  void addBreath(u32 offset, u32 length, u8 depth, const std::string &sEventName = "Breath Depth");
  void addBreathNoItem(u8 depth);
  void insertBreath(u32 offset, u32 length, u8 depth, u32 absTime, const std::string &sEventName = "Breath Depth");
  void addVibratoDepth(u32 offset, u32 length, u8 depth, const std::string& sEventName = "Vibrato Depth");
  void addVibratoDepthNoItem(u8 depth) const;
  void addVibratoFrequency(u32 offset, u32 length, u8 frequency, const std::string& sEventName = "Vibrato Frequency");
  void addVibratoFrequencyNoItem(u8 frequency) const;
  void addVibratoDelay(u32 offset, u32 length, u8 delay, const std::string& sEventName = "Vibrato Delay");
  void addVibratoDelayNoItem(u8 delay) const;
  void addTremoloDepth(u32 offset, u32 length, u8 depth, const std::string& sEventName = "Tremolo Depth");
  void addTremoloDepthNoItem(u8 depth) const;
  void addTremoloFrequency(u32 offset, u32 length, u8 frequency, const std::string& sEventName = "Tremolo Frequency");
  void addTremoloFrequencyNoItem(u8 frequency) const;
  void addTremoloDelay(u32 offset, u32 length, u8 delay, const std::string& sEventName = "Tremolo Delay");
  void addTremoloDelayNoItem(u8 delay) const;

  void addSustainEvent(u32 offset, u32 length, u8 depth, const std::string &sEventName = "Sustain");
  void insertSustainEvent(u32 offset, u32 length, u8 depth, u32 absTime, const std::string &sEventName = "Sustain");
  void addPortamento(u32 offset, u32 length, bool bOn, const std::string &sEventName = "Portamento");
  void addPortamentoNoItem(bool bOn) const;
  void insertPortamento(u32 offset, u32 length, bool bOn, u32 absTime, const std::string &sEventName = "Portamento");
  void insertPortamentoNoItem(bool bOn, u32 absTime) const;
  void addPortamentoTime(u32 offset, u32 length, u8 time, const std::string &sEventName = "Portamento Time");
  void addPortamentoTimeNoItem(u8 time) const;
  void insertPortamentoTime(u32 offset, u32 length, u8 time, u32 absTime, const std::string &sEventName = "Portamento Time");
  void insertPortamentoTimeNoItem(u8 time, u32 absTime) const;
  void addPortamentoTime14Bit(u32 offset, u32 length, u16 time, const std::string &sEventName = "Portamento Time");
  void addPortamentoTime14BitNoItem(u16 time) const;
  void insertPortamentoTime14Bit(u32 offset, u32 length, u16 time, u32 absTime, const std::string &sEventName);
  void insertPortamentoTime14BitNoItem(u16 time, u32 absTime) const;
  void addPortamentoControlNoItem(u8 key) const;
  void insertPortamentoControlNoItem(u8 key, u32 absTime) const;
  void addLegatoPedalNoItem(bool bOn);
  void addProgramChange(u32 offset, u32 length, u32 progNum, const std::string &sEventName = "Program Change");
  void addProgramChange(u32 offset, u32 length, u32 progNum, u8 chan, const std::string &sEventName = "Program Change");
  void addProgramChange(u32 offset, u32 length, u32 progNum, bool requireBank, const std::string &sEventName = "Program Change");
  void addProgramChange(u32 offset, u32 length, u32 progNum, bool requireBank, u8 chan, const std::string &sEventName = "Program Change");
  void addProgramChangeNoItem(u32 progNum, bool requireBank) const;
  void addBankSelect(u32 offset, u32 length, u8 bank, const std::string& sEventName = "Bank Select");
  void addBankSelectNoItem(u8 bank) const;
  void addTempo(u32 offset, u32 length, u32 microsPerQuarter, const std::string &sEventName = "Tempo");
  void addTempoNoItem(u32 microsPerQuarter) const;
  void insertTempo(u32 offset, u32 length, u32 microsPerQuarter, u32 absTime, const std::string &sEventName = "Tempo");
  void insertTempoNoItem(u32 microsPerQuarter, u32 absTime) const;
  void addTempoSlide(u32 offset, u32 length, u32 dur, u32 targMicrosPerQuarter, const std::string &sEventName = "Tempo Slide");
  void addTempoBPM(u32 offset, u32 length, double bpm, const std::string &sEventName = "Tempo");
  void addTempoBPMNoItem(double bpm) const;
  void addTempoBPMSlide(u32 offset, u32 length, u32 dur, double targBPM, const std::string &sEventName = "Tempo Slide");
  void addTimeSig(u32 offset, u32 length, u8 numer, u8 denom, u8 ticksPerQuarter, const std::string &sEventName = "Time Signature");
  void addTimeSigNoItem(u8 numer, u8 denom, u8 ticksPerQuarter) const;
  void insertTimeSig(u32 offset, u32 length, u8 numer, u8 denom, u8 ticksPerQuarter, u32 absTime, const std::string &sEventName = "Time Signature");
  void addEndOfTrack(u32 offset, u32 length, const std::string &sEventName = "Track End");
  void addEndOfTrackNoItem();
  void addControllerEventNoItem(u8 controllerType, u8 controllerValue) const;

  void addGlobalTranspose(u32 offset, u32 length, s8 semitones, const std::string &sEventName = "Global Transpose");
  void addMarker(u32 offset, u32 length, const std::string &markername, u8 databyte1, u8 databyte2, const std::string &sEventName, s8 priority = 0, Type type = Type::Misc);
  void addMarkerNoItem(const std::string &markername, u8 databyte1, u8 databyte2, s8 priority) const;
  void insertMarkerNoItem(u32 absTime, const std::string &markername, u8 databyte1, u8 databyte2, s8 priority) const;

  bool addLoopForever(u32 offset, u32 length, const std::string &sEventName = "Loop Forever");
  bool addJump(u32 offset, u32 length, u32 destination, const std::string &sEventName = "Jump");
  bool addCall(u32 offset, u32 length, u32 destination, u32 returnOffset, const std::string &sEventName = "Call");
  bool addReturn(u32 offset, u32 length, const std::string &sEventName = "Return");

 public:
  u32 dwStartOffset;
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
  u8 prevKey;
  u8 prevVel;
  u8 octave;
  u16 vol;
  Resolution volResolution;
  u16 expression;
  Resolution expressionResolution;
  u16 mastVol;
  Resolution masterVolResolution;
  double panVolumeCorrectionRate; // as percentage of original volume (default: 1.0)
  u16 prevPan;
  u8 prevReverb;
  s8 transpose;
  double fineTuningCents;
  double coarseTuningSemitones;
  u32 curOffset;
  bool bInLoop;
  s8 cDrumNote;            //-1 signals do not use drumNote, otherwise,

  //Table Related Variables
  s8 cKeyCorrection;    //steps to offset the key by

  //SETTINGS
  bool bDetermineTrackLengthEventByEvent;
  bool bWriteGenericEventAsTextEvent;
  bool m_useLinearAmpScale = false;

  std::unordered_set<u32> visitedAddresses;
  u32 visitedAddressMax;

  std::vector<u32> returnOffsets;
  std::vector<LoopState> loopStack;
  std::unordered_set<ControlFlowState, ControlFlowStateHasher> visitedControlFlowStates;
  std::vector<SeqEventTimeIndex::Index> prevDurEventIndices;
  std::unordered_map<int, std::vector<SeqEventTimeIndex::Index>> m_activeNoteEventIndices;

  u32 m_lastEventOffset = 0;
  u32 m_lastEventLength = 0;
  SeqTrack* m_seqEventTarget = nullptr;
  bool m_emitSeqEvents = true;
};

template<typename... Args>
static std::string logEvent(u8 statusByte, spdlog::level::level_enum level = spdlog::level::err,
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
static std::string describeUnknownEvent(u8 statusByte, Args... args) {
  return logEvent(statusByte, spdlog::level::off, "Unknown Event", args...);
}

template<typename... Args>
static std::string describeUnknownSubevent(u8 statusByte, Args... args) {
  return logEvent(statusByte, spdlog::level::off, "Unknown Subevent", args...);
}
