/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once
#include "VGMItem.h"
#include "MidiFile.h"
#include <spdlog/fmt/fmt.h>

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
  EVENTTYPE_BANKSELECT,
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
  explicit SeqEvent(SeqTrack *pTrack,
                    uint32_t offset = 0,
                    uint32_t length = 0,
                    const std::string &name = "",
                    EventColor color = CLR_UNKNOWN,
                    Icon icon = ICON_BINARY,
                    const std::string &desc = "");
  ~SeqEvent() override = default;
  std::string description() override {
    return m_description;
  }
  virtual EventType eventType() { return EVENTTYPE_UNDEFINED; }
  Icon icon() override { return m_icon; }

 public:
  uint8_t channel;
  SeqTrack *parentTrack;
 private:
  Icon m_icon;
  std::string m_description;
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
                  const std::string &name = "");
  EventType eventType() override { return EVENTTYPE_DURNOTE; }
  Icon icon() override { return ICON_NOTE; }

  std::string description() override {
    return fmt::format("{} - abs key: {} ({}), velocity: {}, duration: {}", name(),
      static_cast<int>(absKey), MidiEvent::getNoteName(absKey), static_cast<int>(vel), dur);
  };

 public:
  uint8_t absKey;
  uint8_t vel;
  uint32_t dur;
};

//  **************
//  NoteOnSeqEvent
//  ***************

class NoteOnSeqEvent : public SeqEvent {
 public:
  NoteOnSeqEvent(SeqTrack *pTrack,
                 uint8_t absoluteKey,
                 uint8_t velocity,
                 uint32_t offset = 0,
                 uint32_t length = 0,
                 const std::string &name = "");
  EventType eventType() override { return EVENTTYPE_NOTEON; }
  Icon icon() override { return ICON_NOTE; }

  std::string description() override {
    return fmt::format("{} - abs key: {} ({}), velocity: {}", name(),
      static_cast<int>(absKey), MidiEvent::getNoteName(absKey), static_cast<int>(vel));
  };

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
      (SeqTrack *pTrack, uint8_t absoluteKey, uint32_t offset = 0, uint32_t length = 0, const std::string &name = "");
  EventType eventType() override { return EVENTTYPE_NOTEOFF; }
  Icon icon() override { return ICON_NOTE_OFF; }

  std::string description() override {
    return fmt::format("{} - abs key: {} ({})", name(), static_cast<int>(absKey),
      MidiEvent::getNoteName(absKey));
  };

 public:
  uint8_t absKey;
};

//  ************
//  RestSeqEvent
//  ************

class RestSeqEvent : public SeqEvent {
 public:
  RestSeqEvent(SeqTrack *pTrack, uint32_t duration, uint32_t offset = 0, uint32_t length = 0,
               const std::string &name = "");
  EventType eventType() override { return EVENTTYPE_REST; }
  Icon icon() override { return ICON_REST; }

  std::string description() override { return fmt::format("{} - duration: {}", name(), dur); };

 public:
  uint32_t dur;
};

//  *****************
//  SetOctaveSeqEvent
//  *****************

class SetOctaveSeqEvent : public SeqEvent {
 public:
  SetOctaveSeqEvent(SeqTrack *pTrack, uint8_t octave, uint32_t offset = 0, uint32_t length = 0,
                    const std::string &name = "");

  std::string description() override {
    return fmt::format("{} - octave: {}", name(), static_cast<int>(octave));
  };

  uint8_t octave;
};

//  ***********
//  VolSeqEvent
//  ***********

class VolSeqEvent : public SeqEvent {
 public:
  VolSeqEvent(SeqTrack *pTrack, uint8_t volume, uint32_t offset = 0, uint32_t length = 0,
              const std::string &name = "");
  EventType eventType() override { return EVENTTYPE_VOLUME; }
  Icon icon() override { return ICON_VOLUME; }

  std::string description() override {
    return fmt::format("{} - volume: {:d}", name(), vol);
  };

  uint8_t vol;
};

