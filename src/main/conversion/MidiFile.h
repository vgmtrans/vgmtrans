/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
#include <string>
#include <vector>
#include <list>
#include <map>

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
  MIDIEVENT_TEMPO,
  MIDIEVENT_TIMESIG,
  MIDIEVENT_MODULATION,
  MIDIEVENT_BREATH,
  MIDIEVENT_SUSTAIN,
  MIDIEVENT_PORTAMENTO,
  MIDIEVENT_PORTAMENTOTIME,
  MIDIEVENT_PORTAMENTOTIMEFINE,
  MIDIEVENT_PORTAMENTOCONTROL,
  MIDIEVENT_MONO,
  MIDIEVENT_LFO,
  MIDIEVENT_VIBRATO,
  MIDIEVENT_ENDOFTRACK,
  MIDIEVENT_TEXT,
  MIDIEVENT_RESET,
  MIDIEVENT_MIDIPORT
} MidiEventType;

class MidiTrack {
 public:
  MidiTrack(MidiFile *parentSeq, bool bMonophonic);
  virtual ~MidiTrack();

  void sort();
  void writeTrack(std::vector<uint8_t> &buf) const;

  //void setChannel(int theChannel);
  void setChannelGroup(int theChannelGroup);

  uint32_t getDelta() const;
  void setDelta(uint32_t NewDelta);
  void addDelta(uint32_t AddDelta);
  void subtractDelta(uint32_t SubtractDelta);
  void resetDelta();

  void addNoteOn(uint8_t channel, int8_t key, int8_t vel);
  void insertNoteOn(uint8_t channel, int8_t key, int8_t vel, uint32_t absTime);
  void addNoteOff(uint8_t channel, int8_t key);
  void insertNoteOff(uint8_t channel, int8_t key, uint32_t absTime);
  void addNoteByDur(uint8_t channel, int8_t key, int8_t vel, uint32_t duration);
  void addNoteByDur_TriAce(uint8_t channel, int8_t key, int8_t vel, uint32_t duration);
  void insertNoteByDur(uint8_t channel, int8_t key, int8_t vel, uint32_t duration, uint32_t absTime);
  void purgePrevNoteOffs();
  void purgePrevNoteOffs(uint32_t absTime);
  void addControllerEvent(uint8_t channel,
                          uint8_t controllerNum,
                          uint8_t theDataByte); // This function should be used for only redirection output of MIDI-like formats
  void insertControllerEvent(uint8_t channel,
                             uint8_t controllerNum,
                             uint8_t theDataByte,
                             uint32_t absTime); // This function should be used for only redirection output of MIDI-like formats
  void addVol(uint8_t channel, uint8_t vol/*, int8_t priority = PRIORITY_MIDDLE*/);
  void insertVol(uint8_t channel, uint8_t vol, uint32_t absTime/*, int8_t priority = PRIORITY_MIDDLE*/);
  void addVolumeFine(uint8_t channel, uint8_t volume_lsb);
  void insertVolumeFine(uint8_t channel, uint8_t volume_lsb, uint32_t absTime);
  void addMasterVol(uint8_t channel, u8 volMsb, u8 volLsb = 0);
  void insertMasterVol(uint8_t channel, u8 volMsb, uint32_t absTime);
  void insertMasterVol(uint8_t channel, u8 volMsb, u8 volLsb, uint32_t absTime);
  void addPan(uint8_t channel, uint8_t pan);
  void insertPan(uint8_t channel, uint8_t pan, uint32_t absTime);
  void addExpression(uint8_t channel, uint8_t expression);
  void addExpressionFine(uint8_t channel, uint8_t expression_lsb);
  void insertExpression(uint8_t channel, uint8_t expression, uint32_t absTime);
  void insertExpressionFine(uint8_t channel, uint8_t expression_lsb, uint32_t absTime);
  void addReverb(uint8_t channel, uint8_t reverb);
  void insertReverb(uint8_t channel, uint8_t reverb, uint32_t absTime);
  void addModulation(uint8_t channel, uint8_t depth);
  void insertModulation(uint8_t channel, uint8_t depth, uint32_t absTime);
  void addBreath(uint8_t channel, uint8_t depth);
  void insertBreath(uint8_t channel, uint8_t depth, uint32_t absTime);
  void addSustain(uint8_t channel, uint8_t depth);
  void insertSustain(uint8_t channel, uint8_t depth, uint32_t absTime);
  void addPortamento(uint8_t channel, bool bOn);
  void insertPortamento(uint8_t channel, bool bOn, uint32_t absTime);
  void addPortamentoTime(uint8_t channel, uint8_t time);
  void insertPortamentoTime(uint8_t channel, uint8_t time, uint32_t absTime);
  void addPortamentoTimeFine(uint8_t channel, uint8_t time);
  void insertPortamentoTimeFine(uint8_t channel, uint8_t time, uint32_t absTime);
  void addPortamentoControl(uint8_t channel, uint8_t key);
  void insertPortamentoControl(uint8_t channel, uint8_t key, uint32_t absTime);
  void addMono(uint8_t channel);
  void insertMono(uint8_t channel, uint32_t absTime);

