#include "PrismSnesSeq.h"
#include "ScaleConversion.h"

// TODO: Fix envelope event length

DECLARE_FORMAT(PrismSnes);

static constexpr int kMaxTracks = 24;
static constexpr uint16_t kPpqn = 48;
static constexpr uint8_t kNoteVelocity = 100;

//  ************
//  PrismSnesSeq
//  ************

const uint8_t PrismSnesSeq::PAN_TABLE_1[21] = {
    0x00, 0x01, 0x03, 0x07, 0x0d, 0x15, 0x1e, 0x29,
    0x34, 0x42, 0x51, 0x5e, 0x67, 0x6e, 0x73, 0x77,
    0x7a, 0x7c, 0x7d, 0x7e, 0x7f,
};

const uint8_t PrismSnesSeq::PAN_TABLE_2[21] = {
    0x1e, 0x28, 0x32, 0x3c, 0x46, 0x50, 0x5a, 0x64,
    0x6e, 0x78, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
    0x7f, 0x7f, 0x7f, 0x7f, 0x7f,
};

PrismSnesSeq::PrismSnesSeq(RawFile *file, PrismSnesVersion ver, uint32_t seqdataOffset, std::string newName)
    : VGMSeq(PrismSnesFormat::name, file, seqdataOffset, 0, newName), version(ver),
      envContainer(NULL) {
  bLoadTickByTick = true;
  bAllowDiscontinuousTrackData = true;
  bUseLinearAmplitudeScale = true;

  UseReverb();
  AlwaysWriteInitialReverb(0);
  AlwaysWriteInitialTempo(GetTempoInBPM(0x82));

  LoadEventMap();
}

PrismSnesSeq::~PrismSnesSeq() {
}

void PrismSnesSeq::DemandEnvelopeContainer(uint32_t offset) {
  if (envContainer == NULL) {
    envContainer = AddHeader(offset, 0, "Envelopes");
  }

  if (offset < envContainer->dwOffset) {
    if (envContainer->unLength != 0) {
      envContainer->unLength += envContainer->dwOffset - offset;
    }
    envContainer->dwOffset = offset;
  }
}

void PrismSnesSeq::ResetVars() {
  VGMSeq::ResetVars();

  conditionSwitch = false;
}

bool PrismSnesSeq::GetHeaderInfo() {
  SetPPQN(kPpqn);

  VGMHeader *header = AddHeader(dwOffset, 0);

  uint32_t curOffset = dwOffset;
  for (uint8_t trackIndex = 0; trackIndex < kMaxTracks + 1; trackIndex++) {
    if (curOffset + 1 > 0x10000) {
      return false;
    }

    uint8_t channel = GetByte(curOffset);
    if (channel >= 0x80) {
      header->AddSimpleItem(curOffset, 1, "Header End");
      break;
    }
    if (trackIndex >= kMaxTracks) {
      return false;
    }

    std::stringstream trackName;
    trackName << "Track " << (trackIndex + 1);
    VGMHeader *trackHeader = header->AddHeader(curOffset, 4, trackName.str());

    trackHeader->AddSimpleItem(curOffset, 1, "Logical Channel");
    curOffset++;

    uint8_t a01 = GetByte(curOffset);
    trackHeader->AddSimpleItem(curOffset, 1, "Physical Channel + Flags");
    curOffset++;

    uint16_t addrTrackStart = GetShort(curOffset);
    trackHeader->AddSimpleItem(curOffset, 2, "Track Pointer");
    curOffset += 2;

    PrismSnesTrack *track = new PrismSnesTrack(this, addrTrackStart);
    aTracks.push_back(track);
  }

  return true;
}

bool PrismSnesSeq::GetTrackPointers(void) {
  return true;
}

