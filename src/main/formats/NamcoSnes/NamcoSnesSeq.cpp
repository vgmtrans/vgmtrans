#include "NamcoSnesSeq.h"
#include "ScaleConversion.h"
#include <spdlog/fmt/fmt.h>

DECLARE_FORMAT(NamcoSnes);

static constexpr int MAX_TRACKS = 8;
static constexpr uint16_t SEQ_PPQN = 48;
static constexpr uint8_t NOTE_VELOCITY = 100;

//  ************
//  NamcoSnesSeq
//  ************

NamcoSnesSeq::NamcoSnesSeq(RawFile *file, NamcoSnesVersion ver, uint32_t seqdataOffset, std::string newName)
    : VGMSeqNoTrks(NamcoSnesFormat::name, file, seqdataOffset, newName),
      version(ver) {
  setAllowDiscontinuousTrackData(true);
  setUseLinearAmplitudeScale(true);

  setAlwaysWriteInitialTempo(60000000.0 / (SEQ_PPQN * (125 * 0x86)));

  useReverb();

  loadEventMap();
}

NamcoSnesSeq::~NamcoSnesSeq() {
}

void NamcoSnesSeq::resetVars() {
  VGMSeqNoTrks::resetVars();

  spcDeltaTime = 1;
  spcDeltaTimeScale = 1;
  subReturnAddress = 0;
  loopCount = 0;
  loopCountAlt = 0;

  for (uint8_t trackIndex = 0; trackIndex < MAX_TRACKS; trackIndex++) {
    prevNoteKey[trackIndex] = -1;
    prevNoteType[trackIndex] = NOTE_MELODY;
    instrNum[trackIndex] = -1;
  }
}

bool NamcoSnesSeq::parseHeader() {
  setPPQN(SEQ_PPQN);
  nNumTracks = MAX_TRACKS;

  setEventsOffset(VGMSeq::dwOffset);
  return true;
}

void NamcoSnesSeq::loadEventMap() {
  int statusByte;

  NOTE_NUMBER_REST = 0x54;
  NOTE_NUMBER_NOISE_MIN = 0x55;
  NOTE_NUMBER_PERCUSSION_MIN = 0x80;

  EventMap[0x00] = EVENT_DELTA_TIME;
  EventMap[0x01] = EVENT_OPEN_TRACKS;
  EventMap[0x02] = EVENT_CALL;
  EventMap[0x03] = EVENT_END;
  EventMap[0x04] = EVENT_DELTA_MULTIPLIER;
  EventMap[0x05] = EVENT_MASTER_VOLUME;
  EventMap[0x06] = EVENT_LOOP_AGAIN;
  EventMap[0x07] = EVENT_LOOP_BREAK;
  EventMap[0x08] = EVENT_GOTO;
  EventMap[0x09] = EVENT_NOTE;
  EventMap[0x0a] = EVENT_ECHO_DELAY;
  EventMap[0x0b] = EVENT_UNKNOWN_0B;
  EventMap[0x0c] = EVENT_UNKNOWN1;
  EventMap[0x0d] = EVENT_ECHO;
  EventMap[0x0e] = EVENT_WAIT;
  EventMap[0x0f] = EVENT_LOOP_AGAIN_ALT;
  EventMap[0x10] = EVENT_LOOP_BREAK_ALT;
  EventMap[0x11] = EVENT_ECHO_FEEDBACK;
  EventMap[0x12] = EVENT_ECHO_FIR;
  EventMap[0x13] = EVENT_ECHO_VOLUME;
  EventMap[0x14] = EVENT_ECHO_ADDRESS;
  //EventMap[0x15] = (NamcoSnesSeqEventType)0;
  //EventMap[0x16] = (NamcoSnesSeqEventType)0;
  //EventMap[0x17] = (NamcoSnesSeqEventType)0;

  // note: vcmd 2b-ff are not used
  for (statusByte = 0x18; statusByte <= 0xff; statusByte++) {
    EventMap[statusByte] = EVENT_CONTROL_CHANGES;
  }

  ControlChangeMap[0x00] = CONTROL_PROGCHANGE;
  ControlChangeMap[0x01] = CONTROL_VOLUME;
  ControlChangeMap[0x02] = CONTROL_PAN;
  //ControlChangeMap[0x03] = (NamcoSnesSeqControlType)0;
  //ControlChangeMap[0x04] = (NamcoSnesSeqControlType)0;
  //ControlChangeMap[0x05] = (NamcoSnesSeqControlType)0;
  //ControlChangeMap[0x06] = (NamcoSnesSeqControlType)0;
  //ControlChangeMap[0x07] = (NamcoSnesSeqControlType)0;
  //ControlChangeMap[0x08] = (NamcoSnesSeqControlType)0;
  //ControlChangeMap[0x09] = (NamcoSnesSeqControlType)0;
  ControlChangeMap[0x0a] = CONTROL_ADSR;
  //ControlChangeMap[0x0b] = (NamcoSnesSeqControlType)0;
  //ControlChangeMap[0x0c] = (NamcoSnesSeqControlType)0;
  //ControlChangeMap[0x0d] = (NamcoSnesSeqControlType)0;
  //ControlChangeMap[0x0e] = (NamcoSnesSeqControlType)0;
  //ControlChangeMap[0x0f] = (NamcoSnesSeqControlType)0;

  ControlChangeNames[CONTROL_PROGCHANGE] = "Program Change";
  ControlChangeNames[CONTROL_VOLUME] = "Volume";
  ControlChangeNames[CONTROL_PAN] = "Pan";
  ControlChangeNames[CONTROL_ADSR] = "ADSR";
}