//  *******************
//  Volume14BitSeqEvent
//  *******************

class Volume14BitSeqEvent : public SeqEvent {
public:
  Volume14BitSeqEvent(SeqTrack *pTrack, uint16_t volume, uint32_t offset = 0, uint32_t length = 0,
                      const std::string &name = "");
  EventType eventType() override { return EVENTTYPE_VOLUME; }
  Icon icon() override { return ICON_VOLUME; }

  std::string description() override {
    return fmt::format("{} - volume: {:d}", name(), m_volume);
  };

  uint16_t m_volume;
};

//  ****************
//  VolSlideSeqEvent
//  ****************

class VolSlideSeqEvent : public SeqEvent {
 public:
  VolSlideSeqEvent(SeqTrack *pTrack, uint8_t targetVolume, uint32_t duration, uint32_t offset = 0,
                   uint32_t length = 0, const std::string &name = "");
  EventType eventType() override { return EVENTTYPE_VOLUMESLIDE; }
  Icon icon() override { return ICON_VOLUME; }

  std::string description() override {
    return fmt::format("{} - target volume: {}, duration: {}", name(),
      static_cast<int>(targVol), dur);
  };

  uint8_t targVol;
  uint32_t dur;
};

//  ***********
//  MastVolSeqEvent
//  ***********

class MastVolSeqEvent : public SeqEvent {
 public:
    MastVolSeqEvent(SeqTrack *pTrack, uint8_t volume, uint32_t offset = 0, uint32_t length = 0,
                    const std::string &name = "");
    Icon icon() override { return ICON_VOLUME; }

    std::string description() override {
        return fmt::format("{} - master volume: {}", name(), static_cast<int>(vol));
    };

  uint8_t vol;
};

//  ****************
//  MastVolSlideSeqEvent
//  ****************

class MastVolSlideSeqEvent : public SeqEvent {
 public:
    MastVolSlideSeqEvent(SeqTrack *pTrack, uint8_t targetVolume, uint32_t duration,
                         uint32_t offset = 0, uint32_t length = 0, const std::string &name = "");
    Icon icon() override { return ICON_VOLUME; }

    std::string description() override {
        return fmt::format("{} - target volume: {}, duration: {}", name(),
          static_cast<int>(targVol), dur);
    };

  uint8_t targVol;
  uint32_t dur;
};

//  ******************
//  ExpressionSeqEvent
//  ******************

class ExpressionSeqEvent : public SeqEvent {
 public:
  ExpressionSeqEvent(SeqTrack *pTrack, uint8_t level, uint32_t offset = 0, uint32_t length = 0,
                     const std::string &name = "");
  EventType eventType() override { return EVENTTYPE_EXPRESSION; }
  Icon icon() override { return ICON_VOLUME; }

  std::string description() override {
    return fmt::format("{} - expression: {}", name(), static_cast<int>(level));
  };

 public:
  uint8_t level;
};

//  ***********************
//  ExpressionSlideSeqEvent
//  ***********************

class ExpressionSlideSeqEvent : public SeqEvent {
 public:
    ExpressionSlideSeqEvent(SeqTrack *pTrack, uint8_t targetExpression, uint32_t duration,
                            uint32_t offset = 0, uint32_t length = 0, const std::string &name = "");
  EventType eventType() override { return EVENTTYPE_EXPRESSIONSLIDE; }
  Icon icon() override { return ICON_VOLUME; }

  std::string description() override {
    return fmt::format("{} - target expression: {}, duration: {}", name(),
      static_cast<int>(targExpr), dur);
  };

  uint8_t targExpr;
  uint32_t dur;
};

//  ***********
//  PanSeqEvent
//  ***********

class PanSeqEvent : public SeqEvent {
 public:
  PanSeqEvent(SeqTrack *pTrack, uint8_t pan, uint32_t offset = 0, uint32_t length = 0,
              const std::string &name = "");
  EventType eventType() override { return EVENTTYPE_PAN; }
  Icon icon() override { return ICON_PAN; }

