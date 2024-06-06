/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "KonamiPS1Seq.h"

#include <sstream>
#include <spdlog/fmt/fmt.h>

DECLARE_FORMAT(KonamiPS1);

//  ************
//  KonamiPS1Seq
//  ************

KonamiPS1Seq::KonamiPS1Seq(RawFile *file, uint32_t offset, const std::string &name)
    : VGMSeq(KonamiPS1Format::name, file, offset, kHeaderSize + file->GetWord(offset + 4), name) {
  bLoadTickByTick = true;

  UseReverb();
}

void KonamiPS1Seq::ResetVars() {
  VGMSeq::ResetVars();
}

bool KonamiPS1Seq::GetHeaderInfo() {
  if (!IsKDT1Seq(rawFile(), dwOffset)) {
    return false;
  }

  VGMHeader *header = addHeader(dwOffset, kHeaderSize);
  header->AddSig(dwOffset, 4);
  header->addChild(dwOffset + kOffsetToFileSize, 4, "Size");
  header->addChild(dwOffset + kOffsetToTimebase, 4, "Timebase");
  SetPPQN(GetWord(dwOffset + kOffsetToTimebase));
  header->addChild(dwOffset + kOffsetToTrackCount, 4, "Number Of Tracks");

  uint32_t numTracks = GetWord(dwOffset + kOffsetToTrackCount);
  VGMHeader *trackSizeHeader = addHeader(dwOffset + kHeaderSize, 2 * numTracks, "Track Size");
  for (uint32_t trackIndex = 0; trackIndex < numTracks; trackIndex++) {
    std::string itemName = fmt::format("Track {} size", trackIndex + 1);
    trackSizeHeader->addChild(trackSizeHeader->dwOffset + (trackIndex * 2), 2, itemName);
  }

  return true;
}

bool KonamiPS1Seq::GetTrackPointers() {
  uint32_t numTracks = GetWord(dwOffset + kOffsetToTrackCount);
  uint32_t trackStart = dwOffset + kHeaderSize + (numTracks * 2);
  for (uint32_t trackIndex = 0; trackIndex < numTracks; trackIndex++) {
    uint16_t trackSize = GetShort(dwOffset + kHeaderSize + (trackIndex * 2));
    KonamiPS1Track *track = new KonamiPS1Track(this, trackStart, trackSize);
    aTracks.push_back(track);
    trackStart += trackSize;
  }

  return true;
}

