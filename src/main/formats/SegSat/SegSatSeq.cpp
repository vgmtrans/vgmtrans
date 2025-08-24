#include "SegSatSeq.h"
#include "SegSatFormat.h"
#include "util/MidiConstants.h"

DECLARE_FORMAT(SegSat);

SegSatSeq::SegSatSeq(RawFile *file, uint32_t offset, std::string name)
    : VGMSeqNoTrks(SegSatFormat::name, file, offset, std::move(name)) {
}

void SegSatSeq::resetVars() {
  VGMSeqNoTrks::resetVars();

  remainingEventsInLoop = -1;
  loopEndPos = -1;
  foreverLoopStart = -1;
  durationAccumulator = 0;
}

bool SegSatSeq::parseHeader() {
  setPPQN(readShortBE(offset()));
  normalTrackOffset = offset() + readShortBE(offset() + 4);
  setEventsOffset(offset() + 8);
  nNumTracks = 16;

  VGMHeader *seqHdr = SeqTrack::addHeader(offset(), 8, "Sequence Header");
  seqHdr->addChild(offset(), 2, "Pulses Per Quarter Note");
  seqHdr->addChild(offset() + 2, 2, "Number of Tempo Events");
  seqHdr->addPointer(offset() + 4, 2, offset() + readShortBE(offset()+4),
  true, "Pointer to Normal Track");
  seqHdr->addPointer(offset() + 6, 2, offset() + readShortBE(offset()+6),
    true, "Pointer to First Tempo Event in Loop");
  return true;
}

int counter = 0;