void PrismSnesSeq::LoadEventMap() {
  int statusByte;

  for (statusByte = 0x00; statusByte <= 0x7f; statusByte++) {
    EventMap[statusByte] = EVENT_NOTE;
  }

  for (statusByte = 0x80; statusByte <= 0x9f; statusByte++) {
    EventMap[statusByte] = EVENT_NOISE_NOTE;
  }

  EventMap[0xc0] = EVENT_TEMPO;
  EventMap[0xc1] = EVENT_TEMPO;
  EventMap[0xc2] = EVENT_TEMPO;
  EventMap[0xc3] = EVENT_TEMPO;
  EventMap[0xc4] = EVENT_TEMPO;
  EventMap[0xc5] = EVENT_CONDITIONAL_JUMP;
  EventMap[0xc6] = EVENT_CONDITION;
  EventMap[0xc7] = EVENT_UNKNOWN1;
  EventMap[0xc8] = EVENT_UNKNOWN2;
  EventMap[0xc9] = EVENT_UNKNOWN2;
  EventMap[0xca] = EVENT_RESTORE_ECHO_PARAM;
  EventMap[0xcb] = EVENT_SAVE_ECHO_PARAM;
  EventMap[0xcc] = EVENT_UNKNOWN1;
  EventMap[0xcd] = EVENT_SLUR_OFF;
  EventMap[0xce] = EVENT_SLUR_ON;
  EventMap[0xcf] = EVENT_VOLUME_ENVELOPE;
  EventMap[0xd0] = EVENT_DEFAULT_PAN_TABLE_1;
  EventMap[0xd1] = EVENT_DEFAULT_PAN_TABLE_2;
  EventMap[0xd2] = EVENT_UNKNOWN0;
  EventMap[0xd3] = EVENT_INC_APU_PORT_3;
  EventMap[0xd4] = EVENT_INC_APU_PORT_2;
  EventMap[0xd5] = EVENT_PLAY_SONG_3;
  EventMap[0xd6] = EVENT_PLAY_SONG_2;
  EventMap[0xd7] = EVENT_PLAY_SONG_1;
  EventMap[0xd8] = EVENT_TRANSPOSE_REL;
  EventMap[0xd9] = EVENT_PAN_ENVELOPE;
  EventMap[0xda] = EVENT_PAN_TABLE;
  EventMap[0xdb] = EVENT_NOP2;
  EventMap[0xdc] = EVENT_DEFAULT_LENGTH_OFF;
  EventMap[0xdd] = EVENT_DEFAULT_LENGTH;
  EventMap[0xde] = EVENT_LOOP_UNTIL;
  EventMap[0xdf] = EVENT_LOOP_UNTIL_ALT;
  EventMap[0xe0] = EVENT_RET;
  EventMap[0xe1] = EVENT_CALL;
  EventMap[0xe2] = EVENT_GOTO;
  EventMap[0xe3] = EVENT_TRANSPOSE;
  EventMap[0xe4] = EVENT_TUNING;
  EventMap[0xe5] = EVENT_VIBRATO_DELAY;
  EventMap[0xe6] = EVENT_VIBRATO_OFF;
  EventMap[0xe7] = EVENT_VIBRATO;
  EventMap[0xe8] = EVENT_UNKNOWN1;
  EventMap[0xe9] = EVENT_PITCH_SLIDE;
  EventMap[0xea] = EVENT_VOLUME_REL;
  EventMap[0xeb] = EVENT_PAN;
  EventMap[0xec] = EVENT_VOLUME;
  EventMap[0xed] = EVENT_UNKNOWN_EVENT_ED;
  EventMap[0xee] = EVENT_REST;
  EventMap[0xef] = EVENT_GAIN_ENVELOPE_REST;
  EventMap[0xf0] = EVENT_GAIN_ENVELOPE_DECAY_TIME;
  EventMap[0xf1] = EVENT_MANUAL_DURATION_OFF;
  EventMap[0xf2] = EVENT_MANUAL_DURATION_ON;
  EventMap[0xf3] = EVENT_AUTO_DURATION_THRESHOLD;
  EventMap[0xf4] = EVENT_TIE_WITH_DUR;
  EventMap[0xf5] = EVENT_TIE;
  EventMap[0xf6] = EVENT_GAIN_ENVELOPE_SUSTAIN;
  EventMap[0xf7] = EVENT_ECHO_VOLUME_ENVELOPE;
  EventMap[0xf8] = EVENT_ECHO_VOLUME;
  EventMap[0xf9] = EVENT_ECHO_OFF;
  EventMap[0xfa] = EVENT_ECHO_ON;
  EventMap[0xfb] = EVENT_ECHO_PARAM;
  EventMap[0xfc] = EVENT_ADSR;
  EventMap[0xfd] = EVENT_GAIN_ENVELOPE_DECAY;
  EventMap[0xfe] = EVENT_INSTRUMENT;
  EventMap[0xff] = EVENT_END;

  if (version == PRISMSNES_CGV) {
    EventMap[0xc0] = EventMap[0xd0];
    EventMap[0xc1] = EventMap[0xd0];
    EventMap[0xc2] = EventMap[0xd0];
    EventMap[0xc3] = EventMap[0xd0];
    EventMap[0xc4] = EventMap[0xd0];
    EventMap[0xc5] = EventMap[0xd0];
    EventMap[0xc6] = EventMap[0xd0];
    EventMap[0xc7] = EventMap[0xd0];
    EventMap[0xc8] = EventMap[0xd0];
    EventMap[0xc9] = EventMap[0xd0];
    EventMap[0xca] = EventMap[0xd0];
    EventMap[0xcb] = EventMap[0xd0];
    EventMap[0xcc] = EventMap[0xd0];
    EventMap[0xcd] = EventMap[0xd0];
    EventMap[0xce] = EventMap[0xd0];
    EventMap[0xcf] = EventMap[0xd0];
    EventMap[0xdb] = EVENT_UNKNOWN2;
  }
  else if (version == PRISMSNES_DO) {
    EventMap[0xc0] = EventMap[0xc5];
    EventMap[0xc1] = EventMap[0xc5];
    EventMap[0xc2] = EventMap[0xc5];
    EventMap[0xc3] = EventMap[0xc5];
    EventMap[0xc4] = EventMap[0xc5];
  }
}

double PrismSnesSeq::GetTempoInBPM(uint8_t tempo) {
  if (tempo != 0) {
    return 60000000.0 / (kPpqn * (125 * tempo));
  }
  else {
    return 1.0; // since tempo 0 cannot be expressed, this function returns a very small value.
  }
}

//  **************
//  PrismSnesTrack
//  **************

PrismSnesTrack::PrismSnesTrack(PrismSnesSeq *parentFile, uint32_t offset, uint32_t length)
    : SeqTrack(parentFile, offset, length) {
  ResetVars();
  bDetermineTrackLengthEventByEvent = true;
  bWriteGenericEventAsTextEvent = false;
}

void PrismSnesTrack::ResetVars() {
  SeqTrack::ResetVars();

  panTable.assign(std::begin(PrismSnesSeq::PAN_TABLE_1), std::end(PrismSnesSeq::PAN_TABLE_1));

  defaultLength = 0;
  slur = false;
  manualDuration = false;
  prevNoteSlurred = false;
  prevNoteKey = -1;
  spcVolume = 0;
  loopCount = 0;
  loopCountAlt = 0;
  subReturnAddr = 0;
}

