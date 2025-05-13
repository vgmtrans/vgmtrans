/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "AsciiShuichiSnesSeq.h"
#include "ScaleConversion.h"
#include "SeqEvent.h"

using namespace std;

DECLARE_FORMAT(AsciiShuichiSnes);

static constexpr int MAX_TRACKS = 8;
static constexpr uint16_t SEQ_PPQN = 48;
static constexpr int SEQ_KEY_OFFSET = 36;

static constexpr uint8_t panTable[] = {
  0x80, 0x80, 0x7f, 0x7f, 0x7d, 0x7c, 0x7a, 0x78,
  0x76, 0x73, 0x70, 0x6d, 0x69, 0x65, 0x61, 0x5d,
  0x58, 0x54, 0x4f, 0x49, 0x44, 0x3e, 0x39, 0x33,
  0x2d, 0x27, 0x21, 0x1a, 0x13, 0x0a, 0x00
};

//  ***************
//  AsciiShuichiSnesSeq
//  ***************

AsciiShuichiSnesSeq::AsciiShuichiSnesSeq(RawFile *file, uint32_t seqHeaderOffset, string name)
    : VGMSeq(AsciiShuichiSnesFormat::name, file, seqHeaderOffset, 0, move(name)) {
  bLoadTickByTick = true;
  setAllowDiscontinuousTrackData(true);
  setUseLinearAmplitudeScale(true);

  useReverb();
  setAlwaysWriteInitialReverb(0);

  loadEventMap();
}

void AsciiShuichiSnesSeq::resetVars() {
  VGMSeq::resetVars();

  tempo = 0x14;
}

bool AsciiShuichiSnesSeq::parseHeader() {
  setPPQN(SEQ_PPQN);

  VGMHeader *header = addHeader(dwOffset, 2 * MAX_TRACKS);
  if (dwOffset + header->unLength > 0x10000) {
    return false;
  }

  for (uint8_t trackIndex = 0; trackIndex < MAX_TRACKS; trackIndex++) {
    string trackNameLow = fmt::format("Track Pointer {:d} (Low)", trackIndex + 1);
    string trackNameHigh = fmt::format("Track Pointer {:d} (High)", trackIndex + 1);

    header->addChild(dwOffset + trackIndex, 1, trackNameLow);
    header->addChild(dwOffset + MAX_TRACKS + trackIndex, 1, trackNameHigh);
  }

  return true;
}


bool AsciiShuichiSnesSeq::parseTrackPointers() {
  for (uint8_t trackIndex = 0; trackIndex < MAX_TRACKS; trackIndex++) {
    const uint8_t lo = readByte(dwOffset + trackIndex);
    const uint8_t hi = readByte(dwOffset + MAX_TRACKS + trackIndex);
    const auto trackStartAddress = static_cast<uint16_t>(lo | (hi << 8));

    const auto track = new AsciiShuichiSnesTrack(this, trackStartAddress);
    aTracks.push_back(track);
  }

  return true;
}

