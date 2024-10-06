/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include "base/Types.h"

#include <filesystem>
#include <list>
#include <string>
#include <unordered_map>
#include <vector>

class VGMSeq;

class MidiFile;
class MidiTrack;
class MidiEvent;
class DurNoteEvent;
class NoteEvent;

#define PRIORITY_LOWEST 127
#define PRIORITY_LOWER 96
#define PRIORITY_LOW 64
#define PRIORITY_MIDDLE 0
#define PRIORITY_HIGH -64
#define PRIORITY_HIGHER -96
#define PRIORITY_HIGHEST -128

typedef enum {
  MIDIEVENT_UNDEFINED,
  MIDIEVENT_MASTERVOL,
  MIDIEVENT_GLOBALTRANSPOSE,
  MIDIEVENT_BANKSELECT,
  MIDIEVENT_BANKSELECTFINE,
  MIDIEVENT_MARKER,
  MIDIEVENT_NOTEON,
  MIDIEVENT_NOTEOFF,
  MIDIEVENT_DURNOTE,
  MIDIEVENT_REST,
  MIDIEVENT_EXPRESSION,
  MIDIEVENT_EXPRESSIONSLIDE,
  MIDIEVENT_VOLUME,
  MIDIEVENT_VOLUMESLIDE,
  MIDIEVENT_PAN,
  MIDIEVENT_PROGRAMCHANGE,
  MIDIEVENT_PITCHBEND,
  MIDIEVENT_CHANNELPRESSURE,
  MIDIEVENT_TEMPO,
  MIDIEVENT_TIMESIG,
  MIDIEVENT_MODULATION,
  MIDIEVENT_BREATH,
  MIDIEVENT_SUSTAIN,
  MIDIEVENT_PORTAMENTO,
  MIDIEVENT_PORTAMENTOTIME,
  MIDIEVENT_PORTAMENTOTIMEFINE,
  MIDIEVENT_PORTAMENTOCONTROL,
  MIDIEVENT_LEGATOPEDAL,
  MIDIEVENT_MONO,
  MIDIEVENT_LFO,
  MIDIEVENT_VIBRATO,
  MIDIEVENT_ENDOFTRACK,
  MIDIEVENT_TEXT,
  MIDIEVENT_RESET,
  MIDIEVENT_MIDIPORT,
  MIDIEVENT_YMMY_SYNTHASSIGN
} MidiEventType;

class MidiTrack {
 public:
  MidiTrack(MidiFile *parentSeq, bool bMonophonic);
  virtual ~MidiTrack();

  void sort();
  void writeTrack(std::vector<u8> &buf) const;

  //void setChannel(int theChannel);
  void setChannelGroup(int theChannelGroup);

  u32 getDelta() const;
  void setDelta(u32 NewDelta);
  void addDelta(u32 AddDelta);
  void subtractDelta(u32 SubtractDelta);
  void resetDelta();