bool PrismSnesTrack::ReadEvent() {
  PrismSnesSeq *parentSeq = (PrismSnesSeq *) this->parentSeq;

  uint32_t beginOffset = curOffset;
  if (curOffset >= 0x10000) {
    return false;
  }

  uint8_t statusByte = GetByte(curOffset++);
  bool bContinue = true;

  std::stringstream desc;

  PrismSnesSeqEventType eventType = (PrismSnesSeqEventType) 0;
  std::map<uint8_t, PrismSnesSeqEventType>::iterator pEventType = parentSeq->EventMap.find(statusByte);
  if (pEventType != parentSeq->EventMap.end()) {
    eventType = pEventType->second;
  }

  switch (eventType) {
    case EVENT_UNKNOWN0:
      desc << "Event: 0x" << std::hex << std::setfill('0') << std::setw(2) << std::uppercase << (int) statusByte;
      AddUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc.str());
      break;

    case EVENT_UNKNOWN1: {
      uint8_t arg1 = GetByte(curOffset++);
      desc << "Event: 0x" << std::hex << std::setfill('0') << std::setw(2) << std::uppercase << (int) statusByte
          << std::dec << std::setfill(' ') << std::setw(0)
          << "  Arg1: " << (int) arg1;
      AddUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc.str());
      break;
    }

    case EVENT_UNKNOWN2: {
      uint8_t arg1 = GetByte(curOffset++);
      uint8_t arg2 = GetByte(curOffset++);
      desc << "Event: 0x" << std::hex << std::setfill('0') << std::setw(2) << std::uppercase << (int) statusByte
          << std::dec << std::setfill(' ') << std::setw(0)
          << "  Arg1: " << (int) arg1
          << "  Arg2: " << (int) arg2;
      AddUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc.str());
      break;
    }

    case EVENT_UNKNOWN3: {
      uint8_t arg1 = GetByte(curOffset++);
      uint8_t arg2 = GetByte(curOffset++);
      uint8_t arg3 = GetByte(curOffset++);
      desc << "Event: 0x" << std::hex << std::setfill('0') << std::setw(2) << std::uppercase << (int) statusByte
          << std::dec << std::setfill(' ') << std::setw(0)
          << "  Arg1: " << (int) arg1
          << "  Arg2: " << (int) arg2
          << "  Arg3: " << (int) arg3;
      AddUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc.str());
      break;
    }

    case EVENT_UNKNOWN4: {
      uint8_t arg1 = GetByte(curOffset++);
      uint8_t arg2 = GetByte(curOffset++);
      uint8_t arg3 = GetByte(curOffset++);
      uint8_t arg4 = GetByte(curOffset++);
      desc << "Event: 0x" << std::hex << std::setfill('0') << std::setw(2) << std::uppercase << (int) statusByte
          << std::dec << std::setfill(' ') << std::setw(0)
          << "  Arg1: " << (int) arg1
          << "  Arg2: " << (int) arg2
          << "  Arg3: " << (int) arg3
          << "  Arg4: " << (int) arg4;
      AddUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc.str());
      break;
    }

    case EVENT_NOP2: {
      curOffset += 2;
      AddGenericEvent(beginOffset, curOffset - beginOffset, "NOP", desc.str(), CLR_MISC, ICON_BINARY);
      break;
    }

    case EVENT_NOTE:
    case EVENT_NOISE_NOTE: {
      uint8_t key = statusByte & 0x7f;

      uint8_t len;
      if (!ReadDeltaTime(curOffset, len)) {
        return false;
      }

      uint8_t durDelta;
      if (!ReadDuration(curOffset, len, durDelta)) {
        return false;
      }

      uint8_t dur = GetDuration(curOffset, len, durDelta);

      if (slur) {
        prevNoteSlurred = true;
      }

      if (prevNoteSlurred && key == prevNoteKey) {
        MakePrevDurNoteEnd(GetTime() + dur);
        desc << "Abs Key: " << key << " (" << MidiEvent::GetNoteName(key) << "  Velocity: " << kNoteVelocity << "  Duration: "
            << dur;
        AddGenericEvent(beginOffset, curOffset - beginOffset, "Note (Tied)", desc.str(), CLR_DURNOTE, ICON_NOTE);
      }
      else {
        if (eventType == EVENT_NOISE_NOTE) {
          AddNoteByDur(beginOffset, curOffset - beginOffset, key, kNoteVelocity, dur, "Noise Note");
        }
        else {
          AddNoteByDur(beginOffset, curOffset - beginOffset, key, kNoteVelocity, dur, "Note");
        }
      }
      AddTime(len);

      prevNoteKey = key;
      prevNoteSlurred = false;
      break;
    }

    case EVENT_PITCH_SLIDE: {
      uint8_t noteFrom = GetByte(curOffset++);
      uint8_t noteTo = GetByte(curOffset++);

      uint8_t noteNumberFrom = noteFrom & 0x7f;
      uint8_t noteNumberTo = noteTo & 0x7f;

      uint8_t len;
      if (!ReadDeltaTime(curOffset, len)) {
        return false;
      }

      uint8_t durDelta;
      if (!ReadDuration(curOffset, len, durDelta)) {
        return false;
      }

      uint8_t dur = GetDuration(curOffset, len, durDelta);

      desc << "Note Number (From): " << noteNumberFrom << " ("
          << ((noteFrom & 0x80) != 0 ? "Noise" : MidiEvent::GetNoteName(noteNumberFrom)) << ")" <<
          "  Note Number (To): " << noteNumberTo << " ("
          << ((noteTo & 0x80) != 0 ? "Noise" : MidiEvent::GetNoteName(noteNumberTo)) << ")" <<
          "  Length: " << len << "  Duration: " << dur;
      AddGenericEvent(beginOffset, curOffset - beginOffset, "Pitch Slide", desc.str(), CLR_PITCHBEND, ICON_CONTROL);

      AddNoteByDurNoItem(noteNumberFrom, kNoteVelocity, dur);
      AddTime(len);
      break;
    }

    case EVENT_TIE_WITH_DUR: {
      uint8_t len;
      if (!ReadDeltaTime(curOffset, len)) {
        return false;
      }

      uint8_t durDelta;
      if (!ReadDuration(curOffset, len, durDelta)) {
        return false;
      }

      uint8_t dur = GetDuration(curOffset, len, durDelta);

      desc << "Length: " << len << "  Duration: " << dur;
      AddGenericEvent(beginOffset, curOffset - beginOffset, "Tie with Duration", desc.str(), CLR_TIE, ICON_NOTE);
      MakePrevDurNoteEnd(GetTime() + dur);
      AddTime(len);
      break;
    }

    case EVENT_TIE: {
      prevNoteSlurred = true;
      AddGenericEvent(beginOffset, curOffset - beginOffset, "Tie", desc.str(), CLR_TIE, ICON_NOTE);
      break;
    }

    case EVENT_UNKNOWN_EVENT_ED: {
      uint8_t arg1 = GetByte(curOffset++);
      uint16_t arg2 = GetShort(curOffset);
      curOffset += 2;
      desc << "Arg1: " << arg1 << "  Arg2: $" << std::hex << std::setfill('0') << std::setw(4) << std::uppercase
          << arg2;
      AddUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc.str());
      break;
    }

    case EVENT_REST: {
      uint8_t len;
      if (!ReadDeltaTime(curOffset, len)) {
        return false;
      }

      desc << "Duration: " << len;
      AddGenericEvent(beginOffset, curOffset - beginOffset, "Rest", desc.str(), CLR_REST, ICON_REST);
      AddTime(len);
      break;
    }

    case EVENT_TEMPO: {
      uint8_t newTempo = GetByte(curOffset++);
      AddTempoBPM(beginOffset, curOffset - beginOffset, parentSeq->GetTempoInBPM(newTempo));
      break;
    }

    case EVENT_CONDITIONAL_JUMP: {
      uint16_t dest = GetShort(curOffset);
      curOffset += 2;
      desc << "Destination: $" << std::hex << std::setfill('0') << std::setw(4) << std::uppercase << (int) dest;
      AddGenericEvent(beginOffset, curOffset - beginOffset, "Conditional Jump", desc.str(), CLR_LOOP);

      if (parentSeq->conditionSwitch) {
        curOffset = dest;
      }
      break;
    }

    case EVENT_CONDITION: {
      parentSeq->conditionSwitch = true;
      AddGenericEvent(beginOffset, curOffset - beginOffset, "Condition On", desc.str(), CLR_CHANGESTATE);
      break;
    }

    case EVENT_RESTORE_ECHO_PARAM: {
      AddGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Restore Echo Param",
                      desc.str(),
                      CLR_REVERB,
                      ICON_CONTROL);
      break;
    }

    case EVENT_SAVE_ECHO_PARAM: {
      AddGenericEvent(beginOffset, curOffset - beginOffset, "Save Echo Param", desc.str(), CLR_REVERB, ICON_CONTROL);
      break;
    }

    case EVENT_SLUR_OFF: {
      slur = false;
      AddGenericEvent(beginOffset, curOffset - beginOffset, "Slur Off", desc.str(), CLR_PORTAMENTO, ICON_CONTROL);
      break;
    }

    case EVENT_SLUR_ON: {
      slur = true;
      AddGenericEvent(beginOffset, curOffset - beginOffset, "Slur On", desc.str(), CLR_PORTAMENTO, ICON_CONTROL);
      break;
    }

    case EVENT_VOLUME_ENVELOPE: {
      uint16_t envelopeAddress = GetShort(curOffset);
      curOffset += 2;
      desc << "Envelope: $" << std::hex << std::setfill('0') << std::setw(4) << std::uppercase << envelopeAddress;
      AddGenericEvent(beginOffset, curOffset - beginOffset, "Volume Envelope", desc.str(), CLR_VOLUME, ICON_CONTROL);
      AddVolumeEnvelope(envelopeAddress);
      break;
    }

    case EVENT_DEFAULT_PAN_TABLE_1: {
      panTable.assign(std::begin(PrismSnesSeq::PAN_TABLE_1), std::end(PrismSnesSeq::PAN_TABLE_1));
      AddGenericEvent(beginOffset, curOffset - beginOffset, "Default Pan Table #1", desc.str(), CLR_PAN, ICON_CONTROL);
      break;
    }

    case EVENT_DEFAULT_PAN_TABLE_2: {
      panTable.assign(std::begin(PrismSnesSeq::PAN_TABLE_2), std::end(PrismSnesSeq::PAN_TABLE_2));
      AddGenericEvent(beginOffset, curOffset - beginOffset, "Default Pan Table #2", desc.str(), CLR_PAN, ICON_CONTROL);
      break;
    }

    case EVENT_INC_APU_PORT_3: {
      AddGenericEvent(beginOffset, curOffset - beginOffset, "Increment APU Port 3", desc.str(), CLR_CHANGESTATE);
      break;
    }

    case EVENT_INC_APU_PORT_2: {
      AddGenericEvent(beginOffset, curOffset - beginOffset, "Increment APU Port 2", desc.str(), CLR_CHANGESTATE);
      break;
    }

    case EVENT_PLAY_SONG_3: {
      uint8_t songIndex = GetByte(curOffset++);
      desc << "Song Index: " << songIndex;
      AddGenericEvent(beginOffset, curOffset - beginOffset, "Play Song (3)", desc.str(), CLR_CHANGESTATE);
      break;
    }

    case EVENT_PLAY_SONG_2: {
      uint8_t songIndex = GetByte(curOffset++);
      desc << "Song Index: " << songIndex;
      AddGenericEvent(beginOffset, curOffset - beginOffset, "Play Song (2)", desc.str(), CLR_CHANGESTATE);
      break;
    }

    case EVENT_PLAY_SONG_1: {
      uint8_t songIndex = GetByte(curOffset++);
      desc << "Song Index: " << songIndex;
      AddGenericEvent(beginOffset, curOffset - beginOffset, "Play Song (1)", desc.str(), CLR_CHANGESTATE);
      break;
    }

    case EVENT_TRANSPOSE_REL: {
      int8_t delta = GetByte(curOffset++);
      AddTranspose(beginOffset, curOffset - beginOffset, transpose + delta, "Transpose (Relative)");
      break;
    }

    case EVENT_PAN_ENVELOPE: {
      uint16_t envelopeAddress = GetShort(curOffset);
      curOffset += 2;
      uint16_t envelopeSpeed = GetByte(curOffset++);
      desc << "Envelope: $" << std::hex << std::setfill('0') << std::setw(4) << std::uppercase << envelopeAddress
          << "  Speed: " << std::dec << std::setfill(' ') << std::setw(0) << envelopeSpeed;
      AddGenericEvent(beginOffset, curOffset - beginOffset, "Pan Envelope", desc.str(), CLR_PAN, ICON_CONTROL);
      AddPanEnvelope(envelopeAddress);
      break;
    }

    case EVENT_PAN_TABLE: {
      uint16_t panTableAddress = GetShort(curOffset);
      curOffset += 2;
      desc << "Pan Table: $" << std::hex << std::setfill('0') << std::setw(4) << std::uppercase << panTableAddress;
      AddGenericEvent(beginOffset, curOffset - beginOffset, "Pan Table", desc.str(), CLR_PAN, ICON_CONTROL);

      // update pan table
      if (panTableAddress + 21 <= 0x10000) {
        uint8_t newPanTable[21];
        GetBytes(panTableAddress, 21, newPanTable);
        panTable.assign(std::begin(newPanTable), std::end(newPanTable));
      }

      AddPanTable(panTableAddress);
      break;
    }

    case EVENT_DEFAULT_LENGTH_OFF: {
      defaultLength = 0;
      AddGenericEvent(beginOffset, curOffset - beginOffset, "Default Length Off", desc.str(), CLR_DURNOTE);
      break;
    }

    case EVENT_DEFAULT_LENGTH: {
      defaultLength = GetByte(curOffset++);
      desc << "Duration: " << defaultLength;
      AddGenericEvent(beginOffset, curOffset - beginOffset, "Default Length", desc.str(), CLR_DURNOTE);
      break;
    }

    case EVENT_LOOP_UNTIL: {
      uint8_t count = GetByte(curOffset++);
      uint16_t dest = GetShort(curOffset);
      curOffset += 2;
      desc << "Loop Count: " << count << "  Destination: $" << std::hex << std::setfill('0') << std::setw(4)
          << std::uppercase << (int) dest;
      AddGenericEvent(beginOffset, curOffset - beginOffset, "Loop Until", desc.str(), CLR_LOOP, ICON_ENDREP);

      bool doJump;
      if (loopCount == 0) {
        loopCount = count;
        doJump = true;
      }
      else {
        loopCount--;
        if (loopCount != 0) {
          doJump = true;
        }
        else {
          doJump = false;
        }
      }

      if (doJump) {
        curOffset = dest;
      }

      break;
    }

    case EVENT_LOOP_UNTIL_ALT: {
      uint8_t count = GetByte(curOffset++);
      uint16_t dest = GetShort(curOffset);
      curOffset += 2;
      desc << "Loop Count: " << count << "  Destination: $" << std::hex << std::setfill('0') << std::setw(4)
          << std::uppercase << (int) dest;
      AddGenericEvent(beginOffset, curOffset - beginOffset, "Loop Until (Alt)", desc.str(), CLR_LOOP, ICON_ENDREP);

      bool doJump;
      if (loopCountAlt == 0) {
        loopCountAlt = count;
        doJump = true;
      }
      else {
        loopCountAlt--;
        if (loopCountAlt != 0) {
          doJump = true;
        }
        else {
          doJump = false;
        }
      }

      if (doJump) {
        curOffset = dest;
      }

      break;
    }

    case EVENT_RET: {
      AddGenericEvent(beginOffset, curOffset - beginOffset, "Pattern End", desc.str(), CLR_LOOP, ICON_ENDREP);

      if (subReturnAddr != 0) {
        curOffset = subReturnAddr;
        subReturnAddr = 0;
      }

      break;
    }

    case EVENT_CALL: {
      uint16_t dest = GetShort(curOffset);
      curOffset += 2;
      desc << "Destination: $" << std::hex << std::setfill('0') << std::setw(4) << std::uppercase << (int) dest;
      AddGenericEvent(beginOffset, curOffset - beginOffset, "Pattern Play", desc.str(), CLR_LOOP, ICON_STARTREP);

      subReturnAddr = curOffset;
      curOffset = dest;

      break;
    }

    case EVENT_GOTO: {
      uint16_t dest = GetShort(curOffset);
      curOffset += 2;
      desc << "Destination: $" << std::hex << std::setfill('0') << std::setw(4) << std::uppercase << (int) dest;
      uint32_t length = curOffset - beginOffset;

      if (curOffset < 0x10000 && GetByte(curOffset) == 0xff) {
        AddGenericEvent(curOffset, 1, "End of Track", "", CLR_TRACKEND, ICON_TRACKEND);
      }

      curOffset = dest;
      if (!IsOffsetUsed(dest)) {
        AddGenericEvent(beginOffset, length, "Jump", desc.str(), CLR_LOOPFOREVER);
      }
      else {
        bContinue = AddLoopForever(beginOffset, length, "Jump");
      }

      if (curOffset < dwOffset) {
        dwOffset = curOffset;
      }

      break;
    }

    case EVENT_TRANSPOSE: {
      int8_t newTranspose = GetByte(curOffset++);
      AddTranspose(beginOffset, curOffset - beginOffset, newTranspose);
      break;
    }

    case EVENT_TUNING: {
      uint8_t newTuning = GetByte(curOffset++);
      double semitones = newTuning / 256.0;
      AddFineTuning(beginOffset, curOffset - beginOffset, semitones * 100.0);
      break;
    }

    case EVENT_VIBRATO_DELAY: {
      uint8_t lfoDelay = GetByte(curOffset++);
      desc << "Delay: " << (int) lfoDelay;
      AddGenericEvent(beginOffset, curOffset - beginOffset, "Vibrato Delay", desc.str(), CLR_MODULATION, ICON_CONTROL);
      break;
    }

    case EVENT_VIBRATO_OFF: {
      AddGenericEvent(beginOffset, curOffset - beginOffset, "Vibrato Off", desc.str(), CLR_MODULATION, ICON_CONTROL);
      break;
    }

    case EVENT_VIBRATO: {
      uint8_t lfoDelay = GetByte(curOffset++);
      uint8_t arg2 = GetByte(curOffset++);
      uint8_t arg3 = GetByte(curOffset++);
      desc << "Delay: " << (int) lfoDelay << "  Arg2: " << (int) arg2 << "  Arg3: " << (int) arg3;
      AddGenericEvent(beginOffset, curOffset - beginOffset, "Vibrato", desc.str(), CLR_MODULATION, ICON_CONTROL);
      break;
    }

    case EVENT_VOLUME_REL: {
      int8_t delta = GetByte(curOffset++);
      if (spcVolume + delta > 255) {
        spcVolume = 255;
      }
      else if (spcVolume + delta < 0) {
        spcVolume = 0;
      }
      else {
        spcVolume += delta;
      }
      AddVol(beginOffset, curOffset - beginOffset, spcVolume / 2, "Volume (Relative)");
      break;
    }

    case EVENT_PAN: {
      uint8_t newPan = GetByte(curOffset++);
      if (newPan > 20) {
        // unexpected value
        newPan = 20;
      }

      // TODO: use correct pan table
      double volumeLeft;
      double volumeRight;
      // actual engine divides pan by 256, though pan value must be always 7-bit, perhaps
      volumeLeft = panTable[20 - newPan] / 128.0;
      volumeRight = panTable[newPan] / 128.0;

      // TODO: fix volume scale when L+R > 1.0?
      double volumeScale;
      int8_t midiPan = ConvertVolumeBalanceToStdMidiPan(volumeLeft, volumeRight, &volumeScale);
      volumeScale = std::min(volumeScale, 1.0); // workaround

      AddPan(beginOffset, curOffset - beginOffset, midiPan);
      AddExpressionNoItem(std::round(127.0 * volumeScale));
      break;
    }

    case EVENT_VOLUME: {
      uint8_t newVolume = GetByte(curOffset++);
      spcVolume = newVolume;
      AddVol(beginOffset, curOffset - beginOffset, spcVolume / 2);
      break;
    }

    case EVENT_GAIN_ENVELOPE_REST: {
      uint16_t envelopeAddress = GetShort(curOffset);
      curOffset += 2;
      desc << "Envelope: $" << std::hex << std::setfill('0') << std::setw(4) << std::uppercase << envelopeAddress;
      AddGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "GAIN Envelope (Rest)",
                      desc.str(),
                      CLR_ADSR,
                      ICON_CONTROL);
      AddGAINEnvelope(envelopeAddress);
      break;
    }

    case EVENT_GAIN_ENVELOPE_DECAY_TIME: {
      uint8_t dur = GetByte(curOffset++);
      uint8_t gain = GetByte(curOffset++);
      desc << "Duration: Full-Length - " << dur << "  GAIN: $" << std::hex << std::setfill('0') << std::setw(2)
          << std::uppercase << gain;
      AddGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "GAIN Envelope Decay Time",
                      desc.str(),
                      CLR_ADSR,
                      ICON_CONTROL);
      break;
    }

    case EVENT_MANUAL_DURATION_OFF: {
      manualDuration = false;
      AddGenericEvent(beginOffset, curOffset - beginOffset, "Manual Duration Off", desc.str(), CLR_DURNOTE);
      break;
    }

    case EVENT_MANUAL_DURATION_ON: {
      manualDuration = true;
      AddGenericEvent(beginOffset, curOffset - beginOffset, "Manual Duration On", desc.str(), CLR_DURNOTE);
      break;
    }

    case EVENT_AUTO_DURATION_THRESHOLD: {
      manualDuration = false;
      autoDurationThreshold = GetByte(curOffset++);
      desc << "Duration: Full-Length - " << autoDurationThreshold;
      AddGenericEvent(beginOffset, curOffset - beginOffset, "Auto Duration Threshold", desc.str(), CLR_DURNOTE);
      break;
    }

    case EVENT_GAIN_ENVELOPE_SUSTAIN: {
      uint16_t envelopeAddress = GetShort(curOffset);
      curOffset += 2;
      desc << "Envelope: $" << std::hex << std::setfill('0') << std::setw(4) << std::uppercase << envelopeAddress;
      AddGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "GAIN Envelope (Sustain)",
                      desc.str(),
                      CLR_ADSR,
                      ICON_CONTROL);
      AddGAINEnvelope(envelopeAddress);
      break;
    }

    case EVENT_ECHO_VOLUME_ENVELOPE: {
      uint16_t envelopeAddress = GetShort(curOffset);
      curOffset += 2;
      desc << "Envelope: $" << std::hex << std::setfill('0') << std::setw(4) << std::uppercase << envelopeAddress;
      AddGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "Echo Volume Envelope",
                      desc.str(),
                      CLR_REVERB,
                      ICON_CONTROL);
      AddEchoVolumeEnvelope(envelopeAddress);
      break;
    }

    case EVENT_ECHO_VOLUME: {
      int8_t echoVolumeLeft = GetByte(curOffset++);
      int8_t echoVolumeRight = GetByte(curOffset++);
      int8_t echoVolumeMono = GetByte(curOffset++);
      desc << "Left Volume: " << echoVolumeLeft << "  Right Volume: " << echoVolumeRight << "  Mono Volume: "
          << echoVolumeMono;
      AddGenericEvent(beginOffset, curOffset - beginOffset, "Echo Volume", desc.str(), CLR_REVERB, ICON_CONTROL);
      break;
    }

    case EVENT_ECHO_OFF: {
      AddReverb(beginOffset, curOffset - beginOffset, 0, "Echo Off");
      break;
    }

    case EVENT_ECHO_ON: {
      AddReverb(beginOffset, curOffset - beginOffset, 40, "Echo On");
      break;
    }

    case EVENT_ECHO_PARAM: {
      int8_t echoFeedback = GetByte(curOffset++);
      int8_t echoVolumeLeft = GetByte(curOffset++);
      int8_t echoVolumeRight = GetByte(curOffset++);
      int8_t echoVolumeMono = GetByte(curOffset++);
      desc << "Feedback: " << echoFeedback << "  Left Volume: " << echoVolumeLeft << "  Right Volume: "
          << echoVolumeRight << "  Mono Volume: " << echoVolumeMono;
      AddGenericEvent(beginOffset, curOffset - beginOffset, "Echo Param", desc.str(), CLR_REVERB, ICON_CONTROL);
      break;
    }

    case EVENT_ADSR: {
      uint8_t param = GetByte(curOffset);
      if (param >= 0x80) {
        uint8_t adsr1 = GetByte(curOffset++);
        uint8_t adsr2 = GetByte(curOffset++);
        desc << "ADSR(1): $" << std::hex << std::setfill('0') << std::setw(2) << std::uppercase << adsr1 <<
            "  ADSR(2): $" << std::hex << std::setfill('0') << std::setw(2) << std::uppercase << adsr2;
      }
      else {
        uint8_t instrNum = GetByte(curOffset++);
        desc << "Instrument: " << instrNum;
      }
      AddGenericEvent(beginOffset, curOffset - beginOffset, "ADSR", desc.str(), CLR_ADSR, ICON_CONTROL);
      break;
    }

    case EVENT_GAIN_ENVELOPE_DECAY: {
      uint16_t envelopeAddress = GetShort(curOffset);
      curOffset += 2;
      desc << "Envelope: $" << std::hex << std::setfill('0') << std::setw(4) << std::uppercase << envelopeAddress;
      AddGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      "GAIN Envelope (Decay)",
                      desc.str(),
                      CLR_ADSR,
                      ICON_CONTROL);
      AddGAINEnvelope(envelopeAddress);
      break;
    }

    case EVENT_INSTRUMENT: {
      uint8_t instrNum = GetByte(curOffset++);
      AddProgramChange(beginOffset, curOffset - beginOffset, instrNum, true);
      break;
    }

    case EVENT_END: {
      AddEndOfTrack(beginOffset, curOffset - beginOffset);
      bContinue = false;
      break;
    }

    default: {
      auto descr = logEvent(statusByte);
      AddUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", descr);
      bContinue = false;
      break;
    }
  }

  //assert(curOffset >= parentSeq->dwOffset);

  //std::ostringstream ssTrace;
  //ssTrace << "" << std::hex << std::setfill('0') << std::setw(8) << std::uppercase << beginOffset << ": " << std::setw(2) << (int)statusByte  << " -> " << std::setw(8) << curOffset << std::endl;
  //OutputDebugString(ssTrace.str().c_str());

  return bContinue;
}

