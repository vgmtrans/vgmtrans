#include "stdafx.h"
#include "SeqEvent.h"
#include "SeqTrack.h"

//  ********
//  SeqEvent
//  ********

//DECLARE_MENU(SeqEvent)

SeqEvent::SeqEvent(SeqTrack* pTrack, ULONG offset, ULONG length, const wchar_t* name, BYTE color, Icon icon, const wchar_t* desc)
: VGMItem((VGMFile*)pTrack->parentSeq, offset, length, name, color), desc(desc ? desc : L""), icon(icon), parentTrack(pTrack)
{
}

// ***************
// DurNoteSeqEvent
// ***************

DurNoteSeqEvent::DurNoteSeqEvent(SeqTrack* pTrack, BYTE absoluteKey, BYTE velocity, ULONG duration, ULONG offset, ULONG length, const wchar_t* name)
: SeqEvent(pTrack, offset, length, name, CLR_DURNOTE), absKey(absoluteKey), vel(velocity), dur(duration)
{}


// ************
// NoteOnSeqEvent
// ************

NoteOnSeqEvent::NoteOnSeqEvent(SeqTrack* pTrack, BYTE absoluteKey, BYTE velocity, ULONG offset, ULONG length, const wchar_t* name)
: SeqEvent(pTrack, offset, length, name, CLR_NOTEON), absKey(absoluteKey), vel(velocity)
{}



// ************
// NoteOffSeqEvent
// ************

NoteOffSeqEvent::NoteOffSeqEvent(SeqTrack* pTrack, BYTE absoluteKey, ULONG offset, ULONG length, const wchar_t* name)
: SeqEvent(pTrack, offset, length, name, CLR_NOTEOFF), absKey(absoluteKey)
{}

// ************
// RestSeqEvent
// ************

RestSeqEvent::RestSeqEvent(SeqTrack* pTrack, ULONG duration, ULONG offset, ULONG length, const wchar_t* name)
: SeqEvent(pTrack, offset, length, name, CLR_REST), dur(duration)
{}

// *****************
// SetOctaveSeqEvent
// *****************

SetOctaveSeqEvent::SetOctaveSeqEvent(SeqTrack* pTrack, BYTE theOctave, ULONG offset, ULONG length, const wchar_t* name)
: SeqEvent(pTrack, offset, length, name, CLR_CHANGESTATE), octave(theOctave)
{}

// ***********
// VolSeqEvent
// ***********

VolSeqEvent::VolSeqEvent(SeqTrack* pTrack, BYTE volume, ULONG offset, ULONG length, const wchar_t* name)
: SeqEvent(pTrack, offset, length, name, CLR_VOLUME), vol(volume)
{}

// ****************
// VolSlideSeqEvent
// ****************

VolSlideSeqEvent::VolSlideSeqEvent(SeqTrack* pTrack, BYTE targetVolume, ULONG duration, ULONG offset, ULONG length, const wchar_t* name)
: SeqEvent(pTrack, offset, length, name, CLR_VOLUME), targVol(targetVolume), dur(duration)
{}

// ***********
// MastVolSeqEvent
// ***********

MastVolSeqEvent::MastVolSeqEvent(SeqTrack* pTrack, BYTE volume, ULONG offset, ULONG length, const wchar_t* name)
: SeqEvent(pTrack, offset, length, name, CLR_VOLUME), vol(volume)
{}

// ****************
// MastVolSlideSeqEvent
// ****************

MastVolSlideSeqEvent::MastVolSlideSeqEvent(SeqTrack* pTrack, BYTE targetVolume, ULONG duration, ULONG offset, ULONG length, const wchar_t* name)
: SeqEvent(pTrack, offset, length, name, CLR_VOLUME), targVol(targetVolume), dur(duration)
{}

// ******************
// ExpressionSeqEvent
// ******************

ExpressionSeqEvent::ExpressionSeqEvent(SeqTrack* pTrack, BYTE theLevel, ULONG offset, ULONG length, const wchar_t* name)
: SeqEvent(pTrack, offset, length, name, CLR_EXPRESSION), level(theLevel)
{}

// ***********************
// ExpressionSlideSeqEvent
// ***********************

ExpressionSlideSeqEvent::ExpressionSlideSeqEvent(SeqTrack* pTrack, BYTE targetExpression, ULONG duration, ULONG offset, ULONG length, const wchar_t* name)
: SeqEvent(pTrack, offset, length, name, CLR_EXPRESSION), targExpr(targetExpression), dur(duration)
{}



// ***********
// PanSeqEvent
// ***********

PanSeqEvent::PanSeqEvent(SeqTrack* pTrack, BYTE thePan, ULONG offset, ULONG length, const wchar_t* name)
: SeqEvent(pTrack, offset, length, name, CLR_PAN), pan(thePan)
{}

// ****************
// PanSlideSeqEvent
// ****************

