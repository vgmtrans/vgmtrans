#include "SoftCreatSnesSeq.h"

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
  setVolumeAmplitudeScale(AmplitudeScale::Linear);
  setPanAmplitudeScale(AmplitudeScale::Linear, PanVolumeCorrectionMode::kNoVolumeAdjust);

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

    std::stringstream trackName;
    trackName << "Track Pointer " << (trackIndex + 1);
    header->addChild(addrTrackLowPtr, 1, trackName.str() + " (LSB)");
    header->addChild(addrTrackHighPtr, 1, trackName.str() + " (MSB)");

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

  std::stringstream desc;

  SoftCreatSnesSeqEventType eventType = (SoftCreatSnesSeqEventType) 0;
  std::map<uint8_t, SoftCreatSnesSeqEventType>::iterator pEventType = parentSeq->EventMap.find(statusByte);
  if (pEventType != parentSeq->EventMap.end()) {
    eventType = pEventType->second;
  }

  switch (eventType) {
    case EVENT_UNKNOWN0:
      desc << "Event: 0x" << std::hex << std::setfill('0') << std::setw(2) << std::uppercase << (int) statusByte;
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc.str());
      break;

    case EVENT_UNKNOWN1: {
      uint8_t arg1 = readByte(curOffset++);
      desc << "Event: 0x" << std::hex << std::setfill('0') << std::setw(2) << std::uppercase << (int) statusByte
          << std::dec << std::setfill(' ') << std::setw(0)
          << "  Arg1: " << (int) arg1;
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc.str());
      break;
    }

    case EVENT_UNKNOWN2: {
      uint8_t arg1 = readByte(curOffset++);
      uint8_t arg2 = readByte(curOffset++);
      desc << "Event: 0x" << std::hex << std::setfill('0') << std::setw(2) << std::uppercase << (int) statusByte
          << std::dec << std::setfill(' ') << std::setw(0)
          << "  Arg1: " << (int) arg1
          << "  Arg2: " << (int) arg2;
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc.str());
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
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc.str());
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
      addUnknown(beginOffset, curOffset - beginOffset, "Unknown Event", desc.str());
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
