#include "NeverlandSnesSeq.h"
#include <spdlog/fmt/fmt.h>

DECLARE_FORMAT(NeverlandSnes);

//  ****************
//  NeverlandSnesSeq
//  ****************
#define MAX_TRACKS  8
#define SEQ_PPQN    48

NeverlandSnesSeq::NeverlandSnesSeq(RawFile *file, NeverlandSnesVersion ver, uint32_t seqdataOffset)
    : VGMSeq(NeverlandSnesFormat::name, file, seqdataOffset), version(ver) {
  bLoadTickByTick = true;
  setAllowDiscontinuousTrackData(true);
  setUseLinearAmplitudeScale(true);

  useReverb();
  setAlwaysWriteInitialReverb(0);

  loadEventMap();
}

NeverlandSnesSeq::~NeverlandSnesSeq(void) {
}

void NeverlandSnesSeq::resetVars(void) {
  VGMSeq::resetVars();
}

bool NeverlandSnesSeq::parseHeader(void) {
  setPPQN(SEQ_PPQN);

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
  readBytes(dwOffset + 4, NAME_SIZE, rawName);
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
    uint8_t trackSign = readByte(trackSignPtr);

    auto trackSignName = fmt::format("Track {:d} Entry", trackIndex + 1);
    header->addChild(trackSignPtr, 1, trackSignName);

    uint16_t sectionListOffsetPtr = dwOffset + 0x20 + (trackIndex * 2);
    if (trackSign != 0xff) {
      uint16_t sectionListAddress = getShortAddress(sectionListOffsetPtr);

      auto playlistName = fmt::format("Track {:d} Playlist Pointer", trackIndex + 1);
      header->addChild(sectionListOffsetPtr, 2, playlistName);

      NeverlandSnesTrack *track = new NeverlandSnesTrack(this, sectionListAddress);
      aTracks.push_back(track);
    }
    else {
      header->addChild(sectionListOffsetPtr, 2, "NULL");
    }
  }

  return true;
}

bool NeverlandSnesSeq::parseTrackPointers(void) {
  return true;
}

void NeverlandSnesSeq::loadEventMap() {
  // TODO: NeverlandSnesSeq::LoadEventMap
}

uint16_t NeverlandSnesSeq::convertToApuAddress(uint16_t offset) {
  if (version == NEVERLANDSNES_S2C) {
    return dwOffset + offset;
  }
  else {
    return offset;
  }
}

uint16_t NeverlandSnesSeq::getShortAddress(uint32_t offset) {
  return convertToApuAddress(readShort(offset));
}

//  ******************
//  NeverlandSnesTrack
//  ******************

NeverlandSnesTrack::NeverlandSnesTrack(NeverlandSnesSeq *parentFile, uint32_t offset, uint32_t length)
    : SeqTrack(parentFile, offset, length) {
  resetVars();
  bDetermineTrackLengthEventByEvent = true;
  bWriteGenericEventAsTextEvent = false;
}

void NeverlandSnesTrack::resetVars(void) {
  SeqTrack::resetVars();
}

bool NeverlandSnesTrack::readEvent(void) {
  NeverlandSnesSeq *parentSeq = (NeverlandSnesSeq *) this->parentSeq;

  uint32_t beginOffset = curOffset;
  if (curOffset >= 0x10000) {
    return false;
  }

  uint8_t statusByte = readByte(curOffset++);
  bool bContinue = true;

  std::string desc;

  NeverlandSnesSeqEventType eventType = (NeverlandSnesSeqEventType) 0;
  std::map<uint8_t, NeverlandSnesSeqEventType>::iterator pEventType = parentSeq->EventMap.find(statusByte);
  if (pEventType != parentSeq->EventMap.end()) {
    eventType = pEventType->second;
  }

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
      desc = fmt::format("Event: 0x{:02X}  Arg1: {:d}  Arg2: {:d}  Arg3: {:d}", statusByte, arg1, arg2, arg3);
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
      break;
    }

    case EVENT_UNKNOWN4: {
      uint8_t arg1 = readByte(curOffset++);
      uint8_t arg2 = readByte(curOffset++);
      uint8_t arg3 = readByte(curOffset++);
      uint8_t arg4 = readByte(curOffset++);
      desc = fmt::format("Event: 0x{:02X}  Arg1: {:d}  Arg2: {:d}  Arg3: {:d}  Arg4: {:d}",
                         statusByte, arg1, arg2, arg3, arg4);
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc);
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

uint16_t NeverlandSnesTrack::convertToApuAddress(uint16_t offset) {
  NeverlandSnesSeq *parentSeq = (NeverlandSnesSeq *) this->parentSeq;
  return parentSeq->convertToApuAddress(offset);
}

uint16_t NeverlandSnesTrack::getShortAddress(uint32_t offset) {
  NeverlandSnesSeq *parentSeq = (NeverlandSnesSeq *) this->parentSeq;
  return parentSeq->getShortAddress(offset);
}
