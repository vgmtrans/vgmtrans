/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include <unordered_set>
#include <sstream>
#include "VGMItem.h"
#include "VGMSeq.h"
#include <spdlog/common.h>
#include "LogManager.h"

class VGMSeq;
class SeqEvent;
class MidiTrack;

enum ReadMode : uint8_t;

class SeqTrack : public VGMContainerItem {
 public:
  SeqTrack(VGMSeq *parentSeqFile, uint32_t offset = 0, uint32_t length = 0, std::string name = "Track");
  ~SeqTrack() override;
  virtual void ResetVars();
  void ResetVisitedAddresses();

  Icon GetIcon() override { return ICON_TRACK; };

  virtual bool LoadTrackInit(int trackNum, MidiTrack *preparedMidiTrack);
  virtual void LoadTrackMainLoop(uint32_t stopOffset, int32_t stopTime);
  virtual void SetChannelAndGroupFromTrkNum(int theTrackNum);
  virtual void AddInitialMidiEvents(int trackNum);
  virtual bool ReadEvent();
  virtual void OnTickBegin() { };
  virtual void OnTickEnd() { };

  uint32_t GetTime() const;
  void SetTime(uint32_t NewDelta) const;
  void AddTime(uint32_t AddDelta);

  uint32_t ReadVarLen(uint32_t &offset) const;

 public:
  virtual bool IsOffsetUsed(uint32_t offset);

  uint32_t dwStartOffset;

  std::unordered_set<uint32_t> VisitedAddresses;
  uint32_t VisitedAddressMax;

 protected:
  virtual void OnEvent(uint32_t offset, uint32_t length);
  virtual void AddEvent(SeqEvent *pSeqEvent);
  void AddControllerSlide(uint32_t offset, uint32_t length, uint32_t dur, uint8_t &prevVal, uint8_t targVal, uint8_t (*scalerFunc)(uint8_t), void (MidiTrack::*insertFunc)(uint8_t, uint8_t, uint32_t)) const;
 public:
  void AddGenericEvent(uint32_t offset, uint32_t length, const std::string &sEventName, const std::string &sEventDesc, EventColor color, Icon icon = ICON_BINARY);
  void AddSetOctave(uint32_t offset, uint32_t length, uint8_t newOctave, const std::string &sEventName = "Set Octave");
  void AddIncrementOctave(uint32_t offset, uint32_t length, const std::string &sEventName = "Increment Octave");    // 1,Sep.2009 revise
  void AddDecrementOctave(uint32_t offset, uint32_t length, const std::string &sEventName = "Decrement Octave");    // 1,Sep.2009 revise
  void AddRest(uint32_t offset, uint32_t length, uint32_t restTime, const std::string &sEventName = "Rest");
  void AddHold(uint32_t offset, uint32_t length, const std::string &sEventName = "Hold");
  void AddUnknown(uint32_t offset, uint32_t length, const std::string &sEventName = "Unknown Event", const std::string &sEventDesc = "");

  void AddNoteOn(uint32_t offset, uint32_t length, int8_t key, int8_t vel, const std::string &sEventName = "Note On");
  void AddNoteOnNoItem(int8_t key, int8_t vel);
  void AddPercNoteOn(uint32_t offset, uint32_t length, int8_t key, int8_t vel, const std::string &sEventName = "Percussion Note On");
  void AddPercNoteOnNoItem(int8_t key, int8_t vel);
  void InsertNoteOn(uint32_t offset, uint32_t length, int8_t key, int8_t vel, uint32_t absTime, const std::string &sEventName = "Note On");

  void AddNoteOff(uint32_t offset, uint32_t length, int8_t key, const std::string &sEventName = "Note Off");
  void AddNoteOffNoItem(int8_t key);
  void AddPercNoteOff(uint32_t offset, uint32_t length, int8_t key, const std::string &sEventName = "Percussion Note Off");
  void AddPercNoteOffNoItem(int8_t key);
  void InsertNoteOff(uint32_t offset, uint32_t length, int8_t key, uint32_t absTime, const std::string &sEventName = "Note Off");
  void InsertNoteOffNoItem(int8_t key, uint32_t absTime) const;