  std::string description() override {
    return fmt::format("{} - pan: {}", name(), static_cast<int>(pan));
  };

 public:
  uint8_t pan;
};

//  ****************
//  PanSlideSeqEvent
//  ****************

class PanSlideSeqEvent : public SeqEvent {
 public:
  PanSlideSeqEvent(SeqTrack *pTrack, uint8_t targetPan, uint32_t duration, uint32_t offset = 0,
                   uint32_t length = 0, const std::string &name = "");
  Icon icon() override { return ICON_PAN; }

  std::string description() override {
      return fmt::format("{} - target pan: {}, duration: {}", name(),
        static_cast<int>(targPan), dur);
  };

 public:
  uint8_t targPan;
  uint32_t dur;
};

//  **************
//  ReverbSeqEvent
//  **************

class ReverbSeqEvent : public SeqEvent {
 public:
  ReverbSeqEvent(SeqTrack *pTrack, uint8_t reverb, uint32_t offset = 0, uint32_t length = 0,
                 const std::string &name = "");
  EventType eventType() override { return EVENTTYPE_REVERB; }

  std::string description() override {
    return fmt::format("{} - reverb: {}", name(), static_cast<int>(reverb));
  };

  uint8_t reverb;
};

//  *****************
//  PitchBendSeqEvent
//  *****************

class PitchBendSeqEvent : public SeqEvent {
 public:
  PitchBendSeqEvent(SeqTrack *pTrack, short thePitchBend, uint32_t offset = 0,
                    uint32_t length = 0, const std::string &name = "");
  EventType eventType() override { return EVENTTYPE_PITCHBEND; }
  Icon icon() override { return ICON_PITCHBEND; }

  std::string description() override {
    return fmt::format("{} - pitch bend: {}", name(), static_cast<int>(pitchbend));
  };

  short pitchbend;
};

//  **********************
//  PitchBendRangeSeqEvent
//  **********************

class PitchBendRangeSeqEvent : public SeqEvent {
 public:
  PitchBendRangeSeqEvent(SeqTrack *pTrack, uint16_t cents, uint32_t offset = 0,
                         uint32_t length = 0, const std::string &name = "");
  EventType eventType() override { return EVENTTYPE_PITCHBENDRANGE; }

  std::string description() override {
    return fmt::format("{} - pitch bend range: {:d} cents ", name(), m_cents);
  };

private:
  uint16_t m_cents;
};

//  ******************
//  FineTuningSeqEvent
//  ******************

class FineTuningSeqEvent : public SeqEvent {
 public:
  FineTuningSeqEvent(SeqTrack *pTrack, double cents, uint32_t offset = 0, uint32_t length = 0,
                     const std::string &name = "");
  EventType eventType() override { return EVENTTYPE_PITCHBENDRANGE; }
  std::string description() override {
    return fmt::format("{} - fine tuning: {}", name(), m_cents);
  };

private:
  double m_cents;
};

//  ****************************
//  ModulationDepthRangeSeqEvent
//  ****************************

class ModulationDepthRangeSeqEvent : public SeqEvent {
 public:
  ModulationDepthRangeSeqEvent(SeqTrack *pTrack, double semitones, uint32_t offset = 0,
                               uint32_t length = 0, const std::string &name = "");
  EventType eventType() override { return EVENTTYPE_PITCHBENDRANGE; }
  std::string description() override {
    return fmt::format("{} - modulation depth range: {} cents", name(), m_semitones * 100.0);
  };

private:
  double m_semitones;
};

//  *****************
//  TransposeSeqEvent
//  *****************

class TransposeSeqEvent : public SeqEvent {
 public:
  TransposeSeqEvent(SeqTrack *pTrack, int theTranspose, uint32_t offset = 0, uint32_t length = 0,
                    const std::string &name = "");
  EventType eventType() override { return EVENTTYPE_TRANSPOSE; }

  std::string description() override {
      return fmt::format("{} - transpose: {}", name(), m_transpose);
  };

private:
  int m_transpose;
};