void AsciiShuichiSnesSeq::loadEventMap() {
  for (int statusByte = 0xac; statusByte <= 0xff; statusByte++) {
    EventMap[static_cast<uint8_t>(statusByte)] = EVENT_NOTE;
  }

  EventMap[0x80] = EVENT_END;
  EventMap[0x81] = EVENT_INFINITE_LOOP_START;
  EventMap[0x82] = EVENT_INFINITE_LOOP_END;
  EventMap[0x83] = EVENT_LOOP_START;
  EventMap[0x84] = EVENT_LOOP_END;
  EventMap[0x85] = EVENT_LOOP_BREAK;
  EventMap[0x86] = EVENT_CALL;
  EventMap[0x87] = EVENT_RET;
  EventMap[0x88] = EVENT_UNKNOWN0;
  EventMap[0x89] = EVENT_PROGCHANGE;
  EventMap[0x8a] = EVENT_RELEASE_ADSR;
  EventMap[0x8b] = EVENT_TEMPO;
  EventMap[0x8c] = EVENT_TRANSPOSE_ABS;
  EventMap[0x8d] = EVENT_TRANSPOSE_REL;
  EventMap[0x8e] = EVENT_TUNING;
  EventMap[0x8f] = EVENT_PITCH_BEND_SLIDE;
  EventMap[0x90] = EVENT_DURATION_RATE;
  //EventMap[0x91] = (invalid?);
  EventMap[0x92] = EVENT_VOLUME_AND_PAN;
  EventMap[0x93] = EVENT_VOLUME;
  EventMap[0x94] = EVENT_VOLUME_REL;
  EventMap[0x95] = EVENT_VOLUME_REL_TEMP;
  EventMap[0x96] = EVENT_PAN;
  //EventMap[0x97] = (undefined);
  EventMap[0x98] = EVENT_PAN_FADE;
  EventMap[0x99] = EVENT_VOLUME_FADE;
  EventMap[0x9a] = EVENT_MASTER_VOLUME;
  EventMap[0x9b] = EVENT_ECHO_PARAM;
  EventMap[0x9c] = EVENT_ECHO_CHANNELS;
  EventMap[0x9d] = EVENT_VIBRATO;
  EventMap[0x9e] = EVENT_VIBRATO_OFF;
  EventMap[0x9f] = EVENT_REST;
  EventMap[0xa0] = EVENT_NOISE_ON;
  EventMap[0xa1] = EVENT_NOISE_OFF;
  //EventMap[0xa2] = (undefined);
  EventMap[0xa3] = EVENT_MUTE_CHANNELS;
  EventMap[0xa4] = EVENT_INSTRUMENT_VOLUME_PAN_TRANSPOSE;
  EventMap[0xa5] = EVENT_DURATION_TICKS;
  EventMap[0xa6] = EVENT_WRITE_TO_PORT;
  //EventMap[0xa7] = (undefined);
  //EventMap[0xa8] = (undefined);
  //EventMap[0xa9] = (undefined);
  //EventMap[0xaa] = (undefined);
  EventMap[0xab] = EVENT_UNKNOWN0;
}

double AsciiShuichiSnesSeq::getTempoInBPM() const {
  return getTempoInBPM(tempo);
}

double AsciiShuichiSnesSeq::getTempoInBPM(uint8_t tempo) {
  if (tempo == 0) {
    tempo = 1;
  }

  return 60000000.0 / (SEQ_PPQN * (125 * 4) * tempo);
}



//  *****************
//  AsciiShuichiSnesTrack
//  *****************

AsciiShuichiSnesTrack::AsciiShuichiSnesTrack(AsciiShuichiSnesSeq *parentFile, uint32_t offset, uint32_t length)
    : SeqTrack(parentFile, offset, length) {
  AsciiShuichiSnesTrack::resetVars();
  bDetermineTrackLengthEventByEvent = true;
  bWriteGenericEventAsTextEvent = false;
}

void AsciiShuichiSnesTrack::resetVars() {
  SeqTrack::resetVars();

  cKeyCorrection = SEQ_KEY_OFFSET;

  spcVolume = 0;
  infiniteLoopPoint = 0;
  lastNoteKey = -1;
  rawNoteLength = 0;
  noteDuration = 0;
  noteDurationRate = 0;
  slurDeferred = false;
  useNoteDurationRate = true;
  reachedRepeatBreakBefore = false;
  repeatStartNestLevel = 0;
  repeatEndNestLevel = 0;
  callNestLevel = 0;
  ranges::fill(repeatStartAddressStack, 0);
  ranges::fill(repeatEndAddressStack, 0);
  ranges::fill(repeatCountStack, 0);
  ranges::fill(callStack, 0);
}

