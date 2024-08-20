/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "SeqTrack.h"
#include "SeqEvent.h"
#include "ScaleConversion.h"
#include "Options.h"
#include "helper.h"

//  ********
//  SeqTrack
//  ********

SeqTrack::SeqTrack(VGMSeq *parentFile, uint32_t offset, uint32_t length, std::string name)
    : VGMItem(parentFile, offset, length, std::move(name)),
      dwStartOffset(offset),
      parentSeq(parentFile),
      pMidiTrack(nullptr),
      bMonophonic(parentSeq->usesMonophonicTracks()),
      panVolumeCorrectionRate(1.0),
      bDetermineTrackLengthEventByEvent(false),
      bWriteGenericEventAsTextEvent(false) {
  SeqTrack::resetVars();
}

void SeqTrack::resetVars() {
  active = true;
  bInLoop = false;
  foreverLoops = 0;
  totalTicks = -1;
  deltaTime = 0;
  vol = 100;
  expression = 127;
  mastVol = 127;
  prevPan = 64;
  prevReverb = 40;
  channelGroup = 0;
  transpose = 0;
  cDrumNote = -1;
  cKeyCorrection = 0;
  panVolumeCorrectionRate = 1.0;
}

void SeqTrack::resetVisitedAddresses() {
  visitedAddresses.clear();
  visitedAddresses.reserve(8192);
  visitedAddressMax = 0;
}

bool SeqTrack::readEvent() {
  return false;        //by default, don't add any events, just stop immediately.
}

bool SeqTrack::loadTrackInit(int trackNum, MidiTrack *preparedMidiTrack) {
  resetVisitedAddresses();
  resetVars();
  if (readMode == READMODE_CONVERT_TO_MIDI) {
    if (preparedMidiTrack != nullptr) {
      pMidiTrack = preparedMidiTrack;
    }
    else {
      pMidiTrack = parentSeq->midi->addTrack();
    }
  }
  setChannelAndGroupFromTrkNum(trackNum);

  curOffset = dwStartOffset;    //start at beginning of track

  if (readMode == READMODE_CONVERT_TO_MIDI) {
    if (preparedMidiTrack == nullptr) {
      addInitialMidiEvents(trackNum);
    }
  }
  return true;
}

void SeqTrack::loadTrackMainLoop(uint32_t stopOffset, int32_t stopTime) {
  if (!active) {
    return;
  }

  if (stopTime == -1) {
    stopTime = 0x7FFFFFFF;
  }

  if (getTime() >= static_cast<uint32_t>(stopTime)) {
    active = false;
    return;
  }

  if (parentSeq->bLoadTickByTick) {
    onTickBegin();

    if (deltaTime > 0) {
      deltaTime--;
    }

    while (deltaTime == 0) {
      if (curOffset >= stopOffset) {
        if (readMode == READMODE_FIND_DELTA_LENGTH)
          totalTicks = getTime();

        active = false;
        break;
      }

      if (!readEvent()) {
        totalTicks = getTime();
        active = false;
        break;
      }
    }

    onTickEnd();
  }
  else {
    while (curOffset < stopOffset && getTime() < static_cast<uint32_t>(stopTime)) {
      if (!readEvent()) {
        active = false;
        break;
      }
    }

    if (readMode == READMODE_FIND_DELTA_LENGTH) {
      totalTicks = getTime();
    }
  }
}

void SeqTrack::setChannelAndGroupFromTrkNum(int theTrackNum) {
  if (theTrackNum > 39)
    theTrackNum += 3;
  else if (theTrackNum > 23)
    theTrackNum += 2;                    //compensate for channel 10 - drum track.  we'll skip it, by default
  else if (theTrackNum > 8)
    theTrackNum++;                        //''
  channel = theTrackNum % 16;
  channelGroup = theTrackNum / 16;
  if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->setChannelGroup(channelGroup);
}

void SeqTrack::addInitialMidiEvents(int trackNum) {
  if (trackNum == 0)
    pMidiTrack->addSeqName(parentSeq->name());
  std::string ssTrackName = fmt::format("Track: 0x{:02X}", dwStartOffset);
  pMidiTrack->addTrackName(ssTrackName);

  pMidiTrack->addMidiPort(channelGroup);

  if (trackNum == 0) {
    pMidiTrack->addGMReset();
    pMidiTrack->addGM2Reset();
    if (parentSeq->alwaysWriteInitialTempo())
      pMidiTrack->addTempoBPM(parentSeq->initialTempoBPM);
  }
  if (parentSeq->alwaysWriteInitialVol())
    addVolNoItem(parentSeq->initialVolume());
  if (parentSeq->alwaysWriteInitialExpression())
    addExpressionNoItem(parentSeq->initialExpression());
  if (parentSeq->alwaysWriteInitialReverb())
    addReverbNoItem(parentSeq->initialReverbLevel());
  if (parentSeq->alwaysWriteInitialPitchBendRange())
    addPitchBendRangeNoItem(parentSeq->initialPitchBendRange());
  if (parentSeq->alwaysWriteInitialMonoMode())
    addMonoNoItem();
}

uint32_t SeqTrack::getTime() const {
  return parentSeq->time;
}

void SeqTrack::setTime(uint32_t NewDelta) const {
  parentSeq->time = NewDelta;
  if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->setDelta(NewDelta);
}

void SeqTrack::addTime(uint32_t AddDelta) {
  if (parentSeq->bLoadTickByTick) {
    deltaTime += AddDelta;
  }
  else {
    parentSeq->time += AddDelta;
    if (readMode == READMODE_CONVERT_TO_MIDI)
      pMidiTrack->addDelta(AddDelta);
  }
}

uint32_t SeqTrack::readVarLen(uint32_t &offset) const {
  uint32_t value = 0;

  while (isValidOffset(offset)) {
	  uint8_t c = readByte(offset++);
    value = (value << 7) + (c & 0x7F);
	  // Check if continue bit is set
	  if((c & 0x80) == 0) {
		  break;
	  }
  }
  return value;
}

void SeqTrack::addControllerSlide(uint32_t dur,
                                  uint8_t &prevVal,
                                  uint8_t targVal,
                                  uint8_t(*scalerFunc)(uint8_t),
                                  void (MidiTrack::*insertFunc)(uint8_t, uint8_t, uint32_t)) const {
  if (readMode != READMODE_CONVERT_TO_MIDI)
    return;

  double valInc = static_cast<double>(targVal - prevVal) / dur;
  int8_t newVal = -1;
  for (unsigned int i = 0; i < dur; i++) {
    int8_t prevValInSlide = newVal;

    newVal = std::round(prevVal + (valInc * (i + 1)));
    if (newVal < 0) {
      newVal = 0;
    }
    if (newVal > 127) {
      newVal = 127;
    }
    if (scalerFunc != nullptr) {
      newVal = scalerFunc(newVal);
    }

    //only create an event if the pan value has changed since the last iteration
    if (prevValInSlide != newVal) {
      (pMidiTrack->*insertFunc)(channel, newVal, getTime() + i);
    }
  }
  prevVal = targVal;
}