bool PrismSnesTrack::ReadDeltaTime(uint32_t &curOffset, uint8_t &len) {
  if (curOffset + 1 > 0x10000) {
    return false;
  }

  if (defaultLength != 0) {
    len = defaultLength;
  }
  else {
    len = GetByte(curOffset++);
  }
  return true;
}

bool PrismSnesTrack::ReadDuration(uint32_t &curOffset, uint8_t len, uint8_t &durDelta) {
  if (curOffset + 1 > 0x10000) {
    return false;
  }

  if (manualDuration) {
    durDelta = GetByte(curOffset++);
  }
  else {
    durDelta = len >> 1;
    if (durDelta > autoDurationThreshold) {
      durDelta = autoDurationThreshold;
    }
  }

  if (durDelta > len) {
    // unexpected value
    durDelta = 0;
  }

  return true;
}

uint8_t PrismSnesTrack::GetDuration(uint32_t curOffset, uint8_t len, uint8_t durDelta) {
  // TODO: adjust duration for slur/tie
  if (curOffset + 1 <= 0x10000) {
    uint8_t nextStatusByte = GetByte(curOffset);
    if (nextStatusByte == 0xee || nextStatusByte == 0xf4 || nextStatusByte == 0xce || nextStatusByte == 0xf5) {
      durDelta = 0;
    }
  }

  if (durDelta > len) {
    // unexpected value
    durDelta = 0;
  }

  return len - durDelta;
}

