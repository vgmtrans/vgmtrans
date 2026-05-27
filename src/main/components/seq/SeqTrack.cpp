/*
 * VGMTrans (c) 2002-2025
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "base/types.h"
#include "SeqTrack.h"

#include <algorithm>
#include <cmath>

#include "automation/SeqMidiAutomation.h"
#include "SeqEvent.h"
#include "scale_conversion.h"
#include "Options.h"
#include "VGMSeqNoTrks.h"


namespace {
constexpr u16 maxLevelForResolution(Resolution res) {
  return res == Resolution::FourteenBit ? 16383 : 127;
}

double normalizedLevelFromRaw(u16 rawLevel, Resolution res) {
  return rawLevel / static_cast<double>(maxLevelForResolution(res));
}
}  // namespace

SeqTrack::SeqTrack(VGMSeq *parentFile, u32 offset, u32 length, std::string name)
    : VGMItem(parentFile, offset, length, std::move(name), Type::Track),
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
  infiniteLoops = 0;
  totalTicks = -1;
  deltaTime = 0;
  vol = 100;
  volResolution = Resolution::SevenBit;
  expression = 127;
  expressionResolution = Resolution::SevenBit;
  mastVol = 127;
  masterVolResolution = Resolution::SevenBit;
  prevPan = 64;
  prevReverb = 40;
  channelGroup = 0;
  transpose = 0;
  fineTuningCents = 0;
  coarseTuningSemitones = 0;
  cDrumNote = -1;
  cKeyCorrection = 0;
  panVolumeCorrectionRate = 1.0;
  returnOffsets.clear();
  loopStack.clear();
  visitedControlFlowStates.clear();
  prevDurEventIndices.clear();
  m_activeNoteEventIndices.clear();
  clearSeqEventTarget();
}

void SeqTrack::resetSegmentVars() {
  active = true;
  bInLoop = false;
  infiniteLoops = 0;
  totalTicks = -1;
  deltaTime = 0;
  returnOffsets.clear();
  loopStack.clear();
  visitedControlFlowStates.clear();
  prevDurEventIndices.clear();
  m_activeNoteEventIndices.clear();
  m_lastEventOffset = 0;
  m_lastEventLength = 0;
  clearSeqEventTarget();
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

  addControlFlowState(curOffset);

  if (readMode == READMODE_CONVERT_TO_MIDI) {
    if (preparedMidiTrack == nullptr) {
      addInitialMidiEvents(trackNum);
    }
  }
  return true;
}

void SeqTrack::loadTrackSegmentInit(u32 segmentOffset, u32 segmentLength,
                                    bool segmentActive, u32 initialOffset) {
  resetVisitedAddresses();
  resetSegmentVars();
  active = segmentActive;
  setRange(segmentOffset, segmentLength);
  curOffset = initialOffset;

  if (active) {
    addControlFlowState(curOffset);
  }
}

void SeqTrack::loadTrackMainLoop(u32 stopOffset, s32 stopTime) {
  if (!active) {
    return;
  }

  if (stopTime == -1) {
    stopTime = 0x7FFFFFFF;
  }

  if (getTime() >= static_cast<u32>(stopTime)) {
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
    while (curOffset < stopOffset && getTime() < static_cast<u32>(stopTime)) {
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

/// Assign a MIDI channel/group from a track index, optionally skipping channel 9
void SeqTrack::setChannelAndGroupFromTrkNum(int trk) {
  constexpr int kChannelsPerBank = 16;
  constexpr int kSkippedChannel  = 9;

  if (parentSeq->conversionContext().skipChannel10) {
    // Pack tracks into 15 usable slots per 16-channel bank, skipping 9.
    constexpr int kUsablePerBank = kChannelsPerBank - 1; // 15
    const int group  = trk / kUsablePerBank;
    const int slot15 = trk % kUsablePerBank;
    const int ch     = (slot15 < kSkippedChannel) ? slot15 : slot15 + 1;

    channel      = ch;        // 0..15, never 9
    channelGroup = group;
  } else {
    // Straight 16-wide mapping with no skipped channel.
    channel      = trk % kChannelsPerBank;
    channelGroup = trk / kChannelsPerBank;
  }

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

u32 SeqTrack::getTime() const {
  return parentSeq->time;
}

void SeqTrack::setTime(u32 newTime) {
  parentSeq->time = newTime;
  if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->setDelta(newTime);
}

void SeqTrack::addTime(u32 delta) {
  if (parentSeq->bLoadTickByTick) {
    deltaTime += delta;
  }
  else {
    parentSeq->time += delta;
    if (readMode == READMODE_CONVERT_TO_MIDI)
      pMidiTrack->addDelta(delta);
  }
}

u32 SeqTrack::readVarLen(u32 &offset) const {
  u32 value = 0;

  while (isValidOffset(offset)) {
	  u8 c = readByte(offset++);
    value = (value << 7) + (c & 0x7F);
	  // Check if continue bit is set
	  if((c & 0x80) == 0) {
		  break;
	  }
  }
  return value;
}

void SeqTrack::addControllerSlide(u32 dur,
                                  u16 &prevVal,
                                  u16 targVal,
                                  u8(*scalerFunc)(u8),
                                  void (MidiTrack::*insertFunc)(u8, u8, u32)) const {
  if (readMode != READMODE_CONVERT_TO_MIDI)
    return;

  double valInc = static_cast<double>(targVal - prevVal) / dur;
  s8 newVal = -1;
  for (unsigned int i = 0; i < dur; i++) {
    s8 prevValInSlide = newVal;

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

void SeqTrack::addForModSourceNoItem(ModSource source, u8 value) const {
  if (readMode != READMODE_CONVERT_TO_MIDI) {
    return;
  }

  if (auto controller = midiControllerForModSource(source)) {
    pMidiTrack->addControllerEvent(channel, *controller, value);
    return;
  }

  switch (source) {
    case ModSource::ChannelPressure:
      pMidiTrack->addChannelPressure(channel, value);
      break;
    case ModSource::PitchWheel: {
      const auto bend = static_cast<s16>(std::clamp<int>((static_cast<int>(value) * 128) - 8192, -8192, 8191));
      pMidiTrack->addPitchBend(channel, bend);
      break;
    }
    case ModSource::None:
      [[fallthrough]];
    case ModSource::PolyPressure:
      [[fallthrough]];
    default:
      break;
  }
}

void SeqTrack::addForModDestNoItem(ModDest destination, u8 value) const {
  addForModSourceNoItem(parentSeq->conversionContext().midiSourceFor(destination), value);
}

void SeqTrack::addLfoModulationEvent(ModDest destination,
                                     u32 offset,
                                     u32 length,
                                     u8 value,
                                     const std::string& eventName,
                                     Type type) {
  const bool isNewOffset = onEvent(offset, length);
  recordSeqEvent<SeqEvent>(isNewOffset,
                           getTime(),
                           offset,
                           length,
                           eventName,
                           type,
                           fmt::format("Value: {:d}", value));
  addForModDestNoItem(destination, value);
}

// Emit synth vibrato depth through the configured ModDest source if it changed.
bool SeqTrack::emitVibratoDepth(SeqSynthLfoAutomation& automation, u8 depth, bool force) {
  return automation.emitDepth(depth,
                              [this](u8 outputDepth) {
                                addVibratoDepthNoItem(outputDepth);
                              },
                              force);
}

bool SeqTrack::emitTremoloDepth(SeqSynthLfoAutomation& automation, u8 depth, bool force) {
  return automation.emitDepth(depth,
                              [this](u8 outputDepth) {
                                addTremoloDepthNoItem(outputDepth);
                              },
                              force);
}

bool SeqTrack::isOffsetUsed(u32 offset) {
  if (offset <= visitedAddressMax) {
    auto itrAddress = visitedAddresses.find(offset);
    if (itrAddress != visitedAddresses.end()) {
      return true;
    }
  }
  return false;
}


bool SeqTrack::onEvent(u32 offset, u32 length) {
  m_lastEventOffset = offset;
  m_lastEventLength = length;
  if (offset > visitedAddressMax) {
    visitedAddressMax = offset;
  }
  addControlFlowState(offset);
  return visitedAddresses.insert(offset).second;
}

SeqEvent* SeqTrack::addEvent(SeqEvent *pSeqEvent) {
  if (readMode != READMODE_ADD_TO_UI)
    return nullptr;

  addChild(pSeqEvent);

  // care for a case where the new event is located before the start address
  // (example: Donkey Kong Country - Map, Track 7 of 8)
  if (offset() > pSeqEvent->offset()) {
    if (bDetermineTrackLengthEventByEvent) {
      setLength(length() + ((offset() - pSeqEvent->offset())));
    }
    setOffset(pSeqEvent->offset());
  }

  if (bDetermineTrackLengthEventByEvent) {
    u32 newTrkLen = pSeqEvent->offset() + pSeqEvent->length() - offset();
    if (length() < newTrkLen)
      setLength(newTrkLen);
  }

  return pSeqEvent;
}

SeqEvent* SeqTrack::findSeqEventAtOffset(u32 offset, u32 length) {
  auto* item = getItemAtOffset(offset, true);
  if (item == nullptr) {
    if (auto* noTrks = dynamic_cast<VGMSeqNoTrks*>(parentSeq)) {
      item = noTrks->VGMSeq::getItemAtOffset(offset, true);
    }
  }
  if (item == nullptr) {
    return nullptr;
  }
  auto* seqEvent = dynamic_cast<SeqEvent*>(item);
  if (seqEvent == nullptr) {
    return nullptr;
  }
  // Check for an exact length match (unless a length value is 0)
  if (length != 0 && seqEvent->length() != 0 && seqEvent->length() != length) {
    return nullptr;
  }
  return seqEvent;
}

SeqEvent* SeqTrack::addGenericEvent(u32 offset,
                                    u32 length,
                                    const std::string &sEventName,
                                    const std::string &sEventDesc,
                                    Type type) {
  bool isNewOffset = onEvent(offset, length);

  if (readMode == READMODE_ADD_TO_UI) {
    if (isNewOffset && m_emitSeqEvents) {
      auto* target = seqEventTarget();
      auto* event = new SeqEvent(target, offset, length, sEventName, type, sEventDesc);
      event->channel = static_cast<u8>(channel);
      return target->addEvent(event);
    }
    return nullptr;
  }

  recordSeqEvent<SeqEvent>(isNewOffset, getTime(), offset, length, sEventName, type, sEventDesc);

  if (readMode == READMODE_CONVERT_TO_MIDI && bWriteGenericEventAsTextEvent) {
    std::string miditext(sEventName);
    if (!sEventDesc.empty()) {
      miditext += " - ";
      miditext += sEventDesc;
    }
    pMidiTrack->addText(miditext);
  }

  return nullptr;
}

void SeqTrack::addUnknown(u32 offset,
                          u32 length,
                          const std::string &sEventName,
                          const std::string &sEventDesc) {
  bool isNewOffset = onEvent(offset, length);

  recordSeqEvent<SeqEvent>(isNewOffset, getTime(), offset, length, sEventName, Type::Unknown, sEventDesc);

  if (readMode == READMODE_CONVERT_TO_MIDI) {
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

void SeqTrack::addSetOctave(u32 offset, u32 length, u8 newOctave, const std::string &sEventName) {
  bool isNewOffset = onEvent(offset, length);

  octave = newOctave;
  recordSeqEvent<SetOctaveSeqEvent>(isNewOffset, getTime(), newOctave, offset, length, sEventName);
}

void SeqTrack::addIncrementOctave(u32 offset, u32 length, const std::string &sEventName) {
  bool isNewOffset = onEvent(offset, length);

  octave++;
  recordSeqEvent<SeqEvent>(isNewOffset, getTime(), offset, length, sEventName, Type::ChangeState);
}

void SeqTrack::addDecrementOctave(u32 offset, u32 length, const std::string &sEventName) {
  bool isNewOffset = onEvent(offset, length);

  octave--;
  recordSeqEvent<SeqEvent>(isNewOffset, getTime(), offset, length, sEventName, Type::ChangeState);
}

void SeqTrack::addRest(u32 offset, u32 length, u32 restTime, const std::string &sEventName) {
  bool isNewOffset = onEvent(offset, length);

  u32 startTick = getTime();
  recordDurSeqEvent<RestSeqEvent>(isNewOffset, startTick, restTime, restTime, offset, length, sEventName);

  if (readMode == READMODE_CONVERT_TO_MIDI) {
    pMidiTrack->purgePrevNoteOffs();
    clearPrevDurEvents();
  }
  addTime(restTime);
}

void SeqTrack::addTie(u32 offset, u32 length, u32 duration,
                      const std::string &sEventName, const std::string &sEventDesc) {
  bool isNewOffset = onEvent(offset, length);

  recordDurSeqEvent<SeqEvent>(isNewOffset, getTime(), duration, offset, length, sEventName, Type::Tie, sEventDesc);
}

void SeqTrack::trackActiveNoteIndex(s8 key, SeqEventTimeIndex::Index idx) {
  if (readMode != READMODE_CONVERT_TO_MIDI || idx == SeqEventTimeIndex::kInvalidIndex) {
    return;
  }
  m_activeNoteEventIndices[static_cast<int>(key)].push_back(idx);
}

void SeqTrack::endActiveNoteIndex(s8 key, u32 endTick) {
  if (readMode != READMODE_CONVERT_TO_MIDI) {
    return;
  }
  auto it = m_activeNoteEventIndices.find(static_cast<int>(key));
  if (it == m_activeNoteEventIndices.end() || it->second.empty()) {
    return;
  }
  const auto idx = it->second.back();
  it->second.pop_back();
  if (it->second.empty()) {
    m_activeNoteEventIndices.erase(it);
  }
  if (idx == SeqEventTimeIndex::kInvalidIndex) {
    return;
  }
  auto& timeline = parentSeq->timedEventIndex();
  if (idx >= timeline.size()) {
    return;
  }
  auto& evt = timeline.event(idx);
  if (endTick <= evt.startTick) {
    evt.duration = 1;
  } else {
    evt.duration = endTick - evt.startTick;
  }
}

void SeqTrack::addNoteOn(u32 offset, u32 length, s8 key, s8 vel, const std::string &sEventName) {
  bool isNewOffset = onEvent(offset, length);

  auto noteIndex = recordDurSeqEvent<NoteOnSeqEvent>(isNewOffset, getTime(), 0, key, vel, offset, length, sEventName);
  trackActiveNoteIndex(key, noteIndex);
  addNoteOnNoItem(key, vel);
}

void SeqTrack::addNoteOnNoItem(s8 key, s8 velocity) {
  if (readMode == READMODE_CONVERT_TO_MIDI) {
    u8 finalVel = velocity;
    if (usesLinearAmplitudeScale())
      finalVel = convert7bitPercentAmpToStdMidiVal(velocity);

    if (cDrumNote == -1) {
      pMidiTrack->addNoteOn(channel, key + cKeyCorrection + transpose, finalVel);
    }
    else
      addPercNoteOnNoItem(cDrumNote, finalVel);
  }
  prevKey = key;
  prevVel = velocity;
}


void SeqTrack::addPercNoteOn(u32 offset, u32 length, s8 key, s8 vel, const std::string &sEventName) {
  u8 origChan = channel;
  channel = 9;
  s8 origDrumNote = cDrumNote;
  cDrumNote = -1;
//	DrumAssoc* pDrumAssoc = parentSeq->GetDrumAssoc(key);
//	if (pDrumAssoc)
//		key = pDrumAssoc->GMDrumNote;
  addNoteOn(offset, length, key - transpose, vel, sEventName);
  cDrumNote = origDrumNote;
  channel = origChan;
}

void SeqTrack::addPercNoteOnNoItem(s8 key, s8 vel) {
  u8 origChan = channel;
  channel = 9;
  s8 origDrumNote = cDrumNote;
  cDrumNote = -1;
//	DrumAssoc* pDrumAssoc = parentSeq->GetDrumAssoc(key);
//	if (pDrumAssoc)
//		key = pDrumAssoc->GMDrumNote;
  addNoteOnNoItem(key - transpose, vel);
  cDrumNote = origDrumNote;
  channel = origChan;
}

void SeqTrack::insertNoteOn(u32 offset,
                            u32 length,
                            s8 key,
                            s8 vel,
                            u32 absTime,
                            const std::string &sEventName) {
  bool isNewOffset = onEvent(offset, length);

  u8 finalVel = vel;
  if (usesLinearAmplitudeScale())
    finalVel = convert7bitPercentAmpToStdMidiVal(vel);

  auto noteIndex = recordDurSeqEvent<NoteOnSeqEvent>(isNewOffset, absTime, 0, key, vel, offset, length, sEventName);
  trackActiveNoteIndex(key, noteIndex);

  if (readMode == READMODE_CONVERT_TO_MIDI) {
    pMidiTrack->insertNoteOn(channel, key + cKeyCorrection + transpose, finalVel, absTime);
  }
  prevKey = key;
  prevVel = vel;
}

void SeqTrack::addNoteOff(u32 offset, u32 length, s8 key, const std::string &sEventName) {
  bool isNewOffset = onEvent(offset, length);

  recordSeqEvent<NoteOffSeqEvent>(isNewOffset, getTime(), key, offset, length, sEventName);
  addNoteOffNoItem(key);
}

void SeqTrack::addNoteOffNoItem(s8 key) {
  endActiveNoteIndex(key, getTime());
  if (readMode != READMODE_CONVERT_TO_MIDI)
    return;

  if (cDrumNote == -1) {
    pMidiTrack->addNoteOff(channel, key + cKeyCorrection + transpose);
  }
  else {
    addPercNoteOffNoItem(cDrumNote);
  }
}


void SeqTrack::addPercNoteOff(u32 offset, u32 length, s8 key, const std::string &sEventName) {
  u8 origChan = channel;
  channel = 9;
  s8 origDrumNote = cDrumNote;
  cDrumNote = -1;
//	DrumAssoc* pDrumAssoc = parentSeq->GetDrumAssoc(key);
//	if (pDrumAssoc)
//		key = pDrumAssoc->GMDrumNote;
  addNoteOff(offset, length, key - transpose, sEventName);
  cDrumNote = origDrumNote;
  channel = origChan;
}

void SeqTrack::addPercNoteOffNoItem(s8 key) {
  u8 origChan = channel;
  channel = 9;
  s8 origDrumNote = cDrumNote;
  cDrumNote = -1;
//	DrumAssoc* pDrumAssoc = parentSeq->GetDrumAssoc(key);
//	if (pDrumAssoc)
//		key = pDrumAssoc->GMDrumNote;
  addNoteOffNoItem(key - transpose);
  cDrumNote = origDrumNote;
  channel = origChan;
}

void SeqTrack::insertNoteOff(u32 offset,
                             u32 length,
                             s8 key,
                             u32 absTime,
                             const std::string &sEventName) {
  bool isNewOffset = onEvent(offset, length);

  recordSeqEvent<NoteOffSeqEvent>(isNewOffset, absTime, key, offset, length, sEventName);
  endActiveNoteIndex(key, absTime);
  insertNoteOffNoItem(key, absTime);
}

void SeqTrack::insertNoteOffNoItem(s8 key, u32 absTime) const {
  if (readMode == READMODE_CONVERT_TO_MIDI) {
    pMidiTrack->insertNoteOff(channel, key + cKeyCorrection + transpose, absTime);
  }
}

void SeqTrack::addNoteByDur(u32 offset,
                            u32 length,
                            s8 key,
                            s8 vel,
                            u32 dur,
                            const std::string &sEventName) {
  bool isNewOffset = onEvent(offset, length);

  u32 startTick = getTime();
  if (readMode == READMODE_CONVERT_TO_MIDI) {
    purgePrevDurEvents(startTick);
  }
  auto durEventIndex = recordDurSeqEvent<DurNoteSeqEvent>(
    isNewOffset, startTick, dur, key + cKeyCorrection, vel, dur, offset, length, sEventName);
  if (readMode == READMODE_CONVERT_TO_MIDI && durEventIndex != SeqEventTimeIndex::kInvalidIndex) {
    prevDurEventIndices.push_back(durEventIndex);
  }
  addNoteByDurNoItem(key, vel, dur);
}

void SeqTrack::addNoteByDurNoItem(s8 key, s8 vel, u32 dur) {
  if (readMode == READMODE_CONVERT_TO_MIDI) {
    u8 finalVel = vel;
    if (usesLinearAmplitudeScale())
      finalVel = convert7bitPercentAmpToStdMidiVal(vel);

    if (cDrumNote == -1) {
      pMidiTrack->addNoteByDur(channel, key + cKeyCorrection + transpose, finalVel, dur);
    }
    else
      addPercNoteByDurNoItem(cDrumNote, vel, dur);
  }
  prevKey = key;
  prevVel = vel;
}

void SeqTrack::addNoteByDur_Extend(u32 offset,
                                   u32 length,
                                   s8 key,
                                   s8 vel,
                                   u32 dur,
                                   const std::string &sEventName) {
  bool isNewOffset = onEvent(offset, length);

  u32 startTick = getTime();
  if (readMode == READMODE_CONVERT_TO_MIDI) {
    purgePrevDurEvents(startTick);
  }
  auto durEventIndex = recordDurSeqEvent<DurNoteSeqEvent>(
    isNewOffset, startTick, dur, key, vel, dur, offset, length, sEventName);
  if (readMode == READMODE_CONVERT_TO_MIDI && durEventIndex != SeqEventTimeIndex::kInvalidIndex) {
    prevDurEventIndices.push_back(durEventIndex);
  }
  addNoteByDurNoItem_Extend(key, vel, dur);
}

void SeqTrack::addNoteByDurNoItem_Extend(s8 key, s8 vel, u32 dur) {
  if (readMode == READMODE_CONVERT_TO_MIDI) {
    u8 finalVel = vel;
    if (usesLinearAmplitudeScale())
      finalVel = convert7bitPercentAmpToStdMidiVal(vel);

    if (cDrumNote == -1) {
      pMidiTrack->addNoteByDur_TriAce(channel, key + cKeyCorrection + transpose, finalVel, dur);
    }
    else
      addPercNoteByDurNoItem(cDrumNote, vel, dur);
  }
  prevKey = key;
  prevVel = vel;
}

void SeqTrack::addPercNoteByDur(u32 offset,
                                u32 length,
                                s8 key,
                                s8 vel,
                                u32 dur,
                                const std::string &sEventName) {
  u8 origChan = channel;
  if (!parentSeq->conversionContext().skipChannel10)
    channel = 9;
  s8 origDrumNote = cDrumNote;
  cDrumNote = -1;
//	DrumAssoc* pDrumAssoc = parentSeq->GetDrumAssoc(key);
//	if (pDrumAssoc)
//		key = pDrumAssoc->GMDrumNote;
  addNoteByDur(offset, length, key - transpose, vel, dur, sEventName);
  cDrumNote = origDrumNote;
  channel = origChan;
}

void SeqTrack::addPercNoteByDurNoItem(s8 key, s8 vel, u32 dur) {
  u8 origChan = channel;
  channel = 9;
  s8 origDrumNote = cDrumNote;
  cDrumNote = -1;
//	DrumAssoc* pDrumAssoc = parentSeq->GetDrumAssoc(key);
//	if (pDrumAssoc)
//		key = pDrumAssoc->GMDrumNote;
  addNoteByDurNoItem(key - transpose, vel, dur);
  cDrumNote = origDrumNote;
  channel = origChan;
}

/*void SeqTrack::AddNoteByDur(u32 offset, u32 length, s8 key, s8 vel, u32 dur, u8 chan, const std::string& sEventName)
{
	u8 origChan = channel;
	channel = chan;
	AddNoteByDur(offset, length, key, vel, dur, selectMsg, sEventName);
	channel = origChan;
}*/