  void addNoteOn(u8 channel, s8 key, s8 vel);
  void insertNoteOn(u8 channel, s8 key, s8 vel, u32 absTime);
  void addNoteOff(u8 channel, s8 key);
  void insertNoteOff(u8 channel, s8 key, u32 absTime);
  void addNoteByDur(u8 channel, s8 key, s8 vel, u32 duration);
  void addNoteByDur_TriAce(u8 channel, s8 key, s8 vel, u32 duration);
  void insertNoteByDur(u8 channel, s8 key, s8 vel, u32 duration, u32 absTime);
  void purgePrevNoteOffs();
  void purgePrevNoteOffs(u32 absTime);
  void addControllerEvent(u8 channel,
                          u8 controllerNum,
                          u8 theDataByte); // This function should be used for only redirection output of MIDI-like formats
  void insertControllerEvent(u8 channel,
                             u8 controllerNum,
                             u8 theDataByte,
                             u32 absTime); // This function should be used for only redirection output of MIDI-like formats
  void addVol(u8 channel, u8 vol/*, s8 priority = PRIORITY_MIDDLE*/);
  void insertVol(u8 channel, u8 vol, u32 absTime/*, s8 priority = PRIORITY_MIDDLE*/);
  void addVolumeFine(u8 channel, u8 volume_lsb);
  void insertVolumeFine(u8 channel, u8 volume_lsb, u32 absTime);
  void addMasterVol(u8 channel, u8 volMsb, u8 volLsb = 0);
  void insertMasterVol(u8 channel, u8 volMsb, u32 absTime);
  void insertMasterVol(u8 channel, u8 volMsb, u8 volLsb, u32 absTime);
  void addPan(u8 channel, u8 pan);
  void insertPan(u8 channel, u8 pan, u32 absTime);
  void addExpression(u8 channel, u8 expression);
  void addExpressionFine(u8 channel, u8 expression_lsb);
  void insertExpression(u8 channel, u8 expression, u32 absTime);
  void insertExpressionFine(u8 channel, u8 expression_lsb, u32 absTime);
  void addReverb(u8 channel, u8 reverb);
  void insertReverb(u8 channel, u8 reverb, u32 absTime);
  void addModulation(u8 channel, u8 depth);
  void insertModulation(u8 channel, u8 depth, u32 absTime);
  void addBreath(u8 channel, u8 depth);
  void insertBreath(u8 channel, u8 depth, u32 absTime);
  void addSustain(u8 channel, u8 depth);
  void insertSustain(u8 channel, u8 depth, u32 absTime);
  void addPortamento(u8 channel, bool bOn);
  void insertPortamento(u8 channel, bool bOn, u32 absTime);
  void addPortamentoTime(u8 channel, u8 time);
  void insertPortamentoTime(u8 channel, u8 time, u32 absTime);
  void addPortamentoTimeFine(u8 channel, u8 time);
  void insertPortamentoTimeFine(u8 channel, u8 time, u32 absTime);
  void addPortamentoControl(u8 channel, u8 key);
  void insertPortamentoControl(u8 channel, u8 key, u32 absTime);
  void addMono(u8 channel);
  void insertMono(u8 channel, u32 absTime);
  void addLegatoPedal(u8 channel, bool bOn);
  void insertLegatoPedal(u8 channel, bool bOn, u32 absTime);

  void addPitchBend(u8 channel, s16 bend);
  void insertPitchBend(u8 channel, short bend, u32 absTime);
  void addChannelPressure(u8 channel, u8 pressure);
  void insertChannelPressure(u8 channel, u8 pressure, u32 absTime);
  void addPitchBendRange(u8 channel, u16 cents);
  void insertPitchBendRange(u8 channel, u16 cents, u32 absTime);
  void addFineTuning(u8 channel, u8 msb, u8 lsb);
  void insertFineTuning(u8 channel, u8 msb, u8 lsb, u32 absTime);
  void addFineTuning(u8 channel, double cents);
  void insertFineTuning(u8 channel, double cents, u32 absTime);
  void addCoarseTuning(u8 channel, u8 msb, u8 lsb);
  void insertCoarseTuning(u8 channel, u8 msb, u8 lsb, u32 absTime);
  void addCoarseTuning(u8 channel, double semitones);
  void insertCoarseTuning(u8 channel, double semitones, u32 absTime);
  void addModulationDepthRange(u8 channel, u8 msb, u8 lsb);
  void insertModulationDepthRange(u8 channel, u8 msb, u8 lsb, u32 absTime);
  void addModulationDepthRange(u8 channel, double semitones);
  void insertModulationDepthRange(u8 channel, double semitones, u32 absTime);
  void addProgramChange(u8 channel, u8 progNum);
  void addBankSelect(u8 channel, u8 bank);
  void addBankSelectFine(u8 channel, u8 lsb);
  void insertBankSelect(u8 channel, u8 bank, u32 absTime);

  void addTempo(u32 microSeconds);
  void addTempoBPM(double BPM);
  void insertTempo(u32 microSeconds, u32 absTime);
  void insertTempoBPM(double BPM, u32 absTime);
  void addMidiPort(u8 port);
  void insertMidiPort(u8 port, u32 absTime);
  void addTimeSig(u8 numer, u8 denom, u8 clicksPerQuarter);
  void insertTimeSig(u8 numer, u8 denom, u8 ticksPerQuarter, u32 absTime);
  void addEndOfTrack();
  void insertEndOfTrack(u32 absTime);
  void addText(const std::string &str);
  void insertText(const std::string &str, u32 absTime);
  void addSeqName(const std::string &str);
  void insertSeqName(const std::string &str, u32 absTime);
  void addTrackName(const std::string &str);
  void insertTrackName(const std::string &str, u32 absTime);
  void addGMReset();
  void insertGMReset(u32 absTime);
  void addGM2Reset();
  void insertGM2Reset(u32 absTime);
  void addGSReset();
  void insertGSReset(u32 absTime);
  void addXGReset();
  void insertXGReset(u32 absTime);

