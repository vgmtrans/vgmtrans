#include "pch.h"

#include "SeqTrack.h"
#include "SeqEvent.h"
#include "ScaleConversion.h"
#include "Options.h"
#include "Root.h"

using namespace std;

//  ********
//  SeqTrack
//  ********


SeqTrack::SeqTrack(VGMSeq *parentFile, uint32_t offset, uint32_t length, wstring name)
    : VGMContainerItem(parentFile, offset, length, name),
      parentSeq(parentFile) {
  dwStartOffset = offset;
  bMonophonic = parentSeq->bMonophonicTracks;
  pMidiTrack = NULL;
  ResetVars();
  bDetermineTrackLengthEventByEvent = false;
  bWriteGenericEventAsTextEvent = false;

  AddContainer<SeqEvent>(aEvents);
}

SeqTrack::~SeqTrack() {
  DeleteVect<SeqEvent>(aEvents);
}

void SeqTrack::ResetVars() {
  active = true;
  bInLoop = false;
  foreverLoops = 0;
  deltaLength = -1;
  deltaTime = 0;
  vel = 100;
  vol = 100;
  expression = 127;
  mastVol = 127;
  prevPan = 64;
  prevReverb = 40;
  channelGroup = 0;
  transpose = 0;
  cDrumNote = -1;
  cKeyCorrection = 0;
}


bool SeqTrack::ReadEvent(void) {
  return false;        //by default, don't add any events, just stop immediately.
}

bool SeqTrack::LoadTrackInit(int trackNum, MidiTrack *preparedMidiTrack) {
  VisitedAddresses.clear();
  VisitedAddresses.reserve(8192);
  VisitedAddressMax = 0;

  ResetVars();
  if (readMode == READMODE_CONVERT_TO_MIDI) {
    if (preparedMidiTrack != NULL) {
      pMidiTrack = preparedMidiTrack;
    }
    else {
      pMidiTrack = parentSeq->midi->AddTrack();
    }
  }
  SetChannelAndGroupFromTrkNum(trackNum);

  curOffset = dwStartOffset;    //start at beginning of track

  if (readMode == READMODE_CONVERT_TO_MIDI) {
    if (preparedMidiTrack == NULL) {
      AddInitialMidiEvents(trackNum);
    }
  }
  return true;
}

void SeqTrack::LoadTrackMainLoop(uint32_t stopOffset, int32_t stopTime) {
  if (!active) {
    return;
  }

  if (stopTime == -1) {
    stopTime = 0x7FFFFFFF;
  }

  if (GetTime() >= (unsigned) stopTime) {
    active = false;
    return;
  }

  if (parentSeq->bLoadTickByTick) {
    OnTickBegin();

    if (deltaTime > 0) {
      deltaTime--;
    }

    while (deltaTime == 0) {
      if (curOffset >= stopOffset) {
        if (readMode == READMODE_FIND_DELTA_LENGTH)
          deltaLength = GetTime();

        active = false;
        break;
      }

      if (!ReadEvent()) {
        deltaLength = GetTime();
        active = false;
        break;
      }
    }

    OnTickEnd();
  }
  else {
    while (curOffset < stopOffset && GetTime() < (unsigned) stopTime) {
      if (!ReadEvent()) {
        active = false;
        break;
      }
    }

    if (readMode == READMODE_FIND_DELTA_LENGTH) {
      deltaLength = GetTime();
    }
  }

  return;
}

void SeqTrack::SetChannelAndGroupFromTrkNum(int theTrackNum) {
  if (theTrackNum > 39)
    theTrackNum += 3;
  else if (theTrackNum > 23)
    theTrackNum += 2;                    //compensate for channel 10 - drum track.  we'll skip it, by default
  else if (theTrackNum > 8)
    theTrackNum++;                        //''
  channel = theTrackNum % 16;
  channelGroup = theTrackNum / 16;
  if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->SetChannelGroup(channelGroup);
}

void SeqTrack::AddInitialMidiEvents(int trackNum) {
  if (trackNum == 0)
    pMidiTrack->AddSeqName(parentSeq->GetName()->c_str());
  wostringstream ssTrackName;
  ssTrackName << L"Track: 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << dwStartOffset
      << std::endl;
  pMidiTrack->AddTrackName(ssTrackName.str().c_str());

  if (trackNum == 0) {
    pMidiTrack->AddGMReset();
    pMidiTrack->AddGM2Reset();
    if (parentSeq->bAlwaysWriteInitialTempo)
      pMidiTrack->AddTempoBPM(parentSeq->initialTempoBPM);
  }
  if (parentSeq->bAlwaysWriteInitialVol)
    AddVolNoItem(parentSeq->initialVol);
  if (parentSeq->bAlwaysWriteInitialExpression)
    AddExpressionNoItem(parentSeq->initialExpression);
  if (parentSeq->bAlwaysWriteInitialReverb)
    AddReverbNoItem(parentSeq->initialReverb);
  if (parentSeq->bAlwaysWriteInitialPitchBendRange)
    AddPitchBendRangeNoItem(parentSeq->initialPitchBendRangeSemiTones, parentSeq->initialPitchBendRangeCents);
  if (parentSeq->bAlwaysWriteInitialMono)
    AddMonoNoItem();
}

uint32_t SeqTrack::GetTime() {
  return parentSeq->time;
}

void SeqTrack::SetTime(uint32_t NewDelta) {
  parentSeq->time = NewDelta;
  if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->SetDelta(NewDelta);
}

void SeqTrack::AddTime(uint32_t AddDelta) {
  if (parentSeq->bLoadTickByTick) {
    deltaTime += AddDelta;
  }
  else {
    parentSeq->time += AddDelta;
    if (readMode == READMODE_CONVERT_TO_MIDI)
      pMidiTrack->AddDelta(AddDelta);
  }
}

uint32_t SeqTrack::ReadVarLen(uint32_t &offset) {
  register uint32_t value = 0;
  register uint8_t c;

  if (IsValidOffset(offset) && (value = GetByte(offset++)) & 0x80) {
    value &= 0x7F;
    do {
      if (!IsValidOffset(offset))
        break;
      value = (value << 7) + ((c = GetByte(offset++)) & 0x7F);
    } while (c & 0x80);
  }

  return value;
}