  void addPitchBend(uint8_t channel, int16_t bend);
  void insertPitchBend(uint8_t channel, short bend, uint32_t absTime);
  void addPitchBendRange(uint8_t channel, uint16_t cents);
  void insertPitchBendRange(uint8_t channel, uint16_t cents, uint32_t absTime);
  void addFineTuning(uint8_t channel, uint8_t msb, uint8_t lsb);
  void insertFineTuning(uint8_t channel, uint8_t msb, uint8_t lsb, uint32_t absTime);
  void addFineTuning(uint8_t channel, double cents);
  void insertFineTuning(uint8_t channel, double cents, uint32_t absTime);
  void addCoarseTuning(uint8_t channel, uint8_t msb, uint8_t lsb);
  void insertCoarseTuning(uint8_t channel, uint8_t msb, uint8_t lsb, uint32_t absTime);
  void addCoarseTuning(uint8_t channel, double semitones);
  void insertCoarseTuning(uint8_t channel, double semitones, uint32_t absTime);
  void addModulationDepthRange(uint8_t channel, uint8_t msb, uint8_t lsb);
  void insertModulationDepthRange(uint8_t channel, uint8_t msb, uint8_t lsb, uint32_t absTime);
  void addModulationDepthRange(uint8_t channel, double semitones);
  void insertModulationDepthRange(uint8_t channel, double semitones, uint32_t absTime);
  void addProgramChange(uint8_t channel, uint8_t progNum);
  void addBankSelect(uint8_t channel, uint8_t bank);
  void addBankSelectFine(uint8_t channel, uint8_t lsb);
  void insertBankSelect(uint8_t channel, uint8_t bank, uint32_t absTime);

  void addTempo(uint32_t microSeconds);
  void addTempoBPM(double BPM);
  void insertTempo(uint32_t microSeconds, uint32_t absTime);
  void insertTempoBPM(double BPM, uint32_t absTime);
  void addMidiPort(uint8_t port);
  void insertMidiPort(uint8_t port, uint32_t absTime);
  void addTimeSig(uint8_t numer, uint8_t denom, uint8_t clicksPerQuarter);
  void insertTimeSig(uint8_t numer, uint8_t denom, uint8_t ticksPerQuarter, uint32_t absTime);
  void addEndOfTrack();
  void insertEndOfTrack(uint32_t absTime);
  void addText(const std::string &str);
  void insertText(const std::string &str, uint32_t absTime);
  void addSeqName(const std::string &str);
  void insertSeqName(const std::string &str, uint32_t absTime);
  void addTrackName(const std::string &str);
  void insertTrackName(const std::string &str, uint32_t absTime);
  void addGMReset();
  void insertGMReset(uint32_t absTime);
  void addGM2Reset();
  void insertGM2Reset(uint32_t absTime);
  void addGSReset();
  void insertGSReset(uint32_t absTime);
  void addXGReset();
  void insertXGReset(uint32_t absTime);