  // SPECIAL EVENTS
  void insertGlobalTranspose(u32 absTime, s8 semitones);
  void addMarker(u8 channel,
                 const std::string &markername,
                 u8 databyte1,
                 u8 databyte2,
                 s8 priority = PRIORITY_MIDDLE);
  void insertMarker(u8 channel,
                    const std::string &markername,
                    u8 databyte1,
                    u8 databyte2,
                    s8 priority,
                    u32 absTime);

  // YMMY EVENTS
  void addYmmySynthAssignment(u8 channel, std::string synthName);

 public:
  MidiFile *parentSeq;
  bool bMonophonic;
  bool bHasEndOfTrack;
  int channelGroup;

  // state
  u32 DeltaTime;            //a time value to be used for AddEvent
  DurNoteEvent *prevDurEvent;
  std::vector<NoteEvent *> prevDurNoteOffs;
  bool bSustain;

  std::vector<MidiEvent *> aEvents;
  // activeNotes tracks which keys are on during conversion. It maps the note's original key to the
  // final realized key, which will be different when a global transpose is set. It helps us resolve
  // the correct note off events when a global transpose event occurs amidst live note on events,
  // and also allows us to warn about unpaired note on/off events.
  std::unordered_map<u8, u8> activeNotes;
};

class MidiFile {
 public:
  MidiFile(VGMSeq *assocSeq);
  ~MidiFile();
  MidiTrack *addTrack();
  MidiTrack *insertTrack(u32 trackNum);
  int getMidiTrackIndex(const MidiTrack *midiTrack);
  void setPPQN(u16 ppqn);
  u32 ppqn() const;
  void writeMidiToBuffer(std::vector<u8> &buf);
  void sort();
  bool saveMidiFile(const std::filesystem::path &filepath);

 protected:
  //bool bAddedTempo;
  //bool bAddedTimeSig;

 public:
  VGMSeq *assocSeq;

  std::vector<MidiTrack *> aTracks;
  MidiTrack globalTrack;            //events in the globalTrack will be copied into every other track
  s8 globalTranspose;
  bool bMonophonicTracks;

private:
  u16 m_ppqn;
};

class MidiEvent {
 public:
  MidiEvent(MidiTrack *thePrntTrk, u32 absoluteTime, u8 theChannel, s8 thePriority);
  virtual ~MidiEvent() = default;
  virtual MidiEventType eventType() = 0;
  bool isMetaEvent();
  bool isSysexEvent();
  static void writeVarLength(std::vector<u8> &buf, u32 value);
  virtual u32 writeEvent(std::vector<u8> &buf, u32 time) = 0;
  u32 writeMetaEvent(std::vector<u8> &buf, u32 time, u8 metaType,
    const u8* data, size_t dataSize) const;
  u32 writeMetaTextEvent(std::vector<u8> &buf, u32 time, u8 metaType,
    const std::string& str) const;

  static std::string getNoteName(int noteNumber);

  bool operator<(const MidiEvent &) const;
  bool operator>(const MidiEvent &) const;

  MidiTrack *prntTrk;
  u8 channel;
  u32 absTime;            //absolute time... the number of ticks from the very beginning of the sequence at which this event occurs
  s8 priority;
};

class PriorityCmp {
 public:
  bool operator()(const MidiEvent *a, const MidiEvent *b) const {
    return (a->priority < b->priority);
  }
};

class AbsTimeCmp {
 public:
  bool operator()(const MidiEvent *a, const MidiEvent *b) const {
    return (a->absTime < b->absTime);
  }
};

class NoteEvent
    : public MidiEvent {
 public:
  NoteEvent
      (MidiTrack *prntTrk, u8 channel, u32 absoluteTime, bool bNoteDown, u8 theKey, u8 theVel = 64);
  MidiEventType eventType() override { return MIDIEVENT_NOTEON; }
  u32 writeEvent(std::vector<u8> &buf, u32 time) override;

  bool bNoteDown;
  s8 key;
  s8 vel;
};