void SeqTrack::AddControllerSlide(uint32_t offset,
                                  uint32_t length,
                                  uint32_t dur,
                                  uint8_t &prevVal,
                                  uint8_t targVal,
                                  uint8_t(*scalerFunc)(uint8_t),
                                  void (MidiTrack::*insertFunc)(uint8_t, uint8_t, uint32_t)) {
  if (readMode != READMODE_CONVERT_TO_MIDI)
    return;

  double valInc = (double) ((double) (targVal - prevVal) / (double) dur);
  int8_t newVal = -1;
  for (unsigned int i = 0; i < dur; i++) {
    int8_t prevValInSlide = newVal;

    newVal = roundi(prevVal + (valInc * (i + 1)));
    if (newVal < 0) {
      newVal = 0;
    }
    if (newVal > 127) {
      newVal = 127;
    }
    if (scalerFunc != NULL) {
      newVal = scalerFunc(newVal);
    }

    //only create an event if the pan value has changed since the last iteration
    if (prevValInSlide != newVal) {
      (pMidiTrack->*insertFunc)(channel, newVal, GetTime() + i);
    }
  }
  prevVal = targVal;
}


bool SeqTrack::IsOffsetUsed(uint32_t offset) {
  if (offset <= VisitedAddressMax) {
    auto itrAddress = VisitedAddresses.find(offset);
    if (itrAddress != VisitedAddresses.end()) {
      return true;
    }
  }
  return false;
}


void SeqTrack::OnEvent(uint32_t offset, uint32_t length) {
  if (offset > VisitedAddressMax) {
    VisitedAddressMax = offset;
  }
  VisitedAddresses.insert(offset);
}

void SeqTrack::AddEvent(SeqEvent *pSeqEvent) {
  if (readMode != READMODE_ADD_TO_UI)
    return;

  //if (!bInLoop)
  aEvents.push_back(pSeqEvent);
  //else
  //	delete pSeqEvent;

  // care for a case where the new event is located before the start address
  // (example: Donkey Kong Country - Map, Track 7 of 8)
  if (dwOffset > pSeqEvent->dwOffset) {
    unLength += (dwOffset - pSeqEvent->dwOffset);
    dwOffset = pSeqEvent->dwOffset;
  }

  if (bDetermineTrackLengthEventByEvent) {
    uint32_t length = pSeqEvent->dwOffset + pSeqEvent->unLength - dwOffset;
    if (unLength < length)
      unLength = length;
  }
}

void SeqTrack::AddGenericEvent(uint32_t offset,
                               uint32_t length,
                               const std::wstring &sEventName,
                               const std::wstring &sEventDesc,
                               uint8_t color,
                               Icon icon) {
  OnEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !IsItemAtOffset(offset, false, true)) {
    AddEvent(new SeqEvent(this, offset, length, sEventName, color, icon, sEventDesc));
  }
  else if (readMode == READMODE_CONVERT_TO_MIDI) {
    if (bWriteGenericEventAsTextEvent) {
      wstring miditext(sEventName);
      if (!sEventDesc.empty()) {
        miditext += L" - ";
        miditext += sEventDesc;
      }
      pMidiTrack->AddText(miditext.c_str());
    }
  }
}


void SeqTrack::AddUnknown(uint32_t offset,
                          uint32_t length,
                          const std::wstring &sEventName,
                          const std::wstring &sEventDesc) {
  OnEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !IsItemAtOffset(offset, false, true)) {
    AddEvent(new SeqEvent(this, offset, length, sEventName, CLR_UNKNOWN, ICON_BINARY, sEventDesc));
  }
  else if (readMode == READMODE_CONVERT_TO_MIDI) {
    if (bWriteGenericEventAsTextEvent) {
      wstring miditext(sEventName);
      if (!sEventDesc.empty()) {
        miditext += L" - ";
        miditext += sEventDesc;
      }
      pMidiTrack->AddText(miditext.c_str());
    }
  }
}

void SeqTrack::AddSetOctave(uint32_t offset, uint32_t length, uint8_t newOctave, const std::wstring &sEventName) {
  OnEvent(offset, length);

  octave = newOctave;
  if (readMode == READMODE_ADD_TO_UI && !IsItemAtOffset(offset, false, true))
    AddEvent(new SetOctaveSeqEvent(this, newOctave, offset, length, sEventName));
}

void SeqTrack::AddIncrementOctave(uint32_t offset, uint32_t length, const std::wstring &sEventName) {
  OnEvent(offset, length);

  octave++;
  if (readMode == READMODE_ADD_TO_UI && !IsItemAtOffset(offset, false, true))
    AddEvent(new SeqEvent(this, offset, length, sEventName, CLR_CHANGESTATE));
}

void SeqTrack::AddDecrementOctave(uint32_t offset, uint32_t length, const std::wstring &sEventName) {
  OnEvent(offset, length);

  octave--;
  if (readMode == READMODE_ADD_TO_UI && !IsItemAtOffset(offset, false, true))
    AddEvent(new SeqEvent(this, offset, length, sEventName, CLR_CHANGESTATE));
}

void SeqTrack::AddRest(uint32_t offset, uint32_t length, uint32_t restTime, const std::wstring &sEventName) {
  OnEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !IsItemAtOffset(offset, false, true)) {
    AddEvent(new RestSeqEvent(this, restTime, offset, length, sEventName));
  }
  else if (readMode == READMODE_CONVERT_TO_MIDI) {
    pMidiTrack->PurgePrevNoteOffs();
  }
  AddTime(restTime);
}

void SeqTrack::AddHold(uint32_t offset, uint32_t length, const std::wstring &sEventName) {
  OnEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !IsItemAtOffset(offset, false, true))
    AddEvent(new SeqEvent(this, offset, length, sEventName, CLR_TIE));
}