PanSlideSeqEvent::PanSlideSeqEvent(SeqTrack* pTrack, BYTE targetPan, ULONG duration, ULONG offset, ULONG length, const wchar_t* name)
: SeqEvent(pTrack, offset, length, name, CLR_PAN), targPan(targetPan), dur(duration)
{}

// **************
// ReverbSeqEvent
// **************

ReverbSeqEvent::ReverbSeqEvent(SeqTrack* pTrack, BYTE theReverb, ULONG offset, ULONG length, const wchar_t* name)
: SeqEvent(pTrack, offset, length, name, CLR_REVERB), reverb(theReverb)
{}

// *****************
// PitchBendSeqEvent
// *****************

PitchBendSeqEvent::PitchBendSeqEvent(SeqTrack* pTrack, short thePitchBend, ULONG offset, ULONG length, const wchar_t* name)
: SeqEvent(pTrack, offset, length, name, CLR_PITCHBEND), pitchbend(thePitchBend)
{}

// **********************
// PitchBendRangeSeqEvent
// **********************

PitchBendRangeSeqEvent::PitchBendRangeSeqEvent(SeqTrack* pTrack, BYTE theSemiTones, BYTE theCents,
											   ULONG offset, ULONG length, const wchar_t* name)
: SeqEvent(pTrack, offset, length, name, CLR_PITCHBENDRANGE), semitones(theSemiTones), cents(theCents)
{}

// *****************
// TransposeSeqEvent
// *****************

TransposeSeqEvent::TransposeSeqEvent(SeqTrack* pTrack, int theTranspose, ULONG offset, ULONG length, const wchar_t* name)
: SeqEvent(pTrack, offset, length, name, CLR_TRANSPOSE), transpose(theTranspose)
{}

// ******************
// ModulationSeqEvent
// ******************

ModulationSeqEvent::ModulationSeqEvent(SeqTrack* pTrack, BYTE theDepth, ULONG offset, ULONG length, const wchar_t* name)
: SeqEvent(pTrack, offset, length, name, CLR_MODULATION), depth(theDepth)
{}

// **************
// BreathSeqEvent
// **************

BreathSeqEvent::BreathSeqEvent(SeqTrack* pTrack, BYTE theDepth, ULONG offset, ULONG length, const wchar_t* name)
: SeqEvent(pTrack, offset, length, name, CLR_MODULATION), depth(theDepth)
{}

// ***************
// SustainSeqEvent
// ***************

SustainSeqEvent::SustainSeqEvent(SeqTrack* pTrack, bool bSustain, ULONG offset, ULONG length, const wchar_t* name)
: SeqEvent(pTrack, offset, length, name, CLR_SUSTAIN), bOn(bSustain)
{}

// ******************
// PortamentoSeqEvent
// ******************

PortamentoSeqEvent::PortamentoSeqEvent(SeqTrack* pTrack, bool bPortamento, ULONG offset, ULONG length, const wchar_t* name)
: SeqEvent(pTrack, offset, length, name, CLR_PORTAMENTO), bOn(bPortamento)
{}

// **********************
// PortamentoTimeSeqEvent
// **********************

PortamentoTimeSeqEvent::PortamentoTimeSeqEvent(SeqTrack* pTrack, BYTE theTime, ULONG offset, ULONG length, const wchar_t* name)
: SeqEvent(pTrack, offset, length, name, CLR_PORTAMENTO), time(theTime)
{}

// ******************
// ProgChangeSeqEvent
// ******************

ProgChangeSeqEvent::ProgChangeSeqEvent(SeqTrack* pTrack, ULONG programNumber, ULONG offset, ULONG length, const wchar_t* name)
: SeqEvent(pTrack, offset, length, name, CLR_PROGCHANGE), progNum(programNumber)
{}

// *************
// TempoSeqEvent
// *************

TempoSeqEvent::TempoSeqEvent(SeqTrack* pTrack, double beatsperminute, ULONG offset, ULONG length, const wchar_t* name)
: SeqEvent(pTrack, offset, length, name, CLR_TEMPO), bpm(beatsperminute)
{}

// ******************
// TempoSlideSeqEvent
// ******************

TempoSlideSeqEvent::TempoSlideSeqEvent(SeqTrack* pTrack, double targBPM, ULONG duration, ULONG offset, ULONG length, const wchar_t* name)
: SeqEvent(pTrack, offset, length, name, CLR_TEMPO), targbpm(targBPM), dur(duration)
{}

// ***************
// TimeSigSeqEvent
// ***************

TimeSigSeqEvent::TimeSigSeqEvent(SeqTrack* pTrack, BYTE numerator, BYTE denominator, BYTE theTicksPerQuarter, ULONG offset, ULONG length, const wchar_t* name)
: SeqEvent(pTrack, offset, length, name, CLR_TIMESIG), numer(numerator), denom(denominator), ticksPerQuarter(theTicksPerQuarter)
{}