  // SPECIAL EVENTS
  void insertGlobalTranspose(uint32_t absTime, int8_t semitones);
  void addMarker(uint8_t channel,
                 const std::string &markername,
                 uint8_t databyte1,
                 uint8_t databyte2,
                 int8_t priority = PRIORITY_MIDDLE);
  void insertMarker(uint8_t channel,
                    const std::string &markername,
                    uint8_t databyte1,
                    uint8_t databyte2,
                    int8_t priority,
                    uint32_t absTime);

 public:
  MidiFile *parentSeq;
  bool bMonophonic;
  bool bHasEndOfTrack;
  int channelGroup;

  // state
  uint32_t DeltaTime;            //a time value to be used for AddEvent
  DurNoteEvent *prevDurEvent;
  std::vector<NoteEvent *> prevDurNoteOffs;
  bool bSustain;

  std::vector<MidiEvent *> aEvents;
  // activeNotes tracks which keys are on during conversion. It maps the note's original key to the
  // final realized key, which will be different when a global transpose is set. It helps us resolve
  // the correct note off events when a global transpose event occurs amidst live note on events,
  // and also allows us to warn about unpaired note on/off events.
  std::map<uint8_t, uint8_t> activeNotes;
};

class MidiFile {
 public:
  MidiFile(VGMSeq *assocSeq);
  ~MidiFile();
  MidiTrack *addTrack();
  MidiTrack *insertTrack(uint32_t trackNum);
  int getMidiTrackIndex(const MidiTrack *midiTrack);
  void setPPQN(uint16_t ppqn);
  uint32_t ppqn() const;
  void writeMidiToBuffer(std::vector<uint8_t> &buf);
  void sort();
  bool saveMidiFile(const std::string &filepath);

 protected:
  //bool bAddedTempo;
  //bool bAddedTimeSig;

 public:
  VGMSeq *assocSeq;

  std::vector<MidiTrack *> aTracks;
  MidiTrack globalTrack;            //events in the globalTrack will be copied into every other track
  int8_t globalTranspose;
  bool bMonophonicTracks;

private:
  uint16_t m_ppqn;
};

class MidiEvent {
 public:
  MidiEvent(MidiTrack *thePrntTrk, uint32_t absoluteTime, uint8_t theChannel, int8_t thePriority);
  virtual ~MidiEvent() = default;
  virtual MidiEventType eventType() = 0;
  bool isMetaEvent();
  bool isSysexEvent();
  static void writeVarLength(std::vector<uint8_t> &buf, uint32_t value);
  virtual uint32_t writeEvent(std::vector<uint8_t> &buf, uint32_t time) = 0;
  uint32_t writeMetaEvent(std::vector<uint8_t> &buf, uint32_t time, uint8_t metaType,
    const uint8_t* data, size_t dataSize) const;
  uint32_t writeMetaTextEvent(std::vector<uint8_t> &buf, uint32_t time, uint8_t metaType,
    const std::string& str) const;

  static std::string getNoteName(int noteNumber);

  bool operator<(const MidiEvent &) const;
  bool operator>(const MidiEvent &) const;

  MidiTrack *prntTrk;
  uint8_t channel;
  uint32_t absTime;            //absolute time... the number of ticks from the very beginning of the sequence at which this event occurs
  int8_t priority;
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
      (MidiTrack *prntTrk, uint8_t channel, uint32_t absoluteTime, bool bNoteDown, uint8_t theKey, uint8_t theVel = 64);
  MidiEventType eventType() override { return MIDIEVENT_NOTEON; }
  uint32_t writeEvent(std::vector<uint8_t> &buf, uint32_t time) override;

  bool bNoteDown;
  int8_t key;
  int8_t vel;
};

//class DurNoteEvent
//	: public MidiEvent
//{
//public:
//	DurNoteEvent(MidiTrack* prntTrk, uint8_t channel, uint32_t absoluteTime, uint8_t theKey, uint8_t theVel, uint32_t theDur);
//	//virtual void PrepareWrite(std::vector<MidiEvent*> & aEvents);
//	virtual uint32_t WriteEvent(std::vector<uint8_t> & buf, uint32_t time);
//
//	bool bNoteDown;
//	int8_t key;
//	int8_t vel;
//	uint32_t duration;
//};

