/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "base/Types.h"
#include "VGMItem.h"
#include "MidiFile.h"
#include <spdlog/fmt/fmt.h>

class SeqTrack;

class SeqEvent:
    public VGMItem {
 public:
  explicit SeqEvent(SeqTrack *pTrack,
                    u32 offset = 0,
                    u32 length = 0,
                    const std::string &name = "",
                    Type type = Type::Unknown,
                    const std::string &desc = "");
  ~SeqEvent() override = default;
  std::string description() override {
    return m_description;
  }

 public:
  u8 channel;
  SeqTrack *parentTrack;
 private:
  std::string m_description;
};

//  ***************
//  DurNoteSeqEvent
//  ***************

class DurNoteSeqEvent:
    public SeqEvent {
 public:
  DurNoteSeqEvent(SeqTrack *pTrack,
                  u8 absoluteKey,
                  u8 velocity,
                  u32 duration,
                  u32 offset = 0,
                  u32 length = 0,
                  const std::string &name = "");

  std::string description() override {
    return fmt::format("{} - abs key: {} ({}), velocity: {}, duration: {}", name(),
      static_cast<int>(absKey), MidiEvent::getNoteName(absKey), static_cast<int>(vel), dur);
  };

 public:
  u8 absKey;
  u8 vel;
  u32 dur;
};

//  **************
//  NoteOnSeqEvent
//  ***************

class NoteOnSeqEvent : public SeqEvent {
 public:
  NoteOnSeqEvent(SeqTrack *pTrack,
                 u8 absoluteKey,
                 u8 velocity,
                 u32 offset = 0,
                 u32 length = 0,
                 const std::string &name = "");

  std::string description() override {
    return fmt::format("{} - abs key: {} ({}), velocity: {}", name(),
      static_cast<int>(absKey), MidiEvent::getNoteName(absKey), static_cast<int>(vel));
  };

 public:
  u8 absKey;
  u8 vel;
};

//  ***************
//  NoteOffSeqEvent
//  ***************

class NoteOffSeqEvent:
    public SeqEvent {
 public:
  NoteOffSeqEvent
      (SeqTrack *pTrack, u8 absoluteKey, u32 offset = 0, u32 length = 0, const std::string &name = "");

  std::string description() override {
    return fmt::format("{} - abs key: {} ({})", name(), static_cast<int>(absKey),
      MidiEvent::getNoteName(absKey));
  };

 public:
  u8 absKey;
};

//  ************
//  RestSeqEvent
//  ************

class RestSeqEvent : public SeqEvent {
 public:
  RestSeqEvent(SeqTrack *pTrack, u32 duration, u32 offset = 0, u32 length = 0,
               const std::string &name = "");

  std::string description() override { return fmt::format("{} - duration: {}", name(), dur); };

 public:
  u32 dur;
};

//  *****************
//  SetOctaveSeqEvent
//  *****************

class SetOctaveSeqEvent : public SeqEvent {
 public:
  SetOctaveSeqEvent(SeqTrack *pTrack, u8 octave, u32 offset = 0, u32 length = 0,
                    const std::string &name = "");

  std::string description() override {
    return fmt::format("{} - octave: {}", name(), static_cast<int>(octave));
  };

  u8 octave;
};

//  ***********
//  VolSeqEvent
//  ***********

class VolSeqEvent : public SeqEvent {
 public:
  VolSeqEvent(SeqTrack *pTrack, u8 volume, u32 offset = 0, u32 length = 0,
              const std::string &name = "");
  VolSeqEvent(SeqTrack *pTrack, double volume, u32 offset = 0, u32 length = 0,
              const std::string &name = "");

  std::string description() override {
    if (percentVol > 0) {
      return fmt::format("{} - volume: {:.1f}", name(), percentVol);
    }
    return fmt::format("{} - volume: {:d}", name(), vol);
  };

  u8 vol = -1;
  double percentVol = -1;
};

//  *******************
//  Volume14BitSeqEvent
//  *******************

class Volume14BitSeqEvent : public SeqEvent {
public:
  Volume14BitSeqEvent(SeqTrack *pTrack, u16 volume, u32 offset = 0, u32 length = 0,
                      const std::string &name = "");

  std::string description() override {
    return fmt::format("{} - volume: {:d}", name(), m_volume);
  };

  u16 m_volume;
};

//  ****************
//  VolSlideSeqEvent
//  ****************

class VolSlideSeqEvent : public SeqEvent {
 public:
  VolSlideSeqEvent(SeqTrack *pTrack, u8 targetVolume, u32 duration, u32 offset = 0,
                   u32 length = 0, const std::string &name = "");

  std::string description() override {
    return fmt::format("{} - target volume: {}, duration: {}", name(),
      static_cast<int>(targVol), dur);
  };