bool NamcoSnesSeq::readEvent() {
  uint32_t beginOffset = curOffset;
  if (curOffset >= 0x10000) {
    return false;
  }

  uint8_t statusByte = readByte(curOffset++);
  bool bContinue = true;

  std::string desc;

  NamcoSnesSeqEventType eventType = (NamcoSnesSeqEventType) 0;
  std::map<uint8_t, NamcoSnesSeqEventType>::iterator pEventType = EventMap.find(statusByte);
  if (pEventType != EventMap.end()) {
    eventType = pEventType->second;
  }

  // default first track
  channel = 0;
  setCurTrack(channel);

  switch (eventType) {
    case EVENT_UNKNOWN0:
      desc = fmt::format("Event: 0x{:02X}", statusByte);
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
      break;

    case EVENT_UNKNOWN1: {
      uint8_t arg1 = readByte(curOffset++);
      desc = fmt::format("Event: 0x{:02X}  Arg1: {:d}", statusByte, arg1);
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
      break;
    }

    case EVENT_UNKNOWN2: {
      uint8_t arg1 = readByte(curOffset++);
      uint8_t arg2 = readByte(curOffset++);
      desc = fmt::format("Event: 0x{:02X}  Arg1: {:d}  Arg2: {:d}", statusByte, arg1, arg2);
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
      break;
    }

    case EVENT_UNKNOWN3: {
      uint8_t arg1 = readByte(curOffset++);
      uint8_t arg2 = readByte(curOffset++);
      uint8_t arg3 = readByte(curOffset++);
      desc = fmt::format("Event: 0x{:02X}  Arg1: {:d}  Arg2: {:d}  Arg3: {:d}",
                         statusByte, arg1, arg2, arg3);
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
      break;
    }

    case EVENT_DELTA_TIME: {
      spcDeltaTime = readByte(curOffset++);
      desc = fmt::format("Duration: {:d}", spcDeltaTime);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Delta Time", desc, Type::ChangeState);
      break;
    }

    case EVENT_OPEN_TRACKS: {
      uint8_t targetChannels = readByte(curOffset++);

      desc = "Tracks:";
      for (uint8_t trackIndex = 0; trackIndex < 8; trackIndex++) {
        if ((targetChannels & (0x80 >> trackIndex)) != 0) {
          desc += fmt::format(" {:d}", trackIndex + 1);
        }
      }

      addGenericEvent(beginOffset, curOffset - beginOffset, "Open Tracks", desc, Type::Misc);
      break;
    }

    case EVENT_CALL: {
      uint16_t dest = readShort(curOffset);
      curOffset += 2;
      desc = fmt::format("Destination: ${:04X}", dest);
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Pattern Play",
                      desc,
                      Type::RepeatStart);

      subReturnAddress = curOffset;
      curOffset = dest;
      break;
    }

    case EVENT_END: {
      if ((subReturnAddress & 0xff00) == 0) {
        // end of track
        addEndOfTrack(beginOffset, curOffset - beginOffset);
        bContinue = false;
      }
      else {
        // end of subroutine
        addGenericEvent(beginOffset,
                        curOffset - beginOffset,
                        "Pattern End",
                        desc,
                        Type::RepeatEnd);
        curOffset = subReturnAddress;
        subReturnAddress = 0;
      }
      break;
    }

    case EVENT_DELTA_MULTIPLIER: {
      spcDeltaTimeScale = readByte(curOffset++);
      desc = fmt::format("Delta Time Scale: {:d}", spcDeltaTimeScale);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Delta Time Multiplier", desc, Type::Tempo);
      break;
    }

    case EVENT_MASTER_VOLUME: {
      uint8_t newVol = readByte(curOffset++);
      addMasterVol(beginOffset, curOffset - beginOffset, newVol / 2);
      break;
    }

    case EVENT_LOOP_AGAIN: {
      uint8_t count = readByte(curOffset++);
      uint16_t dest = readShort(curOffset);
      curOffset += 2;
      desc = fmt::format("Times: {:d}  Destination: ${:04X}", count, dest);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Loop Again", desc, Type::RepeatEnd);

      loopCount++;
      if (loopCount == count) {
        // repeat end
        loopCount = 0;
      }
      else {
        // repeat again
        curOffset = dest;
      }

      break;
    }

    case EVENT_LOOP_BREAK: {
      uint8_t count = readByte(curOffset++);
      uint16_t dest = readShort(curOffset);
      curOffset += 2;
      desc = fmt::format("Times: {:d}  Destination: ${:04X}", count, dest);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Loop Break", desc, Type::RepeatEnd);

      loopCount++;
      if (loopCount == count) {
        // repeat break
        curOffset = dest;
        loopCount = 0;
      }

      break;
    }

    case EVENT_LOOP_AGAIN_ALT: {
      uint8_t count = readByte(curOffset++);
      uint16_t dest = readShort(curOffset);
      curOffset += 2;
      desc = fmt::format("Times: {:d}  Destination: ${:04X}", count, dest);
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Loop Again (Alt)",
                      desc,
                      Type::RepeatEnd);

      loopCountAlt++;
      if (loopCountAlt == count) {
        // repeat end
        loopCountAlt = 0;
      }
      else {
        // repeat again
        curOffset = dest;
      }

      break;
    }

    case EVENT_LOOP_BREAK_ALT: {
      uint8_t count = readByte(curOffset++);
      uint16_t dest = readShort(curOffset);
      curOffset += 2;
      desc = fmt::format("Times: {:d}  Destination: ${:04X}", count, dest);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Loop Break", desc, Type::RepeatEnd);

      loopCountAlt++;
      if (loopCountAlt == count) {
        // repeat break
        curOffset = dest;
        loopCountAlt = 0;
      }

      break;
    }

    case EVENT_GOTO: {
      uint16_t dest = readShort(curOffset);
      curOffset += 2;
      desc = fmt::format("Destination: ${:04X}", dest);
      uint32_t length = curOffset - beginOffset;

      // scan end event
      if (curOffset + 1 <= 0x10000 && readByte(curOffset) == 0x03 && (subReturnAddress & 0xff00) == 0) {
        addGenericEvent(curOffset, 1, "End of Track", "", Type::TrackEnd);
      }

      curOffset = dest;
      if (!isOffsetUsed(dest)) {
        addGenericEvent(beginOffset, length, "Jump", desc, Type::LoopForever);
      }
      else {
        bContinue = addLoopForever(beginOffset, length, "Jump");
      }
      break;
    }

    case EVENT_NOTE: {
      uint8_t targetChannels = readByte(curOffset++);
      uint16_t dur = spcDeltaTime * spcDeltaTimeScale;

      desc = "Values:";
      for (uint8_t trackIndex = 0; trackIndex < 8; trackIndex++) {
        if ((targetChannels & (0x80 >> trackIndex)) != 0) {
          uint8_t keyByte = readByte(curOffset++);

          int8_t key;
          NamcoSnesSeqNoteType noteType;
          if (keyByte >= NOTE_NUMBER_PERCUSSION_MIN) {
            key = keyByte - NOTE_NUMBER_PERCUSSION_MIN;
            noteType = NOTE_PERCUSSION;
            desc += fmt::format(" [{:d}] Perc {}", trackIndex + 1, key);
          }
          else if (keyByte >= NOTE_NUMBER_NOISE_MIN) {
            key = keyByte & 0x1f;
            noteType = NOTE_NOISE;
            desc += fmt::format(" [{:d}] Noise {}", trackIndex + 1, key);
          }
          else if (keyByte == NOTE_NUMBER_REST) {
            noteType = prevNoteType[trackIndex];
            desc += fmt::format(" [{:d}] Rest", trackIndex + 1);
          }
          else {
            key = keyByte;
            noteType = NOTE_MELODY;
            desc += fmt::format(" [{:d}] {:d} ({})", trackIndex + 1, key, MidiEvent::getNoteName(key + transpose));
          }

          if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
            channel = trackIndex;
            setCurTrack(channel);

            // key off previous note
            if (prevNoteKey[trackIndex] != -1) {
              addNoteOffNoItem(prevNoteKey[trackIndex]);
              prevNoteKey[trackIndex] = -1;
            }

            // program changes for noise/percussion
            if (noteType != prevNoteType[trackIndex]) {
              switch (noteType) {
                case NOTE_MELODY:
                  addProgramChangeNoItem(instrNum[trackIndex], false);
                  break;

                case NOTE_NOISE:
                  addProgramChangeNoItem(126, false);
                  break;

                case NOTE_PERCUSSION:
                  addProgramChangeNoItem(127, false);

                  // actual music engine applies per-instrument pan
                  addPanNoItem(64);
                  break;
              }

              noteType = prevNoteType[trackIndex];
            }

            if (keyByte != NOTE_NUMBER_REST) {
              prevNoteKey[trackIndex] = key;
              addNoteOnNoItem(key, NOTE_VELOCITY);
            }
          }
        }
      }

      addGenericEvent(beginOffset, curOffset - beginOffset, "Note", desc, Type::DurationNote);
      addTime(dur);
      break;
    }

    case EVENT_ECHO_DELAY: {
      uint8_t echoDelay = readByte(curOffset++);
      desc = fmt::format("Echo Delay: {:d}", echoDelay);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Echo Delay", desc, Type::Reverb);
      break;
    }

    case EVENT_UNKNOWN_0B: {
      uint8_t targetChannels = readByte(curOffset++);

      desc = "Values:";
      for (uint8_t trackIndex = 0; trackIndex < 8; trackIndex++) {
        if ((targetChannels & (0x80 >> trackIndex)) != 0) {
          uint8_t newValue = readByte(curOffset++);
          desc += fmt::format(" [{:d}] {:d}", trackIndex + 1, newValue);
        }
      }

      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
      break;
    }

    case EVENT_ECHO: {
      bool echoOn = (readByte(curOffset++) != 0);
      desc = fmt::format("Echo Write: {}", echoOn ? "On" : "Off");
      addGenericEvent(beginOffset, curOffset - beginOffset, "Echo", desc, Type::Reverb);
      break;
    }

    case EVENT_WAIT: {
      uint16_t dur = spcDeltaTime * spcDeltaTimeScale;
      desc = fmt::format("Delta Time: {:d}", spcDeltaTime);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Wait", desc, Type::Tie);
      addTime(dur);
      break;
    }

    case EVENT_ECHO_FEEDBACK: {
      int8_t echoFeedback = readByte(curOffset++);
      desc = fmt::format("Echo Feedback: {:d}", echoFeedback);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Echo Feedback", desc, Type::Reverb);
      break;
    }

    case EVENT_ECHO_FIR: {
      uint8_t echoFilter = readByte(curOffset++);
      desc = fmt::format("Echo FIR: {:d}", echoFilter);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Echo FIR", desc, Type::Reverb);
      break;
    }

    case EVENT_ECHO_VOLUME: {
      int8_t echoVolumeLeft = readByte(curOffset++);
      int8_t echoVolumeRight = readByte(curOffset++);
      desc = fmt::format("Echo Volume Left: {:d}  Echo Volume Right: {:d}", echoVolumeLeft, echoVolumeRight);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Echo Volume", desc, Type::Reverb);
      break;
    }

    case EVENT_ECHO_ADDRESS: {
      uint16_t echoAddress = readByte(curOffset++) << 8;
      desc = fmt::format("ESA: ${:04X}", echoAddress);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Echo Address", desc, Type::Reverb);
      break;
    }

    case EVENT_CONTROL_CHANGES: {
      uint8_t controlTypeIndex = statusByte & 0x0f;
      uint8_t targetChannels = readByte(curOffset++);

      NamcoSnesSeqControlType controlType = (NamcoSnesSeqControlType) 0;
      std::map<uint8_t, NamcoSnesSeqControlType>::iterator pControlType = ControlChangeMap.find(controlTypeIndex);
      std::string controlName = "Unknown Event";
      if (pControlType != ControlChangeMap.end()) {
        controlType = pControlType->second;
        controlName = ControlChangeNames[controlType];
      }

      desc = "Values:";
      for (uint8_t trackIndex = 0; trackIndex < 8; trackIndex++) {
        if ((targetChannels & (0x80 >> trackIndex)) != 0) {
          uint8_t newValue = readByte(curOffset++);
          desc += fmt::format(" [{:d}] {:d}", trackIndex + 1, newValue);

          if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
            channel = trackIndex;
            setCurTrack(channel);

            switch (controlType) {
              case CONTROL_PROGCHANGE:
                addProgramChangeNoItem(newValue, false);
                instrNum[trackIndex] = newValue;
                break;

              case CONTROL_VOLUME:
                addVolNoItem(newValue / 2);
                break;

              case CONTROL_PAN: {
                uint8_t volumeLeft = newValue & 0xf0;
                uint8_t volumeRight = (newValue & 0x0f) << 4;

                // TODO: apply volume scale
                double linearPan = (double) volumeRight / (volumeLeft + volumeRight);
                uint8_t midiPan = convertLinearPercentPanValToStdMidiVal(linearPan);

                addPanNoItem(midiPan);
                break;
              }

              default:
                break;
            }
          }
        }
      }

      switch (controlType) {
        case CONTROL_PROGCHANGE:
          addGenericEvent(beginOffset, curOffset - beginOffset, controlName, desc, Type::ProgramChange);
          break;

        case CONTROL_VOLUME:
          addGenericEvent(beginOffset, curOffset - beginOffset, controlName, desc, Type::Volume);
          break;

        case CONTROL_PAN:
          addGenericEvent(beginOffset, curOffset - beginOffset, controlName, desc, Type::Pan);
          break;

        case CONTROL_ADSR:
          addGenericEvent(beginOffset, curOffset - beginOffset, controlName, desc, Type::Adsr);
          break;

        default:
          addUnknown(beginOffset, curOffset - beginOffset, controlName, desc);
          break;
      }

      break;
    }

    default: {
      auto descr = logEvent(statusByte);
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", descr);
      bContinue = false;
      break;
    }
  }

  //std::ostringstream ssTrace;
  //ssTrace << "" << std::hex << std::setfill('0') << std::setw(8) << std::uppercase << beginOffset << ": " << std::setw(2) << (int)statusByte  << " -> " << std::setw(8) << curOffset << std::endl;
  //OutputDebugString(ssTrace.str().c_str());

  return bContinue;
}

bool NamcoSnesSeq::postLoad() {
  bool succeeded = VGMSeqNoTrks::postLoad();

  if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
    keyOffAllNotes();
  }

  return succeeded;
}

void NamcoSnesSeq::keyOffAllNotes() {
  for (uint8_t trackIndex = 0; trackIndex < MAX_TRACKS; trackIndex++) {
    if (prevNoteKey[trackIndex] != -1) {
      channel = trackIndex;
      setCurTrack(channel);

      addNoteOffNoItem(prevNoteKey[trackIndex]);
      prevNoteKey[trackIndex] = -1;
    }
  }
}