class ControllerEvent
    : public MidiEvent {
 public:
  ControllerEvent(MidiTrack *prntTrk,
                  uint8_t channel,
                  uint32_t absoluteTime,
                  uint8_t controllerNum,
                  uint8_t theDataByte,
                  int8_t thePriority = PRIORITY_MIDDLE);
  MidiEventType eventType() override { return MIDIEVENT_UNDEFINED; }
  uint32_t writeEvent(std::vector<uint8_t> &buf, uint32_t time) override;

  uint8_t controlNum;
  uint8_t dataByte;
};

class SysexEvent
    : public MidiEvent {
public:
  SysexEvent(MidiTrack *prntTrk,
             uint32_t absoluteTime,
             const std::vector<uint8_t>& sysexData,
             int8_t thePriority = PRIORITY_MIDDLE);
  MidiEventType eventType() override { return MIDIEVENT_UNDEFINED; }
  uint32_t writeEvent(std::vector<uint8_t> &buf, uint32_t time) override;

  std::vector<uint8_t> sysexData;
};

class VolumeEvent
    : public ControllerEvent {
 public:
  VolumeEvent(MidiTrack *prntTrk, uint8_t channel, uint32_t absoluteTime, uint8_t volume)
      : ControllerEvent(prntTrk, channel, absoluteTime, 7, volume, PRIORITY_MIDDLE) { }
  MidiEventType eventType() override { return MIDIEVENT_VOLUME; }
};

class VolumeFineEvent
    : public ControllerEvent {
public:
  VolumeFineEvent(MidiTrack *prntTrk, uint8_t channel, uint32_t absoluteTime, uint8_t volume_lsb)
      : ControllerEvent(prntTrk, channel, absoluteTime, 39, volume_lsb, PRIORITY_MIDDLE) { }
  MidiEventType eventType() override { return MIDIEVENT_VOLUME; }
};

class ExpressionEvent
    : public ControllerEvent {
 public:
  ExpressionEvent(MidiTrack *prntTrk, uint8_t channel, uint32_t absoluteTime, uint8_t expression)
      : ControllerEvent(prntTrk, channel, absoluteTime, 11, expression, PRIORITY_MIDDLE) { }
  MidiEventType eventType() override { return MIDIEVENT_EXPRESSION; }
};

class ExpressionFineEvent
    : public ControllerEvent {
public:
  ExpressionFineEvent(MidiTrack *prntTrk, uint8_t channel, uint32_t absoluteTime, uint8_t expression_lsb)
      : ControllerEvent(prntTrk, channel, absoluteTime, 43, expression_lsb, PRIORITY_MIDDLE) { }
  MidiEventType eventType() override { return MIDIEVENT_EXPRESSION; }
};

class SustainEvent
    : public ControllerEvent {
 public:
  SustainEvent(MidiTrack *prntTrk, uint8_t channel, uint32_t absoluteTime, uint8_t depth)
      : ControllerEvent(prntTrk, channel, absoluteTime, 64, depth, PRIORITY_MIDDLE) { }
  MidiEventType eventType() override { return MIDIEVENT_SUSTAIN; }
};

class PortamentoEvent
    : public ControllerEvent {
 public:
  PortamentoEvent(MidiTrack *prntTrk, uint8_t channel, uint32_t absoluteTime, uint8_t bOn)
      : ControllerEvent(prntTrk, channel, absoluteTime, 65, (bOn) ? 0x7F : 0, PRIORITY_MIDDLE) { }
  MidiEventType eventType() override { return MIDIEVENT_PORTAMENTO; }
};

class PortamentoTimeEvent
    : public ControllerEvent {
 public:
  PortamentoTimeEvent(MidiTrack *prntTrk, uint8_t channel, uint32_t absoluteTime, uint8_t time)
      : ControllerEvent(prntTrk, channel, absoluteTime, 5, time, PRIORITY_MIDDLE) { }
  MidiEventType eventType() override { return MIDIEVENT_PORTAMENTOTIME; }
};

