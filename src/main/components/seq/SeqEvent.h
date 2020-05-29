/*
 * VGMCis (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once
#include "VGMItem.h"
#include "MidiFile.h"
#include <fmt/format.h>

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
    EVENTTYPE_CISPOSE,
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

class SeqEvent : public VGMItem {
   public:
    SeqEvent(SeqTrack *pTrack, uint32_t offset = 0, uint32_t length = 0,
             const std::string &name = "", uint8_t color = 0, Icon icon = ICON_BINARY,
             const std::string &desc = "");
    ~SeqEvent(void) override = default;
    virtual std::string GetDescription() {
        return desc.empty() ? std::string(name) : (std::string(name) + " - " + desc);
    }

    virtual EventType GetEventType() { return EVENTTYPE_UNDEFINED; }
    virtual Icon GetIcon() { return icon; }

   public:
    uint8_t channel;
    SeqTrack *parentTrack;

   private:
    Icon icon;
    std::string desc;
};

//  ***************
//  DurNoteSeqEvent
//  ***************

class DurNoteSeqEvent : public SeqEvent {
   public:
    DurNoteSeqEvent(SeqTrack *pTrack, uint8_t absoluteKey, uint8_t velocity, uint32_t duration,
                    uint32_t offset = 0, uint32_t length = 0, const std::string &name = "");
    virtual ~DurNoteSeqEvent(void) {}
    virtual EventType GetEventType() { return EVENTTYPE_DURNOTE; }
    virtual Icon GetIcon() { return ICON_NOTE; }

    std::string GetDescription() override {
        return fmt::format("{} - abs key: {} ({}), velocity: {}, duration: {}", name,
                           static_cast<int>(absKey), MidiEvent::GetNoteName(absKey),
                           static_cast<int>(vel), dur);
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
    NoteOnSeqEvent(SeqTrack *pTrack, uint8_t absoluteKey, uint8_t velocity, uint32_t offset = 0,
                   uint32_t length = 0, const std::string &name = "");
    virtual ~NoteOnSeqEvent(void) {}
    virtual EventType GetEventType() { return EVENTTYPE_NOTEON; }
    virtual Icon GetIcon() { return ICON_NOTE; }

    std::string GetDescription() override {
        return fmt::format("{} - abs key: {} ({}), velocity: {}", name, static_cast<int>(absKey),
                           MidiEvent::GetNoteName(absKey), static_cast<int>(vel));
    };

   public:
    uint8_t absKey;
    uint8_t vel;
};

//  ***************
//  NoteOffSeqEvent
//  ***************

class NoteOffSeqEvent : public SeqEvent {
   public:
    NoteOffSeqEvent(SeqTrack *pTrack, uint8_t absoluteKey, uint32_t offset = 0, uint32_t length = 0,
                    const std::string &name = "");
    virtual ~NoteOffSeqEvent(void) {}
    virtual EventType GetEventType() { return EVENTTYPE_NOTEOFF; }
    virtual Icon GetIcon() { return ICON_NOTE; }

    std::string GetDescription() override {
        return fmt::format("{} - abs key: {} ({})", name, static_cast<int>(absKey),
                           MidiEvent::GetNoteName(absKey));
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
    virtual ~RestSeqEvent(void) {}
    virtual EventType GetEventType() { return EVENTTYPE_REST; }
    virtual Icon GetIcon() { return ICON_REST; }

    std::string GetDescription() override { return fmt::format("{} - duration: {}", name, dur); };

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
    virtual ~SetOctaveSeqEvent(void) {}

    std::string GetDescription() override {
        return fmt::format("{} - octave: {}", name, static_cast<int>(octave));
    };

   public:
    uint8_t octave;
};

//  ***********
//  VolSeqEvent
//  ***********

class VolSeqEvent : public SeqEvent {
   public:
    VolSeqEvent(SeqTrack *pTrack, uint8_t volume, uint32_t offset = 0, uint32_t length = 0,
                const std::string &name = "");
    virtual ~VolSeqEvent(void) {}
    virtual EventType GetEventType() { return EVENTTYPE_VOLUME; }

    std::string GetDescription() override {
        return fmt::format("{} - volume: {}", name, static_cast<int>(vol));
    };

   public:
    uint8_t vol;
};

//  ****************
//  VolSlideSeqEvent
//  ****************

class VolSlideSeqEvent : public SeqEvent {
   public:
    VolSlideSeqEvent(SeqTrack *pTrack, uint8_t targetVolume, uint32_t duration, uint32_t offset = 0,
                     uint32_t length = 0, const std::string &name = "");
    virtual ~VolSlideSeqEvent(void) {}
    virtual EventType GetEventType() { return EVENTTYPE_VOLUMESLIDE; }

    std::string GetDescription() override {
        return fmt::format("{} - target volume: {}, duration: {}", name, static_cast<int>(targVol),
                           dur);
    };

   public:
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
    virtual ~MastVolSeqEvent(void) {}

    std::string GetDescription() override {
        return fmt::format("{} - master volume: {}", name, static_cast<int>(vol));
    };

   public:
    uint8_t vol;
};

//  ****************
//  MastVolSlideSeqEvent
//  ****************

class MastVolSlideSeqEvent : public SeqEvent {
   public:
    MastVolSlideSeqEvent(SeqTrack *pTrack, uint8_t targetVolume, uint32_t duration,
                         uint32_t offset = 0, uint32_t length = 0, const std::string &name = "");
    virtual ~MastVolSlideSeqEvent(void) {}

    std::string GetDescription() override {
        return fmt::format("{} - target volume: {}, duration: {}", name, static_cast<int>(targVol),
                           dur);
    };

   public:
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
    virtual ~ExpressionSeqEvent(void) {}
    virtual EventType GetEventType() { return EVENTTYPE_EXPRESSION; }

    std::string GetDescription() override {
        return fmt::format("{} - expression: {}", name, static_cast<int>(level));
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
    virtual ~ExpressionSlideSeqEvent(void) {}
    virtual EventType GetEventType() { return EVENTTYPE_EXPRESSIONSLIDE; }

    std::string GetDescription() override {
        return fmt::format("{} - target expression: {}, duration: {}", name,
                           static_cast<int>(targExpr), dur);
    };

   public:
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
    virtual ~PanSeqEvent(void) {}
    virtual EventType GetEventType() { return EVENTTYPE_PAN; }

    std::string GetDescription() override {
        return fmt::format("{} - pan: {}", name, static_cast<int>(pan));
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
    virtual ~PanSlideSeqEvent(void) {}

    std::string GetDescription() override {
        return fmt::format("{} - target pan: {}, duration: {}", name, static_cast<int>(targPan),
                           dur);
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
    virtual ~ReverbSeqEvent(void) {}
    virtual EventType GetEventType() { return EVENTTYPE_REVERB; }

    std::string GetDescription() override {
        return fmt::format("{} - reverb: {}", name, static_cast<int>(reverb));
    };

   public:
    uint8_t reverb;
};

//  *****************
//  PitchBendSeqEvent
//  *****************

class PitchBendSeqEvent : public SeqEvent {
   public:
    PitchBendSeqEvent(SeqTrack *pTrack, short thePitchBend, uint32_t offset = 0,
                      uint32_t length = 0, const std::string &name = "");
    virtual EventType GetEventType() { return EVENTTYPE_PITCHBEND; }

    std::string GetDescription() override {
        return fmt::format("{} - pitch bend: {}", name, static_cast<int>(pitchbend));
    };

   public:
    short pitchbend;
};

//  **********************
//  PitchBendRangeSeqEvent
//  **********************

class PitchBendRangeSeqEvent : public SeqEvent {
   public:
    PitchBendRangeSeqEvent(SeqTrack *pTrack, uint8_t semiTones, uint8_t cents, uint32_t offset = 0,
                           uint32_t length = 0, const std::string &name = "");
    virtual EventType GetEventType() { return EVENTTYPE_PITCHBENDRANGE; }

    std::string GetDescription() override {
        return fmt::format("{} - pitch bend range: {} semitones {} cents ", name,
                           static_cast<int>(semitones), static_cast<int>(cents));
    };

   public:
    uint8_t semitones;
    uint8_t cents;
};

//  ******************
//  FineTuningSeqEvent
//  ******************

class FineTuningSeqEvent : public SeqEvent {
   public:
    FineTuningSeqEvent(SeqTrack *pTrack, double cents, uint32_t offset = 0, uint32_t length = 0,
                       const std::string &name = "");
    virtual EventType GetEventType() { return EVENTTYPE_PITCHBENDRANGE; }
    std::string GetDescription() override {
        return fmt::format("{} - fine tuning: {}", name, cents);
    };

   public:
    double cents;
};

//  ****************************
//  ModulationDepthRangeSeqEvent
//  ****************************

class ModulationDepthRangeSeqEvent : public SeqEvent {
   public:
    ModulationDepthRangeSeqEvent(SeqTrack *pTrack, double semitones, uint32_t offset = 0,
                                 uint32_t length = 0, const std::string &name = "");
    virtual EventType GetEventType() { return EVENTTYPE_PITCHBENDRANGE; }
    std::string GetDescription() override {
        return fmt::format("{} - modulation depth range: {} cents", name, semitones * 100.0);
    };

   public:
    double semitones;
};

//  *****************
//  CisposeSeqEvent
//  *****************

class CisposeSeqEvent : public SeqEvent {
   public:
    CisposeSeqEvent(SeqTrack *pTrack, int theCispose, uint32_t offset = 0, uint32_t length = 0,
                      const std::string &name = "");
    virtual EventType GetEventType() { return EVENTTYPE_CISPOSE; }

    std::string GetDescription() override {
        return fmt::format("{} - cispose: {}", name, cispose);
    };

   public:
    int cispose;
};

//  ******************
//  ModulationSeqEvent
//  ******************

class ModulationSeqEvent : public SeqEvent {
   public:
    ModulationSeqEvent(SeqTrack *pTrack, uint8_t theDepth, uint32_t offset = 0, uint32_t length = 0,
                       const std::string &name = "");
    virtual ~ModulationSeqEvent(void) {}
    virtual EventType GetEventType() { return EVENTTYPE_MODULATION; }

    std::string GetDescription() override {
        return fmt::format("{} - depth: {}", name, static_cast<int>(depth));
    };

   public:
    uint8_t depth;
};

//  **************
//  BreathSeqEvent
//  **************

class BreathSeqEvent : public SeqEvent {
   public:
    BreathSeqEvent(SeqTrack *pTrack, uint8_t theDepth, uint32_t offset = 0, uint32_t length = 0,
                   const std::string &name = "");
    virtual ~BreathSeqEvent(void) {}
    virtual EventType GetEventType() { return EVENTTYPE_BREATH; }

    std::string GetDescription() override {
        return fmt::format("{} - breath: {}", name, static_cast<int>(depth));
    };

   public:
    uint8_t depth;
};

//  ****************
//  SustainSeqEvent
//  ****************

class SustainSeqEvent : public SeqEvent {
   public:
    SustainSeqEvent(SeqTrack *pTrack, uint8_t theDepth, uint32_t offset = 0, uint32_t length = 0,
                    const std::string &name = "");
    virtual ~SustainSeqEvent(void) {}
    virtual EventType GetEventType() { return EVENTTYPE_SUSTAIN; }

    std::string GetDescription() override {
        return fmt::format("{} - sustain pedal: {}", name, static_cast<int>(depth));
    };

   public:
    uint8_t depth;
};

//  ******************
//  PortamentoSeqEvent
//  ******************

class PortamentoSeqEvent : public SeqEvent {
   public:
    PortamentoSeqEvent(SeqTrack *pTrack, bool bPortamento, uint32_t offset = 0, uint32_t length = 0,
                       const std::string &name = "");
    virtual ~PortamentoSeqEvent(void) {}
    virtual EventType GetEventType() { return EVENTTYPE_PORTAMENTO; }

    std::string GetDescription() override {
        return fmt::format("{} - portamento: {}", name, bOn ? "on" : "off");
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
    virtual ~PortamentoTimeSeqEvent(void) {}
    virtual EventType GetEventType() { return EVENTTYPE_PORTAMENTOTIME; }

    std::string GetDescription() override {
        return fmt::format("{} - portamento time: {}", name, static_cast<int>(time));
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
    virtual ~ProgChangeSeqEvent(void) {}
    virtual EventType GetEventType() { return EVENTTYPE_PROGCHANGE; }

    std::string GetDescription() override {
        return fmt::format("{} - program number: {}", name, progNum);
    };

   public:
    uint32_t progNum;
};

//  *************
//  TempoSeqEvent
//  *************

class TempoSeqEvent : public SeqEvent {
   public:
    TempoSeqEvent(SeqTrack *pTrack, double beatsperminute, uint32_t offset = 0, uint32_t length = 0,
                  const std::string &name = "");
    virtual ~TempoSeqEvent(void) {}
    virtual EventType GetEventType() { return EVENTTYPE_TEMPO; }
    virtual Icon GetIcon() { return ICON_TEMPO; }

    std::string GetDescription() override { return fmt::format("{} - bpm: {}", name, bpm); };

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
    virtual ~TempoSlideSeqEvent(void) {}
    virtual Icon GetIcon() { return ICON_TEMPO; }

    std::string GetDescription() override {
        return fmt::format("{} - target bpm: {}, duration: {}", name, targbpm, dur);
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
    virtual ~TimeSigSeqEvent(void) {}
    virtual EventType GetEventType() { return EVENTTYPE_TIMESIG; }

    std::string GetDescription() override {
        return fmt::format("{} - time signature: {}/{}, ticks per quarter: {}", name,
                           static_cast<int>(numer), static_cast<int>(denom),
                           static_cast<int>(ticksPerQuarter));
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
    MarkerSeqEvent(SeqTrack *pTrack, const std::string &markername, uint8_t databyte1,
                   uint8_t databyte2, uint32_t offset = 0, uint32_t length = 0,
                   const std::string &name = "", uint8_t color = CLR_MARKER)
        : SeqEvent(pTrack, offset, length, name, color),
          databyte1(databyte1),
          databyte2(databyte2) {}
    virtual EventType GetEventType() { return EVENTTYPE_MARKER; }

   public:
    uint8_t databyte1;
    uint8_t databyte2;
};

//  ***************
//  VibratoSeqEvent
//  ***************

// class VibratoSeqEvent :
//	public SeqEvent
//{
// public:
//	VibratoSeqEvent(SeqTrack* pTrack, uint8_t detph, uint32_t offset = 0, uint32_t length = 0,
// const std::string& name = "") 	: SeqEvent(pTrack, offset, length, name), depth(depth)
//	{}
//	virtual EventType GetEventType() { return EVENTTYPE_VIBRATO; }
//
// public:
//	uint8_t depth;
//};

//  ****************
//  TrackEndSeqEvent
//  ****************

class TrackEndSeqEvent : public SeqEvent {
   public:
    TrackEndSeqEvent(SeqTrack *pTrack, uint32_t offset = 0, uint32_t length = 0,
                     const std::string &name = "")
        : SeqEvent(pTrack, offset, length, name, CLR_TRACKEND) {}
    virtual EventType GetEventType() { return EVENTTYPE_TRACKEND; }
};

//  *******************
//  LoopForeverSeqEvent
//  *******************

class LoopForeverSeqEvent : public SeqEvent {
   public:
    LoopForeverSeqEvent(SeqTrack *pTrack, uint32_t offset = 0, uint32_t length = 0,
                        const std::string &name = "")
        : SeqEvent(pTrack, offset, length, name, CLR_LOOPFOREVER) {}
    virtual EventType GetEventType() { return EVENTTYPE_LOOPFOREVER; }
};
