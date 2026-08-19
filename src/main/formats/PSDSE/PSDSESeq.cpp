#include "PSDSESeq.h"
#include <cstdint>
#include "PSDSEFormat.h"

PSDSESeq::PSDSESeq(RawFile *file, uint32_t offset, uint32_t length, std::string name)
    : VGMSeq(PSDSEFormat::name, file, offset, length, name) {
  m_endianness = PSDSE::magicInfo(file->readWordBE(offset)).endianness;
  bLoadTickByTick = true;
  setAllowDiscontinuousTrackData(true);
  setPPQN(48);
}

void PSDSESeq::resetVars(void) {
  VGMSeq::resetVars();
  version = 0;
}

bool PSDSESeq::parseHeader(void) {
  uint32_t curOffset = offset();

  const PSDSE::MagicInfo magic = PSDSE::magicInfo(readWordBE(curOffset));
  if (magic.kind != PSDSE::FileKind::Sequence) {
    return false;
  }
  m_endianness = magic.endianness;
  const uint32_t fileLength = PSDSE::readU32(rawFile(), curOffset + 0x08, m_endianness);
  if (fileLength < 0x40 || fileLength > rawFile()->size() - curOffset) {
    return false;
  }
  setLength(fileLength);

  VGMHeader *header = addHeader(curOffset, 0x40, "SMDL Header");
  header->addChild(curOffset, 4, "Magic");
  header->addChild(curOffset + 0x04, 4, "Reserved");
  header->addChild(curOffset + 0x08, 4, "File Length");
  version = PSDSE::readU16(rawFile(), curOffset + 0x0C, m_endianness);
  header->addChild(curOffset + 0x0C, 2, "Version");
  header->addChild(curOffset + 0x0E, 2, "Bank ID");
  header->addChild(curOffset + 0x10, 8, "Reserved");
  header->addChild(curOffset + 0x18, 2, "DSE Version");
  header->addChild(curOffset + 0x1A, 2, "Reserved");
  header->addChild(curOffset + 0x1C, 2, "Reserved");
  header->addChild(curOffset + 0x1E, 2, "Reserved");
  char fname[17];
  readBytes(curOffset + 0x20, 16, fname);
  fname[16] = '\0';
  std::string intName = std::string(fname);
  if (!intName.empty()) {
    setName(intName);
  }
  header->addChild(curOffset + 0x20, 16, "File Name");
  header->addChild(curOffset + 0x30, 16, "Reserved");

  // After the header, there should be a "song" chunk. A chunk header is 0x10
  // bytes: label (4), version (4, always 0x01000000 LE), alignment byte (1),
  // 0xFF (1), padding (2), chunk length (4, LE). The song chunk's payload is
  // the 0x30-byte "seqinfo" struct.
  curOffset += 0x40;
  if (readWordBE(curOffset) == 0x736F6E67) {  // "song"
    VGMHeader *songChunk = addHeader(curOffset, 0x10, "Song Chunk");
    songChunk->addChild(curOffset, 4, "Label");
    songChunk->addChild(curOffset + 0x04, 4, "Chunk Version");
    songChunk->addChild(curOffset + 0x08, 1, "Alignment");
    songChunk->addChild(curOffset + 0x09, 3, "Reserved");
    songChunk->addChild(curOffset + 0x0C, 4, "Chunk Length");
    curOffset += 0x10;

    // The "seqinfo" struct follows, length depends on version. Field offsets
    // are from DseSequence_LoadSong (0x0206E554).
    if (version == 0x0415) {
      VGMHeader *seqInfo = addHeader(curOffset, 0x30, "Seq Info");
      seqInfo->addChild(curOffset + 0x00, 2, "Bank ID");
      seqInfo->addChild(curOffset + 0x02, 2, "Ticks Per Quarter Beat");
      seqInfo->addChild(curOffset + 0x04, 1, "field_0x11");
      seqInfo->addChild(curOffset + 0x06, 1, "Num Tracks");
      seqInfo->addChild(curOffset + 0x07, 1, "Num Channels");
      seqInfo->addChild(curOffset + 0x18, 1, "Loop Flag");
      seqInfo->addChild(curOffset + 0x19, 1, "Global Volume Index");
      seqInfo->addChild(curOffset + 0x1A, 1, "Effect ID");
      seqInfo->addChild(curOffset + 0x1B, 1, "field_0x12");
      curOffset += 0x30;
    } else if (version == 0x0402) {
      addHeader(curOffset, 0x10, "Seq Info");
      curOffset += 0x10;
    }
  }

  return true;
}

