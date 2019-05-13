/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
 #include "pch.h"
#include "VGMSeqNoTrks.h"
#include "SeqEvent.h"
#include "Root.h"

using namespace std;

VGMSeqNoTrks::VGMSeqNoTrks(const string &format, RawFile *file, uint32_t offset, wstring name)
    : VGMSeq(format, file, offset, 0, name),
      SeqTrack(this) {
  ResetVars();
  VGMSeq::AddContainer<SeqEvent>(aEvents);
}

VGMSeqNoTrks::~VGMSeqNoTrks(void) {
}

void VGMSeqNoTrks::ResetVars() {
  midiTracks.clear();        //no need to delete the contents, that happens when the midi is deleted
  TryExpandMidiTracks(nNumTracks);

  channel = 0;
  SetCurTrack(channel);

  VGMSeq::ResetVars();
  SeqTrack::ResetVars();
}

//LoadMain() - loads all sequence data into the class
bool VGMSeqNoTrks::LoadMain() {
  this->SeqTrack::readMode = this->VGMSeq::readMode = READMODE_ADD_TO_UI;
  if (!GetHeaderInfo())
    return false;

  if (!LoadEvents())
    return false;

  if (length() == 0) {
    VGMSeq::SetGuessedLength();
//		length() = (aEvents.back()->dwOffset + aEvents.back()->unLength) - offset();			//length == to the end of the last event
  }

  return true;
}

bool VGMSeqNoTrks::LoadEvents(long stopTime) {
  ResetVars();
  if (bAlwaysWriteInitialTempo)
    AddTempoBPMNoItem(initialTempoBPM);
  if (bAlwaysWriteInitialVol)
    for (int i = 0; i < 16; i++) {
      channel = i;
      AddVolNoItem(initialVol);
    }
  if (bAlwaysWriteInitialExpression)
    for (int i = 0; i < 16; i++) {
      channel = i;
      AddExpressionNoItem(initialExpression);
    }
  if (bAlwaysWriteInitialReverb)
    for (int i = 0; i < 16; i++) {
      channel = i;
      AddReverbNoItem(initialReverb);
    }
  if (bAlwaysWriteInitialPitchBendRange)
    for (int i = 0; i < 16; i++) {
      channel = i;
      AddPitchBendRangeNoItem(initialPitchBendRangeSemiTones, initialPitchBendRangeCents);
    }

  bInLoop = false;
  curOffset = eventsOffset();    //start at beginning of track
  while (curOffset < rawfile->size()) {
    if (GetTime() >= (unsigned) stopTime) {
      break;
    }

    if (!ReadEvent()) {
      break;
    }
  }
  return true;
}


MidiFile *VGMSeqNoTrks::ConvertToMidi() {
  this->SeqTrack::readMode = this->VGMSeq::readMode = READMODE_FIND_DELTA_LENGTH;

  if (!LoadEvents())
    return NULL;
  if (!PostLoad())
    return NULL;

  long stopTime = -1;
  stopTime = deltaLength;

  MidiFile *newmidi = new MidiFile(this);
  this->midi = newmidi;
  this->SeqTrack::readMode = this->VGMSeq::readMode = READMODE_CONVERT_TO_MIDI;
  if (!LoadEvents(stopTime)) {
    delete midi;
    this->midi = NULL;
    return NULL;
  }
  if (!PostLoad()) {
    delete midi;
    this->midi = NULL;
    return NULL;
  }
  this->midi = NULL;
  return newmidi;
}

MidiTrack *VGMSeqNoTrks::GetFirstMidiTrack() {
  if (midiTracks.size() > 0) {
    return midiTracks[0];
  }
  else {
    return pMidiTrack;
  }
}

// checks whether or not we have already created the given number of MidiTracks.  If not, it appends the extra tracks.
// doesn't ever need to be called directly by format code, since SetCurMidiTrack does so automatically.
void VGMSeqNoTrks::TryExpandMidiTracks(uint32_t numTracks) {
  if (VGMSeq::readMode != READMODE_CONVERT_TO_MIDI)
    return;
  if (midiTracks.size() < numTracks) {
    size_t initialTrackSize = midiTracks.size();
    for (size_t i = 0; i < numTracks - initialTrackSize; i++)
      midiTracks.push_back(midi->AddTrack());
  }
}

void VGMSeqNoTrks::SetCurTrack(uint32_t trackNum) {
  if (VGMSeq::readMode != READMODE_CONVERT_TO_MIDI)
    return;

  TryExpandMidiTracks(trackNum + 1);
  pMidiTrack = midiTracks[trackNum];
}


void VGMSeqNoTrks::AddTime(uint32_t delta) {
  VGMSeq::time += delta;
  if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
    for (uint32_t i = 0; i < midiTracks.size(); i++)
      midiTracks[i]->AddDelta(delta);
  }
}