void SeqTrack::insertNoteByDur(u32 offset,
                               u32 length,
                               s8 key,
                               s8 vel,
                               u32 dur,
                               u32 absTime,
                               const std::string &sEventName) {
  bool isNewOffset = onEvent(offset, length);

  if (readMode == READMODE_CONVERT_TO_MIDI) {
    purgePrevDurEvents(std::max(getTime(), absTime));
  }
  auto durEventIndex = recordDurSeqEvent<DurNoteSeqEvent>(
    isNewOffset, absTime, dur, key, vel, dur, offset, length, sEventName);
  if (readMode == READMODE_CONVERT_TO_MIDI && durEventIndex != SeqEventTimeIndex::kInvalidIndex) {
    prevDurEventIndices.push_back(durEventIndex);
  }
  insertNoteByDurNoItem(key, vel, dur, absTime);
}

void SeqTrack::insertNoteByDurNoItem(s8 key, s8 vel, u32 dur, u32 absTime) {
  if (readMode == READMODE_CONVERT_TO_MIDI) {
    u8 finalVel = vel;
    if (usesLinearAmplitudeScale())
      finalVel = convert7bitPercentAmpToStdMidiVal(vel);

    pMidiTrack->insertNoteByDur(channel, key + cKeyCorrection + transpose, finalVel, dur, absTime);
  }
  prevKey = key;
  prevVel = vel;
}

