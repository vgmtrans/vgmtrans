#include "SegSatSeq.h"

DECLARE_FORMAT(SegSat);

SegSatSeq::SegSatSeq(RawFile *file, uint32_t offset)
    : VGMSeqNoTrks(SegSatFormat::name, file, offset) {
}

SegSatSeq::~SegSatSeq() {
}

bool SegSatSeq::parseHeader() {
  //unLength = GetShort(dwOffset+8);
  setPPQN(readShortBE(offset()));
  setEventsOffset(readShortBE(offset() + 4) + offset());
  //TryExpandMidiTracks(16);
  nNumTracks = 16;

  //nNumTracks = GetShort(offset()+2);
  //headerFlag = GetByte(offset()+3);
  //length() = GetShort(offset()+4);
  //if (nNumTracks == 0 || nNumTracks > 24)		//if there are no tracks or there are more tracks than allowed
  //	return FALSE;							//return an error, the sequence shall be deleted

  name() = "Sega Saturn Seq";

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

  uint32_t beginOffset = curOffset;
  uint8_t status_byte = readByte(curOffset++);

  if (status_byte <= 0x7F)            // note on
  {
    channel = status_byte & 0x0F;
    setCurTrack(channel);
    auto key = readByte(curOffset++);
    auto vel = readByte(curOffset++);
    auto noteDuration = readByte(curOffset++);
    addTime(readByte(curOffset++));
    addNoteByDur(beginOffset, curOffset - beginOffset, key, vel, noteDuration);
  }
  else {
    if ((status_byte & 0xF0) == 0xB0) {
      curOffset += 3;
      addUnknown(beginOffset, curOffset - beginOffset);
    }
    else if ((status_byte & 0xF0) == 0xC0) {
      channel = status_byte & 0x0F;
      setCurTrack(channel);
      uint8_t progNum = readByte(curOffset++);
      curOffset++;
      addProgramChange(beginOffset, curOffset - beginOffset, progNum);
    }
    else if ((status_byte & 0xF0) == 0xE0) {
      curOffset += 2;
      addUnknown(beginOffset, curOffset - beginOffset);
    }
    else if (status_byte == 0x81)        //loop x # of events
    {
      uint32_t loopOffset = eventsOffset() + readShortBE(curOffset);
      curOffset += 2;
      remainingEventsInLoop = readByte(curOffset++);
      loopEndPos = curOffset;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Reference Event", "", CLR_LOOP);
      curOffset = loopOffset;
      bInLoop = true;
    }
    else if (status_byte == 0x82) {
      curOffset++;
      addUnknown(beginOffset, curOffset - beginOffset);
    }
    else if (status_byte == 0x83) {
      addEndOfTrack(beginOffset, curOffset - beginOffset);
      return false;
    }
  }
  ::counter++;
  if (counter == 7000) {
    counter = 0;
    return false;
  }
  return true;
}
