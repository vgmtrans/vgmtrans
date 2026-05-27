#include "Types.h"
#include "NinSnesSeq.h"

#include "SeqEvent.h"
#include "ScaleConversion.h"
#include "spdlog/fmt/fmt.h"
#include <algorithm>

namespace {
constexpr size_t MAX_TRACKS = kNinSnesTrackCount;
constexpr u8 SEQ_KEYOFS = 24;
}  // namespace

NinSnesTrackState::NinSnesTrackState() {
  resetVars();
}

void NinSnesTrackState::resetVars() {
  loopCount = 0;
  spcTranspose = 0;

  // just in case
  spcNoteDuration = 1;
  spcNoteDurRate = 0xfc;
  spcNoteVolume = 0xfc;

  // Konami:
  konamiLoopStart = 0;
  konamiLoopCount = 0;

  vibrato.reset();
  pitchEnvelope = {};
  pitch.reset(kDefaultPitchBendRangeCents);
}

//  ************
//  NinSnesTrack
//  ************

NinSnesSectionTrackItem::NinSnesSectionTrackItem(NinSnesSeq* parentSeq, u32 offset,
                                                 u32 length,
                                                 const std::string& theName)
    : SeqTrack(parentSeq, offset, length, theName) {
  bDetermineTrackLengthEventByEvent = true;
}

NinSnesTrack::NinSnesTrack(NinSnesSeq* parentSeq, u32 offset, u32 length,
                           const std::string& theName)
    : SeqTrack(parentSeq, offset, length, theName) {
  resetVars();
  bDetermineTrackLengthEventByEvent = true;
}

// Prepares the persistent parser track to play one track entry from this section.
bool NinSnesTrack::prepareSectionTrack(NinSnesSection& section, u32 trackIndex) {
  resetTransientSectionState(trackIndex);
  currentSegment = &section.trackSegment(trackIndex);
  auto* sectionTrackItem = section.track(trackIndex);
  const bool emitSeqEvents = readMode != READMODE_ADD_TO_UI || !currentSegment->uiEventsLoaded;
  loadTrackSegmentInit(currentSegment->rangeOffset,
                       currentSegment->rangeLength,
                       currentSegment->active,
                       currentSegment->startOffset);
  if (sectionTrackItem != nullptr) {
    sectionTrackItem->readMode = readMode;
    sectionTrackItem->channel = channel;
    sectionTrackItem->channelGroup = channelGroup;
    setSeqEventTarget(sectionTrackItem, emitSeqEvents);
  } else {
    setSeqEventTarget(nullptr, emitSeqEvents);
  }

  if (!currentSegment->active && m_hasPersistentRange) {
    setRange(m_persistentRangeStart, m_persistentRangeEnd - m_persistentRangeStart);
  }
  return true;
}

void NinSnesTrack::resetTransientSectionState(u32 trackIndex) {
  resetVisitedAddresses();
  SeqTrack::resetVars();
  resetTransientPlaybackState();
  setChannelAndGroupFromTrkNum(static_cast<int>(trackIndex));
}

void NinSnesTrack::resetTransientPlaybackState() {
  cKeyCorrection = SEQ_KEYOFS;
  transpose = state.spcTranspose;
  m_lastNoteWasPercussion = false;
  nonPercussionProgram = 0;
  currentPercussionProgram = 0;
  currentLogicalProgram.reset();
  intelliLegato = false;
  currentSegment = nullptr;
  resetPersistentRange();
}

void NinSnesTrack::resetPersistentRange() {
  m_hasPersistentRange = false;
  m_persistentRangeStart = 0;
  m_persistentRangeEnd = 0;
}

void NinSnesTrack::includePersistentRange(u32 eventOffset, u32 eventLength) {
  const u32 eventEnd = eventOffset + eventLength;
  if (!m_hasPersistentRange) {
    m_hasPersistentRange = true;
    m_persistentRangeStart = eventOffset;
    m_persistentRangeEnd = eventEnd;
  } else {
    m_persistentRangeStart = std::min(m_persistentRangeStart, eventOffset);
    m_persistentRangeEnd = std::max(m_persistentRangeEnd, eventEnd);
  }

  setRange(m_persistentRangeStart, m_persistentRangeEnd - m_persistentRangeStart);
}

