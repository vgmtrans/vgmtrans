/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "KonamiPS1Seq.h"

#include <spdlog/fmt/fmt.h>

DECLARE_FORMAT(KonamiPS1);

//  ************
//  KonamiPS1Seq
//  ************

KonamiPS1Seq::KonamiPS1Seq(RawFile *file, uint32_t offset, const std::string &name)
    : VGMSeq(KonamiPS1Format::name, file, offset, kHeaderSize + file->readWord(offset + 4), name) {
  bLoadTickByTick = true;

  useReverb();
}

void KonamiPS1Seq::resetVars() {
  VGMSeq::resetVars();
}

bool KonamiPS1Seq::parseHeader() {
  if (!isKDT1Seq(rawFile(), dwOffset)) {
    return false;
  }

  VGMHeader *header = addHeader(dwOffset, kHeaderSize);
  header->addSig(dwOffset, 4);
  header->addChild(dwOffset + kOffsetToFileSize, 4, "Size");
  header->addChild(dwOffset + kOffsetToTimebase, 4, "Timebase");
  setPPQN(readWord(dwOffset + kOffsetToTimebase));
  header->addChild(dwOffset + kOffsetToTrackCount, 4, "Number Of Tracks");

  uint32_t numTracks = readWord(dwOffset + kOffsetToTrackCount);
  VGMHeader *trackSizeHeader = addHeader(dwOffset + kHeaderSize, 2 * numTracks, "Track Size");
  for (uint32_t trackIndex = 0; trackIndex < numTracks; trackIndex++) {
    std::string itemName = fmt::format("Track {} size", trackIndex + 1);
    trackSizeHeader->addChild(trackSizeHeader->dwOffset + (trackIndex * 2), 2, itemName);
  }

  return true;
}

bool KonamiPS1Seq::parseTrackPointers() {
  uint32_t numTracks = readWord(dwOffset + kOffsetToTrackCount);
  uint32_t trackStart = dwOffset + kHeaderSize + (numTracks * 2);
  for (uint32_t trackIndex = 0; trackIndex < numTracks; trackIndex++) {
    uint16_t trackSize = readShort(dwOffset + kHeaderSize + (trackIndex * 2));
    KonamiPS1Track *track = new KonamiPS1Track(this, trackStart, trackSize);
    aTracks.push_back(track);
    trackStart += trackSize;
  }

  return true;
}

bool KonamiPS1Seq::isKDT1Seq(RawFile *file, uint32_t offset) {
  if (offset + kHeaderSize >= file->size()) {
    return false;
  }

  if (file->readByte(offset) != 'K' || file->readByte(offset + 1) != 'D' ||
      file->readByte(offset + 2) != 'T' || file->readByte(offset + 3) != '1') {
    return false;
  }

  uint32_t dataSize = file->readWord(offset + kOffsetToFileSize);
  uint32_t fileSize = kHeaderSize + dataSize;
  if (offset + fileSize >= file->size()) {
    return false;
  }

  uint32_t numTracks = file->readWord(offset + kOffsetToTrackCount);
  if (numTracks == 0 || offset + kHeaderSize + (numTracks * 2) >= file->size()) {
    return false;
  }

  uint32_t trackSize = 0;
  for (size_t trackIndex = 0; trackIndex < numTracks; trackIndex++) {
    trackSize += file->readShort(offset + kHeaderSize + (trackIndex * 2));
  }

  if (offset + trackSize >= file->size()) {
    return false;
  }
  if (trackSize > fileSize) {
    return false;
  }

  return true;
}

//  ***************
//  KonamiPS1Track
//  ***************

KonamiPS1Track::KonamiPS1Track(KonamiPS1Seq *parentFile, uint32_t offset, uint32_t length)
    : SeqTrack(parentFile, offset, length) {
  resetVars();
  // bDetermineTrackLengthEventByEvent = true;
  // bWriteGenericEventAsTextEvent = true;
}

void KonamiPS1Track::resetVars() {
  SeqTrack::resetVars();

  skipDeltaTime = false;
}

