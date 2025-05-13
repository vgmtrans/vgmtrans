/*
 * VGMTrans (c) 2002-2019
 * VGMTransQt (c) 2020
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file

 * GBA MusicPlayer2000 (Sappy)
 *
 * Special Thanks / Respects To:
 * GBAMusRiper (c) 2012 by Bregalad
 * http://www.romhacking.net/utilities/881/
 * http://www.romhacking.net/documents/%5B462%5Dsappy.txt
 */

#include "MP2kSeq.h"

#include <array>
#include "MP2kFormat.h"

DECLARE_FORMAT(MP2k);

constexpr std::array<u8, 0x31> length_table{
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C,
    0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x1C,
    0x1E, 0x20, 0x24, 0x28, 0x2A, 0x2C, 0x30, 0x34, 0x36, 0x38, 0x3C, 0x40, 0x42,
    0x44, 0x48, 0x4C, 0x4E, 0x50, 0x54, 0x58, 0x5A, 0x5C, 0x60};

constexpr u8 STATE_NOTE = 0;
constexpr u8 STATE_TIE = 1;
constexpr u8 STATE_TIE_END = 2;
constexpr u8 STATE_VOL = 3;
constexpr u8 STATE_PAN = 4;
constexpr u8 STATE_PITCHBEND = 5;
constexpr u8 STATE_MODULATION = 6;

MP2kSeq::MP2kSeq(RawFile *file, uint32_t offset, std::string name)
    : VGMSeq(MP2kFormat::name, file, offset, 0, std::move(name)) {
  setAllowDiscontinuousTrackData(true);
}

bool MP2kSeq::parseHeader() {
  if (dwOffset + 2 > vgmFile()->endOffset()) {
    return false;
  }

  nNumTracks = readShort(dwOffset);

  // if there are no tracks or there are more tracks than allowed
  // return an error; the sequence shall be deleted
  if (nNumTracks == 0 || nNumTracks > 24) {
    return false;
  }
  if (dwOffset + 8 + nNumTracks * 4 > vgmFile()->endOffset()) {
    return false;
  }

  VGMHeader *seqHdr = addHeader(dwOffset, 8 + nNumTracks * 4, "Sequence header");
  seqHdr->addChild(dwOffset, 1, "Number of tracks");
  seqHdr->addChild(dwOffset + 1, 1, "Unknown");
  seqHdr->addChild(dwOffset + 2, 1, "Priority");
  seqHdr->addChild(dwOffset + 3, 1, "Reverb");

  uint32_t dwInstPtr = readWord(dwOffset + 4);
  seqHdr->addPointer(dwOffset + 4, 4, dwInstPtr - 0x8000000, true, "Instrument pointer");
  for (u32 i = 0; i < nNumTracks; i++) {
    uint32_t dwTrackPtrOffset = dwOffset + 8 + i * 4;
    uint32_t dwTrackPtr = readWord(dwTrackPtrOffset);
    seqHdr->addPointer(dwTrackPtrOffset, 4, dwTrackPtr - 0x8000000, true, "Track pointer");
  }

  setPPQN(0x30);

  return true;
}

bool MP2kSeq::parseTrackPointers(void) {
  // Add each tracks
  for (unsigned int i = 0; i < nNumTracks; i++) {
    uint32_t dwTrackPtrOffset = dwOffset + 8 + i * 4;
    uint32_t dwTrackPtr = readWord(dwTrackPtrOffset);
    aTracks.push_back(new MP2kTrack(this, dwTrackPtr - 0x8000000));
  }

  // Make seq offset the first track offset
  for (auto itr = aTracks.begin(); itr != aTracks.end(); ++itr) {
    SeqTrack *track = (*itr);
    if (track->dwOffset < dwOffset) {
      if (unLength != 0) {
        unLength += (dwOffset - track->dwOffset);
      }
      dwOffset = track->dwOffset;
    }
  }

  return true;
}

//  *********
//  MP2kTrack
//  *********

MP2kTrack::MP2kTrack(MP2kSeq *parentFile, uint32_t offset, uint32_t length)
    : SeqTrack(parentFile, offset, length) {
}

