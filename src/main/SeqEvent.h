#pragma once
#include "VGMItem.h"
#include "Menu.h"
#include "MidiFile.h"

#define DESCRIPTION(_str_)                                   \
    virtual std::wstring GetDescription()                    \
    {                                                        \
        std::wostringstream    desc;                         \
        desc << name << L" -  " << _str_;                    \
        return desc.str();                                   \
    }


class SeqTrack;

enum EventType {
  EVENTTYPE_UNDEFINED,
  EVENTTYPE_NOTEON,
  EVENTTYPE_NOTEOFF,
  EVENTTYPE_DURNOTE,
  EVENTTYPE_REST,
  EVENTTYPE_EXPRESSION,
  EVENTTYPE_EXPRESSIONSLIDE,
  EVENTTYPE_VOLUME,
  EVENTTYPE_VOLUMESLIDE,
  EVENTTYPE_PAN,
  EVENTTYPE_REVERB,
  EVENTTYPE_PROGCHANGE,
  EVENTTYPE_PITCHBEND,
  EVENTTYPE_PITCHBENDRANGE,
  EVENTTYPE_FINETUNING,
  EVENTTYPE_TRANSPOSE,
  EVENTTYPE_TEMPO,
  EVENTTYPE_TIMESIG,
  EVENTTYPE_MODULATION,
  EVENTTYPE_BREATH,
  EVENTTYPE_SUSTAIN,
  EVENTTYPE_PORTAMENTO,
  EVENTTYPE_PORTAMENTOTIME,
  EVENTTYPE_MARKER,
  EVENTTYPE_TRACKEND,
  EVENTTYPE_LOOPFOREVER
};

class SeqEvent:
    public VGMItem {
 public:
  SeqEvent(SeqTrack *pTrack,
           uint32_t offset = 0,
           uint32_t length = 0,
           const std::wstring &name = L"",
           uint8_t color = 0,
           Icon icon = ICON_BINARY,
           const std::wstring &desc = L"");
  virtual ~SeqEvent(void) { }
  virtual std::wstring GetDescription() {
    return desc.empty() ? std::wstring(name) : (std::wstring(name) + L" - " + desc);
  }
  virtual ItemType GetType() const { return ITEMTYPE_SEQEVENT; }
  virtual EventType GetEventType() { return EVENTTYPE_UNDEFINED; }
  virtual Icon GetIcon() { return icon; }

 public:
  uint8_t channel;
  SeqTrack *parentTrack;
 private:
  Icon icon;
  std::wstring desc;
};


//  ***************
//  DurNoteSeqEvent
//  ***************

class DurNoteSeqEvent:
    public SeqEvent {
 public:
  DurNoteSeqEvent(SeqTrack *pTrack,
                  uint8_t absoluteKey,
                  uint8_t velocity,
                  uint32_t duration,
                  uint32_t offset = 0,
                  uint32_t length = 0,
                  const std::wstring &name = L"");
  virtual ~DurNoteSeqEvent(void) { }
  virtual EventType GetEventType() { return EVENTTYPE_DURNOTE; }
  virtual Icon GetIcon() { return ICON_NOTE; }
  DESCRIPTION(
      L"Abs Key: " << (int) absKey << " (" << MidiEvent::GetNoteName(absKey) << ") " << L"  Velocity: " << (int) vel
          << L"  Duration: " << dur)
 public:
  uint8_t absKey;
  uint8_t vel;
  uint32_t dur;
};

//  **************
//  NoteOnSeqEvent
//  ***************

class NoteOnSeqEvent:
    public SeqEvent {
 public:
  NoteOnSeqEvent(SeqTrack *pTrack,
                 uint8_t absoluteKey,
                 uint8_t velocity,
                 uint32_t offset = 0,
                 uint32_t length = 0,
                 const std::wstring &name = L"");
  virtual ~NoteOnSeqEvent(void) { }
  virtual EventType GetEventType() { return EVENTTYPE_NOTEON; }
  virtual Icon GetIcon() { return ICON_NOTE; }
  DESCRIPTION(
      L"Abs Key: " << (int) absKey << " (" << MidiEvent::GetNoteName(absKey) << ") " << L"  Velocity: " << (int) vel)
 public:
  uint8_t absKey;
  uint8_t vel;
};

//  ***************
//  NoteOffSeqEvent
//  ***************