void SeqTrack::AddNoteOn(uint32_t offset, uint32_t length, int8_t key, int8_t vel, const std::wstring &sEventName) {
  OnEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !IsItemAtOffset(offset, false, true))
    AddEvent(new NoteOnSeqEvent(this, key, vel, offset, length, sEventName));
  AddNoteOnNoItem(key, vel);
}

void SeqTrack::AddNoteOnNoItem(int8_t key, int8_t vel) {
  if (readMode == READMODE_CONVERT_TO_MIDI) {
    uint8_t finalVel = vel;
    if (parentSeq->bUseLinearAmplitudeScale)
      finalVel = Convert7bitPercentVolValToStdMidiVal(vel);

    if (cDrumNote == -1) {
      pMidiTrack->AddNoteOn(channel, key + cKeyCorrection + transpose, finalVel);
    }
    else
      AddPercNoteOnNoItem(cDrumNote, finalVel);
  }
  prevKey = key;
  prevVel = vel;
  return;
}


void SeqTrack::AddPercNoteOn(uint32_t offset, uint32_t length, int8_t key, int8_t vel, const std::wstring &sEventName) {
  uint8_t origChan = channel;
  channel = 9;
  int8_t origDrumNote = cDrumNote;
  cDrumNote = -1;
//	DrumAssoc* pDrumAssoc = parentSeq->GetDrumAssoc(key);
//	if (pDrumAssoc)
//		key = pDrumAssoc->GMDrumNote;
  AddNoteOn(offset, length, key - transpose, vel, sEventName);
  cDrumNote = origDrumNote;
  channel = origChan;
}

void SeqTrack::AddPercNoteOnNoItem(int8_t key, int8_t vel) {
  uint8_t origChan = channel;
  channel = 9;
  int8_t origDrumNote = cDrumNote;
  cDrumNote = -1;
//	DrumAssoc* pDrumAssoc = parentSeq->GetDrumAssoc(key);
//	if (pDrumAssoc)
//		key = pDrumAssoc->GMDrumNote;
  AddNoteOnNoItem(key - transpose, vel);
  cDrumNote = origDrumNote;
  channel = origChan;
}

void SeqTrack::InsertNoteOn(uint32_t offset,
                            uint32_t length,
                            int8_t key,
                            int8_t vel,
                            uint32_t absTime,
                            const std::wstring &sEventName) {
  OnEvent(offset, length);

  uint8_t finalVel = vel;
  if (parentSeq->bUseLinearAmplitudeScale)
    finalVel = Convert7bitPercentVolValToStdMidiVal(vel);

  if (readMode == READMODE_ADD_TO_UI && !IsItemAtOffset(offset, false, true)) {
    AddEvent(new NoteOnSeqEvent(this, key, vel, offset, length, sEventName));
  }
  else if (readMode == READMODE_CONVERT_TO_MIDI) {
    pMidiTrack->InsertNoteOn(channel, key + cKeyCorrection + transpose, finalVel, absTime);
  }
  prevKey = key;
  prevVel = vel;
}

void SeqTrack::AddNoteOff(uint32_t offset, uint32_t length, int8_t key, const std::wstring &sEventName) {
  OnEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !IsItemAtOffset(offset, false, true))
    AddEvent(new NoteOffSeqEvent(this, key, offset, length, sEventName));
  AddNoteOffNoItem(key);
}

void SeqTrack::AddNoteOffNoItem(int8_t key) {
  if (readMode != READMODE_CONVERT_TO_MIDI)
    return;

  if (cDrumNote == -1) {
    pMidiTrack->AddNoteOff(channel, key + cKeyCorrection + transpose);
  }
  else {
    AddPercNoteOffNoItem(cDrumNote);
  }
  return;
}


void SeqTrack::AddPercNoteOff(uint32_t offset, uint32_t length, int8_t key, const std::wstring &sEventName) {
  uint8_t origChan = channel;
  channel = 9;
  int8_t origDrumNote = cDrumNote;
  cDrumNote = -1;
//	DrumAssoc* pDrumAssoc = parentSeq->GetDrumAssoc(key);
//	if (pDrumAssoc)
//		key = pDrumAssoc->GMDrumNote;
  AddNoteOff(offset, length, key - transpose, sEventName);
  cDrumNote = origDrumNote;
  channel = origChan;
}

void SeqTrack::AddPercNoteOffNoItem(int8_t key) {
  uint8_t origChan = channel;
  channel = 9;
  int8_t origDrumNote = cDrumNote;
  cDrumNote = -1;
//	DrumAssoc* pDrumAssoc = parentSeq->GetDrumAssoc(key);
//	if (pDrumAssoc)
//		key = pDrumAssoc->GMDrumNote;
  AddNoteOffNoItem(key - transpose);
  cDrumNote = origDrumNote;
  channel = origChan;
}

void SeqTrack::InsertNoteOff(uint32_t offset,
                             uint32_t length,
                             int8_t key,
                             uint32_t absTime,
                             const std::wstring &sEventName) {
  OnEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !IsItemAtOffset(offset, false, true))
    AddEvent(new NoteOffSeqEvent(this, key, offset, length, sEventName));
  InsertNoteOffNoItem(key, absTime);
}

void SeqTrack::InsertNoteOffNoItem(int8_t key, uint32_t absTime) {
  if (readMode == READMODE_CONVERT_TO_MIDI) {
    pMidiTrack->InsertNoteOff(channel, key + cKeyCorrection + transpose, absTime);
  }
}

void SeqTrack::AddNoteByDur(uint32_t offset,
                            uint32_t length,
                            int8_t key,
                            int8_t vel,
                            uint32_t dur,
                            const std::wstring &sEventName) {
  OnEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !IsItemAtOffset(offset, false, true))
    AddEvent(new DurNoteSeqEvent(this, key + cKeyCorrection, vel, dur, offset, length, sEventName));
  AddNoteByDurNoItem(key, vel, dur);
}