void SeqTrack::makePrevDurNoteEnd() const {
  makePrevDurNoteEnd(getTime() + (parentSeq->bLoadTickByTick ? deltaTime : 0));
}

void SeqTrack::makePrevDurNoteEnd(u32 absTime) const {
  if (readMode == READMODE_CONVERT_TO_MIDI) {
    for (auto& prevDurNoteOff : pMidiTrack->prevDurNoteOffs) {
      prevDurNoteOff->absTime = absTime;
    }
    auto& timeline = parentSeq->timedEventIndex();
    for (auto idx : prevDurEventIndices) {
      auto& evt = timeline.event(idx);
      evt.duration = absTime > evt.startTick ? absTime - evt.startTick : 0;
    }
  }
}

void SeqTrack::limitPrevDurNoteEnd() const {
  limitPrevDurNoteEnd(getTime() + (parentSeq->bLoadTickByTick ? deltaTime : 0));
}

void SeqTrack::limitPrevDurNoteEnd(u32 absTime) const {
  if (readMode == READMODE_CONVERT_TO_MIDI) {
    for (auto& prevDurNoteOff : pMidiTrack->prevDurNoteOffs) {
      if (prevDurNoteOff->absTime > absTime) {
        prevDurNoteOff->absTime = absTime;
      }
    }
    auto& timeline = parentSeq->timedEventIndex();
    for (auto idx : prevDurEventIndices) {
      auto& evt = timeline.event(idx);
      if (evt.endTickExclusive() > absTime) {
        evt.duration = absTime > evt.startTick ? absTime - evt.startTick : 0;
      }
    }
  }
}