bool SeqTrack::isOffsetUsed(uint32_t offset) {
  if (offset <= visitedAddressMax) {
    auto itrAddress = visitedAddresses.find(offset);
    if (itrAddress != visitedAddresses.end()) {
      return true;
    }
  }
  return false;
}


void SeqTrack::onEvent(uint32_t offset, uint32_t length) {
  if (offset > visitedAddressMax) {
    visitedAddressMax = offset;
  }
  visitedAddresses.insert(offset);
}

void SeqTrack::addEvent(SeqEvent *pSeqEvent) {
  if (readMode != READMODE_ADD_TO_UI)
    return;

  addChild(pSeqEvent);

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

void SeqTrack::addGenericEvent(uint32_t offset,
                               uint32_t length,
                               const std::string &sEventName,
                               const std::string &sEventDesc,
                               EventColor color,
                               Icon icon) {
  onEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !isItemAtOffset(offset, true)) {
    addEvent(new SeqEvent(this, offset, length, sEventName, color, icon, sEventDesc));
  }
  else if (readMode == READMODE_CONVERT_TO_MIDI) {
    if (bWriteGenericEventAsTextEvent) {
      std::string miditext(sEventName);
      if (!sEventDesc.empty()) {
        miditext += " - ";
        miditext += sEventDesc;
      }
      pMidiTrack->addText(miditext);
    }
  }
}


void SeqTrack::addUnknown(uint32_t offset,
                          uint32_t length,
                          const std::string &sEventName,
                          const std::string &sEventDesc) {
  onEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !isItemAtOffset(offset, true)) {
    addEvent(new SeqEvent(this, offset, length, sEventName, CLR_UNKNOWN, ICON_BINARY, sEventDesc));
  }
  else if (readMode == READMODE_CONVERT_TO_MIDI) {
    if (bWriteGenericEventAsTextEvent) {
      std::string miditext(sEventName);
      if (!sEventDesc.empty()) {
        miditext += " - ";
        miditext += sEventDesc;
      }
      pMidiTrack->addText(miditext);
    }
  }
}

void SeqTrack::addSetOctave(uint32_t offset, uint32_t length, uint8_t newOctave, const std::string &sEventName) {
  onEvent(offset, length);

  octave = newOctave;
  if (readMode == READMODE_ADD_TO_UI && !isItemAtOffset(offset, true))
    addEvent(new SetOctaveSeqEvent(this, newOctave, offset, length, sEventName));
}

void SeqTrack::addIncrementOctave(uint32_t offset, uint32_t length, const std::string &sEventName) {
  onEvent(offset, length);

  octave++;
  if (readMode == READMODE_ADD_TO_UI && !isItemAtOffset(offset, true))
    addEvent(new SeqEvent(this, offset, length, sEventName, CLR_CHANGESTATE));
}

void SeqTrack::addDecrementOctave(uint32_t offset, uint32_t length, const std::string &sEventName) {
  onEvent(offset, length);

  octave--;
  if (readMode == READMODE_ADD_TO_UI && !isItemAtOffset(offset, true))
    addEvent(new SeqEvent(this, offset, length, sEventName, CLR_CHANGESTATE));
}

void SeqTrack::addRest(uint32_t offset, uint32_t length, uint32_t restTime, const std::string &sEventName) {
  onEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !isItemAtOffset(offset, true)) {
    addEvent(new RestSeqEvent(this, restTime, offset, length, sEventName));
  }
  else if (readMode == READMODE_CONVERT_TO_MIDI) {
    pMidiTrack->purgePrevNoteOffs();
  }
  addTime(restTime);
}

void SeqTrack::addHold(uint32_t offset, uint32_t length, const std::string &sEventName) {
  onEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !isItemAtOffset(offset, true))
    addEvent(new SeqEvent(this, offset, length, sEventName, CLR_TIE));
}

void SeqTrack::addNoteOn(uint32_t offset, uint32_t length, int8_t key, int8_t vel, const std::string &sEventName) {
  onEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !isItemAtOffset(offset, true))
    addEvent(new NoteOnSeqEvent(this, key, vel, offset, length, sEventName));
  addNoteOnNoItem(key, vel);
}

void SeqTrack::addNoteOnNoItem(int8_t key, int8_t velocity) {
  if (readMode == READMODE_CONVERT_TO_MIDI) {
    uint8_t finalVel = velocity;
    if (parentSeq->usesLinearAmplitudeScale())
      finalVel = convert7bitPercentVolValToStdMidiVal(velocity);

    if (cDrumNote == -1) {
      pMidiTrack->addNoteOn(channel, key + cKeyCorrection + transpose, finalVel);
    }
    else
      addPercNoteOnNoItem(cDrumNote, finalVel);
  }
  prevKey = key;
  prevVel = velocity;
}


void SeqTrack::addPercNoteOn(uint32_t offset, uint32_t length, int8_t key, int8_t vel, const std::string &sEventName) {
  uint8_t origChan = channel;
  channel = 9;
  int8_t origDrumNote = cDrumNote;
  cDrumNote = -1;
//	DrumAssoc* pDrumAssoc = parentSeq->GetDrumAssoc(key);
//	if (pDrumAssoc)
//		key = pDrumAssoc->GMDrumNote;
  addNoteOn(offset, length, key - transpose, vel, sEventName);
  cDrumNote = origDrumNote;
  channel = origChan;
}

void SeqTrack::addPercNoteOnNoItem(int8_t key, int8_t vel) {
  uint8_t origChan = channel;
  channel = 9;
  int8_t origDrumNote = cDrumNote;
  cDrumNote = -1;
//	DrumAssoc* pDrumAssoc = parentSeq->GetDrumAssoc(key);
//	if (pDrumAssoc)
//		key = pDrumAssoc->GMDrumNote;
  addNoteOnNoItem(key - transpose, vel);
  cDrumNote = origDrumNote;
  channel = origChan;
}