bool NinSnesTrack::onEvent(u32 offset, u32 length) {
  const bool isNewOffset = SeqTrack::onEvent(offset, length);
  if (currentSegment != nullptr && readMode == READMODE_ADD_TO_UI) {
    currentSegment->include(offset, length);
  }
  includePersistentRange(offset, length);
  return isNewOffset;
}

void NinSnesTrack::resetVars() {
  SeqTrack::resetVars();

  state.resetVars();
  resetTransientPlaybackState();
}

void NinSnesTrack::onTickBegin() {
  updateVibratoFade();
  updatePitchSlide();

  // The SPC driver checks for queued EVENT_PITCH_SLIDE commands while a track is waiting before
  // reading the next command, including after note, tie, and rest commands.
  if (deltaTime > 1 && state.pitch.motionTicksRemaining() == 0) {
    consumeQueuedPitchSlide();
  }
}

u8 NinSnesTrack::getEffectiveNoteDuration() const {
  if (intelliLegato) {
    return state.spcNoteDuration;
  }

  u8 duration = (state.spcNoteDuration * state.spcNoteDurRate) >> 8;
  return std::min(std::max(duration, static_cast<u8>(1)),
                  static_cast<u8>(state.spcNoteDuration - 2));
}

// Remembers the melodic program restored after percussion notes.
void NinSnesTrack::rememberMelodicProgram(u32 progNum,
                                          std::optional<u8> logicalProgram) {
  nonPercussionProgram = progNum;
  if (logicalProgram.has_value()) {
    currentLogicalProgram = logicalProgram;
  }

  if (readMode == READMODE_ADD_TO_UI) {
    static_cast<NinSnesSeq*>(parentSeq)->addInstrumentRef(progNum);
  }
}

void NinSnesTrack::restoreNonPercussionProgramIfNeeded() {
  if (!m_lastNoteWasPercussion) {
    return;
  }

  addBankSelectNoItem(0);
  addProgramChangeNoItem(nonPercussionProgram, false);
  m_lastNoteWasPercussion = false;
}

void NinSnesTrack::switchToPercussionProgramIfNeeded(u8 program) {
  if (m_lastNoteWasPercussion && currentPercussionProgram == program) {
    return;
  }

  addBankSelectNoItem(127);
  addProgramChangeNoItem(program, false);
  currentPercussionProgram = program;
  m_lastNoteWasPercussion = true;
}

void NinSnesTrack::addProgramChangeEvent(u32 offset, u32 length, u32 progNum,
                                         bool requireBank, const std::string& eventName,
                                         std::optional<u8> logicalProgram) {
  rememberMelodicProgram(progNum, logicalProgram);

  bool isNewOffset = onEvent(offset, length);
  recordSeqEvent<ProgChangeSeqEvent>(isNewOffset, getTime(), progNum, offset, length, eventName);

  if (!m_lastNoteWasPercussion) {
    addProgramChangeNoItem(progNum, requireBank);
  }
}

NinSnesSeq& NinSnesTrack::seq() const {
  return *static_cast<NinSnesSeq*>(parentSeq);
}

NinSnesIntelliModeId NinSnesTrack::intelliMode() const {
  return seq().profile().intelliMode;
}

void NinSnesTrack::readStandardNoteParam(u32 beginOffset, u8 statusByte,
                                         std::string& desc) {
  auto& parentSeq = seq();
  state.spcNoteDuration = statusByte;
  desc = fmt::format("Duration: {:d}", state.spcNoteDuration);

  if (curOffset + 1 < 0x10000 && readByte(curOffset) <= 0x7f) {
    const u8 quantizeAndVelocity = readByte(curOffset++);
    const u8 durIndex = (quantizeAndVelocity >> 4) & 7;
    const u8 velIndex = quantizeAndVelocity & 15;

    state.spcNoteDurRate = parentSeq.durRateTable[durIndex];
    state.spcNoteVolume = parentSeq.volumeTable[velIndex];

    fmt::format_to(std::back_inserter(desc),
                   "  Quantize: {:d} ({:d}/256)  Velocity: {:d} ({:d}/256)", durIndex,
                   state.spcNoteDurRate, velIndex, state.spcNoteVolume);
  }

  addGenericEvent(beginOffset, curOffset - beginOffset, "Note Param", desc, Type::DurationChange);
}

