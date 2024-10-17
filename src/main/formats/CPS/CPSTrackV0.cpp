#include "CPSTrackV0.h"


CPSTrackV0::CPSTrackV0(CPS0Seq *parentSeq, CPSSynth channelSynth, uint32_t offset, uint32_t length)
    : SeqTrack(parentSeq, offset, length), channelSynth(channelSynth) {
  if (channelSynth == CPSSynth::YM2151) {
    synthType = SynthType::YM2151;
  }
  CPSTrackV0::resetVars();
}

void CPSTrackV0::resetVars() {
  noteDuration = 0xFF;
  bPrevNoteTie = false;
  prevTieNote = 0;
  curDeltaTable = 0;
  noteState = 0;
  bank = 0;
  progNum = -1;
  cps0transpose = 0;
  restFlag = false;
  portamentoCentsPerSec = 0;
  prevPortamentoDuration = 0;
  memset(loop, 0, sizeof(loop));
  memset(loopOffset, 0, sizeof(loopOffset));
  SeqTrack::resetVars();
}

void CPSTrackV0::addInitialMidiEvents(int trackNum) {
  SeqTrack::addInitialMidiEvents(trackNum);
  addPortamentoTime14BitNoItem(0);
}

bool CPSTrackV0::readEvent() {
  u32 beginOffset = curOffset;
  u8 statusByte = readByte(curOffset++);
  auto cpsSeq = static_cast<CPS0Seq*>(parentSeq);
  u8 masterVol = cpsSeq->masterVolume();

  if (statusByte >= 0x40) {
    int shiftAmount = ((statusByte >> 5) & 0x07) - 2;
    shiftAmount = shiftAmount < 0 ? 0 : shiftAmount;
    uint32_t delta = (cpsSeq->tempoRaw << shiftAmount) >> 8;
    // uint32_t delta = shiftAmount << 8;

    if (dottedNoteFlag) {
      delta += (delta / 2);
      dottedNoteFlag = false;
    }

    u8 absDur = static_cast<u8>(static_cast<u16>(delta * noteDuration) >> 8);
    absDur = (absDur == 0) ? 1 : absDur;

    key = (statusByte & 0x1F);
    if (key == 0) {
      restFlag = true;
      addRest(beginOffset, curOffset - beginOffset, delta);
    } else {
      key += cps0transpose + instrTranspose;
      while (key > 0x60) { key -= 12; }
      while (key < 0) { key += 12; }
      key += 12;
      addNoteByDur(beginOffset, curOffset - beginOffset, key, masterVol, absDur);
      addTime(delta);
      restFlag = false;
    }

    return true;
  }

  if (statusByte >= 0x20) {
    // if ((statusByte & 0xF0) != 0x20) {
    if (statusByte >= 0x30) {
      // pChanState->field_0x9 = statusByte & 0xf;
    } else {
      // pChanState->field_0xa = (statusByte & 0xf) + 1;
      // pChanState->set_by_event_06 = pChanState->set_by_event_06 | 2;
    }
    return true;
  }

  int loopNum;
  switch (statusByte) {
    case 0x00: {  // tempo
      u8 tempoByte = readByte(curOffset++);
      u16 newTempo = (((u8)(tempoByte + 0x80U) >> 3) << 8) | (((u8)(tempoByte + 0x80U) >> 2) << 7);

      cpsSeq->tempoRaw = newTempo;
      u16 ticks_per_iteration = ((cpsSeq->tempoRaw + 0x80) & 0xFC) << 5;
      // u16 ticks_per_iteration =cpsSeq->tempoRaw;
      auto internal_ppqn = parentSeq->ppqn() << 8;
      // auto internal_ppqn = newTempo;
      auto iterations_per_beat = static_cast<double>(internal_ppqn) / ticks_per_iteration;
      const uint32_t micros_per_beat = lround((iterations_per_beat / CPS2_DRIVER_RATE_HZ) * 1000000);
      // addTempo(beginOffset, curOffset - beginOffset, micros_per_beat);
      addTempoBPM(beginOffset, curOffset - beginOffset, 290);
      // addTempoBPM(beginOffset, curOffset - beginOffset, cpsSeq->tempoRaw);

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
      addGenericEvent(beginOffset, curOffset - beginOffset, "Set Duration", "", CLR_CHANGESTATE);
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
      // If the num loops is 0, then loop forever
      if (numLoops == 0) {
        bool should_continue = addLoopForever(beginOffset, 3);
        curOffset = readShort(curOffset);;
        return should_continue;
      }
      // Otherwise, if the current loopCounter is 0, it hasn't been set yet
      if (loop[loopNum] == 0) {
        loop[loopNum] = numLoops;
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
      curOffset = readShort(curOffset);
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
      break;
    }

    case 0x06:        // set dotted note flag
      dottedNoteFlag = true;
      break;

    case 0x07: {      // transpose
      cps0transpose = readByte(curOffset++);
      std::string desc = fmt::format("Transpose: {:d} semitones", cps0transpose);
      addGenericEvent(beginOffset, curOffset-beginOffset, "Transpose", desc, CLR_TRANSPOSE);
      break;
    }

    case 0x08:        // set some state
      curOffset++;
      break;

    case 0x09:        // set an attenuation related state
      curOffset++;
      break;

    case 0x0A:        // NOP
      curOffset++;
      break;

    case 0x0B:        // NOP
      curOffset += 2;
      break;

    case 0x0C: {      // Program Change
      uint8_t progNum = readByte(curOffset++);
      addProgramChange(beginOffset, curOffset - beginOffset, progNum % 128);
      if (channelSynth == CPSSynth::YM2151) {
        instrTranspose = cpsSeq->getTransposeForInstr(progNum);
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