  void AddNoteByDur(uint32_t offset, uint32_t length, int8_t key, int8_t vel, uint32_t dur, const std::string &sEventName = "Note with Duration");
  void AddNoteByDurNoItem(int8_t key, int8_t vel, uint32_t dur);
  void AddNoteByDur_Extend(uint32_t offset, uint32_t length, int8_t key, int8_t vel, uint32_t dur, const std::string &sEventName = "Note with Duration (Extended)");
  void AddNoteByDurNoItem_Extend(int8_t key, int8_t vel, uint32_t dur);
  void AddPercNoteByDur(uint32_t offset, uint32_t length, int8_t key, int8_t vel, uint32_t dur, const std::string &sEventName = "Percussion Note with Duration");
  void AddPercNoteByDurNoItem(int8_t key, int8_t vel, uint32_t dur);
  void InsertNoteByDur(uint32_t offset, uint32_t length, int8_t key, int8_t vel, uint32_t dur, uint32_t absTime, const std::string &sEventName = "Note On With Duration");

  void MakePrevDurNoteEnd() const;
  void MakePrevDurNoteEnd(uint32_t absTime) const;
  void LimitPrevDurNoteEnd() const;
  void LimitPrevDurNoteEnd(uint32_t absTime) const;
  void AddVol(uint32_t offset, uint32_t length, uint8_t vol, const std::string &sEventName = "Volume");
  void AddVolNoItem(uint8_t vol);
  void AddVolSlide(uint32_t offset, uint32_t length, uint32_t dur, uint8_t targVol, const std::string &sEventName = "Volume Slide");
  void InsertVol(uint32_t offset, uint32_t length, uint8_t vol, uint32_t absTime, const std::string &sEventName = "Volume");
  void AddExpression(uint32_t offset, uint32_t length, uint8_t level, const std::string &sEventName = "Expression");
  void AddExpressionNoItem(uint8_t level);
  void AddExpressionSlide(uint32_t offset, uint32_t length, uint32_t dur, uint8_t targExpr, const std::string &sEventName = "Expression Slide");
  void InsertExpression(uint32_t offset, uint32_t length, uint8_t level, uint32_t absTime, const std::string &sEventName = "Expression");
  void AddMasterVol(uint32_t offset, uint32_t length, uint8_t vol, const std::string &sEventName = "Master Volume");
  void AddMasterVolNoItem(uint8_t newVol);
  void AddMastVolSlide(uint32_t offset, uint32_t length, uint32_t dur, uint8_t targVol, const std::string &sEventName = "Master Volume Slide");