void PrismSnesTrack::AddVolumeEnvelope(uint16_t envelopeAddress) {
  PrismSnesSeq *parentSeq = (PrismSnesSeq *) this->parentSeq;
  parentSeq->DemandEnvelopeContainer(envelopeAddress);

  if (!IsOffsetUsed(envelopeAddress)) {
    std::ostringstream envelopeName;
    envelopeName << "Volume Envelope ($" << std::hex << std::setfill('0') << std::setw(4) << std::uppercase
        << envelopeAddress << ")";
    VGMHeader *envHeader = parentSeq->envContainer->AddHeader(envelopeAddress, 0, envelopeName.str());

    uint16_t envOffset = 0;
    while (envelopeAddress + envOffset + 2 <= 0x10000) {
      std::ostringstream eventName;

      uint8_t volumeFrom = GetByte(envelopeAddress + envOffset);
      int8_t volumeDelta = GetByte(envelopeAddress + envOffset + 1);
      if (volumeFrom == 0 && volumeDelta == 0) {
        // $00 $00 $xx $yy sets offset to $yyxx
        uint16_t newAddress = GetByte(envelopeAddress + envOffset + 2);

        eventName << "Jump to $" << std::hex << std::setfill('0') << std::setw(4) << std::uppercase << newAddress;
        envHeader->AddSimpleItem(envelopeAddress + envOffset, 4, eventName.str());
        envOffset += 4;
        break;
      }

      uint8_t envelopeSpeed = GetByte(envelopeAddress + envOffset + 2);
      uint8_t deltaTime = GetByte(envelopeAddress + envOffset + 3);
      eventName << "Volume: " << volumeFrom << "  Volume Delta: " << volumeDelta << "  Envelope Speed: "
          << envelopeSpeed << "  Delta-Time: " << deltaTime;
      envHeader->AddSimpleItem(envelopeAddress + envOffset, 4, eventName.str());
      envOffset += 4;

      // workaround: quit to prevent out of bound
      break;
    }

    envHeader->unLength = envOffset;
  }
}

