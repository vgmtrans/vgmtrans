#include "PSDSESeq.h"
#include <cstdint>
#include "PSDSEFormat.h"

PSDSESeq::PSDSESeq(RawFile *file, uint32_t offset, uint32_t length, std::string name)
    : VGMSeq(PSDSEFormat::name, file, offset, length, name) {
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

  // "smdl" magic
  if (readWordBE(curOffset) != 0x736D646C) {
    return false;
  }

  VGMHeader *header = addHeader(curOffset, 0x40, "SMDL Header");
  header->addChild(curOffset, 4, "Magic");
  header->addChild(curOffset + 0x08, 4, "File Length");
  version = readShort(curOffset + 0x0C);
  header->addChild(curOffset + 0x0C, 2, "Version");
  header->addChild(curOffset + 0x0E, 2, "Bank ID");
  char fname[17];
  readBytes(curOffset + 0x20, 16, fname);
  fname[16] = '\0';
  std::string intName = std::string(fname);
  if (!intName.empty()) {
    setName(intName);
  }
  header->addChild(curOffset + 0x20, 16, "File Name");

  // After the header, there should be a "song" chunk
  curOffset += 0x40;
  if (readWordBE(curOffset) == 0x736F6E67) {  // "song"
    uint32_t songChunkLen = 0x10;
    VGMHeader *songChunk = addHeader(curOffset, songChunkLen, "Song Chunk");
    songChunk->addChild(curOffset, 4, "Label");
    // The remaining fields of the song chunk header are unknown,
    // seen in the wild as 0x1000000, 0xFF10, 0xFFFFFFB0
    curOffset += 0x10;

    // The "seqinfo" struct follows, length depends on version
    if (version == 0x0415) {
      VGMHeader *seqInfo = addHeader(curOffset, 0x30, "Seq Info");
      seqInfo->addChild(curOffset + 0x02, 2, "Offset to Tracks");
      seqInfo->addChild(curOffset + 0x06, 1, "Num Tracks");
      // The rest of seqinfo is unknown,
      // but examples in the wild all look the same.
      // Note that we don't use the offset field above, we just scan for "trk " chunks
      // because we can.
      curOffset += 0x30;
    } else if (version == 0x0402) {
      addHeader(curOffset, 0x10, "Seq Info");
      // Similar story here
      curOffset += 0x10;
    }
  }

  return true;
}

bool PSDSESeq::parseTrackPointers(void) {
  uint32_t curOffset = offset() + 0x40;  // Skip SMDL header
  uint32_t eof = offset() + length();
  if (length() == 0) {
    eof = readWord(offset() + 0x08) + offset();
  }

  while (curOffset < eof) {
    uint32_t chunkType = readWordBE(curOffset);
    if (chunkType == 0x74726B20) {  // "trk "
      uint32_t chunkLen = readWord(curOffset + 0x0C);

      PSDSETrack *track = new PSDSETrack(this, curOffset + 0x10, chunkLen);
      // Assign channel based on track index (0-15).
      // TODO: Check if this is correct
      track->channel = aTracks.size() % 16;
      aTracks.push_back(track);

      // Skip to next chunk, aligned to 4 bytes
      curOffset += 0x10 + chunkLen;
      if ((chunkLen % 4) != 0) {
        curOffset += (4 - (chunkLen % 4));
      }

    } else {
      curOffset += 4;
    }
  }

  return !aTracks.empty();
}

// ************
// PSDSETrack
// ************

PSDSETrack::PSDSETrack(PSDSESeq *parentSeq, long offset, long length)
    : SeqTrack(parentSeq, offset, length) {
  resetVars();
}

