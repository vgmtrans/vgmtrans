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
                   Type type,
                   const std::string &desc)
    : VGMItem(pTrack->parentSeq, offset, length, name, type), channel(0),
      parentTrack(pTrack), m_description(desc) {}

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
    : SeqEvent(pTrack, offset, length, name, Type::DurationNote), absKey(absoluteKey), vel(velocity), dur(duration) { }


// ************
// NoteOnSeqEvent
// ************

NoteOnSeqEvent::NoteOnSeqEvent(SeqTrack *pTrack,
                               uint8_t absoluteKey,
                               uint8_t velocity,
                               uint32_t offset,
                               uint32_t length,
                               const std::string &name)
    : SeqEvent(pTrack, offset, length, name, Type::NoteOn), absKey(absoluteKey), vel(velocity) { }



// ************
// NoteOffSeqEvent
// ************

NoteOffSeqEvent::NoteOffSeqEvent(SeqTrack *pTrack,
                                 uint8_t absoluteKey,
                                 uint32_t offset,
                                 uint32_t length,
                                 const std::string &name)
    : SeqEvent(pTrack, offset, length, name, Type::NoteOff), absKey(absoluteKey) { }

// ************
// RestSeqEvent
// ************

RestSeqEvent::RestSeqEvent(SeqTrack *pTrack,
                           uint32_t duration,
                           uint32_t offset,
                           uint32_t length,
                           const std::string &name)
    : SeqEvent(pTrack, offset, length, name, Type::Rest), dur(duration) { }

// *****************
// SetOctaveSeqEvent
// *****************

SetOctaveSeqEvent::SetOctaveSeqEvent(SeqTrack *pTrack,
                                     uint8_t theOctave,
                                     uint32_t offset,
                                     uint32_t length,
                                     const std::string &name)
    : SeqEvent(pTrack, offset, length, name, Type::ChangeState), octave(theOctave) { }

// ***********
// VolSeqEvent
// ***********

VolSeqEvent::VolSeqEvent(SeqTrack *pTrack, uint8_t volume, uint32_t offset, uint32_t length, const std::string &name)
    : SeqEvent(pTrack, offset, length, name, Type::Volume), vol(volume) { }

// ***********
// Volume14BitSeqEvent
// ***********

Volume14BitSeqEvent::Volume14BitSeqEvent(SeqTrack *pTrack, uint16_t volume, uint32_t offset, uint32_t length, const std::string &name)
    : SeqEvent(pTrack, offset, length, name, Type::Volume), m_volume(volume) { }

// ****************
// VolSlideSeqEvent
// ****************

VolSlideSeqEvent::VolSlideSeqEvent(SeqTrack *pTrack,
                                   uint8_t targetVolume,
                                   uint32_t duration,
                                   uint32_t offset,
                                   uint32_t length,
                                   const std::string &name)
    : SeqEvent(pTrack, offset, length, name, Type::VolumeSlide), targVol(targetVolume), dur(duration) { }

// ***********
// MastVolSeqEvent
// ***********

MastVolSeqEvent::MastVolSeqEvent(SeqTrack *pTrack,
                                 uint8_t volume,
                                 uint32_t offset,
                                 uint32_t length,
                                 const std::string &name)
    : SeqEvent(pTrack, offset, length, name, Type::MasterVolume), vol(volume) { }

// ****************
// MastVolSlideSeqEvent
// ****************

MastVolSlideSeqEvent::MastVolSlideSeqEvent(SeqTrack *pTrack,
                                           uint8_t targetVolume,
                                           uint32_t duration,
                                           uint32_t offset,
                                           uint32_t length,
                                           const std::string &name)
    : SeqEvent(pTrack, offset, length, name, Type::MasterVolumeSlide), targVol(targetVolume), dur(duration) { }

// ******************
// ExpressionSeqEvent
// ******************

ExpressionSeqEvent::ExpressionSeqEvent(SeqTrack *pTrack,
                                       uint8_t theLevel,
                                       uint32_t offset,
                                       uint32_t length,
                                       const std::string &name)
    : SeqEvent(pTrack, offset, length, name, Type::Expression), level(theLevel) { }

// ***********************
// ExpressionSlideSeqEvent
// ***********************

ExpressionSlideSeqEvent::ExpressionSlideSeqEvent(SeqTrack *pTrack,
                                                 uint8_t targetExpression,
                                                 uint32_t duration,
                                                 uint32_t offset,
                                                 uint32_t length,
                                                 const std::string &name)
    : SeqEvent(pTrack, offset, length, name, Type::ExpressionSlide), targExpr(targetExpression), dur(duration) { }



// ***********
// PanSeqEvent
// ***********

PanSeqEvent::PanSeqEvent(SeqTrack *pTrack, uint8_t thePan, uint32_t offset, uint32_t length, const std::string &name)
    : SeqEvent(pTrack, offset, length, name, Type::Pan), pan(thePan) { }

// ****************
// PanSlideSeqEvent
// ****************

PanSlideSeqEvent::PanSlideSeqEvent(SeqTrack *pTrack,
                                   uint8_t targetPan,
                                   uint32_t duration,
                                   uint32_t offset,
                                   uint32_t length,
                                   const std::string &name)
    : SeqEvent(pTrack, offset, length, name, Type::PanSlide), targPan(targetPan), dur(duration) { }

// **************
// ReverbSeqEvent
// **************

ReverbSeqEvent::ReverbSeqEvent(SeqTrack *pTrack,
                               uint8_t theReverb,
                               uint32_t offset,
                               uint32_t length,
                               const std::string &name)
    : SeqEvent(pTrack, offset, length, name, Type::Reverb), reverb(theReverb) { }