void PrismSnesTrack::AddPanEnvelope(uint16_t envelopeAddress) {
  PrismSnesSeq *parentSeq = (PrismSnesSeq *) this->parentSeq;
  parentSeq->DemandEnvelopeContainer(envelopeAddress);

  if (!IsOffsetUsed(envelopeAddress)) {
    std::ostringstream envelopeName;
    envelopeName << "Pan Envelope ($" << std::hex << std::setfill('0') << std::setw(4) << std::uppercase
        << envelopeAddress << ")";
    VGMHeader *envHeader = parentSeq->envContainer->AddHeader(envelopeAddress, 0, envelopeName.str());

    uint16_t envOffset = 0;
    while (envOffset < 0x100) {
      std::ostringstream eventName;

      uint8_t newPan = GetByte(envelopeAddress + envOffset);
      if (newPan >= 0x80) {
        // $ff $xx sets offset to $xx
        uint8_t newOffset = GetByte(envelopeAddress + envOffset + 1);

        eventName << "Jump to $" << std::hex << std::setfill('0') << std::setw(4) << std::uppercase
            << (envelopeAddress + newOffset);
        envHeader->AddSimpleItem(envelopeAddress + envOffset, 2, eventName.str());
        envOffset += 2;
        break;
      }

      eventName << "Pan: " << newPan;
      envHeader->AddSimpleItem(envelopeAddress + envOffset, 1, eventName.str());
      envOffset++;

      // workaround: quit to prevent out of bound
      break;
    }

    envHeader->unLength = envOffset;
  }
}