void SeqTrack::purgePrevDurEvents(u32 absTime) {
  if (readMode != READMODE_CONVERT_TO_MIDI) {
    return;
  }
  if (prevDurEventIndices.empty()) {
    return;
  }
  auto& timeline = parentSeq->timedEventIndex();
  prevDurEventIndices.erase(
    std::remove_if(prevDurEventIndices.begin(), prevDurEventIndices.end(),
      [&timeline, absTime](SeqEventTimeIndex::Index idx) {
        return timeline.endTickExclusive(idx) <= absTime;
      }),
    prevDurEventIndices.end());
}

void SeqTrack::clearPrevDurEvents() {
  prevDurEventIndices.clear();
}

double SeqTrack::applyPanVolumeCorrection(double level, LevelController controller) const {
  if (controller != LevelController::MasterVolume) {
    PanVolumeCorrectionMode relevantCorrectionMode = controller == LevelController::Volume ?
      PanVolumeCorrectionMode::kAdjustVolumeController :
      PanVolumeCorrectionMode::kAdjustExpressionController;
    if (parentSeq->panVolumeCorrectionMode == relevantCorrectionMode)
      level *= panVolumeCorrectionRate;
  }
  return level;
}

void SeqTrack::addLevelNoItem(double level, LevelController controller, Resolution res, int absTime) {
  level = std::clamp(level, 0.0, 1.0);
  const u16 maxLevel = maxLevelForResolution(res);
  const u16 origLevel = static_cast<u16>(std::lround(level * maxLevel));
  switch (controller) {
    case LevelController::Volume:
      vol = origLevel;
      volResolution = res;
      break;
    case LevelController::Expression:
      expression = origLevel;
      expressionResolution = res;
      break;
    case LevelController::MasterVolume:
      mastVol = origLevel;
      masterVolResolution = res;
      break;
  }

  level = applyPanVolumeCorrection(level, controller);
  level = std::clamp(level, 0.0, 1.0);
  switch (res) {
    case Resolution::SevenBit: {
      const u8 midiLevel = usesLinearAmplitudeScale()
        ? convertPercentAmpToStdMidiVal(level)
        : static_cast<u8>(std::lround(level * maxLevel));
      switch (controller) {
        case LevelController::Volume:
          if (readMode == READMODE_CONVERT_TO_MIDI) {
            if (absTime < 0) {
              pMidiTrack->addVol(channel, midiLevel);
            } else {
              pMidiTrack->insertVol(channel, midiLevel, absTime);
            }
          }
          break;
        case LevelController::Expression:
          if (readMode == READMODE_CONVERT_TO_MIDI) {
            if (absTime < 0) {
              pMidiTrack->addExpression(channel, midiLevel);
            } else {
              pMidiTrack->insertExpression(channel, midiLevel, absTime);
            }
          }
          break;
        case LevelController::MasterVolume:
          if (readMode == READMODE_CONVERT_TO_MIDI) {
            if (absTime < 0) {
              pMidiTrack->addMasterVol(channel, midiLevel);
            } else {
              pMidiTrack->insertMasterVol(channel, midiLevel, 0, absTime);
            }
          }
          break;
      }
      break;
    }
    case Resolution::FourteenBit: {
      const u16 midiLevel = usesLinearAmplitudeScale()
        ? convertPercentAmpToStd14BitMidiVal(level)
        : static_cast<u16>(std::lround(level * maxLevel));
      const u8 levelHi = static_cast<u8>((midiLevel >> 7) & 0x7F);
      const u8 levelLo = static_cast<u8>(midiLevel & 0x7F);
      switch (controller) {
        case LevelController::Volume:
          if (readMode == READMODE_CONVERT_TO_MIDI) {
            if (absTime < 0) {
              pMidiTrack->addVolumeFine(channel, levelLo);
              pMidiTrack->addVol(channel, levelHi);
            } else {
              pMidiTrack->insertVolumeFine(channel, levelLo, absTime);
              pMidiTrack->insertVol(channel, levelHi, absTime);
            }
          }
          break;
        case LevelController::Expression:
          if (readMode == READMODE_CONVERT_TO_MIDI) {
            if (absTime < 0) {
              pMidiTrack->addExpressionFine(channel, levelLo);
              pMidiTrack->addExpression(channel, levelHi);
            } else {
              pMidiTrack->insertExpressionFine(channel, levelLo, absTime);
              pMidiTrack->insertExpression(channel, levelHi, absTime);
            }
          }
          break;
        case LevelController::MasterVolume:
          if (readMode == READMODE_CONVERT_TO_MIDI) {
            if (absTime < 0) {
              pMidiTrack->addMasterVol(channel, levelHi, levelLo);
            } else {
              pMidiTrack->insertMasterVol(channel, levelHi, levelLo, absTime);
            }
          }
          break;
      }
      break;
    }
  }
}

void SeqTrack::reapplyStoredLevelNoItem(LevelController controller, int absTime) {
  u16 rawLevel;
  Resolution resolution;
  switch (controller) {
    case LevelController::Volume:
      rawLevel = vol;
      resolution = volResolution;
      break;
    case LevelController::Expression:
      rawLevel = expression;
      resolution = expressionResolution;
      break;
    case LevelController::MasterVolume:
      rawLevel = mastVol;
      resolution = masterVolResolution;
      break;
  }

  addLevelNoItem(normalizedLevelFromRaw(rawLevel, resolution), controller, resolution, absTime);
}

void SeqTrack::addVol(u32 offset, u32 length, double volPercent, Resolution res, const std::string &sEventName) {
  bool isNewOffset = onEvent(offset, length);

  recordSeqEvent<VolSeqEvent>(isNewOffset, getTime(), volPercent, offset, length, sEventName);
  addLevelNoItem(volPercent, LevelController::Volume, res, -1);
}

void SeqTrack::addVol(u32 offset, u32 length, u8 newVol, const std::string &sEventName) {
  bool isNewOffset = onEvent(offset, length);

  recordSeqEvent<VolSeqEvent>(isNewOffset, getTime(), newVol, offset, length, sEventName);
  addVolNoItem(newVol);
}

void SeqTrack::addVolNoItem(u8 newVol) {
  addLevelNoItem(newVol / 127.0, LevelController::Volume, Resolution::SevenBit);
}

void SeqTrack::addVolSlide(u32 offset,
                           u32 length,
                           u32 dur,
                           u8 targVol,
                           const std::string &sEventName) {
  bool isNewOffset = onEvent(offset, length);

  recordDurSeqEvent<VolSlideSeqEvent>(isNewOffset, getTime(), dur, targVol, dur, offset, length, sEventName);

  if (readMode == READMODE_CONVERT_TO_MIDI) {
    addControllerSlide(dur,
                       vol,
                       targVol,
                       usesLinearAmplitudeScale() ? convert7bitPercentAmpToStdMidiVal : nullptr,
                       &MidiTrack::insertVol);
    volResolution = Resolution::SevenBit;
  }
}

void SeqTrack::insertVol(u32 offset,
                         u32 length,
                         u8 newVol,
                         u32 absTime,
                         const std::string &sEventName) {
  bool isNewOffset = onEvent(offset, length);

  recordSeqEvent<VolSeqEvent>(isNewOffset, absTime, newVol, offset, length, sEventName);
  addLevelNoItem(newVol / 127.0, LevelController::Volume, Resolution::SevenBit, absTime);
}

void SeqTrack::addExpression(u32 offset, u32 length, double levelPercent, Resolution res, const std::string &sEventName) {
  bool isNewOffset = onEvent(offset, length);

  recordSeqEvent<ExpressionSeqEvent>(isNewOffset, getTime(), levelPercent, offset, length, sEventName);
  addLevelNoItem(levelPercent, LevelController::Expression, res, -1);
}


void SeqTrack::addExpression(u32 offset, u32 length, u8 level, const std::string &sEventName) {
  bool isNewOffset = onEvent(offset, length);

  recordSeqEvent<ExpressionSeqEvent>(isNewOffset, getTime(), level, offset, length, sEventName);
  addExpressionNoItem(level);
}