void SeqTrack::AddNoteByDurNoItem(int8_t key, int8_t vel, uint32_t dur) {
  if (readMode == READMODE_CONVERT_TO_MIDI) {
    uint8_t finalVel = vel;
    if (parentSeq->bUseLinearAmplitudeScale)
      finalVel = Convert7bitPercentVolValToStdMidiVal(vel);

    if (cDrumNote == -1) {
      pMidiTrack->AddNoteByDur(channel, key + cKeyCorrection + transpose, finalVel, dur);
    }
    else
      AddPercNoteByDurNoItem(cDrumNote, vel, dur);
  }
  prevKey = key;
  prevVel = vel;
  return;
}

void SeqTrack::AddNoteByDur_Extend(uint32_t offset,
                                   uint32_t length,
                                   int8_t key,
                                   int8_t vel,
                                   uint32_t dur,
                                   const std::wstring &sEventName) {
  OnEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !IsItemAtOffset(offset, false, true))
    AddEvent(new DurNoteSeqEvent(this, key, vel, dur, offset, length, sEventName));
  AddNoteByDurNoItem_Extend(key, vel, dur);
}

void SeqTrack::AddNoteByDurNoItem_Extend(int8_t key, int8_t vel, uint32_t dur) {
  if (readMode == READMODE_CONVERT_TO_MIDI) {
    uint8_t finalVel = vel;
    if (parentSeq->bUseLinearAmplitudeScale)
      finalVel = Convert7bitPercentVolValToStdMidiVal(vel);

    if (cDrumNote == -1) {
      pMidiTrack->AddNoteByDur_TriAce(channel, key + cKeyCorrection + transpose, finalVel, dur);
    }
    else
      AddPercNoteByDurNoItem(cDrumNote, vel, dur);
  }
  prevKey = key;
  prevVel = vel;
  return;
}

void SeqTrack::AddPercNoteByDur(uint32_t offset,
                                uint32_t length,
                                int8_t key,
                                int8_t vel,
                                uint32_t dur,
                                const std::wstring &sEventName) {
  uint8_t origChan = channel;
  channel = 9;
  int8_t origDrumNote = cDrumNote;
  cDrumNote = -1;
//	DrumAssoc* pDrumAssoc = parentSeq->GetDrumAssoc(key);
//	if (pDrumAssoc)
//		key = pDrumAssoc->GMDrumNote;
  AddNoteByDur(offset, length, key - transpose, vel, dur, sEventName);
  cDrumNote = origDrumNote;
  channel = origChan;
}

void SeqTrack::AddPercNoteByDurNoItem(int8_t key, int8_t vel, uint32_t dur) {
  uint8_t origChan = channel;
  channel = 9;
  int8_t origDrumNote = cDrumNote;
  cDrumNote = -1;
//	DrumAssoc* pDrumAssoc = parentSeq->GetDrumAssoc(key);
//	if (pDrumAssoc)
//		key = pDrumAssoc->GMDrumNote;
  AddNoteByDurNoItem(key - transpose, vel, dur);
  cDrumNote = origDrumNote;
  channel = origChan;
}

/*void SeqTrack::AddNoteByDur(uint32_t offset, uint32_t length, int8_t key, int8_t vel, uint32_t dur, uint8_t chan, const std::wstring& sEventName)
{
	uint8_t origChan = channel;
	channel = chan;
	AddNoteByDur(offset, length, key, vel, dur, selectMsg, sEventName);
	channel = origChan;
}*/

void SeqTrack::InsertNoteByDur(uint32_t offset,
                               uint32_t length,
                               int8_t key,
                               int8_t vel,
                               uint32_t dur,
                               uint32_t absTime,
                               const std::wstring &sEventName) {
  OnEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !IsItemAtOffset(offset, false, true)) {
    AddEvent(new DurNoteSeqEvent(this, key, vel, dur, offset, length, sEventName));
  }
  else if (readMode == READMODE_CONVERT_TO_MIDI) {
    uint8_t finalVel = vel;
    if (parentSeq->bUseLinearAmplitudeScale)
      finalVel = Convert7bitPercentVolValToStdMidiVal(vel);

    pMidiTrack->InsertNoteByDur(channel, key + cKeyCorrection + transpose, finalVel, dur, absTime);
  }
  prevKey = key;
  prevVel = vel;
}

void SeqTrack::MakePrevDurNoteEnd() {
  MakePrevDurNoteEnd(GetTime() + (parentSeq->bLoadTickByTick ? deltaTime : 0));
}

void SeqTrack::MakePrevDurNoteEnd(uint32_t absTime) {
  if (readMode == READMODE_CONVERT_TO_MIDI) {
    for (auto it = pMidiTrack->prevDurNoteOffs.begin(); it != pMidiTrack->prevDurNoteOffs.end(); ++it) {
      (*it)->AbsTime = absTime;
    }
  }
}

void SeqTrack::LimitPrevDurNoteEnd() {
  LimitPrevDurNoteEnd(GetTime() + (parentSeq->bLoadTickByTick ? deltaTime : 0));
}

void SeqTrack::LimitPrevDurNoteEnd(uint32_t absTime) {
  if (readMode == READMODE_CONVERT_TO_MIDI) {
    for (auto it = pMidiTrack->prevDurNoteOffs.begin(); it != pMidiTrack->prevDurNoteOffs.end(); ++it) {
      if ((*it)->AbsTime > absTime) {
        (*it)->AbsTime = absTime;
      }
    }
  }
}

void SeqTrack::AddVol(uint32_t offset, uint32_t length, uint8_t newVol, const std::wstring &sEventName) {
  OnEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !IsItemAtOffset(offset, false, true))
    AddEvent(new VolSeqEvent(this, newVol, offset, length, sEventName));
  AddVolNoItem(newVol);
}

void SeqTrack::AddVolNoItem(uint8_t newVol) {
  if (readMode == READMODE_CONVERT_TO_MIDI) {
    uint8_t finalVol = newVol;
    if (parentSeq->bUseLinearAmplitudeScale)
      finalVol = Convert7bitPercentVolValToStdMidiVal(newVol);

    pMidiTrack->AddVol(channel, finalVol);
  }
  vol = newVol;
  return;
}