//  ******************
//  ModulationSeqEvent
//  ******************

class ModulationSeqEvent : public SeqEvent {
 public:
  ModulationSeqEvent(SeqTrack *pTrack, uint8_t theDepth, uint32_t offset = 0, uint32_t length = 0,
                     const std::string &name = "");
  EventType eventType() override { return EVENTTYPE_MODULATION; }

  std::string description() override {
      return fmt::format("{} - depth: {}", name(), static_cast<int>(depth));
  };

  uint8_t depth;
};

//  **************
//  BreathSeqEvent
//  **************

class BreathSeqEvent : public SeqEvent {
 public:
  BreathSeqEvent(SeqTrack *pTrack, uint8_t theDepth, uint32_t offset = 0, uint32_t length = 0,
                 const std::string &name = "");
  EventType eventType() override { return EVENTTYPE_BREATH; }

  std::string description() override {
    return fmt::format("{} - breath: {}", name(), static_cast<int>(depth));
  };

  uint8_t depth;
};

//  ****************
//  SustainSeqEvent
//  ****************

class SustainSeqEvent : public SeqEvent {
 public:
  SustainSeqEvent(SeqTrack *pTrack, uint8_t theDepth, uint32_t offset = 0, uint32_t length = 0,
                  const std::string &name = "");
  EventType eventType() override { return EVENTTYPE_SUSTAIN; }

  std::string description() override {
    return fmt::format("{} - sustain pedal: {}", name(), static_cast<int>(depth));
  };

  uint8_t depth;
};

//  ******************
//  PortamentoSeqEvent
//  ******************

class PortamentoSeqEvent : public SeqEvent {
 public:
  PortamentoSeqEvent(SeqTrack *pTrack, bool bPortamento, uint32_t offset = 0, uint32_t length = 0,
                     const std::string &name = "");
  EventType eventType() override { return EVENTTYPE_PORTAMENTO; }

  std::string description() override {
    return fmt::format("{} - portamento: {}", name(), bOn ? "on" : "off");
  };

 public:
  bool bOn;
};

//  **********************
//  PortamentoTimeSeqEvent
//  **********************

class PortamentoTimeSeqEvent : public SeqEvent {
 public:
  PortamentoTimeSeqEvent(SeqTrack *pTrack, uint8_t time, uint32_t offset = 0, uint32_t length = 0,
                         const std::string &name = "");
  EventType eventType() override { return EVENTTYPE_PORTAMENTOTIME; }

  std::string description() override {
    return fmt::format("{} - portamento time: {}", name(), static_cast<int>(time));
  };

 public:
  uint8_t time;
};

//  ******************
//  ProgChangeSeqEvent
//  ******************

class ProgChangeSeqEvent : public SeqEvent {
 public:
  ProgChangeSeqEvent(SeqTrack *pTrack, uint32_t programNumber, uint32_t offset = 0,
                     uint32_t length = 0, const std::string &name = "");
  EventType eventType() override { return EVENTTYPE_PROGCHANGE; }
  Icon icon() override { return ICON_PROGCHANGE; }

  std::string description() override {
    return fmt::format("{} - program number: {}", name(), progNum);
  };

 public:
  uint32_t progNum;
};

//  ******************
//  BankSelectSeqEvent
//  ******************

class BankSelectSeqEvent : public SeqEvent {
public:
  BankSelectSeqEvent(SeqTrack *pTrack, uint32_t bank, uint32_t offset = 0,
                     uint32_t length = 0, const std::string &name = "");
  EventType eventType() override { return EVENTTYPE_BANKSELECT; }
  Icon icon() override { return ICON_PROGCHANGE; }

  std::string description() override {
    return fmt::format("{} - bank: {}", name(), bank);
  };

public:
  uint32_t bank;
};

//  *************
//  TempoSeqEvent
//  *************