void SeqTrack::insertNoteOn(uint32_t offset,
                            uint32_t length,
                            int8_t key,
                            int8_t vel,
                            uint32_t absTime,
                            const std::string &sEventName) {
  onEvent(offset, length);

  uint8_t finalVel = vel;
  if (parentSeq->usesLinearAmplitudeScale())
    finalVel = convert7bitPercentVolValToStdMidiVal(vel);

  if (readMode == READMODE_ADD_TO_UI && !isItemAtOffset(offset, true)) {
    addEvent(new NoteOnSeqEvent(this, key, vel, offset, length, sEventName));
  }
  else if (readMode == READMODE_CONVERT_TO_MIDI) {
    pMidiTrack->insertNoteOn(channel, key + cKeyCorrection + transpose, finalVel, absTime);
  }
  prevKey = key;
  prevVel = vel;
}

void SeqTrack::addNoteOff(uint32_t offset, uint32_t length, int8_t key, const std::string &sEventName) {
  onEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !isItemAtOffset(offset, true))
    addEvent(new NoteOffSeqEvent(this, key, offset, length, sEventName));
  addNoteOffNoItem(key);
}

void SeqTrack::addNoteOffNoItem(int8_t key) {
  if (readMode != READMODE_CONVERT_TO_MIDI)
    return;

  if (cDrumNote == -1) {
    pMidiTrack->addNoteOff(channel, key + cKeyCorrection + transpose);
  }
  else {
    addPercNoteOffNoItem(cDrumNote);
  }
}


void SeqTrack::addPercNoteOff(uint32_t offset, uint32_t length, int8_t key, const std::string &sEventName) {
  uint8_t origChan = channel;
  channel = 9;
  int8_t origDrumNote = cDrumNote;
  cDrumNote = -1;
//	DrumAssoc* pDrumAssoc = parentSeq->GetDrumAssoc(key);
//	if (pDrumAssoc)
//		key = pDrumAssoc->GMDrumNote;
  addNoteOff(offset, length, key - transpose, sEventName);
  cDrumNote = origDrumNote;
  channel = origChan;
}

void SeqTrack::addPercNoteOffNoItem(int8_t key) {
  uint8_t origChan = channel;
  channel = 9;
  int8_t origDrumNote = cDrumNote;
  cDrumNote = -1;
//	DrumAssoc* pDrumAssoc = parentSeq->GetDrumAssoc(key);
//	if (pDrumAssoc)
//		key = pDrumAssoc->GMDrumNote;
  addNoteOffNoItem(key - transpose);
  cDrumNote = origDrumNote;
  channel = origChan;
}

void SeqTrack::insertNoteOff(uint32_t offset,
                             uint32_t length,
                             int8_t key,
                             uint32_t absTime,
                             const std::string &sEventName) {
  onEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !isItemAtOffset(offset, true))
    addEvent(new NoteOffSeqEvent(this, key, offset, length, sEventName));
  insertNoteOffNoItem(key, absTime);
}

void SeqTrack::insertNoteOffNoItem(int8_t key, uint32_t absTime) const {
  if (readMode == READMODE_CONVERT_TO_MIDI) {
    pMidiTrack->insertNoteOff(channel, key + cKeyCorrection + transpose, absTime);
  }
}

void SeqTrack::addNoteByDur(uint32_t offset,
                            uint32_t length,
                            int8_t key,
                            int8_t vel,
                            uint32_t dur,
                            const std::string &sEventName) {
  onEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !isItemAtOffset(offset, true))
    addEvent(new DurNoteSeqEvent(this, key + cKeyCorrection, vel, dur, offset, length, sEventName));
  addNoteByDurNoItem(key, vel, dur);
}

void SeqTrack::addNoteByDurNoItem(int8_t key, int8_t vel, uint32_t dur) {
  if (readMode == READMODE_CONVERT_TO_MIDI) {
    uint8_t finalVel = vel;
    if (parentSeq->usesLinearAmplitudeScale())
      finalVel = convert7bitPercentVolValToStdMidiVal(vel);

    if (cDrumNote == -1) {
      pMidiTrack->addNoteByDur(channel, key + cKeyCorrection + transpose, finalVel, dur);
    }
    else
      addPercNoteByDurNoItem(cDrumNote, vel, dur);
  }
  prevKey = key;
  prevVel = vel;
}

void SeqTrack::addNoteByDur_Extend(uint32_t offset,
                                   uint32_t length,
                                   int8_t key,
                                   int8_t vel,
                                   uint32_t dur,
                                   const std::string &sEventName) {
  onEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !isItemAtOffset(offset, true))
    addEvent(new DurNoteSeqEvent(this, key, vel, dur, offset, length, sEventName));
  addNoteByDurNoItem_Extend(key, vel, dur);
}

void SeqTrack::addNoteByDurNoItem_Extend(int8_t key, int8_t vel, uint32_t dur) {
  if (readMode == READMODE_CONVERT_TO_MIDI) {
    uint8_t finalVel = vel;
    if (parentSeq->usesLinearAmplitudeScale())
      finalVel = convert7bitPercentVolValToStdMidiVal(vel);

    if (cDrumNote == -1) {
      pMidiTrack->addNoteByDur_TriAce(channel, key + cKeyCorrection + transpose, finalVel, dur);
    }
    else
      addPercNoteByDurNoItem(cDrumNote, vel, dur);
  }
  prevKey = key;
  prevVel = vel;
}

void SeqTrack::addPercNoteByDur(uint32_t offset,
                                uint32_t length,
                                int8_t key,
                                int8_t vel,
                                uint32_t dur,
                                const std::string &sEventName) {
  uint8_t origChan = channel;
  channel = 9;
  int8_t origDrumNote = cDrumNote;
  cDrumNote = -1;
//	DrumAssoc* pDrumAssoc = parentSeq->GetDrumAssoc(key);
//	if (pDrumAssoc)
//		key = pDrumAssoc->GMDrumNote;
  addNoteByDur(offset, length, key - transpose, vel, dur, sEventName);
  cDrumNote = origDrumNote;
  channel = origChan;
}

void SeqTrack::addPercNoteByDurNoItem(int8_t key, int8_t vel, uint32_t dur) {
  uint8_t origChan = channel;
  channel = 9;
  int8_t origDrumNote = cDrumNote;
  cDrumNote = -1;
//	DrumAssoc* pDrumAssoc = parentSeq->GetDrumAssoc(key);
//	if (pDrumAssoc)
//		key = pDrumAssoc->GMDrumNote;
  addNoteByDurNoItem(key - transpose, vel, dur);
  cDrumNote = origDrumNote;
  channel = origChan;
}