void SeqTrack::AddVolSlide(uint32_t offset,
                           uint32_t length,
                           uint32_t dur,
                           uint8_t targVol,
                           const std::wstring &sEventName) {
  OnEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !IsItemAtOffset(offset, false, true))
    AddEvent(new VolSlideSeqEvent(this, targVol, dur, offset, length, sEventName));
  else if (readMode == READMODE_CONVERT_TO_MIDI)
    AddControllerSlide(offset,
                       length,
                       dur,
                       vol,
                       targVol,
                       parentSeq->bUseLinearAmplitudeScale ? Convert7bitPercentVolValToStdMidiVal : NULL,
                       &MidiTrack::InsertVol);
}

void SeqTrack::InsertVol(uint32_t offset,
                         uint32_t length,
                         uint8_t newVol,
                         uint32_t absTime,
                         const std::wstring &sEventName) {
  OnEvent(offset, length);

  uint8_t finalVol = newVol;
  if (parentSeq->bUseLinearAmplitudeScale)
    finalVol = Convert7bitPercentVolValToStdMidiVal(newVol);

  if (readMode == READMODE_ADD_TO_UI && !IsItemAtOffset(offset, false, true))
    AddEvent(new VolSeqEvent(this, newVol, offset, length, sEventName));
  else if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->InsertVol(channel, finalVol, absTime);
  vol = newVol;
}

void SeqTrack::AddExpression(uint32_t offset, uint32_t length, uint8_t level, const std::wstring &sEventName) {
  OnEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !IsItemAtOffset(offset, false, true))
    AddEvent(new ExpressionSeqEvent(this, level, offset, length, sEventName));
  AddExpressionNoItem(level);
}

void SeqTrack::AddExpressionNoItem(uint8_t level) {
  if (readMode == READMODE_CONVERT_TO_MIDI) {
    uint8_t finalExpression = level;
    if (parentSeq->bUseLinearAmplitudeScale)
      finalExpression = Convert7bitPercentVolValToStdMidiVal(level);

    pMidiTrack->AddExpression(channel, finalExpression);
  }
  expression = level;
}

void SeqTrack::AddExpressionSlide(uint32_t offset,
                                  uint32_t length,
                                  uint32_t dur,
                                  uint8_t targExpr,
                                  const std::wstring &sEventName) {
  OnEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !IsItemAtOffset(offset, false, true))
    AddEvent(new ExpressionSlideSeqEvent(this, targExpr, dur, offset, length, sEventName));
  else if (readMode == READMODE_CONVERT_TO_MIDI)
    AddControllerSlide(offset,
                       length,
                       dur,
                       expression,
                       targExpr,
                       parentSeq->bUseLinearAmplitudeScale ? Convert7bitPercentVolValToStdMidiVal : NULL,
                       &MidiTrack::InsertExpression);
}

void SeqTrack::InsertExpression(uint32_t offset,
                                uint32_t length,
                                uint8_t level,
                                uint32_t absTime,
                                const std::wstring &sEventName) {
  OnEvent(offset, length);

  uint8_t finalExpression = level;
  if (parentSeq->bUseLinearAmplitudeScale)
    finalExpression = Convert7bitPercentVolValToStdMidiVal(level);

  if (readMode == READMODE_ADD_TO_UI && !IsItemAtOffset(offset, false, true))
    AddEvent(new ExpressionSeqEvent(this, level, offset, length, sEventName));
  else if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->InsertExpression(channel, finalExpression, absTime);
  expression = level;
}


void SeqTrack::AddMasterVol(uint32_t offset, uint32_t length, uint8_t newVol, const std::wstring &sEventName) {
  OnEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !IsItemAtOffset(offset, false, true))
    AddEvent(new MastVolSeqEvent(this, newVol, offset, length, sEventName));
  AddMasterVolNoItem(newVol);
}

void SeqTrack::AddMasterVolNoItem(uint8_t newVol) {
  if (readMode == READMODE_CONVERT_TO_MIDI) {
    uint8_t finalVol = newVol;
    if (parentSeq->bUseLinearAmplitudeScale)
      finalVol = Convert7bitPercentVolValToStdMidiVal(newVol);

    pMidiTrack->AddMasterVol(channel, finalVol);
  }
  mastVol = newVol;
}

void SeqTrack::AddMastVolSlide(uint32_t offset,
                               uint32_t length,
                               uint32_t dur,
                               uint8_t targVol,
                               const std::wstring &sEventName) {
  OnEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !IsItemAtOffset(offset, false, true))
    AddEvent(new MastVolSlideSeqEvent(this, targVol, dur, offset, length, sEventName));
  else if (readMode == READMODE_CONVERT_TO_MIDI)
    AddControllerSlide(offset,
                       length,
                       dur,
                       mastVol,
                       targVol,
                       parentSeq->bUseLinearAmplitudeScale ? Convert7bitPercentVolValToStdMidiVal : NULL,
                       &MidiTrack::InsertMasterVol);
}

void SeqTrack::AddPan(uint32_t offset, uint32_t length, uint8_t pan, const std::wstring &sEventName) {
  OnEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !IsItemAtOffset(offset, false, true))
    AddEvent(new PanSeqEvent(this, pan, offset, length, sEventName));
  AddPanNoItem(pan);
}

void SeqTrack::AddPanNoItem(uint8_t pan) {
  if (readMode == READMODE_CONVERT_TO_MIDI) {
    pMidiTrack->AddPan(channel, pan);
  }
  prevPan = pan;
}

void SeqTrack::AddPanSlide(uint32_t offset,
                           uint32_t length,
                           uint32_t dur,
                           uint8_t targPan,
                           const std::wstring &sEventName) {
  OnEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !IsItemAtOffset(offset, false, true))
    AddEvent(new PanSlideSeqEvent(this, targPan, dur, offset, length, sEventName));
  else if (readMode == READMODE_CONVERT_TO_MIDI)
    AddControllerSlide(offset, length, dur, prevPan, targPan, NULL, &MidiTrack::InsertPan);
}


void SeqTrack::InsertPan(uint32_t offset,
                         uint32_t length,
                         uint8_t pan,
                         uint32_t absTime,
                         const std::wstring &sEventName) {
  OnEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !IsItemAtOffset(offset, false, true))
    AddEvent(new PanSeqEvent(this, pan, offset, length, sEventName));
  else if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->InsertPan(channel, pan, absTime);
}

