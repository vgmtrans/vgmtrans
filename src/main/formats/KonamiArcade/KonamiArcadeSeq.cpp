#include "KonamiArcadeSeq.h"
#include <spdlog/fmt/fmt.h>

DECLARE_FORMAT(KonamiArcade);

// ***************
// KonamiArcadeSeq
// ***************

KonamiArcadeSeq::KonamiArcadeSeq(RawFile *file, u32 offset, u32 ramOffset)
    : VGMSeq(KonamiArcadeFormat::name, file, offset, 0, "Konami Arcade Seq"),
      m_memOffset(ramOffset) {
  setAllowDiscontinuousTrackData(true);
  setAlwaysWriteInitialVol(127);
}

bool KonamiArcadeSeq::parseHeader() {
  setPPQN(0x30);
  return true;
}

bool KonamiArcadeSeq::parseTrackPointers() {
  uint32_t pos = dwOffset;
  for (int i = 0; i < 16; i++) {
    u16 memTrackOffset = readWord(pos);
    if (memTrackOffset == 0) continue;

    u32 romTrackOffset = dwOffset + (memTrackOffset - m_memOffset);

    aTracks.push_back(new KonamiArcadeTrack(this, romTrackOffset, 0));
    pos += 2;
  }
  return true;
}

// *****************
// KonamiArcadeTrack
// *****************


KonamiArcadeTrack::KonamiArcadeTrack(KonamiArcadeSeq *parentSeq, uint32_t offset, uint32_t length)
    : SeqTrack(parentSeq, offset, length), bInJump(false) {
}

void KonamiArcadeTrack::resetVars() {
  bInJump = false;
  prevDelta = 0;
  prevDur = 0;
  jump_return_offset = 0;
  memset(loopCounter, 0, sizeof(loopCounter));
  memset(loopMarker, 0, sizeof(loopMarker));
  memset(loopAtten, 0, sizeof(loopAtten));
  memset(loopTranspose, 0, sizeof(loopTranspose));
  SeqTrack::resetVars();
}