class TempoSeqEvent : public SeqEvent {
 public:
  TempoSeqEvent(SeqTrack *pTrack, double beatsperminute, uint32_t offset = 0, uint32_t length = 0,
              const std::string &name = "");
  EventType eventType() override { return EVENTTYPE_TEMPO; }
  Icon icon() override { return ICON_TEMPO; }

  std::string description() override { return fmt::format("{} - bpm: {}", name(), bpm); };

 public:
  double bpm;
};

//  ******************
//  TempoSlideSeqEvent
//  ******************

class TempoSlideSeqEvent : public SeqEvent {
 public:
  TempoSlideSeqEvent(SeqTrack *pTrack, double targBPM, uint32_t duration, uint32_t offset = 0,
                     uint32_t length = 0, const std::string &name = "");
  Icon icon() override { return ICON_TEMPO; }

  std::string description() override {
    return fmt::format("{} - target bpm: {}, duration: {}", name(), targbpm, dur);
  };

 public:
  double targbpm;
  uint32_t dur;
};

//  ***************
//  TimeSigSeqEvent
//  ***************

class TimeSigSeqEvent : public SeqEvent {
 public:
  TimeSigSeqEvent(SeqTrack *pTrack, uint8_t numerator, uint8_t denominator,
                  uint8_t theTicksPerQuarter, uint32_t offset = 0, uint32_t length = 0,
                const std::string &name = "");
  EventType eventType() override { return EVENTTYPE_TIMESIG; }

  std::string description() override {
    return fmt::format("{} - time signature: {}/{}, ticks per quarter: {}", name(),
      static_cast<int>(numer), static_cast<int>(denom), static_cast<int>(ticksPerQuarter));
  };

 public:
  uint8_t numer;
  uint8_t denom;
  uint8_t ticksPerQuarter;
};

//  **************
//  MarkerSeqEvent
//  **************

class MarkerSeqEvent : public SeqEvent {
 public:
  MarkerSeqEvent(SeqTrack *pTrack,
                 const std::string &markername,
                 uint8_t databyte1,
                 uint8_t databyte2,
                 uint32_t offset = 0,
                 uint32_t length = 0,
                 const std::string &name = "",
                 EventColor color = CLR_MARKER)
      : SeqEvent(pTrack, offset, length, name, color), databyte1(databyte1), databyte2(databyte2),
        markerName(markername) {}
  EventType eventType() override { return EVENTTYPE_MARKER; }

 public:
  uint8_t databyte1;
  uint8_t databyte2;
  std::string markerName;
};

//  ***************
//  VibratoSeqEvent
//  ***************

//class VibratoSeqEvent :
//	public SeqEvent
//{
//public:
//	VibratoSeqEvent(SeqTrack* pTrack, uint8_t detph, uint32_t offset = 0, uint32_t length = 0, const std::string& name = "")
//	: SeqEvent(pTrack, offset, length, name), depth(depth)
//	{}
//	EventType GetEventType() override { return EVENTTYPE_VIBRATO; }
//
//public:
//	uint8_t depth;
//};

//  ****************
//  TrackEndSeqEvent
//  ****************

class TrackEndSeqEvent : public SeqEvent {
 public:
  explicit TrackEndSeqEvent(SeqTrack *pTrack, uint32_t offset = 0, uint32_t length = 0,
                   const std::string &name = "")
      : SeqEvent(pTrack, offset, length, name, CLR_TRACKEND) {}
  EventType eventType() override { return EVENTTYPE_TRACKEND; }
  Icon icon() override { return ICON_TRACKEND; }
};

//  *******************
//  LoopForeverSeqEvent
//  *******************

class LoopForeverSeqEvent : public SeqEvent {
 public:
  explicit LoopForeverSeqEvent(SeqTrack *pTrack, uint32_t offset = 0, uint32_t length = 0,
                      const std::string &name = "", const std::string &descr = "")
      : SeqEvent(pTrack, offset, length, name, CLR_LOOPFOREVER) {}
  EventType eventType() override { return EVENTTYPE_LOOPFOREVER; }
  Icon icon() override { return ICON_LOOP_FOREVER; }
};