void SeqTrack::AddReverb(uint32_t offset, uint32_t length, uint8_t reverb, const std::wstring &sEventName) {
  OnEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !IsItemAtOffset(offset, false, true))
    AddEvent(new ReverbSeqEvent(this, reverb, offset, length, sEventName));
  AddReverbNoItem(reverb);
}

void SeqTrack::AddReverbNoItem(uint8_t reverb) {
  if (readMode == READMODE_CONVERT_TO_MIDI) {
    pMidiTrack->AddReverb(channel, reverb);
  }
  prevReverb = reverb;
}

void SeqTrack::AddMonoNoItem() {
  if (readMode == READMODE_CONVERT_TO_MIDI) {
    pMidiTrack->AddMono(channel);
  }
}

void SeqTrack::InsertReverb(uint32_t offset,
                            uint32_t length,
                            uint8_t reverb,
                            uint32_t absTime,
                            const std::wstring &sEventName) {
  OnEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !IsItemAtOffset(offset, false, true))
    AddEvent(new ReverbSeqEvent(this, reverb, offset, length, sEventName));
  else if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->InsertReverb(channel, reverb, absTime);
}

void SeqTrack::AddPitchBendMidiFormat(uint32_t offset,
                                      uint32_t length,
                                      uint8_t lo,
                                      uint8_t hi,
                                      const std::wstring &sEventName) {
  AddPitchBend(offset, length, lo + (hi << 7) - 0x2000, sEventName);
}

void SeqTrack::AddPitchBend(uint32_t offset, uint32_t length, int16_t bend, const std::wstring &sEventName) {
  OnEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !IsItemAtOffset(offset, false, true))
    AddEvent(new PitchBendSeqEvent(this, bend, offset, length, sEventName));
  else if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->AddPitchBend(channel, bend);
}

void SeqTrack::AddPitchBendRange(uint32_t offset,
                                 uint32_t length,
                                 uint8_t semitones,
                                 uint8_t cents,
                                 const std::wstring &sEventName) {
  OnEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !IsItemAtOffset(offset, false, true))
    AddEvent(new PitchBendRangeSeqEvent(this, semitones, cents, offset, length, sEventName));
  else if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->AddPitchBendRange(channel, semitones, cents);
}

void SeqTrack::AddPitchBendRangeNoItem(uint8_t semitones, uint8_t cents) {
  if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->AddPitchBendRange(channel, semitones, cents);
}

void SeqTrack::AddFineTuning(uint32_t offset, uint32_t length, double cents, const std::wstring &sEventName) {
  OnEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !IsItemAtOffset(offset, false, true))
    AddEvent(new FineTuningSeqEvent(this, cents, offset, length, sEventName));
  else if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->AddFineTuning(channel, cents);
}

void SeqTrack::AddFineTuningNoItem(double cents) {
  if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->AddFineTuning(channel, cents);
}

void SeqTrack::AddModulationDepthRange(uint32_t offset,
                                       uint32_t length,
                                       double semitones,
                                       const std::wstring &sEventName) {
  OnEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !IsItemAtOffset(offset, false, true))
    AddEvent(new ModulationDepthRangeSeqEvent(this, semitones, offset, length, sEventName));
  else if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->AddModulationDepthRange(channel, semitones);
}

void SeqTrack::AddModulationDepthRangeNoItem(double semitones) {
  if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->AddModulationDepthRange(channel, semitones);
}

void SeqTrack::AddTranspose(uint32_t offset, uint32_t length, int8_t theTranspose, const std::wstring &sEventName) {
  OnEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !IsItemAtOffset(offset, false, true))
    AddEvent(new TransposeSeqEvent(this, theTranspose, offset, length, sEventName));
  transpose = theTranspose;
}


void SeqTrack::AddModulation(uint32_t offset, uint32_t length, uint8_t depth, const std::wstring &sEventName) {
  OnEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !IsItemAtOffset(offset, false, true))
    AddEvent(new ModulationSeqEvent(this, depth, offset, length, sEventName));
  else if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->AddModulation(channel, depth);
}

void SeqTrack::InsertModulation(uint32_t offset,
                                uint32_t length,
                                uint8_t depth,
                                uint32_t absTime,
                                const std::wstring &sEventName) {
  OnEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !IsItemAtOffset(offset, false, true))
    AddEvent(new ModulationSeqEvent(this, depth, offset, length, sEventName));
  else if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->InsertModulation(channel, depth, absTime);
}

void SeqTrack::AddBreath(uint32_t offset, uint32_t length, uint8_t depth, const std::wstring &sEventName) {
  OnEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !IsItemAtOffset(offset, false, true))
    AddEvent(new BreathSeqEvent(this, depth, offset, length, sEventName));
  else if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->AddBreath(channel, depth);
}

void SeqTrack::InsertBreath(uint32_t offset,
                            uint32_t length,
                            uint8_t depth,
                            uint32_t absTime,
                            const std::wstring &sEventName) {
  OnEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !IsItemAtOffset(offset, false, true))
    AddEvent(new BreathSeqEvent(this, depth, offset, length, sEventName));
  else if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->InsertBreath(channel, depth, absTime);
}

void SeqTrack::AddSustainEvent(uint32_t offset, uint32_t length, uint8_t depth, const std::wstring &sEventName) {
  OnEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !IsItemAtOffset(offset, false, true))
    AddEvent(new SustainSeqEvent(this, depth, offset, length, sEventName));
  else if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->AddSustain(channel, depth);
}

void SeqTrack::InsertSustainEvent(uint32_t offset,
                                  uint32_t length,
                                  uint8_t depth,
                                  uint32_t absTime,
                                  const std::wstring &sEventName) {
  OnEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !IsItemAtOffset(offset, false, true))
    AddEvent(new SustainSeqEvent(this, depth, offset, length, sEventName));
  else if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->InsertSustain(channel, depth, absTime);
}