void PSDSETrack::resetVars(void) {
  SeqTrack::resetVars();
  // Best-guess default values
  currentOctave = 4;
  lastNoteDuration = 48;
  pitchBendRangeSemitones = 2;
  lastWaitDuration = 0;
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

    addNoteByDur(beginOffset, curOffset - beginOffset, key, velocity, duration);
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
      int8_t ticks = readByte(curOffset++);
      (void)ticks;
      addUnknown(beginOffset, curOffset - beginOffset, "Repeat Last Wait + Ticks");
      // addTime(lastWait + ticks); seems unneeded?
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
      int8_t tickInterval = readByte(curOffset++);
      (void)tickInterval;
      // Pause until all notes are released and check at the tick rate specified by  tickInterval
      addUnknown(beginOffset, curOffset - beginOffset, "Pause until all notes are released");
    } break;
    case 0x98:
      addEndOfTrack(beginOffset, curOffset - beginOffset);
      return false;
    case 0x99:
      // TODO: Actually implement loop points
      addGenericEvent(beginOffset, curOffset - beginOffset, "Loop Point", "",
                      VGMItem::Type::Marker);
      break;
    case 0x9C: {
      uint8_t nRepeats = readByte(curOffset++);
      (void)nRepeats;
      addUnknown(beginOffset, curOffset - beginOffset, "Segno");
    } break;
    case 0x9D: {
      addUnknown(beginOffset, curOffset - beginOffset, "Dal Segno");
    } break;
    case 0x9E: {
      addUnknown(beginOffset, curOffset - beginOffset, "To Coda");
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
      uint8_t swdl = readByte(curOffset++);
      uint8_t bank = readByte(curOffset++);
      (void)swdl;
      (void)bank;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Set SWDL and Bank", "",
                      VGMItem::Type::Unknown);
    } break;
    case 0xA9: {
      uint8_t bankHi = readByte(curOffset++);
      addBankSelectNoItem(bankHi * 128);  // Approximation?
      addGenericEvent(beginOffset, curOffset - beginOffset, "Set Bank Hi", "",
                      VGMItem::Type::Unknown);
    } break;
    case 0xAA: {
      uint8_t bankLo = readByte(curOffset++);
      (void)bankLo;
      // TODO: VGMSeq doesn't have this
      addGenericEvent(beginOffset, curOffset - beginOffset, "Set Bank Lo", "",
                      VGMItem::Type::Unknown);
    } break;
    case 0xAB: {
      // No-op
      addGenericEvent(beginOffset, curOffset - beginOffset, "No-op", "", VGMItem::Type::Unknown);
    } break;
    case 0xAC:  // Program Change
    {
      uint8_t prog = readByte(curOffset++);
      addProgramChange(beginOffset, curOffset - beginOffset, prog);
    } break;
    case 0xB2:  // Loop Point (Dal Segno)
      addGenericEvent(beginOffset, curOffset - beginOffset, "Loop Point", "",
                      VGMItem::Type::Marker);
      break;
    case 0xB3:  // Fine Loop
      addGenericEvent(beginOffset, curOffset - beginOffset, "Fine Loop", "", VGMItem::Type::Marker);
      break;

    case 0xC0:  // Pan (Often same as E8?)
    {
      uint8_t pan = readByte(curOffset++);
      addPan(beginOffset, curOffset - beginOffset, pan);
    } break;
    case 0xC3:  // Transpose
    {
      int8_t transp = readByte(curOffset++);
      addTranspose(beginOffset, curOffset - beginOffset, transp);
    } break;
    case 0xC4:  // Pitch Bend Range
    {
      uint8_t range = readByte(curOffset++);
      addPitchBendRange(beginOffset, curOffset - beginOffset, range * 100);
    } break;
    case 0xC5:      // Mono/Poly?
      curOffset++;  // 1 byte param
      addGenericEvent(beginOffset, curOffset - beginOffset, "Mono/Poly Mode", "",
                      VGMItem::Type::Unknown);
      break;

    case 0xC8:      // Random
      curOffset++;  // 1 byte param
      addGenericEvent(beginOffset, curOffset - beginOffset, "Random", "", VGMItem::Type::Unknown);
      break;

    case 0xCA:  // Variable
      curOffset +=
          2;  // 2 byte param? Check docs. Assuming 2 based on other complex ops or just generic.
      // Actually commonly 2 bytes: Op, Value.
      addGenericEvent(beginOffset, curOffset - beginOffset, "Variable Op", "",
                      VGMItem::Type::Unknown);
      break;

    case 0xD0:  // Tie
      addGenericEvent(beginOffset, curOffset - beginOffset, "Tie", "", VGMItem::Type::Unknown);
      break;
    case 0xD1:  // Portamento
    {
      uint8_t val = readByte(curOffset++);
      addPortamento(beginOffset, curOffset - beginOffset, val != 0);
      addPortamentoTime(beginOffset, curOffset - beginOffset,
                        val);  // Reuse val as time? Usually it's boolean or time. DSE specific.
    } break;
    case 0xD2:  // Modulation (Vibrato)
    {
      uint8_t depth = readByte(curOffset++);
      addModulation(beginOffset, curOffset - beginOffset, depth);
    } break;

    case 0xD4:  // Envelope Override
    {
      curOffset += 5;  // A, D, S, R, ?
      addGenericEvent(beginOffset, curOffset - beginOffset, "Envelope Override", "",
                      VGMItem::Type::Unknown);
    } break;

    case 0xD8:      // LFO
      curOffset++;  // 1 byte?
      addGenericEvent(beginOffset, curOffset - beginOffset, "LFO Config", "",
                      VGMItem::Type::Unknown);
      break;

    case 0xD7:  // Pitch Bend
    {
      int16_t bend = getShortBE(curOffset);
      curOffset += 2;

      // "500 == 1 semitone. Negative val, means increase pitch, positive the opposite."
      // So first, invert the sign.
      double semitones = static_cast<double>(-bend) / 500.0;

      // Calculate MIDI deflection based on current pitch bend range
      // deflection = (semitones / range) * 8192
      // If range is 0, deflection is 0.
      double deflection = 0.0;
      if (pitchBendRangeSemitones > 0) {
        deflection = (semitones / static_cast<double>(pitchBendRangeSemitones)) * 8192.0;
      }

      uint16_t midiBend = std::clamp(8192 + static_cast<int>(deflection), 0, 16383);
      uint8_t lo = midiBend & 0x7F;
      uint8_t hi = (midiBend >> 7) & 0x7F;
      addPitchBendMidiFormat(beginOffset, curOffset - beginOffset, lo, hi);
    } break;

    case 0xDB:  // Set Pitch Bend Range
    {
      uint8_t range = readByte(curOffset++);
      pitchBendRangeSemitones = range;
      // Add RPN event to set pitch bend range in the MIDI output
      // range is in semitones
      addPitchBendRange(beginOffset, curOffset - beginOffset, range * 100);
    } break;
    case 0xE0:  // Volume
    {
      uint8_t vol = readByte(curOffset++);
      addVol(beginOffset, curOffset - beginOffset, vol);
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
    case 0xF0:  // Replace LFO
    {
      curOffset += 5;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Replace LFO", "",
                      VGMItem::Type::Unknown);
    } break;
    case 0xF1:  // Set LFO Delay/Fade
    {
      curOffset += 4;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Set LFO Delay/Fade", "",
                      VGMItem::Type::Unknown);
    } break;
    case 0xF2:  // Set LFO Param
    {
      curOffset += 2;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Set LFO Param", "",
                      VGMItem::Type::Unknown);
    } break;
    case 0xF3:  // Set LFO Route
    {
      curOffset += 3;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Set LFO Route", "",
                      VGMItem::Type::Unknown);
    } break;

    case 0xF6:  // Sync/Unknown
    {
      curOffset += 1;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Sync/Unknown (F6)", "",
                      VGMItem::Type::Unknown);
    } break;

    case 0xF8:  // Skip next 2 bytes
    {
      curOffset += 2;
    } break;
    default: {
      addUnknown(beginOffset, curOffset - beginOffset);
    } break;
  }

  return true;
}