  void AddPan(uint32_t offset, uint32_t length, uint8_t pan, const std::string &sEventName = "Pan");
  void AddPanNoItem(uint8_t pan);
  void AddPanSlide(uint32_t offset, uint32_t length, uint32_t dur, uint8_t targPan, const std::string &sEventName = "Pan Slide");
  void InsertPan(uint32_t offset, uint32_t length, uint8_t pan, uint32_t absTime, const std::string &sEventName = "Pan");
  void AddReverb(uint32_t offset, uint32_t length, uint8_t reverb, const std::string &sEventName = "Reverb");
  void AddReverbNoItem(uint8_t reverb);
  void AddMonoNoItem() const;
  void InsertReverb(uint32_t offset, uint32_t length, uint8_t reverb, uint32_t absTime, const std::string &sEventName = "Reverb");
  void AddPitchBend(uint32_t offset, uint32_t length, int16_t bend, const std::string &sEventName = "Pitch Bend");
  void AddPitchBendRange(uint32_t offset, uint32_t length, uint8_t semitones, uint8_t cents = 0, const std::string &sEventName = "Pitch Bend Range");
  void AddPitchBendRangeNoItem(uint8_t range, uint8_t cents = 0) const;
  void AddFineTuning(uint32_t offset, uint32_t length, double cents, const std::string &sEventName = "Fine Tuning");
  void AddFineTuningNoItem(double cents) const;
  void AddModulationDepthRange(uint32_t offset, uint32_t length, double semitones, const std::string &sEventName = "Modulation Depth Range");
  void AddModulationDepthRangeNoItem(double semitones) const;
  void AddTranspose(uint32_t offset, uint32_t length, int8_t transpose, const std::string &sEventName = "Transpose");
  void AddPitchBendMidiFormat(uint32_t offset, uint32_t length, uint8_t lo, uint8_t hi, const std::string &sEventName = "Pitch Bend");
  void AddModulation(uint32_t offset, uint32_t length, uint8_t depth, const std::string &sEventName = "Modulation Depth");
  void InsertModulation(uint32_t offset, uint32_t length, uint8_t depth, uint32_t absTime, const std::string &sEventName = "Modulation Depth");
  void AddBreath(uint32_t offset, uint32_t length, uint8_t depth, const std::string &sEventName = "Breath Depth");
  void InsertBreath(uint32_t offset, uint32_t length, uint8_t depth, uint32_t absTime, const std::string &sEventName = "Breath Depth");

  void AddSustainEvent(uint32_t offset, uint32_t length, uint8_t depth, const std::string &sEventName = "Sustain");
  void InsertSustainEvent(uint32_t offset, uint32_t length, uint8_t depth, uint32_t absTime, const std::string &sEventName = "Sustain");
  void AddPortamento(uint32_t offset, uint32_t length, bool bOn, const std::string &sEventName = "Portamento");
  void AddPortamentoNoItem(bool bOn) const;
  void InsertPortamento(uint32_t offset, uint32_t length, bool bOn, uint32_t absTime, const std::string &sEventName = "Portamento");
  void InsertPortamentoNoItem(bool bOn, uint32_t absTime) const;
  void AddPortamentoTime(uint32_t offset, uint32_t length, uint8_t time, const std::string &sEventName = "Portamento Time");
  void AddPortamentoTimeNoItem(uint8_t time) const;
  void InsertPortamentoTime(uint32_t offset, uint32_t length, uint8_t time, uint32_t absTime, const std::string &sEventName = "Portamento Time");
  void InsertPortamentoTimeNoItem(uint8_t time, uint32_t absTime) const;
  void AddPortamentoTime14Bit(uint32_t offset, uint32_t length, uint16_t time, const std::string &sEventName = "Portamento Time");
  void AddPortamentoTime14BitNoItem(uint16_t time) const;
  void AddPortamentoControlNoItem(uint8_t key) const;
  void AddProgramChange(uint32_t offset, uint32_t length, uint32_t progNum, const std::string &sEventName = "Program Change");
  void AddProgramChange(uint32_t offset, uint32_t length, uint32_t progNum, uint8_t chan, const std::string &sEventName = "Program Change");
  void AddProgramChange(uint32_t offset, uint32_t length, uint32_t progNum, bool requireBank, const std::string &sEventName = "Program Change");
  void AddProgramChange(uint32_t offset, uint32_t length, uint32_t progNum, bool requireBank, uint8_t chan, const std::string &sEventName = "Program Change");
  void AddProgramChangeNoItem(uint32_t progNum, bool requireBank) const;
  void AddBankSelectNoItem(uint8_t bank) const;
  void AddTempo(uint32_t offset, uint32_t length, uint32_t microsPerQuarter, const std::string &sEventName = "Tempo");
  void AddTempoNoItem(uint32_t microsPerQuarter) const;
  void AddTempoSlide(uint32_t offset, uint32_t length, uint32_t dur, uint32_t targMicrosPerQuarter, const std::string &sEventName = "Tempo Slide");
  void AddTempoBPM(uint32_t offset, uint32_t length, double bpm, const std::string &sEventName = "Tempo");
  void AddTempoBPMNoItem(double bpm) const;
  void AddTempoBPMSlide(uint32_t offset, uint32_t length, uint32_t dur, double targBPM, const std::string &sEventName = "Tempo Slide");
  void AddTimeSig(uint32_t offset, uint32_t length, uint8_t numer, uint8_t denom, uint8_t ticksPerQuarter, const std::string &sEventName = "Time Signature");
  void AddTimeSigNoItem(uint8_t numer, uint8_t denom, uint8_t ticksPerQuarter) const;
  void InsertTimeSig(uint32_t offset, uint32_t length, uint8_t numer, uint8_t denom, uint8_t ticksPerQuarter, uint32_t absTime, const std::string &sEventName = "Time Signature");
  void AddEndOfTrack(uint32_t offset, uint32_t length, const std::string &sEventName = "Track End");
  void AddEndOfTrackNoItem();

