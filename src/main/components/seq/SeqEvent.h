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

class SeqEvent:
    public VGMItem {
 public:
  explicit SeqEvent(SeqTrack *pTrack,
                    uint32_t offset = 0,
                    uint32_t length = 0,
                    const std::string &name = "",
                    Type type = Type::Unknown,
                    const std::string &desc = "");
  ~SeqEvent() override = default;
  std::string description() override {
    return m_description;
  }

 public:
  uint8_t channel;
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
                  uint8_t absoluteKey,
                  uint8_t velocity,
                  uint32_t duration,
                  uint32_t offset = 0,
                  uint32_t length = 0,
                  const std::string &name = "");

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
  VolSeqEvent(SeqTrack *pTrack, double volume, uint32_t offset = 0, uint32_t length = 0,
              const std::string &name = "");

  std::string description() override {
    if (percentVol > 0) {
      return fmt::format("{} - volume: {:.1f}", name(), percentVol);
    }
    return fmt::format("{} - volume: {:d}", name(), vol);
  };

  uint8_t vol = -1;
  double percentVol = -1;
};

//  *******************
//  Volume14BitSeqEvent
//  *******************

class Volume14BitSeqEvent : public SeqEvent {
public:
  Volume14BitSeqEvent(SeqTrack *pTrack, uint16_t volume, uint32_t offset = 0, uint32_t length = 0,
                      const std::string &name = "");

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
    ExpressionSlideSeqEvent(SeqTrack *pTrack, uint8_t targetExpression, uint32_t duration,
                            uint32_t offset = 0, uint32_t length = 0, const std::string &name = "");

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
  CoarseTuningSeqEvent(SeqTrack *pTrack, double semitones, uint32_t offset = 0, uint32_t length = 0,
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
  ModulationDepthRangeSeqEvent(SeqTrack *pTrack, double semitones, uint32_t offset = 0,
                               uint32_t length = 0, const std::string &name = "");

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
  enum class Scope : uint8_t {
    Track,
    Global,
  };

  TransposeSeqEvent(SeqTrack *pTrack,
                    int theTranspose,
                    uint32_t offset = 0,
                    uint32_t length = 0,
                    const std::string &name = "",
                    Scope scope = Scope::Track);

  std::string description() override {
      return fmt::format("{} - transpose: {}", name(), m_transpose);
  };

  [[nodiscard]] int transpose() const noexcept { return m_transpose; }
  [[nodiscard]] Scope scope() const noexcept { return m_scope; }

private:
  int m_transpose;
  Scope m_scope;
};

//  ******************
//  ModulationSeqEvent
//  ******************

class ModulationSeqEvent : public SeqEvent {
 public:
  ModulationSeqEvent(SeqTrack *pTrack, uint8_t theDepth, uint32_t offset = 0, uint32_t length = 0,
                     const std::string &name = "");

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
                 Type type = Type::Marker)
      : SeqEvent(pTrack, offset, length, name, type), databyte1(databyte1), databyte2(databyte2),
        markerName(markername) {}

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
      : SeqEvent(pTrack, offset, length, name, Type::TrackEnd) {}
};

//  *******************
//  LoopForeverSeqEvent
//  *******************

class LoopForeverSeqEvent : public SeqEvent {
 public:
  explicit LoopForeverSeqEvent(SeqTrack *pTrack, uint32_t offset = 0, uint32_t length = 0,
                      const std::string &name = "", const std::string &descr = "")
      : SeqEvent(pTrack, offset, length, name, Type::LoopForever) {}
};

//  ************
//  JumpSeqEvent
//  ************

class JumpSeqEvent : public SeqEvent {
public:
  JumpSeqEvent(SeqTrack *pTrack,
               uint32_t destination,
               uint32_t offset = 0,
               uint32_t length = 0,
               const std::string &name = "")
    : SeqEvent(pTrack, offset, length, name, Type::Jump), m_destination(destination) {}


  std::string description() override {
    return fmt::format("{} - destination: 0x{:X}", name(), m_destination);
  }

private:
  uint32_t m_destination;
};

//  ************
//  CallSeqEvent
//  ************

class CallSeqEvent : public SeqEvent {
public:
  CallSeqEvent(SeqTrack *pTrack,
               uint32_t destination,
               uint32_t returnOffset,
               uint32_t offset = 0,
               uint32_t length = 0,
               const std::string &name = "")
  : SeqEvent(pTrack, offset, length, name, Type::Misc),
    m_destination(destination),
    m_returnOffset(returnOffset) {}

  std::string description() override {
    return fmt::format("{} - destination: 0x{:X}, return: 0x{:X}", name(), m_destination, m_returnOffset);
  }

private:
  uint32_t m_destination;
  uint32_t m_returnOffset;
};

//  **************
//  ReturnSeqEvent
//  **************

class ReturnSeqEvent : public SeqEvent {
public:
  ReturnSeqEvent(SeqTrack *pTrack,
                 uint32_t destination,
                 bool hasDestination,
                 uint32_t offset = 0,
                 uint32_t length = 0,
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
  uint32_t m_destination;
  bool m_hasDestination;
};