void NinSnesTrack::readLemmingsNoteParam(u32 beginOffset, u8 statusByte,
                                         std::string& desc) {
  state.spcNoteDuration = statusByte;
  desc = fmt::format("Duration: {:d}", state.spcNoteDuration);

  if (curOffset + 1 < 0x10000 && readByte(curOffset) <= 0x7f) {
    const u8 durByte = readByte(curOffset++);
    state.spcNoteDurRate = (durByte << 1) + (durByte >> 1) + (durByte & 1);
    fmt::format_to(std::back_inserter(desc), "  Quantize: {} ({}/256)", durByte,
                   state.spcNoteDurRate);

    if (curOffset + 1 < 0x10000 && readByte(curOffset) <= 0x7f) {
      const u8 velByte = readByte(curOffset++);
      state.spcNoteVolume = velByte << 1;
      fmt::format_to(std::back_inserter(desc), "  Velocity: {} ({}/256)", velByte,
                     state.spcNoteVolume);
    }
  }

  addGenericEvent(beginOffset, curOffset - beginOffset, "Note Param", desc, Type::DurationChange);
}

bool NinSnesTrack::handleCoreEvent(NinSnesSeqEventType eventType, u32 beginOffset,
                                   u8 statusByte, std::string& desc,
                                   bool& continueReading) {
  auto& parentSeq = seq();

  switch (eventType) {
    case EVENT_UNKNOWN0:
      desc = fmt::format("Event: 0x{:02X}", statusByte);
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
      return true;

    case EVENT_UNKNOWN1: {
      const u8 arg1 = readByte(curOffset++);
      desc = fmt::format("Event: 0x{:02X}  Arg1: {:d}", statusByte, arg1);
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
      return true;
    }

    case EVENT_UNKNOWN2: {
      const u8 arg1 = readByte(curOffset++);
      const u8 arg2 = readByte(curOffset++);
      desc = fmt::format("Event: 0x{:02X}  Arg1: {:d}  Arg2: {:d}", statusByte, arg1, arg2);
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
      return true;
    }

    case EVENT_UNKNOWN3: {
      const u8 arg1 = readByte(curOffset++);
      const u8 arg2 = readByte(curOffset++);
      const u8 arg3 = readByte(curOffset++);
      desc = fmt::format("Event: 0x{:02X}  Arg1: {:d}  Arg2: {:d}  Arg3: {:d}", statusByte, arg1,
                         arg2, arg3);
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
      return true;
    }

    case EVENT_UNKNOWN4: {
      const u8 arg1 = readByte(curOffset++);
      const u8 arg2 = readByte(curOffset++);
      const u8 arg3 = readByte(curOffset++);
      const u8 arg4 = readByte(curOffset++);
      desc = fmt::format("Event: 0x{:02X}  Arg1: {:d}  Arg2: {:d}  Arg3: {:d}  Arg4: {:d}",
                         statusByte, arg1, arg2, arg3, arg4);
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
      return true;
    }

    case EVENT_NOP:
      addGenericEvent(beginOffset, curOffset - beginOffset, "NOP", desc, Type::Nop);
      return true;

    case EVENT_NOP1:
      curOffset++;
      addGenericEvent(beginOffset, curOffset - beginOffset, "NOP", desc, Type::Nop);
      return true;

    case EVENT_END:
      if (state.loopCount == 0) {
        if (readMode == READMODE_FIND_DELTA_LENGTH) {
          for (size_t trackIndex = 0; trackIndex < parentSeq.aTracks.size(); trackIndex++) {
            parentSeq.aTracks[trackIndex]->totalTicks = getTime();
          }
        } else if (readMode == READMODE_CONVERT_TO_MIDI) {
          for (size_t trackIndex = 0; trackIndex < parentSeq.aTracks.size(); trackIndex++) {
            parentSeq.aTracks[trackIndex]->limitPrevDurNoteEnd();
          }
        }

        parentSeq.deactivateAllTracks();
        continueReading = false;
        parentSeq.bIncTickAfterProcessingTracks = false;
      } else {
        state.loopCount--;
        curOffset =
            (state.loopCount == 0) ? state.loopReturnAddress : state.loopStartAddress;
      }
      return true;

    case EVENT_NOTE_PARAM:
      readStandardNoteParam(beginOffset, statusByte, desc);
      return true;

    case EVENT_NOTE: {
      const u8 noteNumber = statusByte - parentSeq.STATUS_NOTE_MIN;
      const u8 duration = getEffectiveNoteDuration();
      restoreNonPercussionProgramIfNeeded();
      beginNotePitch(noteNumber);
      addNoteByDur(beginOffset, curOffset - beginOffset, noteNumber, state.spcNoteVolume / 2,
                   duration, "Note");
      consumeQueuedPitchSlide();
      m_lastNoteWasPercussion = false;
      addTime(state.spcNoteDuration);
      return true;
    }

    case EVENT_TIE: {
      u8 duration = (state.spcNoteDuration * state.spcNoteDurRate) >> 8;
      duration = std::min(std::max(duration, static_cast<u8>(1)),
                          static_cast<u8>(state.spcNoteDuration - 2));
      desc = fmt::format("Duration: {:d}", duration);
      makePrevDurNoteEnd(getTime() + duration);
      addTie(beginOffset, curOffset - beginOffset, duration, "Tie", desc);
      consumeQueuedPitchSlide();
      addTime(state.spcNoteDuration);
      return true;
    }

    case EVENT_REST:
      addRest(beginOffset, curOffset - beginOffset, state.spcNoteDuration);
      consumeQueuedPitchSlide();
      return true;

    case EVENT_PERCUSSION_NOTE: {
      const u8 slot = statusByte - parentSeq.STATUS_PERCUSSION_NOTE_MIN;
      const u8 duration = getEffectiveNoteDuration();

      if (handleIntelliPercussionNote(beginOffset, slot, duration)) {
        return true;
      }

      u8 instrIndex = 0;
      parentSeq.resolveProgramNumber(static_cast<u8>(slot + parentSeq.spcPercussionBase),
                                     &instrIndex);

      const u8 noteIndex = 0x24 + instrIndex - parentSeq.spcPercussionBase;
      parentSeq.addPercussionInstrNoteMapping(instrIndex, noteIndex, parentSeq.globalTranspose);

      switchToPercussionProgramIfNeeded();
      beginNotePitch(static_cast<u8>(noteIndex - parentSeq.globalTranspose));
      addPercNoteByDur(beginOffset, curOffset - beginOffset,
                       static_cast<s8>(noteIndex - cKeyCorrection - parentSeq.globalTranspose),
                       state.spcNoteVolume / 2, duration, "Percussion Note");
      consumeQueuedPitchSlide();
      m_lastNoteWasPercussion = true;
      addTime(state.spcNoteDuration);
      return true;
    }

    case EVENT_PROGCHANGE: {
      const u8 instrumentByte = readByte(curOffset++);
      u8 logicalProgNum = 0;
      const u32 newProgNum = parentSeq.resolveProgramNumber(instrumentByte, &logicalProgNum);
      addProgramChangeEvent(beginOffset, curOffset - beginOffset, newProgNum, true,
                            "Program Change", logicalProgNum);
      return true;
    }

    case EVENT_CALL: {
      const u16 dest = getShortAddress(curOffset);
      curOffset += 2;
      const u8 times = readByte(curOffset++);

      state.loopReturnAddress = curOffset;
      state.loopStartAddress = dest;
      state.loopCount = times;

      desc = fmt::format("Destination: ${:04X}  Times: {:d}", dest, times);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Pattern Play", desc,
                      Type::RepeatStart);
      addPendingEndEvent(statusByte, desc);
      curOffset = state.loopStartAddress;
      return true;
    }

    default:
      return false;
  }
}