/*void SeqTrack::AddNoteByDur(uint32_t offset, uint32_t length, int8_t key, int8_t vel, uint32_t dur, uint8_t chan, const std::string& sEventName)
{
	uint8_t origChan = channel;
	channel = chan;
	AddNoteByDur(offset, length, key, vel, dur, selectMsg, sEventName);
	channel = origChan;
}*/

void SeqTrack::insertNoteByDur(uint32_t offset,
                               uint32_t length,
                               int8_t key,
                               int8_t vel,
                               uint32_t dur,
                               uint32_t absTime,
                               const std::string &sEventName) {
  onEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !isItemAtOffset(offset, true)) {
    addEvent(new DurNoteSeqEvent(this, key, vel, dur, offset, length, sEventName));
  }
  else if (readMode == READMODE_CONVERT_TO_MIDI) {
    uint8_t finalVel = vel;
    if (parentSeq->usesLinearAmplitudeScale())
      finalVel = convert7bitPercentVolValToStdMidiVal(vel);

    pMidiTrack->insertNoteByDur(channel, key + cKeyCorrection + transpose, finalVel, dur, absTime);
  }
  prevKey = key;
  prevVel = vel;
}

void SeqTrack::makePrevDurNoteEnd() const {
  makePrevDurNoteEnd(getTime() + (parentSeq->bLoadTickByTick ? deltaTime : 0));
}

void SeqTrack::makePrevDurNoteEnd(uint32_t absTime) const {
  if (readMode == READMODE_CONVERT_TO_MIDI) {
    for (auto& prevDurNoteOff : pMidiTrack->prevDurNoteOffs) {
      prevDurNoteOff->absTime = absTime;
    }
  }
}

void SeqTrack::limitPrevDurNoteEnd() const {
  limitPrevDurNoteEnd(getTime() + (parentSeq->bLoadTickByTick ? deltaTime : 0));
}

void SeqTrack::limitPrevDurNoteEnd(uint32_t absTime) const {
  if (readMode == READMODE_CONVERT_TO_MIDI) {
    for (auto& prevDurNoteOff : pMidiTrack->prevDurNoteOffs) {
      if (prevDurNoteOff->absTime > absTime) {
        prevDurNoteOff->absTime = absTime;
      }
    }
  }
}

void SeqTrack::addVol(uint32_t offset, uint32_t length, uint8_t newVol, const std::string &sEventName) {
  onEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !isItemAtOffset(offset, true))
    addEvent(new VolSeqEvent(this, newVol, offset, length, sEventName));
  addVolNoItem(newVol);
}

void SeqTrack::addVolNoItem(uint8_t newVol) {
  if (readMode == READMODE_CONVERT_TO_MIDI) {
    double newVolPercent = newVol / 127.0;
    if (parentSeq->panVolumeCorrectionMode == PanVolumeCorrectionMode::kAdjustVolumeController)
      newVolPercent *= panVolumeCorrectionRate;
    if (parentSeq->usesLinearAmplitudeScale())
      newVolPercent = sqrt(newVolPercent);

    const uint8_t finalVol = static_cast<uint8_t>(std::min(newVolPercent * 127, 127.0));
    pMidiTrack->addVol(channel, finalVol);
  }
  vol = newVol;
}

void SeqTrack::addVolume14Bit(uint32_t offset, uint32_t length, uint16_t volume, const std::string &sEventName) {
  onEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !isItemAtOffset(offset, true))
    addEvent(new Volume14BitSeqEvent(this, volume, offset, length, sEventName));
  addVolume14BitNoItem(volume);
}

// Add a 14 bit volume event by writing to controllers 39 and 7. Note that we assume the parameter
// is in the range of 0 -> 127^2, but we normalize the value to the full 14 bit range: 0 -> (1 << 14) - 1
void SeqTrack::addVolume14BitNoItem(uint16_t volume) {
  if (readMode == READMODE_CONVERT_TO_MIDI) {
    double newVolPercent = std::min(volume / (127.0 * 127.0), 1.0);
    if (parentSeq->panVolumeCorrectionMode == PanVolumeCorrectionMode::kAdjustVolumeController)
      newVolPercent *= panVolumeCorrectionRate;
    if (parentSeq->usesLinearAmplitudeScale())
      newVolPercent = sqrt(newVolPercent);

    const uint16_t finalVol = static_cast<uint16_t>(std::min(newVolPercent * 16383.0, 16383.0));
    uint8_t volume_hi = static_cast<uint8_t>((finalVol >> 7) & 0x7F);
    uint8_t volume_lo = static_cast<uint8_t>(finalVol & 0x7F);
    pMidiTrack->addVolumeFine(channel, volume_lo);
    pMidiTrack->addVol(channel, volume_hi);
  }
  vol = static_cast<uint8_t>((volume >> 7) & 0x7F);
}

void SeqTrack::addVolSlide(uint32_t offset,
                           uint32_t length,
                           uint32_t dur,
                           uint8_t targVol,
                           const std::string &sEventName) {
  onEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !isItemAtOffset(offset, true))
    addEvent(new VolSlideSeqEvent(this, targVol, dur, offset, length, sEventName));
  else if (readMode == READMODE_CONVERT_TO_MIDI)
    addControllerSlide(dur,
                       vol,
                       targVol,
                       parentSeq->usesLinearAmplitudeScale() ? convert7bitPercentVolValToStdMidiVal : nullptr,
                       &MidiTrack::insertVol);
}

void SeqTrack::insertVol(uint32_t offset,
                         uint32_t length,
                         uint8_t newVol,
                         uint32_t absTime,
                         const std::string &sEventName) {
  onEvent(offset, length);

  double newVolPercent = newVol / 127.0;
  if (parentSeq->panVolumeCorrectionMode == PanVolumeCorrectionMode::kAdjustVolumeController)
    newVolPercent *= panVolumeCorrectionRate;
  if (parentSeq->usesLinearAmplitudeScale())
    newVolPercent = sqrt(newVolPercent);

  const uint8_t finalVol = static_cast<uint8_t>(std::min(newVolPercent * 127, 127.0));
  if (readMode == READMODE_ADD_TO_UI && !isItemAtOffset(offset, true))
    addEvent(new VolSeqEvent(this, newVol, offset, length, sEventName));
  else if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->insertVol(channel, finalVol, absTime);
  vol = newVol;
}

void SeqTrack::addExpression(uint32_t offset, uint32_t length, uint8_t level, const std::string &sEventName) {
  onEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !isItemAtOffset(offset, true))
    addEvent(new ExpressionSeqEvent(this, level, offset, length, sEventName));
  addExpressionNoItem(level);
}