void SeqTrack::AddPortamento(uint32_t offset, uint32_t length, bool bOn, const std::wstring &sEventName) {
  OnEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !IsItemAtOffset(offset, false, true))
    AddEvent(new PortamentoSeqEvent(this, bOn, offset, length, sEventName));
  AddPortamentoNoItem(bOn);
}

void SeqTrack::AddPortamentoNoItem(bool bOn) {
  if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->AddPortamento(channel, bOn);
}

void SeqTrack::InsertPortamento(uint32_t offset,
                                uint32_t length,
                                bool bOn,
                                uint32_t absTime,
                                const std::wstring &sEventName) {
  OnEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !IsItemAtOffset(offset, false, true))
    AddEvent(new PortamentoSeqEvent(this, bOn, offset, length, sEventName));
  else if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->InsertPortamento(channel, bOn, absTime);
}

void SeqTrack::AddPortamentoTime(uint32_t offset, uint32_t length, uint8_t time, const std::wstring &sEventName) {
  OnEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !IsItemAtOffset(offset, false, true))
    AddEvent(new PortamentoTimeSeqEvent(this, time, offset, length, sEventName));
  AddPortamentoTimeNoItem(time);
}

void SeqTrack::AddPortamentoTimeNoItem(uint8_t time) {
  if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->AddPortamentoTime(channel, time);
}

void SeqTrack::InsertPortamentoTime(uint32_t offset,
                                    uint32_t length,
                                    uint8_t time,
                                    uint32_t absTime,
                                    const std::wstring &sEventName) {
  OnEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !IsItemAtOffset(offset, false, true))
    AddEvent(new PortamentoTimeSeqEvent(this, time, offset, length, sEventName));
  else if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->InsertPortamentoTime(channel, time, absTime);
}


/*void InsertNoteOnEvent(int8_t key, int8_t vel, uint32_t absTime);
void AddNoteOffEvent(int8_t key);
void InsertNoteOffEvent(int8_t key, int8_t vel, uint32_t absTime);
void AddNoteByDur(int8_t key, int8_t vel);
void InsertNoteByDur(int8_t key, int8_t vel, uint32_t absTime);
void AddVolumeEvent(uint8_t vol);
void InsertVolumeEvent(uint8_t vol, uint32_t absTime);
void AddExpression(uint8_t expression);
void InsertExpression(uint8_t expression, uint32_t absTime);
void AddPanEvent(uint8_t pan);
void InsertPanEvent(uint8_t pan, uint32_t absTime);*/

void SeqTrack::AddProgramChange(uint32_t offset, uint32_t length, uint32_t progNum, const std::wstring &sEventName) {
  AddProgramChange(offset, length, progNum, false, sEventName);
}

void SeqTrack::AddProgramChange(uint32_t offset,
                                uint32_t length,
                                uint32_t progNum,
                                uint8_t chan,
                                const std::wstring &sEventName) {
  AddProgramChange(offset, length, progNum, false, chan, sEventName);
}

void SeqTrack::AddProgramChange(uint32_t offset,
                                uint32_t length,
                                uint32_t progNum,
                                bool requireBank,
                                const std::wstring &sEventName) {
  OnEvent(offset, length);

/*	InstrAssoc* pInstrAssoc = parentSeq->GetInstrAssoc(progNum);
	if (pInstrAssoc)
	{
		if (pInstrAssoc->drumNote == -1)		//if this program uses a drum note
		{
			progNum = pInstrAssoc->GMProgNum;
			cKeyCorrection = pInstrAssoc->keyCorrection;
			cDrumNote = -1;
		}
		else
			cDrumNote = pInstrAssoc->drumNote;
	}
	else
		cDrumNote = -1;
*/
  if (readMode == READMODE_ADD_TO_UI) {
    if (!IsItemAtOffset(offset, false, true)) {
      AddEvent(new ProgChangeSeqEvent(this, progNum, offset, length, sEventName));
    }
    parentSeq->AddInstrumentRef(progNum);
  }
  AddProgramChangeNoItem(progNum, requireBank);
}

void SeqTrack::AddProgramChange(uint32_t offset,
                                uint32_t length,
                                uint32_t progNum,
                                bool requireBank,
                                uint8_t chan,
                                const std::wstring &sEventName) {
  //if (selectMsg = NULL)
  //	selectMsg.Forma
  uint8_t origChan = channel;
  channel = chan;
  AddProgramChange(offset, length, progNum, requireBank, sEventName);
  channel = origChan;
}

void SeqTrack::AddProgramChangeNoItem(uint32_t progNum, bool requireBank) {
  if (readMode == READMODE_CONVERT_TO_MIDI) {
    if (requireBank) {
      pMidiTrack->AddBankSelect(channel, (progNum >> 14) & 0x7f);
      pMidiTrack->AddBankSelectFine(channel, (progNum >> 7) & 0x7f);
    }
    pMidiTrack->AddProgramChange(channel, progNum & 0x7f);
  }
}

void SeqTrack::AddBankSelectNoItem(uint8_t bank) {
  if (readMode == READMODE_CONVERT_TO_MIDI) {
    pMidiTrack->AddBankSelect(channel, bank / 128);
    pMidiTrack->AddBankSelectFine(channel, bank % 128);
  }
}

void SeqTrack::AddTempo(uint32_t offset, uint32_t length, uint32_t microsPerQuarter, const std::wstring &sEventName) {
  OnEvent(offset, length);

  double bpm = 60000000.0 / microsPerQuarter;
  if (readMode == READMODE_ADD_TO_UI && !IsItemAtOffset(offset, false, true))
    AddEvent(new TempoSeqEvent(this, bpm, offset, length, sEventName));
  AddTempoNoItem(microsPerQuarter);
}

void SeqTrack::AddTempoNoItem(uint32_t microsPerQuarter) {
  parentSeq->tempoBPM = 60000000.0 / microsPerQuarter;
  if (readMode == READMODE_CONVERT_TO_MIDI) {
    // Some MIDI tool can recognise tempo event only in the first track.
    MidiTrack *pFirstMidiTrack = parentSeq->GetFirstMidiTrack();
    pFirstMidiTrack->InsertTempo(microsPerQuarter, pMidiTrack->GetDelta());
  }
}

