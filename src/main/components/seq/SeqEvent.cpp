/*
* VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#include "base/types.h"
#include "SeqEvent.h"
#include "SeqTrack.h"

//  ********
//  SeqEvent
//  ********

SeqEvent::SeqEvent(SeqTrack *pTrack,
                   u32 offset,
                   u32 length,
                   const std::string &name,
                   Type type,
                   const std::string &desc)
    : VGMItem(pTrack->parentSeq, offset, length, name, type), channel(0),
      parentTrack(pTrack), m_description(desc) {}

// ***************
// DurNoteSeqEvent
// ***************

DurNoteSeqEvent::DurNoteSeqEvent(SeqTrack *pTrack,
                                 u8 absoluteKey,
                                 u8 velocity,
                                 u32 duration,
                                 u32 offset,
                                 u32 length,
                                 const std::string &name)
    : SeqEvent(pTrack, offset, length, name, Type::DurationNote), absKey(absoluteKey), vel(velocity), dur(duration) { }


// ************
// NoteOnSeqEvent
// ************

NoteOnSeqEvent::NoteOnSeqEvent(SeqTrack *pTrack,
                               u8 absoluteKey,
                               u8 velocity,
                               u32 offset,
                               u32 length,
                               const std::string &name)
    : SeqEvent(pTrack, offset, length, name, Type::NoteOn), absKey(absoluteKey), vel(velocity) { }



// ************
// NoteOffSeqEvent
// ************

NoteOffSeqEvent::NoteOffSeqEvent(SeqTrack *pTrack,
                                 u8 absoluteKey,
                                 u32 offset,
                                 u32 length,
                                 const std::string &name)
    : SeqEvent(pTrack, offset, length, name, Type::NoteOff), absKey(absoluteKey) { }

// ************
// RestSeqEvent
// ************

RestSeqEvent::RestSeqEvent(SeqTrack *pTrack,
                           u32 duration,
                           u32 offset,
                           u32 length,
                           const std::string &name)
    : SeqEvent(pTrack, offset, length, name, Type::Rest), dur(duration) { }

// *****************
// SetOctaveSeqEvent
// *****************

SetOctaveSeqEvent::SetOctaveSeqEvent(SeqTrack *pTrack,
                                     u8 theOctave,
                                     u32 offset,
                                     u32 length,
                                     const std::string &name)
    : SeqEvent(pTrack, offset, length, name, Type::ChangeState), octave(theOctave) { }

// ***********
// VolSeqEvent
// ***********

VolSeqEvent::VolSeqEvent(SeqTrack *pTrack, u8 volume, u32 offset, u32 length, const std::string &name)
    : SeqEvent(pTrack, offset, length, name, Type::Volume), vol(volume) { }

VolSeqEvent::VolSeqEvent(SeqTrack *pTrack, double volume, u32 offset, u32 length, const std::string &name)
    : SeqEvent(pTrack, offset, length, name, Type::Volume), percentVol(volume) { }

// ***********
// Volume14BitSeqEvent
// ***********

Volume14BitSeqEvent::Volume14BitSeqEvent(SeqTrack *pTrack, u16 volume, u32 offset, u32 length, const std::string &name)
    : SeqEvent(pTrack, offset, length, name, Type::Volume), m_volume(volume) { }

// ****************
// VolSlideSeqEvent
// ****************

VolSlideSeqEvent::VolSlideSeqEvent(SeqTrack *pTrack,
                                   u8 targetVolume,
                                   u32 duration,
                                   u32 offset,
                                   u32 length,
                                   const std::string &name)
    : SeqEvent(pTrack, offset, length, name, Type::VolumeSlide), targVol(targetVolume), dur(duration) { }

// ***********
// MastVolSeqEvent
// ***********

MastVolSeqEvent::MastVolSeqEvent(SeqTrack *pTrack,
                                 u8 volume,
                                 u32 offset,
                                 u32 length,
                                 const std::string &name)
    : SeqEvent(pTrack, offset, length, name, Type::MasterVolume), vol(volume) { }

MastVolSeqEvent::MastVolSeqEvent(SeqTrack *pTrack,
                                 double volume,
                                 u32 offset,
                                 u32 length,
                                 const std::string &name)
    : SeqEvent(pTrack, offset, length, name, Type::MasterVolume), percentVol(volume) { }

// ****************
// MastVolSlideSeqEvent
// ****************

MastVolSlideSeqEvent::MastVolSlideSeqEvent(SeqTrack *pTrack,
                                           u8 targetVolume,
                                           u32 duration,
                                           u32 offset,
                                           u32 length,
                                           const std::string &name)
    : SeqEvent(pTrack, offset, length, name, Type::MasterVolumeSlide), targVol(targetVolume), dur(duration) { }

// ******************
// ExpressionSeqEvent
// ******************

ExpressionSeqEvent::ExpressionSeqEvent(SeqTrack *pTrack, u8 theLevel, u32 offset, u32 length, const std::string &name)
    : SeqEvent(pTrack, offset, length, name, Type::Expression), level(theLevel) { }

ExpressionSeqEvent::ExpressionSeqEvent(SeqTrack *pTrack, double level, u32 offset, u32 length, const std::string &name)
    : SeqEvent(pTrack, offset, length, name, Type::Expression), percentLevel(level) { }

// ***********************
// ExpressionSlideSeqEvent
// ***********************

ExpressionSlideSeqEvent::ExpressionSlideSeqEvent(SeqTrack *pTrack,
                                                 u8 targetExpression,
                                                 u32 duration,
                                                 u32 offset,
                                                 u32 length,
                                                 const std::string &name)
    : SeqEvent(pTrack, offset, length, name, Type::ExpressionSlide), targExpr(targetExpression), dur(duration) { }



// ***********
// PanSeqEvent
// ***********

PanSeqEvent::PanSeqEvent(SeqTrack *pTrack, u8 thePan, u32 offset, u32 length, const std::string &name)
    : SeqEvent(pTrack, offset, length, name, Type::Pan), pan(thePan) { }

// ****************
// PanSlideSeqEvent
// ****************

PanSlideSeqEvent::PanSlideSeqEvent(SeqTrack *pTrack,
                                   u8 targetPan,
                                   u32 duration,
                                   u32 offset,
                                   u32 length,
                                   const std::string &name)
    : SeqEvent(pTrack, offset, length, name, Type::PanSlide), targPan(targetPan), dur(duration) { }

// **************
// ReverbSeqEvent
// **************

ReverbSeqEvent::ReverbSeqEvent(SeqTrack *pTrack,
                               u8 theReverb,
                               u32 offset,
                               u32 length,
                               const std::string &name)
    : SeqEvent(pTrack, offset, length, name, Type::Reverb), reverb(theReverb) { }

// *****************
// PitchBendSeqEvent
// *****************

PitchBendSeqEvent::PitchBendSeqEvent(SeqTrack *pTrack,
                                     short thePitchBend,
                                     u32 offset,
                                     u32 length,
                                     const std::string &name)
    : SeqEvent(pTrack, offset, length, name, Type::PitchBend), pitchbend(thePitchBend) { }

// ***********************
// ChannelPressureSeqEvent
// ***********************

ChannelPressureSeqEvent::ChannelPressureSeqEvent(SeqTrack *pTrack,
                                                 u8 thePressure,
                                                 u32 offset,
                                                 u32 length,
                                                 const std::string &name)
    : SeqEvent(pTrack, offset, length, name, Type::ChannelPressure), pressure(thePressure) { }

// **********************
// PitchBendRangeSeqEvent
// **********************

PitchBendRangeSeqEvent::PitchBendRangeSeqEvent(SeqTrack *pTrack, u16 cents,
                                               u32 offset, u32 length, const std::string &name)
    : SeqEvent(pTrack, offset, length, name, Type::PitchBendRange), m_cents(cents) { }

// ******************
// FineTuningSeqEvent
// ******************

FineTuningSeqEvent::FineTuningSeqEvent(SeqTrack *pTrack, double cents,
                                       u32 offset, u32 length, const std::string &name)
    : SeqEvent(pTrack, offset, length, name, Type::Misc), m_cents(cents) { }

// ********************
// CoarseTuningSeqEvent
// ********************

CoarseTuningSeqEvent::CoarseTuningSeqEvent(SeqTrack *pTrack, double semitones,
                                       u32 offset, u32 length, const std::string &name)
    : SeqEvent(pTrack, offset, length, name, Type::Misc), m_semitones(semitones) { }

// ****************************
// ModulationDepthRangeSeqEvent
// ****************************

ModulationDepthRangeSeqEvent::ModulationDepthRangeSeqEvent(SeqTrack *pTrack, double semitones,
                                                           u32 offset, u32 length, const std::string &name)
    : SeqEvent(pTrack, offset, length, name, Type::Misc), m_semitones(semitones) { }

// *****************
// TransposeSeqEvent
// *****************

TransposeSeqEvent::TransposeSeqEvent(SeqTrack *pTrack,
                                     int theTranspose,
                                     u32 offset,
                                     u32 length,
                                     const std::string &name)
    : SeqEvent(pTrack, offset, length, name, Type::Transpose), m_transpose(theTranspose) { }

// ******************
// ModulationSeqEvent
// ******************

ModulationSeqEvent::ModulationSeqEvent(SeqTrack *pTrack,
                                       u8 theDepth,
                                       u32 offset,
                                       u32 length,
                                       const std::string &name)
    : SeqEvent(pTrack, offset, length, name, Type::Modulation), depth(theDepth) { }

// **************
// BreathSeqEvent
// **************

BreathSeqEvent::BreathSeqEvent(SeqTrack *pTrack,
                               u8 theDepth,
                               u32 offset,
                               u32 length,
                               const std::string &name)
    : SeqEvent(pTrack, offset, length, name, Type::Modulation), depth(theDepth) { }

// ***************
// SustainSeqEvent
// ***************

SustainSeqEvent::SustainSeqEvent(SeqTrack *pTrack,
                                 u8 theDepth,
                                 u32 offset,
                                 u32 length,
                                 const std::string &name)
    : SeqEvent(pTrack, offset, length, name, Type::Sustain), depth(theDepth) { }

// ******************
// PortamentoSeqEvent
// ******************

PortamentoSeqEvent::PortamentoSeqEvent(SeqTrack *pTrack,
                                       bool bPortamento,
                                       u32 offset,
                                       u32 length,
                                       const std::string &name)
    : SeqEvent(pTrack, offset, length, name, Type::Portamento), bOn(bPortamento) { }

// **********************
// PortamentoTimeSeqEvent
// **********************

PortamentoTimeSeqEvent::PortamentoTimeSeqEvent(SeqTrack *pTrack,
                                               u8 theTime,
                                               u32 offset,
                                               u32 length,
                                               const std::string &name)
    : SeqEvent(pTrack, offset, length, name, Type::PortamentoTime), time(theTime) { }

// ******************
// ProgChangeSeqEvent
// ******************

ProgChangeSeqEvent::ProgChangeSeqEvent(SeqTrack *pTrack,
                                       u32 programNumber,
                                       u32 offset,
                                       u32 length,
                                       const std::string &name)
    : SeqEvent(pTrack, offset, length, name, Type::ProgramChange), progNum(programNumber) { }

// ******************
// BankSelectSeqEvent
// ******************

BankSelectSeqEvent::BankSelectSeqEvent(SeqTrack *pTrack,
                                       u32 bank,
                                       u32 offset,
                                       u32 length,
                                       const std::string &name)
    : SeqEvent(pTrack, offset, length, name, Type::BankSelect), bank(bank) { }

// *************
// TempoSeqEvent
// *************

TempoSeqEvent::TempoSeqEvent(SeqTrack *pTrack,
                             double beatsperminute,
                             u32 offset,
                             u32 length,
                             const std::string &name)
    : SeqEvent(pTrack, offset, length, name, Type::Tempo), bpm(beatsperminute) { }

// ******************
// TempoSlideSeqEvent
// ******************

TempoSlideSeqEvent::TempoSlideSeqEvent(SeqTrack *pTrack,
                                       double targBPM,
                                       u32 duration,
                                       u32 offset,
                                       u32 length,
                                       const std::string &name)
    : SeqEvent(pTrack, offset, length, name, Type::Tempo), targbpm(targBPM), dur(duration) { }

// ***************
// TimeSigSeqEvent
// ***************

TimeSigSeqEvent::TimeSigSeqEvent(SeqTrack *pTrack,
                                 u8 numerator,
                                 u8 denominator,
                                 u8 theTicksPerQuarter,
                                 u32 offset,
                                 u32 length,
                                 const std::string &name)
    : SeqEvent(pTrack, offset, length, name, Type::TimeSignature), numer(numerator), denom(denominator),
      ticksPerQuarter(theTicksPerQuarter) { }