void SeqTrack::addExpressionNoItem(u8 level) {
  addLevelNoItem(level / 127.0, LevelController::Expression, Resolution::SevenBit);
  expression = level;
}

void SeqTrack::addExpressionSlide(u32 offset,
                                  u32 length,
                                  u32 dur,
                                  u8 targExpr,
                                  const std::string &sEventName) {
  bool isNewOffset = onEvent(offset, length);

  recordDurSeqEvent<ExpressionSlideSeqEvent>(isNewOffset, getTime(), dur, targExpr, dur, offset, length, sEventName);

  if (readMode == READMODE_CONVERT_TO_MIDI) {
    addControllerSlide(dur,
                       expression,
                       targExpr,
                       usesLinearAmplitudeScale() ? convert7bitPercentAmpToStdMidiVal : nullptr,
                       &MidiTrack::insertExpression);
    expressionResolution = Resolution::SevenBit;
  }
}

void SeqTrack::insertExpression(u32 offset,
                                u32 length,
                                u8 level,
                                u32 absTime,
                                const std::string &sEventName) {
  bool isNewOffset = onEvent(offset, length);

  recordSeqEvent<ExpressionSeqEvent>(isNewOffset, absTime, level, offset, length, sEventName);
  insertExpressionNoItem(level, absTime);
}

void SeqTrack::insertExpressionNoItem(u8 level, u32 absTime) {
  addLevelNoItem(level / 127.0, LevelController::Expression, Resolution::SevenBit, absTime);
}

void SeqTrack::addMasterVol(u32 offset, u32 length, u8 newVol, const std::string &sEventName) {
  bool isNewOffset = onEvent(offset, length);

  recordSeqEvent<MastVolSeqEvent>(isNewOffset, getTime(), newVol, offset, length, sEventName);
  addMasterVolNoItem(newVol);
}

void SeqTrack::addMasterVol(u32 offset, u32 length, double volPercent, Resolution res, const std::string &sEventName) {
  bool isNewOffset = onEvent(offset, length);

  recordSeqEvent<MastVolSeqEvent>(isNewOffset, getTime(), volPercent, offset, length, sEventName);
  addLevelNoItem(volPercent, LevelController::MasterVolume, res, -1);
}

void SeqTrack::addMasterVolNoItem(u8 newVol) {
  addLevelNoItem(newVol / 127.0, LevelController::MasterVolume, Resolution::SevenBit);
}

void SeqTrack::addMastVolSlide(u32 offset,
                               u32 length,
                               u32 dur,
                               u8 targVol,
                               const std::string &sEventName) {
  bool isNewOffset = onEvent(offset, length);

  recordDurSeqEvent<MastVolSlideSeqEvent>(isNewOffset, getTime(), dur, targVol, dur, offset, length, sEventName);

  if (readMode == READMODE_CONVERT_TO_MIDI) {
    addControllerSlide(dur,
                       mastVol,
                       targVol,
                       usesLinearAmplitudeScale() ? convert7bitPercentAmpToStdMidiVal : nullptr,
                       &MidiTrack::insertMasterVol);
    masterVolResolution = Resolution::SevenBit;
  }
}

void SeqTrack::addPan(u32 offset, u32 length, u8 pan, const std::string &sEventName) {
  bool isNewOffset = onEvent(offset, length);

  recordSeqEvent<PanSeqEvent>(isNewOffset, getTime(), pan, offset, length, sEventName);
  addPanNoItem(pan);
}

void SeqTrack::addPanNoItem(u8 pan) {
  if (readMode == READMODE_CONVERT_TO_MIDI) {
    const u8 midiPan = usesLinearAmplitudeScale()
      ? convert7bitLinearPercentPanValToStdMidiVal(pan, &panVolumeCorrectionRate)
      : pan;
    pMidiTrack->addPan(channel, midiPan);

    switch (parentSeq->panVolumeCorrectionMode) {
    case PanVolumeCorrectionMode::kAdjustVolumeController:
      reapplyStoredLevelNoItem(LevelController::Volume);
      break;

    case PanVolumeCorrectionMode::kAdjustExpressionController:
      reapplyStoredLevelNoItem(LevelController::Expression);
      break;

    default:
      break;
    }
  }
  prevPan = pan;
}

void SeqTrack::addPanSlide(u32 offset,
                           u32 length,
                           u32 dur,
                           u8 targPan,
                           const std::string &sEventName) {
  bool isNewOffset = onEvent(offset, length);

  recordDurSeqEvent<PanSlideSeqEvent>(isNewOffset, getTime(), dur, targPan, dur, offset, length, sEventName);

  if (readMode == READMODE_CONVERT_TO_MIDI)
    addControllerSlide(dur, prevPan, targPan, nullptr, &MidiTrack::insertPan);
}


void SeqTrack::insertPan(u32 offset,
                         u32 length,
                         u8 pan,
                         u32 absTime,
                         const std::string &sEventName) {
  bool isNewOffset = onEvent(offset, length);

  recordSeqEvent<PanSeqEvent>(isNewOffset, absTime, pan, offset, length, sEventName);

  if (readMode == READMODE_CONVERT_TO_MIDI) {
    const u8 midiPan = usesLinearAmplitudeScale()
      ? convert7bitLinearPercentPanValToStdMidiVal(pan, &panVolumeCorrectionRate)
      : pan;
    pMidiTrack->insertPan(channel, midiPan, absTime);

    // TODO: (bugfix) Pan volume compensation does not work properly when using pan slider and volume slider at the same time
    switch (parentSeq->panVolumeCorrectionMode) {
    case PanVolumeCorrectionMode::kAdjustVolumeController:
      reapplyStoredLevelNoItem(LevelController::Volume, absTime);
      break;

    case PanVolumeCorrectionMode::kAdjustExpressionController:
      reapplyStoredLevelNoItem(LevelController::Expression, absTime);
      break;

    default:
      break;
    }
  }
}

void SeqTrack::addReverb(u32 offset, u32 length, u8 reverb, const std::string &sEventName) {
  bool isNewOffset = onEvent(offset, length);

  recordSeqEvent<ReverbSeqEvent>(isNewOffset, getTime(), reverb, offset, length, sEventName);
  addReverbNoItem(reverb);
}