class PortamentoTimeFineEvent
    : public ControllerEvent {
public:
  PortamentoTimeFineEvent(MidiTrack *prntTrk, uint8_t channel, uint32_t absoluteTime, uint8_t time)
      : ControllerEvent(prntTrk, channel, absoluteTime, 37, time, PRIORITY_MIDDLE) { }
  MidiEventType eventType() override { return MIDIEVENT_PORTAMENTOTIMEFINE; }
};

class PortamentoControlEvent
    : public ControllerEvent {
public:
  PortamentoControlEvent(MidiTrack *prntTrk, uint8_t channel, uint32_t absoluteTime, uint8_t key)
      : ControllerEvent(prntTrk, channel, absoluteTime, 84, key, PRIORITY_MIDDLE) { }
  MidiEventType eventType() override { return MIDIEVENT_PORTAMENTOCONTROL; }
};

class PanEvent
    : public ControllerEvent {
 public:
  PanEvent(MidiTrack *prntTrk, uint8_t channel, uint32_t absoluteTime, uint8_t pan)
      : ControllerEvent(prntTrk, channel, absoluteTime, 10, pan, PRIORITY_MIDDLE) { }
  MidiEventType eventType() override { return MIDIEVENT_PAN; }
};

class ModulationEvent
    : public ControllerEvent {
 public:
  ModulationEvent(MidiTrack *prntTrk, uint8_t channel, uint32_t absoluteTime, uint8_t depth)
      : ControllerEvent(prntTrk, channel, absoluteTime, 1, depth, PRIORITY_MIDDLE) { }
  MidiEventType eventType() override { return MIDIEVENT_MODULATION; }
};

class BreathEvent
    : public ControllerEvent {
 public:
  BreathEvent(MidiTrack *prntTrk, uint8_t channel, uint32_t absoluteTime, uint8_t depth)
      : ControllerEvent(prntTrk, channel, absoluteTime, 2, depth, PRIORITY_MIDDLE) { }
  MidiEventType eventType() override { return MIDIEVENT_BREATH; }
};

class BankSelectEvent
    : public ControllerEvent {
 public:
  BankSelectEvent(MidiTrack *prntTrk, uint8_t channel, uint32_t absoluteTime, uint8_t bank)
      : ControllerEvent(prntTrk, channel, absoluteTime, 0, bank, PRIORITY_HIGH) { }
  MidiEventType eventType() override { return MIDIEVENT_BANKSELECT; }
};

class BankSelectFineEvent
    : public ControllerEvent {
 public:
  BankSelectFineEvent(MidiTrack *prntTrk, uint8_t channel, uint32_t absoluteTime, uint8_t bank)
      : ControllerEvent(prntTrk, channel, absoluteTime, 32, bank, PRIORITY_HIGH) { }
  MidiEventType eventType() override { return MIDIEVENT_BANKSELECTFINE; }
};

/*
class VolEvent
	: public ControllerEvent
{
public:
	VolEvent(MidiTrack* prntTrk, uint8_t channel, uint32_t absoluteTime, uint8_t theVol, int8_t thePriority = PRIORITY_MIDDLE);
	virtual VolEvent* MakeCopy();
	//virtual uint32_t WriteEvent(std::vector<uint8_t> & buf, uint32_t time);

	uint8_t vol;
};

class VolMarkerEvent
	: public MidiEvent
{
public:
	VolMarkerEvent(MidiTrack* prntTrk, uint8_t channel, uint32_t absoluteTime, uint8_t theVol, int8_t thePriority = PRIORITY_HIGHER);
	virtual VolMarkerEvent* MakeCopy();
	virtual uint32_t WriteEvent(std::vector<uint8_t> & buf, uint32_t time);

	uint8_t vol;
};*/

class MasterVolEvent
    : public SysexEvent {
public:
  MasterVolEvent(MidiTrack *prntTrk, uint8_t /* channel */, uint32_t absoluteTime, u8 msb, u8 lsb)
      : SysexEvent(prntTrk, absoluteTime, {0x07, 0x7F, 0x7F, 0x04, 0x01, lsb, msb }, PRIORITY_HIGHER) { }
  MidiEventType eventType() override { return MIDIEVENT_MASTERVOL; }
};