bool NinSnesTrack::handleControllerEvent(NinSnesSeqEventType eventType, u32 beginOffset,
                                         std::string& desc) {
  auto& parentSeq = seq();

  switch (eventType) {
    case EVENT_PAN: {
      const u8 newPan = readByte(curOffset++);
      double volumeScale;
      bool reverseLeft;
      bool reverseRight;
      const s8 midiPan = calculatePanValue(newPan, volumeScale, reverseLeft, reverseRight);
      addPan(beginOffset, curOffset - beginOffset, midiPan);
      addExpressionNoItem(convertPercentAmpToStdMidiVal(volumeScale));
      return true;
    }

    case EVENT_PAN_FADE: {
      const u8 fadeLength = readByte(curOffset++);
      const u8 newPan = readByte(curOffset++);
      double volumeLeft;
      double volumeRight;
      getVolumeBalance(newPan << 8, volumeLeft, volumeRight);
      const u8 midiPan = convertVolumeBalanceToStdMidiPan(volumeLeft, volumeRight);
      addPanSlide(beginOffset, curOffset - beginOffset, fadeLength, midiPan);
      return true;
    }

    case EVENT_VIBRATO_ON: {
      auto& vibrato = state.vibrato;
      const u8 vibratoDelay = readByte(curOffset++);
      const u8 vibratoRate = readByte(curOffset++);
      const u8 vibratoDepth = readByte(curOffset++);
      vibrato.configure(vibratoDelay, vibratoRate, vibratoDepth);
      desc = fmt::format("Delay: {:d}  Rate: {:d}  Depth: {:d}", vibrato.delay(), vibrato.rate(),
                         vibrato.depth());
      addGenericEvent(beginOffset, curOffset - beginOffset, "Vibrato", desc, Type::Vibrato);
      applyConfiguredVibrato();
      return true;
    }

    case EVENT_VIBRATO_OFF:
      state.vibrato.clearConfig();
      addGenericEvent(beginOffset, curOffset - beginOffset, "Vibrato Off", desc, Type::Vibrato);
      applyConfiguredVibrato();
      return true;

    case EVENT_MASTER_VOLUME: {
      const u8 newVol = readByte(curOffset++);
      addMasterVol(beginOffset, curOffset - beginOffset, newVol / 2);
      return true;
    }

    case EVENT_MASTER_VOLUME_FADE: {
      const u8 fadeLength = readByte(curOffset++);
      const u8 newVol = readByte(curOffset++);
      desc = fmt::format("Length: {:d}  Volume: {:d}", fadeLength, newVol);
      addMastVolSlide(beginOffset, curOffset - beginOffset, fadeLength, newVol / 2);
      return true;
    }

    case EVENT_TEMPO: {
      const u8 newTempo = readByte(curOffset++);
      parentSeq.setImmediateTempo(newTempo);
      addTempoBPM(beginOffset, curOffset - beginOffset, parentSeq.getTempoInBPM(newTempo));
      parentSeq.syncTempoDependentTracks();
      return true;
    }

    case EVENT_TEMPO_FADE: {
      const u8 fadeLength = readByte(curOffset++);
      const u8 newTempo = readByte(curOffset++);
      if (fadeLength == 0) {
        parentSeq.setImmediateTempo(newTempo);
        addTempoBPM(beginOffset, curOffset - beginOffset, parentSeq.getTempoInBPM(newTempo));
        parentSeq.syncTempoDependentTracks();
      } else {
        const bool isNewOffset = onEvent(beginOffset, curOffset - beginOffset);
        recordDurSeqEvent<TempoSlideSeqEvent>(isNewOffset, getTime(), fadeLength,
                                              parentSeq.getTempoInBPM(newTempo), fadeLength,
                                              beginOffset, curOffset - beginOffset, "Tempo Slide");
        parentSeq.startTempoFade(fadeLength, newTempo);
      }
      return true;
    }

    case EVENT_GLOBAL_TRANSPOSE: {
      const s8 semitones = readByte(curOffset++);
      parentSeq.globalTranspose = semitones;
      addGlobalTranspose(beginOffset, curOffset - beginOffset, semitones);
      return true;
    }

    case EVENT_TRANSPOSE: {
      const s8 semitones = readByte(curOffset++);
      state.spcTranspose = semitones;
      addTranspose(beginOffset, curOffset - beginOffset, semitones);
      return true;
    }

    case EVENT_TREMOLO_ON: {
      const u8 tremoloDelay = readByte(curOffset++);
      const u8 tremoloRate = readByte(curOffset++);
      const u8 tremoloDepth = readByte(curOffset++);
      desc = fmt::format("Delay: {:d}  Rate: {:d}  Depth: {:d}", tremoloDelay, tremoloRate,
                         tremoloDepth);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Tremolo", desc, Type::Tremelo);
      return true;
    }

    case EVENT_TREMOLO_OFF:
      addGenericEvent(beginOffset, curOffset - beginOffset, "Tremolo Off", desc, Type::Tremelo);
      return true;

    case EVENT_VOLUME: {
      const u8 newVol = readByte(curOffset++);
      addVol(beginOffset, curOffset - beginOffset, newVol / 2);
      return true;
    }

    case EVENT_VOLUME_FADE: {
      const u8 fadeLength = readByte(curOffset++);
      const u8 newVol = readByte(curOffset++);
      addVolSlide(beginOffset, curOffset - beginOffset, fadeLength, newVol / 2);
      return true;
    }

    case EVENT_VIBRATO_FADE: {
      auto& vibrato = state.vibrato;
      const u8 fadeLength = readByte(curOffset++);
      desc = fmt::format("Length: {:d}", fadeLength);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Vibrato Fade", desc, Type::Vibrato);
      vibrato.setReusableFadeToConfiguredDepth(fadeLength);
      return true;
    }

    case EVENT_PITCH_ENVELOPE_TO: {
      const u8 pitchEnvDelay = readByte(curOffset++);
      const u8 pitchEnvLength = readByte(curOffset++);
      const s8 pitchEnvSemitones = static_cast<s8>(readByte(curOffset++));
      state.pitchEnvelope = {
        NinSnesTrackState::StoredPitchEnvelope::Mode::To,
        pitchEnvDelay,
        pitchEnvLength,
        pitchEnvSemitones,
      };
      desc = fmt::format("Delay: {:d}  Length: {:d}  Semitones: {:d}", pitchEnvDelay,
                         pitchEnvLength, pitchEnvSemitones);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Pitch Envelope (To)", desc,
                      Type::PitchEnvelope);
      return true;
    }

    case EVENT_PITCH_ENVELOPE_FROM: {
      const u8 pitchEnvDelay = readByte(curOffset++);
      const u8 pitchEnvLength = readByte(curOffset++);
      const s8 pitchEnvSemitones = static_cast<s8>(readByte(curOffset++));
      state.pitchEnvelope = {
        NinSnesTrackState::StoredPitchEnvelope::Mode::From,
        pitchEnvDelay,
        pitchEnvLength,
        pitchEnvSemitones,
      };
      desc = fmt::format("Delay: {:d}  Length: {:d}  Semitones: {:d}", pitchEnvDelay,
                         pitchEnvLength, pitchEnvSemitones);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Pitch Envelope (From)", desc,
                      Type::PitchEnvelope);
      return true;
    }

    case EVENT_PITCH_ENVELOPE_OFF:
      state.pitchEnvelope = {};
      addGenericEvent(beginOffset, curOffset - beginOffset, "Pitch Envelope Off", desc,
                      Type::PitchEnvelope);
      return true;

    case EVENT_TUNING: {
      const u8 newTuning = readByte(curOffset++);
      addFineTuning(beginOffset, curOffset - beginOffset, (newTuning / 256.0) * 100.0);
      return true;
    }

    case EVENT_ECHO_ON: {
      const u8 spcEON = readByte(curOffset++);
      const u8 spcEVOL_L = readByte(curOffset++);
      const u8 spcEVOL_R = readByte(curOffset++);

      desc = "Channels: ";
      for (int channelNo = MAX_TRACKS - 1; channelNo >= 0; channelNo--) {
        if ((spcEON & (1 << channelNo)) != 0) {
          fmt::format_to(std::back_inserter(desc), "{}", channelNo);
          parentSeq.aTracks[channelNo]->addReverbNoItem(40);
        } else {
          desc.push_back('-');
          parentSeq.aTracks[channelNo]->addReverbNoItem(0);
        }
      }

      fmt::format_to(std::back_inserter(desc), "  Volume Left: {:d}  Volume Right: {:d}", spcEVOL_L,
                     spcEVOL_R);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Echo", desc, Type::Reverb);
      return true;
    }

    case EVENT_ECHO_OFF:
      addGenericEvent(beginOffset, curOffset - beginOffset, "Echo Off", desc, Type::Reverb);
      return true;

    case EVENT_ECHO_PARAM: {
      const u8 spcEDL = readByte(curOffset++);
      const u8 spcEFB = readByte(curOffset++);
      const u8 spcFIR = readByte(curOffset++);
      desc = fmt::format("Delay: {:d}  Feedback: {:d}  FIR: {:d}", spcEDL, spcEFB, spcFIR);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Echo Param", desc, Type::Reverb);
      return true;
    }

    case EVENT_ECHO_VOLUME_FADE: {
      const u8 fadeLength = readByte(curOffset++);
      const u8 spcEVOL_L = readByte(curOffset++);
      const u8 spcEVOL_R = readByte(curOffset++);
      desc = fmt::format("Length: {:d}  Volume Left: {:d}  Volume Right: {:d}", fadeLength,
                         spcEVOL_L, spcEVOL_R);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Echo Volume Fade", desc,
                      Type::Reverb);
      return true;
    }

    case EVENT_PITCH_SLIDE: {
      const auto slide = readPitchSlide(beginOffset);
      addPitchSlideEvent(slide);
      beginPitchSlide(slide);
      return true;
    }

    case EVENT_PERCUSSION_PATCH_BASE: {
      const u8 percussionBase = readByte(curOffset++);
      parentSeq.spcPercussionBase = percussionBase;
      if (intelliMode() == NinSnesIntelliModeId::Ta) {
        parentSeq.setIntelliCustomPercTableEnabled(false);
      }

      desc = fmt::format("Percussion Base: {:d}", percussionBase);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Percussion Base", desc,
                      Type::ChangeState);
      return true;
    }

    default:
      return false;
  }
}