void SeqTrack::addExpressionNoItem(uint8_t level) {
  if (readMode == READMODE_CONVERT_TO_MIDI) {
    double newVolPercent = level / 127.0;
    if (parentSeq->panVolumeCorrectionMode == PanVolumeCorrectionMode::kAdjustExpressionController)
      newVolPercent *= panVolumeCorrectionRate;
    if (parentSeq->usesLinearAmplitudeScale())
      newVolPercent = sqrt(newVolPercent);

    const uint8_t finalExpression = static_cast<uint8_t>(std::min(newVolPercent * 127, 127.0));
    pMidiTrack->addExpression(channel, finalExpression);
  }
  expression = level;
}

void SeqTrack::addExpressionSlide(uint32_t offset,
                                  uint32_t length,
                                  uint32_t dur,
                                  uint8_t targExpr,
                                  const std::string &sEventName) {
  onEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !isItemAtOffset(offset, true))
    addEvent(new ExpressionSlideSeqEvent(this, targExpr, dur, offset, length, sEventName));
  else if (readMode == READMODE_CONVERT_TO_MIDI)
    addControllerSlide(dur,
                       expression,
                       targExpr,
                       parentSeq->usesLinearAmplitudeScale() ? convert7bitPercentVolValToStdMidiVal : nullptr,
                       &MidiTrack::insertExpression);
}

void SeqTrack::insertExpression(uint32_t offset,
                                uint32_t length,
                                uint8_t level,
                                uint32_t absTime,
                                const std::string &sEventName) {
  onEvent(offset, length);

  double newVolPercent = level / 127.0;
  if (parentSeq->panVolumeCorrectionMode == PanVolumeCorrectionMode::kAdjustExpressionController)
    newVolPercent *= panVolumeCorrectionRate;
  if (parentSeq->usesLinearAmplitudeScale())
    newVolPercent = sqrt(newVolPercent);

  const uint8_t finalExpression = static_cast<uint8_t>(std::min(newVolPercent * 127, 127.0));
  if (readMode == READMODE_ADD_TO_UI && !isItemAtOffset(offset, true))
    addEvent(new ExpressionSeqEvent(this, level, offset, length, sEventName));
  else if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->insertExpression(channel, finalExpression, absTime);
  expression = level;
}


void SeqTrack::addMasterVol(uint32_t offset, uint32_t length, uint8_t newVol, const std::string &sEventName) {
  onEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !isItemAtOffset(offset, true))
    addEvent(new MastVolSeqEvent(this, newVol, offset, length, sEventName));
  addMasterVolNoItem(newVol);
}

void SeqTrack::addMasterVolNoItem(uint8_t newVol) {
  if (readMode == READMODE_CONVERT_TO_MIDI) {
    uint8_t finalVol = newVol;
    if (parentSeq->usesLinearAmplitudeScale())
      finalVol = convert7bitPercentVolValToStdMidiVal(newVol);

    pMidiTrack->addMasterVol(channel, finalVol);
  }
  mastVol = newVol;
}

void SeqTrack::addMastVolSlide(uint32_t offset,
                               uint32_t length,
                               uint32_t dur,
                               uint8_t targVol,
                               const std::string &sEventName) {
  onEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !isItemAtOffset(offset, true))
    addEvent(new MastVolSlideSeqEvent(this, targVol, dur, offset, length, sEventName));
  else if (readMode == READMODE_CONVERT_TO_MIDI)
    addControllerSlide(dur,
                       mastVol,
                       targVol,
                       parentSeq->usesLinearAmplitudeScale() ? convert7bitPercentVolValToStdMidiVal : nullptr,
                       &MidiTrack::insertMasterVol);
}

void SeqTrack::addPan(uint32_t offset, uint32_t length, uint8_t pan, const std::string &sEventName) {
  onEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !isItemAtOffset(offset, true))
    addEvent(new PanSeqEvent(this, pan, offset, length, sEventName));
  addPanNoItem(pan);
}

void SeqTrack::addPanNoItem(uint8_t pan) {
  if (readMode == READMODE_CONVERT_TO_MIDI) {
    const uint8_t midiPan = parentSeq->usesLinearAmplitudeScale()
      ? convert7bitLinearPercentPanValToStdMidiVal(pan, &panVolumeCorrectionRate)
      : pan;
    pMidiTrack->addPan(channel, midiPan);

    switch (parentSeq->panVolumeCorrectionMode) {
    case PanVolumeCorrectionMode::kAdjustVolumeController:
      addVolNoItem(vol);
      break;

    case PanVolumeCorrectionMode::kAdjustExpressionController:
      addExpressionNoItem(expression);
      break;

    default:
      break;
    }
  }
  prevPan = pan;
}

void SeqTrack::addPanSlide(uint32_t offset,
                           uint32_t length,
                           uint32_t dur,
                           uint8_t targPan,
                           const std::string &sEventName) {
  onEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !isItemAtOffset(offset, true))
    addEvent(new PanSlideSeqEvent(this, targPan, dur, offset, length, sEventName));
  else if (readMode == READMODE_CONVERT_TO_MIDI)
    addControllerSlide(dur, prevPan, targPan, nullptr, &MidiTrack::insertPan);
}


void SeqTrack::insertPan(uint32_t offset,
                         uint32_t length,
                         uint8_t pan,
                         uint32_t absTime,
                         const std::string &sEventName) {
  onEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !isItemAtOffset(offset, true))
    addEvent(new PanSeqEvent(this, pan, offset, length, sEventName));
  else if (readMode == READMODE_CONVERT_TO_MIDI) {
    const uint8_t midiPan = parentSeq->usesLinearAmplitudeScale()
      ? convert7bitLinearPercentPanValToStdMidiVal(pan, &panVolumeCorrectionRate)
      : pan;
    pMidiTrack->insertPan(channel, midiPan, absTime);

    // TODO: (bugfix) Pan volume compensation does not work properly when using pan slider and volume slider at the same time
    switch (parentSeq->panVolumeCorrectionMode) {
    case PanVolumeCorrectionMode::kAdjustVolumeController:
      insertVol(offset, length, vol, absTime);
      break;

    case PanVolumeCorrectionMode::kAdjustExpressionController:
      insertExpression(offset, length, expression, absTime);
      break;

    default:
      break;
    }
  }
}

void SeqTrack::addReverb(uint32_t offset, uint32_t length, uint8_t reverb, const std::string &sEventName) {
  onEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !isItemAtOffset(offset, true))
    addEvent(new ReverbSeqEvent(this, reverb, offset, length, sEventName));
  addReverbNoItem(reverb);
}