  void AddGlobalTranspose(uint32_t offset, uint32_t length, int8_t semitones, const std::string &sEventName = "Global Transpose");
  void AddMarker(uint32_t offset, uint32_t length, const std::string &markername, uint8_t databyte1, uint8_t databyte2, const std::string &sEventName, int8_t priority = 0, EventColor color = CLR_MISC);
  void AddMarkerNoItem(const std::string &markername, uint8_t databyte1, uint8_t databyte2, int8_t priority) const;
  void InsertMarkerNoItem(uint32_t absTime, const std::string &markername, uint8_t databyte1, uint8_t databyte2, int8_t priority) const;

  bool AddLoopForever(uint32_t offset, uint32_t length, const std::string &sEventName = "Loop Forever");

 public:
  ReadMode readMode;        //state variable that determines behavior for all methods.  Are we adding UI items or converting to MIDI?

  VGMSeq *parentSeq;
  MidiTrack *pMidiTrack;
  bool bMonophonic;
  int channel;
  int channelGroup;

  long deltaLength;
  int foreverLoops;
  bool active;            //indicates whether a VGMSeq is loading this track

  long deltaTime;            //delta time, an interval to the next event (ticks)
  int8_t vel;
  int8_t key;
  uint32_t dur;
  uint8_t prevKey;
  uint8_t prevVel;
  uint8_t octave;
  uint8_t vol;
  uint8_t expression;
  uint8_t mastVol;
  double panVolumeCorrectionRate; // as percentage of original volume (default: 1.0)
  uint8_t prevPan;
  uint8_t prevReverb;
  int8_t transpose;
  uint32_t curOffset;
  bool bInLoop;
  int8_t cDrumNote;            //-1 signals do not use drumNote, otherwise,

  //Table Related Variables
  int8_t cKeyCorrection;    //steps to offset the key by

  std::vector<SeqEvent *> aEvents;

 protected:
  //SETTINGS
  bool bDetermineTrackLengthEventByEvent;
  bool bWriteGenericEventAsTextEvent;
};

template<typename... Args>
static std::string logEvent(uint8_t statusByte, spdlog::level::level_enum level = spdlog::level::err,
  std::string title = "Event", Args... args) {

  std::ostringstream description;
  description <<  title << ": 0x" << std::hex << std::setfill('0') << std::setw(2)
              << std::uppercase << statusByte << std::dec << std::setfill(' ') << std::setw(0);

  // Use a fold expression to process each argument
  int arg_idx = 1;
  ((description << "  Arg" << arg_idx++ << ": " << args), ...);

  auto str = description.str();
  L_LOG(level, str);
  return str;
}

template<typename... Args>
static std::string describeUnknownEvent(uint8_t statusByte, Args... args) {
  return logEvent(statusByte, spdlog::level::off, "Event", args...);
}