class NoteOffSeqEvent:
    public SeqEvent {
 public:
  NoteOffSeqEvent
      (SeqTrack *pTrack, uint8_t absoluteKey, uint32_t offset = 0, uint32_t length = 0, const std::wstring &name = L"");
  virtual ~NoteOffSeqEvent(void) { }
  virtual EventType GetEventType() { return EVENTTYPE_NOTEOFF; }
  virtual Icon GetIcon() { return ICON_NOTE; }
  DESCRIPTION(L"Abs Key: " << (int) absKey << " (" << MidiEvent::GetNoteName(absKey) << ") ")
 public:
  uint8_t absKey;
};

//  ************
//  RestSeqEvent
//  ************

class RestSeqEvent:
    public SeqEvent {
 public:
  RestSeqEvent
      (SeqTrack *pTrack, uint32_t duration, uint32_t offset = 0, uint32_t length = 0, const std::wstring &name = L"");
  virtual ~RestSeqEvent(void) { }
  virtual EventType GetEventType() { return EVENTTYPE_REST; }
  virtual Icon GetIcon() { return ICON_REST; }
  DESCRIPTION(L"Duration: " << dur)

 public:
  uint32_t dur;
};

//  *****************
//  SetOctaveSeqEvent
//  *****************

class SetOctaveSeqEvent:
    public SeqEvent {
 public:
  SetOctaveSeqEvent
      (SeqTrack *pTrack, uint8_t octave, uint32_t offset = 0, uint32_t length = 0, const std::wstring &name = L"");
  virtual ~SetOctaveSeqEvent(void) { }
  DESCRIPTION(L"Octave: " << (int) octave)
 public:
  uint8_t octave;
};

//  ***********
//  VolSeqEvent
//  ***********

class VolSeqEvent:
    public SeqEvent {
 public:
  VolSeqEvent
      (SeqTrack *pTrack, uint8_t volume, uint32_t offset = 0, uint32_t length = 0, const std::wstring &name = L"");
  virtual ~VolSeqEvent(void) { }
  virtual EventType GetEventType() { return EVENTTYPE_VOLUME; }
  DESCRIPTION(L"Volume: " << (int) vol)

 public:
  uint8_t vol;
};

//  ****************
//  VolSlideSeqEvent
//  ****************

class VolSlideSeqEvent:
    public SeqEvent {
 public:
  VolSlideSeqEvent(SeqTrack *pTrack,
                   uint8_t targetVolume,
                   uint32_t duration,
                   uint32_t offset = 0,
                   uint32_t length = 0,
                   const std::wstring &name = L"");
  virtual ~VolSlideSeqEvent(void) { }
  virtual EventType GetEventType() { return EVENTTYPE_VOLUMESLIDE; }
  DESCRIPTION(L"Target Volume: " << (int) targVol << L"  Duration: " << dur)

 public:
  uint8_t targVol;
  uint32_t dur;
};


//  ***********
//  MastVolSeqEvent
//  ***********

class MastVolSeqEvent:
    public SeqEvent {
 public:
  MastVolSeqEvent
      (SeqTrack *pTrack, uint8_t volume, uint32_t offset = 0, uint32_t length = 0, const std::wstring &name = L"");
  virtual ~MastVolSeqEvent(void) { }
  DESCRIPTION(L"Master Volume: " << (int) vol)

 public:
  uint8_t vol;
};

//  ****************
//  MastVolSlideSeqEvent
//  ****************

class MastVolSlideSeqEvent:
    public SeqEvent {
 public:
  MastVolSlideSeqEvent(SeqTrack *pTrack,
                       uint8_t targetVolume,
                       uint32_t duration,
                       uint32_t offset = 0,
                       uint32_t length = 0,
                       const std::wstring &name = L"");
  virtual ~MastVolSlideSeqEvent(void) { }
  DESCRIPTION(L"Target Volume: " << (int) targVol << L"  Duration: " << dur)

 public:
  uint8_t targVol;
  uint32_t dur;
};

//  ******************
//  ExpressionSeqEvent
//  ******************

class ExpressionSeqEvent:
    public SeqEvent {
 public:
  ExpressionSeqEvent
      (SeqTrack *pTrack, uint8_t level, uint32_t offset = 0, uint32_t length = 0, const std::wstring &name = L"");
  virtual ~ExpressionSeqEvent(void) { }
  virtual EventType GetEventType() { return EVENTTYPE_EXPRESSION; }
  DESCRIPTION(L"Expression: " << (int) level)

 public:
  uint8_t level;
};

