/*
 * VGMTrans (c) 2002-2025
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#include "NinN64Seq.h"
#include "NinN64Format.h"

DECLARE_FORMAT(NinN64)

NinN64Seq::NinN64Seq(RawFile *file, uint32_t offset, std::string name)
    : VGMSeq(NinN64Format::name, file, offset, 0, std::move(name)) {}

bool NinN64Seq::parseHeader() {
  if (!isValidOffset(dwOffset + 0x45)) {
    return false;
  }

  VGMHeader *header = addHeader(dwOffset, 0x48, "Sequence Header");
  if (header != nullptr) {
    header->addChild(dwOffset, 0x40, "Track Pointers");
    header->addChild(dwOffset + 0x40, 4, "PPQN");
    header->addChild(dwOffset + 0x44, 2, "Tempo Seed");
  }

  uint32_t ppqnValue = getWordBE(dwOffset + 0x40);
  if (ppqnValue == 0) {
    ppqnValue = 0x60;  // fall back to a reasonable default if header was malformed
  }
  setPPQN(static_cast<uint16_t>(ppqnValue & 0xFFFF));

  return true;
}

bool NinN64Seq::parseTrackPointers() {
  VGMHeader *pointerTable = addHeader(dwOffset, 0x40, "Track Pointer Table");

  for (size_t trackIndex = 0; trackIndex < kMaxTracks; ++trackIndex) {
    const uint32_t ptrOffset = dwOffset + static_cast<uint32_t>(trackIndex * 4);
    const uint32_t trackRelativeOffset = getWordBE(ptrOffset);
    if (trackRelativeOffset == 0) {
      continue;
    }

    const uint32_t trackStart = dwOffset + trackRelativeOffset;
    if (!isValidOffset(trackStart)) {
      L_WARN("NinN64 sequence {} has track {} pointer outside file (0x{:X})", name(), trackIndex,
                   trackStart);
      continue;
    }

    if (pointerTable != nullptr) {
      pointerTable->addPointer(ptrOffset, 4, trackStart, true, fmt::format("Track {} Pointer", trackIndex));
    }

    auto trackName = fmt::format("Track {:02}", trackIndex + 1);
    auto *track = new NinN64Track(this, trackStart, trackName);
    aTracks.push_back(track);
  }

  return !aTracks.empty();
}

NinN64Track::NinN64Track(NinN64Seq *parentSeq, uint32_t offset, const std::string &name)
    : SeqTrack(parentSeq, offset, 0, name), runningStatus(0) {}

void NinN64Track::resetVars() {
  SeqTrack::resetVars();
  runningStatus = 0;
}

uint8_t NinN64Track::readByte() {
  if (!isValidOffset(curOffset)) {
    ++curOffset;
    return 0;
  }

  uint8_t value = parentSeq->rawFile()->readByte(curOffset);
  ++curOffset;
  return value;
}

uint32_t NinN64Track::readVarLen() {
  uint32_t value = 0;
  uint8_t c = readByte();
  value = c & 0x7F;
  while (c & 0x80) {
    c = readByte();
    value = (value << 7) + (c & 0x7F);
  }
  return value;
}

bool NinN64Track::readEvent() {
  if (!isValidOffset(curOffset)) {
    addEndOfTrackNoItem();
    return false;
  }

  const uint32_t eventStart = curOffset;
  const uint32_t delta = readVarLen();
  addTime(delta);

  uint8_t statusOrData = readByte();
  uint8_t status = statusOrData;
  uint8_t firstData = 0;
  bool hasPrefetchedData = false;

  if (status < 0x80) {
    if (runningStatus == 0) {
      addEndOfTrack(eventStart, 0);
      return false;
    }
    firstData = statusOrData;
    hasPrefetchedData = true;
    status = runningStatus;
  } else {
    runningStatus = status;
  }

  if (status >= 0x80 && status <= 0xEF) {
    channel = status & 0x0F;
  }

  switch (status & 0xF0) {
    case 0x80: {
      uint8_t key = hasPrefetchedData ? firstData : readByte();
      (void)readByte();
      addNoteOff(eventStart, curOffset - eventStart, key);
      break;
    }

    case 0x90: {
      uint8_t key = hasPrefetchedData ? firstData : readByte();
      uint8_t vel = readByte();
      uint32_t duration = readVarLen();
      if (vel == 0) {
        addNoteOff(eventStart, curOffset - eventStart, key);
      } else {
        addNoteByDur(eventStart, curOffset - eventStart, key, vel, duration);
      }
      break;
    }

    case 0xA0: {
      uint8_t key = hasPrefetchedData ? firstData : readByte();
      uint8_t amount = readByte();
      addGenericEvent(eventStart, curOffset - eventStart, fmt::format("Key Pressure {:02X}", key),
                      fmt::format("Pressure {:02X}", amount), Type::Control);
      break;
    }

    case 0xB0: {
      uint8_t controller = hasPrefetchedData ? firstData : readByte();
      uint8_t value = readByte();
      addGenericEvent(eventStart, curOffset - eventStart,
                      fmt::format("Controller {:02X}", controller),
                      fmt::format("Value {:02X}", value), Type::Control);
      addControllerEventNoItem(controller, value);
      break;
    }

    case 0xC0: {
      uint8_t program = hasPrefetchedData ? firstData : readByte();
      addProgramChange(eventStart, curOffset - eventStart, program);
      break;
    }

    case 0xD0: {
      uint8_t pressure = hasPrefetchedData ? firstData : readByte();
      addGenericEvent(eventStart, curOffset - eventStart, "Channel Pressure",
                      fmt::format("Value {:02X}", pressure), Type::Control);
      break;
    }

    case 0xE0: {
      uint8_t lsb = hasPrefetchedData ? firstData : readByte();
      uint8_t msb = readByte();
      addPitchBendMidiFormat(eventStart, curOffset - eventStart, lsb, msb);
      break;
    }

    case 0xF0: {
      if (status == 0xFF) {
        uint8_t metaType = hasPrefetchedData ? firstData : readByte();
        switch (metaType) {
          case 0x2D: {
            for (int i = 0; i < 6; i++) {
              (void)readByte();
            }
            addGenericEvent(eventStart, curOffset - eventStart, "Loop End", "", Type::Loop);
            break;
          }
          case 0x2E: {
            (void)readByte();
            (void)readByte();
            addGenericEvent(eventStart, curOffset - eventStart, "Loop Start", "", Type::Loop);
            break;
          }
          case 0x2F: {
            addEndOfTrack(eventStart, curOffset - eventStart);
            return false;
          }
          case 0x51: {
            uint8_t b1 = readByte();
            uint8_t b2 = readByte();
            uint8_t b3 = readByte();
            uint32_t micros = (static_cast<uint32_t>(b1) << 16)
                              | (static_cast<uint32_t>(b2) << 8)
                              | b3;
            addTempo(eventStart, curOffset - eventStart, micros);
            break;
          }
          default: {
            addGenericEvent(eventStart, curOffset - eventStart,
                            fmt::format("Meta {:02X}", metaType), "", Type::Unrecognized);
            break;
          }
        }
      } else {
        addUnknown(eventStart, curOffset - eventStart, "System Event");
      }
      break;
    }

    default:
      addUnknown(eventStart, curOffset - eventStart);
      return false;
  }

  return true;
}