bool PSDSESeq::parseTrackPointers(void) {
  uint32_t curOffset = offset() + 0x40;  // Skip SMDL header
  uint32_t eof = offset() + length();

  while (curOffset < eof) {
    uint32_t chunkType = readWordBE(curOffset);
    if (chunkType == 0x74726B20) {  // "trk "
      uint32_t chunkLen = PSDSE::readU32(rawFile(), curOffset + 0x0C, m_endianness);
      if (chunkLen < 4 || curOffset + 0x10 > eof || chunkLen > eof - (curOffset + 0x10)) {
        return false;
      }

      // The first four data bytes are a track preamble, not sequence events:
      //   [0x10] track ID, [0x11] channel ID, [0x12]/[0x13] reserved.
      // (DseSequence_LoadSong reads [0x10] and [0x11] only.)
      addTrack<PSDSETrack>(this, curOffset + 0x14, chunkLen - 4,
                           readByte(curOffset + 0x11) & 0x0F);

      // Skip to next chunk, aligned to 4 bytes
      curOffset += 0x10 + chunkLen;
      if ((chunkLen % 4) != 0) {
        curOffset += (4 - (chunkLen % 4));
      }

    } else {
      curOffset += 4;
    }
  }

  return hasTracks();
}

// ************
// PSDSETrack
// ************

PSDSETrack::PSDSETrack(PSDSESeq *parentSeq, long offset, long length, uint8_t channel)
    : SeqTrack(parentSeq, offset, length), m_channel(channel) {
  resetVars();
}

void PSDSETrack::setChannelAndGroupFromTrkNum(int trackNum) {
  (void)trackNum;
  channel = m_channel;
  channelGroup = 0;
  if (readMode == READMODE_CONVERT_TO_MIDI) {
    pMidiTrack->setChannelGroup(channelGroup);
  }
}

void PSDSETrack::resetVars(void) {
  SeqTrack::resetVars();
  // Best-guess default values
  currentOctave = 4;
  lastNoteDuration = 48;
  lastWaitDuration = 0;
  noteDurationMultiplier = 127;
}