  u8 targVol;
  u32 dur;
};

//  ***********
//  MastVolSeqEvent
//  ***********

class MastVolSeqEvent : public SeqEvent {
 public:
    MastVolSeqEvent(SeqTrack *pTrack, u8 volume, u32 offset = 0, u32 length = 0,
                    const std::string &name = "");
    MastVolSeqEvent(SeqTrack *pTrack, double volume, u32 offset = 0, u32 length = 0,
                    const std::string &name = "");

    std::string description() override {
      if (percentVol > 0) {
        return fmt::format("{} - master volume: {:.1f}", name(), percentVol);
      }
      return fmt::format("{} - master volume: {}", name(), vol);
    };

  u8 vol = -1;
  double percentVol = -1;
};

//  ****************
//  MastVolSlideSeqEvent
//  ****************

class MastVolSlideSeqEvent : public SeqEvent {
 public:
    MastVolSlideSeqEvent(SeqTrack *pTrack, u8 targetVolume, u32 duration,
                         u32 offset = 0, u32 length = 0, const std::string &name = "");

    std::string description() override {
        return fmt::format("{} - target volume: {}, duration: {}", name(),
          static_cast<int>(targVol), dur);
    };

  u8 targVol;
  u32 dur;
};

//  ******************
//  ExpressionSeqEvent
//  ******************

class ExpressionSeqEvent : public SeqEvent {
 public:
  ExpressionSeqEvent(SeqTrack *pTrack, u8 level, u32 offset = 0, u32 length = 0, const std::string &name = "");
  ExpressionSeqEvent(SeqTrack *pTrack, double level, u32 offset = 0, u32 length = 0, const std::string &name = "");

  std::string description() override {
    if (percentLevel > 0) {
      return fmt::format("{} - expression: {:.1f}", name(), percentLevel);
    }
    return fmt::format("{} - expression: {:d}", name(), level);
  };

 public:
  u8 level = -1;
  double percentLevel = -1;
};

//  ***********************
//  ExpressionSlideSeqEvent
//  ***********************

class ExpressionSlideSeqEvent : public SeqEvent {
 public:
    ExpressionSlideSeqEvent(SeqTrack *pTrack, u8 targetExpression, u32 duration,
                            u32 offset = 0, u32 length = 0, const std::string &name = "");

  std::string description() override {
    return fmt::format("{} - target expression: {}, duration: {}", name(),
      static_cast<int>(targExpr), dur);
  };

  u8 targExpr;
  u32 dur;
};

//  ***********
//  PanSeqEvent
//  ***********

class PanSeqEvent : public SeqEvent {
 public:
  PanSeqEvent(SeqTrack *pTrack, u8 pan, u32 offset = 0, u32 length = 0,
              const std::string &name = "");

  std::string description() override {
    return fmt::format("{} - pan: {}", name(), static_cast<int>(pan));
  };

 public:
  u8 pan;
};

//  ****************
//  PanSlideSeqEvent
//  ****************

class PanSlideSeqEvent : public SeqEvent {
 public:
  PanSlideSeqEvent(SeqTrack *pTrack, u8 targetPan, u32 duration, u32 offset = 0,
                   u32 length = 0, const std::string &name = "");

  std::string description() override {
      return fmt::format("{} - target pan: {}, duration: {}", name(),
        static_cast<int>(targPan), dur);
  };

 public:
  u8 targPan;
  u32 dur;
};

//  **************
//  ReverbSeqEvent
//  **************

class ReverbSeqEvent : public SeqEvent {
 public:
  ReverbSeqEvent(SeqTrack *pTrack, u8 reverb, u32 offset = 0, u32 length = 0,
                 const std::string &name = "");

  std::string description() override {
    return fmt::format("{} - reverb: {}", name(), static_cast<int>(reverb));
  };

  u8 reverb;
};

//  *****************
//  PitchBendSeqEvent
//  *****************

class PitchBendSeqEvent : public SeqEvent {
 public:
  PitchBendSeqEvent(SeqTrack *pTrack, short thePitchBend, u32 offset = 0,
                    u32 length = 0, const std::string &name = "");

  std::string description() override {
    return fmt::format("{} - pitch bend: {}", name(), static_cast<int>(pitchbend));
  };

  short pitchbend;
};

//  ***********************
//  ChannelPressureSeqEvent
//  ***********************

class ChannelPressureSeqEvent : public SeqEvent {
 public:
  ChannelPressureSeqEvent(SeqTrack *pTrack, u8 pressure, u32 offset = 0,
                          u32 length = 0, const std::string &name = "");

  std::string description() override {
    return fmt::format("{} - channel pressure: {}", name(), static_cast<int>(pressure));
  };

  u8 pressure;
};

//  **********************
//  PitchBendRangeSeqEvent
//  **********************