bool KonamiPS1Track::readEvent() {
  KonamiPS1Seq *parentSeq = (KonamiPS1Seq *)this->parentSeq;

  uint32_t beginOffset = curOffset;
  if (curOffset >= vgmFile()->endOffset()) {
    return false;
  }

  if (!skipDeltaTime) {
    uint32_t delta = readVarLen(curOffset);
    addTime(delta);

    std::string description = fmt::format("Duration: {:d}", delta);
    addGenericEvent(beginOffset, curOffset - beginOffset, "Delta Time",
                    description, Type::Rest);

    skipDeltaTime = true;
    return true;
  }

  uint8_t statusByte = readByte(curOffset++);
  uint8_t command = statusByte & 0x7f;
  bool note = (statusByte & 0x80) == 0;

  bool bContinue = true;
  if (note) {
    uint8_t noteNumber = command;
    uint8_t paramByte = readByte(curOffset++);
    uint8_t velocity = paramByte & 0x7f;
    skipDeltaTime = (paramByte & 0x80) != 0;

    addNoteOn(beginOffset, curOffset - beginOffset, noteNumber, velocity);
    prevKey = noteNumber;
    prevVel = velocity;
  } else {
    std::string description;

    uint8_t paramByte;
    if (command == 0x4a) {
      // Note Off (Reset Running Status)
      skipDeltaTime = false;
    } else if (command == 0x4b) {
      // Note Off (Sustain Running Status)
      skipDeltaTime = true;  // has already set? Nobody cares!
    } else {
      // Commands excluding note off are two bytes length.
      paramByte = readByte(curOffset++);
      skipDeltaTime = (paramByte & 0x80) != 0;
      paramByte &= 0x7f;
    }

    switch (command) {
      case 1:
        addModulation(beginOffset, curOffset - beginOffset, paramByte);
        break;

      case 6:
        description = fmt::format("Parameter: {:d}", paramByte);
        addGenericEvent(beginOffset, curOffset - beginOffset, "NRPN Data Entry",
                        description, Type::Misc);
        if (readMode == READMODE_CONVERT_TO_MIDI) {
          pMidiTrack->addControllerEvent(channel, command, paramByte);
        }
        break;

      case 7:
        addVol(beginOffset, curOffset - beginOffset, paramByte);
        break;

      case 10:
        addPan(beginOffset, curOffset - beginOffset, paramByte);
        break;

      case 11:
        addExpression(beginOffset, curOffset - beginOffset, paramByte);
        break;

      case 15:
        description = fmt::format("Parameter: {:d}", paramByte);
        addGenericEvent(beginOffset, curOffset - beginOffset, "Stereo Widening (?)",
                        description, Type::Pan);
        if (readMode == READMODE_CONVERT_TO_MIDI) {
          pMidiTrack->addControllerEvent(channel, command, paramByte);
        }
        break;

      case 64:
        addSustainEvent(beginOffset, curOffset - beginOffset, paramByte);
        break;

      case 70:
        description = fmt::format("Parameter: {:d}", paramByte);
        addGenericEvent(beginOffset, curOffset - beginOffset, "Set Channel", description,
                        Type::Pan);
        if (readMode == READMODE_CONVERT_TO_MIDI) {
          pMidiTrack->addControllerEvent(channel, command, paramByte);
        }
        break;

      case 71: {
        // Nisto:
        // There may be issues in calculating accurate tempos for other games that use
        // sequence command 0xC7 to set the tempo, as this command appears to be using a
        // (console-specific?) calculation to compensate for some kind of timing delay(?)
        // Both Silent Hill and Suikoden 2 multiplies the tempo parameter by 2 and adds 2
        // (e.g. (29 * 2) + 2 = 60). However, using this equation (even for Silent Hill or
        // Suikoden 2 themselves), the tempo will still sound off when converted to a
        // standard MIDI and played back on any modern PC.
        //
        // In the KCET driver for Silent Hill 2 (PlayStation 2), the equation was changed to
        // ((x * 2) + 10), which appears to give a more accurate real-world tempo, so I've
        // decided to use it universally.
        uint8_t bpm = static_cast<uint8_t>(std::min<unsigned int>(paramByte * 2 + 10, 255));
        addTempoBPM(beginOffset, curOffset - beginOffset, bpm,
                    "Tempo (10-255 BPM, divisible by two)");
        break;
      }

      case 72:
        addPitchBend(beginOffset, curOffset - beginOffset,
                     static_cast<int16_t>(paramByte << 7) - 8192);
        break;

      case 73:
        addProgramChange(beginOffset, curOffset - beginOffset, paramByte);
        break;

      case 74:
        // Not sure how it will work for a chord (polyphonic track)
        addNoteOff(beginOffset, curOffset - beginOffset, prevKey,
                   "Note Off (Reset Running Status)");
        break;

      case 75:
        // Not sure how it will work for a chord (polyphonic track)
        addNoteOff(beginOffset, curOffset - beginOffset, prevKey,
                   "Note Off (Sustain Running Status)");
        break;

      case 76:
        addTempoBPM(beginOffset, curOffset - beginOffset, paramByte, "Tempo (0-127 BPM)");
        break;

      case 77:
        addTempoBPM(beginOffset, curOffset - beginOffset, paramByte + 128, "Tempo (128-255 BPM)");
        break;

      case 91:
        addReverb(beginOffset, curOffset - beginOffset, paramByte);
        break;

      case 99:
        description = fmt::format("Parameter: {:d}", paramByte);
        addGenericEvent(beginOffset, curOffset - beginOffset, "NRPN (LSB)", description,
                        Type::Misc);
        if (readMode == READMODE_CONVERT_TO_MIDI) {
          pMidiTrack->addControllerEvent(channel, command, paramByte);
        }
        break;

      case 100:
        if (paramByte == 20) {
          addGenericEvent(beginOffset, curOffset - beginOffset, "Loop Start", "", Type::Loop);
        } else if (paramByte == 30) {
          addGenericEvent(beginOffset, curOffset - beginOffset, "Loop End", "", Type::Loop);
        } else {
          description = fmt::format("Parameter: {:d}", paramByte);
          addGenericEvent(beginOffset, curOffset - beginOffset, "NRPN (LSB)", description,
                          Type::Misc);
        }
        if (readMode == READMODE_CONVERT_TO_MIDI) {
          pMidiTrack->addControllerEvent(channel, command, paramByte);
        }
        break;

      case 118:
        description = fmt::format("Parameter: {:d}", paramByte);
        addGenericEvent(beginOffset, curOffset - beginOffset, "Seq Beat", description,
                        Type::Misc);
        if (readMode == READMODE_CONVERT_TO_MIDI) {
          pMidiTrack->addControllerEvent(channel, command, paramByte);
        }
        break;

      case 127:
        addEndOfTrack(beginOffset, curOffset - beginOffset);
        bContinue = false;
        break;

      default:
        addUnknown(beginOffset, curOffset - beginOffset);
        if (readMode == READMODE_CONVERT_TO_MIDI) {
          pMidiTrack->addControllerEvent(channel, command, paramByte);
        }

        // bContinue = false;
        break;
    }
  }

  return bContinue;
}