bool MP2kTrack::readEvent() {
  uint32_t beginOffset = curOffset;
  uint8_t status_byte = readByte(curOffset++);
  bool bContinue = true;

  /* Status change event (note, vel (fade), vol, ...) */
  if (status_byte <= 0x7F) {
    handleStatusCommand(beginOffset, status_byte);
  } else if (status_byte >= 0x80 && status_byte <= 0xB0) { /* Rest event */
    addRest(beginOffset, curOffset - beginOffset, length_table[status_byte - 0x80] * 2);
  } else if (status_byte >= 0xD0) { /* Note duration event */
    state = STATE_NOTE;
    curDuration = length_table[status_byte - 0xCF] * 2;

    /* Assume key and velocity values if the next value is greater than 0x7F */
    if (readByte(curOffset) > 0x7F) {
      addNoteByDurNoItem(prevKey, prevVel, curDuration);
      addGenericEvent(beginOffset, curOffset - beginOffset,
                      "Duration Note State + Note On (prev key and vel)", "", Type::DurationNote);
    } else {
      addGenericEvent(beginOffset, curOffset - beginOffset, "Duration Note State", "",
                      Type::ChangeState);
    }
  } else if (status_byte == 0xB1) { /* End of track */
    addEndOfTrack(beginOffset, curOffset - beginOffset);
    return false;
  } else if (status_byte == 0xB2) { /* Goto */
    uint32_t destOffset = getWord(curOffset) - 0x8000000;
    curOffset += 4;
    uint32_t length = curOffset - beginOffset;
    uint32_t dwEndTrackOffset = curOffset;

    curOffset = destOffset;
    if (!isOffsetUsed(destOffset) || loopEndPositions.size() != 0) {
      addGenericEvent(beginOffset, length, "Goto", "", Type::LoopForever);
    } else {
      bContinue = addLoopForever(beginOffset, length, "Goto");
    }

    // Add next end of track event
    if (readMode == READMODE_ADD_TO_UI) {
      if (dwEndTrackOffset < this->parentSeq->vgmFile()->endOffset()) {
        uint8_t nextCmdByte = readByte(dwEndTrackOffset);
        if (nextCmdByte == 0xB1) {
          addEndOfTrack(dwEndTrackOffset, 1);
        }
      }
    }
  } else if (status_byte > 0xB2 && status_byte <= 0xCF) { /* Special event */
    handleSpecialCommand(beginOffset, status_byte);
  }

  return bContinue;
}

void MP2kTrack::handleStatusCommand(u32 beginOffset, u8 status_byte) {
  switch (state) {
    case STATE_NOTE: {
      /* Velocity update might be needed */
      if (readByte(curOffset) <= 0x7F) {
        current_vel = readByte(curOffset++);
        /* If the next value is 0-127, it's unknown (velocity-related?) */
        if (readByte(curOffset) <= 0x7F) {
          curOffset++;
        }
      }
      addNoteByDur(beginOffset, curOffset - beginOffset, status_byte, current_vel, curDuration);
      break;
    }

    case STATE_TIE: {
      /* Velocity update might be needed */
      if (readByte(curOffset) <= 0x7F) {
        current_vel = readByte(curOffset++);
        /* If the next value is 0-127, it's unknown (velocity-related?) */
        if (readByte(curOffset) <= 0x7F) {
          curOffset++;
        }
      }
      addNoteOn(beginOffset, curOffset - beginOffset, status_byte, current_vel, "Tie");
      break;
    }

    case STATE_TIE_END: {
      addNoteOff(beginOffset, curOffset - beginOffset, status_byte, "End Tie");
      break;
    }

    case STATE_VOL: {
      addVol(beginOffset, curOffset - beginOffset, status_byte);
      break;
    }

    case STATE_PAN: {
      addPan(beginOffset, curOffset - beginOffset, status_byte);
      break;
    }

    case STATE_PITCHBEND: {
      addPitchBend(beginOffset, curOffset - beginOffset, static_cast<int16_t>(status_byte - 0x40) * 128);
      break;
    }

    case STATE_MODULATION: {
      addModulation(beginOffset, curOffset - beginOffset, status_byte);
      break;
    }

    default: {
      L_WARN("Illegal status {} {}", state, status_byte);
      break;
    }
  }
}