/*
class ExpressionEvent
	: public MidiEvent
{
public:
	ExpressionEvent(MidiTrack* prntTrk, uint8_t channel, uint32_t absoluteTime, uint8_t expression, int8_t thePriority = PRIORITY_HIGHER);
	virtual ExpressionEvent* MakeCopy();
	virtual uint32_t WriteEvent(std::vector<uint8_t> & buf, uint32_t time);

	uint8_t expression;
};*/

class MonoEvent
    : public ControllerEvent {
 public:
  MonoEvent(MidiTrack *prntTrk, uint8_t channel, uint32_t absoluteTime)
      : ControllerEvent(prntTrk, channel, absoluteTime, 126, 0, PRIORITY_HIGHER) { }
  MidiEventType eventType() override { return MIDIEVENT_MONO; }
};

class ProgChangeEvent
    : public MidiEvent {
 public:
  ProgChangeEvent(MidiTrack *prntTrk, uint8_t channel, uint32_t absoluteTime, uint8_t progNum);
  MidiEventType eventType() override { return MIDIEVENT_PROGRAMCHANGE; }
  //virtual ProgChangeEvent* MakeCopy();
  uint32_t writeEvent(std::vector<uint8_t> &buf, uint32_t time) override;

  uint8_t programNum;
};

class PitchBendEvent
    : public MidiEvent {
 public:
  PitchBendEvent(MidiTrack *prntTrk, uint8_t channel, uint32_t absoluteTime, int16_t bend);
  MidiEventType eventType() override { return MIDIEVENT_PITCHBEND; }
  //virtual PitchBendEvent* MakeCopy();
  uint32_t writeEvent(std::vector<uint8_t> &buf, uint32_t time) override;

  int16_t bend;
};


class TempoEvent
    : public MidiEvent {
 public:
  TempoEvent(MidiTrack *prntTrk, uint32_t absoluteTime, uint32_t microSeconds);
  MidiEventType eventType() override { return MIDIEVENT_TEMPO; }
  //virtual TempoEvent* MakeCopy();
  uint32_t writeEvent(std::vector<uint8_t> &buf, uint32_t time) override;

  uint32_t microSecs;
};

class MidiPortEvent : public MidiEvent {
public:
  MidiPortEvent(MidiTrack *prntTrk, uint32_t absoluteTime, uint8_t port);
  MidiEventType eventType() override { return MIDIEVENT_MIDIPORT; }
  // virtual TimeSigEvent* MakeCopy();
  uint32_t writeEvent(std::vector<uint8_t> &buf, uint32_t time) override;

  uint8_t port;
};

class TimeSigEvent
    : public MidiEvent {
 public:
  TimeSigEvent
      (MidiTrack *prntTrk, uint32_t absoluteTime, uint8_t numerator, uint8_t denominator, uint8_t clicksPerQuarter);
  MidiEventType eventType() override { return MIDIEVENT_TIMESIG; }
  //virtual TimeSigEvent* MakeCopy();
  uint32_t writeEvent(std::vector<uint8_t> &buf, uint32_t time) override;

  uint8_t numer;
  uint8_t denom;
  uint8_t ticksPerQuarter;
};

class EndOfTrackEvent
    : public MidiEvent {
 public:
  EndOfTrackEvent(MidiTrack *prntTrk, uint32_t absoluteTime);
  MidiEventType eventType() override { return MIDIEVENT_ENDOFTRACK; }
  //virtual EndOfTrackEvent* MakeCopy();
  uint32_t writeEvent(std::vector<uint8_t> &buf, uint32_t time) override;
};

class TextEvent
    : public MidiEvent {
 public:
  TextEvent(MidiTrack *prntTrk, uint32_t absoluteTime, std::string str);
  MidiEventType eventType() override { return MIDIEVENT_TEXT; }
  uint32_t writeEvent(std::vector<uint8_t> &buf, uint32_t time) override;

  std::string text;
};

