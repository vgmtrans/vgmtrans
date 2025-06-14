/*
* VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#include "HudsonSnesSeq.h"

DECLARE_FORMAT(HudsonSnes);

static constexpr int MAX_TRACKS = 8;
static constexpr uint16_t SEQ_PPQN = 48;

//  *************
//  HudsonSnesSeq
//  *************

HudsonSnesSeq::HudsonSnesSeq(RawFile *file, HudsonSnesVersion ver, uint32_t seqdataOffset, std::string name)
    : VGMSeq(HudsonSnesFormat::name, file, seqdataOffset, 0, std::move(name)),
      version(ver),
      TimebaseShift(2),
      TrackAvailableBits(0),
      InstrumentTableAddress(0),
      InstrumentTableSize(0),
      PercussionTableAddress(0),
      PercussionTableSize(0),
      NoteEventHasVelocity(false) {
  bLoadTickByTick = true;
  setAllowDiscontinuousTrackData(true);
  setUseLinearAmplitudeScale(true);

  useReverb();
  setAlwaysWriteInitialReverb(0);

  LoadEventMap();
}

void HudsonSnesSeq::resetVars() {
  VGMSeq::resetVars();

  DisableNoteVelocity = false;

  memset(UserRAM, 0, HUDSONSNES_USERRAM_SIZE);
  UserCmpReg = 0;
  UserCarry = false;
}

bool HudsonSnesSeq::parseHeader() {
  setPPQN(SEQ_PPQN);

  VGMHeader *header = addHeader(dwOffset, 0);
  uint32_t curOffset = dwOffset;

  // track addresses
  if (version == HUDSONSNES_V0 || version == HUDSONSNES_V1) {
    VGMHeader *trackPtrHeader = header->addHeader(curOffset, 0, "Track Pointers");
    if (!parseTrackPointersInHeader(trackPtrHeader, curOffset)) {
      return false;
    }
    trackPtrHeader->setGuessedLength();
  }

  while (true) {
    if (curOffset + 1 > 0x10000) {
      return false;
    }

    uint32_t beginOffset = curOffset;
    uint8_t statusByte = readByte(curOffset++);

    HudsonSnesSeqHeaderEventType eventType = (HudsonSnesSeqHeaderEventType) 0;
    std::map<uint8_t, HudsonSnesSeqHeaderEventType>::iterator pEventType = HeaderEventMap.find(statusByte);
    if (pEventType != HeaderEventMap.end()) {
      eventType = pEventType->second;
    }

    switch (eventType) {
      case HEADER_EVENT_END: {
        header->addChild(beginOffset, curOffset - beginOffset, "Header End");
        goto header_end;
      }

      case HEADER_EVENT_TIMEBASE: {
        VGMHeader *aHeader = header->addHeader(beginOffset, 1, "Timebase");
        aHeader->addChild(beginOffset, 1, "Event ID");

        // actual music engine decreases timebase based on the following value.
        // however, we always use SEQ_PPQN and adjust note lengths when necessary instead.
        aHeader->addChild(curOffset, 1, "Timebase");
        TimebaseShift = readByte(curOffset++) & 3;

        aHeader->unLength = curOffset - beginOffset;
        break;
      }

      case HEADER_EVENT_TRACKS: {
        VGMHeader *trackPtrHeader = header->addHeader(beginOffset, 0, "Track Pointers");
        trackPtrHeader->addChild(beginOffset, 1, "Event ID");
        if (!parseTrackPointersInHeader(trackPtrHeader, curOffset)) {
          return false;
        }
        trackPtrHeader->unLength = curOffset - beginOffset;
        break;
      }

      case HEADER_EVENT_INSTRUMENTS_V1: {
        VGMHeader *instrHeader = header->addHeader(beginOffset, 1, "Instruments");
        instrHeader->addChild(beginOffset, 1, "Event ID");

        instrHeader->addChild(curOffset, 1, "Size");
        uint8_t tableSize = readByte(curOffset++);
        if (curOffset + tableSize > 0x10000) {
          return false;
        }

        InstrumentTableAddress = curOffset;
        InstrumentTableSize = tableSize;

        // instrument table will be parsed by HudsonSnesInstr, too
        uint8_t tableLength = tableSize / 4;
        for (uint8_t instrNum = 0; instrNum < tableLength; instrNum++) {
          uint16_t addrInstrItem = InstrumentTableAddress + instrNum * 4;
          std::stringstream instrName;
          instrName << "Instrument " << instrNum;
          VGMHeader *aInstrHeader = instrHeader->addHeader(addrInstrItem, 4, instrName.str().c_str());
          aInstrHeader->addChild(addrInstrItem, 1, "SRCN");
          aInstrHeader->addChild(addrInstrItem + 1, 1, "ADSR(1)");
          aInstrHeader->addChild(addrInstrItem + 2, 1, "ADSR(2)");
          aInstrHeader->addChild(addrInstrItem + 3, 1, "GAIN");
        }
        curOffset += tableSize;

        instrHeader->unLength = curOffset - beginOffset;
        break;
      }

      case HEADER_EVENT_PERCUSSIONS_V1: {
        VGMHeader *percHeader = header->addHeader(beginOffset, 1, "Percussions");
        percHeader->addChild(beginOffset, 1, "Event ID");

        percHeader->addChild(curOffset, 1, "Size");
        uint8_t tableSize = readByte(curOffset++);
        if (curOffset + tableSize > 0x10000) {
          return false;
        }

        PercussionTableAddress = curOffset;
        PercussionTableSize = tableSize;

        uint8_t tableLength = tableSize / 4;
        for (uint8_t percNote = 0; percNote < tableLength; percNote++) {
          uint16_t addrPercItem = PercussionTableAddress + percNote * 4;
          std::stringstream percNoteName;
          percNoteName << "Percussion " << percNote;
          VGMHeader *percNoteHeader = percHeader->addHeader(addrPercItem, 4, percNoteName.str().c_str());
          percNoteHeader->addChild(addrPercItem, 1, "Instrument");
          percNoteHeader->addChild(addrPercItem + 1, 1, "Unity Key");
          percNoteHeader->addChild(addrPercItem + 2, 1, "Volume");
          percNoteHeader->addChild(addrPercItem + 3, 1, "Pan");
        }
        curOffset += tableSize;

        percHeader->unLength = curOffset - beginOffset;
        break;
      }

      case HEADER_EVENT_INSTRUMENTS_V2: {
        VGMHeader *instrHeader = header->addHeader(beginOffset, 1, "Instruments");
        instrHeader->addChild(beginOffset, 1, "Event ID");

        instrHeader->addChild(curOffset, 1, "Number of Items");
        uint8_t tableSize = readByte(curOffset++) * 4;
        if (curOffset + tableSize > 0x10000) {
          return false;
        }

        InstrumentTableAddress = curOffset;
        InstrumentTableSize = tableSize;

        // instrument table will be parsed by HudsonSnesInstr, too
        uint8_t tableLength = tableSize / 4;
        for (uint8_t instrNum = 0; instrNum < tableLength; instrNum++) {
          uint16_t addrInstrItem = InstrumentTableAddress + instrNum * 4;
          std::stringstream instrName;
          instrName << "Instrument " << instrNum;
          VGMHeader *aInstrHeader = instrHeader->addHeader(addrInstrItem, 4, instrName.str().c_str());
          aInstrHeader->addChild(addrInstrItem, 1, "SRCN");
          aInstrHeader->addChild(addrInstrItem + 1, 1, "ADSR(1)");
          aInstrHeader->addChild(addrInstrItem + 2, 1, "ADSR(2)");
          aInstrHeader->addChild(addrInstrItem + 3, 1, "GAIN");
        }
        curOffset += tableSize;

        instrHeader->unLength = curOffset - beginOffset;
        break;
      }

      case HEADER_EVENT_PERCUSSIONS_V2: {
        VGMHeader *percHeader = header->addHeader(beginOffset, 1, "Percussions");
        percHeader->addChild(beginOffset, 1, "Event ID");

        percHeader->addChild(curOffset, 1, "Number of Items");
        uint8_t tableSize = readByte(curOffset++) * 4;
        if (curOffset + tableSize > 0x10000) {
          return false;
        }

        PercussionTableAddress = curOffset;
        PercussionTableSize = tableSize;

        uint8_t tableLength = tableSize / 4;
        for (uint8_t percNote = 0; percNote < tableLength; percNote++) {
          uint16_t addrPercItem = PercussionTableAddress + percNote * 4;
          std::stringstream percNoteName;
          percNoteName << "Percussion " << percNote;
          VGMHeader *percNoteHeader = percHeader->addHeader(addrPercItem, 4, percNoteName.str().c_str());
          percNoteHeader->addChild(addrPercItem, 1, "Instrument");
          percNoteHeader->addChild(addrPercItem + 1, 1, "Unity Key");
          percNoteHeader->addChild(addrPercItem + 2, 1, "Volume");
          percNoteHeader->addChild(addrPercItem + 3, 1, "Pan");
        }
        curOffset += tableSize;

        percHeader->unLength = curOffset - beginOffset;
        break;
      }

      case HEADER_EVENT_05: {
        VGMHeader *aHeader = header->addHeader(beginOffset, 1, "Unknown");
        aHeader->addChild(beginOffset, 1, "Event ID");

        aHeader->addChild(curOffset, 1, "Number of Items");
        uint8_t tableSize = readByte(curOffset++) * 2;
        if (curOffset + tableSize > 0x10000) {
          return false;
        }

        aHeader->addUnknownChild(curOffset, tableSize);
        curOffset += tableSize;

        aHeader->unLength = curOffset - beginOffset;
        break;
      }

      case HEADER_EVENT_06: {
        VGMHeader *aHeader = header->addHeader(beginOffset, 1, "Unknown");
        aHeader->addChild(beginOffset, 1, "Event ID");

        aHeader->addChild(curOffset, 1, "Number of Items");
        uint8_t tableSize = readByte(curOffset++) * 2;
        if (curOffset + tableSize > 0x10000) {
          return false;
        }

        aHeader->addUnknownChild(curOffset, tableSize);
        curOffset += tableSize;

        aHeader->unLength = curOffset - beginOffset;
        break;
      }

      case HEADER_EVENT_ECHO_PARAM: {
        VGMHeader *aHeader = header->addHeader(beginOffset, 1, "Echo Param");
        aHeader->addChild(beginOffset, 1, "Event ID");

        aHeader->addChild(curOffset, 1, "Use Default Echo");
        uint8_t useDefaultEcho = readByte(curOffset++);

        if (useDefaultEcho == 0) {
          aHeader->addChild(curOffset, 1, "EVOL(L)");
          curOffset++;
          aHeader->addChild(curOffset, 1, "EVOL(R)");
          curOffset++;
          aHeader->addChild(curOffset, 1, "EDL");
          curOffset++;
          aHeader->addChild(curOffset, 1, "EFB");
          curOffset++;
		  aHeader->addChild(curOffset, 1, "FIR");
          curOffset++;
          aHeader->addChild(curOffset, 1, "EON");
          curOffset++;
        }

        aHeader->unLength = curOffset - beginOffset;
        break;
      }

      case HEADER_EVENT_NOTE_VELOCITY: {
        VGMHeader *aHeader = header->addHeader(beginOffset, 1, "Note Velocity");
        aHeader->addChild(beginOffset, 1, "Event ID");

        aHeader->addChild(curOffset, 1, "Note Velocity On/Off");
        uint8_t noteVelOn = readByte(curOffset++);
        if (noteVelOn != 0) {
          NoteEventHasVelocity = true;
        }

        aHeader->unLength = curOffset - beginOffset;
        break;
      }

      case HEADER_EVENT_09: {
        VGMHeader *aHeader = header->addHeader(beginOffset, 1, "Unknown");
        aHeader->addChild(beginOffset, 1, "Event ID");

        aHeader->addChild(curOffset, 1, "Number of Items");
        uint8_t tableSize = readByte(curOffset++) * 2;
        if (curOffset + tableSize > 0x10000) {
          return false;
        }

        aHeader->addUnknownChild(curOffset, tableSize);
        curOffset += tableSize;

        aHeader->unLength = curOffset - beginOffset;
        break;
      }

      default:
        header->addUnknownChild(beginOffset, curOffset - beginOffset);
        goto header_end;
    }
  }

  header_end:
  header->setGuessedLength();

  return true;
}

bool HudsonSnesSeq::parseTrackPointersInHeader(VGMHeader *header, uint32_t &offset) {
  uint32_t beginOffset = offset;
  uint32_t curOffset = beginOffset;

  if (curOffset + 1 > 0x10000) {
    return false;
  }

  // flag field that indicates track availability
  header->addChild(curOffset, 1, "Track Availability");
  TrackAvailableBits = readByte(curOffset++);

  // read track addresses (DSP channel 8 to 1)
  for (int trackIndex = 0; trackIndex < MAX_TRACKS; trackIndex++) {
    if ((TrackAvailableBits & (1 << trackIndex)) != 0) {
      if (curOffset + 2 > 0x10000) {
        offset = curOffset;
        return false;
      }

      std::stringstream trackName;
      trackName << "Track Pointer " << (trackIndex + 1);
      header->addChild(curOffset, 2, trackName.str().c_str());
      TrackAddresses[trackIndex] = readShort(curOffset);
      curOffset += 2;
    }
  }

  offset = curOffset;
  return true;
}

bool HudsonSnesSeq::parseTrackPointers() {
  for (int trackIndex = 0; trackIndex < MAX_TRACKS; trackIndex++) {
    if ((TrackAvailableBits & (1 << trackIndex)) != 0) {
      HudsonSnesTrack *track = new HudsonSnesTrack(this, TrackAddresses[trackIndex]);
      aTracks.push_back(track);
    }
  }

  return true;
}

void HudsonSnesSeq::LoadEventMap() {
  if (version == HUDSONSNES_V0 || version == HUDSONSNES_V1) {
    HeaderEventMap[0x00] = HEADER_EVENT_END;
    HeaderEventMap[0x01] = HEADER_EVENT_TIMEBASE;
    HeaderEventMap[0x02] = HEADER_EVENT_INSTRUMENTS_V1;
    HeaderEventMap[0x03] = HEADER_EVENT_PERCUSSIONS_V1;
    HeaderEventMap[0x04] = HEADER_EVENT_INSTRUMENTS_V2;
    HeaderEventMap[0x05] = HEADER_EVENT_05;
  }
  else { // HUDSONSNES_V2
    HeaderEventMap[0x00] = HEADER_EVENT_END;
    HeaderEventMap[0x01] = HEADER_EVENT_TRACKS;
    HeaderEventMap[0x02] = HEADER_EVENT_TIMEBASE;
    HeaderEventMap[0x03] = HEADER_EVENT_INSTRUMENTS_V2;
    HeaderEventMap[0x04] = HEADER_EVENT_PERCUSSIONS_V2;
    HeaderEventMap[0x05] = HEADER_EVENT_05;
    HeaderEventMap[0x06] = HEADER_EVENT_06;
    HeaderEventMap[0x07] = HEADER_EVENT_ECHO_PARAM;
    HeaderEventMap[0x08] = HEADER_EVENT_NOTE_VELOCITY;
    HeaderEventMap[0x09] = HEADER_EVENT_09;
  }

  for (int statusByte = 0x00; statusByte <= 0xcf; statusByte++) {
    EventMap[statusByte] = EVENT_NOTE;
  }

  EventMap[0xd0] = EVENT_NOP;
  EventMap[0xd1] = EVENT_TEMPO;
  EventMap[0xd2] = EVENT_OCTAVE;
  EventMap[0xd3] = EVENT_OCTAVE_UP;
  EventMap[0xd4] = EVENT_OCTAVE_DOWN;
  EventMap[0xd5] = EVENT_QUANTIZE;
  EventMap[0xd6] = EVENT_PROGCHANGE;
  EventMap[0xd7] = EVENT_NOP1;
  EventMap[0xd8] = EVENT_NOP1;
  EventMap[0xd9] = EVENT_VOLUME;
  EventMap[0xda] = EVENT_PAN;
  EventMap[0xdb] = EVENT_REVERSE_PHASE;
  EventMap[0xdc] = EVENT_VOLUME_REL;
  EventMap[0xdd] = EVENT_LOOP_START;
  EventMap[0xde] = EVENT_LOOP_END;
  EventMap[0xdf] = EVENT_SUBROUTINE;
  EventMap[0xe0] = EVENT_GOTO;
  EventMap[0xe1] = EVENT_TUNING;
  EventMap[0xe2] = EVENT_VIBRATO;
  EventMap[0xe3] = EVENT_VIBRATO_DELAY;
  EventMap[0xe4] = EVENT_ECHO_VOLUME;
  EventMap[0xe5] = EVENT_ECHO_PARAM;
  EventMap[0xe6] = EVENT_ECHO_ON;
  EventMap[0xe7] = EVENT_TRANSPOSE_ABS;
  EventMap[0xe8] = EVENT_TRANSPOSE_REL;
  EventMap[0xe9] = EVENT_PITCH_ATTACK_ENV_ON;
  EventMap[0xea] = EVENT_PITCH_ATTACK_ENV_OFF;
  EventMap[0xeb] = EVENT_LOOP_POINT;
  EventMap[0xec] = EVENT_JUMP_TO_LOOP_POINT;
  EventMap[0xed] = EVENT_LOOP_POINT_ONCE;
  EventMap[0xee] = EVENT_VOLUME_FROM_TABLE;
  EventMap[0xef] = EVENT_UNKNOWN2;
  EventMap[0xf0] = EVENT_UNKNOWN1;
  EventMap[0xf1] = EVENT_PORTAMENTO;
  EventMap[0xf2] = EVENT_NOP;
  EventMap[0xf3] = EVENT_NOP;
  EventMap[0xf4] = EVENT_NOP;
  EventMap[0xf5] = EVENT_NOP;
  EventMap[0xf6] = EVENT_NOP;
  EventMap[0xf7] = EVENT_NOP;
  EventMap[0xf8] = EVENT_NOP;
  EventMap[0xf9] = EVENT_NOP;
  EventMap[0xfa] = EVENT_NOP;
  EventMap[0xfb] = EVENT_NOP;
  EventMap[0xfc] = EVENT_NOP;
  EventMap[0xfd] = EVENT_NOP;
  EventMap[0xfe] = EVENT_SUBEVENT;
  EventMap[0xff] = EVENT_END;

  SubEventMap[0x00] = SUBEVENT_END;
  SubEventMap[0x01] = SUBEVENT_ECHO_OFF;
  SubEventMap[0x02] = SUBEVENT_UNKNOWN0;
  SubEventMap[0x03] = SUBEVENT_PERC_ON;
  SubEventMap[0x04] = SUBEVENT_PERC_OFF;
  SubEventMap[0x05] = SUBEVENT_VIBRATO_TYPE;
  SubEventMap[0x06] = SUBEVENT_VIBRATO_TYPE;
  SubEventMap[0x07] = SUBEVENT_VIBRATO_TYPE;
  SubEventMap[0x08] = SUBEVENT_UNKNOWN0;
  SubEventMap[0x09] = SUBEVENT_UNKNOWN0;

  if (version == HUDSONSNES_V2) {
    EventMap[0xd0] = EVENT_UNKNOWN0;
    EventMap[0xd7] = EVENT_NOP;
    EventMap[0xd8] = EVENT_NOP;
    EventMap[0xe2] = EVENT_VIBRATO;
    EventMap[0xee] = EVENT_NOP;
    EventMap[0xf2] = EVENT_UNKNOWN1;
    EventMap[0xf3] = EVENT_UNKNOWN1;

    SubEventMap[0x05] = SUBEVENT_NOP;
    SubEventMap[0x06] = SUBEVENT_NOP;
    SubEventMap[0x07] = SUBEVENT_NOP;
    SubEventMap[0x0a] = SUBEVENT_UNKNOWN0;
    SubEventMap[0x0b] = SUBEVENT_UNKNOWN0;
    SubEventMap[0x0c] = SUBEVENT_NOTE_VEL_OFF;
    SubEventMap[0x0d] = SUBEVENT_UNKNOWN1;
    SubEventMap[0x0e] = SUBEVENT_NOP;
    SubEventMap[0x0f] = SUBEVENT_NOP;
    SubEventMap[0x10] = SUBEVENT_MOV_IMM;
    SubEventMap[0x11] = SUBEVENT_MOV;
    SubEventMap[0x12] = SUBEVENT_CMP_IMM;
    SubEventMap[0x13] = SUBEVENT_CMP;
    SubEventMap[0x14] = SUBEVENT_BNE;
    SubEventMap[0x15] = SUBEVENT_BEQ;
    SubEventMap[0x16] = SUBEVENT_BCS;
    SubEventMap[0x17] = SUBEVENT_BCC;
    SubEventMap[0x18] = SUBEVENT_BMI;
    SubEventMap[0x19] = SUBEVENT_BPL;
    SubEventMap[0x1a] = SUBEVENT_ADSR_AR;
    SubEventMap[0x1b] = SUBEVENT_ADSR_DR;
    SubEventMap[0x1c] = SUBEVENT_ADSR_SL;
    SubEventMap[0x1d] = SUBEVENT_ADSR_SR;
    SubEventMap[0x1e] = SUBEVENT_ADSR_RR;
  }
}

//  ***************
//  HudsonSnesTrack
//  ***************

HudsonSnesTrack::HudsonSnesTrack(HudsonSnesSeq *parentFile, uint32_t offset, uint32_t length)
    : SeqTrack(parentFile, offset, length) {
  HudsonSnesTrack::resetVars();
  bDetermineTrackLengthEventByEvent = true;
  bWriteGenericEventAsTextEvent = false;
}

void HudsonSnesTrack::resetVars() {
  SeqTrack::resetVars();

  vel = 127;
  octave = 2;
  prevNoteKey = -1;
  prevNoteSlurred = false;
  infiniteLoopPoint = dwStartOffset;
  loopPointOnceProcessed = false;
  spcNoteQuantize = 8;
  spcVolume = 100;
  spcCallStackPtr = 0;
}


bool HudsonSnesTrack::readEvent() {
  HudsonSnesSeq *parentSeq = (HudsonSnesSeq *) this->parentSeq;

  uint32_t beginOffset = curOffset;
  if (curOffset >= 0x10000) {
    return false;
  }

  uint8_t statusByte = readByte(curOffset++);
  bool bContinue = true;

  std::stringstream desc;

  HudsonSnesSeqEventType eventType = (HudsonSnesSeqEventType) 0;
  std::map<uint8_t, HudsonSnesSeqEventType>::iterator pEventType = parentSeq->EventMap.find(statusByte);
  if (pEventType != parentSeq->EventMap.end()) {
    eventType = pEventType->second;
  }

  switch (eventType) {
    case EVENT_UNKNOWN0:
      desc << "Event: 0x" << std::hex << std::setfill('0') << std::setw(2) << std::uppercase << (int) statusByte;
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc.str().c_str());
      break;

    case EVENT_UNKNOWN1: {
      uint8_t arg1 = readByte(curOffset++);
      desc << "Event: 0x" << std::hex << std::setfill('0') << std::setw(2) << std::uppercase << (int) statusByte
          << std::dec << std::setfill(' ') << std::setw(0)
          << "  Arg1: " << (int) arg1;
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc.str().c_str());
      break;
    }

    case EVENT_UNKNOWN2: {
      uint8_t arg1 = readByte(curOffset++);
      uint8_t arg2 = readByte(curOffset++);
      desc << "Event: 0x" << std::hex << std::setfill('0') << std::setw(2) << std::uppercase << (int) statusByte
          << std::dec << std::setfill(' ') << std::setw(0)
          << "  Arg1: " << (int) arg1
          << "  Arg2: " << (int) arg2;
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc.str().c_str());
      break;
    }

    case EVENT_UNKNOWN3: {
      uint8_t arg1 = readByte(curOffset++);
      uint8_t arg2 = readByte(curOffset++);
      uint8_t arg3 = readByte(curOffset++);
      desc << "Event: 0x" << std::hex << std::setfill('0') << std::setw(2) << std::uppercase << (int) statusByte
          << std::dec << std::setfill(' ') << std::setw(0)
          << "  Arg1: " << (int) arg1
          << "  Arg2: " << (int) arg2
          << "  Arg3: " << (int) arg3;
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc.str().c_str());
      break;
    }

    case EVENT_UNKNOWN4: {
      uint8_t arg1 = readByte(curOffset++);
      uint8_t arg2 = readByte(curOffset++);
      uint8_t arg3 = readByte(curOffset++);
      uint8_t arg4 = readByte(curOffset++);
      desc << "Event: 0x" << std::hex << std::setfill('0') << std::setw(2) << std::uppercase << (int) statusByte
          << std::dec << std::setfill(' ') << std::setw(0)
          << "  Arg1: " << (int) arg1
          << "  Arg2: " << (int) arg2
          << "  Arg3: " << (int) arg3
          << "  Arg4: " << (int) arg4;
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc.str().c_str());
      break;
    }

    case EVENT_NOP: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "NOP", desc.str().c_str(), Type::Nop);
      break;
    }

    case EVENT_NOP1: {
      curOffset++;
      addGenericEvent(beginOffset, curOffset - beginOffset, "NOP", desc.str().c_str(), Type::Nop);
      break;
    }

    case EVENT_NOTE: {
      const uint8_t NOTE_DUR_TABLE[8] = {
          0xc0, 0x60, 0x30, 0x18, 0x0c, 0x06, 0x03, 0x01
      };

      uint8_t lenIndex = statusByte & 7;
      bool noKeyoff = (statusByte & 8) != 0; // i.e. slur/tie
      uint8_t keyIndex = statusByte >> 4;

      uint8_t len;
      if (lenIndex != 0) {
        // actually driver adds TimebaseShift to lenIndex.
        // we do not need to decrease the length because we always use SEQ_PPQN.

        if (lenIndex >= 8) {
          // out of range (undefined behavior)
          lenIndex = 7;
        }
        len = NOTE_DUR_TABLE[lenIndex - 1];
      }
      else {
        len = readByte(curOffset++);

        // adjust note length to fit to SEQ_PPQN
        len <<= parentSeq->TimebaseShift;
      }

      // Note: velocity is not officially used
      if (parentSeq->NoteEventHasVelocity && !parentSeq->DisableNoteVelocity) {
        vel = readByte(curOffset++);
      }

      uint8_t dur;
      if (noKeyoff) {
        // slur/tie = full-length
        dur = len;
      }
      else {
        if (spcNoteQuantize <= 8) {
          // q1-q8 (len * N/8)
          dur = len * spcNoteQuantize / 8;
        }
        else {
          // @qN (len - N)
          uint8_t q = spcNoteQuantize - 8;
          uint8_t restDur = q << parentSeq->TimebaseShift;

          if (restDur < len) {
            dur = len - restDur;
          }
          else {
            dur = 1 << parentSeq->TimebaseShift; // TODO: needs verifying
          }
        }
      }

      bool rest = (keyIndex == 0);
      if (rest) {
        addRest(beginOffset, curOffset - beginOffset, len);
        prevNoteSlurred = false;
      }
      else {
        int8_t key = (octave * 12) + (keyIndex - 1);
        if (prevNoteSlurred && key == prevNoteKey) {
          // tie
          makePrevDurNoteEnd(getTime() + dur);
          addGenericEvent(beginOffset, curOffset - beginOffset, "Tie", desc.str().c_str(), Type::Tie);
        }
        else {
          // note
          addNoteByDur(beginOffset, curOffset - beginOffset, key, vel, dur);
          prevNoteKey = key;
        }
        prevNoteSlurred = noKeyoff;
        addTime(len);
      }

      break;
    }

    case EVENT_TEMPO: {
      uint8_t newTempo = readByte(curOffset++);
      addTempoBPM(beginOffset, curOffset - beginOffset, newTempo);
      break;
    }

    case EVENT_OCTAVE: {
      uint8_t newOctave = readByte(curOffset++);
      addSetOctave(beginOffset, curOffset - beginOffset, newOctave);
      break;
    }

    case EVENT_OCTAVE_UP: {
      addIncrementOctave(beginOffset, curOffset - beginOffset);
      break;
    }

    case EVENT_OCTAVE_DOWN: {
      addDecrementOctave(beginOffset, curOffset - beginOffset);
      break;
    }

    case EVENT_QUANTIZE: {
      uint8_t newQuantize = readByte(curOffset++);
	  spcNoteQuantize = newQuantize;
      if (newQuantize <= 8) {
        desc << "Length: " << (int) newQuantize << "/8";
      }
      else {
        desc << "Length: " << "Full-Length - " << (int) (newQuantize - 8);
      }
      addGenericEvent(beginOffset, curOffset - beginOffset, "Duration Rate", desc.str().c_str(), Type::DurationNote);
      break;
    }

    case EVENT_PROGCHANGE: {
      uint8_t newProg = readByte(curOffset++);
      addProgramChange(beginOffset, curOffset - beginOffset, newProg);
      break;
    }

    case EVENT_VOLUME: {
      uint8_t vol = readByte(curOffset++);
      spcVolume = vol;
      addVol(beginOffset, curOffset - beginOffset, spcVolume >> 1);
      break;
    }

    case EVENT_PAN: {
      const uint8_t PAN_TABLE[31] = {
          0x00, 0x07, 0x0d, 0x14, 0x1a, 0x21, 0x27, 0x2e,
          0x34, 0x3a, 0x40, 0x45, 0x4b, 0x50, 0x55, 0x5a,
          0x5e, 0x63, 0x67, 0x6b, 0x6e, 0x71, 0x74, 0x77,
          0x79, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f, 0x7f,
      };

      uint8_t panIndex = readByte(curOffset++) & 0x1f;
      if (panIndex > 30) {
        // out of range
        panIndex = 30;
      }

      // TODO: calculate accurate pan value
      // Left Volume  = PAN_TABLE[panIndex];
      // Right Volume = PAN_TABLE[30 - panIndex];
      addPan(beginOffset, curOffset - beginOffset, PAN_TABLE[30 - panIndex]);

      break;
    }

    case EVENT_REVERSE_PHASE: {
      uint8_t reverse = readByte(curOffset++);
      bool reverseLeft = (reverse & 2) != 0;
      bool reverseRight = (reverse & 1) != 0;
      desc << "Reverse Left: " << (reverseLeft ? "On" : "Off") << "Reverse Right: "
          << (reverseRight ? "On" : "Off");
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Reverse Phase",
                      desc.str().c_str(),
                      Type::ChangeState);
      break;
    }

    case EVENT_VOLUME_REL: {
      int8_t delta = readByte(curOffset++);
      int newVolume = spcVolume + delta;
      if (newVolume < 0) {
        newVolume = 0;
      }
      else if (newVolume > 0xff) {
        newVolume = 0xff;
      }
      spcVolume = (uint8_t) newVolume;
      addVol(beginOffset, curOffset - beginOffset, spcVolume >> 1, "Volume (Relative)");
      break;
    }

    case EVENT_LOOP_START: {
      uint8_t count = readByte(curOffset);
      desc << "Loop Count: " << (int) count;
      addGenericEvent(beginOffset, 2, "Loop Start", desc.str().c_str(), Type::RepeatStart);

      if (spcCallStackPtr + 3 > HUDSONSNES_CALLSTACK_SIZE) {
        // stack overflow
        bContinue = false;
        break;
      }

      // save loop start address and repeat count
      spcCallStack[spcCallStackPtr++] = curOffset & 0xff;
      spcCallStack[spcCallStackPtr++] = (curOffset >> 8) & 0xff;
      spcCallStack[spcCallStackPtr++] = count;

      // increment pointer after pushing return address
      curOffset++;

      break;
    }

    case EVENT_LOOP_END: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "Loop End", desc.str().c_str(), Type::RepeatEnd);

      if (spcCallStackPtr < 3) {
        // access violation
        bContinue = false;
        break;
      }

      uint8_t count = spcCallStack[spcCallStackPtr - 1];
      if (--count == 0) {
        // repeat end, fall through
        spcCallStackPtr -= 3;
      }
      else {
        // repeat again
        spcCallStack[spcCallStackPtr - 1] = count;
        curOffset = (spcCallStack[spcCallStackPtr - 3] | (spcCallStack[spcCallStackPtr - 2] << 8)) + 1;
      }

      break;
    }

    case EVENT_SUBROUTINE: {
      uint16_t dest = readShort(curOffset);
      desc << "Destination: $" << std::hex << std::setfill('0') << std::setw(4) << std::uppercase << (int) dest;
      addGenericEvent(beginOffset, 3, "Pattern Play", desc.str().c_str(), Type::RepeatStart);

      if (spcCallStackPtr + 2 > HUDSONSNES_CALLSTACK_SIZE) {
        // stack overflow
        bContinue = false;
        break;
      }

      // save loop start address and repeat count
      spcCallStack[spcCallStackPtr++] = curOffset & 0xff;
      spcCallStack[spcCallStackPtr++] = (curOffset >> 8) & 0xff;

      // jump to subroutine address
      curOffset = dest;

      break;
    }

    case EVENT_GOTO: {
      uint16_t dest = readShort(curOffset);
      curOffset += 2;
      desc << "Destination: $" << std::hex << std::setfill('0') << std::setw(4) << std::uppercase << (int) dest;
      uint32_t length = curOffset - beginOffset;

      curOffset = dest;
      if (!isOffsetUsed(dest)) {
        addGenericEvent(beginOffset, length, "Jump", desc.str().c_str(), Type::LoopForever);
      }
      else {
        bContinue = addLoopForever(beginOffset, length, "Jump");
      }
      break;
    }

    case EVENT_TUNING: {
      int8_t newTuning = readByte(curOffset++);
      double semitones = newTuning / 256.0;
      addFineTuning(beginOffset, curOffset - beginOffset, semitones * 100.0);
      break;
    }

    case EVENT_VIBRATO: {
      if (parentSeq->version == HUDSONSNES_V0 || parentSeq->version == HUDSONSNES_V1) {
        uint8_t lfoRate = readByte(curOffset++);
        uint8_t lfoDepth = readByte(curOffset++);
        desc << "Rate: " << (int) lfoRate << "  Depth: " << (int) lfoDepth;
        addGenericEvent(beginOffset,
                        curOffset - beginOffset,
                        "Vibrato",
                        desc.str().c_str(),
                        Type::Vibrato);
      }
      else { // HUDSONSNES_V2
        uint8_t arg1 = readByte(curOffset++);
        uint8_t arg2 = readByte(curOffset++);
        uint8_t arg3 = readByte(curOffset++);
        desc << "Arg1: " << (int) arg1 << "  Arg2: " << (int) arg2 << "  Arg3: " << (int) arg3;
        addGenericEvent(beginOffset,
                        curOffset - beginOffset,
                        "Vibrato",
                        desc.str().c_str(),
                        Type::Vibrato);
      }
      break;
    }

    case EVENT_VIBRATO_DELAY: {
      uint8_t lfoDelay = readByte(curOffset++);
      desc << "Delay: " << (int) lfoDelay;
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Vibrato Delay",
                      desc.str().c_str(),
                      Type::Vibrato);
      break;
    }

    case EVENT_ECHO_VOLUME: {
      int8_t volumeLeft = readByte(curOffset++);
      int8_t volumeRight = readByte(curOffset++);
      desc << "Left Volume: " << (int) volumeLeft << "  Right Volume: " << (int) volumeRight;
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Echo Volume",
                      desc.str().c_str(),
                      Type::Reverb);
      break;
    }

    case EVENT_ECHO_PARAM: {
      uint8_t delay = readByte(curOffset++);
      uint8_t feedback = readByte(curOffset++);
      uint8_t filterIndex = readByte(curOffset++);
      desc << "Echo Delay: " << (int) delay << "  Echo Feedback: " << (int) feedback << "  FIR: "
          << (int) filterIndex;
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Echo Parameter",
                      desc.str().c_str(),
                      Type::Reverb);
      break;
    }

    case EVENT_ECHO_ON: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "Echo On", desc.str().c_str(), Type::Reverb);
      break;
    }

    case EVENT_TRANSPOSE_ABS: {
      int8_t newTranspose = readByte(curOffset++);
      addTranspose(beginOffset, curOffset - beginOffset, newTranspose);
      break;
    }

    case EVENT_TRANSPOSE_REL: {
      int8_t delta = readByte(curOffset++);
      addTranspose(beginOffset, curOffset - beginOffset, transpose + delta, "Transpose (Relative)");
      break;
    }

    case EVENT_PITCH_ATTACK_ENV_ON: {
      uint8_t speed = readByte(curOffset++);
      uint8_t depth = readByte(curOffset++);
      uint8_t direction = readByte(curOffset++);
      bool upTo = (direction != 0);
      desc << "Speed: " << (int) speed << "  Depth: " << (int) depth << "  Direction: " << (upTo ? "Up" : "Down");
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Attack Pitch Envelope On",
                      desc.str().c_str(),
                      Type::PitchEnvelope);
      break;
    }

    case EVENT_PITCH_ATTACK_ENV_OFF: {
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Attack Pitch Envelope Off",
                      desc.str().c_str(),
                      Type::PitchEnvelope);
      break;
    }

    case EVENT_LOOP_POINT: {
      infiniteLoopPoint = curOffset;
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Infinite Loop Point",
                      desc.str().c_str(),
                      Type::RepeatStart);
      break;
    }

    case EVENT_JUMP_TO_LOOP_POINT: {
      bContinue = addLoopForever(beginOffset, curOffset - beginOffset);
      curOffset = infiniteLoopPoint;
      break;
    }

    case EVENT_LOOP_POINT_ONCE: {
      addGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Infinite Loop Point (Ignore after the Second Time)",
                      desc.str().c_str(),
                      Type::RepeatStart);
      if (!loopPointOnceProcessed) {
        infiniteLoopPoint = curOffset;
        loopPointOnceProcessed = true;
      }
      break;
    }

    case EVENT_VOLUME_FROM_TABLE: {
      const uint8_t VOLUME_TABLE[91] = {
          0x00, 0x01, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
          0x02, 0x02, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
          0x04, 0x04, 0x04, 0x04, 0x05, 0x05, 0x05, 0x05,
          0x06, 0x06, 0x06, 0x07, 0x07, 0x08, 0x08, 0x09,
          0x09, 0x0a, 0x0a, 0x0b, 0x0b, 0x0c, 0x0d, 0x0e,
          0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
          0x17, 0x18, 0x1a, 0x1b, 0x1d, 0x1e, 0x20, 0x22,
          0x24, 0x26, 0x28, 0x2b, 0x2d, 0x30, 0x33, 0x36,
          0x39, 0x3c, 0x40, 0x44, 0x48, 0x4c, 0x51, 0x55,
          0x5a, 0x60, 0x66, 0x6c, 0x72, 0x79, 0x80, 0x87,
          0x8f, 0x98, 0xa1, 0xaa, 0xb5, 0xbf, 0xcb, 0xd7,
          0xe3, 0xf1, 0xff,
      };

      uint8_t volIndex = readByte(curOffset++);
      if (volIndex > 90) {
        // out of range
        volIndex = 90;
      }

      spcVolume = VOLUME_TABLE[volIndex];
      addVol(beginOffset, curOffset - beginOffset, spcVolume >> 1, "Volume From Table");
      break;
    }

    case EVENT_PORTAMENTO: {
      uint8_t portamentoTime = readByte(curOffset++);
      uint8_t arg2 = readByte(curOffset++);

      if (portamentoTime == 0) {
        addPortamento(beginOffset, curOffset - beginOffset, false);
        addPortamentoTimeNoItem(0);
      }
      else {
        addPortamento(beginOffset, curOffset - beginOffset, true);
        addPortamentoTimeNoItem(portamentoTime);
      }

      break;
    }

    case EVENT_END: {
      if (spcCallStackPtr == 0) {
        // end of track
        addEndOfTrack(beginOffset, curOffset - beginOffset);
        bContinue = false;
      }
      else {
        // end subroutine
        addGenericEvent(beginOffset,
                        curOffset - beginOffset,
                        "End Pattern",
                        desc.str().c_str(),
                        Type::RepeatEnd);

        if (spcCallStackPtr < 2) {
          // access violation
          bContinue = false;
          break;
        }

        curOffset = (spcCallStack[spcCallStackPtr - 2] | (spcCallStack[spcCallStackPtr - 1] << 8)) + 2;
        spcCallStackPtr -= 2;
      }
      break;
    }

    case EVENT_SUBEVENT: {
      uint8_t subStatusByte = readByte(curOffset++);
      HudsonSnesSeqSubEventType subEventType = (HudsonSnesSeqSubEventType) 0;
      std::map<uint8_t, HudsonSnesSeqSubEventType>::iterator pSubEventType = parentSeq->SubEventMap.find(subStatusByte);
      if (pSubEventType != parentSeq->SubEventMap.end()) {
        subEventType = pSubEventType->second;
      }

      switch (subEventType) {
        case SUBEVENT_UNKNOWN0:
          desc << "Subevent: 0x" << std::hex << std::setfill('0') << std::setw(2) << std::uppercase
              << (int) subStatusByte;
          addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc.str().c_str());
          break;

        case SUBEVENT_UNKNOWN1: {
          uint8_t arg1 = readByte(curOffset++);
          desc << "Subevent: 0x" << std::hex << std::setfill('0') << std::setw(2) << std::uppercase
              << (int) subStatusByte
              << std::dec << std::setfill(' ') << std::setw(0)
              << "  Arg1: " << (int) arg1;
          addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc.str().c_str());
          break;
        }

        case SUBEVENT_UNKNOWN2: {
          uint8_t arg1 = readByte(curOffset++);
          uint8_t arg2 = readByte(curOffset++);
          desc << "Subevent: 0x" << std::hex << std::setfill('0') << std::setw(2) << std::uppercase
              << (int) subStatusByte
              << std::dec << std::setfill(' ') << std::setw(0)
              << "  Arg1: " << (int) arg1
              << "  Arg2: " << (int) arg2;
          addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc.str().c_str());
          break;
        }

        case SUBEVENT_UNKNOWN3: {
          uint8_t arg1 = readByte(curOffset++);
          uint8_t arg2 = readByte(curOffset++);
          uint8_t arg3 = readByte(curOffset++);
          desc << "Subevent: 0x" << std::hex << std::setfill('0') << std::setw(2) << std::uppercase
              << (int) subStatusByte
              << std::dec << std::setfill(' ') << std::setw(0)
              << "  Arg1: " << (int) arg1
              << "  Arg2: " << (int) arg2
              << "  Arg3: " << (int) arg3;
          addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc.str().c_str());
          break;
        }

        case SUBEVENT_UNKNOWN4: {
          uint8_t arg1 = readByte(curOffset++);
          uint8_t arg2 = readByte(curOffset++);
          uint8_t arg3 = readByte(curOffset++);
          uint8_t arg4 = readByte(curOffset++);
          desc << "Subevent: 0x" << std::hex << std::setfill('0') << std::setw(2) << std::uppercase
              << (int) subStatusByte
              << std::dec << std::setfill(' ') << std::setw(0)
              << "  Arg1: " << (int) arg1
              << "  Arg2: " << (int) arg2
              << "  Arg3: " << (int) arg3
              << "  Arg4: " << (int) arg4;
          addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc.str().c_str());
          break;
        }

        case SUBEVENT_NOP: {
          addGenericEvent(beginOffset, curOffset - beginOffset, "NOP", desc.str().c_str(), Type::Nop);
          break;
        }

        case SUBEVENT_END: {
          addEndOfTrack(beginOffset, curOffset - beginOffset);
          bContinue = false;
          break;
        }

        case SUBEVENT_ECHO_OFF: {
          addGenericEvent(beginOffset,
                          curOffset - beginOffset,
                          "Echo Off",
                          desc.str().c_str(),
                          Type::Reverb);
          break;
        }

        case SUBEVENT_PERC_ON: {
          addGenericEvent(beginOffset,
                          curOffset - beginOffset,
                          "Percussion On",
                          desc.str().c_str(),
                          Type::UseDrumKit);
          break;
        }

        case SUBEVENT_PERC_OFF: {
          addGenericEvent(beginOffset,
                          curOffset - beginOffset,
                          "Percussion Off",
                          desc.str().c_str(),
                          Type::UseDrumKit);
          break;
        }

        case SUBEVENT_VIBRATO_TYPE: {
          uint8_t vibratoType = subStatusByte - 0x05;
          desc << "Vibrato Type: " << (int) vibratoType;
          addGenericEvent(beginOffset,
                          curOffset - beginOffset,
                          "Vibrato Type",
                          desc.str().c_str(),
                          Type::Vibrato);
          break;
        }

        case SUBEVENT_NOTE_VEL_OFF: {
          parentSeq->DisableNoteVelocity = true;
          addGenericEvent(beginOffset, curOffset - beginOffset, "Note Velocity Off", desc.str().c_str(), Type::ChangeState);
          break;
        }

        case SUBEVENT_MOV_IMM: {
          uint8_t reg = readByte(curOffset++);
          uint8_t val = readByte(curOffset++);
          desc << "Register: " << (int) reg << "  Value: " << (int) val;
          addGenericEvent(beginOffset, curOffset - beginOffset, "MOV (Immediate)", desc.str().c_str(), Type::Misc);

          if (reg < HUDSONSNES_USERRAM_SIZE) {
            parentSeq->UserRAM[reg] = val;
            parentSeq->UserCmpReg = val;
          }
          break;
        }

        case SUBEVENT_MOV: {
          uint8_t regDst = readByte(curOffset++);
          uint8_t regSrc = readByte(curOffset++);
          desc << "Destination Register: " << (int) regDst << "  Source Register: " << (int) regSrc;
          addGenericEvent(beginOffset, curOffset - beginOffset, "MOV", desc.str().c_str(), Type::Misc);

          if (regDst < HUDSONSNES_USERRAM_SIZE && regSrc < HUDSONSNES_USERRAM_SIZE) {
            uint8_t val = parentSeq->UserRAM[regSrc];
            parentSeq->UserRAM[regDst] = val;
            parentSeq->UserCmpReg = val;
          }
          break;
        }

        case SUBEVENT_CMP_IMM: {
          uint8_t reg = readByte(curOffset++);
          uint8_t val = readByte(curOffset++);
          desc << "Register: " << (int) reg << "  Value: " << (int) val;
          addGenericEvent(beginOffset, curOffset - beginOffset, "CMP (Immediate)", desc.str().c_str(), Type::Misc);

          if (reg < HUDSONSNES_USERRAM_SIZE) {
            parentSeq->UserCmpReg = parentSeq->UserRAM[reg] - val;
            parentSeq->UserCarry = (parentSeq->UserRAM[reg] > val);
          }
          break;
        }

        case SUBEVENT_CMP: {
          uint8_t regDst = readByte(curOffset++);
          uint8_t regSrc = readByte(curOffset++);
          desc << "Destination Register: " << (int) regDst << "  Source Register: " << (int) regSrc;
          addGenericEvent(beginOffset, curOffset - beginOffset, "CMP", desc.str().c_str(), Type::Misc);

          if (regDst < HUDSONSNES_USERRAM_SIZE && regSrc < HUDSONSNES_USERRAM_SIZE) {
            parentSeq->UserCmpReg = parentSeq->UserRAM[regDst] - parentSeq->UserRAM[regSrc];
            parentSeq->UserCarry = (parentSeq->UserRAM[regDst] > parentSeq->UserRAM[regSrc]);
          }
          break;
        }

        case SUBEVENT_BNE: {
          uint16_t dest = readShort(curOffset);
          curOffset += 2;
          desc << "Destination: $" << std::hex << std::setfill('0') << std::setw(4) << std::uppercase << (int) dest;
          addGenericEvent(beginOffset, curOffset - beginOffset, "BNE", desc.str().c_str(), Type::Misc);

          if (parentSeq->UserCmpReg != 0) {
            curOffset = dest;
          }

          break;
        }

        case SUBEVENT_BEQ: {
          uint16_t dest = readShort(curOffset);
          curOffset += 2;
          desc << "Destination: $" << std::hex << std::setfill('0') << std::setw(4) << std::uppercase << (int) dest;
          addGenericEvent(beginOffset, curOffset - beginOffset, "BEQ", desc.str().c_str(), Type::Misc);

          if (parentSeq->UserCmpReg != 0) {
            curOffset = dest;
          }

          break;
        }

        case SUBEVENT_BCS: {
          uint16_t dest = readShort(curOffset);
          curOffset += 2;
          desc << "Destination: $" << std::hex << std::setfill('0') << std::setw(4) << std::uppercase << (int) dest;
          addGenericEvent(beginOffset, curOffset - beginOffset, "BCS", desc.str().c_str(), Type::Misc);

          if (parentSeq->UserCarry) {
            curOffset = dest;
          }

          break;
        }

        case SUBEVENT_BCC: {
          uint16_t dest = readShort(curOffset);
          curOffset += 2;
          desc << "Destination: $" << std::hex << std::setfill('0') << std::setw(4) << std::uppercase << (int) dest;
          addGenericEvent(beginOffset, curOffset - beginOffset, "BCC", desc.str().c_str(), Type::Misc);

          if (!parentSeq->UserCarry) {
            curOffset = dest;
          }

          break;
        }

        case SUBEVENT_BMI: {
          uint16_t dest = readShort(curOffset);
          curOffset += 2;
          desc << "Destination: $" << std::hex << std::setfill('0') << std::setw(4) << std::uppercase << (int) dest;
          addGenericEvent(beginOffset, curOffset - beginOffset, "BMI", desc.str().c_str(), Type::Misc);

          if ((parentSeq->UserCmpReg & 0x80) != 0) {
            curOffset = dest;
          }

          break;
        }

        case SUBEVENT_BPL: {
          uint16_t dest = readShort(curOffset);
          curOffset += 2;
          desc << "Destination: $" << std::hex << std::setfill('0') << std::setw(4) << std::uppercase << (int) dest;
          addGenericEvent(beginOffset, curOffset - beginOffset, "BPL", desc.str().c_str(), Type::Misc);

          if ((parentSeq->UserCmpReg & 0x80) == 0) {
            curOffset = dest;
          }

          break;
        }

        case SUBEVENT_ADSR_AR: {
          uint8_t newAR = readByte(curOffset++) & 15;
          desc << "AR: " << (int) newAR;
          addGenericEvent(beginOffset,
                          curOffset - beginOffset,
                          "ADSR Attack Rate",
                          desc.str().c_str(),
                          Type::Adsr);
          break;
        }

        case SUBEVENT_ADSR_DR: {
          uint8_t newDR = readByte(curOffset++) & 7;
          desc << "DR: " << (int) newDR;
          addGenericEvent(beginOffset,
                          curOffset - beginOffset,
                          "ADSR Decay Rate",
                          desc.str().c_str(),
                          Type::Adsr);
          break;
        }

        case SUBEVENT_ADSR_SL: {
          uint8_t newSL = readByte(curOffset++) & 7;
          desc << "SL: " << (int) newSL;
          addGenericEvent(beginOffset,
                          curOffset - beginOffset,
                          "ADSR Sustain Level",
                          desc.str().c_str(),
                          Type::Adsr);
          break;
        }

        case SUBEVENT_ADSR_SR: {
          uint8_t newSR = readByte(curOffset++) & 15;
          desc << "SR: " << (int) newSR;
          addGenericEvent(beginOffset,
                          curOffset - beginOffset,
                          "ADSR Sustain Rate",
                          desc.str().c_str(),
                          Type::Adsr);
          break;
        }

        case SUBEVENT_ADSR_RR: {
          uint8_t newSR = readByte(curOffset++) & 15;
          desc << "SR: " << (int) newSR;
          addGenericEvent(beginOffset,
                          curOffset - beginOffset,
                          "ADSR Release Rate",
                          desc.str().c_str(),
                          Type::Adsr);
          break;
        }

        default: {
          auto descr = logEvent(subStatusByte, spdlog::level::err, "Subevent");
          addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", descr);
          bContinue = false;
          break;
        }
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

  //ostringstream ssTrace;
  //ssTrace << "" << std::hex << std::setfill('0') << std::setw(8) << std::uppercase << beginOffset << ": " << std::setw(2) << (int)statusByte  << " -> " << std::setw(8) << curOffset << std::endl;
  //OutputDebugString(ssTrace.str().c_str());

  return bContinue;
}