// *****************
// PitchBendSeqEvent
// *****************

PitchBendSeqEvent::PitchBendSeqEvent(SeqTrack *pTrack,
                                     short thePitchBend,
                                     uint32_t offset,
                                     uint32_t length,
                                     const std::string &name)
    : SeqEvent(pTrack, offset, length, name, Type::PitchBend), pitchbend(thePitchBend) { }

// **********************
// PitchBendRangeSeqEvent
// **********************

PitchBendRangeSeqEvent::PitchBendRangeSeqEvent(SeqTrack *pTrack, uint16_t cents,
                                               uint32_t offset, uint32_t length, const std::string &name)
    : SeqEvent(pTrack, offset, length, name, Type::PitchBendRange), m_cents(cents) { }

// ******************
// FineTuningSeqEvent
// ******************

FineTuningSeqEvent::FineTuningSeqEvent(SeqTrack *pTrack, double cents,
                                       uint32_t offset, uint32_t length, const std::string &name)
    : SeqEvent(pTrack, offset, length, name, Type::Misc), m_cents(cents) { }

// ********************
// CoarseTuningSeqEvent
// ********************

CoarseTuningSeqEvent::CoarseTuningSeqEvent(SeqTrack *pTrack, double semitones,
                                       uint32_t offset, uint32_t length, const std::string &name)
    : SeqEvent(pTrack, offset, length, name, Type::Misc), m_semitones(semitones) { }

// ****************************
// ModulationDepthRangeSeqEvent
// ****************************

ModulationDepthRangeSeqEvent::ModulationDepthRangeSeqEvent(SeqTrack *pTrack, double semitones,
                                                           uint32_t offset, uint32_t length, const std::string &name)
    : SeqEvent(pTrack, offset, length, name, Type::Misc), m_semitones(semitones) { }

// *****************
// TransposeSeqEvent
// *****************

TransposeSeqEvent::TransposeSeqEvent(SeqTrack *pTrack,
                                     int theTranspose,
                                     uint32_t offset,
                                     uint32_t length,
                                     const std::string &name)
    : SeqEvent(pTrack, offset, length, name, Type::Transpose), m_transpose(theTranspose) { }

// ******************
// ModulationSeqEvent
// ******************

ModulationSeqEvent::ModulationSeqEvent(SeqTrack *pTrack,
                                       uint8_t theDepth,
                                       uint32_t offset,
                                       uint32_t length,
                                       const std::string &name)
    : SeqEvent(pTrack, offset, length, name, Type::Modulation), depth(theDepth) { }

// **************
// BreathSeqEvent
// **************

BreathSeqEvent::BreathSeqEvent(SeqTrack *pTrack,
                               uint8_t theDepth,
                               uint32_t offset,
                               uint32_t length,
                               const std::string &name)
    : SeqEvent(pTrack, offset, length, name, Type::Modulation), depth(theDepth) { }

// ***************
// SustainSeqEvent
// ***************

SustainSeqEvent::SustainSeqEvent(SeqTrack *pTrack,
                                 uint8_t theDepth,
                                 uint32_t offset,
                                 uint32_t length,
                                 const std::string &name)
    : SeqEvent(pTrack, offset, length, name, Type::Sustain), depth(theDepth) { }

// ******************
// PortamentoSeqEvent
// ******************

PortamentoSeqEvent::PortamentoSeqEvent(SeqTrack *pTrack,
                                       bool bPortamento,
                                       uint32_t offset,
                                       uint32_t length,
                                       const std::string &name)
    : SeqEvent(pTrack, offset, length, name, Type::Portamento), bOn(bPortamento) { }

// **********************
// PortamentoTimeSeqEvent
// **********************

PortamentoTimeSeqEvent::PortamentoTimeSeqEvent(SeqTrack *pTrack,
                                               uint8_t theTime,
                                               uint32_t offset,
                                               uint32_t length,
                                               const std::string &name)
    : SeqEvent(pTrack, offset, length, name, Type::PortamentoTime), time(theTime) { }

// ******************
// ProgChangeSeqEvent
// ******************

ProgChangeSeqEvent::ProgChangeSeqEvent(SeqTrack *pTrack,
                                       uint32_t programNumber,
                                       uint32_t offset,
                                       uint32_t length,
                                       const std::string &name)
    : SeqEvent(pTrack, offset, length, name, Type::ProgramChange), progNum(programNumber) { }

// ******************
// BankSelectSeqEvent
// ******************

BankSelectSeqEvent::BankSelectSeqEvent(SeqTrack *pTrack,
                                       uint32_t bank,
                                       uint32_t offset,
                                       uint32_t length,
                                       const std::string &name)
    : SeqEvent(pTrack, offset, length, name, Type::BankSelect), bank(bank) { }

// *************
// TempoSeqEvent
// *************

TempoSeqEvent::TempoSeqEvent(SeqTrack *pTrack,
                             double beatsperminute,
                             uint32_t offset,
                             uint32_t length,
                             const std::string &name)
    : SeqEvent(pTrack, offset, length, name, Type::Tempo), bpm(beatsperminute) { }

// ******************
// TempoSlideSeqEvent
// ******************

TempoSlideSeqEvent::TempoSlideSeqEvent(SeqTrack *pTrack,
                                       double targBPM,
                                       uint32_t duration,
                                       uint32_t offset,
                                       uint32_t length,
                                       const std::string &name)
    : SeqEvent(pTrack, offset, length, name, Type::Tempo), targbpm(targBPM), dur(duration) { }

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
    : SeqEvent(pTrack, offset, length, name, Type::TimeSignature), numer(numerator), denom(denominator),
      ticksPerQuarter(theTicksPerQuarter) { }