//  ***********************
//  ExpressionSlideSeqEvent
//  ***********************

class ExpressionSlideSeqEvent:
    public SeqEvent {
 public:
  ExpressionSlideSeqEvent(SeqTrack *pTrack,
                          uint8_t targetExpression,
                          uint32_t duration,
                          uint32_t offset = 0,
                          uint32_t length = 0,
                          const std::wstring &name = L"");
  virtual ~ExpressionSlideSeqEvent(void) { }
  virtual EventType GetEventType() { return EVENTTYPE_EXPRESSIONSLIDE; }
  DESCRIPTION(L"Target Expression: " << (int) targExpr << L"  Duration: " << dur)

 public:
  uint8_t targExpr;
  uint32_t dur;
};


//  ***********
//  PanSeqEvent
//  ***********

class PanSeqEvent:
    public SeqEvent {
 public:
  PanSeqEvent(SeqTrack *pTrack, uint8_t pan, uint32_t offset = 0, uint32_t length = 0, const std::wstring &name = L"");
  virtual ~PanSeqEvent(void) { }
  virtual EventType GetEventType() { return EVENTTYPE_PAN; }
  DESCRIPTION(L"Pan: " << (int) pan)

 public:
  uint8_t pan;
};

//  ****************
//  PanSlideSeqEvent
//  ****************

class PanSlideSeqEvent:
    public SeqEvent {
 public:
  PanSlideSeqEvent(SeqTrack *pTrack,
                   uint8_t targetPan,
                   uint32_t duration,
                   uint32_t offset = 0,
                   uint32_t length = 0,
                   const std::wstring &name = L"");
  virtual ~PanSlideSeqEvent(void) { }
  DESCRIPTION(L"Target Pan: " << (int) targPan << L"  Duration: " << dur)

 public:
  uint8_t targPan;
  uint32_t dur;
};


//  **************
//  ReverbSeqEvent
//  **************

class ReverbSeqEvent:
    public SeqEvent {
 public:
  ReverbSeqEvent
      (SeqTrack *pTrack, uint8_t reverb, uint32_t offset = 0, uint32_t length = 0, const std::wstring &name = L"");
  virtual ~ReverbSeqEvent(void) { }
  virtual EventType GetEventType() { return EVENTTYPE_REVERB; }
  DESCRIPTION(L"Reverb: " << (int) reverb)

 public:
  uint8_t reverb;
};


//  *****************
//  PitchBendSeqEvent
//  *****************

class PitchBendSeqEvent:
    public SeqEvent {
 public:
  PitchBendSeqEvent
      (SeqTrack *pTrack, short thePitchBend, uint32_t offset = 0, uint32_t length = 0, const std::wstring &name = L"");
  virtual EventType GetEventType() { return EVENTTYPE_PITCHBEND; }
  DESCRIPTION(L"Pitch Bend: " << pitchbend)

 public:
  short pitchbend;
};


//  **********************
//  PitchBendRangeSeqEvent
//  **********************

class PitchBendRangeSeqEvent:
    public SeqEvent {
 public:
  PitchBendRangeSeqEvent(SeqTrack *pTrack, uint8_t semiTones, uint8_t cents,
                         uint32_t offset = 0, uint32_t length = 0, const std::wstring &name = L"");
  virtual EventType GetEventType() { return EVENTTYPE_PITCHBENDRANGE; }
  DESCRIPTION(L"Pitch Bend Range: " << semitones << L" semitones, " << cents << L" cents")

 public:
  uint8_t semitones;
  uint8_t cents;
};

//  ******************
//  FineTuningSeqEvent
//  ******************

class FineTuningSeqEvent:
    public SeqEvent {
 public:
  FineTuningSeqEvent(SeqTrack *pTrack, double cents,
                     uint32_t offset = 0, uint32_t length = 0, const std::wstring &name = L"");
  virtual EventType GetEventType() { return EVENTTYPE_PITCHBENDRANGE; }
  DESCRIPTION(L"Fine Tuning: " << cents << L" cents")

 public:
  double cents;
};

//  ****************************
//  ModulationDepthRangeSeqEvent
//  ****************************

