#include "pch.h"
#include "HudsonSnesSeq.h"

DECLARE_FORMAT(HudsonSnes);

//  *************
//  HudsonSnesSeq
//  *************
#define MAX_TRACKS  8
#define SEQ_PPQN    48

HudsonSnesSeq::HudsonSnesSeq(RawFile *file, HudsonSnesVersion ver, uint32_t seqdataOffset, std::wstring newName)
    : VGMSeq(HudsonSnesFormat::name, file, seqdataOffset, 0, newName),
      version(ver),
      TimebaseShift(2),
      TrackAvailableBits(0),
      InstrumentTableAddress(0),
      InstrumentTableSize(0),
      PercussionTableAddress(0),
      PercussionTableSize(0) {
  bLoadTickByTick = true;
  bAllowDiscontinuousTrackData = true;
  bUseLinearAmplitudeScale = true;

  UseReverb();
  AlwaysWriteInitialReverb(0);

  LoadEventMap();
}

HudsonSnesSeq::~HudsonSnesSeq(void) {
}

void HudsonSnesSeq::ResetVars(void) {
  VGMSeq::ResetVars();

  memset(UserRAM, 0, HUDSONSNES_USERRAM_SIZE);
  UserCmpReg = 0;
  UserCarry = false;
}

bool HudsonSnesSeq::GetHeaderInfo(void) {
  SetPPQN(SEQ_PPQN);

  VGMHeader *header = AddHeader(dwOffset, 0);
  uint32_t curOffset = dwOffset;

  // track addresses
  if (version == HUDSONSNES_V0 || version == HUDSONSNES_V1) {
    VGMHeader *trackPtrHeader = header->AddHeader(curOffset, 0, L"Track Pointers");
    if (!GetTrackPointersInHeaderInfo(trackPtrHeader, curOffset)) {
      return false;
    }
    trackPtrHeader->SetGuessedLength();
  }

  while (true) {
    if (curOffset + 1 > 0x10000) {
      return false;
    }

    uint32_t beginOffset = curOffset;
    uint8_t statusByte = GetByte(curOffset++);

    HudsonSnesSeqHeaderEventType eventType = (HudsonSnesSeqHeaderEventType) 0;
    std::map<uint8_t, HudsonSnesSeqHeaderEventType>::iterator pEventType = HeaderEventMap.find(statusByte);
    if (pEventType != HeaderEventMap.end()) {
      eventType = pEventType->second;
    }

    switch (eventType) {
      case HEADER_EVENT_END: {
        header->AddSimpleItem(beginOffset, curOffset - beginOffset, L"Header End");
        goto header_end;
      }

      case HEADER_EVENT_TIMEBASE: {
        VGMHeader *aHeader = header->AddHeader(beginOffset, 1, L"Timebase");
        aHeader->AddSimpleItem(beginOffset, 1, L"Event ID");

        // actual music engine decreases timebase based on the following value.
        // however, we always use SEQ_PPQN and adjust note lengths when necessary instead.
        aHeader->AddSimpleItem(curOffset, 1, L"Timebase");
        TimebaseShift = GetByte(curOffset++) & 3;

        aHeader->unLength = curOffset - beginOffset;
        break;
      }

      case HEADER_EVENT_TRACKS: {
        VGMHeader *trackPtrHeader = header->AddHeader(beginOffset, 0, L"Track Pointers");
        trackPtrHeader->AddSimpleItem(beginOffset, 1, L"Event ID");
        if (!GetTrackPointersInHeaderInfo(trackPtrHeader, curOffset)) {
          return false;
        }
        trackPtrHeader->unLength = curOffset - beginOffset;
        break;
      }

      case HEADER_EVENT_INSTRUMENTS_V1: {
        VGMHeader *instrHeader = header->AddHeader(beginOffset, 1, L"Instruments");
        instrHeader->AddSimpleItem(beginOffset, 1, L"Event ID");

        instrHeader->AddSimpleItem(curOffset, 1, L"Size");
        uint8_t tableSize = GetByte(curOffset++);
        if (curOffset + tableSize > 0x10000) {
          return false;
        }

        InstrumentTableAddress = curOffset;
        InstrumentTableSize = tableSize;

        // instrument table will be parsed by HudsonSnesInstr, too
        uint8_t tableLength = tableSize / 4;
        for (uint8_t instrNum = 0; instrNum < tableLength; instrNum++) {
          uint16_t addrInstrItem = InstrumentTableAddress + instrNum * 4;
          std::wstringstream instrName;
          instrName << L"Instrument " << instrNum;
          VGMHeader *aInstrHeader = instrHeader->AddHeader(addrInstrItem, 4, instrName.str().c_str());
          aInstrHeader->AddSimpleItem(addrInstrItem, 1, L"SRCN");
          aInstrHeader->AddSimpleItem(addrInstrItem + 1, 1, L"ADSR(1)");
          aInstrHeader->AddSimpleItem(addrInstrItem + 2, 1, L"ADSR(2)");
          aInstrHeader->AddSimpleItem(addrInstrItem + 3, 1, L"GAIN");
        }
        curOffset += tableSize;

        instrHeader->unLength = curOffset - beginOffset;
        break;
      }

      case HEADER_EVENT_PERCUSSIONS_V1: {
        VGMHeader *percHeader = header->AddHeader(beginOffset, 1, L"Percussions");
        percHeader->AddSimpleItem(beginOffset, 1, L"Event ID");

        percHeader->AddSimpleItem(curOffset, 1, L"Size");
        uint8_t tableSize = GetByte(curOffset++);
        if (curOffset + tableSize > 0x10000) {
          return false;
        }

        PercussionTableAddress = curOffset;
        PercussionTableSize = tableSize;

        uint8_t tableLength = tableSize / 4;
        for (uint8_t percNote = 0; percNote < tableLength; percNote++) {
          uint16_t addrPercItem = PercussionTableAddress + percNote * 4;
          std::wstringstream percNoteName;
          percNoteName << L"Percussion " << percNote;
          VGMHeader *percNoteHeader = percHeader->AddHeader(addrPercItem, 4, percNoteName.str().c_str());
          percNoteHeader->AddSimpleItem(addrPercItem, 1, L"Instrument");
          percNoteHeader->AddSimpleItem(addrPercItem + 1, 1, L"Unity Key");
          percNoteHeader->AddSimpleItem(addrPercItem + 2, 1, L"Volume");
          percNoteHeader->AddSimpleItem(addrPercItem + 3, 1, L"Pan");
        }
        curOffset += tableSize;

        percHeader->unLength = curOffset - beginOffset;
        break;
      }

      case HEADER_EVENT_INSTRUMENTS_V2: {
        VGMHeader *instrHeader = header->AddHeader(beginOffset, 1, L"Instruments");
        instrHeader->AddSimpleItem(beginOffset, 1, L"Event ID");

        instrHeader->AddSimpleItem(curOffset, 1, L"Number of Items");
        uint8_t tableSize = GetByte(curOffset++) * 4;
        if (curOffset + tableSize > 0x10000) {
          return false;
        }

        InstrumentTableAddress = curOffset;
        InstrumentTableSize = tableSize;

        // instrument table will be parsed by HudsonSnesInstr, too
        uint8_t tableLength = tableSize / 4;
        for (uint8_t instrNum = 0; instrNum < tableLength; instrNum++) {
          uint16_t addrInstrItem = InstrumentTableAddress + instrNum * 4;
          std::wstringstream instrName;
          instrName << L"Instrument " << instrNum;
          VGMHeader *aInstrHeader = instrHeader->AddHeader(addrInstrItem, 4, instrName.str().c_str());
          aInstrHeader->AddSimpleItem(addrInstrItem, 1, L"SRCN");
          aInstrHeader->AddSimpleItem(addrInstrItem + 1, 1, L"ADSR(1)");
          aInstrHeader->AddSimpleItem(addrInstrItem + 2, 1, L"ADSR(2)");
          aInstrHeader->AddSimpleItem(addrInstrItem + 3, 1, L"GAIN");
        }
        curOffset += tableSize;

        instrHeader->unLength = curOffset - beginOffset;
        break;
      }

      case HEADER_EVENT_PERCUSSIONS_V2: {
        VGMHeader *percHeader = header->AddHeader(beginOffset, 1, L"Percussions");
        percHeader->AddSimpleItem(beginOffset, 1, L"Event ID");

        percHeader->AddSimpleItem(curOffset, 1, L"Number of Items");
        uint8_t tableSize = GetByte(curOffset++) * 4;
        if (curOffset + tableSize > 0x10000) {
          return false;
        }

        PercussionTableAddress = curOffset;
        PercussionTableSize = tableSize;

        uint8_t tableLength = tableSize / 4;
        for (uint8_t percNote = 0; percNote < tableLength; percNote++) {
          uint16_t addrPercItem = PercussionTableAddress + percNote * 4;
          std::wstringstream percNoteName;
          percNoteName << L"Percussion " << percNote;
          VGMHeader *percNoteHeader = percHeader->AddHeader(addrPercItem, 4, percNoteName.str().c_str());
          percNoteHeader->AddSimpleItem(addrPercItem, 1, L"Instrument");
          percNoteHeader->AddSimpleItem(addrPercItem + 1, 1, L"Unity Key");
          percNoteHeader->AddSimpleItem(addrPercItem + 2, 1, L"Volume");
          percNoteHeader->AddSimpleItem(addrPercItem + 3, 1, L"Pan");
        }
        curOffset += tableSize;

        percHeader->unLength = curOffset - beginOffset;
        break;
      }

      case HEADER_EVENT_05: {
        VGMHeader *aHeader = header->AddHeader(beginOffset, 1, L"Unknown");
        aHeader->AddSimpleItem(beginOffset, 1, L"Event ID");

        aHeader->AddSimpleItem(curOffset, 1, L"Number of Items");
        uint8_t tableSize = GetByte(curOffset++) * 2;
        if (curOffset + tableSize > 0x10000) {
          return false;
        }

        aHeader->AddUnknownItem(curOffset, tableSize);
        curOffset += tableSize;

        aHeader->unLength = curOffset - beginOffset;
        break;
      }

      case HEADER_EVENT_06: {
        VGMHeader *aHeader = header->AddHeader(beginOffset, 1, L"Unknown");
        aHeader->AddSimpleItem(beginOffset, 1, L"Event ID");

        aHeader->AddSimpleItem(curOffset, 1, L"Number of Items");
        uint8_t tableSize = GetByte(curOffset++) * 2;
        if (curOffset + tableSize > 0x10000) {
          return false;
        }

        aHeader->AddUnknownItem(curOffset, tableSize);
        curOffset += tableSize;

        aHeader->unLength = curOffset - beginOffset;
        break;
      }

      case HEADER_EVENT_ECHO_PARAM: {
        VGMHeader *aHeader = header->AddHeader(beginOffset, 1, L"Echo Param");
        aHeader->AddSimpleItem(beginOffset, 1, L"Event ID");

        aHeader->AddSimpleItem(curOffset, 1, L"Use Default Echo");
        uint8_t useDefaultEcho = GetByte(curOffset++);

        if (useDefaultEcho == 0) {
          aHeader->AddSimpleItem(curOffset, 1, L"EVOL(L)");
          curOffset++;
          aHeader->AddSimpleItem(curOffset, 1, L"EVOL(R)");
          curOffset++;
          aHeader->AddSimpleItem(curOffset, 1, L"EDL");
          curOffset++;
          aHeader->AddSimpleItem(curOffset, 1, L"EFB");
          curOffset++;
		  aHeader->AddSimpleItem(curOffset, 1, L"FIR");
          curOffset++;
          aHeader->AddSimpleItem(curOffset, 1, L"EON");
          curOffset++;
        }

        aHeader->unLength = curOffset - beginOffset;
        break;
      }

      case HEADER_EVENT_08: {
        VGMHeader *aHeader = header->AddHeader(beginOffset, 1, L"Unknown");
        aHeader->AddSimpleItem(beginOffset, 1, L"Event ID");

        aHeader->AddUnknownItem(curOffset, 1);
        uint8_t arg1 = GetByte(curOffset++);

        aHeader->unLength = curOffset - beginOffset;
        break;
      }

      case HEADER_EVENT_09: {
        VGMHeader *aHeader = header->AddHeader(beginOffset, 1, L"Unknown");
        aHeader->AddSimpleItem(beginOffset, 1, L"Event ID");

        aHeader->AddSimpleItem(curOffset, 1, L"Number of Items");
        uint8_t tableSize = GetByte(curOffset++) * 2;
        if (curOffset + tableSize > 0x10000) {
          return false;
        }

        aHeader->AddUnknownItem(curOffset, tableSize);
        curOffset += tableSize;

        aHeader->unLength = curOffset - beginOffset;
        break;
      }

      default:
        header->AddUnknownItem(beginOffset, curOffset - beginOffset);
        goto header_end;
    }
  }

  header_end:
  header->SetGuessedLength();

  return true;
}