void SeqTrack::addReverbNoItem(uint8_t reverb) {
  if (readMode == READMODE_CONVERT_TO_MIDI) {
    pMidiTrack->addReverb(channel, reverb);
  }
  prevReverb = reverb;
}

void SeqTrack::addMonoNoItem() const {
  if (readMode == READMODE_CONVERT_TO_MIDI) {
    pMidiTrack->addMono(channel);
  }
}

void SeqTrack::insertReverb(uint32_t offset,
                            uint32_t length,
                            uint8_t reverb,
                            uint32_t absTime,
                            const std::string &sEventName) {
  onEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !isItemAtOffset(offset, true))
    addEvent(new ReverbSeqEvent(this, reverb, offset, length, sEventName));
  else if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->insertReverb(channel, reverb, absTime);
}

void SeqTrack::addPitchBendMidiFormat(uint32_t offset,
                                      uint32_t length,
                                      uint8_t lo,
                                      uint8_t hi,
                                      const std::string &sEventName) {
  addPitchBend(offset, length, lo + (hi << 7) - 0x2000, sEventName);
}

void SeqTrack::addPitchBend(uint32_t offset, uint32_t length, int16_t bend, const std::string &sEventName) {
  onEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !isItemAtOffset(offset, true))
    addEvent(new PitchBendSeqEvent(this, bend, offset, length, sEventName));
  else if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->addPitchBend(channel, bend);
}

void SeqTrack::addPitchBendAsPercent(uint32_t offset, uint32_t length, double percent, const std::string &sEventName) {
  const s16 minVal = -8192;
  const s16 maxVal = 8191;
  const s16 bendVal = static_cast<s16>(percent * 8192);
  s16 bend = std::clamp(bendVal, minVal, maxVal);
  addPitchBend(offset, length, bend, sEventName);
}

void SeqTrack::addPitchBendRange(uint32_t offset, uint32_t length, uint16_t cents, const std::string &sEventName) {
  onEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !isItemAtOffset(offset, true))
    addEvent(new PitchBendRangeSeqEvent(this, cents, offset, length, sEventName));
  else if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->addPitchBendRange(channel, cents);
}

void SeqTrack::addPitchBendRangeNoItem(uint16_t cents) const {
  if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->addPitchBendRange(channel, cents);
}

void SeqTrack::addFineTuning(uint32_t offset, uint32_t length, double cents, const std::string &sEventName) {
  onEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !isItemAtOffset(offset, true))
    addEvent(new FineTuningSeqEvent(this, cents, offset, length, sEventName));
  else if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->addFineTuning(channel, cents);
}

void SeqTrack::addFineTuningNoItem(double cents) const {
  if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->addFineTuning(channel, cents);
}

void SeqTrack::addModulationDepthRange(uint32_t offset,
                                       uint32_t length,
                                       double semitones,
                                       const std::string &sEventName) {
  onEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !isItemAtOffset(offset, true))
    addEvent(new ModulationDepthRangeSeqEvent(this, semitones, offset, length, sEventName));
  else if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->addModulationDepthRange(channel, semitones);
}

void SeqTrack::addModulationDepthRangeNoItem(double semitones) const {
  if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->addModulationDepthRange(channel, semitones);
}

void SeqTrack::addTranspose(uint32_t offset, uint32_t length, int8_t theTranspose, const std::string &sEventName) {
  onEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !isItemAtOffset(offset, true))
    addEvent(new TransposeSeqEvent(this, theTranspose, offset, length, sEventName));
  transpose = theTranspose;
}


void SeqTrack::addModulation(uint32_t offset, uint32_t length, uint8_t depth, const std::string &sEventName) {
  onEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !isItemAtOffset(offset, true))
    addEvent(new ModulationSeqEvent(this, depth, offset, length, sEventName));
  else if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->addModulation(channel, depth);
}

void SeqTrack::insertModulation(uint32_t offset,
                                uint32_t length,
                                uint8_t depth,
                                uint32_t absTime,
                                const std::string &sEventName) {
  onEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !isItemAtOffset(offset, true))
    addEvent(new ModulationSeqEvent(this, depth, offset, length, sEventName));
  else if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->insertModulation(channel, depth, absTime);
}

void SeqTrack::addBreath(uint32_t offset, uint32_t length, uint8_t depth, const std::string &sEventName) {
  onEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !isItemAtOffset(offset, true))
    addEvent(new BreathSeqEvent(this, depth, offset, length, sEventName));
  else if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->addBreath(channel, depth);
}

void SeqTrack::insertBreath(uint32_t offset,
                            uint32_t length,
                            uint8_t depth,
                            uint32_t absTime,
                            const std::string &sEventName) {
  onEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !isItemAtOffset(offset, true))
    addEvent(new BreathSeqEvent(this, depth, offset, length, sEventName));
  else if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->insertBreath(channel, depth, absTime);
}

void SeqTrack::addSustainEvent(uint32_t offset, uint32_t length, uint8_t depth, const std::string &sEventName) {
  onEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !isItemAtOffset(offset, true))
    addEvent(new SustainSeqEvent(this, depth, offset, length, sEventName));
  else if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->addSustain(channel, depth);
}

void SeqTrack::insertSustainEvent(uint32_t offset,
                                  uint32_t length,
                                  uint8_t depth,
                                  uint32_t absTime,
                                  const std::string &sEventName) {
  onEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !isItemAtOffset(offset, true))
    addEvent(new SustainSeqEvent(this, depth, offset, length, sEventName));
  else if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->insertSustain(channel, depth, absTime);
}

void SeqTrack::addPortamento(uint32_t offset, uint32_t length, bool bOn, const std::string &sEventName) {
  onEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !isItemAtOffset(offset, true))
    addEvent(new PortamentoSeqEvent(this, bOn, offset, length, sEventName));
  addPortamentoNoItem(bOn);
}

void SeqTrack::addPortamentoNoItem(bool bOn) const {
  if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->addPortamento(channel, bOn);
}

void SeqTrack::insertPortamento(uint32_t offset,
                                uint32_t length,
                                bool bOn,
                                uint32_t absTime,
                                const std::string &sEventName) {
  onEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !isItemAtOffset(offset, true))
    addEvent(new PortamentoSeqEvent(this, bOn, offset, length, sEventName));
  else if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->insertPortamento(channel, bOn, absTime);
}

void SeqTrack::insertPortamentoNoItem(bool bOn, uint32_t absTime) const {
  if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->insertPortamento(channel, bOn, absTime);
}

