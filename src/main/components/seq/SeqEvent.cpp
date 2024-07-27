/*
* VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#include "SeqEvent.h"
#include "SeqTrack.h"

//  ********
//  SeqEvent
//  ********

SeqEvent::SeqEvent(SeqTrack *pTrack,
                   uint32_t offset,
                   uint32_t length,
                   const std::string &name,
                   EventColor color,
                   Icon icon,
                   const std::string &desc)
    : VGMItem(pTrack->parentSeq, offset, length, name, color), channel(0),
      parentTrack(pTrack), m_icon(icon), m_description(desc) {}

// ***************
// DurNoteSeqEvent
// ***************

DurNoteSeqEvent::DurNoteSeqEvent(SeqTrack *pTrack,
                                 uint8_t absoluteKey,
                                 uint8_t velocity,
                                 uint32_t duration,
                                 uint32_t offset,
                                 uint32_t length,
                                 const std::string &name)
    : SeqEvent(pTrack, offset, length, name, CLR_DURNOTE), absKey(absoluteKey), vel(velocity), dur(duration) { }


// ************
// NoteOnSeqEvent
// ************

NoteOnSeqEvent::NoteOnSeqEvent(SeqTrack *pTrack,
                               uint8_t absoluteKey,
                               uint8_t velocity,
                               uint32_t offset,
                               uint32_t length,
                               const std::string &name)
    : SeqEvent(pTrack, offset, length, name, CLR_NOTEON), absKey(absoluteKey), vel(velocity) { }



// ************
// NoteOffSeqEvent
// ************

NoteOffSeqEvent::NoteOffSeqEvent(SeqTrack *pTrack,
                                 uint8_t absoluteKey,
                                 uint32_t offset,
                                 uint32_t length,
                                 const std::string &name)
    : SeqEvent(pTrack, offset, length, name, CLR_NOTEOFF), absKey(absoluteKey) { }

// ************
// RestSeqEvent
// ************

RestSeqEvent::RestSeqEvent(SeqTrack *pTrack,
                           uint32_t duration,
                           uint32_t offset,
                           uint32_t length,
                           const std::string &name)
    : SeqEvent(pTrack, offset, length, name, CLR_REST), dur(duration) { }

// *****************
// SetOctaveSeqEvent
// *****************

SetOctaveSeqEvent::SetOctaveSeqEvent(SeqTrack *pTrack,
                                     uint8_t theOctave,
                                     uint32_t offset,
                                     uint32_t length,
                                     const std::string &name)
    : SeqEvent(pTrack, offset, length, name, CLR_CHANGESTATE), octave(theOctave) { }

// ***********
// VolSeqEvent
// ***********

VolSeqEvent::VolSeqEvent(SeqTrack *pTrack, uint8_t volume, uint32_t offset, uint32_t length, const std::string &name)
    : SeqEvent(pTrack, offset, length, name, CLR_VOLUME), vol(volume) { }

// ***********
// Volume14BitSeqEvent
// ***********

Volume14BitSeqEvent::Volume14BitSeqEvent(SeqTrack *pTrack, uint16_t volume, uint32_t offset, uint32_t length, const std::string &name)
    : SeqEvent(pTrack, offset, length, name, CLR_VOLUME), m_volume(volume) { }

// ****************
// VolSlideSeqEvent
// ****************

VolSlideSeqEvent::VolSlideSeqEvent(SeqTrack *pTrack,
                                   uint8_t targetVolume,
                                   uint32_t duration,
                                   uint32_t offset,
                                   uint32_t length,
                                   const std::string &name)
    : SeqEvent(pTrack, offset, length, name, CLR_VOLUME), targVol(targetVolume), dur(duration) { }

// ***********
// MastVolSeqEvent
// ***********

MastVolSeqEvent::MastVolSeqEvent(SeqTrack *pTrack,
                                 uint8_t volume,
                                 uint32_t offset,
                                 uint32_t length,
                                 const std::string &name)
    : SeqEvent(pTrack, offset, length, name, CLR_VOLUME), vol(volume) { }

// ****************
// MastVolSlideSeqEvent
// ****************

MastVolSlideSeqEvent::MastVolSlideSeqEvent(SeqTrack *pTrack,
                                           uint8_t targetVolume,
                                           uint32_t duration,
                                           uint32_t offset,
                                           uint32_t length,
                                           const std::string &name)
    : SeqEvent(pTrack, offset, length, name, CLR_VOLUME), targVol(targetVolume), dur(duration) { }

// ******************
// ExpressionSeqEvent
// ******************

ExpressionSeqEvent::ExpressionSeqEvent(SeqTrack *pTrack,
                                       uint8_t theLevel,
                                       uint32_t offset,
                                       uint32_t length,
                                       const std::string &name)
    : SeqEvent(pTrack, offset, length, name, CLR_EXPRESSION), level(theLevel) { }

// ***********************
// ExpressionSlideSeqEvent
// ***********************

ExpressionSlideSeqEvent::ExpressionSlideSeqEvent(SeqTrack *pTrack,
                                                 uint8_t targetExpression,
                                                 uint32_t duration,
                                                 uint32_t offset,
                                                 uint32_t length,
                                                 const std::string &name)
    : SeqEvent(pTrack, offset, length, name, CLR_EXPRESSION), targExpr(targetExpression), dur(duration) { }



// ***********
// PanSeqEvent
// ***********

PanSeqEvent::PanSeqEvent(SeqTrack *pTrack, uint8_t thePan, uint32_t offset, uint32_t length, const std::string &name)
    : SeqEvent(pTrack, offset, length, name, CLR_PAN), pan(thePan) { }

// ****************
// PanSlideSeqEvent
// ****************

PanSlideSeqEvent::PanSlideSeqEvent(SeqTrack *pTrack,
                                   uint8_t targetPan,
                                   uint32_t duration,
                                   uint32_t offset,
                                   uint32_t length,
                                   const std::string &name)
    : SeqEvent(pTrack, offset, length, name, CLR_PAN), targPan(targetPan), dur(duration) { }

// **************
// ReverbSeqEvent
// **************

ReverbSeqEvent::ReverbSeqEvent(SeqTrack *pTrack,
                               uint8_t theReverb,
                               uint32_t offset,
                               uint32_t length,
                               const std::string &name)
    : SeqEvent(pTrack, offset, length, name, CLR_REVERB), reverb(theReverb) { }

// *****************
// PitchBendSeqEvent
// *****************

PitchBendSeqEvent::PitchBendSeqEvent(SeqTrack *pTrack,
                                     short thePitchBend,
                                     uint32_t offset,
                                     uint32_t length,
                                     const std::string &name)
    : SeqEvent(pTrack, offset, length, name, CLR_PITCHBEND), pitchbend(thePitchBend) { }

// **********************
// PitchBendRangeSeqEvent
// **********************

PitchBendRangeSeqEvent::PitchBendRangeSeqEvent(SeqTrack *pTrack, uint16_t cents,
                                               uint32_t offset, uint32_t length, const std::string &name)
    : SeqEvent(pTrack, offset, length, name, CLR_PITCHBENDRANGE), m_cents(cents) { }

// ******************
// FineTuningSeqEvent
// ******************

FineTuningSeqEvent::FineTuningSeqEvent(SeqTrack *pTrack, double cents,
                                       uint32_t offset, uint32_t length, const std::string &name)
    : SeqEvent(pTrack, offset, length, name, CLR_MISC), m_cents(cents) { }

// ****************************
// ModulationDepthRangeSeqEvent
// ****************************

ModulationDepthRangeSeqEvent::ModulationDepthRangeSeqEvent(SeqTrack *pTrack, double semitones,
                                                           uint32_t offset, uint32_t length, const std::string &name)
    : SeqEvent(pTrack, offset, length, name, CLR_MISC), m_semitones(semitones) { }

// *****************
// TransposeSeqEvent
// *****************

TransposeSeqEvent::TransposeSeqEvent(SeqTrack *pTrack,
                                     int theTranspose,
                                     uint32_t offset,
                                     uint32_t length,
                                     const std::string &name)
    : SeqEvent(pTrack, offset, length, name, CLR_TRANSPOSE), m_transpose(theTranspose) { }

// ******************
// ModulationSeqEvent
// ******************

ModulationSeqEvent::ModulationSeqEvent(SeqTrack *pTrack,
                                       uint8_t theDepth,
                                       uint32_t offset,
                                       uint32_t length,
                                       const std::string &name)
    : SeqEvent(pTrack, offset, length, name, CLR_MODULATION), depth(theDepth) { }

// **************
// BreathSeqEvent
// **************

BreathSeqEvent::BreathSeqEvent(SeqTrack *pTrack,
                               uint8_t theDepth,
                               uint32_t offset,
                               uint32_t length,
                               const std::string &name)
    : SeqEvent(pTrack, offset, length, name, CLR_MODULATION), depth(theDepth) { }

// ***************
// SustainSeqEvent
// ***************

SustainSeqEvent::SustainSeqEvent(SeqTrack *pTrack,
                                 uint8_t theDepth,
                                 uint32_t offset,
                                 uint32_t length,
                                 const std::string &name)
    : SeqEvent(pTrack, offset, length, name, CLR_SUSTAIN), depth(theDepth) { }

// ******************
// PortamentoSeqEvent
// ******************

PortamentoSeqEvent::PortamentoSeqEvent(SeqTrack *pTrack,
                                       bool bPortamento,
                                       uint32_t offset,
                                       uint32_t length,
                                       const std::string &name)
    : SeqEvent(pTrack, offset, length, name, CLR_PORTAMENTO), bOn(bPortamento) { }

// **********************
// PortamentoTimeSeqEvent
// **********************

PortamentoTimeSeqEvent::PortamentoTimeSeqEvent(SeqTrack *pTrack,
                                               uint8_t theTime,
                                               uint32_t offset,
                                               uint32_t length,
                                               const std::string &name)
    : SeqEvent(pTrack, offset, length, name, CLR_PORTAMENTO), time(theTime) { }

// ******************
// ProgChangeSeqEvent
// ******************

ProgChangeSeqEvent::ProgChangeSeqEvent(SeqTrack *pTrack,
                                       uint32_t programNumber,
                                       uint32_t offset,
                                       uint32_t length,
                                       const std::string &name)
    : SeqEvent(pTrack, offset, length, name, CLR_PROGCHANGE), progNum(programNumber) { }

// ******************
// BankSelectSeqEvent
// ******************

BankSelectSeqEvent::BankSelectSeqEvent(SeqTrack *pTrack,
                                       uint32_t bank,
                                       uint32_t offset,
                                       uint32_t length,
                                       const std::string &name)
    : SeqEvent(pTrack, offset, length, name, CLR_BANKSELECT), bank(bank) { }

// *************
// TempoSeqEvent
// *************

TempoSeqEvent::TempoSeqEvent(SeqTrack *pTrack,
                             double beatsperminute,
                             uint32_t offset,
                             uint32_t length,
                             const std::string &name)
    : SeqEvent(pTrack, offset, length, name, CLR_TEMPO), bpm(beatsperminute) { }

// ******************
// TempoSlideSeqEvent
// ******************

TempoSlideSeqEvent::TempoSlideSeqEvent(SeqTrack *pTrack,
                                       double targBPM,
                                       uint32_t duration,
                                       uint32_t offset,
                                       uint32_t length,
                                       const std::string &name)
    : SeqEvent(pTrack, offset, length, name, CLR_TEMPO), targbpm(targBPM), dur(duration) { }

// ***************
// TimeSigSeqEvent
// ***************

TimeSigSeqEvent::TimeSigSeqEvent(SeqTrack *pTrack,
                                 uint8_t numerator,
                                 uint8_t denominator,
                                 uint8_t theTicksPerQuarter,
                                 uint32_t offset,
                                 uint32_t length,
                                 const std::string &name)
    : SeqEvent(pTrack, offset, length, name, CLR_TIMESIG), numer(numerator), denom(denominator),
      ticksPerQuarter(theTicksPerQuarter) { }