bool HudsonSnesSeq::GetTrackPointersInHeaderInfo(VGMHeader *header, uint32_t &offset) {
  uint32_t beginOffset = offset;
  uint32_t curOffset = beginOffset;

  if (curOffset + 1 > 0x10000) {
    return false;
  }

  // flag field that indicates track availability
  header->AddSimpleItem(curOffset, 1, L"Track Availability");
  TrackAvailableBits = GetByte(curOffset++);

  // read track addresses (DSP channel 8 to 1)
  for (int trackIndex = 0; trackIndex < MAX_TRACKS; trackIndex++) {
    if ((TrackAvailableBits & (1 << trackIndex)) != 0) {
      if (curOffset + 2 > 0x10000) {
        offset = curOffset;
        return false;
      }

      std::wstringstream trackName;
      trackName << L"Track Pointer " << (trackIndex + 1);
      header->AddSimpleItem(curOffset, 2, trackName.str().c_str());
      TrackAddresses[trackIndex] = GetShort(curOffset);
      curOffset += 2;
    }
  }

  offset = curOffset;
  return true;
}

bool HudsonSnesSeq::GetTrackPointers(void) {
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
    HeaderEventMap[0x08] = HEADER_EVENT_08;
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
    SubEventMap[0x0c] = SUBEVENT_UNKNOWN0;
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

HudsonSnesTrack::HudsonSnesTrack(HudsonSnesSeq *parentFile, long offset, long length)
    : SeqTrack(parentFile, offset, length) {
  ResetVars();
  bDetermineTrackLengthEventByEvent = true;
  bWriteGenericEventAsTextEvent = false;
}

void HudsonSnesTrack::ResetVars(void) {
  SeqTrack::ResetVars();

  vel = 100;
  octave = 2;
  prevNoteKey = -1;
  prevNoteSlurred = false;
  infiniteLoopPoint = dwStartOffset;
  loopPointOnceProcessed = false;
  spcNoteQuantize = 8;
  spcVolume = 100;
  spcCallStackPtr = 0;
}


bool HudsonSnesTrack::ReadEvent(void) {
  HudsonSnesSeq *parentSeq = (HudsonSnesSeq *) this->parentSeq;

  uint32_t beginOffset = curOffset;
  if (curOffset >= 0x10000) {
    return false;
  }

  uint8_t statusByte = GetByte(curOffset++);
  bool bContinue = true;

  std::wstringstream desc;

  HudsonSnesSeqEventType eventType = (HudsonSnesSeqEventType) 0;
  std::map<uint8_t, HudsonSnesSeqEventType>::iterator pEventType = parentSeq->EventMap.find(statusByte);
  if (pEventType != parentSeq->EventMap.end()) {
    eventType = pEventType->second;
  }

  switch (eventType) {
    case EVENT_UNKNOWN0:
      desc << L"Event: 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << (int) statusByte;
      AddUnknown(beginOffset, curOffset - beginOffset, L"Unknown Event", desc.str().c_str());
      break;

    case EVENT_UNKNOWN1: {
      uint8_t arg1 = GetByte(curOffset++);
      desc << L"Event: 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << (int) statusByte
          << std::dec << std::setfill(L' ') << std::setw(0)
          << L"  Arg1: " << (int) arg1;
      AddUnknown(beginOffset, curOffset - beginOffset, L"Unknown Event", desc.str().c_str());
      break;
    }

    case EVENT_UNKNOWN2: {
      uint8_t arg1 = GetByte(curOffset++);
      uint8_t arg2 = GetByte(curOffset++);
      desc << L"Event: 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << (int) statusByte
          << std::dec << std::setfill(L' ') << std::setw(0)
          << L"  Arg1: " << (int) arg1
          << L"  Arg2: " << (int) arg2;
      AddUnknown(beginOffset, curOffset - beginOffset, L"Unknown Event", desc.str().c_str());
      break;
    }

    case EVENT_UNKNOWN3: {
      uint8_t arg1 = GetByte(curOffset++);
      uint8_t arg2 = GetByte(curOffset++);
      uint8_t arg3 = GetByte(curOffset++);
      desc << L"Event: 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << (int) statusByte
          << std::dec << std::setfill(L' ') << std::setw(0)
          << L"  Arg1: " << (int) arg1
          << L"  Arg2: " << (int) arg2
          << L"  Arg3: " << (int) arg3;
      AddUnknown(beginOffset, curOffset - beginOffset, L"Unknown Event", desc.str().c_str());
      break;
    }

    case EVENT_UNKNOWN4: {
      uint8_t arg1 = GetByte(curOffset++);
      uint8_t arg2 = GetByte(curOffset++);
      uint8_t arg3 = GetByte(curOffset++);
      uint8_t arg4 = GetByte(curOffset++);
      desc << L"Event: 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << (int) statusByte
          << std::dec << std::setfill(L' ') << std::setw(0)
          << L"  Arg1: " << (int) arg1
          << L"  Arg2: " << (int) arg2
          << L"  Arg3: " << (int) arg3
          << L"  Arg4: " << (int) arg4;
      AddUnknown(beginOffset, curOffset - beginOffset, L"Unknown Event", desc.str().c_str());
      break;
    }

    case EVENT_NOP: {
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"NOP", desc.str().c_str(), CLR_MISC, ICON_BINARY);
      break;
    }

    case EVENT_NOP1: {
      curOffset++;
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"NOP", desc.str().c_str(), CLR_MISC, ICON_BINARY);
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
        len = GetByte(curOffset++);

        // adjust note length to fit to SEQ_PPQN
        len <<= parentSeq->TimebaseShift;
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
        AddRest(beginOffset, curOffset - beginOffset, len);
        prevNoteSlurred = false;
      }
      else {
        int8_t key = (octave * 12) + (keyIndex - 1);
        if (prevNoteSlurred && key == prevNoteKey) {
          // tie
          MakePrevDurNoteEnd(GetTime() + dur);
          AddGenericEvent(beginOffset, curOffset - beginOffset, L"Tie", desc.str().c_str(), CLR_TIE, ICON_NOTE);
        }
        else {
          // note
          AddNoteByDur(beginOffset, curOffset - beginOffset, key, vel, dur);
          prevNoteKey = key;
        }
        prevNoteSlurred = noKeyoff;
        AddTime(len);
      }

      break;
    }

    case EVENT_TEMPO: {
      uint8_t newTempo = GetByte(curOffset++);
      AddTempoBPM(beginOffset, curOffset - beginOffset, newTempo);
      break;
    }

    case EVENT_OCTAVE: {
      uint8_t newOctave = GetByte(curOffset++);
      AddSetOctave(beginOffset, curOffset - beginOffset, newOctave);
      break;
    }

    case EVENT_OCTAVE_UP: {
      AddIncrementOctave(beginOffset, curOffset - beginOffset);
      break;
    }

    case EVENT_OCTAVE_DOWN: {
      AddDecrementOctave(beginOffset, curOffset - beginOffset);
      break;
    }

    case EVENT_QUANTIZE: {
      uint8_t newQuantize = GetByte(curOffset++);
	  spcNoteQuantize = newQuantize;
      if (newQuantize <= 8) {
        desc << L"Length: " << (int) newQuantize << L"/8";
      }
      else {
        desc << L"Length: " << L"Full-Length - " << (int) (newQuantize - 8);
      }
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Duration Rate", desc.str().c_str(), CLR_DURNOTE);
      break;
    }

    case EVENT_PROGCHANGE: {
      uint8_t newProg = GetByte(curOffset++);
      AddProgramChange(beginOffset, curOffset - beginOffset, newProg);
      break;
    }

    case EVENT_VOLUME: {
      uint8_t vol = GetByte(curOffset++);
      spcVolume = vol;
      AddVol(beginOffset, curOffset - beginOffset, spcVolume >> 1);
      break;
    }

    case EVENT_PAN: {
      const uint8_t PAN_TABLE[31] = {
          0x00, 0x07, 0x0d, 0x14, 0x1a, 0x21, 0x27, 0x2e,
          0x34, 0x3a, 0x40, 0x45, 0x4b, 0x50, 0x55, 0x5a,
          0x5e, 0x63, 0x67, 0x6b, 0x6e, 0x71, 0x74, 0x77,
          0x79, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f, 0x7f,
      };

      uint8_t panIndex = GetByte(curOffset++) & 0x1f;
      if (panIndex > 30) {
        // out of range
        panIndex = 30;
      }

      // TODO: calculate accurate pan value
      // Left Volume  = PAN_TABLE[panIndex];
      // Right Volume = PAN_TABLE[30 - panIndex];
      AddPan(beginOffset, curOffset - beginOffset, PAN_TABLE[30 - panIndex]);

      break;
    }

    case EVENT_REVERSE_PHASE: {
      uint8_t reverse = GetByte(curOffset++);
      bool reverseLeft = (reverse & 2) != 0;
      bool reverseRight = (reverse & 1) != 0;
      desc << L"Reverse Left: " << (reverseLeft ? L"On" : L"Off") << L"Reverse Right: "
          << (reverseRight ? L"On" : L"Off");
      AddGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      L"Reverse Phase",
                      desc.str().c_str(),
                      CLR_CHANGESTATE,
                      ICON_CONTROL);
      break;
    }

    case EVENT_VOLUME_REL: {
      int8_t delta = GetByte(curOffset++);
      int newVolume = spcVolume + delta;
      if (newVolume < 0) {
        newVolume = 0;
      }
      else if (newVolume > 0xff) {
        newVolume = 0xff;
      }
      spcVolume = (uint8_t) newVolume;
      AddVol(beginOffset, curOffset - beginOffset, spcVolume >> 1, L"Volume (Relative)");
      break;
    }

    case EVENT_LOOP_START: {
      uint8_t count = GetByte(curOffset);
      desc << L"Loop Count: " << (int) count;
      AddGenericEvent(beginOffset, 2, L"Loop Start", desc.str().c_str(), CLR_LOOP, ICON_STARTREP);

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
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Loop End", desc.str().c_str(), CLR_LOOP, ICON_ENDREP);

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
      uint16_t dest = GetShort(curOffset);
      desc << "Destination: $" << std::hex << std::setfill(L'0') << std::setw(4) << std::uppercase << (int) dest;
      AddGenericEvent(beginOffset, 3, L"Pattern Play", desc.str().c_str(), CLR_LOOP, ICON_STARTREP);

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
      uint16_t dest = GetShort(curOffset);
      curOffset += 2;
      desc << L"Destination: $" << std::hex << std::setfill(L'0') << std::setw(4) << std::uppercase << (int) dest;
      uint32_t length = curOffset - beginOffset;

      curOffset = dest;
      if (!IsOffsetUsed(dest)) {
        AddGenericEvent(beginOffset, length, L"Jump", desc.str().c_str(), CLR_LOOPFOREVER);
      }
      else {
        bContinue = AddLoopForever(beginOffset, length, L"Jump");
      }
      break;
    }

    case EVENT_TUNING: {
      int8_t newTuning = GetByte(curOffset++);
      double semitones = newTuning / 256.0;
      AddFineTuning(beginOffset, curOffset - beginOffset, semitones * 100.0);
      break;
    }

    case EVENT_VIBRATO: {
      if (parentSeq->version == HUDSONSNES_V0 || parentSeq->version == HUDSONSNES_V1) {
        uint8_t lfoRate = GetByte(curOffset++);
        uint8_t lfoDepth = GetByte(curOffset++);
        desc << L"Rate: " << (int) lfoRate << L"  Depth: " << (int) lfoDepth;
        AddGenericEvent(beginOffset,
                        curOffset - beginOffset,
                        L"Vibrato",
                        desc.str().c_str(),
                        CLR_MODULATION,
                        ICON_CONTROL);
      }
      else { // HUDSONSNES_V2
        uint8_t arg1 = GetByte(curOffset++);
        uint8_t arg2 = GetByte(curOffset++);
        uint8_t arg3 = GetByte(curOffset++);
        desc << L"Arg1: " << (int) arg1 << L"  Arg2: " << (int) arg2 << L"  Arg3: " << (int) arg3;
        AddGenericEvent(beginOffset,
                        curOffset - beginOffset,
                        L"Vibrato",
                        desc.str().c_str(),
                        CLR_MODULATION,
                        ICON_CONTROL);
      }
      break;
    }

    case EVENT_VIBRATO_DELAY: {
      uint8_t lfoDelay = GetByte(curOffset++);
      desc << L"Delay: " << (int) lfoDelay;
      AddGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      L"Vibrato Delay",
                      desc.str().c_str(),
                      CLR_MODULATION,
                      ICON_CONTROL);
      break;
    }

    case EVENT_ECHO_VOLUME: {
      int8_t volumeLeft = GetByte(curOffset++);
      int8_t volumeRight = GetByte(curOffset++);
      desc << L"Left Volume: " << (int) volumeLeft << L"  Right Volume: " << (int) volumeRight;
      AddGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      L"Echo Volume",
                      desc.str().c_str(),
                      CLR_REVERB,
                      ICON_CONTROL);
      break;
    }

    case EVENT_ECHO_PARAM: {
      uint8_t delay = GetByte(curOffset++);
      uint8_t feedback = GetByte(curOffset++);
      uint8_t filterIndex = GetByte(curOffset++);
      desc << L"Echo Delay: " << (int) delay << L"  Echo Feedback: " << (int) feedback << L"  FIR: "
          << (int) filterIndex;
      AddGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      L"Echo Parameter",
                      desc.str().c_str(),
                      CLR_REVERB,
                      ICON_CONTROL);
      break;
    }

    case EVENT_ECHO_ON: {
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Echo On", desc.str().c_str(), CLR_REVERB, ICON_CONTROL);
      break;
    }

    case EVENT_TRANSPOSE_ABS: {
      int8_t newTranspose = GetByte(curOffset++);
      AddTranspose(beginOffset, curOffset - beginOffset, newTranspose);
      break;
    }

    case EVENT_TRANSPOSE_REL: {
      int8_t delta = GetByte(curOffset++);
      AddTranspose(beginOffset, curOffset - beginOffset, transpose + delta, L"Transpose (Relative)");
      break;
    }

    case EVENT_PITCH_ATTACK_ENV_ON: {
      uint8_t speed = GetByte(curOffset++);
      uint8_t depth = GetByte(curOffset++);
      uint8_t direction = GetByte(curOffset++);
      bool upTo = (direction != 0);
      desc << L"Speed: " << (int) speed << L"  Depth: " << (int) depth << L"  Direction: " << (upTo ? L"Up" : L"Down");
      AddGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      L"Attack Pitch Envelope On",
                      desc.str().c_str(),
                      CLR_PITCHBEND,
                      ICON_CONTROL);
      break;
    }

    case EVENT_PITCH_ATTACK_ENV_OFF: {
      AddGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      L"Attack Pitch Envelope Off",
                      desc.str().c_str(),
                      CLR_PITCHBEND,
                      ICON_CONTROL);
      break;
    }

    case EVENT_LOOP_POINT: {
      infiniteLoopPoint = curOffset;
      AddGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      L"Infinite Loop Point",
                      desc.str().c_str(),
                      CLR_LOOP,
                      ICON_STARTREP);
      break;
    }

    case EVENT_JUMP_TO_LOOP_POINT: {
      bContinue = AddLoopForever(beginOffset, curOffset - beginOffset);
      curOffset = infiniteLoopPoint;
      break;
    }

    case EVENT_LOOP_POINT_ONCE: {
      AddGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      L"Infinite Loop Point (Ignore after the Second Time)",
                      desc.str().c_str(),
                      CLR_LOOP,
                      ICON_STARTREP);
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

      uint8_t volIndex = GetByte(curOffset++);
      if (volIndex > 90) {
        // out of range
        volIndex = 90;
      }

      spcVolume = VOLUME_TABLE[volIndex];
      AddVol(beginOffset, curOffset - beginOffset, spcVolume >> 1, L"Volume From Table");
      break;
    }

    case EVENT_PORTAMENTO: {
      uint8_t portamentoTime = GetByte(curOffset++);
      uint8_t arg2 = GetByte(curOffset++);

      if (portamentoTime == 0) {
        AddPortamento(beginOffset, curOffset - beginOffset, false);
        AddPortamentoTimeNoItem(0);
      }
      else {
        AddPortamento(beginOffset, curOffset - beginOffset, true);
        AddPortamentoTimeNoItem(portamentoTime);
      }

      break;
    }

    case EVENT_END: {
      if (spcCallStackPtr == 0) {
        // end of track
        AddEndOfTrack(beginOffset, curOffset - beginOffset);
        bContinue = false;
      }
      else {
        // end subroutine
        AddGenericEvent(beginOffset,
                        curOffset - beginOffset,
                        L"End Pattern",
                        desc.str().c_str(),
                        CLR_LOOP,
                        ICON_ENDREP);

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
      uint8_t subStatusByte = GetByte(curOffset++);
      HudsonSnesSeqSubEventType subEventType = (HudsonSnesSeqSubEventType) 0;
      std::map<uint8_t, HudsonSnesSeqSubEventType>::iterator pSubEventType = parentSeq->SubEventMap.find(subStatusByte);
      if (pSubEventType != parentSeq->SubEventMap.end()) {
        subEventType = pSubEventType->second;
      }

      switch (subEventType) {
        case SUBEVENT_UNKNOWN0:
          desc << L"Subevent: 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase
              << (int) subStatusByte;
          AddUnknown(beginOffset, curOffset - beginOffset, L"Unknown Event", desc.str().c_str());
          break;

        case SUBEVENT_UNKNOWN1: {
          uint8_t arg1 = GetByte(curOffset++);
          desc << L"Subevent: 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase
              << (int) subStatusByte
              << std::dec << std::setfill(L' ') << std::setw(0)
              << L"  Arg1: " << (int) arg1;
          AddUnknown(beginOffset, curOffset - beginOffset, L"Unknown Event", desc.str().c_str());
          break;
        }

        case SUBEVENT_UNKNOWN2: {
          uint8_t arg1 = GetByte(curOffset++);
          uint8_t arg2 = GetByte(curOffset++);
          desc << L"Subevent: 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase
              << (int) subStatusByte
              << std::dec << std::setfill(L' ') << std::setw(0)
              << L"  Arg1: " << (int) arg1
              << L"  Arg2: " << (int) arg2;
          AddUnknown(beginOffset, curOffset - beginOffset, L"Unknown Event", desc.str().c_str());
          break;
        }

        case SUBEVENT_UNKNOWN3: {
          uint8_t arg1 = GetByte(curOffset++);
          uint8_t arg2 = GetByte(curOffset++);
          uint8_t arg3 = GetByte(curOffset++);
          desc << L"Subevent: 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase
              << (int) subStatusByte
              << std::dec << std::setfill(L' ') << std::setw(0)
              << L"  Arg1: " << (int) arg1
              << L"  Arg2: " << (int) arg2
              << L"  Arg3: " << (int) arg3;
          AddUnknown(beginOffset, curOffset - beginOffset, L"Unknown Event", desc.str().c_str());
          break;
        }

        case SUBEVENT_UNKNOWN4: {
          uint8_t arg1 = GetByte(curOffset++);
          uint8_t arg2 = GetByte(curOffset++);
          uint8_t arg3 = GetByte(curOffset++);
          uint8_t arg4 = GetByte(curOffset++);
          desc << L"Subevent: 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase
              << (int) subStatusByte
              << std::dec << std::setfill(L' ') << std::setw(0)
              << L"  Arg1: " << (int) arg1
              << L"  Arg2: " << (int) arg2
              << L"  Arg3: " << (int) arg3
              << L"  Arg4: " << (int) arg4;
          AddUnknown(beginOffset, curOffset - beginOffset, L"Unknown Event", desc.str().c_str());
          break;
        }

        case SUBEVENT_NOP: {
          AddGenericEvent(beginOffset, curOffset - beginOffset, L"NOP", desc.str().c_str(), CLR_MISC, ICON_BINARY);
          break;
        }

        case SUBEVENT_END: {
          AddEndOfTrack(beginOffset, curOffset - beginOffset);
          bContinue = false;
          break;
        }

        case SUBEVENT_ECHO_OFF: {
          AddGenericEvent(beginOffset,
                          curOffset - beginOffset,
                          L"Echo Off",
                          desc.str().c_str(),
                          CLR_REVERB,
                          ICON_CONTROL);
          break;
        }

        case SUBEVENT_PERC_ON: {
          AddGenericEvent(beginOffset,
                          curOffset - beginOffset,
                          L"Percussion On",
                          desc.str().c_str(),
                          CLR_CHANGESTATE,
                          ICON_CONTROL);
          break;
        }

        case SUBEVENT_PERC_OFF: {
          AddGenericEvent(beginOffset,
                          curOffset - beginOffset,
                          L"Percussion Off",
                          desc.str().c_str(),
                          CLR_CHANGESTATE,
                          ICON_CONTROL);
          break;
        }

        case SUBEVENT_VIBRATO_TYPE: {
          uint8_t vibratoType = subStatusByte - 0x05;
          desc << L"Vibrato Type: " << (int) vibratoType;
          AddGenericEvent(beginOffset,
                          curOffset - beginOffset,
                          L"Vibrato Type",
                          desc.str().c_str(),
                          CLR_LFO,
                          ICON_CONTROL);
          break;
        }

        case SUBEVENT_MOV_IMM: {
          uint8_t reg = GetByte(curOffset++);
          uint8_t val = GetByte(curOffset++);
          desc << L"Register: " << (int) reg << L"  Value: " << (int) val;
          AddGenericEvent(beginOffset, curOffset - beginOffset, L"MOV (Immediate)", desc.str().c_str(), CLR_MISC);

          if (reg < HUDSONSNES_USERRAM_SIZE) {
            parentSeq->UserRAM[reg] = val;
            parentSeq->UserCmpReg = val;
          }
          break;
        }

        case SUBEVENT_MOV: {
          uint8_t regDst = GetByte(curOffset++);
          uint8_t regSrc = GetByte(curOffset++);
          desc << L"Destination Register: " << (int) regDst << L"  Source Register: " << (int) regSrc;
          AddGenericEvent(beginOffset, curOffset - beginOffset, L"MOV", desc.str().c_str(), CLR_MISC);

          if (regDst < HUDSONSNES_USERRAM_SIZE && regSrc < HUDSONSNES_USERRAM_SIZE) {
            uint8_t val = parentSeq->UserRAM[regSrc];
            parentSeq->UserRAM[regDst] = val;
            parentSeq->UserCmpReg = val;
          }
          break;
        }

        case SUBEVENT_CMP_IMM: {
          uint8_t reg = GetByte(curOffset++);
          uint8_t val = GetByte(curOffset++);
          desc << L"Register: " << (int) reg << L"  Value: " << (int) val;
          AddGenericEvent(beginOffset, curOffset - beginOffset, L"CMP (Immediate)", desc.str().c_str(), CLR_MISC);

          if (reg < HUDSONSNES_USERRAM_SIZE) {
            parentSeq->UserCmpReg = parentSeq->UserRAM[reg] - val;
            parentSeq->UserCarry = (parentSeq->UserRAM[reg] > val);
          }
          break;
        }

        case SUBEVENT_CMP: {
          uint8_t regDst = GetByte(curOffset++);
          uint8_t regSrc = GetByte(curOffset++);
          desc << L"Destination Register: " << (int) regDst << L"  Source Register: " << (int) regSrc;
          AddGenericEvent(beginOffset, curOffset - beginOffset, L"CMP", desc.str().c_str(), CLR_MISC);

          if (regDst < HUDSONSNES_USERRAM_SIZE && regSrc < HUDSONSNES_USERRAM_SIZE) {
            parentSeq->UserCmpReg = parentSeq->UserRAM[regDst] - parentSeq->UserRAM[regSrc];
            parentSeq->UserCarry = (parentSeq->UserRAM[regDst] > parentSeq->UserRAM[regSrc]);
          }
          break;
        }

        case SUBEVENT_BNE: {
          uint16_t dest = GetShort(curOffset);
          curOffset += 2;
          desc << L"Destination: $" << std::hex << std::setfill(L'0') << std::setw(4) << std::uppercase << (int) dest;
          AddGenericEvent(beginOffset, curOffset - beginOffset, L"BNE", desc.str().c_str(), CLR_MISC);

          if (parentSeq->UserCmpReg != 0) {
            curOffset = dest;
          }

          break;
        }

        case SUBEVENT_BEQ: {
          uint16_t dest = GetShort(curOffset);
          curOffset += 2;
          desc << L"Destination: $" << std::hex << std::setfill(L'0') << std::setw(4) << std::uppercase << (int) dest;
          AddGenericEvent(beginOffset, curOffset - beginOffset, L"BEQ", desc.str().c_str(), CLR_MISC);

          if (parentSeq->UserCmpReg != 0) {
            curOffset = dest;
          }

          break;
        }

        case SUBEVENT_BCS: {
          uint16_t dest = GetShort(curOffset);
          curOffset += 2;
          desc << L"Destination: $" << std::hex << std::setfill(L'0') << std::setw(4) << std::uppercase << (int) dest;
          AddGenericEvent(beginOffset, curOffset - beginOffset, L"BCS", desc.str().c_str(), CLR_MISC);

          if (parentSeq->UserCarry) {
            curOffset = dest;
          }

          break;
        }

        case SUBEVENT_BCC: {
          uint16_t dest = GetShort(curOffset);
          curOffset += 2;
          desc << L"Destination: $" << std::hex << std::setfill(L'0') << std::setw(4) << std::uppercase << (int) dest;
          AddGenericEvent(beginOffset, curOffset - beginOffset, L"BCC", desc.str().c_str(), CLR_MISC);

          if (!parentSeq->UserCarry) {
            curOffset = dest;
          }

          break;
        }

        case SUBEVENT_BMI: {
          uint16_t dest = GetShort(curOffset);
          curOffset += 2;
          desc << L"Destination: $" << std::hex << std::setfill(L'0') << std::setw(4) << std::uppercase << (int) dest;
          AddGenericEvent(beginOffset, curOffset - beginOffset, L"BMI", desc.str().c_str(), CLR_MISC);

          if ((parentSeq->UserCmpReg & 0x80) != 0) {
            curOffset = dest;
          }

          break;
        }

        case SUBEVENT_BPL: {
          uint16_t dest = GetShort(curOffset);
          curOffset += 2;
          desc << L"Destination: $" << std::hex << std::setfill(L'0') << std::setw(4) << std::uppercase << (int) dest;
          AddGenericEvent(beginOffset, curOffset - beginOffset, L"BPL", desc.str().c_str(), CLR_MISC);

          if ((parentSeq->UserCmpReg & 0x80) == 0) {
            curOffset = dest;
          }

          break;
        }

        case SUBEVENT_ADSR_AR: {
          uint8_t newAR = GetByte(curOffset++) & 15;
          desc << L"AR: " << (int) newAR;
          AddGenericEvent(beginOffset,
                          curOffset - beginOffset,
                          L"ADSR Attack Rate",
                          desc.str().c_str(),
                          CLR_ADSR,
                          ICON_CONTROL);
          break;
        }

        case SUBEVENT_ADSR_DR: {
          uint8_t newDR = GetByte(curOffset++) & 7;
          desc << L"DR: " << (int) newDR;
          AddGenericEvent(beginOffset,
                          curOffset - beginOffset,
                          L"ADSR Decay Rate",
                          desc.str().c_str(),
                          CLR_ADSR,
                          ICON_CONTROL);
          break;
        }

        case SUBEVENT_ADSR_SL: {
          uint8_t newSL = GetByte(curOffset++) & 7;
          desc << L"SL: " << (int) newSL;
          AddGenericEvent(beginOffset,
                          curOffset - beginOffset,
                          L"ADSR Sustain Level",
                          desc.str().c_str(),
                          CLR_ADSR,
                          ICON_CONTROL);
          break;
        }

        case SUBEVENT_ADSR_SR: {
          uint8_t newSR = GetByte(curOffset++) & 15;
          desc << L"SR: " << (int) newSR;
          AddGenericEvent(beginOffset,
                          curOffset - beginOffset,
                          L"ADSR Sustain Rate",
                          desc.str().c_str(),
                          CLR_ADSR,
                          ICON_CONTROL);
          break;
        }

        case SUBEVENT_ADSR_RR: {
          uint8_t newSR = GetByte(curOffset++) & 15;
          desc << L"SR: " << (int) newSR;
          AddGenericEvent(beginOffset,
                          curOffset - beginOffset,
                          L"ADSR Release Rate",
                          desc.str().c_str(),
                          CLR_ADSR,
                          ICON_CONTROL);
          break;
        }

        default:
          desc << L"Subevent: 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase
              << (int) subStatusByte;
          AddUnknown(beginOffset, curOffset - beginOffset, L"Unknown Event", desc.str().c_str());
          pRoot->AddLogItem(new LogItem((std::wstring(L"Unknown Event - ") + desc.str()).c_str(),
                                        LOG_LEVEL_ERR,
                                        L"HudsonSnesSeq"));
          bContinue = false;
          break;
      }

      break;
    }

    default:
      desc << L"Event: 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << (int) statusByte;
      AddUnknown(beginOffset, curOffset - beginOffset, L"Unknown Event", desc.str().c_str());
      pRoot->AddLogItem(new LogItem((std::wstring(L"Unknown Event - ") + desc.str()).c_str(),
                                    LOG_LEVEL_ERR,
                                    L"HudsonSnesSeq"));
      bContinue = false;
      break;
  }

  //wostringstream ssTrace;
  //ssTrace << L"" << std::hex << std::setfill(L'0') << std::setw(8) << std::uppercase << beginOffset << L": " << std::setw(2) << (int)statusByte  << L" -> " << std::setw(8) << curOffset << std::endl;
  //OutputDebugString(ssTrace.str().c_str());

  return bContinue;
}