class SeqNameEvent
    : public MidiEvent {
 public:
  SeqNameEvent(MidiTrack *prntTrk, uint32_t absoluteTime, std::string str);
  MidiEventType eventType() override { return MIDIEVENT_TEXT; }
  uint32_t writeEvent(std::vector<uint8_t> &buf, uint32_t time) override;

  std::string text;
};

class TrackNameEvent
    : public MidiEvent {
 public:
  TrackNameEvent(MidiTrack *prntTrk, uint32_t absoluteTime, std::string str);
  MidiEventType eventType() override { return MIDIEVENT_TEXT; }
  uint32_t writeEvent(std::vector<uint8_t> &buf, uint32_t time) override;

  std::string text;
};

// SPECIAL EVENTS THAT AFFECT OTHER MIDI EVENTS RATHER THAN DIRECTLY OUTPUT TO THE FILE
class GlobalTransposeEvent
    : public MidiEvent {
 public:
  GlobalTransposeEvent(MidiTrack *prntTrk, uint32_t absoluteTime, int8_t semitones);
  MidiEventType eventType() override { return MIDIEVENT_GLOBALTRANSPOSE; }
  uint32_t writeEvent(std::vector<uint8_t> &buf, uint32_t time) override;

  int8_t semitones;
};

class MarkerEvent
    : public MidiEvent {
 public:
  MarkerEvent(MidiTrack *prntTrk,
              uint8_t channel,
              uint32_t absoluteTime,
              const std::string &name,
              uint8_t databyte1,
              uint8_t databyte2,
              int8_t thePriority = PRIORITY_MIDDLE)
      : MidiEvent(prntTrk, absoluteTime, channel, thePriority), name(name), databyte1(databyte1),
        databyte2(databyte2) { }
  MidiEventType eventType() override { return MIDIEVENT_MARKER; }
  uint32_t writeEvent(std::vector<uint8_t>& /*buf*/, uint32_t time) override { return time; }

  std::string name;
  uint8_t databyte1, databyte2;
};

class GMResetEvent
    : public SysexEvent {
 public:
  GMResetEvent(MidiTrack *prntTrk, uint32_t absoluteTime)
       : SysexEvent(prntTrk, absoluteTime, { 0x05, 0x7E, 0x7F, 0x09, 0x01 }, PRIORITY_HIGHEST) { }
  MidiEventType eventType() override { return MIDIEVENT_RESET; }
};

class GM2ResetEvent
    : public SysexEvent {
 public:
  GM2ResetEvent(MidiTrack *prntTrk, uint32_t absoluteTime)
       : SysexEvent(prntTrk, absoluteTime, { 0x05, 0x7E, 0x7F, 0x09, 0x03 }, PRIORITY_HIGHEST) { }
  MidiEventType eventType() override { return MIDIEVENT_RESET; }
};

class GSResetEvent
    : public SysexEvent {
 public:
  GSResetEvent(MidiTrack *prntTrk, uint32_t absoluteTime)
       : SysexEvent(prntTrk, absoluteTime, { 0x0A, 0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7F, 0x00, 0x41 },
                    PRIORITY_HIGHEST) { }
  MidiEventType eventType() override { return MIDIEVENT_RESET; }
};

class XGResetEvent
    : public SysexEvent {
 public:
  XGResetEvent(MidiTrack *prntTrk, uint32_t absoluteTime)
       : SysexEvent(prntTrk, absoluteTime,  { 0x08, 0x43, 0x10, 0x4C, 0x00, 0x00, 0x7E, 0x00 },
                    PRIORITY_HIGHEST) { }
  MidiEventType eventType() override { return MIDIEVENT_RESET; }
};



/*
class AFX_EXT_CLASS ProgChangeEvent
	: public MidiEvent
{
	ProgChangeEvent(void);
	~ProgramChangeEvent(void);

	uint8_t progNum;
}

class AFX_EXT_CLASS ControllerEvent
	: public MidiEvent
{
public:
	NoteEvent(void);
	~NoteEvent(void);

	int8_t key;
	int8_t vel;
};*/

/*class AFX_EXT_CLASS NoteEventDur
	: public MidiEvent
{
public:
	NoteEvent(void);
	~NoteEvent(void);

	int8_t key;
	int8_t vel;
}*/