//class DurNoteEvent
//	: public MidiEvent
//{
//public:
//	DurNoteEvent(MidiTrack* prntTrk, u8 channel, u32 absoluteTime, u8 theKey, u8 theVel, u32 theDur);
//	//virtual void PrepareWrite(std::vector<MidiEvent*> & aEvents);
//	virtual u32 WriteEvent(std::vector<u8> & buf, u32 time);
//
//	bool bNoteDown;
//	s8 key;
//	s8 vel;
//	u32 duration;
//};

class ControllerEvent
    : public MidiEvent {
 public:
  ControllerEvent(MidiTrack *prntTrk,
                  u8 channel,
                  u32 absoluteTime,
                  u8 controllerNum,
                  u8 theDataByte,
                  s8 thePriority = PRIORITY_MIDDLE);
  MidiEventType eventType() override { return MIDIEVENT_UNDEFINED; }
  u32 writeEvent(std::vector<u8> &buf, u32 time) override;

  u8 controlNum;
  u8 dataByte;
};

class SysexEvent
    : public MidiEvent {
public:
  SysexEvent(MidiTrack *prntTrk,
             u32 absoluteTime,
             const std::vector<u8>& sysexData,
             s8 thePriority = PRIORITY_MIDDLE);
  MidiEventType eventType() override { return MIDIEVENT_UNDEFINED; }
  u32 writeEvent(std::vector<u8> &buf, u32 time) override;

  std::vector<u8> sysexData;
};

class VolumeEvent
    : public ControllerEvent {
 public:
  VolumeEvent(MidiTrack *prntTrk, u8 channel, u32 absoluteTime, u8 volume)
      : ControllerEvent(prntTrk, channel, absoluteTime, 7, volume, PRIORITY_MIDDLE) { }
  MidiEventType eventType() override { return MIDIEVENT_VOLUME; }
};

class VolumeFineEvent
    : public ControllerEvent {
public:
  VolumeFineEvent(MidiTrack *prntTrk, u8 channel, u32 absoluteTime, u8 volume_lsb)
      : ControllerEvent(prntTrk, channel, absoluteTime, 39, volume_lsb, PRIORITY_MIDDLE) { }
  MidiEventType eventType() override { return MIDIEVENT_VOLUME; }
};

class ExpressionEvent
    : public ControllerEvent {
 public:
  ExpressionEvent(MidiTrack *prntTrk, u8 channel, u32 absoluteTime, u8 expression)
      : ControllerEvent(prntTrk, channel, absoluteTime, 11, expression, PRIORITY_MIDDLE) { }
  MidiEventType eventType() override { return MIDIEVENT_EXPRESSION; }
};

class ExpressionFineEvent
    : public ControllerEvent {
public:
  ExpressionFineEvent(MidiTrack *prntTrk, u8 channel, u32 absoluteTime, u8 expression_lsb)
      : ControllerEvent(prntTrk, channel, absoluteTime, 43, expression_lsb, PRIORITY_MIDDLE) { }
  MidiEventType eventType() override { return MIDIEVENT_EXPRESSION; }
};

class SustainEvent
    : public ControllerEvent {
 public:
  SustainEvent(MidiTrack *prntTrk, u8 channel, u32 absoluteTime, u8 depth)
      : ControllerEvent(prntTrk, channel, absoluteTime, 64, depth, PRIORITY_MIDDLE) { }
  MidiEventType eventType() override { return MIDIEVENT_SUSTAIN; }
};

class PortamentoEvent
    : public ControllerEvent {
 public:
  PortamentoEvent(MidiTrack *prntTrk, u8 channel, u32 absoluteTime, u8 bOn)
      : ControllerEvent(prntTrk, channel, absoluteTime, 65, (bOn) ? 0x7F : 0, PRIORITY_MIDDLE) { }
  MidiEventType eventType() override { return MIDIEVENT_PORTAMENTO; }
};

class PortamentoTimeEvent
    : public ControllerEvent {
 public:
  PortamentoTimeEvent(MidiTrack *prntTrk, u8 channel, u32 absoluteTime, u8 time)
      : ControllerEvent(prntTrk, channel, absoluteTime, 5, time, PRIORITY_MIDDLE) { }
  MidiEventType eventType() override { return MIDIEVENT_PORTAMENTOTIME; }
};

class PortamentoTimeFineEvent
    : public ControllerEvent {
public:
  PortamentoTimeFineEvent(MidiTrack *prntTrk, u8 channel, u32 absoluteTime, u8 time)
      : ControllerEvent(prntTrk, channel, absoluteTime, 37, time, PRIORITY_MIDDLE) { }
  MidiEventType eventType() override { return MIDIEVENT_PORTAMENTOTIMEFINE; }
};