bool PSDSETrack::readEvent(void) {
  uint32_t beginOffset = curOffset;
  if (curOffset >= offset() + length()) {
    return false;
  }

  uint8_t status_byte = readByte(curOffset++);

  // Duration table for 0x80-0x8F delay events (48 PPQN)
  static const uint8_t PsdseDurTable[] = {96, 72, 64, 48, 36, 32, 24, 18, 16, 12, 9, 8, 6, 4, 3, 2};

  if (status_byte <= 0x7F) {
    // Play Note Event
    // 0x00-0x7F: status_byte IS the velocity.
    uint8_t velocity = status_byte;

    // Variable length encoded note flags
    uint8_t noteFlagByte = readByte(curOffset++);
    uint8_t highNybble = (noteFlagByte & 0xF0) >> 4;
    uint8_t lowNybble = (noteFlagByte & 0x0F);

    uint8_t numDurBytes = (highNybble & 0xC) >> 2;

    int8_t octMod = (highNybble & 0x3) - 2;
    currentOctave += octMod;

    uint32_t duration = 0;
    if (numDurBytes > 0) {
      for (int i = 0; i < numDurBytes; i++) {
        duration = (duration << 8) | readByte(curOffset++);
      }
      lastNoteDuration = duration;
    } else {
      duration = lastNoteDuration;
    }

    uint8_t noteIdx = lowNybble;
    uint8_t key = currentOctave * 12 + noteIdx;

    // DSE scales each note's duration by the track's note-duration multiplier
    // (opcode 0xBC, default 127). The engine computes (all 32-bit arithmetic):
    //   scaled = rawDuration * noteDurationMultiplier
    //   hi     = (scaled * 0x02040811) >> 32
    //   ticks  = (hi + ((scaled - hi) >> 1)) >> 6
    // 0x02040811 is the fixed-point reciprocal of 127, so this is
    // (rawDuration * multiplier / 127), which is the identity at the default.
    const uint64_t product = static_cast<uint64_t>(duration) * noteDurationMultiplier;
    const uint32_t scaled = static_cast<uint32_t>(product);
    const uint32_t hi = static_cast<uint32_t>((product * 0x02040811ULL) >> 32);
    const uint32_t noteDuration = (hi + ((scaled - hi) >> 1)) >> 6;

    addNoteByDur(beginOffset, curOffset - beginOffset, key, velocity, noteDuration);
    // Note: Do NOT addTime(duration) here, Wait events handle that

    return true;
  }

  // 0x80-0x8F: Wait Event
  if (status_byte >= 0x80 && status_byte <= 0x8F) {
    uint8_t durIdx = status_byte & 0x0F;
    uint32_t duration = PsdseDurTable[durIdx];
    lastWaitDuration = duration;
    addGenericEvent(beginOffset, curOffset - beginOffset, "Wait", "", VGMItem::Type::Rest);
    addTime(duration);
    return true;
  }

  switch (status_byte) {
    case 0x90:
      addGenericEvent(beginOffset, curOffset - beginOffset, "Repeat Last Wait", "",
                      VGMItem::Type::Rest);
      addTime(lastWaitDuration);
      break;
    case 0x91: {
      const int8_t ticks = static_cast<int8_t>(readByte(curOffset++));
      const int64_t adjusted = static_cast<int64_t>(lastWaitDuration) + ticks;
      lastWaitDuration = static_cast<uint32_t>(std::max<int64_t>(0, adjusted));
      addGenericEvent(beginOffset, curOffset - beginOffset, "Repeat Last Wait + Ticks", "",
                      VGMItem::Type::Rest);
      addTime(lastWaitDuration);
    } break;
    case 0x92: {
      uint8_t ticks = readByte(curOffset++);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Wait (8-bit)", "",
                      VGMItem::Type::Rest);
      lastWaitDuration = ticks;
      addTime(ticks);
    } break;
    case 0x93: {
      uint16_t ticks = readShort(curOffset);
      curOffset += 2;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Wait (16-bit)", "",
                      VGMItem::Type::Rest);
      lastWaitDuration = ticks;
      addTime(ticks);
    } break;
    case 0x94: {
      uint32_t ticks =
          readByte(curOffset) | (readByte(curOffset + 1) << 8) | (readByte(curOffset + 2) << 16);
      curOffset += 3;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Wait (24-bit)", "",
                      VGMItem::Type::Rest);
      lastWaitDuration = ticks;
      addTime(ticks);
    } break;
    case 0x95: {
      // Wait Until Fadeout: pause until all notes have been released, checking
      // every `tickInterval` ticks. The interval byte has no MIDI equivalent.
      curOffset++;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Wait Until Fadeout", "",
                      VGMItem::Type::Rest);
    } break;
    case 0x98:
      addEndOfTrack(beginOffset, curOffset - beginOffset);
      return false;
    case 0x99:
      addGenericEvent(beginOffset, curOffset - beginOffset, "Main Loop Begin", "",
                      VGMItem::Type::Marker);
      break;
    case 0x9C: {
      curOffset++;  // sub-loop repeat count
      addGenericEvent(beginOffset, curOffset - beginOffset, "Sub Loop Begin", "",
                      VGMItem::Type::Marker);
    } break;
    case 0x9D: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "Sub Loop End", "",
                      VGMItem::Type::Marker);
    } break;
    case 0x9E: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "Sub Loop Break On Last Iteration", "",
                      VGMItem::Type::Marker);
    } break;
    case 0xA0: {
      uint8_t octave = readByte(curOffset++);
      currentOctave = octave;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Set Octave", "",
                      VGMItem::Type::Unknown);
    } break;
    case 0xA1: {
      int8_t octDiff = readByte(curOffset++);
      currentOctave += octDiff;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Add Octave", "",
                      VGMItem::Type::Unknown);
    } break;
    case 0xA4:  // Set Tempo
    case 0xA5: {
      uint8_t bpm = readByte(curOffset++);
      addTempoBPM(beginOffset, curOffset - beginOffset, bpm);
    } break;
    case 0xA8: {
      // 16-bit bank (SWDL) id, big-endian: (byte0 << 8) | byte1.
      curOffset += 2;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Set Bank", "",
                      VGMItem::Type::Unknown);
    } break;
    case 0xA9: {
      curOffset++;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Set Bank MSB", "",
                      VGMItem::Type::Unknown);
    } break;
    case 0xAA: {
      curOffset++;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Set Bank LSB", "",
                      VGMItem::Type::Unknown);
    } break;
    case 0xAB: {
      curOffset++;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Dummy Byte", "",
                      VGMItem::Type::Unknown);
    } break;
    case 0xAC:  // Program Change
    {
      uint8_t prog = readByte(curOffset++);
      addProgramChange(beginOffset, curOffset - beginOffset, prog);
    } break;
    case 0xAF:
      curOffset += 3;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Song Volume Fade", "",
                      VGMItem::Type::Unknown);
      break;
    case 0xB0:
      addGenericEvent(beginOffset, curOffset - beginOffset, "Restore Envelope Defaults", "",
                      VGMItem::Type::Unknown);
      break;
    case 0xB1:
      curOffset++;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Set Envelope Attack Begin", "",
                      VGMItem::Type::Unknown);
      break;
    case 0xB2:
      curOffset++;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Set Envelope Attack Time", "",
                      VGMItem::Type::Unknown);
      break;
    case 0xB3:
      curOffset++;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Set Envelope Hold Time", "",
                      VGMItem::Type::Unknown);
      break;
    case 0xB4:
      curOffset += 2;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Set Envelope Decay Time And Sustain Level",
                      "", VGMItem::Type::Unknown);
      break;
    case 0xB5:
      curOffset++;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Set Envelope Sustain Time", "",
                      VGMItem::Type::Unknown);
      break;
    case 0xB6:
      curOffset++;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Set Envelope Release Time", "",
                      VGMItem::Type::Unknown);
      break;
    case 0xBC: {
      // Set Note Duration Multiplier: scales subsequent note durations by
      // value/127 (default is 127, i.e. no scaling).
      noteDurationMultiplier = readByte(curOffset++);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Set Note Duration Multiplier", "",
                      VGMItem::Type::Unknown);
    } break;
    case 0xBE: {
      // Force LFO Envelope Level: forces the channel LFO to a constant
      // envelope level (signed byte). No MIDI equivalent.
      curOffset++;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Force LFO Envelope Level", "",
                      VGMItem::Type::Unknown);
    } break;
    case 0xBF:
      curOffset++;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Set Hold Notes", "",
                      VGMItem::Type::Unknown);
      break;
    case 0xC0:
      addGenericEvent(beginOffset, curOffset - beginOffset, "Set Flag Bit 1", "",
                      VGMItem::Type::Unknown);
      break;
    case 0xC3: {
      // Set Optional Volume: a secondary channel volume used only by special
      // driver modes, not the primary track volume (that is opcode 0xE0).
      curOffset++;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Set Optional Volume", "",
                      VGMItem::Type::Unknown);
    } break;
    case 0xCB:
      curOffset += 2;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Dummy 2 Bytes", "",
                      VGMItem::Type::Unknown);
      break;
    case 0xD0:
      curOffset++;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Set Tuning", "",
                      VGMItem::Type::FineTune);
      break;
    case 0xD1:
      curOffset++;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Tuning Delta Coarse", "",
                      VGMItem::Type::FineTune);
      break;
    case 0xD2:
      curOffset++;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Tuning Delta Fine", "",
                      VGMItem::Type::FineTune);
      break;
    case 0xD3:
      curOffset += 2;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Tuning Delta Full", "",
                      VGMItem::Type::FineTune);
      break;
    case 0xD4:
      curOffset += 3;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Tuning Fade", "",
                      VGMItem::Type::FineTune);
      break;
    case 0xD5:
      curOffset += 2;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Set Note Random Region", "",
                      VGMItem::Type::Unknown);
      break;
    case 0xD6:
      curOffset += 2;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Set Tuning Jitter Amplitude", "",
                      VGMItem::Type::Unknown);
      break;
    case 0xD8:  // DseTrackEvent_SetUnknown2: stores a 16-bit value to channel field 0x56
    {
      curOffset += 2;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Set Channel Field 0x56", "",
                      VGMItem::Type::FineTune);
    } break;

    case 0xD7:  // Set Key Bend (a per-key pitch detune, not a MIDI pitch bend)
    {
      curOffset += 2;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Set Key Bend", "",
                      VGMItem::Type::FineTune);
    } break;

    case 0xDB:  // Set Key Bend Range
    {
      curOffset++;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Set Key Bend Range", "",
                      VGMItem::Type::FineTune);
    } break;
    case 0xDC:  // Setup Key Bend LFO (pitch)
    {
      curOffset += 5;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Setup Key Bend LFO", "",
                      VGMItem::Type::Unknown);
    } break;
    case 0xE4:  // Setup Volume LFO
    {
      curOffset += 5;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Setup Volume LFO", "",
                      VGMItem::Type::Unknown);
    } break;
    case 0xEC:  // Setup Pan LFO
    {
      curOffset += 5;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Setup Pan LFO", "",
                      VGMItem::Type::Unknown);
    } break;
    case 0xDD:  // Setup Key Bend LFO Envelope
    {
      curOffset += 4;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Setup Key Bend LFO Envelope", "",
                      VGMItem::Type::Unknown);
    } break;
    case 0xE5:  // Setup Volume LFO Envelope
    {
      curOffset += 4;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Setup Volume LFO Envelope", "",
                      VGMItem::Type::Unknown);
    } break;
    case 0xED:  // Setup Pan LFO Envelope
    {
      curOffset += 4;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Setup Pan LFO Envelope", "",
                      VGMItem::Type::Unknown);
    } break;
    case 0xDF:  // Use Key Bend LFO
    {
      curOffset++;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Use Key Bend LFO", "",
                      VGMItem::Type::Unknown);
    } break;
    case 0xE7:  // Use Volume LFO
    {
      curOffset++;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Use Volume LFO", "",
                      VGMItem::Type::Unknown);
    } break;
    case 0xEF:  // Use Pan LFO
    {
      curOffset++;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Use Pan LFO", "",
                      VGMItem::Type::Unknown);
    } break;
    case 0xE0:  // Volume
    {
      uint8_t vol = readByte(curOffset++);
      addVol(beginOffset, curOffset - beginOffset, vol);
    } break;
    case 0xE1:  // Volume Delta
    {
      curOffset++;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Volume Delta", "",
                      VGMItem::Type::Unknown);
    } break;
    case 0xE9:  // Pan Delta
    {
      curOffset++;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Pan Delta", "",
                      VGMItem::Type::Unknown);
    } break;
    case 0xE2:  // Volume Fade
    {
      curOffset += 3;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Volume Fade", "",
                      VGMItem::Type::Unknown);
    } break;
    case 0xEA:  // Pan Fade
    {
      curOffset += 3;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Pan Fade", "",
                      VGMItem::Type::Unknown);
    } break;
    case 0xE3:  // Expression
    {
      uint8_t expr = readByte(curOffset++);
      addExpression(beginOffset, curOffset - beginOffset, expr);
    } break;
    case 0xE8:  // Pan
    {
      uint8_t pan = readByte(curOffset++);
      addPan(beginOffset, curOffset - beginOffset, pan);
    } break;
    case 0xF0:  // Setup LFO (generic, selected by channel lfo index)
    {
      curOffset += 5;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Setup LFO", "",
                      VGMItem::Type::Unknown);
    } break;
    case 0xF1:  // Setup LFO Envelope
    {
      curOffset += 4;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Setup LFO Envelope", "",
                      VGMItem::Type::Unknown);
    } break;
    case 0xF2:  // Set LFO Parameter
    {
      curOffset += 2;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Set LFO Parameter", "",
                      VGMItem::Type::Unknown);
    } break;
    case 0xF3:  // Use LFO
    {
      curOffset += 3;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Use LFO", "",
                      VGMItem::Type::Unknown);
    } break;

    case 0xF6:  // Signal
    {
      curOffset++;  // signal id
      addGenericEvent(beginOffset, curOffset - beginOffset, "Signal", "",
                      VGMItem::Type::Unknown);
    } break;

    case 0xF8:  // Dummy 2 Bytes
    {
      curOffset += 2;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Dummy 2 Bytes", "",
                      VGMItem::Type::Unknown);
    } break;
    default: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "Invalid Opcode", "",
                      VGMItem::Type::Unknown);
    } break;
  }

  return true;
}