void SeqTrack::addReverbNoItem(u8 reverb) {
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

void SeqTrack::insertReverb(u32 offset,
                            u32 length,
                            u8 reverb,
                            u32 absTime,
                            const std::string &sEventName) {
  bool isNewOffset = onEvent(offset, length);

  recordSeqEvent<ReverbSeqEvent>(isNewOffset, absTime, reverb, offset, length, sEventName);

  if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->insertReverb(channel, reverb, absTime);
}

void SeqTrack::addPitchBendMidiFormat(u32 offset,
                                      u32 length,
                                      u8 lo,
                                      u8 hi,
                                      const std::string &sEventName) {
  addPitchBend(offset, length, lo + (hi << 7) - 0x2000, sEventName);
}

void SeqTrack::addPitchBend(u32 offset, u32 length, s16 bend, const std::string &sEventName) {
  bool isNewOffset = onEvent(offset, length);

  recordSeqEvent<PitchBendSeqEvent>(isNewOffset, getTime(), bend, offset, length, sEventName);

  addPitchBendNoItem(bend);
}

void SeqTrack::addPitchBendAsPercent(u32 offset, u32 length, double percent, const std::string &sEventName) {
  const s16 minVal = -8192;
  const s16 maxVal = 8191;
  const s16 bendVal = static_cast<s16>(percent * 8192);
  s16 bend = std::clamp(bendVal, minVal, maxVal);
  addPitchBend(offset, length, bend, sEventName);
}

void SeqTrack::addPitchBendNoItem(s16 bend) const {
  if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->addPitchBend(channel, bend);
}

void SeqTrack::addPitchBendRange(u32 offset, u32 length, u16 cents, const std::string &sEventName) {
  bool isNewOffset = onEvent(offset, length);

  recordSeqEvent<PitchBendRangeSeqEvent>(isNewOffset, getTime(), cents, offset, length, sEventName);

  if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->addPitchBendRange(channel, cents);
}

void SeqTrack::addPitchBendRangeNoItem(u16 cents) const {
  if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->addPitchBendRange(channel, cents);
}

void SeqTrack::addChannelPressure(u32 offset,
                                  u32 length,
                                  u8 pressure,
                                  const std::string &sEventName) {
  bool isNewOffset = onEvent(offset, length);

  recordSeqEvent<ChannelPressureSeqEvent>(isNewOffset, getTime(), pressure, offset, length, sEventName);
  addChannelPressureNoItem(pressure);
}

void SeqTrack::addChannelPressureNoItem(u8 pressure) {
  if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->addChannelPressure(channel, pressure);
}

void SeqTrack::insertChannelPressure(u32 offset,
                                     u32 length,
                                     u8 pressure,
                                     u32 absTime,
                                     const std::string &sEventName) {
  bool isNewOffset = onEvent(offset, length);

  recordSeqEvent<ChannelPressureSeqEvent>(isNewOffset, absTime, pressure, offset, length, sEventName);

  if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->insertChannelPressure(channel, pressure, absTime);
}

void SeqTrack::addFineTuning(u32 offset, u32 length, double cents, const std::string &sEventName) {
  bool isNewOffset = onEvent(offset, length);

  recordSeqEvent<FineTuningSeqEvent>(isNewOffset, getTime(), cents, offset, length, sEventName);
  addFineTuningNoItem(cents);
}

void SeqTrack::addFineTuningNoItem(double cents) {
  if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->addFineTuning(channel, cents);
  fineTuningCents = cents;
}

void SeqTrack::addCoarseTuning(u32 offset, u32 length, double semitones, const std::string &sEventName) {
  bool isNewOffset = onEvent(offset, length);

  recordSeqEvent<CoarseTuningSeqEvent>(isNewOffset, getTime(), semitones, offset, length, sEventName);
  addCoarseTuningNoItem(semitones);
}

void SeqTrack::addCoarseTuningNoItem(double semitones) {
  if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->addCoarseTuning(channel, semitones);
  coarseTuningSemitones = semitones;
}

void SeqTrack::addModulationDepthRange(u32 offset,
                                       u32 length,
                                       double semitones,
                                       const std::string &sEventName) {
  bool isNewOffset = onEvent(offset, length);

  recordSeqEvent<ModulationDepthRangeSeqEvent>(isNewOffset, getTime(), semitones, offset, length, sEventName);

  if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->addModulationDepthRange(channel, semitones);
}

void SeqTrack::addModulationDepthRangeNoItem(double semitones) const {
  if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->addModulationDepthRange(channel, semitones);
}

void SeqTrack::addTranspose(u32 offset, u32 length, s8 theTranspose, const std::string &sEventName) {
  bool isNewOffset = onEvent(offset, length);

  recordSeqEvent<TransposeSeqEvent>(isNewOffset, getTime(), theTranspose, offset, length, sEventName);
  transpose = theTranspose;
}


void SeqTrack::addModulation(u32 offset, u32 length, u8 depth, const std::string &sEventName) {
  bool isNewOffset = onEvent(offset, length);

  recordSeqEvent<ModulationSeqEvent>(isNewOffset, getTime(), depth, offset, length, sEventName);

  if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->addModulation(channel, depth);
}

void SeqTrack::addModulationNoItem(u8 depth) {
  if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->addModulation(channel, depth);
}


void SeqTrack::insertModulation(u32 offset,
                                u32 length,
                                u8 depth,
                                u32 absTime,
                                const std::string &sEventName) {
  bool isNewOffset = onEvent(offset, length);

  recordSeqEvent<ModulationSeqEvent>(isNewOffset, absTime, depth, offset, length, sEventName);

  if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->insertModulation(channel, depth, absTime);
}

void SeqTrack::addBreath(u32 offset, u32 length, u8 depth, const std::string &sEventName) {
  bool isNewOffset = onEvent(offset, length);

  recordSeqEvent<BreathSeqEvent>(isNewOffset, getTime(), depth, offset, length, sEventName);

  if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->addBreath(channel, depth);
}

void SeqTrack::addBreathNoItem(u8 depth) {
  if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->addBreath(channel, depth);
}

void SeqTrack::insertBreath(u32 offset,
                            u32 length,
                            u8 depth,
                            u32 absTime,
                            const std::string &sEventName) {
  bool isNewOffset = onEvent(offset, length);

  recordSeqEvent<BreathSeqEvent>(isNewOffset, absTime, depth, offset, length, sEventName);

  if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->insertBreath(channel, depth, absTime);
}

void SeqTrack::addVibratoDepth(u32 offset,
                               u32 length,
                               u8 depth,
                               const std::string& sEventName) {
  const bool isNewOffset = onEvent(offset, length);
  recordSeqEvent<ModulationSeqEvent>(isNewOffset, getTime(), depth, offset, length, sEventName);
  addVibratoDepthNoItem(depth);
}

void SeqTrack::addVibratoDepthNoItem(u8 depth) const {
  addForModDestNoItem(ModDest::VibLfoToPitch, depth);
}

void SeqTrack::addVibratoFrequency(u32 offset,
                                   u32 length,
                                   u8 frequency,
                                   const std::string& sEventName) {
  addLfoModulationEvent(ModDest::VibLfoFreq, offset, length, frequency, sEventName, Type::Vibrato);
}

void SeqTrack::addVibratoFrequencyNoItem(u8 frequency) const {
  addForModDestNoItem(ModDest::VibLfoFreq, frequency);
}

void SeqTrack::addVibratoDelay(u32 offset,
                               u32 length,
                               u8 delay,
                               const std::string& sEventName) {
  addLfoModulationEvent(ModDest::VibLfoDelay, offset, length, delay, sEventName, Type::Vibrato);
}

void SeqTrack::addVibratoDelayNoItem(u8 delay) const {
  addForModDestNoItem(ModDest::VibLfoDelay, delay);
}

void SeqTrack::addTremoloDepth(u32 offset,
                               u32 length,
                               u8 depth,
                               const std::string& sEventName) {
  addLfoModulationEvent(ModDest::ModLfoToVol, offset, length, depth, sEventName, Type::Tremelo);
}

void SeqTrack::addTremoloDepthNoItem(u8 depth) const {
  addForModDestNoItem(ModDest::ModLfoToVol, depth);
}

void SeqTrack::addTremoloFrequency(u32 offset,
                                   u32 length,
                                   u8 frequency,
                                   const std::string& sEventName) {
  addLfoModulationEvent(ModDest::ModLfoFreq, offset, length, frequency, sEventName, Type::Tremelo);
}

void SeqTrack::addTremoloFrequencyNoItem(u8 frequency) const {
  addForModDestNoItem(ModDest::ModLfoFreq, frequency);
}

void SeqTrack::addTremoloDelay(u32 offset,
                               u32 length,
                               u8 delay,
                               const std::string& sEventName) {
  addLfoModulationEvent(ModDest::ModLfoDelay, offset, length, delay, sEventName, Type::Tremelo);
}

void SeqTrack::addTremoloDelayNoItem(u8 delay) const {
  addForModDestNoItem(ModDest::ModLfoDelay, delay);
}

void SeqTrack::addSustainEvent(u32 offset, u32 length, u8 depth, const std::string &sEventName) {
  bool isNewOffset = onEvent(offset, length);

  recordSeqEvent<SustainSeqEvent>(isNewOffset, getTime(), depth, offset, length, sEventName);

  if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->addSustain(channel, depth);
}

void SeqTrack::insertSustainEvent(u32 offset,
                                  u32 length,
                                  u8 depth,
                                  u32 absTime,
                                  const std::string &sEventName) {
  bool isNewOffset = onEvent(offset, length);

  recordSeqEvent<SustainSeqEvent>(isNewOffset, absTime, depth, offset, length, sEventName);

  if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->insertSustain(channel, depth, absTime);
}

void SeqTrack::addPortamento(u32 offset, u32 length, bool bOn, const std::string &sEventName) {
  bool isNewOffset = onEvent(offset, length);

  recordSeqEvent<PortamentoSeqEvent>(isNewOffset, getTime(), bOn, offset, length, sEventName);
  addPortamentoNoItem(bOn);
}

void SeqTrack::addPortamentoNoItem(bool bOn) const {
  if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->addPortamento(channel, bOn);
}

void SeqTrack::insertPortamento(u32 offset,
                                u32 length,
                                bool bOn,
                                u32 absTime,
                                const std::string &sEventName) {
  bool isNewOffset = onEvent(offset, length);

  recordSeqEvent<PortamentoSeqEvent>(isNewOffset, absTime, bOn, offset, length, sEventName);

  if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->insertPortamento(channel, bOn, absTime);
}

void SeqTrack::insertPortamentoNoItem(bool bOn, u32 absTime) const {
  if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->insertPortamento(channel, bOn, absTime);
}

void SeqTrack::addPortamentoTime(u32 offset, u32 length, u8 time, const std::string &sEventName) {
  bool isNewOffset = onEvent(offset, length);

  recordSeqEvent<PortamentoTimeSeqEvent>(isNewOffset, getTime(), time, offset, length, sEventName);
  addPortamentoTimeNoItem(time);
}

void SeqTrack::addPortamentoTimeNoItem(u8 time) const {
  if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->addPortamentoTime(channel, time);
}

void SeqTrack::insertPortamentoTime(u32 offset,
                                    u32 length,
                                    u8 time,
                                    u32 absTime,
                                    const std::string &sEventName) {
  bool isNewOffset = onEvent(offset, length);

  recordSeqEvent<PortamentoTimeSeqEvent>(isNewOffset, absTime, time, offset, length, sEventName);
  insertPortamentoTimeNoItem(time, absTime);
}

void SeqTrack::insertPortamentoTimeNoItem(u8 time, u32 absTime) const {
  if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->insertPortamentoTime(channel, time, absTime);
}

void SeqTrack::addPortamentoTime14Bit(u32 offset, u32 length, u16 time, const std::string &sEventName) {
  bool isNewOffset = onEvent(offset, length);

  recordSeqEvent<PortamentoTimeSeqEvent>(isNewOffset, getTime(), time, offset, length, sEventName);
  addPortamentoTime14BitNoItem(time);
}

void SeqTrack::addPortamentoTime14BitNoItem(u16 time) const {
  if (readMode == READMODE_CONVERT_TO_MIDI) {
    u8 lsb = time & 127;
    u8 msb = (time >> 7) & 127;
    pMidiTrack->addPortamentoTimeFine(channel, lsb);
    pMidiTrack->addPortamentoTime(channel, msb);
  }
}

void SeqTrack::insertPortamentoTime14Bit(u32 offset, u32 length, u16 time, u32 absTime, const std::string &sEventName) {
  bool isNewOffset = onEvent(offset, length);

  recordSeqEvent<PortamentoTimeSeqEvent>(isNewOffset, absTime, time, offset, length, sEventName);
  insertPortamentoTime14BitNoItem(time, absTime);
}

void SeqTrack::insertPortamentoTime14BitNoItem(u16 time, u32 absTime) const {
  if (readMode == READMODE_CONVERT_TO_MIDI) {
    u8 lsb = time & 127;
    u8 msb = (time >> 7) & 127;
    pMidiTrack->insertPortamentoTimeFine(channel, lsb, absTime);
    pMidiTrack->insertPortamentoTime(channel, msb, absTime);
  }
}

void SeqTrack::addPortamentoControlNoItem(u8 key) const {
  if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->addPortamentoControl(channel, key);
}

void SeqTrack::insertPortamentoControlNoItem(u8 key, u32 absTime) const {
  if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->insertPortamentoControl(channel, key, absTime);
}

/*void InsertNoteOnEvent(s8 key, s8 vel, u32 absTime);
void AddNoteOffEvent(s8 key);
void InsertNoteOffEvent(s8 key, s8 vel, u32 absTime);
void AddNoteByDur(s8 key, s8 vel);
void InsertNoteByDur(s8 key, s8 vel, u32 absTime);
void AddVolumeEvent(u8 vol);
void InsertVolumeEvent(u8 vol, u32 absTime);
void AddExpression(u8 expression);
void InsertExpression(u8 expression, u32 absTime);
void AddPanEvent(u8 pan);
void InsertPanEvent(u8 pan, u32 absTime);*/

void SeqTrack::addLegatoPedalNoItem(bool bOn) {
  if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->addLegatoPedal(channel, bOn);
}

void SeqTrack::addProgramChange(u32 offset, u32 length, u32 progNum, const std::string &sEventName) {
  addProgramChange(offset, length, progNum, false, sEventName);
}

void SeqTrack::addProgramChange(u32 offset,
                                u32 length,
                                u32 progNum,
                                u8 chan,
                                const std::string &sEventName) {
  addProgramChange(offset, length, progNum, false, chan, sEventName);
}

void SeqTrack::addProgramChange(u32 offset,
                                u32 length,
                                u32 progNum,
                                bool requireBank,
                                const std::string &sEventName) {
  bool isNewOffset = onEvent(offset, length);

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
  recordSeqEvent<ProgChangeSeqEvent>(isNewOffset, getTime(), progNum, offset, length, sEventName);

  if (readMode == READMODE_ADD_TO_UI) {
    parentSeq->addInstrumentRef(progNum);
  }
  addProgramChangeNoItem(progNum, requireBank);
}

void SeqTrack::addProgramChange(u32 offset,
                                u32 length,
                                u32 progNum,
                                bool requireBank,
                                u8 chan,
                                const std::string &sEventName) {
  //if (selectMsg = NULL)
  //	selectMsg.Forma
  u8 origChan = channel;
  channel = chan;
  addProgramChange(offset, length, progNum, requireBank, sEventName);
  channel = origChan;
}

void SeqTrack::addProgramChangeNoItem(u32 progNum, bool requireBank) const {
  if (readMode == READMODE_CONVERT_TO_MIDI) {
    if (requireBank) {
      if (auto style = parentSeq->conversionContext().bankSelectStyle;
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

void SeqTrack::addBankSelect(u32 offset, u32 length, u8 bank, const std::string& sEventName) {
  bool isNewOffset = onEvent(offset, length);

  recordSeqEvent<BankSelectSeqEvent>(isNewOffset, getTime(), bank, offset, length, sEventName);
  if (readMode == READMODE_ADD_TO_UI) {
    parentSeq->addBankReference(bank);
  }
  addBankSelectNoItem(bank);
}

void SeqTrack::addBankSelectNoItem(u8 bank) const {
  if (readMode == READMODE_CONVERT_TO_MIDI) {
    if (auto style = parentSeq->conversionContext().bankSelectStyle;
        style == BankSelectStyle::GS) {
      pMidiTrack->addBankSelect(channel, bank & 0x7f);
    } else if (style == BankSelectStyle::MMA) {
      pMidiTrack->addBankSelect(channel, bank >> 7);
      pMidiTrack->addBankSelectFine(channel, bank & 0x7f);
    }
  }
}

void SeqTrack::addTempo(u32 offset, u32 length, u32 microsPerQuarter, const std::string &sEventName) {
  bool isNewOffset = onEvent(offset, length);

  double bpm = 60000000.0 / microsPerQuarter;
  recordSeqEvent<TempoSeqEvent>(isNewOffset, getTime(), bpm, offset, length, sEventName);
  addTempoNoItem(microsPerQuarter);
}

void SeqTrack::addTempoNoItem(u32 microsPerQuarter) const {
  parentSeq->tempoBPM = 60000000.0 / microsPerQuarter;
  if (readMode == READMODE_CONVERT_TO_MIDI) {
    // Some MIDI engines only recognise tempo events in the first track.
    MidiTrack *pFirstMidiTrack = parentSeq->firstMidiTrack();
    pFirstMidiTrack->insertTempo(microsPerQuarter, pMidiTrack->getDelta());
  }
}

void SeqTrack::insertTempo(u32 offset,
                           u32 length,
                           u32 microsPerQuarter,
                           u32 absTime,
                           const std::string &sEventName) {
  bool isNewOffset = onEvent(offset, length);

  double bpm = 60000000.0 / microsPerQuarter;
  recordSeqEvent<TempoSeqEvent>(isNewOffset, absTime, bpm, offset, length, sEventName);
  insertTempoNoItem(microsPerQuarter, absTime);
}

void SeqTrack::insertTempoNoItem(u32 microsPerQuarter, u32 absTime) const {
  if (readMode == READMODE_CONVERT_TO_MIDI) {
    // Some MIDI tool can recognise tempo event only in the first track.
    MidiTrack *pFirstMidiTrack = parentSeq->firstMidiTrack();
    pFirstMidiTrack->insertTempo(microsPerQuarter, absTime);
  }
}

void SeqTrack::addTempoSlide(u32 offset,
                             u32 length,
                             u32 dur,
                             u32 targMicrosPerQuarter,
                             const std::string &sEventName) {
  addTempoBPMSlide(offset, length, dur, (60000000.0 / targMicrosPerQuarter), sEventName);
}

void SeqTrack::addTempoBPM(u32 offset, u32 length, double bpm, const std::string &sEventName) {
  bool isNewOffset = onEvent(offset, length);

  recordSeqEvent<TempoSeqEvent>(isNewOffset, getTime(), bpm, offset, length, sEventName);
  addTempoBPMNoItem(bpm);
}

void SeqTrack::addTempoBPMNoItem(double bpm) const {
  parentSeq->tempoBPM = bpm;
  if (readMode == READMODE_CONVERT_TO_MIDI) {
    // Some MIDI tool can recognise tempo event only in the first track.
    MidiTrack *pFirstMidiTrack = parentSeq->firstMidiTrack();
    if (!pFirstMidiTrack->bHasEndOfTrack) {
      pFirstMidiTrack->insertTempoBPM(bpm, pMidiTrack->getDelta());
    } else {
      // Adding an event after the EOT is even worse. So here's a little workaround.
      // Test case - SNES Wizardry VI "Rest"
      pMidiTrack->addTempoBPM(bpm);
    }
  }
}

void SeqTrack::addTempoBPMSlide(u32 offset,
                                u32 length,
                                u32 dur,
                                double targBPM,
                                const std::string &sEventName) {
  bool isNewOffset = onEvent(offset, length);

  recordDurSeqEvent<TempoSlideSeqEvent>(isNewOffset, getTime(), dur, targBPM, dur, offset, length, sEventName);

  if (readMode == READMODE_CONVERT_TO_MIDI) {
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

void SeqTrack::addTimeSig(u32 offset,
                          u32 length,
                          u8 numer,
                          u8 denom,
                          u8 ticksPerQuarter,
                          const std::string &sEventName) {
  bool isNewOffset = onEvent(offset, length);

  recordSeqEvent<TimeSigSeqEvent>(isNewOffset, getTime(), numer, denom, ticksPerQuarter, offset, length, sEventName);
  addTimeSigNoItem(numer, denom, ticksPerQuarter);
}

void SeqTrack::addTimeSigNoItem(u8 numer, u8 denom, u8 ticksPerQuarter) const {
  if (readMode == READMODE_CONVERT_TO_MIDI) {
    MidiTrack *pFirstMidiTrack = parentSeq->firstMidiTrack();
    pFirstMidiTrack->addTimeSig(numer, denom, ticksPerQuarter);
  }
}

void SeqTrack::insertTimeSig(u32 offset,
                             u32 length,
                             u8 numer,
                             u8 denom,
                             u8 ticksPerQuarter,
                             u32 absTime,
                             const std::string &sEventName) {
  bool isNewOffset = onEvent(offset, length);

  recordSeqEvent<TimeSigSeqEvent>(isNewOffset, absTime, numer, denom, ticksPerQuarter, offset, length, sEventName);

  if (readMode == READMODE_CONVERT_TO_MIDI) {
    MidiTrack *pFirstMidiTrack = parentSeq->firstMidiTrack();
    pFirstMidiTrack->insertTimeSig(numer, denom, ticksPerQuarter, absTime);
  }
}

void SeqTrack::addEndOfTrack(u32 offset, u32 length, const std::string &sEventName) {
  bool isNewOffset = onEvent(offset, length);

  recordSeqEvent<TrackEndSeqEvent>(isNewOffset, getTime(), offset, length, sEventName);

  if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->addEndOfTrack();
  return addEndOfTrackNoItem();
}

void SeqTrack::addEndOfTrackNoItem() {
  if (readMode == READMODE_FIND_DELTA_LENGTH)
    totalTicks = getTime();
}

void SeqTrack::addControllerEventNoItem(u8 controllerType, u8 controllerValue) const {
  if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->addControllerEvent(channel, controllerType, controllerValue);
}


void SeqTrack::addGlobalTranspose(u32 offset, u32 length, s8 semitones, const std::string &sEventName) {
  bool isNewOffset = onEvent(offset, length);

  recordSeqEvent<TransposeSeqEvent>(isNewOffset, getTime(), semitones, offset, length, sEventName);

  if (readMode == READMODE_CONVERT_TO_MIDI)
    parentSeq->midi->globalTrack.insertGlobalTranspose(getTime(), semitones);
}

void SeqTrack::addMarker(u32 offset,
                         u32 length,
                         const std::string &markername,
                         u8 databyte1,
                         u8 databyte2,
                         const std::string &sEventName,
                         s8 priority,
                         Type type) {
  bool isNewOffset = onEvent(offset, length);

  recordSeqEvent<MarkerSeqEvent>(isNewOffset, getTime(), markername, databyte1, databyte2, offset, length, sEventName, type);
  addMarkerNoItem(markername, databyte1, databyte2, priority);
}

void SeqTrack::addMarkerNoItem(const std::string &markername,
                               u8 databyte1,
                               u8 databyte2,
                               s8 priority) const {
  if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->addMarker(channel, markername, databyte1, databyte2, priority);
}

void SeqTrack::insertMarkerNoItem(u32 absTime,
                                  const std::string &markername,
                                  u8 databyte1,
                                  u8 databyte2,
                                  s8 priority) const {
  if (readMode == READMODE_CONVERT_TO_MIDI)
    pMidiTrack->insertMarker(channel, markername, databyte1, databyte2, priority, absTime);
}

bool SeqTrack::shouldTrackControlFlowState() const {
  return parentSeq != nullptr && parentSeq->shouldTrackControlFlowState();
}

void SeqTrack::addControlFlowState(u32 destinationOffset) {
  if (!shouldTrackControlFlowState()) {
    return;
  }

  ControlFlowState state{destinationOffset, returnOffsets, loopStack};
  visitedControlFlowStates.insert(state);
}

// Check if a tentative control flow state for a given offset has been visited. If it hasn't,
// increment the infiniteLoops counter and return a bool indicating whether to continue parsing.
bool SeqTrack::checkControlStateForInfiniteLoop(u32 offset) {
  // We handle READMODE_FIND_DELTA_LENGTH to determine total time WITH loops.
  // We handle READMODE_ADD_TO_UI so that we stop adding events when the first infinite loop point is reached.
  // We ignore READMODE_CONVERT_TO_MIDI as we use stopTime to determine when to stop parsing.
  if (!shouldTrackControlFlowState())
    return true;

  ControlFlowState state{offset, returnOffsets, loopStack};
  bool isVisited = visitedControlFlowStates.contains(state);
  if (!isVisited)
    return true;

  // We've hit infinite loop point. Increment the infiniteLoop counter and record the time.
  infiniteLoops++;
  totalTicks = getTime();

  if (readMode == READMODE_CONVERT_TO_MIDI) {
    // Workaround for tracks that never increment the time - exit on first infinite loop point
    if (getTime() == 0)
      return false;
    return true;
  }

  // For READMODE_ADD_TO_UI, we quit after visiting every event once. We've done that at this point.
  if (readMode == READMODE_ADD_TO_UI) {
    return false;
  }
  // When we've hit the maximum number of sequence loops, quit parsing
  if (infiniteLoops > parentSeq->conversionContext().sequenceLoops) {
    return false;
  }
  // Otherwise, clear control flow states to allow another full loop of conversion
  visitedControlFlowStates.clear();
  return true;
}

void SeqTrack::pushReturnOffset(u32 returnOffset) {
  returnOffsets.push_back(returnOffset);
}

bool SeqTrack::popReturnOffset(u32 &returnOffset) {
  if (returnOffsets.empty()) {
    return false;
  }

  returnOffset = returnOffsets.back();
  returnOffsets.pop_back();
  return true;
}

// A jump event simply moves the current track offset. It can create infinite loops.
bool SeqTrack::addJump(u32 offset, u32 length, u32 destination, const std::string &sEventName) {
  bool isNewOffset = onEvent(offset, length);
  curOffset = destination;

  recordSeqEvent<JumpSeqEvent>(isNewOffset, getTime(), destination, offset, length, sEventName);

  return checkControlStateForInfiniteLoop(destination);
}

// A call event jumps to an offset and records a return offset (for a return event).
bool SeqTrack::addCall(u32 offset, u32 length, u32 destination, u32 returnOffset, const std::string &sEventName) {
  bool isNewOffset = onEvent(offset, length);
  curOffset = destination;

  pushReturnOffset(returnOffset);

  recordSeqEvent<CallSeqEvent>(isNewOffset, getTime(), destination, returnOffset, offset, length, sEventName);

  return checkControlStateForInfiniteLoop(destination);
}

// A return event marks the end of a call - we jump back to after the call event offset.
bool SeqTrack::addReturn(u32 offset, u32 length, const std::string &sEventName) {
  bool isNewOffset = onEvent(offset, length);

  u32 destination = 0;
  bool hasDestination = popReturnOffset(destination);

  recordSeqEvent<ReturnSeqEvent>(isNewOffset, getTime(), destination, hasDestination, offset, length, sEventName);

  if (!hasDestination) {
    return false;
  }

  curOffset = destination;
  return checkControlStateForInfiniteLoop(destination);
}

// when in FIND_DELTA_LENGTH mode, returns true until we've hit the max number of loops defined in options
bool SeqTrack::addLoopForever(u32 offset, u32 length, const std::string &sEventName) {
  bool isNewOffset = onEvent(offset, length);

  this->infiniteLoops++;
  recordSeqEvent<LoopForeverSeqEvent>(isNewOffset, getTime(), offset, length, sEventName);
  if (readMode == READMODE_ADD_TO_UI) {
    return isNewOffset;
  }
  else if (readMode == READMODE_FIND_DELTA_LENGTH) {
    totalTicks = getTime();
    return (this->infiniteLoops <= parentSeq->conversionContext().sequenceLoops);
  }
  return true;

}