bool AsciiShuichiSnesTrack::readEvent() {
  const auto parentSeq = dynamic_cast<AsciiShuichiSnesSeq*>(this->parentSeq);

  const uint32_t beginOffset = curOffset;
  if (curOffset >= 0x10000) {
    return false;
  }

  const uint8_t statusByte = readByte(curOffset++);
  bool bContinue = true;

  auto eventType = static_cast<AsciiShuichiSnesSeqEventType>(0);
  const auto pEventType = parentSeq->EventMap.find(statusByte);
  if (pEventType != parentSeq->EventMap.end()) {
    eventType = pEventType->second;
  }

  switch (eventType) {
    case EVENT_UNKNOWN0: {
      const auto desc = describeUnknownEvent(statusByte);
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
      break;
    }

    case EVENT_UNKNOWN1: {
      const int arg1 = readByte(curOffset++);
      const auto desc = describeUnknownEvent(statusByte, arg1);
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
      break;
    }

    case EVENT_UNKNOWN2: {
      const int arg1 = readByte(curOffset++);
      const int arg2 = readByte(curOffset++);
      const auto desc = describeUnknownEvent(statusByte, arg1, arg2);
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
      break;
    }

    case EVENT_UNKNOWN3: {
      const int arg1 = readByte(curOffset++);
      const int arg2 = readByte(curOffset++);
      const int arg3 = readByte(curOffset++);
      const auto desc = describeUnknownEvent(statusByte, arg1, arg2, arg3);
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
      break;
    }

    case EVENT_UNKNOWN4: {
      const int arg1 = readByte(curOffset++);
      const int arg2 = readByte(curOffset++);
      const int arg3 = readByte(curOffset++);
      const int arg4 = readByte(curOffset++);
      const auto desc = describeUnknownEvent(statusByte, arg1, arg2, arg3, arg4);
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
      break;
    }

    case EVENT_END: {
      addEndOfTrack(beginOffset, curOffset - beginOffset);
      bContinue = false;
      break;
    }

    case EVENT_NOTE: {
      const auto key = static_cast<int8_t>(statusByte - 0xac);
      const bool isSlurEvent = (statusByte == 0xff);  // slur/tie

      const uint8_t maybeLength = readByte(curOffset);
      if (maybeLength < 0x80) {
        rawNoteLength = maybeLength;
        if (useNoteDurationRate) {
          noteDuration = static_cast<uint8_t>(
              rawNoteLength * (noteDurationRate == 0 ? 256 : noteDurationRate) / 256);
        }
        curOffset++;
      }

      if (isSlurEvent) {
        // i.e. combine with next note - omit key-off and just change pitch
        addGenericEvent(beginOffset, curOffset - beginOffset, "Tie / Slur", "", Type::Tie, ICON_NOTE);
        slurDeferred = true;
      } else {
        // note event
        const auto actualDur = min(static_cast<int>(noteDuration), max(1, rawNoteLength - 3));

        if (slurDeferred && key == lastNoteKey) {
          // tie
          makePrevDurNoteEnd(getTime() + actualDur);

          // add event without midi event
          if (readMode == READMODE_ADD_TO_UI && !isItemAtOffset(beginOffset, true))
            addEvent(new DurNoteSeqEvent(this, key + cKeyCorrection, 100, noteDuration, beginOffset,
                                         curOffset - beginOffset, "Note with Duration"));
        } else {
          if (slurDeferred) {
            makePrevDurNoteEnd(getTime());
          }
          addNoteByDur(beginOffset, curOffset - beginOffset, key, 100, actualDur);
        }

        slurDeferred = false;
        lastNoteKey = key;

        addTime(rawNoteLength);
      }
      break;
    }

    case EVENT_INFINITE_LOOP_START: {
      infiniteLoopPoint = static_cast<uint16_t>(curOffset);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Infinite Loop Point",
                      "", Type::Loop, ICON_STARTREP);
      break;
    }

    case EVENT_INFINITE_LOOP_END: {
      bContinue = addLoopForever(beginOffset, curOffset - beginOffset);
      curOffset = infiniteLoopPoint;
      break;
    }

    case EVENT_LOOP_START:
      addGenericEvent(beginOffset, curOffset - beginOffset, "Loop Start", "", Type::Loop,
                      ICON_STARTREP);
      repeatStartAddressStack[repeatStartNestLevel] = static_cast<uint16_t>(curOffset);
      repeatStartNestLevel++;
      break;

    case EVENT_LOOP_END: {
      const uint8_t count = readByte(curOffset++);
      const int realLoopCount = (count == 0) ? 256 : count;

      const auto desc = fmt::format("Loop count: {:d}", realLoopCount);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Loop End", desc, Type::Loop,
                      ICON_ENDREP);

      bool firstTime =
          repeatEndNestLevel == 0 || curOffset != repeatEndAddressStack[repeatEndNestLevel - 1];

      bool shouldRepeatAgain = false;
      if (firstTime) {
        // first time
        if (count == 1) {
          // repeat once
          repeatStartNestLevel--;
        } else {
          // repeat N times
          repeatCountStack[repeatEndNestLevel] = count - 1;
          repeatEndAddressStack[repeatEndNestLevel] = static_cast<uint16_t>(curOffset);
          repeatEndNestLevel++;
          shouldRepeatAgain = true;
        }
      } else {
        // after the first time
        const auto currentRepeatCount = repeatCountStack[repeatEndNestLevel - 1];
        if (currentRepeatCount == 1) {
          // repeat end
          repeatStartNestLevel--;
          repeatEndNestLevel--;
        } else {
          // repeat again
          repeatCountStack[repeatEndNestLevel - 1] = currentRepeatCount - 1;
          shouldRepeatAgain = true;
        }
      }

      if (shouldRepeatAgain) {
        if (repeatStartNestLevel == 0) {
          L_WARN("Stack overflow in Loop End Event.");
          bContinue = false;
        } else {
          curOffset = repeatStartAddressStack[repeatStartNestLevel - 1];
        }
      }
      break;
    }

    case EVENT_LOOP_BREAK: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "Loop Break", "", Type::Loop,
                      ICON_ENDREP);

      if (!reachedRepeatBreakBefore) {
        // first time, always skip
        reachedRepeatBreakBefore = true;
      } else {
        if (repeatEndNestLevel == 0) {
          L_WARN("Stack overflow in Loop Break Event.");
          bContinue = false;
        } else {
          // end the repeat if last time
          if (repeatCountStack[repeatEndNestLevel - 1] == 1) {
            repeatEndNestLevel--;
            repeatStartNestLevel--;
            reachedRepeatBreakBefore = false;
            curOffset = repeatEndAddressStack[repeatEndNestLevel];
          }
        }
      }

      break;
    }

    case EVENT_CALL: {
      uint16_t dest = readShort(curOffset);
      curOffset += 2;

      const auto desc = fmt::format("Destination: {:#04x}", dest);
      addGenericEvent(beginOffset, 3, "Pattern Play", desc, Type::Loop, ICON_STARTREP);

      if (callNestLevel >= callStack.size()) {
        L_WARN("Stack overflow in Pattern Play Event.");
        bContinue = false;
        break;
      }

      // save loop start address and repeat count
      callStack[callNestLevel++] = static_cast<uint16_t>(curOffset);

      // jump to subroutine address
      curOffset = dest;

      break;
    }

    case EVENT_RET: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "End Pattern", "", Type::Loop,
                      ICON_ENDREP);

      if (callNestLevel == 0) {
        L_WARN("Stack overflow in Pattern End Event.");
        bContinue = false;
        break;
      }

      curOffset = callStack[--callNestLevel];
      break;
    }

    case EVENT_PROGCHANGE: {
      const uint8_t newProgramNumber = readByte(curOffset++);
      addProgramChange(beginOffset, curOffset - beginOffset, newProgramNumber, true);
      break;
    }

    case EVENT_RELEASE_ADSR: {
      const uint8_t adsr2 = readByte(curOffset++);
      const auto desc = fmt::format("ADSR(2): {:#02x}", adsr2);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Release ADSR", desc, Type::Adsr,
                      ICON_CONTROL);
      break;
    }

    case EVENT_TEMPO: {
      const uint8_t newTempo = readByte(curOffset++);
      parentSeq->tempo = newTempo;
      addTempoBPM(beginOffset, curOffset - beginOffset, parentSeq->getTempoInBPM());
      break;
    }

    case EVENT_TRANSPOSE_ABS: {
      const auto newTranspose = static_cast<int8_t>(readByte(curOffset++));
      addTranspose(beginOffset, curOffset - beginOffset, newTranspose);

      // use coarse tuning instead
      transpose = 0;
      if (readMode == READMODE_CONVERT_TO_MIDI) {
        pMidiTrack->addCoarseTuning(channel, newTranspose);
      }
      break;
    }

    case EVENT_TRANSPOSE_REL: {
      const auto transposeDelta = static_cast<int8_t>(readByte(curOffset++));
      const auto newTranspose = static_cast<int8_t>(transpose + transposeDelta);
      addTranspose(beginOffset, curOffset - beginOffset, transposeDelta, "Transpose (Relative)");

      // use coarse tuning instead
      transpose = 0;
      if (readMode == READMODE_CONVERT_TO_MIDI) {
        pMidiTrack->addCoarseTuning(channel, newTranspose);
      }
      break;
    }

    case EVENT_TUNING: {
      const auto newTuning = static_cast<int8_t>(readByte(curOffset++));
      const double cents = pitchScaleToCents(1.0 + (newTuning / 4096.0));
      addFineTuning(beginOffset, curOffset - beginOffset, cents);
      break;
    }

    case EVENT_PITCH_BEND_SLIDE: {
      const uint8_t arg1 = readByte(curOffset++);
      const uint8_t arg2 = readByte(curOffset++);

      const auto desc = logEvent(statusByte, spdlog::level::off, "Event", static_cast<int>(arg1),
                      static_cast<int>(arg2));
      addGenericEvent(beginOffset, curOffset - beginOffset, "Pitch Bend Slide", desc, Type::PitchBend,
                      ICON_CONTROL);
      // TODO: pitch bend slide
      break;
    }

    case EVENT_DURATION_RATE: {
      const uint8_t rate = readByte(curOffset++);
      useNoteDurationRate = true;
      noteDurationRate = rate;

      noteDuration = static_cast<uint8_t>(rawNoteLength *
                                          (noteDurationRate == 0 ? 256 : noteDurationRate) / 256);

      const int actualRate = (rate == 0) ? 256 : rate;
      const auto desc =
          fmt::format("Note Length: {:d}/256 ({:.1f}%)", actualRate, actualRate / 256.0);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Duration Rate", desc, Type::DurationNote,
                      ICON_CONTROL);
      break;
    }

    case EVENT_DURATION_TICKS: {
      const uint8_t ticks = readByte(curOffset++);
      useNoteDurationRate = false;
      noteDuration = ticks;

      const auto desc = fmt::format("Note Length: {:d} ticks", ticks);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Duration", desc, Type::DurationNote,
                      ICON_CONTROL);
      break;
    }

    case EVENT_VOLUME_AND_PAN: {
      const uint8_t volume = readByte(curOffset++);
      const uint8_t pan = readByte(curOffset++);
      spcVolume = volume;

      const auto desc = fmt::format("Volume: {:d}, pan: {:d}/{:d}", volume, pan, countof(panTable));
      addGenericEvent(beginOffset, curOffset - beginOffset, "Volume & Pan", desc, Type::Volume,
                      ICON_CONTROL);

      addVolNoItem(spcVolume / 2);

      const int8_t midiPan = calcMidiPanValue(pan);
      addPanNoItem(midiPan); // TODO: apply volume scale
      break;
    }

    case EVENT_VOLUME: {
      const uint8_t volume = readByte(curOffset++);
      spcVolume = volume;

      const auto desc = fmt::format("Volume: {:d}", volume);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Volume", desc, Type::Volume,
                      ICON_CONTROL);
      addVolNoItem(spcVolume / 2);
      break;
    }

    case EVENT_VOLUME_REL: {
      const auto volumeDelta = static_cast<int8_t>(readByte(curOffset++));
      spcVolume += volumeDelta;

      const auto desc = fmt::format("Volume delta: {:d}", volumeDelta);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Volume (Relative)", desc, Type::Volume,
                      ICON_CONTROL);
      addVolNoItem(spcVolume / 2);
      break;
    }

    case EVENT_VOLUME_REL_TEMP: {
      const auto volumeDelta = static_cast<int8_t>(readByte(curOffset++));

      const auto desc = fmt::format("Volume delta: {:d}", volumeDelta);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Volume (One-Shot)", desc, Type::Volume,
                      ICON_CONTROL);
      // TODO: volume MIDI CC
      break;
    }

    case EVENT_PAN: {
      const uint8_t pan = readByte(curOffset++);

      const auto desc = fmt::format("Pan: {:d}/{:d}", pan, countof(panTable));
      addGenericEvent(beginOffset, curOffset - beginOffset, "Pan", desc, Type::Pan, ICON_CONTROL);

      const int8_t midiPan = calcMidiPanValue(pan);
      addPanNoItem(midiPan);  // TODO: apply volume scale
      break;
    }

    case EVENT_PAN_FADE: {
      const uint8_t arg1 = readByte(curOffset++);
      const uint8_t arg2 = readByte(curOffset++);
      const uint8_t arg3 = readByte(curOffset++);

      const auto desc = logEvent(statusByte, spdlog::level::off, "Event", static_cast<int>(arg1),
                      static_cast<int>(arg2), static_cast<int>(arg3));
      addGenericEvent(beginOffset, curOffset - beginOffset, "Pan Fade", desc, Type::Pan, ICON_CONTROL);
      break;
    }

    case EVENT_VOLUME_FADE: {
      const uint8_t delay = readByte(curOffset++);
      const uint8_t rate = readByte(curOffset++);
      const uint8_t depth = readByte(curOffset++);
      const uint8_t stepLength = readByte(curOffset++);

      const auto desc =
          fmt::format("Volume Fade - delay: {:d}, rate: {:d}, depth: {:d}, step length: {:d}",
                      delay, rate, depth, stepLength);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Volume Fade", desc, Type::Volume,
                      ICON_CONTROL);
      // TODO: volume MIDI CC
      break;
    }

    case EVENT_MASTER_VOLUME: {
      const auto newVolL = static_cast<int8_t>(readByte(curOffset++));
      const auto newVolR = static_cast<int8_t>(readByte(curOffset++));


      const auto desc = fmt::format("Master Volume - left: {:d}, right: {:d}", newVolL, newVolR);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Master Volume L/R", desc, Type::Volume,
                      ICON_CONTROL);
      break;
    }

    case EVENT_ECHO_PARAM: {
      const uint8_t delay = readByte(curOffset++);
      const uint8_t feedback = readByte(curOffset++);
      const uint8_t arg3 = readByte(curOffset++);
      
      const auto desc = fmt::format("Echo delay: {:d}, feedback: {:d}, arg3: {:d}", delay, feedback, arg3);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Echo Delay & Feedback", desc,
                      Type::Reverb, ICON_CONTROL);
      break;
    }

    case EVENT_ECHO_CHANNELS: {
      const uint8_t dspEchoOn = readByte(curOffset++);

      const auto desc = fmt::format("Echo channels: {:#02x}", dspEchoOn);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Echo Channels", desc, Type::Reverb,
                      ICON_CONTROL);

      // TODO: output MIDI reverb
      break;
    }

    case EVENT_VIBRATO: {
      const uint8_t delay = readByte(curOffset++);
      const uint8_t rate = readByte(curOffset++);
      const uint8_t depth = readByte(curOffset++);
      const uint8_t stepLength = readByte(curOffset++);

      const auto desc =
          fmt::format("Vibrato delay: {:d}, rate: {:d}, depth: {:d}, step length: {:d}", delay,
                      rate, depth, stepLength);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Vibrato", desc, Type::Modulation,
                      ICON_CONTROL);
      break;
    }

    case EVENT_VIBRATO_OFF: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "Vibrato Off", "", Type::Modulation,
                      ICON_CONTROL);
      break;
    }

    case EVENT_REST: {
      const uint8_t maybeLength = readByte(curOffset);
      if (maybeLength < 0x80) {
        rawNoteLength = maybeLength;
        if (useNoteDurationRate) {
          noteDuration = static_cast<uint8_t>(
              rawNoteLength * (noteDurationRate == 0 ? 256 : noteDurationRate) / 256);
        }
        curOffset++;
      }
      addRest(beginOffset, curOffset - beginOffset, rawNoteLength);
      break;
    }

    case EVENT_NOISE_ON:
      lastNoteKey = -1;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Noise On", "", Type::ProgramChange,
                      ICON_CONTROL);
      break;

    case EVENT_NOISE_OFF:
      lastNoteKey = -1;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Noise Off", "", Type::ProgramChange,
                      ICON_CONTROL);
      break;

    case EVENT_MUTE_CHANNELS: {
      const uint8_t channels = readByte(curOffset++);

      const auto desc = fmt::format("Mute channels: {:#02x}", channels);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Mute Channels", desc, Type::Reverb,
                      ICON_CONTROL);
      break;
    }

    case EVENT_INSTRUMENT_VOLUME_PAN_TRANSPOSE: {
      const uint8_t newProgramNumber = readByte(curOffset++);
      const uint8_t volume = readByte(curOffset++);
      const uint8_t pan = readByte(curOffset++);
      const auto newTranspose = static_cast<int8_t>(readByte(curOffset++));

      const auto desc =
          fmt::format("Instrument: {:d}, volume: {:d}, pan: {:d}/{:d}, transpose: {:d}",
                      newProgramNumber, volume, pan, countof(panTable), newTranspose);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Instrument, Volume, Pan, Transpose",
                      desc, Type::ProgramChange, ICON_CONTROL);

      addProgramChangeNoItem(newProgramNumber, true);

      spcVolume = volume;
      addVolNoItem(spcVolume / 2);

      const int8_t midiPan = calcMidiPanValue(pan);
      addPanNoItem(midiPan);  // TODO: apply volume scale

      if (readMode == READMODE_CONVERT_TO_MIDI) {
        pMidiTrack->addCoarseTuning(channel, newTranspose);
      }
      break;
    }

    case EVENT_WRITE_TO_PORT: {
      const uint8_t value = readByte(curOffset++);
      const auto desc = fmt::format("APUI02: {:d} ({:#02x})", value, value);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Write to I/O Port", desc, Type::Misc,
                      ICON_CONTROL);
      break;
    }

    default: {
      const auto desc = logEvent(statusByte);
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
      bContinue = false;
      break;
    }
  }

  return bContinue;
}

void AsciiShuichiSnesTrack::getVolumeBalance(uint8_t pan, double &volumeLeft, double &volumeRight) {
  if (pan < countof(panTable)) {
    const uint8_t vl = panTable[pan];
    const uint8_t vr = panTable[30 - pan];
    volumeLeft = vl / 256.0;
    volumeRight = vr / 256.0;
  } else {
    L_WARN("Panpot is out of range. Value must be in the range 0 to 30.");
    volumeLeft = 1.0;
    volumeRight = 1.0;
  }
}

int8_t AsciiShuichiSnesTrack::calcMidiPanValue(uint8_t pan) {
  double volumeLeft;
  double volumeRight;
  getVolumeBalance(pan, volumeLeft, volumeRight);
  return static_cast<int8_t>(convertVolumeBalanceToStdMidiPan(volumeLeft, volumeRight));
}