void SeqTrack::addPortamentoTime(uint32_t offset, uint32_t length, uint8_t time, const std::string &sEventName) {
  onEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !isItemAtOffset(offset, true))
    addEvent(new PortamentoTimeSeqEvent(this, time, offset, length, sEventName));
  addPortamentoTimeNoItem(time);
}

void SeqTrack::addPortamentoTimeNoItem(uint8_t time) const {
  if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->addPortamentoTime(channel, time);
}

void SeqTrack::addPortamentoTime14Bit(uint32_t offset, uint32_t length, uint16_t time, const std::string &sEventName) {
  onEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !isItemAtOffset(offset, true))
    addEvent(new PortamentoTimeSeqEvent(this, time, offset, length, sEventName));
  addPortamentoTime14BitNoItem(time);
}

void SeqTrack::addPortamentoTime14BitNoItem(uint16_t time) const {
  if (readMode == READMODE_CONVERT_TO_MIDI) {
    uint8_t lsb = time & 127;
    uint8_t msb = (time >> 7) & 127;
    pMidiTrack->addPortamentoTimeFine(channel, lsb);
    pMidiTrack->addPortamentoTime(channel, msb);
  }
}

void SeqTrack::insertPortamentoTime(uint32_t offset,
                                    uint32_t length,
                                    uint8_t time,
                                    uint32_t absTime,
                                    const std::string &sEventName) {
  onEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !isItemAtOffset(offset, true))
    addEvent(new PortamentoTimeSeqEvent(this, time, offset, length, sEventName));
  insertPortamentoTimeNoItem(time, absTime);
}

void SeqTrack::insertPortamentoTimeNoItem(uint8_t time, uint32_t absTime) const {
  if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->insertPortamentoTime(channel, time, absTime);
}

void SeqTrack::addPortamentoControlNoItem(uint8_t key) const {
  if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->addPortamentoControl(channel, key);
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

void SeqTrack::addProgramChange(uint32_t offset, uint32_t length, uint32_t progNum, const std::string &sEventName) {
  addProgramChange(offset, length, progNum, false, sEventName);
}

void SeqTrack::addProgramChange(uint32_t offset,
                                uint32_t length,
                                uint32_t progNum,
                                uint8_t chan,
                                const std::string &sEventName) {
  addProgramChange(offset, length, progNum, false, chan, sEventName);
}

void SeqTrack::addProgramChange(uint32_t offset,
                                uint32_t length,
                                uint32_t progNum,
                                bool requireBank,
                                const std::string &sEventName) {
  onEvent(offset, length);

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
    if (!isItemAtOffset(offset, true)) {
      addEvent(new ProgChangeSeqEvent(this, progNum, offset, length, sEventName));
    }
    parentSeq->addInstrumentRef(progNum);
  }
  addProgramChangeNoItem(progNum, requireBank);
}

void SeqTrack::addProgramChange(uint32_t offset,
                                uint32_t length,
                                uint32_t progNum,
                                bool requireBank,
                                uint8_t chan,
                                const std::string &sEventName) {
  //if (selectMsg = NULL)
  //	selectMsg.Forma
  uint8_t origChan = channel;
  channel = chan;
  addProgramChange(offset, length, progNum, requireBank, sEventName);
  channel = origChan;
}

void SeqTrack::addProgramChangeNoItem(uint32_t progNum, bool requireBank) const {
  if (readMode == READMODE_CONVERT_TO_MIDI) {
    if (requireBank) {
      if (auto style = ConversionOptions::the().bankSelectStyle();
          style == BankSelectStyle::GS) {
        pMidiTrack->addBankSelect(channel, (progNum >> 7) & 0x7f);
      } else if (style == BankSelectStyle::MMA) {
        pMidiTrack->addBankSelect(channel, (progNum >> 14) & 0x7f);
        pMidiTrack->addBankSelectFine(channel, (progNum >> 7) & 0x7f);
      }
    }
    pMidiTrack->addProgramChange(channel, progNum & 0x7f);
  }
}

void SeqTrack::addBankSelect(uint32_t offset, uint32_t length, uint8_t bank, const std::string& sEventName) {
  onEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI) {
    addEvent(new BankSelectSeqEvent(this, bank, offset, length, sEventName));
  }
  addBankSelectNoItem(bank);
}

void SeqTrack::addBankSelectNoItem(uint8_t bank) const {
  if (readMode == READMODE_CONVERT_TO_MIDI) {
    if (auto style = ConversionOptions::the().bankSelectStyle();
        style == BankSelectStyle::GS) {
      pMidiTrack->addBankSelect(channel, bank & 0x7f);
    } else if (style == BankSelectStyle::MMA) {
      pMidiTrack->addBankSelect(channel, bank >> 7);
      pMidiTrack->addBankSelectFine(channel, bank & 0x7f);
    }
  }
}

void SeqTrack::addTempo(uint32_t offset, uint32_t length, uint32_t microsPerQuarter, const std::string &sEventName) {
  onEvent(offset, length);

  double bpm = 60000000.0 / microsPerQuarter;
  if (readMode == READMODE_ADD_TO_UI && !isItemAtOffset(offset, true))
    addEvent(new TempoSeqEvent(this, bpm, offset, length, sEventName));
  addTempoNoItem(microsPerQuarter);
}

void SeqTrack::addTempoNoItem(uint32_t microsPerQuarter) const {
  parentSeq->tempoBPM = 60000000.0 / microsPerQuarter;
  if (readMode == READMODE_CONVERT_TO_MIDI) {
    // Some MIDI engines only recognise tempo events in the first track.
    MidiTrack *pFirstMidiTrack = parentSeq->firstMidiTrack();
    pFirstMidiTrack->insertTempo(microsPerQuarter, pMidiTrack->getDelta());
  }
}

void SeqTrack::insertTempo(uint32_t offset,
                           uint32_t length,
                           uint32_t microsPerQuarter,
                           uint32_t absTime,
                           const std::string &sEventName) {
  onEvent(offset, length);

  double bpm = 60000000.0 / microsPerQuarter;
  if (readMode == READMODE_ADD_TO_UI && !isItemAtOffset(offset, true))
    addEvent(new TempoSeqEvent(this, bpm, offset, length, sEventName));
  insertTempoNoItem(microsPerQuarter, absTime);
}

void SeqTrack::insertTempoNoItem(uint32_t microsPerQuarter, uint32_t absTime) const {
  if (readMode == READMODE_CONVERT_TO_MIDI) {
    // Some MIDI tool can recognise tempo event only in the first track.
    MidiTrack *pFirstMidiTrack = parentSeq->firstMidiTrack();
    pFirstMidiTrack->insertTempo(microsPerQuarter, absTime);
  }
}

