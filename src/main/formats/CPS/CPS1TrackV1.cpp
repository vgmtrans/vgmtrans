#include "CPS1TrackV1.h"

CPS1TrackV1::CPS1TrackV1(CPS1Seq *parentSeq, CPSSynth channelSynth, uint32_t offset, uint32_t length)
    : SeqTrack(parentSeq, offset, length), channelSynth(channelSynth) {
  if (channelSynth == CPSSynth::YM2151) {
    synthType = SynthType::YM2151;
  }
  CPS1TrackV1::resetVars();
}

void CPS1TrackV1::resetVars() {
  noteDuration = 0;
  bPrevNoteTie = false;
  prevTieNote = 0;
  curDeltaTable = 0;
  noteState = 0;
  bank = 0;
  progNum = -1;
  cps0transpose = 0;
  restFlag = false;
  extendDeltaFlag = false;
  tieNoteFlag = false;
  shortenDeltaCounter = false;
  portamentoCentsPerSec = 0;
  prevPortamentoDuration = 0;
  memset(loop, 0, sizeof(loop));
  memset(loopOffset, 0, sizeof(loopOffset));
  SeqTrack::resetVars();
}

void CPS1TrackV1::addInitialMidiEvents(int trackNum) {
  SeqTrack::addInitialMidiEvents(trackNum);
  addPortamentoTime14BitNoItem(0);
}