class PitchBendRangeSeqEvent : public SeqEvent {
 public:
  PitchBendRangeSeqEvent(SeqTrack *pTrack, u16 cents, u32 offset = 0,
                         u32 length = 0, const std::string &name = "");

  std::string description() override {
    return fmt::format("{} - pitch bend range: {:d} cents ", name(), m_cents);
  };

private:
  u16 m_cents;
};

//  ******************
//  FineTuningSeqEvent
//  ******************

class FineTuningSeqEvent : public SeqEvent {
 public:
  FineTuningSeqEvent(SeqTrack *pTrack, double cents, u32 offset = 0, u32 length = 0,
                     const std::string &name = "");

  std::string description() override {
    return fmt::format("{} - fine tuning: {}", name(), m_cents);
  };

private:
  double m_cents;
};

//  ********************
//  CoarseTuningSeqEvent
//  ********************

class CoarseTuningSeqEvent : public SeqEvent {
public:
  CoarseTuningSeqEvent(SeqTrack *pTrack, double semitones, u32 offset = 0, u32 length = 0,
                       const std::string &name = "");

  std::string description() override {
    return fmt::format("{} - coarse tuning: {}", name(), m_semitones);
  };

private:
  double m_semitones;
};

//  ****************************
//  ModulationDepthRangeSeqEvent
//  ****************************

class ModulationDepthRangeSeqEvent : public SeqEvent {
 public:
  ModulationDepthRangeSeqEvent(SeqTrack *pTrack, double semitones, u32 offset = 0,
                               u32 length = 0, const std::string &name = "");

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
  TransposeSeqEvent(SeqTrack *pTrack, int theTranspose, u32 offset = 0, u32 length = 0,
                    const std::string &name = "");

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
  ModulationSeqEvent(SeqTrack *pTrack, u8 theDepth, u32 offset = 0, u32 length = 0,
                     const std::string &name = "");

  std::string description() override {
      return fmt::format("{} - depth: {}", name(), static_cast<int>(depth));
  };

  u8 depth;
};

//  **************
//  BreathSeqEvent
//  **************

class BreathSeqEvent : public SeqEvent {
 public:
  BreathSeqEvent(SeqTrack *pTrack, u8 theDepth, u32 offset = 0, u32 length = 0,
                 const std::string &name = "");

  std::string description() override {
    return fmt::format("{} - breath: {}", name(), static_cast<int>(depth));
  };

  u8 depth;
};

//  ****************
//  SustainSeqEvent
//  ****************

class SustainSeqEvent : public SeqEvent {
 public:
  SustainSeqEvent(SeqTrack *pTrack, u8 theDepth, u32 offset = 0, u32 length = 0,
                  const std::string &name = "");

  std::string description() override {
    return fmt::format("{} - sustain pedal: {}", name(), static_cast<int>(depth));
  };

  u8 depth;
};

//  ******************
//  PortamentoSeqEvent
//  ******************

class PortamentoSeqEvent : public SeqEvent {
 public:
  PortamentoSeqEvent(SeqTrack *pTrack, bool bPortamento, u32 offset = 0, u32 length = 0,
                     const std::string &name = "");

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
  PortamentoTimeSeqEvent(SeqTrack *pTrack, u8 time, u32 offset = 0, u32 length = 0,
                         const std::string &name = "");

  std::string description() override {
    return fmt::format("{} - portamento time: {}", name(), static_cast<int>(time));
  };

 public:
  u8 time;
};

//  ******************
//  ProgChangeSeqEvent
//  ******************

class ProgChangeSeqEvent : public SeqEvent {
 public:
  ProgChangeSeqEvent(SeqTrack *pTrack, u32 programNumber, u32 offset = 0,
                     u32 length = 0, const std::string &name = "");

  std::string description() override {
    return fmt::format("{} - program number: {}", name(), progNum);
  };

 public:
  u32 progNum;
};

//  ******************
//  BankSelectSeqEvent
//  ******************

class BankSelectSeqEvent : public SeqEvent {
public:
  BankSelectSeqEvent(SeqTrack *pTrack, u32 bank, u32 offset = 0,
                     u32 length = 0, const std::string &name = "");

  std::string description() override {
    return fmt::format("{} - bank: {}", name(), bank);
  };

public:
  u32 bank;
};

//  *************
//  TempoSeqEvent
//  *************

class TempoSeqEvent : public SeqEvent {
 public:
  TempoSeqEvent(SeqTrack *pTrack, double beatsperminute, u32 offset = 0, u32 length = 0,
              const std::string &name = "");

  std::string description() override { return fmt::format("{} - bpm: {}", name(), bpm); };

 public:
  double bpm;
};

//  ******************
//  TempoSlideSeqEvent
//  ******************