bool NinSnesTrack::handleVariantEvent(NinSnesSeqEventType eventType, u32 beginOffset,
                                      u8 statusByte, std::string& desc) {
  auto& parentSeq = seq();

  switch (eventType) {
    case EVENT_RD2_PROGCHANGE_AND_ADSR: {
      const u8 instrumentByte = readByte(curOffset++);
      curOffset += 2;
      u8 logicalProgNum = 0;
      const u32 newProgNum = parentSeq.resolveProgramNumber(instrumentByte, &logicalProgNum);
      addProgramChangeEvent(beginOffset, curOffset - beginOffset, newProgNum, true,
                            "Program Change & ADSR", logicalProgNum);
      return true;
    }

    case EVENT_KONAMI_LOOP_START:
      addGenericEvent(beginOffset, curOffset - beginOffset, "Loop Start", desc, Type::RepeatStart);
      state.konamiLoopStart = curOffset;
      state.konamiLoopCount = 0;
      return true;

    case EVENT_KONAMI_LOOP_END: {
      const u8 times = readByte(curOffset++);
      const s8 volumeDelta = readByte(curOffset++);
      const s8 pitchDelta = readByte(curOffset++);
      desc = fmt::format("Times: {:d}  Volume Delta: {:d}  Pitch Delta: {:d}/16 semitones", times,
                         volumeDelta, pitchDelta);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Loop End", desc, Type::RepeatEnd);

      state.konamiLoopCount++;
      if (state.konamiLoopCount != times) {
        curOffset = state.konamiLoopStart;
      }
      return true;
    }

    case EVENT_KONAMI_ADSR_AND_GAIN: {
      const u8 adsr1 = readByte(curOffset++);
      const u8 adsr2 = readByte(curOffset++);
      const u8 gain = readByte(curOffset++);
      desc = fmt::format("ADSR(1): ${:02X}  ADSR(2): ${:02X}  GAIN: ${:02X}", adsr1, adsr2, gain);
      addGenericEvent(beginOffset, curOffset - beginOffset, "ADSR/GAIN", desc, Type::Adsr);
      return true;
    }

    case EVENT_LEMMINGS_NOTE_PARAM:
      readLemmingsNoteParam(beginOffset, statusByte, desc);
      return true;

    case EVENT_QUINTET_TUNING: {
      const u8 newTuning = readByte(curOffset++);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Fine Tuning", desc, Type::FineTune);
      addFineTuningNoItem((newTuning / 256.0) * 61.8);
      return true;
    }

    case EVENT_QUINTET_ADSR: {
      const u8 adsr1 = readByte(curOffset++);
      const u8 sustainRate = readByte(curOffset++);
      const u8 sustainLevel = readByte(curOffset++);
      const u8 adsr2 = (sustainLevel << 5) | sustainRate;
      desc =
          fmt::format("ADSR(1): ${:02X}  Sustain Rate: {:d} Sustain Level: {}  (ADSR(2): ${:02X})",
                      adsr1, sustainRate, sustainLevel, adsr2);
      addGenericEvent(beginOffset, curOffset - beginOffset, "ADSR", desc, Type::Adsr);
      return true;
    }

    default:
      return false;
  }
}