void PrismSnesTrack::AddEchoVolumeEnvelope(uint16_t envelopeAddress) {
  PrismSnesSeq *parentSeq = (PrismSnesSeq *) this->parentSeq;
  parentSeq->DemandEnvelopeContainer(envelopeAddress);

  if (!IsOffsetUsed(envelopeAddress)) {
    std::ostringstream envelopeName;
    envelopeName << "Echo Volume Envelope ($" << std::hex << std::setfill('0') << std::setw(4) << std::uppercase
        << envelopeAddress << ")";
    VGMHeader *envHeader = parentSeq->envContainer->AddHeader(envelopeAddress, 0, envelopeName.str());

    uint16_t envOffset = 0;
    while (envOffset < 0x100) {
      std::ostringstream eventName;

      uint8_t deltaTime = GetByte(envelopeAddress + envOffset);
      if (deltaTime == 0xff) {
        // $ff $xx sets offset to $xx
        uint8_t newOffset = GetByte(envelopeAddress + envOffset + 1);

        eventName << "Jump to $" << std::hex << std::setfill('0') << std::setw(4) << std::uppercase
            << (envelopeAddress + newOffset);
        envHeader->AddSimpleItem(envelopeAddress + envOffset, 2, eventName.str());
        envOffset += 2;
        break;
      }

      int8_t echoVolumeLeft = GetByte(envelopeAddress + envOffset + 1);
      int8_t echoVolumeRight = GetByte(envelopeAddress + envOffset + 2);
      int8_t echoVolumeMono = GetByte(envelopeAddress + envOffset + 3);
      eventName << "Delta-Time: " << deltaTime << "  Left Volume: " << echoVolumeLeft << "  Right Volume: "
          << echoVolumeRight << "  Mono Volume: " << echoVolumeMono;
      envHeader->AddSimpleItem(envelopeAddress + envOffset, 4, eventName.str());
      envOffset += 4;

      // workaround: quit to prevent out of bound
      break;
    }

    envHeader->unLength = envOffset;
  }
}