class PortamentoControlEvent
    : public ControllerEvent {
public:
  PortamentoControlEvent(MidiTrack *prntTrk, u8 channel, u32 absoluteTime, u8 key)
      : ControllerEvent(prntTrk, channel, absoluteTime, 84, key, PRIORITY_MIDDLE) { }
  MidiEventType eventType() override { return MIDIEVENT_PORTAMENTOCONTROL; }
  u32 writeEvent(std::vector<u8> &buf, u32 time) override;
};

class LegatoPedalEvent
    : public ControllerEvent {
public:
  LegatoPedalEvent(MidiTrack *prntTrk, u8 channel, u32 absoluteTime, bool bOn)
      : ControllerEvent(prntTrk, channel, absoluteTime, 68, (bOn) ? 0x7F : 0, PRIORITY_HIGH) { }
  MidiEventType eventType() override { return MIDIEVENT_LEGATOPEDAL; }
};

class PanEvent
    : public ControllerEvent {
 public:
  PanEvent(MidiTrack *prntTrk, u8 channel, u32 absoluteTime, u8 pan)
      : ControllerEvent(prntTrk, channel, absoluteTime, 10, pan, PRIORITY_MIDDLE) { }
  MidiEventType eventType() override { return MIDIEVENT_PAN; }
};

class ModulationEvent
    : public ControllerEvent {
 public:
  ModulationEvent(MidiTrack *prntTrk, u8 channel, u32 absoluteTime, u8 depth)
      : ControllerEvent(prntTrk, channel, absoluteTime, 1, depth, PRIORITY_MIDDLE) { }
  MidiEventType eventType() override { return MIDIEVENT_MODULATION; }
};

class BreathEvent
    : public ControllerEvent {
 public:
  BreathEvent(MidiTrack *prntTrk, u8 channel, u32 absoluteTime, u8 depth)
      : ControllerEvent(prntTrk, channel, absoluteTime, 2, depth, PRIORITY_MIDDLE) { }
  MidiEventType eventType() override { return MIDIEVENT_BREATH; }
};

class BankSelectEvent
    : public ControllerEvent {
 public:
  BankSelectEvent(MidiTrack *prntTrk, u8 channel, u32 absoluteTime, u8 bank)
      : ControllerEvent(prntTrk, channel, absoluteTime, 0, bank, PRIORITY_HIGH) { }
  MidiEventType eventType() override { return MIDIEVENT_BANKSELECT; }
};

class BankSelectFineEvent
    : public ControllerEvent {
 public:
  BankSelectFineEvent(MidiTrack *prntTrk, u8 channel, u32 absoluteTime, u8 bank)
      : ControllerEvent(prntTrk, channel, absoluteTime, 32, bank, PRIORITY_HIGH) { }
  MidiEventType eventType() override { return MIDIEVENT_BANKSELECTFINE; }
};

/*
class VolEvent
	: public ControllerEvent
{
public:
	VolEvent(MidiTrack* prntTrk, u8 channel, u32 absoluteTime, u8 theVol, s8 thePriority = PRIORITY_MIDDLE);
	virtual VolEvent* MakeCopy();
	//virtual u32 WriteEvent(std::vector<u8> & buf, u32 time);

	u8 vol;
};

class VolMarkerEvent
	: public MidiEvent
{
public:
	VolMarkerEvent(MidiTrack* prntTrk, u8 channel, u32 absoluteTime, u8 theVol, s8 thePriority = PRIORITY_HIGHER);
	virtual VolMarkerEvent* MakeCopy();
	virtual u32 WriteEvent(std::vector<u8> & buf, u32 time);

	u8 vol;
};*/

class MasterVolEvent
    : public SysexEvent {
public:
  MasterVolEvent(MidiTrack *prntTrk, u8 /* channel */, u32 absoluteTime, u8 msb, u8 lsb)
      : SysexEvent(prntTrk, absoluteTime, {0x07, 0x7F, 0x7F, 0x04, 0x01, lsb, msb }, PRIORITY_HIGHER) { }
  MidiEventType eventType() override { return MIDIEVENT_MASTERVOL; }
};