class TempoSlideSeqEvent : public SeqEvent {
 public:
  TempoSlideSeqEvent(SeqTrack *pTrack, double targBPM, u32 duration, u32 offset = 0,
                     u32 length = 0, const std::string &name = "");

  std::string description() override {
    return fmt::format("{} - target bpm: {}, duration: {}", name(), targbpm, dur);
  };

 public:
  double targbpm;
  u32 dur;
};

//  ***************
//  TimeSigSeqEvent
//  ***************

class TimeSigSeqEvent : public SeqEvent {
 public:
  TimeSigSeqEvent(SeqTrack *pTrack, u8 numerator, u8 denominator,
                  u8 theTicksPerQuarter, u32 offset = 0, u32 length = 0,
                const std::string &name = "");

  std::string description() override {
    return fmt::format("{} - time signature: {}/{}, ticks per quarter: {}", name(),
      static_cast<int>(numer), static_cast<int>(denom), static_cast<int>(ticksPerQuarter));
  };

 public:
  u8 numer;
  u8 denom;
  u8 ticksPerQuarter;
};

//  **************
//  MarkerSeqEvent
//  **************

class MarkerSeqEvent : public SeqEvent {
 public:
  MarkerSeqEvent(SeqTrack *pTrack,
                 const std::string &markername,
                 u8 databyte1,
                 u8 databyte2,
                 u32 offset = 0,
                 u32 length = 0,
                 const std::string &name = "",
                 Type type = Type::Marker)
      : SeqEvent(pTrack, offset, length, name, type), databyte1(databyte1), databyte2(databyte2),
        markerName(markername) {}

 public:
  u8 databyte1;
  u8 databyte2;
  std::string markerName;
};

//  ***************
//  VibratoSeqEvent
//  ***************

//class VibratoSeqEvent :
//	public SeqEvent
//{
//public:
//	VibratoSeqEvent(SeqTrack* pTrack, u8 detph, u32 offset = 0, u32 length = 0, const std::string& name = "")
//	: SeqEvent(pTrack, offset, length, name), depth(depth)
//	{}
//	EventType GetEventType() override { return EVENTTYPE_VIBRATO; }
//
//public:
//	u8 depth;
//};

//  ****************
//  TrackEndSeqEvent
//  ****************

class TrackEndSeqEvent : public SeqEvent {
 public:
  explicit TrackEndSeqEvent(SeqTrack *pTrack, u32 offset = 0, u32 length = 0,
                   const std::string &name = "")
      : SeqEvent(pTrack, offset, length, name, Type::TrackEnd) {}
};

//  *******************
//  LoopForeverSeqEvent
//  *******************

class LoopForeverSeqEvent : public SeqEvent {
 public:
  explicit LoopForeverSeqEvent(SeqTrack *pTrack, u32 offset = 0, u32 length = 0,
                      const std::string &name = "", const std::string &descr = "")
      : SeqEvent(pTrack, offset, length, name, Type::LoopForever) {}
};

//  ************
//  JumpSeqEvent
//  ************

class JumpSeqEvent : public SeqEvent {
public:
  JumpSeqEvent(SeqTrack *pTrack,
               u32 destination,
               u32 offset = 0,
               u32 length = 0,
               const std::string &name = "")
    : SeqEvent(pTrack, offset, length, name, Type::Jump), m_destination(destination) {}


  std::string description() override {
    return fmt::format("{} - destination: 0x{:X}", name(), m_destination);
  }

private:
  u32 m_destination;
};

//  ************
//  CallSeqEvent
//  ************

class CallSeqEvent : public SeqEvent {
public:
  CallSeqEvent(SeqTrack *pTrack,
               u32 destination,
               u32 returnOffset,
               u32 offset = 0,
               u32 length = 0,
               const std::string &name = "")
  : SeqEvent(pTrack, offset, length, name, Type::Misc),
    m_destination(destination),
    m_returnOffset(returnOffset) {}

  std::string description() override {
    return fmt::format("{} - destination: 0x{:X}, return: 0x{:X}", name(), m_destination, m_returnOffset);
  }

private:
  u32 m_destination;
  u32 m_returnOffset;
};

//  **************
//  ReturnSeqEvent
//  **************

class ReturnSeqEvent : public SeqEvent {
public:
  ReturnSeqEvent(SeqTrack *pTrack,
                 u32 destination,
                 bool hasDestination,
                 u32 offset = 0,
                 u32 length = 0,
                 const std::string &name = "")
  : SeqEvent(pTrack, offset, length, name, Type::Misc),
    m_destination(destination),
    m_hasDestination(hasDestination) {}

  std::string description() override {
    if (!m_hasDestination) {
      return fmt::format("{} - no return address", name());
    }
    return fmt::format("{} - destination: 0x{:X}", name(), m_destination);
  }

private:
  u32 m_destination;
  bool m_hasDestination;
};
