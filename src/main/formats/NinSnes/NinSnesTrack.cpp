#include "NinSnesSeq.h"

#include "SeqEvent.h"
#include "ScaleConversion.h"
#include "spdlog/fmt/fmt.h"
#include <algorithm>

namespace {
constexpr size_t MAX_TRACKS = kNinSnesTrackCount;
constexpr uint8_t SEQ_KEYOFS = 24;
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
}

//  ************
//  NinSnesTrack
//  ************

NinSnesSectionTrackItem::NinSnesSectionTrackItem(NinSnesSeq* parentSeq, uint32_t offset,
                                                 uint32_t length,
                                                 const std::string& theName)
    : SeqTrack(parentSeq, offset, length, theName) {
  bDetermineTrackLengthEventByEvent = true;
}

NinSnesTrack::NinSnesTrack(NinSnesSeq* parentSeq, uint32_t offset, uint32_t length,
                           const std::string& theName)
    : SeqTrack(parentSeq, offset, length, theName) {
  resetVars();
  bDetermineTrackLengthEventByEvent = true;
}

// Prepares the persistent parser track to play one track entry from this section.
bool NinSnesTrack::prepareSectionTrack(NinSnesSection& section, uint32_t trackIndex) {
  resetTransientSectionState(trackIndex);
  currentSegment = &section.trackSegment(trackIndex);
  auto* sectionTrackItem = section.track(trackIndex);
  const bool emitSeqEvents = readMode != READMODE_ADD_TO_UI || !currentSegment->uiEventsLoaded;
  const bool loaded = loadTrackSegmentInit(currentSegment->rangeOffset,
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
  return loaded;
}

void NinSnesTrack::resetTransientSectionState(uint32_t trackIndex) {
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

void NinSnesTrack::includePersistentRange(uint32_t eventOffset, uint32_t eventLength) {
  const uint32_t eventEnd = eventOffset + eventLength;
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

bool NinSnesTrack::onEvent(uint32_t offset, uint32_t length) {
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

uint8_t NinSnesTrack::getEffectiveNoteDuration() const {
  if (intelliLegato) {
    return state.spcNoteDuration;
  }

  uint8_t duration = (state.spcNoteDuration * state.spcNoteDurRate) >> 8;
  return std::min(std::max(duration, static_cast<uint8_t>(1)),
                  static_cast<uint8_t>(state.spcNoteDuration - 2));
}

// Remembers the melodic program restored after percussion notes.
void NinSnesTrack::rememberMelodicProgram(uint32_t progNum,
                                          std::optional<uint8_t> logicalProgram) {
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

void NinSnesTrack::switchToPercussionProgramIfNeeded(uint8_t program) {
  if (m_lastNoteWasPercussion && currentPercussionProgram == program) {
    return;
  }

  addBankSelectNoItem(127);
  addProgramChangeNoItem(program, false);
  currentPercussionProgram = program;
  m_lastNoteWasPercussion = true;
}

void NinSnesTrack::addProgramChangeEvent(uint32_t offset, uint32_t length, uint32_t progNum,
                                         bool requireBank, const std::string& eventName,
                                         std::optional<uint8_t> logicalProgram) {
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

void NinSnesTrack::readStandardNoteParam(uint32_t beginOffset, uint8_t statusByte,
                                         std::string& desc) {
  auto& parentSeq = seq();
  state.spcNoteDuration = statusByte;
  desc = fmt::format("Duration: {:d}", state.spcNoteDuration);

  if (curOffset + 1 < 0x10000 && readByte(curOffset) <= 0x7f) {
    const uint8_t quantizeAndVelocity = readByte(curOffset++);
    const uint8_t durIndex = (quantizeAndVelocity >> 4) & 7;
    const uint8_t velIndex = quantizeAndVelocity & 15;

    state.spcNoteDurRate = parentSeq.durRateTable[durIndex];
    state.spcNoteVolume = parentSeq.volumeTable[velIndex];

    fmt::format_to(std::back_inserter(desc),
                   "  Quantize: {:d} ({:d}/256)  Velocity: {:d} ({:d}/256)", durIndex,
                   state.spcNoteDurRate, velIndex, state.spcNoteVolume);
  }

  addGenericEvent(beginOffset, curOffset - beginOffset, "Note Param", desc, Type::DurationChange);
}

void NinSnesTrack::readLemmingsNoteParam(uint32_t beginOffset, uint8_t statusByte,
                                         std::string& desc) {
  state.spcNoteDuration = statusByte;
  desc = fmt::format("Duration: {:d}", state.spcNoteDuration);

  if (curOffset + 1 < 0x10000 && readByte(curOffset) <= 0x7f) {
    const uint8_t durByte = readByte(curOffset++);
    state.spcNoteDurRate = (durByte << 1) + (durByte >> 1) + (durByte & 1);
    fmt::format_to(std::back_inserter(desc), "  Quantize: {} ({}/256)", durByte,
                   state.spcNoteDurRate);

    if (curOffset + 1 < 0x10000 && readByte(curOffset) <= 0x7f) {
      const uint8_t velByte = readByte(curOffset++);
      state.spcNoteVolume = velByte << 1;
      fmt::format_to(std::back_inserter(desc), "  Velocity: {} ({}/256)", velByte,
                     state.spcNoteVolume);
    }
  }

  addGenericEvent(beginOffset, curOffset - beginOffset, "Note Param", desc, Type::DurationChange);
}

bool NinSnesTrack::handleCoreEvent(NinSnesSeqEventType eventType, uint32_t beginOffset,
                                   uint8_t statusByte, std::string& desc,
                                   bool& continueReading) {
  auto& parentSeq = seq();

  switch (eventType) {
    case EVENT_UNKNOWN0:
      desc = fmt::format("Event: 0x{:02X}", statusByte);
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
      return true;

    case EVENT_UNKNOWN1: {
      const uint8_t arg1 = readByte(curOffset++);
      desc = fmt::format("Event: 0x{:02X}  Arg1: {:d}", statusByte, arg1);
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
      return true;
    }

    case EVENT_UNKNOWN2: {
      const uint8_t arg1 = readByte(curOffset++);
      const uint8_t arg2 = readByte(curOffset++);
      desc = fmt::format("Event: 0x{:02X}  Arg1: {:d}  Arg2: {:d}", statusByte, arg1, arg2);
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
      return true;
    }

    case EVENT_UNKNOWN3: {
      const uint8_t arg1 = readByte(curOffset++);
      const uint8_t arg2 = readByte(curOffset++);
      const uint8_t arg3 = readByte(curOffset++);
      desc = fmt::format("Event: 0x{:02X}  Arg1: {:d}  Arg2: {:d}  Arg3: {:d}", statusByte, arg1,
                         arg2, arg3);
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
      return true;
    }

    case EVENT_UNKNOWN4: {
      const uint8_t arg1 = readByte(curOffset++);
      const uint8_t arg2 = readByte(curOffset++);
      const uint8_t arg3 = readByte(curOffset++);
      const uint8_t arg4 = readByte(curOffset++);
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
      const uint8_t noteNumber = statusByte - parentSeq.STATUS_NOTE_MIN;
      const uint8_t duration = getEffectiveNoteDuration();
      restoreNonPercussionProgramIfNeeded();
      addNoteByDur(beginOffset, curOffset - beginOffset, noteNumber, state.spcNoteVolume / 2,
                   duration, "Note");
      m_lastNoteWasPercussion = false;
      addTime(state.spcNoteDuration);
      return true;
    }

    case EVENT_TIE: {
      uint8_t duration = (state.spcNoteDuration * state.spcNoteDurRate) >> 8;
      duration = std::min(std::max(duration, static_cast<uint8_t>(1)),
                          static_cast<uint8_t>(state.spcNoteDuration - 2));
      desc = fmt::format("Duration: {:d}", duration);
      makePrevDurNoteEnd(getTime() + duration);
      addTie(beginOffset, curOffset - beginOffset, duration, "Tie", desc);
      addTime(state.spcNoteDuration);
      return true;
    }

    case EVENT_REST:
      addRest(beginOffset, curOffset - beginOffset, state.spcNoteDuration);
      return true;

    case EVENT_PERCUSSION_NOTE: {
      const uint8_t slot = statusByte - parentSeq.STATUS_PERCUSSION_NOTE_MIN;
      const uint8_t duration = getEffectiveNoteDuration();

      if (handleIntelliPercussionNote(beginOffset, slot, duration)) {
        return true;
      }

      uint8_t instrIndex = 0;
      parentSeq.resolveProgramNumber(static_cast<uint8_t>(slot + parentSeq.spcPercussionBase),
                                     &instrIndex);

      const uint8_t noteIndex = 0x24 + instrIndex - parentSeq.spcPercussionBase;
      parentSeq.addPercussionInstrNoteMapping(instrIndex, noteIndex, parentSeq.globalTranspose);

      switchToPercussionProgramIfNeeded();
      addPercNoteByDur(beginOffset, curOffset - beginOffset,
                       static_cast<int8_t>(noteIndex - cKeyCorrection - parentSeq.globalTranspose),
                       state.spcNoteVolume / 2, duration, "Percussion Note");
      m_lastNoteWasPercussion = true;
      addTime(state.spcNoteDuration);
      return true;
    }

    case EVENT_PROGCHANGE: {
      const uint8_t instrumentByte = readByte(curOffset++);
      uint8_t logicalProgNum = 0;
      const uint32_t newProgNum = parentSeq.resolveProgramNumber(instrumentByte, &logicalProgNum);
      addProgramChangeEvent(beginOffset, curOffset - beginOffset, newProgNum, true,
                            "Program Change", logicalProgNum);
      return true;
    }

    case EVENT_CALL: {
      const uint16_t dest = getShortAddress(curOffset);
      curOffset += 2;
      const uint8_t times = readByte(curOffset++);

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

bool NinSnesTrack::handleControllerEvent(NinSnesSeqEventType eventType, uint32_t beginOffset,
                                         std::string& desc) {
  auto& parentSeq = seq();

  switch (eventType) {
    case EVENT_PAN: {
      const uint8_t newPan = readByte(curOffset++);
      double volumeScale;
      bool reverseLeft;
      bool reverseRight;
      const int8_t midiPan = calculatePanValue(newPan, volumeScale, reverseLeft, reverseRight);
      addPan(beginOffset, curOffset - beginOffset, midiPan);
      addExpressionNoItem(convertPercentAmpToStdMidiVal(volumeScale));
      return true;
    }

    case EVENT_PAN_FADE: {
      const uint8_t fadeLength = readByte(curOffset++);
      const uint8_t newPan = readByte(curOffset++);
      double volumeLeft;
      double volumeRight;
      getVolumeBalance(newPan << 8, volumeLeft, volumeRight);
      const uint8_t midiPan = convertVolumeBalanceToStdMidiPan(volumeLeft, volumeRight);
      addPanSlide(beginOffset, curOffset - beginOffset, fadeLength, midiPan);
      return true;
    }

    case EVENT_VIBRATO_ON: {
      const uint8_t vibratoDelay = readByte(curOffset++);
      const uint8_t vibratoRate = readByte(curOffset++);
      const uint8_t vibratoDepth = readByte(curOffset++);
      desc = fmt::format("Delay: {:d}  Rate: {:d}  Depth: {:d}", vibratoDelay, vibratoRate,
                         vibratoDepth);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Vibrato", desc, Type::Vibrato);
      return true;
    }

    case EVENT_VIBRATO_OFF:
      addGenericEvent(beginOffset, curOffset - beginOffset, "Vibrato Off", desc, Type::Vibrato);
      return true;

    case EVENT_MASTER_VOLUME: {
      const uint8_t newVol = readByte(curOffset++);
      addMasterVol(beginOffset, curOffset - beginOffset, newVol / 2);
      return true;
    }

    case EVENT_MASTER_VOLUME_FADE: {
      const uint8_t fadeLength = readByte(curOffset++);
      const uint8_t newVol = readByte(curOffset++);
      desc = fmt::format("Length: {:d}  Volume: {:d}", fadeLength, newVol);
      addMastVolSlide(beginOffset, curOffset - beginOffset, fadeLength, newVol / 2);
      return true;
    }

    case EVENT_TEMPO: {
      const uint8_t newTempo = readByte(curOffset++);
      parentSeq.setImmediateTempo(newTempo);
      addTempoBPM(beginOffset, curOffset - beginOffset, parentSeq.getTempoInBPM(newTempo));
      return true;
    }

    case EVENT_TEMPO_FADE: {
      const uint8_t fadeLength = readByte(curOffset++);
      const uint8_t newTempo = readByte(curOffset++);
      addTempoSlide(beginOffset, curOffset - beginOffset, fadeLength,
                    static_cast<int>(60000000 / parentSeq.getTempoInBPM(newTempo)));
      return true;
    }

    case EVENT_GLOBAL_TRANSPOSE: {
      const int8_t semitones = readByte(curOffset++);
      parentSeq.globalTranspose = semitones;
      addGlobalTranspose(beginOffset, curOffset - beginOffset, semitones);
      return true;
    }

    case EVENT_TRANSPOSE: {
      const int8_t semitones = readByte(curOffset++);
      state.spcTranspose = semitones;
      addTranspose(beginOffset, curOffset - beginOffset, semitones);
      return true;
    }

    case EVENT_TREMOLO_ON: {
      const uint8_t tremoloDelay = readByte(curOffset++);
      const uint8_t tremoloRate = readByte(curOffset++);
      const uint8_t tremoloDepth = readByte(curOffset++);
      desc = fmt::format("Delay: {:d}  Rate: {:d}  Depth: {:d}", tremoloDelay, tremoloRate,
                         tremoloDepth);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Tremolo", desc, Type::Tremelo);
      return true;
    }

    case EVENT_TREMOLO_OFF:
      addGenericEvent(beginOffset, curOffset - beginOffset, "Tremolo Off", desc, Type::Tremelo);
      return true;

    case EVENT_VOLUME: {
      const uint8_t newVol = readByte(curOffset++);
      addVol(beginOffset, curOffset - beginOffset, newVol / 2);
      return true;
    }

    case EVENT_VOLUME_FADE: {
      const uint8_t fadeLength = readByte(curOffset++);
      const uint8_t newVol = readByte(curOffset++);
      addVolSlide(beginOffset, curOffset - beginOffset, fadeLength, newVol / 2);
      return true;
    }

    case EVENT_VIBRATO_FADE: {
      const uint8_t fadeLength = readByte(curOffset++);
      desc = fmt::format("Length: {:d}", fadeLength);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Vibrato Fade", desc, Type::Vibrato);
      return true;
    }

    case EVENT_PITCH_ENVELOPE_TO: {
      const uint8_t pitchEnvDelay = readByte(curOffset++);
      const uint8_t pitchEnvLength = readByte(curOffset++);
      const int8_t pitchEnvSemitones = static_cast<int8_t>(readByte(curOffset++));
      desc = fmt::format("Delay: {:d}  Length: {:d}  Semitones: {:d}", pitchEnvDelay,
                         pitchEnvLength, pitchEnvSemitones);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Pitch Envelope (To)", desc,
                      Type::PitchEnvelope);
      return true;
    }

    case EVENT_PITCH_ENVELOPE_FROM: {
      const uint8_t pitchEnvDelay = readByte(curOffset++);
      const uint8_t pitchEnvLength = readByte(curOffset++);
      const int8_t pitchEnvSemitones = static_cast<int8_t>(readByte(curOffset++));
      desc = fmt::format("Delay: {:d}  Length: {:d}  Semitones: {:d}", pitchEnvDelay,
                         pitchEnvLength, pitchEnvSemitones);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Pitch Envelope (From)", desc,
                      Type::PitchEnvelope);
      return true;
    }

    case EVENT_PITCH_ENVELOPE_OFF:
      addGenericEvent(beginOffset, curOffset - beginOffset, "Pitch Envelope Off", desc,
                      Type::PitchEnvelope);
      return true;

    case EVENT_TUNING: {
      const uint8_t newTuning = readByte(curOffset++);
      addFineTuning(beginOffset, curOffset - beginOffset, (newTuning / 256.0) * 100.0);
      return true;
    }

    case EVENT_ECHO_ON: {
      const uint8_t spcEON = readByte(curOffset++);
      const uint8_t spcEVOL_L = readByte(curOffset++);
      const uint8_t spcEVOL_R = readByte(curOffset++);

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
      const uint8_t spcEDL = readByte(curOffset++);
      const uint8_t spcEFB = readByte(curOffset++);
      const uint8_t spcFIR = readByte(curOffset++);
      desc = fmt::format("Delay: {:d}  Feedback: {:d}  FIR: {:d}", spcEDL, spcEFB, spcFIR);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Echo Param", desc, Type::Reverb);
      return true;
    }

    case EVENT_ECHO_VOLUME_FADE: {
      const uint8_t fadeLength = readByte(curOffset++);
      const uint8_t spcEVOL_L = readByte(curOffset++);
      const uint8_t spcEVOL_R = readByte(curOffset++);
      desc = fmt::format("Length: {:d}  Volume Left: {:d}  Volume Right: {:d}", fadeLength,
                         spcEVOL_L, spcEVOL_R);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Echo Volume Fade", desc,
                      Type::Reverb);
      return true;
    }

    case EVENT_PITCH_SLIDE: {
      const uint8_t pitchSlideDelay = readByte(curOffset++);
      const uint8_t pitchSlideLength = readByte(curOffset++);
      const uint8_t pitchSlideTargetNote = readByte(curOffset++);
      desc = fmt::format("Delay: {:d}  Length: {:d}  Note: {:d}", pitchSlideDelay,
                         pitchSlideLength, static_cast<int>(pitchSlideTargetNote - 0x80));
      addGenericEvent(beginOffset, curOffset - beginOffset, "Pitch Slide", desc,
                      Type::PitchBendSlide);
      return true;
    }

    case EVENT_PERCUSSION_PATCH_BASE: {
      const uint8_t percussionBase = readByte(curOffset++);
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

bool NinSnesTrack::handleVariantEvent(NinSnesSeqEventType eventType, uint32_t beginOffset,
                                      uint8_t statusByte, std::string& desc) {
  auto& parentSeq = seq();

  switch (eventType) {
    case EVENT_RD2_PROGCHANGE_AND_ADSR: {
      const uint8_t instrumentByte = readByte(curOffset++);
      curOffset += 2;
      uint8_t logicalProgNum = 0;
      const uint32_t newProgNum = parentSeq.resolveProgramNumber(instrumentByte, &logicalProgNum);
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
      const uint8_t times = readByte(curOffset++);
      const int8_t volumeDelta = readByte(curOffset++);
      const int8_t pitchDelta = readByte(curOffset++);
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
      const uint8_t adsr1 = readByte(curOffset++);
      const uint8_t adsr2 = readByte(curOffset++);
      const uint8_t gain = readByte(curOffset++);
      desc = fmt::format("ADSR(1): ${:02X}  ADSR(2): ${:02X}  GAIN: ${:02X}", adsr1, adsr2, gain);
      addGenericEvent(beginOffset, curOffset - beginOffset, "ADSR/GAIN", desc, Type::Adsr);
      return true;
    }

    case EVENT_LEMMINGS_NOTE_PARAM:
      readLemmingsNoteParam(beginOffset, statusByte, desc);
      return true;

    case EVENT_QUINTET_TUNING: {
      const uint8_t newTuning = readByte(curOffset++);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Fine Tuning", desc, Type::FineTune);
      addFineTuningNoItem((newTuning / 256.0) * 61.8);
      return true;
    }

    case EVENT_QUINTET_ADSR: {
      const uint8_t adsr1 = readByte(curOffset++);
      const uint8_t sustainRate = readByte(curOffset++);
      const uint8_t sustainLevel = readByte(curOffset++);
      const uint8_t adsr2 = (sustainLevel << 5) | sustainRate;
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

void NinSnesTrack::addPendingEndEvent(uint8_t statusByte, const std::string& desc) {
  const auto& parentSeq = seq();
  if (curOffset + 1 <= 0x10000 && statusByte != parentSeq.STATUS_END &&
      readByte(curOffset) == parentSeq.STATUS_END) {
    addGenericEvent(curOffset, 1, (state.loopCount == 0) ? "Section End" : "Pattern End", desc,
                    (state.loopCount == 0) ? Type::TrackEnd : Type::RepeatEnd);
  }
}

bool NinSnesTrack::readEvent() {
  const auto& parentSeq = seq();
  const uint32_t beginOffset = curOffset;
  if (curOffset >= 0x10000) {
    return false;
  }

  const uint8_t statusByte = readByte(curOffset++);
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

uint16_t NinSnesTrack::convertToApuAddress(uint16_t offset) {
  return seq().convertToAPUAddress(offset);
}

uint16_t NinSnesTrack::getShortAddress(uint32_t offset) {
  return seq().getShortAddress(offset);
}

void NinSnesTrack::getVolumeBalance(uint16_t pan, double& volumeLeft, double& volumeRight) {
  const auto& parentSeq = seq();
  getNinSnesVolumeBalance(parentSeq.profile(), parentSeq.panTable, pan, volumeLeft, volumeRight);
}

uint8_t NinSnesTrack::readPanTable(uint16_t pan) {
  const auto& parentSeq = seq();
  return readNinSnesPanTable(parentSeq.profile(), parentSeq.panTable, pan);
}

int8_t NinSnesTrack::calculatePanValue(uint8_t pan, double& volumeScale, bool& reverseLeft,
                                       bool& reverseRight) {
  const auto& parentSeq = seq();
  const auto panState = decodeNinSnesPanValue(parentSeq.profile(), pan);
  reverseLeft = panState.reverseLeft;
  reverseRight = panState.reverseRight;

  double volumeLeft;
  double volumeRight;
  getVolumeBalance(panState.panIndex << 8, volumeLeft, volumeRight);

  // TODO: correct volume scale of TOSE sequence
  int8_t midiPan = convertVolumeBalanceToStdMidiPan(volumeLeft, volumeRight, &volumeScale);
  volumeScale = std::min(volumeScale, 1.0);  // workaround

  return midiPan;
}