void NinSnesTrack::addPendingEndEvent(u8 statusByte, const std::string& desc) {
  const auto& parentSeq = seq();
  if (curOffset + 1 <= 0x10000 && statusByte != parentSeq.STATUS_END &&
      readByte(curOffset) == parentSeq.STATUS_END) {
    addGenericEvent(curOffset, 1, (state.loopCount == 0) ? "Section End" : "Pattern End", desc,
                    (state.loopCount == 0) ? Type::TrackEnd : Type::RepeatEnd);
  }
}

bool NinSnesTrack::readEvent() {
  const auto& parentSeq = seq();
  const u32 beginOffset = curOffset;
  if (curOffset >= 0x10000) {
    return false;
  }

  const u8 statusByte = readByte(curOffset++);
  bool continueReading = true;
  std::string desc;

  NinSnesSeqEventType eventType = static_cast<NinSnesSeqEventType>(0);
  if (const auto pEventType = parentSeq.EventMap.find(statusByte);
      pEventType != parentSeq.EventMap.end()) {
    eventType = pEventType->second;
  }

  if (!handleCoreEvent(eventType, beginOffset, statusByte, desc, continueReading) &&
      !handleControllerEvent(eventType, beginOffset, desc) &&
      !handleVariantEvent(eventType, beginOffset, statusByte, desc) &&
      !handleIntelliEvent(eventType, beginOffset, statusByte, desc)) {
    const auto descr = logEvent(statusByte);
    addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", descr);
    continueReading = false;
  }

  addPendingEndEvent(statusByte, desc);
  return continueReading;
}

