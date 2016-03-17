#pragma once
#include "VGMItem.h"
#include "VGMSeq.h"

class VGMSeq;
class SeqEvent;
class MidiTrack;

enum ReadMode: uint8_t;

class SeqTrack:
    public VGMContainerItem {
 public:
  SeqTrack(VGMSeq *parentSeqFile, uint32_t offset = 0, uint32_t length = 0, std::wstring name = L"Track");
  virtual ~SeqTrack(void);
  virtual void ResetVars();

  virtual Icon GetIcon() { return ICON_TRACK; };

  virtual bool LoadTrackInit(int trackNum, MidiTrack *preparedMidiTrack);
  virtual void LoadTrackMainLoop(uint32_t stopOffset, int32_t stopTime);
  virtual void SetChannelAndGroupFromTrkNum(int theTrackNum);
  virtual void AddInitialMidiEvents(int trackNum);
  virtual bool ReadEvent(void);
  virtual void OnTickBegin() { };
  virtual void OnTickEnd() { };

  uint32_t GetTime(void);
  void SetTime(uint32_t NewDelta);
  void AddTime(uint32_t AddDelta);

  uint32_t ReadVarLen(uint32_t &offset);

 public:
  virtual bool IsOffsetUsed(uint32_t offset);

  uint32_t dwStartOffset;

  std::unordered_set<uint32_t> VisitedAddresses;
  uint32_t VisitedAddressMax;

 protected:
  virtual void OnEvent(uint32_t offset, uint32_t length);
  virtual void AddEvent(SeqEvent *pSeqEvent);
  void AddControllerSlide(uint32_t offset, uint32_t length, uint32_t dur, uint8_t &prevVal, uint8_t targVal, uint8_t (*scalerFunc)(uint8_t), void (MidiTrack::*insertFunc)(uint8_t, uint8_t, uint32_t));
 public:
  void AddGenericEvent(uint32_t offset, uint32_t length, const std::wstring &sEventName, const std::wstring &sEventDesc, uint8_t color, Icon icon = ICON_BINARY);
  void AddSetOctave(uint32_t offset, uint32_t length, uint8_t newOctave, const std::wstring &sEventName = L"Set Octave");
  void AddIncrementOctave(uint32_t offset, uint32_t length, const std::wstring &sEventName = L"Increment Octave");    // 1,Sep.2009 revise
  void AddDecrementOctave(uint32_t offset, uint32_t length, const std::wstring &sEventName = L"Decrement Octave");    // 1,Sep.2009 revise
  void AddRest(uint32_t offset, uint32_t length, uint32_t restTime, const std::wstring &sEventName = L"Rest");
  void AddHold(uint32_t offset, uint32_t length, const std::wstring &sEventName = L"Hold");
  void AddUnknown(uint32_t offset, uint32_t length, const std::wstring &sEventName = L"Unknown Event", const std::wstring &sEventDesc = L"");

  void AddNoteOn(uint32_t offset, uint32_t length, int8_t key, int8_t vel, const std::wstring &sEventName = L"Note On");
  void AddNoteOnNoItem(int8_t key, int8_t vel);
  void AddPercNoteOn(uint32_t offset, uint32_t length, int8_t key, int8_t vel, const std::wstring &sEventName = L"Percussion Note On");
  void AddPercNoteOnNoItem(int8_t key, int8_t vel);
  void InsertNoteOn(uint32_t offset, uint32_t length, int8_t key, int8_t vel, uint32_t absTime, const std::wstring &sEventName = L"Note On");

  void AddNoteOff(uint32_t offset, uint32_t length, int8_t key, const std::wstring &sEventName = L"Note Off");
  void AddNoteOffNoItem(int8_t key);
  void AddPercNoteOff(uint32_t offset, uint32_t length, int8_t key, const std::wstring &sEventName = L"Percussion Note Off");
  void AddPercNoteOffNoItem(int8_t key);
  void InsertNoteOff(uint32_t offset, uint32_t length, int8_t key, uint32_t absTime, const std::wstring &sEventName = L"Note Off");
  void InsertNoteOffNoItem(int8_t key, uint32_t absTime);

  void AddNoteByDur(uint32_t offset, uint32_t length, int8_t key, int8_t vel, uint32_t dur, const std::wstring &sEventName = L"Note with Duration");
  void AddNoteByDurNoItem(int8_t key, int8_t vel, uint32_t dur);
  void AddNoteByDur_Extend(uint32_t offset, uint32_t length, int8_t key, int8_t vel, uint32_t dur, const std::wstring &sEventName = L"Note with Duration (Extended)");
  void AddNoteByDurNoItem_Extend(int8_t key, int8_t vel, uint32_t dur);
  void AddPercNoteByDur(uint32_t offset, uint32_t length, int8_t key, int8_t vel, uint32_t dur, const std::wstring &sEventName = L"Percussion Note with Duration");
  void AddPercNoteByDurNoItem(int8_t key, int8_t vel, uint32_t dur);
  void InsertNoteByDur(uint32_t offset, uint32_t length, int8_t key, int8_t vel, uint32_t dur, uint32_t absTime, const std::wstring &sEventName = L"Note On With Duration");

  void MakePrevDurNoteEnd();
  void MakePrevDurNoteEnd(uint32_t absTime);
  void LimitPrevDurNoteEnd();
  void LimitPrevDurNoteEnd(uint32_t absTime);
  void AddVol(uint32_t offset, uint32_t length, uint8_t vol, const std::wstring &sEventName = L"Volume");
  void AddVolNoItem(uint8_t vol);
  void AddVolSlide(uint32_t offset, uint32_t length, uint32_t dur, uint8_t targVol, const std::wstring &sEventName = L"Volume Slide");
  void InsertVol(uint32_t offset, uint32_t length, uint8_t vol, uint32_t absTime, const std::wstring &sEventName = L"Volume");
  void AddExpression(uint32_t offset, uint32_t length, uint8_t level, const std::wstring &sEventName = L"Expression");
  void AddExpressionNoItem(uint8_t level);
  void AddExpressionSlide(uint32_t offset, uint32_t length, uint32_t dur, uint8_t targExpr, const std::wstring &sEventName = L"Expression Slide");
  void InsertExpression(uint32_t offset, uint32_t length, uint8_t level, uint32_t absTime, const std::wstring &sEventName = L"Expression");
  void AddMasterVol(uint32_t offset, uint32_t length, uint8_t vol, const std::wstring &sEventName = L"Master Volume");
  void AddMasterVolNoItem(uint8_t newVol);
  void AddMastVolSlide(uint32_t offset, uint32_t length, uint32_t dur, uint8_t targVol, const std::wstring &sEventName = L"Master Volume Slide");

  void AddPan(uint32_t offset, uint32_t length, uint8_t pan, const std::wstring &sEventName = L"Pan");
  void AddPanNoItem(uint8_t pan);
  void AddPanSlide(uint32_t offset, uint32_t length, uint32_t dur, uint8_t targPan, const std::wstring &sEventName = L"Pan Slide");
  void InsertPan(uint32_t offset, uint32_t length, uint8_t pan, uint32_t absTime, const std::wstring &sEventName = L"Pan");
  void AddReverb(uint32_t offset, uint32_t length, uint8_t reverb, const std::wstring &sEventName = L"Reverb");
  void AddReverbNoItem(uint8_t reverb);
  void AddMonoNoItem();
  void InsertReverb(uint32_t offset, uint32_t length, uint8_t reverb, uint32_t absTime, const std::wstring &sEventName = L"Reverb");
  void AddPitchBend(uint32_t offset, uint32_t length, int16_t bend, const std::wstring &sEventName = L"Pitch Bend");
  void AddPitchBendRange(uint32_t offset, uint32_t length, uint8_t semitones, uint8_t cents = 0, const std::wstring &sEventName = L"Pitch Bend Range");
  void AddPitchBendRangeNoItem(uint8_t range, uint8_t cents = 0);
  void AddFineTuning(uint32_t offset, uint32_t length, double cents, const std::wstring &sEventName = L"Fine Tuning");
  void AddFineTuningNoItem(double cents);
  void AddModulationDepthRange(uint32_t offset, uint32_t length, double semitones, const std::wstring &sEventName = L"Modulation Depth Range");
  void AddModulationDepthRangeNoItem(double semitones);
  void AddTranspose(uint32_t offset, uint32_t length, int8_t transpose, const std::wstring &sEventName = L"Transpose");
  void AddPitchBendMidiFormat(uint32_t offset, uint32_t length, uint8_t lo, uint8_t hi, const std::wstring &sEventName = L"Pitch Bend");
  void AddModulation(uint32_t offset, uint32_t length, uint8_t depth, const std::wstring &sEventName = L"Modulation Depth");
  void InsertModulation(uint32_t offset, uint32_t length, uint8_t depth, uint32_t absTime, const std::wstring &sEventName = L"Modulation Depth");
  void AddBreath(uint32_t offset, uint32_t length, uint8_t depth, const std::wstring &sEventName = L"Breath Depth");
  void InsertBreath(uint32_t offset, uint32_t length, uint8_t depth, uint32_t absTime, const std::wstring &sEventName = L"Breath Depth");

  void AddSustainEvent(uint32_t offset, uint32_t length, uint8_t depth, const std::wstring &sEventName = L"Sustain");
  void InsertSustainEvent(uint32_t offset, uint32_t length, uint8_t depth, uint32_t absTime, const std::wstring &sEventName = L"Sustain");
  void AddPortamento(uint32_t offset, uint32_t length, bool bOn, const std::wstring &sEventName = L"Portamento");
  void AddPortamentoNoItem(bool bOn);
  void InsertPortamento(uint32_t offset, uint32_t length, bool bOn, uint32_t absTime, const std::wstring &sEventName = L"Portamento");
  void AddPortamentoTime(uint32_t offset, uint32_t length, uint8_t time, const std::wstring &sEventName = L"Portamento Time");
  void AddPortamentoTimeNoItem(uint8_t time);
  void InsertPortamentoTime(uint32_t offset, uint32_t length, uint8_t time, uint32_t absTime, const std::wstring &sEventName = L"Portamento Time");
  void AddProgramChange(uint32_t offset, uint32_t length, uint32_t progNum, const std::wstring &sEventName = L"Program Change");
  void AddProgramChange(uint32_t offset, uint32_t length, uint32_t progNum, uint8_t chan, const std::wstring &sEventName = L"Program Change");
  void AddProgramChange(uint32_t offset, uint32_t length, uint32_t progNum, bool requireBank, const std::wstring &sEventName = L"Program Change");
  void AddProgramChange(uint32_t offset, uint32_t length, uint32_t progNum, bool requireBank, uint8_t chan, const std::wstring &sEventName = L"Program Change");
  void AddProgramChangeNoItem(uint32_t progNum, bool requireBank);
  void AddBankSelectNoItem(uint8_t bank);
  void AddTempo(uint32_t offset, uint32_t length, uint32_t microsPerQuarter, const std::wstring &sEventName = L"Tempo");
  void AddTempoNoItem(uint32_t microsPerQuarter);
  void AddTempoSlide(uint32_t offset, uint32_t length, uint32_t dur, uint32_t targMicrosPerQuarter, const std::wstring &sEventName = L"Tempo Slide");
  void AddTempoBPM(uint32_t offset, uint32_t length, double bpm, const std::wstring &sEventName = L"Tempo");
  void AddTempoBPMNoItem(double bpm);
  void AddTempoBPMSlide(uint32_t offset, uint32_t length, uint32_t dur, double targBPM, const std::wstring &sEventName = L"Tempo Slide");
  void AddTimeSig(uint32_t offset, uint32_t length, uint8_t numer, uint8_t denom, uint8_t ticksPerQuarter, const std::wstring &sEventName = L"Time Signature");
  void AddTimeSigNoItem(uint8_t numer, uint8_t denom, uint8_t ticksPerQuarter);
  void InsertTimeSig(uint32_t offset, uint32_t length, uint8_t numer, uint8_t denom, uint8_t ticksPerQuarter, uint32_t absTime, const std::wstring &sEventName = L"Time Signature");
  bool AddEndOfTrack(uint32_t offset, uint32_t length, const std::wstring &sEventName = L"Track End");
  bool AddEndOfTrackNoItem();

  void AddGlobalTranspose(uint32_t offset, uint32_t length, int8_t semitones, const std::wstring &sEventName = L"Global Transpose");
  void AddMarker(uint32_t offset, uint32_t length, const std::string &markername, uint8_t databyte1, uint8_t databyte2, const std::wstring &sEventName, int8_t priority = 0, uint8_t color = CLR_MISC);

  bool AddLoopForever(uint32_t offset, uint32_t length, const std::wstring &sEventName = L"Loop Forever");

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