void SeqTrack::AddTempoSlide(uint32_t offset,
                             uint32_t length,
                             uint32_t dur,
                             uint32_t targMicrosPerQuarter,
                             const std::wstring &sEventName) {
  AddTempoBPMSlide(offset, length, dur, ((double) 60000000 / targMicrosPerQuarter), sEventName);
}

void SeqTrack::AddTempoBPM(uint32_t offset, uint32_t length, double bpm, const std::wstring &sEventName) {
  OnEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !IsItemAtOffset(offset, false, true))
    AddEvent(new TempoSeqEvent(this, bpm, offset, length, sEventName));
  AddTempoBPMNoItem(bpm);
}

void SeqTrack::AddTempoBPMNoItem(double bpm) {
  parentSeq->tempoBPM = bpm;
  if (readMode == READMODE_CONVERT_TO_MIDI) {
    // Some MIDI tool can recognise tempo event only in the first track.
    MidiTrack *pFirstMidiTrack = parentSeq->GetFirstMidiTrack();
    pFirstMidiTrack->InsertTempoBPM(bpm, pMidiTrack->GetDelta());
  }
}

void SeqTrack::AddTempoBPMSlide(uint32_t offset,
                                uint32_t length,
                                uint32_t dur,
                                double targBPM,
                                const std::wstring &sEventName) {
  OnEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !IsItemAtOffset(offset, false, true))
    AddEvent(new TempoSlideSeqEvent(this, targBPM, dur, offset, length, sEventName));
  else if (readMode == READMODE_CONVERT_TO_MIDI) {
    double tempoInc = (targBPM - parentSeq->tempoBPM) / ((double) dur);
    double newTempo;
    for (unsigned int i = 0; i < dur; i++) {
      // Some MIDI software only recognise tempo events in the first track.
      MidiTrack *pFirstMidiTrack = parentSeq->GetFirstMidiTrack();
      newTempo = parentSeq->tempoBPM + (tempoInc * (i + 1));
      pFirstMidiTrack->InsertTempoBPM(newTempo, GetTime() + i);
    }
  }
  parentSeq->tempoBPM = targBPM;
}

void SeqTrack::AddTimeSig(uint32_t offset,
                          uint32_t length,
                          uint8_t numer,
                          uint8_t denom,
                          uint8_t ticksPerQuarter,
                          const std::wstring &sEventName) {
  OnEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !IsItemAtOffset(offset, false, true)) {
    AddEvent(new TimeSigSeqEvent(this, numer, denom, ticksPerQuarter, offset, length, sEventName));
  }
  AddTimeSigNoItem(numer, denom, ticksPerQuarter);
}

void SeqTrack::AddTimeSigNoItem(uint8_t numer, uint8_t denom, uint8_t ticksPerQuarter) {
  if (readMode == READMODE_CONVERT_TO_MIDI) {
    MidiTrack *pFirstMidiTrack = parentSeq->GetFirstMidiTrack();
    pFirstMidiTrack->AddTimeSig(numer, denom, ticksPerQuarter);
  }
}

void SeqTrack::InsertTimeSig(uint32_t offset,
                             uint32_t length,
                             uint8_t numer,
                             uint8_t denom,
                             uint8_t ticksPerQuarter,
                             uint32_t absTime,
                             const std::wstring &sEventName) {
  OnEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !IsItemAtOffset(offset, false, true)) {
    AddEvent(new TimeSigSeqEvent(this, numer, denom, ticksPerQuarter, offset, length, sEventName));
  }
  else if (readMode == READMODE_CONVERT_TO_MIDI) {
    MidiTrack *pFirstMidiTrack = parentSeq->GetFirstMidiTrack();
    pFirstMidiTrack->InsertTimeSig(numer, denom, ticksPerQuarter, absTime);
  }
}

bool SeqTrack::AddEndOfTrack(uint32_t offset, uint32_t length, const std::wstring &sEventName) {
  OnEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !IsItemAtOffset(offset, false, true))
    AddEvent(new TrackEndSeqEvent(this, offset, length, sEventName));
  else if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->AddEndOfTrack();
  return AddEndOfTrackNoItem();
}

bool SeqTrack::AddEndOfTrackNoItem() {
  if (readMode == READMODE_FIND_DELTA_LENGTH)
    deltaLength = GetTime();
  return false;
}

void SeqTrack::AddGlobalTranspose(uint32_t offset, uint32_t length, int8_t semitones, const std::wstring &sEventName) {
  OnEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !IsItemAtOffset(offset, false, true))
    AddEvent(new TransposeSeqEvent(this, semitones, offset, length, sEventName));
  else if (readMode == READMODE_CONVERT_TO_MIDI)
    parentSeq->midi->globalTrack.InsertGlobalTranspose(GetTime(), semitones);
  //pMidiTrack->(channel, transpose);
}

void SeqTrack::AddMarker(uint32_t offset,
                         uint32_t length,
                         const string &markername,
                         uint8_t databyte1,
                         uint8_t databyte2,
                         const std::wstring &sEventName,
                         int8_t priority,
                         uint8_t color) {
  OnEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !IsItemAtOffset(offset, false, true))
    AddEvent(new MarkerSeqEvent(this, markername, databyte1, databyte2, offset, length, sEventName, color));
  else if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->AddMarker(channel, markername, databyte1, databyte2, priority);
}

// when in FIND_DELTA_LENGTH mode, returns true until we've hit the max number of loops defined in options
bool SeqTrack::AddLoopForever(uint32_t offset, uint32_t length, const std::wstring &sEventName) {
  OnEvent(offset, length);

  this->foreverLoops++;
  if (readMode == READMODE_ADD_TO_UI) {
    if (!IsItemAtOffset(offset, false, true))
      AddEvent(new LoopForeverSeqEvent(this, offset, length, sEventName));
    return false;
  }
  else if (readMode == READMODE_FIND_DELTA_LENGTH) {
    deltaLength = GetTime();
    return (this->foreverLoops < ConversionOptions::GetNumSequenceLoops());
  }
  return true;

}