u16 NinSnesTrack::convertToApuAddress(u16 offset) {
  return seq().convertToAPUAddress(offset);
}

u16 NinSnesTrack::getShortAddress(u32 offset) {
  return seq().getShortAddress(offset);
}

void NinSnesTrack::getVolumeBalance(u16 pan, double& volumeLeft, double& volumeRight) {
  const auto& parentSeq = seq();
  getNinSnesVolumeBalance(parentSeq.profile(), parentSeq.panTable, pan, volumeLeft, volumeRight);
}

u8 NinSnesTrack::readPanTable(u16 pan) {
  const auto& parentSeq = seq();
  return readNinSnesPanTable(parentSeq.profile(), parentSeq.panTable, pan);
}

s8 NinSnesTrack::calculatePanValue(u8 pan, double& volumeScale, bool& reverseLeft,
                                       bool& reverseRight) {
  const auto& parentSeq = seq();
  const auto panState = decodeNinSnesPanValue(parentSeq.profile(), pan);
  reverseLeft = panState.reverseLeft;
  reverseRight = panState.reverseRight;

  double volumeLeft;
  double volumeRight;
  getVolumeBalance(panState.panIndex << 8, volumeLeft, volumeRight);

  // TODO: correct volume scale of TOSE sequence
  s8 midiPan = convertVolumeBalanceToStdMidiPan(volumeLeft, volumeRight, &volumeScale);
  volumeScale = std::min(volumeScale, 1.0);  // workaround

  return midiPan;
}