bool SegSatSeq::readEvent() {
  if (bInLoop) {
    remainingEventsInLoop--;
    if (remainingEventsInLoop == -1) {
      bInLoop = false;
      curOffset = loopEndPos;
    }
  }

  // If we're in the tempo track, expect a tempo event
  if (curOffset < normalTrackOffset) {
    u32 tempoDelta = 0;
    while (curOffset < normalTrackOffset) {
      u32 mpqn = readWordBE(curOffset + 4);
      insertTempo(curOffset, 8, mpqn, tempoDelta);
      tempoDelta = readWordBE(curOffset);
      curOffset += 8;
    }
    return true;
  }

  uint32_t beginOffset = curOffset;
  uint8_t status_byte = readByte(curOffset++);

  if (status_byte <= 0x7F)            // note on
  {
    setChannel(status_byte & 0x0F);
    u16 durBit8 = (status_byte & 0x40) << 2;
    u16 deltaBit8 = (status_byte & 0x20) << 3;
    if ((status_byte & 0x10) > 0) {
      L_DEBUG("found 0x10 bit on for note on status byte {:x}", beginOffset);
    }
    auto key = readByte(curOffset++);
    auto vel = readByte(curOffset++);
    u16 noteDuration = readByte(curOffset++) | durBit8;
    noteDuration += durationAccumulator;
    durationAccumulator = 0;
    u16 deltaTime = readByte(curOffset++) | deltaBit8;
    addTime(deltaTime);
    addNoteByDur(beginOffset, curOffset - beginOffset, key, vel, noteDuration);
  }
  else {
    if ((status_byte & 0xF0) == Midi::CONTROL_CHANGE) {
      // BX are midi controller events
      setChannel(status_byte & 0x0F);
      u8 controllerType = readByte(curOffset++);
      u8 controllerValue = readByte(curOffset++);
      u8 deltaTime = readByte(curOffset++);
      addTime(deltaTime);
      switch (controllerType) {
        case Midi::MODULATION_MSB:
          addModulation(beginOffset, curOffset - beginOffset, controllerValue);
          break;
        case Midi::BREATH_MSB:
          addBreath(beginOffset, curOffset - beginOffset, controllerValue);
          break;
        case Midi::VOLUME_MSB:
          addVol(beginOffset, curOffset - beginOffset, controllerValue);
          break;
        case Midi::PAN_MSB:
          addPan(beginOffset, curOffset - beginOffset, controllerValue);
          break;
        case Midi::EXPRESSION_MSB:
          addExpression(beginOffset, curOffset - beginOffset, controllerValue);
          break;
        case Midi::BANK_SELECT_LSB:
          addBankSelect(beginOffset, curOffset - beginOffset, controllerValue);
          break;
        case Midi::SUSTAIN_SWITCH:
          addSustainEvent(beginOffset, curOffset - beginOffset, controllerValue);
          break;
        default:
          addUnknown(beginOffset, curOffset - beginOffset);
          if (controllerValue <= 0x7F)
            addControllerEventNoItem(controllerType, controllerValue);
          break;
      }
    }
    else if ((status_byte & 0xF0) == 0xC0) {
      setChannel(status_byte & 0x0F);
      u8 progNum = readByte(curOffset++);
      addTime(readByte(curOffset++));
      addProgramChange(beginOffset, curOffset - beginOffset, progNum);
    }
    else if ((status_byte & 0xF0) == 0xE0) {
      setChannel(status_byte & 0x0F);
      s16 bend = (static_cast<s32>(readByte(curOffset++)) << 7) - 8192;
      addTime(readByte(curOffset++));
      addPitchBend(beginOffset, curOffset - beginOffset, bend);
    }
    else {
      switch (status_byte) {
        case 0x81: {
          if (remainingEventsInLoop != -1) {
            L_ERROR("entering nested SegSat loop at {:x}. This shouldn't happen.", beginOffset);
          }
          uint32_t loopOffset = normalTrackOffset + readShortBE(curOffset);
          curOffset += 2;
          remainingEventsInLoop = readByte(curOffset++);
          loopEndPos = curOffset;
          addGenericEvent(beginOffset, curOffset - beginOffset, "Loop", "", Type::Loop);
          curOffset = loopOffset;
          bInLoop = true;
          break;
        }
        case 0x82:
          addTime(readByte(curOffset++));
          if (foreverLoopStart == -1) {
            foreverLoopStart = curOffset;
            addGenericEvent(beginOffset, curOffset - beginOffset,
              "Forever Loop Start Point", "", Type::RepeatStart);
          } else {
            if (!addLoopForever(beginOffset, curOffset - beginOffset))
              return false;
            if (SeqTrack::isValidOffset(foreverLoopStart))
              curOffset = foreverLoopStart;
          }
          break;
        case 0x83:
          addEndOfTrack(beginOffset, curOffset - beginOffset);
          return false;
        case 0x87:
          addGenericEvent(beginOffset, curOffset - beginOffset, "Add 256 Duration Ticks", "", Type::Tie);
          durationAccumulator += 256;
          break;
        case 0x88:
          addGenericEvent(beginOffset, curOffset - beginOffset, "Add 512 Duration Ticks", "", Type::Tie);
          durationAccumulator += 512;
          break;
        case 0x89:
          addGenericEvent(beginOffset, curOffset - beginOffset, "Add 2048 Duration Ticks", "", Type::Tie);
          durationAccumulator += 2048;
          break;
        case 0x8A:
          addGenericEvent(beginOffset, curOffset - beginOffset, "Add 4096 Duration Ticks", "", Type::Tie);
          durationAccumulator += 4096;
          break;
        case 0x8B:
          addGenericEvent(beginOffset, curOffset - beginOffset, "Add 8192 Duration Ticks", "", Type::Tie);
          durationAccumulator += 8192;
          break;
        case 0x8C:
          addRest(beginOffset, curOffset - beginOffset, 256, "Rest 256 Ticks");
          break;
        case 0x8D:
          addRest(beginOffset, curOffset - beginOffset, 512, "Rest 512 Ticks");
          break;
        case 0x8E:
          addRest(beginOffset, curOffset - beginOffset, 2048, "Rest 2048 Ticks");
          break;
        case 0x8F:
          addRest(beginOffset, curOffset - beginOffset, 4096, "Rest 4096 Ticks");
          break;
      }
    }
  }
  return true;
}