void PrismSnesTrack::AddGAINEnvelope(uint16_t envelopeAddress) {
  PrismSnesSeq *parentSeq = (PrismSnesSeq *) this->parentSeq;
  parentSeq->DemandEnvelopeContainer(envelopeAddress);

  if (!IsOffsetUsed(envelopeAddress)) {
    std::ostringstream envelopeName;
    envelopeName << "GAIN Envelope ($" << std::hex << std::setfill('0') << std::setw(4) << std::uppercase
        << envelopeAddress << ")";
    VGMHeader *envHeader = parentSeq->envContainer->AddHeader(envelopeAddress, 0, envelopeName.str());

    uint16_t envOffset = 0;
    while (envOffset < 0x100) {
      std::ostringstream eventName;

      uint8_t gain = GetByte(envelopeAddress + envOffset);
      if (gain == 0xff) {
        // $ff $xx sets offset to $xx
        uint8_t newOffset = GetByte(envelopeAddress + envOffset + 1);

        eventName << "Jump to $" << std::hex << std::setfill('0') << std::setw(4) << std::uppercase
            << (envelopeAddress + newOffset);
        envHeader->AddSimpleItem(envelopeAddress + envOffset, 2, eventName.str());
        envOffset += 2;
        break;
      }

      uint8_t deltaTime = GetByte(envelopeAddress + envOffset + 1);
      eventName << "GAIN: $" << std::hex << std::setfill('0') << std::setw(2) << std::uppercase << gain << std::dec
          << std::setfill(' ') << std::setw(0) << ", Delta-Time: " << deltaTime;
      envHeader->AddSimpleItem(envelopeAddress + envOffset, 2, eventName.str());
      envOffset += 2;

      // workaround: quit to prevent out of bound
      break;
    }

    envHeader->unLength = envOffset;
  }
}

void PrismSnesTrack::AddPanTable(uint16_t panTableAddress) {
  PrismSnesSeq *parentSeq = (PrismSnesSeq *) this->parentSeq;
  parentSeq->DemandEnvelopeContainer(panTableAddress);

  if (!IsOffsetUsed(panTableAddress)) {
    std::ostringstream eventName;
    eventName << "Pan Table ($" << std::hex << std::setfill('0') << std::setw(4) << std::uppercase << panTableAddress
        << ")";
    parentSeq->envContainer->AddSimpleItem(panTableAddress, 21, eventName.str());
  }
}