/*
class ExpressionEvent
	: public MidiEvent
{
public:
	ExpressionEvent(MidiTrack* prntTrk, u8 channel, u32 absoluteTime, u8 expression, s8 thePriority = PRIORITY_HIGHER);
	virtual ExpressionEvent* MakeCopy();
	virtual u32 WriteEvent(std::vector<u8> & buf, u32 time);

	u8 expression;
};*/

class MonoEvent
    : public ControllerEvent {
 public:
  MonoEvent(MidiTrack *prntTrk, u8 channel, u32 absoluteTime)
      : ControllerEvent(prntTrk, channel, absoluteTime, 126, 0, PRIORITY_HIGHER) { }
  MidiEventType eventType() override { return MIDIEVENT_MONO; }
};

class ProgChangeEvent
    : public MidiEvent {
 public:
  ProgChangeEvent(MidiTrack *prntTrk, u8 channel, u32 absoluteTime, u8 progNum);
  MidiEventType eventType() override { return MIDIEVENT_PROGRAMCHANGE; }
  //virtual ProgChangeEvent* MakeCopy();
  u32 writeEvent(std::vector<u8> &buf, u32 time) override;

  u8 programNum;
};

class PitchBendEvent
    : public MidiEvent {
 public:
  PitchBendEvent(MidiTrack *prntTrk, u8 channel, u32 absoluteTime, s16 bend);
  MidiEventType eventType() override { return MIDIEVENT_PITCHBEND; }
  //virtual PitchBendEvent* MakeCopy();
  u32 writeEvent(std::vector<u8> &buf, u32 time) override;

  s16 bend;
};

class ChannelPressureEvent
    : public MidiEvent {
 public:
  ChannelPressureEvent(MidiTrack *prntTrk, u8 channel, u32 absoluteTime, u8 pressure);
  MidiEventType eventType() override { return MIDIEVENT_CHANNELPRESSURE; }
  u32 writeEvent(std::vector<u8> &buf, u32 time) override;

  u8 pressure;
};

class TempoEvent
    : public MidiEvent {
 public:
  TempoEvent(MidiTrack *prntTrk, u32 absoluteTime, u32 microSeconds);
  MidiEventType eventType() override { return MIDIEVENT_TEMPO; }
  //virtual TempoEvent* MakeCopy();
  u32 writeEvent(std::vector<u8> &buf, u32 time) override;

  u32 microSecs;
};

class MidiPortEvent : public MidiEvent {
public:
  MidiPortEvent(MidiTrack *prntTrk, u32 absoluteTime, u8 port);
  MidiEventType eventType() override { return MIDIEVENT_MIDIPORT; }
  // virtual TimeSigEvent* MakeCopy();
  u32 writeEvent(std::vector<u8> &buf, u32 time) override;

  u8 port;
};

class TimeSigEvent
    : public MidiEvent {
 public:
  TimeSigEvent
      (MidiTrack *prntTrk, u32 absoluteTime, u8 numerator, u8 denominator, u8 clicksPerQuarter);
  MidiEventType eventType() override { return MIDIEVENT_TIMESIG; }
  //virtual TimeSigEvent* MakeCopy();
  u32 writeEvent(std::vector<u8> &buf, u32 time) override;

  u8 numer;
  u8 denom;
  u8 ticksPerQuarter;
};

class EndOfTrackEvent
    : public MidiEvent {
 public:
  EndOfTrackEvent(MidiTrack *prntTrk, u32 absoluteTime);
  MidiEventType eventType() override { return MIDIEVENT_ENDOFTRACK; }
  //virtual EndOfTrackEvent* MakeCopy();
  u32 writeEvent(std::vector<u8> &buf, u32 time) override;
};

class TextEvent
    : public MidiEvent {
 public:
  TextEvent(MidiTrack *prntTrk, u32 absoluteTime, std::string str);
  MidiEventType eventType() override { return MIDIEVENT_TEXT; }
  u32 writeEvent(std::vector<u8> &buf, u32 time) override;

  std::string text;
};

class SeqNameEvent
    : public MidiEvent {
 public:
  SeqNameEvent(MidiTrack *prntTrk, u32 absoluteTime, std::string str);
  MidiEventType eventType() override { return MIDIEVENT_TEXT; }
  u32 writeEvent(std::vector<u8> &buf, u32 time) override;

  std::string text;
};