bool KonamiPS1Seq::IsKDT1Seq(RawFile *file, uint32_t offset) {
  if (offset + kHeaderSize >= file->size()) {
    return false;
  }

  if (file->GetByte(offset) != 'K' || file->GetByte(offset + 1) != 'D' ||
      file->GetByte(offset + 2) != 'T' || file->GetByte(offset + 3) != '1') {
    return false;
  }

  uint32_t dataSize = file->GetWord(offset + kOffsetToFileSize);
  uint32_t fileSize = kHeaderSize + dataSize;
  if (offset + fileSize >= file->size()) {
    return false;
  }

  uint32_t numTracks = file->GetWord(offset + kOffsetToTrackCount);
  if (numTracks == 0 || offset + kHeaderSize + (numTracks * 2) >= file->size()) {
    return false;
  }

  uint32_t trackSize = 0;
  for (size_t trackIndex = 0; trackIndex < numTracks; trackIndex++) {
    trackSize += file->GetShort(offset + kHeaderSize + (trackIndex * 2));
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
  ResetVars();
  // bDetermineTrackLengthEventByEvent = true;
  // bWriteGenericEventAsTextEvent = true;
}

void KonamiPS1Track::ResetVars() {
  SeqTrack::ResetVars();

  skipDeltaTime = false;
}

bool KonamiPS1Track::ReadEvent() {
  KonamiPS1Seq *parentSeq = (KonamiPS1Seq *)this->parentSeq;

  uint32_t beginOffset = curOffset;
  if (curOffset >= vgmFile()->GetEndOffset()) {
    return false;
  }

  if (!skipDeltaTime) {
    uint32_t delta = ReadVarLen(curOffset);
    AddTime(delta);

    std::stringstream description;
    description << "Duration: " << delta;
    AddGenericEvent(beginOffset, curOffset - beginOffset, "Delta Time",
      description.str(), CLR_REST, ICON_REST);

    skipDeltaTime = true;
    return true;
  }

  uint8_t statusByte = GetByte(curOffset++);
  uint8_t command = statusByte & 0x7f;
  bool note = (statusByte & 0x80) == 0;

  bool bContinue = true;
  if (note) {
    uint8_t noteNumber = command;
    uint8_t paramByte = GetByte(curOffset++);
    uint8_t velocity = paramByte & 0x7f;
    skipDeltaTime = (paramByte & 0x80) != 0;

    AddNoteOn(beginOffset, curOffset - beginOffset, noteNumber, velocity);
    prevKey = noteNumber;
    prevVel = velocity;
  } else {
    std::stringstream description;

    uint8_t paramByte;
    if (command == 0x4a) {
      // Note Off (Reset Running Status)
      skipDeltaTime = false;
    } else if (command == 0x4b) {
      // Note Off (Sustain Running Status)
      skipDeltaTime = true;  // has already set? Nobody cares!
    } else {
      // Commands excluding note off are two bytes length.
      paramByte = GetByte(curOffset++);
      skipDeltaTime = (paramByte & 0x80) != 0;
      paramByte &= 0x7f;
    }

    switch (command) {
      case 1:
        AddModulation(beginOffset, curOffset - beginOffset, paramByte);
        break;

      case 6:
        description << "Parameter: " << paramByte;
        AddGenericEvent(beginOffset, curOffset - beginOffset, "NRPN Data Entry",
          description.str(), CLR_MISC);
        if (readMode == READMODE_CONVERT_TO_MIDI) {
          pMidiTrack->AddControllerEvent(channel, command, paramByte);
        }
        break;

      case 7:
        AddVol(beginOffset, curOffset - beginOffset, paramByte);
        break;

      case 10:
        AddPan(beginOffset, curOffset - beginOffset, paramByte);
        break;

      case 11:
        AddExpression(beginOffset, curOffset - beginOffset, paramByte);
        break;

      case 15:
        description << "Parameter: " << paramByte;
        AddGenericEvent(beginOffset, curOffset - beginOffset, "Stereo Widening (?)",
                        description.str(), CLR_PAN, ICON_CONTROL);
        if (readMode == READMODE_CONVERT_TO_MIDI) {
          pMidiTrack->AddControllerEvent(channel, command, paramByte);
        }
        break;

      case 64:
        description << "Parameter: " << paramByte;
        AddSustainEvent(beginOffset, curOffset - beginOffset, paramByte);
        break;

      case 70:
        description << "Parameter: " << paramByte;
        AddGenericEvent(beginOffset, curOffset - beginOffset, "Set Channe", description.str(),
                        CLR_PAN, ICON_CONTROL);
        if (readMode == READMODE_CONVERT_TO_MIDI) {
          pMidiTrack->AddControllerEvent(channel, command, paramByte);
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
        AddTempoBPM(beginOffset, curOffset - beginOffset, bpm,
                    "Tempo (10-255 BPM, divisible by two)");
        break;
      }

      case 72:
        AddPitchBend(beginOffset, curOffset - beginOffset,
                     static_cast<int16_t>(paramByte << 7) - 8192);
        break;

      case 73:
        AddProgramChange(beginOffset, curOffset - beginOffset, paramByte);
        break;

      case 74:
        // Not sure how it will work for a chord (polyphonic track)
        AddNoteOff(beginOffset, curOffset - beginOffset, prevKey,
                   "Note Off (Reset Running Status)");
        break;

      case 75:
        // Not sure how it will work for a chord (polyphonic track)
        AddNoteOff(beginOffset, curOffset - beginOffset, prevKey,
                   "Note Off (Sustain Running Status)");
        break;

      case 76:
        AddTempoBPM(beginOffset, curOffset - beginOffset, paramByte, "Tempo (0-127 BPM)");
        break;

      case 77:
        AddTempoBPM(beginOffset, curOffset - beginOffset, paramByte + 128, "Tempo (128-255 BPM)");
        break;

      case 91:
        AddReverb(beginOffset, curOffset - beginOffset, paramByte);
        break;

      case 99:
        description << "Parameter: " << paramByte;
        AddGenericEvent(beginOffset, curOffset - beginOffset, "NRPN (LSB)", description.str(),
                        CLR_MISC);
        if (readMode == READMODE_CONVERT_TO_MIDI) {
          pMidiTrack->AddControllerEvent(channel, command, paramByte);
        }
        break;

      case 100:
        if (paramByte == 20) {
          AddGenericEvent(beginOffset, curOffset - beginOffset, "Loop Start", "", CLR_LOOP);
        } else if (paramByte == 30) {
          AddGenericEvent(beginOffset, curOffset - beginOffset, "Loop End", "", CLR_LOOP);
        } else {
          description << "Parameter: " << paramByte;
          AddGenericEvent(beginOffset, curOffset - beginOffset, "NRPN (LSB)", description.str(),
                          CLR_MISC);
        }
        if (readMode == READMODE_CONVERT_TO_MIDI) {
          pMidiTrack->AddControllerEvent(channel, command, paramByte);
        }
        break;

      case 118:
        description << "Parameter: " << paramByte;
        AddGenericEvent(beginOffset, curOffset - beginOffset, "Seq Beat", description.str(),
                        CLR_MISC);
        if (readMode == READMODE_CONVERT_TO_MIDI) {
          pMidiTrack->AddControllerEvent(channel, command, paramByte);
        }
        break;

      case 127:
        AddEndOfTrack(beginOffset, curOffset - beginOffset);
        bContinue = false;
        break;

      default:
        AddUnknown(beginOffset, curOffset - beginOffset);
        if (readMode == READMODE_CONVERT_TO_MIDI) {
          pMidiTrack->AddControllerEvent(channel, command, paramByte);
        }

        // bContinue = false;
        break;
    }
  }

  return bContinue;
}