bool CPS1TrackV1::readEvent() {
  u32 beginOffset = curOffset;
  u8 statusByte = readByte(curOffset++);
  auto cpsSeq = static_cast<CPS1Seq*>(parentSeq);
  u8 masterVol = cpsSeq->masterVolume();

  if (statusByte >= 0x40) {
    int shiftAmount = ((statusByte >> 5) & 0x07) - 2;
    shiftAmount = shiftAmount < 0 ? 0 : shiftAmount;
    uint32_t delta = (3 << shiftAmount);

    if (shortenDeltaCounter > 0) {
      delta /= 1.5;
      shortenDeltaCounter--;
    }
    if (extendDeltaFlag) {
      delta += (delta / 2);
      extendDeltaFlag = false;
    }

    u8 absDur = static_cast<u8>(static_cast<u16>(delta * noteDuration) >> 8);
    absDur += 1;

    // We must provide some time between sequential notes to allow the YM2151 envelope to reset.
    // A dumb but effective solution is to shorten note duration by a tick.
    absDur = std::max(1, absDur-1);

    key = (statusByte & 0x1F);
    if (key == 0) {
      if (bPrevNoteTie) {
        addNoteOffNoItem(prevTieNote);
      }
      restFlag = true;
      addRest(beginOffset, curOffset - beginOffset, delta);
    } else {
      key += cps0transpose + instrTranspose;
      while (key > 0x60) { key -= 12; }
      while (key < 0) { key += 12; }
      key += 12;

      // Tie note
      if (tieNoteCounter > 1) {
        if (!bPrevNoteTie) {
          addNoteOn(beginOffset, curOffset - beginOffset, key, masterVol, "Note On (tied / with portamento)");
        }
        else if (key != prevTieNote) {
          addNoteOffNoItem(prevTieNote);
          addNoteOn(beginOffset, curOffset - beginOffset, key, masterVol, "Note On (tied)");
        }
        else
          addGenericEvent(beginOffset, curOffset - beginOffset, "Tie", "", Type::NoteOn);
        bPrevNoteTie = true;
        prevTieNote = key;
        tieNoteCounter--;
      }
      else {
        if (bPrevNoteTie) {
          if (key != prevTieNote) {
            addNoteOffNoItem(prevTieNote);
            addNoteByDur(beginOffset, curOffset - beginOffset, key, masterVol, absDur);
          }
          else {
            addTime(absDur);
            delta -= absDur;
            addNoteOff(beginOffset, curOffset - beginOffset, prevTieNote, "Note Off (tied)");
          }
        }
        else {
          addNoteByDur(beginOffset, curOffset - beginOffset, key, masterVol, absDur);
        }
        bPrevNoteTie = false;
      }

      addTime(delta);
      restFlag = false;
    }

    return true;
  }

  if (statusByte >= 0x20) {
    if (statusByte >= 0x30) {
      shortenDeltaCounter = statusByte & 0xf;
      std::string desc = fmt::format("Shorten next {:d} events", shortenDeltaCounter);
      addGenericEvent(beginOffset, curOffset-beginOffset, desc, "", Type::ChangeState);
    } else {
      tieNoteCounter = (statusByte & 0xf) + 1;
      tieNoteFlag = true;
      std::string desc = fmt::format("Tie next {:d} notes", tieNoteCounter);
      addGenericEvent(beginOffset, curOffset-beginOffset, desc, "", Type::ChangeState);
    }
    return true;
  }
  // statusByte is between 00-1F

  int loopNum;
  switch (statusByte) {
    case 0x00: {  // tempo
      u8 tempoByte = readByte(curOffset++);
      u16 newTempo = (((u8)(tempoByte + 0x80U) >> 3) << 8) | (((u8)(tempoByte + 0x80U) >> 2) << 7);
      const uint32_t micros_per_beat = newTempo << 7;
      addTempo(beginOffset, curOffset - beginOffset, micros_per_beat);

      // if (some status byte == 0) {
      //   some_status_byte = 0;
      // } else {
      //   // seems more uncommon
      //   some_other_status_byte = value;
      // }

      break;
    }
    case 0x01: {
      noteDuration = readByte(curOffset++);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Set Duration", "", Type::ChangeState);
      break;
    }
    case 0x02: { // loop break
      loopNum = 0;
      goto loopBreak;
    }
    // Loop x times or loop forever
    case 0x03:
      loopNum = 0;
      goto doLoop;

    case 0x04:
      loopNum = 1;
      goto doLoop;

    case 0x05:
      loopNum = 2;
      goto doLoop;

    doLoop: {
      u8 numLoops = readByte(curOffset++);
      u16 offset = readShort(curOffset);
      // If the num loops is 0, then loop forever
      if (numLoops == 0) {
        bool should_continue = addLoopForever(beginOffset, 4);
        curOffset = offset;
        return should_continue;
      }
      // Otherwise, if the current loopCounter is 0, it hasn't been set yet
      if (loop[loopNum] == 0) {
        loop[loopNum] = numLoops;
        std::string name = fmt::format("Loop #{:d}", loopNum);
        std::string desc = fmt::format("Loop count: {:d}. Offset: {:X}", numLoops, offset);
        addGenericEvent(beginOffset, 4, name, desc, Type::Loop);
      } else {
        // The loop counter was previously set: decrement the counter
        loop[loopNum]--;
        // If the counter hits 0, we've finished all loops. Skip past the loop event.
        if (loop[loopNum] == 0) {
          curOffset += 2;
          break;
        }
      }
      // Do the loop
      curOffset = offset;
      break;
    }

    // On last loop iteration, end the loop early by jumping to a provided offset.
    loopBreak: {
      if (loop[loopNum] == 1) {
        loop[loopNum] = 0;
        curOffset = readShort(curOffset);
        break;
      }
      curOffset += 2;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Loop break", "", Type::Loop);
      break;
    }

    case 0x06:        // set dotted note flag
      extendDeltaFlag = true;
      addGenericEvent(beginOffset, curOffset-beginOffset, "Extend next event", "", Type::ChangeState);
      break;

    case 0x07: {      // transpose
      cps0transpose = readByte(curOffset++);
      std::string desc = fmt::format("{:+d} semitones", cps0transpose);
      addGenericEvent(beginOffset, curOffset-beginOffset, "Transpose", desc, Type::Transpose);
      break;
    }

    case 0x08: {      // set some state
      uint8_t pitchbend = readByte(curOffset++);
      pitchbend >>= 2;
      double cents = pitchbend * 1.587301587301587;
      if (pitchbend >= 32) {
        cents = -100 + cents;
      }
      addPitchBendAsPercent(beginOffset, curOffset-beginOffset, cents / 200.0);
      break;
    }

    case 0x09:        // set an attenuation related state
      curOffset++;
      break;

    case 0x0A:        // NOP
      curOffset++;
      addGenericEvent(beginOffset, curOffset-beginOffset, "NOP", "", Type::Misc);
      break;

    case 0x0B:        // NOP
      curOffset += 2;
      break;

    case 0x0C: {      // Program Change
      uint8_t progNum = readByte(curOffset++);
      addProgramChange(beginOffset, curOffset - beginOffset, progNum % 128);
      if (channelSynth == CPSSynth::YM2151) {
        instrTranspose = cpsSeq->transposeForInstr(progNum);
      }
      break;
    }

    case 0x0D: {      // Set Volume
      s8 vol = 0x7f + readByte(curOffset++);
      addVol(beginOffset, curOffset - beginOffset, vol);
      break;
    }

    case 0x0E:        // NOP
      curOffset++;
      break;

    case 0x0F:        // End of Track
      addEndOfTrack(beginOffset, curOffset - beginOffset);
      return false;
  }

  return true;
}