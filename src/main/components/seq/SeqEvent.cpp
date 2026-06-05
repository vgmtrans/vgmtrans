/*
* VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#include "SeqEvent.h"

#include "base/Types.h"
#include "SeqTrack.h"
#include "VGMSeq.h"

//  ********
//  SeqEvent
//  ********

SeqEvent::SeqEvent(SeqTrack *track,
                   u32 offset,
                   u32 length,
                   const std::string &name,
                   Type type,
                   const std::string &desc)
    : VGMItem(track->parentSeq, offset, length, name, type), channel(0),
      parentTrack(track), m_description(desc) {}

// ***************
// DurNoteSeqEvent
// ***************

DurNoteSeqEvent::DurNoteSeqEvent(SeqTrack *track,
                                 u8 absoluteKey,
                                 u8 velocity,
                                 u32 duration,
                                 u32 offset,
                                 u32 length,
                                 const std::string &name)
    : SeqEvent(track, offset, length, name, Type::DurationNote), absKey(absoluteKey), vel(velocity), dur(duration) { }


// ************
// NoteOnSeqEvent
// ************

NoteOnSeqEvent::NoteOnSeqEvent(SeqTrack *track,
                               u8 absoluteKey,
                               u8 velocity,
                               u32 offset,
                               u32 length,
                               const std::string &name)
    : SeqEvent(track, offset, length, name, Type::NoteOn), absKey(absoluteKey), vel(velocity) { }



// ************
// NoteOffSeqEvent
// ************

NoteOffSeqEvent::NoteOffSeqEvent(SeqTrack *track,
                                 u8 absoluteKey,
                                 u32 offset,
                                 u32 length,
                                 const std::string &name)
    : SeqEvent(track, offset, length, name, Type::NoteOff), absKey(absoluteKey) { }

// ************
// RestSeqEvent
// ************

RestSeqEvent::RestSeqEvent(SeqTrack *track,
                           u32 duration,
                           u32 offset,
                           u32 length,
                           const std::string &name)
    : SeqEvent(track, offset, length, name, Type::Rest), dur(duration) { }

// *****************
// SetOctaveSeqEvent
// *****************

SetOctaveSeqEvent::SetOctaveSeqEvent(SeqTrack *track,
                                     u8 octave,
                                     u32 offset,
                                     u32 length,
                                     const std::string &name)
    : SeqEvent(track, offset, length, name, Type::ChangeState), octave(octave) { }

// ***********
// VolSeqEvent
// ***********

VolSeqEvent::VolSeqEvent(SeqTrack *track, u8 volume, u32 offset, u32 length, const std::string &name)
    : SeqEvent(track, offset, length, name, Type::Volume), vol(volume) { }

VolSeqEvent::VolSeqEvent(SeqTrack *track, double volume, u32 offset, u32 length, const std::string &name)
    : SeqEvent(track, offset, length, name, Type::Volume), percentVol(volume) { }

// ***********
// Volume14BitSeqEvent
// ***********

Volume14BitSeqEvent::Volume14BitSeqEvent(SeqTrack *track, u16 volume, u32 offset, u32 length, const std::string &name)
    : SeqEvent(track, offset, length, name, Type::Volume), m_volume(volume) { }

// ****************
// VolSlideSeqEvent
// ****************

VolSlideSeqEvent::VolSlideSeqEvent(SeqTrack *track,
                                   u8 targetVolume,
                                   u32 duration,
                                   u32 offset,
                                   u32 length,
                                   const std::string &name)
    : SeqEvent(track, offset, length, name, Type::VolumeSlide), targVol(targetVolume), dur(duration) { }

// ***********
// MastVolSeqEvent
// ***********

MastVolSeqEvent::MastVolSeqEvent(SeqTrack *track,
                                 u8 volume,
                                 u32 offset,
                                 u32 length,
                                 const std::string &name)
    : SeqEvent(track, offset, length, name, Type::MasterVolume), vol(volume) { }

MastVolSeqEvent::MastVolSeqEvent(SeqTrack *track,
                                 double volume,
                                 u32 offset,
                                 u32 length,
                                 const std::string &name)
    : SeqEvent(track, offset, length, name, Type::MasterVolume), percentVol(volume) { }

// ****************
// MastVolSlideSeqEvent
// ****************

MastVolSlideSeqEvent::MastVolSlideSeqEvent(SeqTrack *track,
                                           u8 targetVolume,
                                           u32 duration,
                                           u32 offset,
                                           u32 length,
                                           const std::string &name)
    : SeqEvent(track, offset, length, name, Type::MasterVolumeSlide), targVol(targetVolume), dur(duration) { }

// ******************
// ExpressionSeqEvent
// ******************

ExpressionSeqEvent::ExpressionSeqEvent(SeqTrack *track, u8 level, u32 offset, u32 length, const std::string &name)
    : SeqEvent(track, offset, length, name, Type::Expression), level(level) { }

ExpressionSeqEvent::ExpressionSeqEvent(SeqTrack *track, double level, u32 offset, u32 length, const std::string &name)
    : SeqEvent(track, offset, length, name, Type::Expression), percentLevel(level) { }

// ***********************
// ExpressionSlideSeqEvent
// ***********************

ExpressionSlideSeqEvent::ExpressionSlideSeqEvent(SeqTrack *track,
                                                 u8 targetExpression,
                                                 u32 duration,
                                                 u32 offset,
                                                 u32 length,
                                                 const std::string &name)
    : SeqEvent(track, offset, length, name, Type::ExpressionSlide), targExpr(targetExpression), dur(duration) { }



// ***********
// PanSeqEvent
// ***********

PanSeqEvent::PanSeqEvent(SeqTrack *track, u8 pan, u32 offset, u32 length, const std::string &name)
    : SeqEvent(track, offset, length, name, Type::Pan), pan(pan) { }

// ****************
// PanSlideSeqEvent
// ****************

PanSlideSeqEvent::PanSlideSeqEvent(SeqTrack *track,
                                   u8 targetPan,
                                   u32 duration,
                                   u32 offset,
                                   u32 length,
                                   const std::string &name)
    : SeqEvent(track, offset, length, name, Type::PanSlide), targPan(targetPan), dur(duration) { }

// **************
// ReverbSeqEvent
// **************

ReverbSeqEvent::ReverbSeqEvent(SeqTrack *track,
                               u8 reverb,
                               u32 offset,
                               u32 length,
                               const std::string &name)
    : SeqEvent(track, offset, length, name, Type::Reverb), reverb(reverb) { }

// *****************
// PitchBendSeqEvent
// *****************

PitchBendSeqEvent::PitchBendSeqEvent(SeqTrack *track,
                                     short pitchbend,
                                     u32 offset,
                                     u32 length,
                                     const std::string &name)
    : SeqEvent(track, offset, length, name, Type::PitchBend), pitchbend(pitchbend) { }

// ***********************
// ChannelPressureSeqEvent
// ***********************

ChannelPressureSeqEvent::ChannelPressureSeqEvent(SeqTrack *track,
                                                 u8 pressure,
                                                 u32 offset,
                                                 u32 length,
                                                 const std::string &name)
    : SeqEvent(track, offset, length, name, Type::ChannelPressure), pressure(pressure) { }

// **********************
// PitchBendRangeSeqEvent
// **********************

PitchBendRangeSeqEvent::PitchBendRangeSeqEvent(SeqTrack *track, u16 cents,
                                               u32 offset, u32 length, const std::string &name)
    : SeqEvent(track, offset, length, name, Type::PitchBendRange), m_cents(cents) { }

// ******************
// FineTuningSeqEvent
// ******************

FineTuningSeqEvent::FineTuningSeqEvent(SeqTrack *track, double cents,
                                       u32 offset, u32 length, const std::string &name)
    : SeqEvent(track, offset, length, name, Type::Misc), m_cents(cents) { }

// ********************
// CoarseTuningSeqEvent
// ********************

CoarseTuningSeqEvent::CoarseTuningSeqEvent(SeqTrack *track, double semitones,
                                       u32 offset, u32 length, const std::string &name)
    : SeqEvent(track, offset, length, name, Type::Misc), m_semitones(semitones) { }

// ****************************
// ModulationDepthRangeSeqEvent
// ****************************

ModulationDepthRangeSeqEvent::ModulationDepthRangeSeqEvent(SeqTrack *track, double semitones,
                                                           u32 offset, u32 length, const std::string &name)
    : SeqEvent(track, offset, length, name, Type::Misc), m_semitones(semitones) { }

// *****************
// TransposeSeqEvent
// *****************

TransposeSeqEvent::TransposeSeqEvent(SeqTrack *track,
                                     int transpose,
                                     u32 offset,
                                     u32 length,
                                     const std::string &name)
    : SeqEvent(track, offset, length, name, Type::Transpose), m_transpose(transpose) { }

// ******************
// ModulationSeqEvent
// ******************

ModulationSeqEvent::ModulationSeqEvent(SeqTrack *track,
                                       u8 depth,
                                       u32 offset,
                                       u32 length,
                                       const std::string &name)
    : SeqEvent(track, offset, length, name, Type::Modulation), depth(depth) { }

// **************
// BreathSeqEvent
// **************

BreathSeqEvent::BreathSeqEvent(SeqTrack *track,
                               u8 depth,
                               u32 offset,
                               u32 length,
                               const std::string &name)
    : SeqEvent(track, offset, length, name, Type::Modulation), depth(depth) { }

// ***************
// SustainSeqEvent
// ***************

SustainSeqEvent::SustainSeqEvent(SeqTrack *track,
                                 u8 depth,
                                 u32 offset,
                                 u32 length,
                                 const std::string &name)
    : SeqEvent(track, offset, length, name, Type::Sustain), depth(depth) { }

// ******************
// PortamentoSeqEvent
// ******************

PortamentoSeqEvent::PortamentoSeqEvent(SeqTrack *track,
                                       bool bPortamento,
                                       u32 offset,
                                       u32 length,
                                       const std::string &name)
    : SeqEvent(track, offset, length, name, Type::Portamento), bOn(bPortamento) { }

// **********************
// PortamentoTimeSeqEvent
// **********************

PortamentoTimeSeqEvent::PortamentoTimeSeqEvent(SeqTrack *track,
                                               u8 time,
                                               u32 offset,
                                               u32 length,
                                               const std::string &name)
    : SeqEvent(track, offset, length, name, Type::PortamentoTime), time(time) { }

// ******************
// ProgChangeSeqEvent
// ******************

ProgChangeSeqEvent::ProgChangeSeqEvent(SeqTrack *track,
                                       u32 programNumber,
                                       u32 offset,
                                       u32 length,
                                       const std::string &name)
    : SeqEvent(track, offset, length, name, Type::ProgramChange), progNum(programNumber) { }

// ******************
// BankSelectSeqEvent
// ******************

BankSelectSeqEvent::BankSelectSeqEvent(SeqTrack *track,
                                       u32 bank,
                                       u32 offset,
                                       u32 length,
                                       const std::string &name)
    : SeqEvent(track, offset, length, name, Type::BankSelect), bank(bank) { }

// *************
// TempoSeqEvent
// *************

TempoSeqEvent::TempoSeqEvent(SeqTrack *track,
                             double beatsperminute,
                             u32 offset,
                             u32 length,
                             const std::string &name)
    : SeqEvent(track, offset, length, name, Type::Tempo), bpm(beatsperminute) { }

// ******************
// TempoSlideSeqEvent
// ******************

TempoSlideSeqEvent::TempoSlideSeqEvent(SeqTrack *track,
                                       double targBPM,
                                       u32 duration,
                                       u32 offset,
                                       u32 length,
                                       const std::string &name)
    : SeqEvent(track, offset, length, name, Type::Tempo), targbpm(targBPM), dur(duration) { }

// ***************
// TimeSigSeqEvent
// ***************

TimeSigSeqEvent::TimeSigSeqEvent(SeqTrack *track,
                                 u8 numerator,
                                 u8 denominator,
                                 u8 ticksPerQuarter,
                                 u32 offset,
                                 u32 length,
                                 const std::string &name)
    : SeqEvent(track, offset, length, name, Type::TimeSignature), numer(numerator), denom(denominator),
      ticksPerQuarter(ticksPerQuarter) { }
