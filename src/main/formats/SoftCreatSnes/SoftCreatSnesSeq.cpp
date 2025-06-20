#include "SoftCreatSnesSeq.h"
#include <spdlog/fmt/fmt.h>

DECLARE_FORMAT(SoftCreatSnes);

//  ****************
//  SoftCreatSnesSeq
//  ****************
#define MAX_TRACKS  8
#define SEQ_PPQN    48

SoftCreatSnesSeq::SoftCreatSnesSeq(RawFile *file,
                                   SoftCreatSnesVersion ver,
                                   uint32_t seqdataOffset,
                                   uint8_t headerAlignSize,
                                   std::string newName)
    : VGMSeq(SoftCreatSnesFormat::name, file, seqdataOffset, 0, newName), version(ver),
      headerAlignSize(headerAlignSize) {
  bLoadTickByTick = true;
  setAllowDiscontinuousTrackData(true);
  setUseLinearAmplitudeScale(true);

  useReverb();
  setAlwaysWriteInitialReverb(0);

  loadEventMap();
}

SoftCreatSnesSeq::~SoftCreatSnesSeq(void) {
}

void SoftCreatSnesSeq::resetVars(void) {
  VGMSeq::resetVars();
}

bool SoftCreatSnesSeq::parseHeader(void) {
  setPPQN(SEQ_PPQN);

  VGMHeader *header = addHeader(dwOffset, headerAlignSize * MAX_TRACKS);
  if (dwOffset + headerAlignSize * MAX_TRACKS > 0x10000) {
    return false;
  }

  for (uint8_t trackIndex = 0; trackIndex < MAX_TRACKS; trackIndex++) {
    uint32_t addrTrackLowPtr = dwOffset + (trackIndex * 2 * headerAlignSize);
    uint32_t addrTrackHighPtr = addrTrackLowPtr + headerAlignSize;
    if (addrTrackLowPtr + 1 > 0x10000 || addrTrackHighPtr + 1 > 0x10000) {
      return false;
    }

    auto trackName = fmt::format("Track Pointer {:d}", trackIndex + 1);
    header->addChild(addrTrackLowPtr, 1, trackName + " (LSB)");
    header->addChild(addrTrackHighPtr, 1, trackName + " (MSB)");

    uint16_t addrTrackStart = readByte(addrTrackLowPtr) | (readByte(addrTrackHighPtr) << 8);
    if (addrTrackStart != 0xffff) {
      SoftCreatSnesTrack *track = new SoftCreatSnesTrack(this, addrTrackStart);
      aTracks.push_back(track);
    }
  }

  return true;
}

bool SoftCreatSnesSeq::parseTrackPointers(void) {
  return true;
}

void SoftCreatSnesSeq::loadEventMap() {
  if (version == SOFTCREATSNES_NONE) {
    return;
  }

  // TODO: SoftCreatSnesSeq::LoadEventMap
}


//  ******************
//  SoftCreatSnesTrack
//  ******************

SoftCreatSnesTrack::SoftCreatSnesTrack(SoftCreatSnesSeq *parentFile, uint32_t offset, uint32_t length)
    : SeqTrack(parentFile, offset, length) {
  resetVars();
  bDetermineTrackLengthEventByEvent = true;
  bWriteGenericEventAsTextEvent = false;
}

void SoftCreatSnesTrack::resetVars(void) {
  SeqTrack::resetVars();
}

bool SoftCreatSnesTrack::readEvent(void) {
  SoftCreatSnesSeq *parentSeq = (SoftCreatSnesSeq *) this->parentSeq;

  uint32_t beginOffset = curOffset;
  if (curOffset >= 0x10000) {
    return false;
  }

  uint8_t statusByte = readByte(curOffset++);
  bool bContinue = true;


  SoftCreatSnesSeqEventType eventType = (SoftCreatSnesSeqEventType) 0;
  std::map<uint8_t, SoftCreatSnesSeqEventType>::iterator pEventType = parentSeq->EventMap.find(statusByte);
  if (pEventType != parentSeq->EventMap.end()) {
    eventType = pEventType->second;
  }

  std::string desc;

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