class ModulationDepthRangeSeqEvent:
    public SeqEvent {
 public:
  ModulationDepthRangeSeqEvent(SeqTrack *pTrack, double semitones,
                               uint32_t offset = 0, uint32_t length = 0, const std::wstring &name = L"");
  virtual EventType GetEventType() { return EVENTTYPE_PITCHBENDRANGE; }
  DESCRIPTION(L"Modulation Depth Range: " << (semitones * 100.0) << L" cents")

 public:
  double semitones;
};

//  *****************
//  TransposeSeqEvent
//  *****************

class TransposeSeqEvent:
    public SeqEvent {
 public:
  TransposeSeqEvent
      (SeqTrack *pTrack, int theTranspose, uint32_t offset = 0, uint32_t length = 0, const std::wstring &name = L"");
  virtual EventType GetEventType() { return EVENTTYPE_TRANSPOSE; }
  DESCRIPTION(L"Transpose: " << transpose)

 public:
  int transpose;
};


//  ******************
//  ModulationSeqEvent
//  ******************

class ModulationSeqEvent:
    public SeqEvent {
 public:
  ModulationSeqEvent
      (SeqTrack *pTrack, uint8_t theDepth, uint32_t offset = 0, uint32_t length = 0, const std::wstring &name = L"");
  virtual ~ModulationSeqEvent(void) { }
  virtual EventType GetEventType() { return EVENTTYPE_MODULATION; }
  DESCRIPTION(L"Depth: " << (int) depth)

 public:
  uint8_t depth;
};

//  **************
//  BreathSeqEvent
//  **************

class BreathSeqEvent:
    public SeqEvent {
 public:
  BreathSeqEvent
      (SeqTrack *pTrack, uint8_t theDepth, uint32_t offset = 0, uint32_t length = 0, const std::wstring &name = L"");
  virtual ~BreathSeqEvent(void) { }
  virtual EventType GetEventType() { return EVENTTYPE_BREATH; }
  DESCRIPTION(L"Breath: " << (int) depth)

 public:
  uint8_t depth;
};

//  ****************
//  SustainSeqEvent
//  ****************

class SustainSeqEvent:
    public SeqEvent {
 public:
  SustainSeqEvent
      (SeqTrack *pTrack, uint8_t theDepth, uint32_t offset = 0, uint32_t length = 0, const std::wstring &name = L"");
  virtual ~SustainSeqEvent(void) { }
  virtual EventType GetEventType() { return EVENTTYPE_SUSTAIN; }
  DESCRIPTION(L"Sustain Pedal: " << (int) depth);

 public:
  uint8_t depth;
};

//  ******************
//  PortamentoSeqEvent
//  ******************

class PortamentoSeqEvent:
    public SeqEvent {
 public:
  PortamentoSeqEvent
      (SeqTrack *pTrack, bool bPortamento, uint32_t offset = 0, uint32_t length = 0, const std::wstring &name = L"");
  virtual ~PortamentoSeqEvent(void) { }
  virtual EventType GetEventType() { return EVENTTYPE_PORTAMENTO; }
  DESCRIPTION(L"Portamento: " << (bOn) ? L"On" : L"Off")

 public:
  bool bOn;
};

//  **********************
//  PortamentoTimeSeqEvent
//  **********************

class PortamentoTimeSeqEvent:
    public SeqEvent {
 public:
  PortamentoTimeSeqEvent
      (SeqTrack *pTrack, uint8_t time, uint32_t offset = 0, uint32_t length = 0, const std::wstring &name = L"");
  virtual ~PortamentoTimeSeqEvent(void) { }
  virtual EventType GetEventType() { return EVENTTYPE_PORTAMENTOTIME; }
  DESCRIPTION(L"Portamento Time: " << (int) time)

 public:
  uint8_t time;
};


//  ******************
//  ProgChangeSeqEvent
//  ******************

class ProgChangeSeqEvent:
    public SeqEvent {
 public:
  ProgChangeSeqEvent(SeqTrack *pTrack,
                     uint32_t programNumber,
                     uint32_t offset = 0,
                     uint32_t length = 0,
                     const std::wstring &name = L"");
  virtual ~ProgChangeSeqEvent(void) { }
  virtual EventType GetEventType() { return EVENTTYPE_PROGCHANGE; }
  DESCRIPTION(L"Program Number: " << (int) progNum)

 public:
  uint32_t progNum;
};