void SeqTrack::addTempoSlide(uint32_t offset,
                             uint32_t length,
                             uint32_t dur,
                             uint32_t targMicrosPerQuarter,
                             const std::string &sEventName) {
  addTempoBPMSlide(offset, length, dur, (60000000.0 / targMicrosPerQuarter), sEventName);
}

void SeqTrack::addTempoBPM(uint32_t offset, uint32_t length, double bpm, const std::string &sEventName) {
  onEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !isItemAtOffset(offset, true))
    addEvent(new TempoSeqEvent(this, bpm, offset, length, sEventName));
  addTempoBPMNoItem(bpm);
}

void SeqTrack::addTempoBPMNoItem(double bpm) const {
  parentSeq->tempoBPM = bpm;
  if (readMode == READMODE_CONVERT_TO_MIDI) {
    // Some MIDI tool can recognise tempo event only in the first track.
    MidiTrack *pFirstMidiTrack = parentSeq->firstMidiTrack();
    pFirstMidiTrack->insertTempoBPM(bpm, pMidiTrack->getDelta());
  }
}

void SeqTrack::addTempoBPMSlide(uint32_t offset,
                                uint32_t length,
                                uint32_t dur,
                                double targBPM,
                                const std::string &sEventName) {
  onEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !isItemAtOffset(offset, true))
    addEvent(new TempoSlideSeqEvent(this, targBPM, dur, offset, length, sEventName));
  else if (readMode == READMODE_CONVERT_TO_MIDI) {
    double tempoInc = (targBPM - parentSeq->tempoBPM) / dur;
    for (unsigned int i = 0; i < dur; i++) {
      // Some MIDI software only recognise tempo events in the first track.
      MidiTrack *pFirstMidiTrack = parentSeq->firstMidiTrack();
      double newTempo = parentSeq->tempoBPM + (tempoInc * (i + 1));
      pFirstMidiTrack->insertTempoBPM(newTempo, getTime() + i);
    }
  }
  parentSeq->tempoBPM = targBPM;
}

void SeqTrack::addTimeSig(uint32_t offset,
                          uint32_t length,
                          uint8_t numer,
                          uint8_t denom,
                          uint8_t ticksPerQuarter,
                          const std::string &sEventName) {
  onEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !isItemAtOffset(offset, true)) {
    addEvent(new TimeSigSeqEvent(this, numer, denom, ticksPerQuarter, offset, length, sEventName));
  }
  addTimeSigNoItem(numer, denom, ticksPerQuarter);
}

void SeqTrack::addTimeSigNoItem(uint8_t numer, uint8_t denom, uint8_t ticksPerQuarter) const {
  if (readMode == READMODE_CONVERT_TO_MIDI) {
    MidiTrack *pFirstMidiTrack = parentSeq->firstMidiTrack();
    pFirstMidiTrack->addTimeSig(numer, denom, ticksPerQuarter);
  }
}

void SeqTrack::insertTimeSig(uint32_t offset,
                             uint32_t length,
                             uint8_t numer,
                             uint8_t denom,
                             uint8_t ticksPerQuarter,
                             uint32_t absTime,
                             const std::string &sEventName) {
  onEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !isItemAtOffset(offset, true)) {
    addEvent(new TimeSigSeqEvent(this, numer, denom, ticksPerQuarter, offset, length, sEventName));
  }
  else if (readMode == READMODE_CONVERT_TO_MIDI) {
    MidiTrack *pFirstMidiTrack = parentSeq->firstMidiTrack();
    pFirstMidiTrack->insertTimeSig(numer, denom, ticksPerQuarter, absTime);
  }
}

void SeqTrack::addEndOfTrack(uint32_t offset, uint32_t length, const std::string &sEventName) {
  onEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !isItemAtOffset(offset, true))
    addEvent(new TrackEndSeqEvent(this, offset, length, sEventName));
  else if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->addEndOfTrack();
  return addEndOfTrackNoItem();
}

void SeqTrack::addEndOfTrackNoItem() {
  if (readMode == READMODE_FIND_DELTA_LENGTH)
    totalTicks = getTime();
}

void SeqTrack::addControllerEventNoItem(uint8_t controllerType, uint8_t controllerValue) const {
  if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->addControllerEvent(channel, controllerType, controllerValue);
}


void SeqTrack::addGlobalTranspose(uint32_t offset, uint32_t length, int8_t semitones, const std::string &sEventName) {
  onEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !isItemAtOffset(offset, true))
    addEvent(new TransposeSeqEvent(this, semitones, offset, length, sEventName));
  else if (readMode == READMODE_CONVERT_TO_MIDI)
    parentSeq->midi->globalTrack.insertGlobalTranspose(getTime(), semitones);
}

void SeqTrack::addMarker(uint32_t offset,
                         uint32_t length,
                         const std::string &markername,
                         uint8_t databyte1,
                         uint8_t databyte2,
                         const std::string &sEventName,
                         int8_t priority,
                         EventColor color) {
  onEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI && !isItemAtOffset(offset, true))
    addEvent(new MarkerSeqEvent(this, markername, databyte1, databyte2, offset, length, sEventName, color));
  addMarkerNoItem(markername, databyte1, databyte2, priority);
}

void SeqTrack::addMarkerNoItem(const std::string &markername,
                               uint8_t databyte1,
                               uint8_t databyte2,
                               int8_t priority) const {
  if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->addMarker(channel, markername, databyte1, databyte2, priority);
}

void SeqTrack::insertMarkerNoItem(uint32_t absTime,
                                  const std::string &markername,
                                  uint8_t databyte1,
                                  uint8_t databyte2,
                                  int8_t priority) const {
  if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->insertMarker(channel, markername, databyte1, databyte2, priority, absTime);
}

// when in FIND_DELTA_LENGTH mode, returns true until we've hit the max number of loops defined in options
bool SeqTrack::addLoopForever(uint32_t offset, uint32_t length, const std::string &sEventName) {
  onEvent(offset, length);

  this->foreverLoops++;
  if (readMode == READMODE_ADD_TO_UI) {
    if (!isItemAtOffset(offset, true))
      addEvent(new LoopForeverSeqEvent(this, offset, length, sEventName));
    return false;
  }
  else if (readMode == READMODE_FIND_DELTA_LENGTH) {
    totalTicks = getTime();
    return (this->foreverLoops < ConversionOptions::the().numSequenceLoops());
  }
  return true;

}