void MP2kTrack::handleSpecialCommand(u32 beginOffset, u8 status_byte) {
  switch (status_byte) {
    // Branch
    case 0xB3: {
      uint32_t destOffset = getWord(curOffset);
      curOffset += 4;
      loopEndPositions.push_back(curOffset);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Pattern Play", "", Type::Loop);
      curOffset = destOffset - 0x8000000;
      break;
    }

    // Branch Break
    case 0xB4: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "Pattern End", "", Type::Loop);
      if (loopEndPositions.size() != 0) {
        curOffset = loopEndPositions.back();
        loopEndPositions.pop_back();
      }
      break;
    }

    case 0xBA: {
      curOffset++;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Priority", "", Type::Priority);
      break;
    }

    case 0xBB: {
      uint8_t tempo = readByte(curOffset++) * 2;  // tempo in bpm is data byte * 2
      addTempoBPM(beginOffset, curOffset - beginOffset, tempo);
      break;
    }

    case 0xBC: {
      transpose = readByte(curOffset++);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Key Shift", "", Type::Transpose);
      break;
    }

    case 0xBD: {
      uint8_t progNum = readByte(curOffset++);
      addProgramChange(beginOffset, curOffset - beginOffset, progNum);
      break;
    }

    case 0xBE: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "Volume State", "", Type::ChangeState);
      state = STATE_VOL;
      break;
    }

    // pan
    case 0xBF: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "Pan State", "", Type::ChangeState);
      state = STATE_PAN;
      break;
    }

    // pitch bend
    case 0xC0: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "Pitch Bend State", "",
                      Type::ChangeState);
      state = STATE_PITCHBEND;
      break;
    }

    // pitch bend range
    case 0xC1: {
      curOffset++;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Pitch Bend Range", "",
                      Type::PitchBendRange);
      break;
    }

    // lfo speed
    case 0xC2: {
      curOffset++;
      addGenericEvent(beginOffset, curOffset - beginOffset, "LFO Speed", "", Type::Lfo);
      break;
    }

    // lfo delay
    case 0xC3: {
      curOffset++;
      addGenericEvent(beginOffset, curOffset - beginOffset, "LFO Delay", "", Type::Lfo);
      break;
    }

    // modulation depth
    case 0xC4: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "Modulation Depth State", "",
                      Type::Modulation);
      state = STATE_MODULATION;
      break;
    }

    // modulation type
    case 0xC5: {
      curOffset++;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Modulation Type", "", Type::Modulation);
      break;
    }

    case 0xC8: {
      curOffset++;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Microtune", "", Type::PitchBend);
      break;
    }

    // extend command
    case 0xCD: {
      // This command has a subcommand-byte and its arguments.
      // xIECV = 0x08;		//  imi.echo vol   ***lib
      // xIECL = 0x09;		//  imi.echo len   ***lib
      //
      // Probably, some games extend this command by their own code.

      uint8_t subCommand = readByte(curOffset++);
      uint8_t subParam = readByte(curOffset);

      if (subCommand == 0x08 && subParam <= 127) {
        curOffset++;
        addGenericEvent(beginOffset, curOffset - beginOffset, "Echo Volume", "", Type::Misc);
      } else if (subCommand == 0x09 && subParam <= 127) {
        curOffset++;
        addGenericEvent(beginOffset, curOffset - beginOffset, "Echo Length", "", Type::Misc);
      } else {
        // Heuristic method
        while (subParam <= 127) {
          curOffset++;
          subParam = readByte(curOffset);
        }
        addGenericEvent(beginOffset, curOffset - beginOffset, "Extend Command", "", Type::Unknown);
      }
      break;
    }

    case 0xCE: {
      state = STATE_TIE_END;

      // yes, this seems to be how the actual driver code handles it.  Ex. Aria of Sorrow
      // (U): 0x80D91C0 - handle 0xCE event
      if (readByte(curOffset) > 0x7F) {
        addNoteOffNoItem(prevKey);
        addGenericEvent(beginOffset, curOffset - beginOffset, "End Tie State + End Tie", "",
                        Type::Tie);
      } else
        addGenericEvent(beginOffset, curOffset - beginOffset, "End Tie State", "", Type::Tie);
      break;
    }

    case 0xCF: {
      state = STATE_TIE;
      if (readByte(curOffset) > 0x7F) {
        addNoteOnNoItem(prevKey, prevVel);
        addGenericEvent(beginOffset, curOffset - beginOffset,
                        "Tie State + Tie (with prev key and vel)", "", Type::Tie);
      } else
        addGenericEvent(beginOffset, curOffset - beginOffset, "Tie State", "", Type::Tie);
      break;
    }

    default: {
      addUnknown(beginOffset, curOffset - beginOffset);
      break;
    }
  }
}

//  *********
//  MP2kEvent
//  *********

MP2kEvent::MP2kEvent(MP2kTrack *pTrack, uint8_t stateType)
    : SeqEvent(pTrack), eventState(stateType) {
}