class TrackNameEvent
    : public MidiEvent {
 public:
  TrackNameEvent(MidiTrack *prntTrk, u32 absoluteTime, std::string str);
  MidiEventType eventType() override { return MIDIEVENT_TEXT; }
  u32 writeEvent(std::vector<u8> &buf, u32 time) override;

  std::string text;
};

// SPECIAL EVENTS THAT AFFECT OTHER MIDI EVENTS RATHER THAN DIRECTLY OUTPUT TO THE FILE
class GlobalTransposeEvent
    : public MidiEvent {
 public:
  GlobalTransposeEvent(MidiTrack *prntTrk, u32 absoluteTime, s8 semitones);
  MidiEventType eventType() override { return MIDIEVENT_GLOBALTRANSPOSE; }
  u32 writeEvent(std::vector<u8> &buf, u32 time) override;

  s8 semitones;
};

class MarkerEvent
    : public MidiEvent {
 public:
  MarkerEvent(MidiTrack *prntTrk,
              u8 channel,
              u32 absoluteTime,
              const std::string &name,
              u8 databyte1,
              u8 databyte2,
              s8 thePriority = PRIORITY_MIDDLE)
      : MidiEvent(prntTrk, absoluteTime, channel, thePriority), name(name), databyte1(databyte1),
        databyte2(databyte2) { }
  MidiEventType eventType() override { return MIDIEVENT_MARKER; }
  u32 writeEvent(std::vector<u8>& /*buf*/, u32 time) override { return time; }

  std::string name;
  u8 databyte1, databyte2;
};

class GMResetEvent
    : public SysexEvent {
 public:
  GMResetEvent(MidiTrack *prntTrk, u32 absoluteTime)
       : SysexEvent(prntTrk, absoluteTime, { 0x05, 0x7E, 0x7F, 0x09, 0x01 }, PRIORITY_HIGHEST) { }
  MidiEventType eventType() override { return MIDIEVENT_RESET; }
};

class GM2ResetEvent
    : public SysexEvent {
 public:
  GM2ResetEvent(MidiTrack *prntTrk, u32 absoluteTime)
       : SysexEvent(prntTrk, absoluteTime, { 0x05, 0x7E, 0x7F, 0x09, 0x03 }, PRIORITY_HIGHEST) { }
  MidiEventType eventType() override { return MIDIEVENT_RESET; }
};

class GSResetEvent
    : public SysexEvent {
 public:
  GSResetEvent(MidiTrack *prntTrk, u32 absoluteTime)
       : SysexEvent(prntTrk, absoluteTime, { 0x0A, 0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7F, 0x00, 0x41 },
                    PRIORITY_HIGHEST) { }
  MidiEventType eventType() override { return MIDIEVENT_RESET; }
};

class XGResetEvent
    : public SysexEvent {
 public:
  XGResetEvent(MidiTrack *prntTrk, u32 absoluteTime)
       : SysexEvent(prntTrk, absoluteTime,  { 0x08, 0x43, 0x10, 0x4C, 0x00, 0x00, 0x7E, 0x00 },
                    PRIORITY_HIGHEST) { }
  MidiEventType eventType() override { return MIDIEVENT_RESET; }
};

// ***********
// YMMY Events
// ***********

class YmmySynthChangeEvent
    : public SysexEvent {
public:
  YmmySynthChangeEvent(MidiTrack *prntTrk, u8 channel, u8 group, u32 absoluteTime, std::string synthName)
      : SysexEvent(prntTrk, absoluteTime, buildData(channel, group, synthName), PRIORITY_HIGHEST) {
    // SysexEvent(prntTrk, absoluteTime, sysexData, PRIORITY_HIGHEST);
  }
  MidiEventType eventType() override { return MIDIEVENT_YMMY_SYNTHASSIGN; }

private:
  // Helper function to construct the data vector
  static std::vector<u8> buildData(u8 channel, u8 group, const std::string& synthName) {
    std::vector<u8> data;

    // Add length of data
    data.push_back(synthName.size() + 5 + 1);

    // Add YMMY Sysex event identifier
    data.push_back(0x7D);

    // Add Synth assignment opcode
    data.push_back(0x7E);

    // Add channel and group
    data.push_back(channel);
    data.push_back(group);

    // Add the contents of synthName followed by a null terminator (0x00)
    data.insert(data.end(), synthName.begin(), synthName.end());
    data.push_back(0x00);  // Null-terminated ASCII string

    return data;
  }
};
