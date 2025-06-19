/*
* VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

// If you want to convert it without losing any events,
// check seqq2mid tool created by loveemu <https://code.google.com/p/loveemu/>

#include "HeartBeatPS1Seq.h"
#include "HeartBeatPS1Format.h"

DECLARE_FORMAT(HeartBeatPS1)

HeartBeatPS1Seq::HeartBeatPS1Seq(RawFile *file, uint32_t offset, uint32_t length, const std::string &name)
    : VGMSeqNoTrks(HeartBeatPS1Format::name, file, offset, name) {
  this->length() = length;

  useReverb();
}

HeartBeatPS1Seq::~HeartBeatPS1Seq() {
}

bool HeartBeatPS1Seq::parseHeader() {
  uint32_t curOffset = offset();

  uint32_t seq_size = readWord(curOffset);
  if (seq_size < 0x13) {
    return false;
  }

  VGMHeader *header = VGMSeq::addHeader(curOffset, 0x3c);

  header->addChild(curOffset, 4, "Sequence Size");
  header->addChild(curOffset + 4, 2, "Sequence ID");
  header->addChild(curOffset + 6, 1, "Number of Instrument Set");
  header->addChild(curOffset + 7, 1, "Load Position");
  header->addChild(curOffset + 8, 4, "Reserved");

  curOffset += 0x0c;

  // validate instrument header
  uint32_t total_instr_size = 0;
  for (uint8_t instrset_index = 0; instrset_index < 4; instrset_index++) {
    uint32_t sampcoll_size = readWord(curOffset);
    uint32_t instrset_size = readWord(curOffset + 0x04);

    std::ostringstream instrHeaderName;
    instrHeaderName << "Instrument Set " << (instrset_index + 1);
    VGMHeader *instrHeader = header->addHeader(curOffset, 0x0c, instrHeaderName.str());

    instrHeader->addChild(curOffset, 4, "Sample Collection Size");
    instrHeader->addChild(curOffset + 4, 4, "Instrument Set Size");
    instrHeader->addChild(curOffset + 8, 2, "Instrument Set ID");
    instrHeader->addUnknownChild(curOffset + 10, 2);

    total_instr_size += sampcoll_size;
    total_instr_size += instrset_size;

    curOffset += 0x0c;
  }

  // check total file size
  uint32_t total_size = HEARTBEATPS1_SND_HEADER_SIZE + total_instr_size + seq_size;
  if (total_size > 0x200000 || offset() + total_size > rawFile()->size()) {
    return false;
  }

  // set file length if not specified
  if (length() == 0) {
    length() = total_size;
  }

  // save sequence data offset
  seqHeaderOffset = offset() + HEARTBEATPS1_SND_HEADER_SIZE + total_instr_size;

  VGMHeader *seqHeader = VGMSeq::addHeader(seqHeaderOffset, 0x10, "Sequence Header");

  seqHeader->addChild(seqHeaderOffset, 4, "Signature");
  seqHeader->addChild(seqHeaderOffset + 4, 2, "Version");
  seqHeader->addUnknownChild(seqHeaderOffset + 6, 2);
  seqHeader->addChild(seqHeaderOffset + 8, 2, "PPQN");
  seqHeader->addTempo(seqHeaderOffset + 10, 3, "Initial Tempo");
  seqHeader->addChild(seqHeaderOffset + 13, 2, "Time Signature");
  seqHeader->addChild(seqHeaderOffset + 15, 1, "Number of Tracks");

  setPPQN(readShortBE(seqHeaderOffset + 8));
  nNumTracks = 16;

  uint8_t numer = readByte(seqHeaderOffset + 0x0D);
  // uint8_t denom = GetByte(seqHeaderOffset + 0x0E);
  if (numer == 0 || numer > 32) {                //sanity check
    return false;
  }

  uint8_t trackCount = readByte(seqHeaderOffset + 0x0F);
  if (trackCount > 0 && trackCount <= 16) {
    nNumTracks = trackCount;
  }

  setEventsOffset(seqHeaderOffset + 0x10);

  return true;
}

void HeartBeatPS1Seq::resetVars() {
  VGMSeqNoTrks::resetVars();

  uint32_t initialTempo = (readShortBE(seqHeaderOffset + 10) << 8) | readByte(seqHeaderOffset + 10 + 2);
  addTempoNoItem(initialTempo);

  uint8_t numer = readByte(seqHeaderOffset + 0x0D);
  uint8_t denom = readByte(seqHeaderOffset + 0x0E);
  addTimeSigNoItem(numer, 1 << denom, static_cast<uint8_t>(ppqn()));
}

SeqTrack::State HeartBeatPS1Seq::readEvent() {
  uint32_t beginOffset = curOffset;

  // in this format, end of track (FF 2F 00) comes without delta-time.
  // so handle that crazy sequence the first.
  if (curOffset + 3 <= rawFile()->size()) {
    if (readByte(curOffset) == 0xff &&
        readByte(curOffset + 1) == 0x2f &&
        readByte(curOffset + 2) == 0x00) {
      curOffset += 3;
      addEndOfTrack(beginOffset, curOffset - beginOffset);
      return State::Finished;
    }
  }

  uint32_t delta = readVarLen(curOffset);
  if (curOffset >= rawFile()->size())
    return State::Finished;
  addTime(delta);

  uint8_t status_byte = readByte(curOffset++);

  // Running Status
  if (status_byte <= 0x7F) {
    status_byte = runningStatus;
    curOffset--;
  }
  else
    runningStatus = status_byte;

  channel = status_byte & 0x0F;
  setCurTrack(channel);

  switch (status_byte & 0xF0) {
    //note off
    case 0x80 : {
      auto key = readByte(curOffset++);
      auto vel = readByte(curOffset++);
      addNoteOff(beginOffset, curOffset - beginOffset, key);
      break;
    }

    //note event
    case 0x90 : {
      auto key = readByte(curOffset++);
      auto vel = readByte(curOffset++);

      // If the velocity is > 0, it's a note on. Otherwise, it's a note off
      if (vel > 0)
        addNoteOn(beginOffset, curOffset - beginOffset, key, vel);
      else
        addNoteOff(beginOffset, curOffset - beginOffset, key);
      break;
    }

    case 0xA0 :
      addUnknown(beginOffset, curOffset - beginOffset);
      return State::Finished;

    case 0xB0 : {
      uint8_t controlNum = readByte(curOffset++);
      uint8_t value = readByte(curOffset++);
      switch (controlNum)        //control number
      {
        case 1:
          addGenericEvent(beginOffset, curOffset - beginOffset, "Modulation", "", Type::Unknown);
          if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
            pMidiTrack->addControllerEvent(channel, controlNum, value);
          }
          break;

        // identical to CC#11?
        case 2:
          addGenericEvent(beginOffset, curOffset - beginOffset, "Breath Controller?", "", Type::Unknown);
          if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
            pMidiTrack->addControllerEvent(channel, controlNum, value);
          }
          break;

        case 4:
          addGenericEvent(beginOffset, curOffset - beginOffset, "Foot Controller?", "", Type::Unknown);
          if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
            pMidiTrack->addControllerEvent(channel, controlNum, value);
          }
          break;

        case 5:
          addGenericEvent(beginOffset, curOffset - beginOffset, "Portamento Time?", "", Type::Unknown);
          if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
            pMidiTrack->addControllerEvent(channel, controlNum, value);
          }
          break;

        case 6 :
          addGenericEvent(beginOffset, curOffset - beginOffset, "NRPN Data Entry", "", Type::Misc);
          if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
            pMidiTrack->addControllerEvent(channel, controlNum, value);
          }
          break;

        //volume
        case 7 :
          addVol(beginOffset, curOffset - beginOffset, value);
          break;

        case 9:
          addGenericEvent(beginOffset, curOffset - beginOffset, "Control 9", "", Type::Unknown);
          if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
            pMidiTrack->addControllerEvent(channel, controlNum, value);
          }
          break;

        //pan
        case 10 :
          addPan(beginOffset, curOffset - beginOffset, value);
          break;

        //expression
        case 11 :
          addExpression(beginOffset, curOffset - beginOffset, value);
          break;

        case 20:
          addGenericEvent(beginOffset, curOffset - beginOffset, "Control 20", "", Type::Unknown);
          if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
            pMidiTrack->addControllerEvent(channel, controlNum, value);
          }
          break;

        case 21:
          addGenericEvent(beginOffset, curOffset - beginOffset, "Control 21", "", Type::Unknown);
          if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
            pMidiTrack->addControllerEvent(channel, controlNum, value);
          }
          break;

        case 22:
          addGenericEvent(beginOffset, curOffset - beginOffset, "Control 22", "", Type::Unknown);
          if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
            pMidiTrack->addControllerEvent(channel, controlNum, value);
          }
          break;

        case 23:
          addGenericEvent(beginOffset, curOffset - beginOffset, "Control 23", "", Type::Unknown);
          if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
            pMidiTrack->addControllerEvent(channel, controlNum, value);
          }
          break;

        case 32:
          addGenericEvent(beginOffset, curOffset - beginOffset, "Bank LSB?", "", Type::Unknown);
          if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
            pMidiTrack->addControllerEvent(channel, controlNum, value);
          }
          break;

        case 52:
          addGenericEvent(beginOffset, curOffset - beginOffset, "Control 52", "", Type::Unknown);
          if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
            pMidiTrack->addControllerEvent(channel, controlNum, value);
          }
          break;

        case 53:
          addGenericEvent(beginOffset, curOffset - beginOffset, "Control 53", "", Type::Unknown);
          if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
            pMidiTrack->addControllerEvent(channel, controlNum, value);
          }
          break;

        case 54:
          addGenericEvent(beginOffset, curOffset - beginOffset, "Control 54", "", Type::Unknown);
          if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
            pMidiTrack->addControllerEvent(channel, controlNum, value);
          }
          break;

        case 55:
          addGenericEvent(beginOffset, curOffset - beginOffset, "Control 55", "", Type::Unknown);
          if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
            pMidiTrack->addControllerEvent(channel, controlNum, value);
          }
          break;

        case 56:
          addGenericEvent(beginOffset, curOffset - beginOffset, "Control 56", "", Type::Unknown);
          if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
            pMidiTrack->addControllerEvent(channel, controlNum, value);
          }
          break;

        case 64:
          addGenericEvent(beginOffset, curOffset - beginOffset, "Hold 1?", "", Type::Unknown);
          if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
            pMidiTrack->addControllerEvent(channel, controlNum, value);
          }
          break;

        case 69:
          addGenericEvent(beginOffset, curOffset - beginOffset, "Hold 2?", "", Type::Unknown);
          if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
            pMidiTrack->addControllerEvent(channel, controlNum, value);
          }
          break;

        case 71:
          addGenericEvent(beginOffset, curOffset - beginOffset, "Resonance?", "", Type::Unknown);
          if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
            pMidiTrack->addControllerEvent(channel, controlNum, value);
          }
          break;

        case 72:
          addGenericEvent(beginOffset, curOffset - beginOffset, "Release Time?", "", Type::Unknown);
          if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
            pMidiTrack->addControllerEvent(channel, controlNum, value);
          }
          break;

        case 73:
          addGenericEvent(beginOffset, curOffset - beginOffset, "Attack Time?", "", Type::Unknown);
          if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
            pMidiTrack->addControllerEvent(channel, controlNum, value);
          }
          break;

        case 74:
          addGenericEvent(beginOffset, curOffset - beginOffset, "Cut Off Frequency?", "", Type::Unknown);
          if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
            pMidiTrack->addControllerEvent(channel, controlNum, value);
          }
          break;

        case 75:
          addGenericEvent(beginOffset, curOffset - beginOffset, "Decay Time?", "", Type::Unknown);
          if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
            pMidiTrack->addControllerEvent(channel, controlNum, value);
          }
          break;

        case 76:
          addGenericEvent(beginOffset, curOffset - beginOffset, "Vibrato Rate?", "", Type::Unknown);
          if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
            pMidiTrack->addControllerEvent(channel, controlNum, value);
          }
          break;

        case 77:
          addGenericEvent(beginOffset, curOffset - beginOffset, "Vibrato Depth?", "", Type::Unknown);
          if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
            pMidiTrack->addControllerEvent(channel, controlNum, value);
          }
          break;

        case 78:
          addGenericEvent(beginOffset, curOffset - beginOffset, "Vibrato Delay?", "", Type::Unknown);
          if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
            pMidiTrack->addControllerEvent(channel, controlNum, value);
          }
          break;

        case 79:
          addGenericEvent(beginOffset, curOffset - beginOffset, "Control 79", "", Type::Unknown);
          if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
            pMidiTrack->addControllerEvent(channel, controlNum, value);
          }
          break;

        case 91:
          addReverb(beginOffset, curOffset - beginOffset, value);
          break;

        case 92:
          addGenericEvent(beginOffset, curOffset - beginOffset, "Tremolo Depth?", "", Type::Unknown);
          if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
            pMidiTrack->addControllerEvent(channel, controlNum, value);
          }
          break;

        case 98:
          switch (value) {
            case 20 :
              addGenericEvent(beginOffset, curOffset - beginOffset, "NRPN 1 #20", "", Type::Misc);
              break;

            case 30 :
              addGenericEvent(beginOffset, curOffset - beginOffset, "NRPN 1 #30", "", Type::Misc);
              break;

            default:
              addGenericEvent(beginOffset, curOffset - beginOffset, "NRPN 1", "", Type::Misc);
              break;
          }

          if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
            pMidiTrack->addControllerEvent(channel, controlNum, value);
          }
          break;

        //(0x63) nrpn msb
        case 99 :
          switch (value) {
            case 20 :
              addGenericEvent(beginOffset, curOffset - beginOffset, "Loop Start", "", Type::Loop);
              break;

            case 30 :
              addGenericEvent(beginOffset, curOffset - beginOffset, "Loop End", "", Type::Loop);
              break;

            default:
              addGenericEvent(beginOffset, curOffset - beginOffset, "NRPN 2", "", Type::Misc);
              break;
          }

          if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
            pMidiTrack->addControllerEvent(channel, controlNum, value);
          }
          break;

        case 121:
          addGenericEvent(beginOffset, curOffset - beginOffset, "Reset All Controllers", "", Type::Misc);
          if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
            pMidiTrack->addControllerEvent(channel, controlNum, value);
          }
          break;

        case 126:
          addGenericEvent(beginOffset, curOffset - beginOffset, "MONO?", "", Type::Unknown);
          if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
            pMidiTrack->addControllerEvent(channel, controlNum, value);
          }
          break;

        case 127:
          addGenericEvent(beginOffset, curOffset - beginOffset, "Poly?", "", Type::Unknown);
          if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
            pMidiTrack->addControllerEvent(channel, controlNum, value);
          }
          break;

        default:
          addGenericEvent(beginOffset, curOffset - beginOffset, "Control Event", "", Type::Unknown);
          if (VGMSeq::readMode == READMODE_CONVERT_TO_MIDI) {
            pMidiTrack->addControllerEvent(channel, controlNum, value);
          }
          break;
      }
    }
      break;

    case 0xC0 : {
      uint8_t progNum = readByte(curOffset++);
      addProgramChange(beginOffset, curOffset - beginOffset, progNum);
    }
      break;

    case 0xD0 :
      addUnknown(beginOffset, curOffset - beginOffset);
      return State::Finished;

    case 0xE0 : {
      uint8_t lo = readByte(curOffset++);
      uint8_t hi = readByte(curOffset++);
      addPitchBendMidiFormat(beginOffset, curOffset - beginOffset, lo, hi);
      break;
    }

    case 0xF0 : {
      if (status_byte == 0xFF) {
        if (curOffset + 1 > rawFile()->size())
          return State::Finished;

        uint8_t metaNum = readByte(curOffset++);
        uint32_t metaLen = readVarLen(curOffset);
        if (curOffset + metaLen > rawFile()->size())
          return State::Finished;

        switch (metaNum) {
          case 0x51 :
            addTempo(beginOffset,
                     curOffset + metaLen - beginOffset,
                     (readShortBE(curOffset) << 8) | readByte(curOffset + 2));
            curOffset += metaLen;
            break;

          case 0x58 : {
            uint8_t numer = readByte(curOffset);
            uint8_t denom = readByte(curOffset + 1);
            addTimeSig(beginOffset, curOffset + metaLen - beginOffset, numer, 1 << denom, static_cast<uint8_t>(ppqn()));
            curOffset += metaLen;
            break;
          }

          case 0x2F : // apparently not used, but just in case.
            addEndOfTrack(beginOffset, curOffset + metaLen - beginOffset);
            curOffset += metaLen;
            return State::Finished;

          default :
            addUnknown(beginOffset, curOffset + metaLen - beginOffset);
            curOffset += metaLen;
            break;
        }
      }
      else {
        addUnknown(beginOffset, curOffset - beginOffset);
        return State::Finished;
      }
    }
      break;

    default:
      break;
  }
  return State::Active;
}