#include "NeverlandSnesSeq.h"

DECLARE_FORMAT(NeverlandSnes);

//  ****************
//  NeverlandSnesSeq
//  ****************
#define MAX_TRACKS  8
#define SEQ_PPQN    48

NeverlandSnesSeq::NeverlandSnesSeq(RawFile *file, NeverlandSnesVersion ver, uint32_t seqdataOffset)
    : VGMSeq(NeverlandSnesFormat::name, file, seqdataOffset), version(ver) {
  bLoadTickByTick = true;
  bAllowDiscontinuousTrackData = true;
  bUseLinearAmplitudeScale = true;

  UseReverb();
  AlwaysWriteInitialReverb(0);

  LoadEventMap();
}

NeverlandSnesSeq::~NeverlandSnesSeq(void) {
}

void NeverlandSnesSeq::ResetVars(void) {
  VGMSeq::ResetVars();
}

bool NeverlandSnesSeq::GetHeaderInfo(void) {
  SetPPQN(SEQ_PPQN);

  VGMHeader *header = addHeader(dwOffset, 0);
  if (version == NEVERLANDSNES_SFC) {
    header->unLength = 0x40;
  }
  else if (version == NEVERLANDSNES_S2C) {
    header->unLength = 0x50;
  }

  if (dwOffset + header->unLength >= 0x10000) {
    return false;
  }

  header->addChild(dwOffset, 3, "Signature");
  header->addUnknownChild(dwOffset + 3, 1);

  const size_t NAME_SIZE = 12;
  char rawName[NAME_SIZE + 1] = {0};
  GetBytes(dwOffset + 4, NAME_SIZE, rawName);
  header->addChild(dwOffset + 4, 12, "Song Name");

  // trim name text
  for (int i = NAME_SIZE - 1; i >= 0; i--) {
    if (rawName[i] != ' ') {
      break;
    }
    rawName[i] = '\0';
  }
  // set name to the sequence
  if (rawName[0] != ('\0')) {
    std::string nameStr = std::string(rawName);
    setName(nameStr);
  }
  else {
    setName("NeverlandSnesSeq");
  }

  for (uint8_t trackIndex = 0; trackIndex < MAX_TRACKS; trackIndex++) {
    uint16_t trackSignPtr = dwOffset + 0x10 + trackIndex;
    uint8_t trackSign = GetByte(trackSignPtr);

    std::stringstream trackSignName;
    trackSignName << "Track " << (trackIndex + 1) << " Entry";
    header->addChild(trackSignPtr, 1, trackSignName.str());

    uint16_t sectionListOffsetPtr = dwOffset + 0x20 + (trackIndex * 2);
    if (trackSign != 0xff) {
      uint16_t sectionListAddress = GetShortAddress(sectionListOffsetPtr);

      std::stringstream playlistName;
      playlistName << "Track " << (trackIndex + 1) << " Playlist Pointer";
      header->addChild(sectionListOffsetPtr, 2, playlistName.str());

      NeverlandSnesTrack *track = new NeverlandSnesTrack(this, sectionListAddress);
      aTracks.push_back(track);
    }
    else {
      header->addChild(sectionListOffsetPtr, 2, "NULL");
    }
  }

  return true;
}

bool NeverlandSnesSeq::GetTrackPointers(void) {
  return true;
}

void NeverlandSnesSeq::LoadEventMap() {
  // TODO: NeverlandSnesSeq::LoadEventMap
}

uint16_t NeverlandSnesSeq::ConvertToAPUAddress(uint16_t offset) {
  if (version == NEVERLANDSNES_S2C) {
    return dwOffset + offset;
  }
  else {
    return offset;
  }
}

uint16_t NeverlandSnesSeq::GetShortAddress(uint32_t offset) {
  return ConvertToAPUAddress(GetShort(offset));
}

//  ******************
//  NeverlandSnesTrack
//  ******************

NeverlandSnesTrack::NeverlandSnesTrack(NeverlandSnesSeq *parentFile, uint32_t offset, uint32_t length)
    : SeqTrack(parentFile, offset, length) {
  ResetVars();
  bDetermineTrackLengthEventByEvent = true;
  bWriteGenericEventAsTextEvent = false;
}

void NeverlandSnesTrack::ResetVars(void) {
  SeqTrack::ResetVars();
}

bool NeverlandSnesTrack::ReadEvent(void) {
  NeverlandSnesSeq *parentSeq = (NeverlandSnesSeq *) this->parentSeq;

  uint32_t beginOffset = curOffset;
  if (curOffset >= 0x10000) {
    return false;
  }

  uint8_t statusByte = GetByte(curOffset++);
  bool bContinue = true;

  std::stringstream desc;

  NeverlandSnesSeqEventType eventType = (NeverlandSnesSeqEventType) 0;
  std::map<uint8_t, NeverlandSnesSeqEventType>::iterator pEventType = parentSeq->EventMap.find(statusByte);
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

    default: {
      auto descr = logEvent(statusByte);
      AddUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", descr);
      bContinue = false;
      break;
    }
  }

  //std::ostringstream ssTrace;
  //ssTrace << "" << std::hex << std::setfill('0') << std::setw(8) << std::uppercase << beginOffset << ": " << std::setw(2) << (int)statusByte  << " -> " << std::setw(8) << curOffset << std::endl;
  //OutputDebugString(ssTrace.str().c_str());

  return bContinue;
}

uint16_t NeverlandSnesTrack::ConvertToAPUAddress(uint16_t offset) {
  NeverlandSnesSeq *parentSeq = (NeverlandSnesSeq *) this->parentSeq;
  return parentSeq->ConvertToAPUAddress(offset);
}

uint16_t NeverlandSnesTrack::GetShortAddress(uint32_t offset) {
  NeverlandSnesSeq *parentSeq = (NeverlandSnesSeq *) this->parentSeq;
  return parentSeq->GetShortAddress(offset);
}