//  *************
//  TempoSeqEvent
//  *************

class TempoSeqEvent:
    public SeqEvent {
 public:
  TempoSeqEvent(SeqTrack *pTrack,
                double beatsperminute,
                uint32_t offset = 0,
                uint32_t length = 0,
                const std::wstring &name = L"");
  virtual ~TempoSeqEvent(void) { }
  virtual EventType GetEventType() { return EVENTTYPE_TEMPO; }
  virtual Icon GetIcon() { return ICON_TEMPO; }
  DESCRIPTION(L"BPM: " << bpm)

 public:
  double bpm;
};

//  ******************
//  TempoSlideSeqEvent
//  ******************

class TempoSlideSeqEvent:
    public SeqEvent {
 public:
  TempoSlideSeqEvent(SeqTrack *pTrack,
                     double targBPM,
                     uint32_t duration,
                     uint32_t offset = 0,
                     uint32_t length = 0,
                     const std::wstring &name = L"");
  virtual ~TempoSlideSeqEvent(void) { }
  virtual Icon GetIcon() { return ICON_TEMPO; }
  DESCRIPTION(L"BPM: " << targbpm << L"  Duration: " << dur)

 public:
  double targbpm;
  uint32_t dur;
};

//  ***************
//  TimeSigSeqEvent
//  ***************

class TimeSigSeqEvent:
    public SeqEvent {
 public:
  TimeSigSeqEvent(SeqTrack *pTrack,
                  uint8_t numerator,
                  uint8_t denominator,
                  uint8_t theTicksPerQuarter,
                  uint32_t offset = 0,
                  uint32_t length = 0,
                  const std::wstring &name = L"");
  virtual ~TimeSigSeqEvent(void) { }
  virtual EventType GetEventType() { return EVENTTYPE_TIMESIG; }
  DESCRIPTION(L"Signature: " << (int) numer << L"/" << (int) denom << L"  Ticks Per Quarter: " << (int) ticksPerQuarter)

 public:
  uint8_t numer;
  uint8_t denom;
  uint8_t ticksPerQuarter;
};

//  **************
//  MarkerSeqEvent
//  **************

class MarkerSeqEvent:
    public SeqEvent {
 public:
  MarkerSeqEvent(SeqTrack *pTrack,
                 const std::string &markername,
                 uint8_t databyte1,
                 uint8_t databyte2,
                 uint32_t offset = 0,
                 uint32_t length = 0,
                 const std::wstring &name = L"",
                 uint8_t color = CLR_MARKER)
      : SeqEvent(pTrack, offset, length, name, color), databyte1(databyte1), databyte2(databyte2) { }
  virtual EventType GetEventType() { return EVENTTYPE_MARKER; }

 public:
  uint8_t databyte1;
  uint8_t databyte2;
};

//  ***************
//  VibratoSeqEvent
//  ***************

//class VibratoSeqEvent :
//	public SeqEvent
//{
//public:
//	VibratoSeqEvent(SeqTrack* pTrack, uint8_t detph, uint32_t offset = 0, uint32_t length = 0, const std::wstring& name = L"") 
//	: SeqEvent(pTrack, offset, length, name), depth(depth)
//	{}
//	virtual EventType GetEventType() { return EVENTTYPE_VIBRATO; }
//
//public:
//	uint8_t depth;
//};

//  ****************
//  TrackEndSeqEvent
//  ****************

class TrackEndSeqEvent:
    public SeqEvent {
 public:
  TrackEndSeqEvent(SeqTrack *pTrack, uint32_t offset = 0, uint32_t length = 0, const std::wstring &name = L"")
      : SeqEvent(pTrack, offset, length, name, CLR_TRACKEND) { }
  virtual EventType GetEventType() { return EVENTTYPE_TRACKEND; }
};

//  *******************
//  LoopForeverSeqEvent
//  *******************

class LoopForeverSeqEvent:
    public SeqEvent {
 public:
  LoopForeverSeqEvent(SeqTrack *pTrack, uint32_t offset = 0, uint32_t length = 0, const std::wstring &name = L"")
      : SeqEvent(pTrack, offset, length, name, CLR_LOOPFOREVER) { }
  virtual EventType GetEventType() { return EVENTTYPE_LOOPFOREVER; }
};