bool KonamiArcadeTrack::readEvent() {
  uint32_t beginOffset = curOffset;
  uint8_t status_byte = readByte(curOffset++);

  if (status_byte == 0xFF) {
    if (bInJump) {
      bInJump = false;
      curOffset = jump_return_offset;
    }
    else {
      addEndOfTrack(beginOffset, curOffset - beginOffset);
      return 0;
    }
  }
  else if (status_byte == 0x60) {
    addUnknown(beginOffset, curOffset - beginOffset);
    return 1;
  }
  else if (status_byte == 0x61) {
    addUnknown(beginOffset, curOffset - beginOffset);
    return 1;
  }
  else if (status_byte < 0xC0)    //note event
  {
    uint8_t note, delta;
    if (status_byte < 0x62) {
      delta = readByte(curOffset++);
      note = status_byte;
      prevDelta = delta;
    }
    else {
      delta = prevDelta;
      note = status_byte - 0x62;
    }

    uint8_t durOrVel = readByte(curOffset++);
    uint8_t dur, vel;
    if (durOrVel < 0x80) {
      dur = durOrVel;
      prevDur = dur;
      vel = readByte(curOffset++);
    }
    else {
      dur = prevDur;
      vel = durOrVel - 0x80;
    }
    u8 atten = 0x7f - vel;

    uint32_t newdur = (delta * dur) / 0x64;
    if (newdur == 0)
      newdur = 1;
    addNoteByDur(beginOffset, curOffset - beginOffset, note, vel, newdur);
    addTime(delta);
  }
  else {
    int loopNum;
    switch (status_byte) {
      case 0xC0:
        curOffset++;
        addUnknown(beginOffset, curOffset - beginOffset);
        break;

      case 0xC2:
      case 0xC3:
        curOffset++;
        addUnknown(beginOffset, curOffset - beginOffset);
        break;

      case 0xCE:
        curOffset += 2;
        addUnknown(beginOffset, curOffset - beginOffset);
        break;

      case 0xD0:
        curOffset += 3;
        addUnknown(beginOffset, curOffset - beginOffset);
        break;

      case 0xD1:
      case 0xD2:
      case 0xD3:
      case 0xD4:
      case 0xD5:
      case 0xD6:
        curOffset += 2;
        addUnknown(beginOffset, curOffset - beginOffset);
        break;

      case 0xD7:
        curOffset += 3;
        addUnknown(beginOffset, curOffset - beginOffset);
        break;

      case 0xD8:
        curOffset += 2;
        addUnknown(beginOffset, curOffset - beginOffset);
        break;

      case 0xD9:
        curOffset += 3;
        addUnknown(beginOffset, curOffset - beginOffset);
        break;

      case 0xDA:
        curOffset += 2;
        addUnknown(beginOffset, curOffset - beginOffset);
        break;

      case 0xDE:
        curOffset++;
        addUnknown(beginOffset, curOffset - beginOffset);
        break;

      // Rest
      case 0xE0: {
        uint8_t delta = readByte(curOffset++);
        // addTime(delta);
        addRest(beginOffset, curOffset-beginOffset, delta);
        break;
      }

      // Hold
      case 0xE1: {
        uint8_t delta = readByte(curOffset++);
        uint8_t dur = readByte(curOffset++);
        uint32_t newdur = (delta * dur) / 0x64;
        addTime(newdur);
        this->makePrevDurNoteEnd();
        addTime(delta - newdur);
        auto desc = fmt::format("total delta: {:d} ticks  additional duration: {:d} ticks", delta, newdur);
        addGenericEvent(beginOffset, curOffset - beginOffset, "Hold", desc, CLR_TIE);
        break;
      }

      // program change
      case 0xE2: {
        uint8_t progNum = readByte(curOffset++);
        addProgramChange(beginOffset, curOffset - beginOffset, progNum);
        break;
      }

      // Pan
      case 0xE3:
        // 0x00 seems centered.
        // 0x01 causes full left,
        // 0x7F causes full right, channel
        // but collapses to center very quickly. 0x05 sounds centered, as does 0x7F - 4
        curOffset++;
        addUnknown(beginOffset, curOffset - beginOffset, "Pan");
        break;

      // LFO settings (or perhaps just vibrato)
      case 0xE4: {
        u8 depth = readByte(curOffset++); // lower seems to result in more effect. 7F turns off all vibrato regardless of amplitude.
        u8 freq = readByte(curOffset++);  // higher results in faster fluctuation
        u8 amplitude = readByte(curOffset++); // higher results in greater LFO effect
        addUnknown(beginOffset, curOffset - beginOffset, "Vibrato?");
        break;
      }

      case 0xE5:
        curOffset += 3;
        addUnknown(beginOffset, curOffset - beginOffset);
        break;

      case 0xE6:
        loopNum = 0;
        goto loopMarker;
      case 0xE8: {
        loopNum = 1;
      loopMarker:
        loopMarker[loopNum] = curOffset;
        auto name = fmt::format("Loop {:d} Marker", loopNum);
        addGenericEvent(beginOffset, curOffset - beginOffset, name, "", CLR_LOOP);
        break;
      }

      case 0xE7:
        loopNum = 0;
        goto doLoop;
      case 0xE9: {
        loopNum = 1;
      doLoop:
        u8 loopCount = readByte(curOffset++);
        u8 initialLoopAtten = -readByte(curOffset++);
        u8 initialLoopTranspose = readByte(curOffset++);
        if (loopCounter[loopNum] == 0) {
          if (loopCount == 1) {
            loopAtten[loopNum] = 0;
            loopTranspose[loopNum] = 0;
            break;
          }
          loopCounter[loopNum] = loopCount - 1;
          loopAtten[loopNum] = initialLoopAtten;
          loopTranspose[loopNum] = initialLoopTranspose;
        }
        else {
          if (loopCount > 0 && --loopCounter[loopNum] == 0) {
            loopAtten[loopNum] = 0;
            loopTranspose[loopNum] = 0;
            break;
          }
          loopAtten[loopNum] += initialLoopAtten;
          loopTranspose[loopNum] += initialLoopTranspose;
        }

        // TODO: confusing logic sets a couple channel state vars here based on atten and transpose

        bool shouldContinue = true;
        if (loopCount == 0) {
          shouldContinue = addLoopForever(beginOffset, curOffset - beginOffset);
        }
        else {
          auto desc = fmt::format("count: {:d}  attenuation: {:d}  transpose {:d}", loopCount,
            initialLoopAtten, initialLoopTranspose);
          addGenericEvent(beginOffset, curOffset - beginOffset, "Loop", desc, CLR_LOOP);
        }
        if (loopMarker[loopNum] < parentSeq->dwOffset) {
          L_ERROR("KonamiArcadeSeq wants to loopMarker outside bounds of sequence. Loop at %X", beginOffset);
          return false;
        }
        printf("E7/E9 LOOPING FROM %X", beginOffset);
        curOffset = loopMarker[loopNum];
        printf(" TO: %X\n", curOffset);
        return shouldContinue;
      }

      //tempo
      case 0xEA: {
        u8 bpm = readByte(curOffset++);
        addTempoBPM(beginOffset, curOffset - beginOffset, bpm);
        break;
      }

      case 0xEC:
        curOffset++;
        addUnknown(beginOffset, curOffset - beginOffset);
        break;

      // Volume
      case 0xEE: {
        u8 vol = readByte(curOffset++);
        u8 atten = ~vol & 0x7F;
        addVol(beginOffset, curOffset - beginOffset, vol);
        //AddMasterVol(beginOffset, curOffset-beginOffset, vol);
        break;
      }

      case 0xEF:
        curOffset += 2;
        addUnknown(beginOffset, curOffset - beginOffset);
        break;

      case 0xF0:
        curOffset++;
        addUnknown(beginOffset, curOffset - beginOffset);
        break;

      case 0xF1:
        curOffset += 3;
        addUnknown(beginOffset, curOffset - beginOffset);
        break;

      case 0xF2:
        curOffset++;
        addUnknown(beginOffset, curOffset - beginOffset);
        break;

      case 0xF3:
        curOffset += 3;
        addUnknown(beginOffset, curOffset - beginOffset);
        break;

      case 0xF6:
      case 0xF7:
        addUnknown(beginOffset, curOffset - beginOffset);
        break;

      case 0xF8:
        curOffset += 2;
        addUnknown(beginOffset, curOffset - beginOffset);
        break;

      case 0xF9: {
        u8 data = readByte(curOffset++);
        addUnknown(beginOffset, curOffset - beginOffset);
      }

      //release rate
      case 0xFA: {
        // lower value results in longer sustain
        u8 releaseRate = readByte(curOffset++);
        addUnknown(beginOffset, curOffset - beginOffset, "Release Rate");
        break;
      }

      case 0xFD: {
        auto seq = static_cast<KonamiArcadeSeq*>(parentSeq);
        u16 memJumpOffset = readShort(curOffset);
        u32 romOffset = seq->dwOffset + (memJumpOffset - seq->memOffset());
        bool shouldContinue = addLoopForever(beginOffset, 3);
        if (romOffset < seq->dwOffset) {
          L_ERROR("KonamiArcadeEvent FD attempted jump to offset outside the sequence at {:X}.  Jump offset: {:X}", beginOffset, romOffset);
          return false;
        }
        printf("FD LOOPING FROM %X", beginOffset);
        curOffset = romOffset;
        printf(" TO: %X\n", curOffset);
        return shouldContinue;
      }

      case 0xFE: {
        bInJump = true;
        jump_return_offset = curOffset + 2;
        addGenericEvent(beginOffset, jump_return_offset - beginOffset, "Jump", "", CLR_LOOP);
        auto seq = static_cast<KonamiArcadeSeq*>(parentSeq);
        u16 memJumpOffset = readShort(curOffset);
        u32 romOffset = seq->dwOffset + (memJumpOffset - seq->memOffset());
        if (romOffset < seq->dwOffset) {
          L_ERROR("KonamiArcade Event FE attempted jump to offset outside the sequence at {:X}.  Jump offset: {:X}", beginOffset, romOffset);
          return false;
        }
        printf("FE LOOPING FROM %X", beginOffset);
        curOffset = romOffset;
        printf(" TO: %X\n", curOffset);
        break;
      }

      default:
        addEndOfTrack(beginOffset, curOffset - beginOffset);
        return false;
    }
  }
  return true;
}