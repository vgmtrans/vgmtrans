/*
 * VGMTrans (c) 2002-2025
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#include "KonamiTMNT2Seq.h"

#include <utility>
#include <spdlog/fmt/fmt.h>

DECLARE_FORMAT(KonamiTMNT2);

KonamiTMNT2Seq::KonamiTMNT2Seq(RawFile *file,
                               KonamiTMNT2FormatVer fmtVer,
                               u32 offset,
                               std::vector<u32> ym2151TrackOffsets,
                               std::vector<u32> k053260TrackOffsets,
                               const std::string &name)
    : VGMSeq(KonamiTMNT2Format::name, file, offset, 0, name),
      m_fmtVer(fmtVer),
      m_ym2151TrackOffsets(std::move(ym2151TrackOffsets)),
      m_k053260TrackOffsets(std::move(k053260TrackOffsets)) {
  setPPQN(120);
  setAlwaysWriteInitialTempo(108);
  setAllowDiscontinuousTrackData(true);
  // setShouldTrackControlFlowState(true);
}

void KonamiTMNT2Seq::resetVars() {
  VGMSeq::resetVars();
}

// bool KonamiTMNT2Seq::parseHeader() {
  // addHeader(dwOffset, 0, "Konami TMNT2 Header");
  // return true;
// }

bool KonamiTMNT2Seq::parseTrackPointers() {
  // for (auto offset : m_ym2151TrackOffsets) {
  //   auto *track = new KonamiTMNT2K053260Track(this, offset, 0);
  //   aTracks.push_back(track);
  // }
  for (auto offset : m_k053260TrackOffsets) {
    auto *track = new KonamiTMNT2K053260Track(this, offset, 0);
    aTracks.push_back(track);
  }

  nNumTracks = static_cast<uint32_t>(aTracks.size());
  return nNumTracks > 0;
}

KonamiTMNT2K053260Track::KonamiTMNT2K053260Track(KonamiTMNT2Seq *parentSeq, uint32_t offset, uint32_t length)
    : SeqTrack(parentSeq, offset, length) {}

void KonamiTMNT2K053260Track::resetVars() {
  SeqTrack::resetVars();

  m_state = 0;
  m_rawBaseDur = 0;
  m_baseDur = 0;
  m_extendDur = 0;
  m_durSubtract = 0;
  m_attenuation = 0;
  m_octave = 0;
  m_transpose_0 = 0;
  m_transpose_1 = 0;
  m_addedToNote = 0;
  m_dxVal = 1;
  memset(m_loopCounter, 0, sizeof(m_loopCounter));
  memset(m_loopStartOffset, 0, sizeof(m_loopStartOffset));
  m_warpCounter = 0;
  m_warpOrigin = 0;
  m_warpDest = 0;
}

bool KonamiTMNT2K053260Track::readEvent() {
  if (!isValidOffset(curOffset)) {
    return false;
  }

  if (curOffset == 0x9787)
    printf("TESTING");
  uint32_t beginOffset = curOffset;
  uint8_t opcode = readByte(curOffset++);

  if (opcode < 0xD0) {
    // NOTE

    // determine duration
    u16 dur = opcode & 0xF;
    if (dur == 0)
      dur = 0x10;
    dur += m_extendDur;
    m_extendDur = 0;
    if ((m_state & 0x20) > 0) {
      dur *= 3;
      m_state |= 0x40;             // set bit 6
      m_state &= (0xFF - 0x20);    // reset bit 5
      m_durSubtract = dur;
      // TODO? stores duration value in chan_state[0x67]
    } else {
      dur *= m_baseDur;
      if ((m_state & 0x40) > 0) {
        dur -= m_durSubtract;
        m_state &= (0xFF - 0x40);
      }
    }

    if ((opcode & 0xF0) == 0) {
      // Don't play note?
      m_state |= 0x80;
      addRest(beginOffset, curOffset - beginOffset, dur);
      return true;
    }
    if (percussionMode()) {
      if (m_addedToNote > 0)
        printf("TESTING");
      u8 semitones = opcode >> 4;
      s8 note = semitones + (m_addedToNote * 16);
      note -= transpose;
      addNoteByDur(beginOffset, curOffset - beginOffset, note, 0x7F, dur);
    } else {
      // Melodic
      u8 semitones = (opcode >> 4) - 1;
      // u8 durationIndex = opcode & 0x0F;
      u8 note = semitones + m_addedToNote;
      // note -= 12;
      // printf("DUR: %d\n", dur);
      if (dur == 0) {
        printf("DUR 0. offset: %X\n", beginOffset);
      }
      addNoteByDur(beginOffset, curOffset - beginOffset, note, 0x7F, dur);
    }
    addTime(dur);
    return true;
  }

  // if (opcode >= 0xD0) {
    m_extendDur = 0;
    u8 loopIdx;
    switch (opcode) {
      case 0xD0:
      case 0xD1:
      case 0xD2:
      case 0xD3:
      case 0xD4:
      case 0xD5:
      case 0xD6:
      case 0xD7:
        // m_dxVal
        addUnknown(beginOffset, curOffset - beginOffset, fmt::format("Opcode {:02X}", opcode));
        break;
      case 0xD8:
        curOffset += 1;
        addUnknown(beginOffset, curOffset - beginOffset, fmt::format("Opcode {:02X}", opcode));
        break;
      case 0xD9: {
        // Note extender
        while (opcode == 0xD9) {
          m_extendDur += 0x10;
          opcode = readByte(curOffset++);
        }
        curOffset -= 1;
        auto desc = fmt::format("Extend Duration - {}", m_extendDur);
        addGenericEvent(beginOffset, curOffset - beginOffset, "Extend Duration", desc, Type::DurationChange);
        break;
      }
      case 0xDA:
        curOffset += 1;
        addUnknown(beginOffset, curOffset - beginOffset, fmt::format("Opcode {:02X}", opcode));
        break;
      case 0xDB:
        curOffset += 1;
        addUnknown(beginOffset, curOffset - beginOffset, fmt::format("Opcode {:02X}", opcode));
        break;
      case 0xDC:
        curOffset += 1;
        addUnknown(beginOffset, curOffset - beginOffset, fmt::format("Opcode {:02X}", opcode));
        break;
      case 0xDD:
        if (m_rawBaseDur == m_baseDur) {
          m_baseDur = m_rawBaseDur * 3;
        } else {
          m_baseDur = m_rawBaseDur;
        }
        addGenericEvent(beginOffset, 1, "Duration Multiplier Toggle", "", Type::DurationChange);
        break;
      case 0xDE:
        m_state |= 0x20;
        break;
      case 0xDF:
        addUnknown(beginOffset, curOffset - beginOffset, fmt::format("Opcode {:02X}", opcode));
        break;
      case 0xE0: {
        u8 val = readByte(curOffset++);
        if ((val & 0xF0) == 0) {
          m_state = val & 0xF0;
          m_rawBaseDur = val;
          m_baseDur = val * 3;
          m_program = readByte(curOffset++);
          addProgramChangeNoItem(m_program, false);
          m_attenuation = readByte(curOffset++) & 0x7F;
          u8 unsure = readByte(curOffset++);
          addGenericEvent(beginOffset, curOffset - beginOffset, "Program Change / Base Dur / Attenuation / State", "", Type::ProgramChange);


          // m_state = val;
          // m_rawBaseDur = readByte(curOffset++);
          // m_baseDur = m_rawBaseDur * 3;
          // m_attenuation = readByte(curOffset++);
          // // fourth byte is unclear
          // curOffset += 1;
        }
        else {
          // m_transpose_1 = (val >> 4) - 1;
          m_addedToNote = (val >> 4) - 1;
          m_rawBaseDur = val & 0xF;
          m_baseDur = m_rawBaseDur * 3;
          m_attenuation = readByte(curOffset++);
          setPercussionModeOn();
          addGenericEvent(beginOffset, curOffset - beginOffset, "Percussion On / Pitch / Base Dur / Attenuation", "", Type::ProgramChange);
        }
        break;
      }
      case 0xE1: {
        s8 val = readByte(curOffset++);
        setPercussionModeOff();
        if (val == 0) {
          addGenericEvent(beginOffset, 2, "Percussion Mode Off", "", Type::ChangeState);
          break;
        }
        m_addedToNote = val;
        setPercussionModeOn();
        addGenericEvent(beginOffset, 2, "Percussion Mode On", "", Type::ChangeState);
        break;
      }
      case 0xE2: {
        // Set base duration
        m_rawBaseDur = readByte(curOffset++);
        m_baseDur = m_rawBaseDur * 3;
        auto desc = fmt::format("Base Duration - {}", m_baseDur);
        addGenericEvent(beginOffset, 2, "Set Base Duration", desc, Type::DurationChange);
        break;
      }
      case 0xE3:
        // PROGRAM CHANGE
        m_program = readByte(curOffset++);
        addProgramChange(beginOffset, 2, m_program);
        break;
      case 0xE4: {
        m_attenuation = readByte(curOffset++) & 0x7F;
        addGenericEvent(beginOffset, 2, "Set Attenuation", "", Type::Volume);
        break;
      }
      case 0xE5:
        // Vibrato? Tremelo?
        if (readByte(curOffset++) == 0) {
          addUnknown(beginOffset, curOffset - beginOffset, "Vibrato? Tremelo? Off");
          break;
        }
        curOffset += 1;
        addUnknown(beginOffset, curOffset - beginOffset, "Vibrato? Tremelo? On");
        break;
      case 0xE6:    // NOP
        break;
      case 0xE7:
        curOffset += 1;
        addUnknown(beginOffset, curOffset - beginOffset, fmt::format("Opcode {:02X}", opcode));
        break;
      case 0xE8:    // NOP
      case 0xE9:    // NOP
      case 0xEA:    // NOP
        break;
      case 0xEB:
      case 0xEC: {
        // Transpose
        // Bits 0–3: fine semitone offset within an octave (0–15)
        // Bits 4–5: octave offset (0–3), each octave = 12 semitones
        // Bit 6: target selector:
        //  0 = per-track transpose
        //  1 = global transpose
        // Bit 7: sign bit:
        u8 val = readByte(curOffset++);
        s8 transpose = val & 0xF;             // semitones
        transpose += ((val >> 4) & 3) * 12;   // octaves
        if ((val & 0x80) > 0)
          transpose = -transpose;
        if ((val & 0x40) > 0)
          addGlobalTranspose(beginOffset, 2, transpose);
        else
          addTranspose(beginOffset, 2, transpose);
        break;
      }
      case 0xED:
        // Pitch bend
        curOffset += 1;
        addUnknown(beginOffset, curOffset - beginOffset, "Pitchbend?");
        break;
      case 0xEE:
        curOffset += 3;
        addUnknown(beginOffset, curOffset - beginOffset, fmt::format("Opcode {:02X}", opcode));
        break;
      case 0xEF:    // NOP
        break;
      case 0xF0:
      case 0xF1:
      case 0xF2:
      case 0xF3:
      case 0xF4:
      case 0xF5:
      case 0xF6:
      case 0xF7: {
        if (percussionMode()) {
          m_addedToNote = (opcode & 0xF) - 1;
        } else {
          m_addedToNote = (opcode & 0xF) * 12;
        }
        // u8 val = opcode & 0xF;

        // u8 semitones = val - 1;
        // if (percussionMode() == false) {
        //   u8 cVar6 = val + 1;
        //   u8 bVar4 = 0;
        //   do {
        //     semitones = bVar4;
        //     cVar6 = cVar6 - 1;
        //     bVar4 = semitones + 0xc;
        //   } while (cVar6 != 0);
        // }
        // m_addedToNote = semitones;
        addGenericEvent(beginOffset, 1, "Set Octave", fmt::format("Octave - {}", opcode & 0xF), Type::Octave);

        // if ((m_state & 2) > 0) {
        //   m_addedToNote = val - 1;
        //   addGenericEvent(beginOffset, 1, "Set Pitch", "", Type::Octave);
        // } else {
        //   m_addedToNote = val * 12;
        //   addGenericEvent(beginOffset, 1, "Set Octave", fmt::format("Octave - {}", val), Type::Octave);
        // }
        break;
      }
      case 0xF8:    // NOP
        break;
      // JUMP
      case 0xF9: {
        u16 dest = readShort(curOffset);
        // addJump(beginOffset, 3, dest);
        // curOffset += 2;
        // addGenericEvent(beginOffset, 3, "Jump", "", Type::Jump);
        bool shouldContinue = true;
        if (dest < beginOffset) {
          shouldContinue = addLoopForever(beginOffset, 3);
        } else {
          auto desc = fmt::format("Destination: ${:X}", dest);
          addGenericEvent(beginOffset, 3, "Jump", desc, Type::Loop);
        }
        if (dest < parentSeq->dwOffset) {
          L_ERROR("KonamiArcadeEvent FD attempted jump to offset outside the sequence at {:X}.  Jump offset: {:X}", beginOffset, dest);
          return false;
        }
        curOffset = dest;
        return shouldContinue;
        break;
      }
      // LOOP
      case 0xFA:
        loopIdx = 0;
        goto loopMarker;
      case 0xFB:
        loopIdx = 1;
      loopMarker:
        if (m_loopStartOffset[loopIdx] == 0) {
          m_loopStartOffset[loopIdx] = curOffset;
          m_loopCounter[loopIdx] = 0;
          addGenericEvent(beginOffset, 1, "Loop Start", "", Type::Loop);
        } else {
          u8 loopCount = readByte(curOffset++);
          if (loopCount == 0xFF) {
            bool shouldContinue = addLoopForever(beginOffset, 2);
            curOffset = m_loopStartOffset[loopIdx];
            return shouldContinue;
          }
          if (++m_loopCounter[loopIdx] != loopCount) {
            auto desc = fmt::format("Loop With Count - {} times", loopCount);
            addGenericEvent(beginOffset, 2, "Loop With Count", desc, Type::Loop);
            curOffset = m_loopStartOffset[loopIdx];
          } else {
            m_loopStartOffset[loopIdx] = 0;
          }
        }
        break;
      // CALL
      case 0xFC:
      case 0xFD: {
        if (returnOffsets.empty()) {
          u16 dest = readShort(curOffset);
          curOffset += 2;
          if (dest < parentSeq->dwOffset) {
            return false;
          }
          // m_callRetOffset = curOffset;
          addCall(beginOffset, 3, dest, curOffset);
        } else {
          addReturn(beginOffset, 1);
        }
        break;
      }
      case 0xFE:    // weird offset toggler
        m_warpCounter += 1;          // state

        if (m_warpCounter == 1) {
          m_warpOrigin = curOffset;
          addGenericEvent(beginOffset, 1, "Warp Start", "", Type::RepeatStart);
        } else if ((m_warpCounter & 1) == 0) {     // even state
          if (m_warpCounter != 2) {
            curOffset = m_warpDest;
            addGenericEvent(beginOffset, 1, "Warp Jump", "", Type::Jump);
          }
        } else {                       // odd state > 1
          m_warpDest = curOffset;
          if (readByte(m_warpDest) == 0xfe) {
            m_warpCounter = 0xff; // special mode
            m_warpDest = ++curOffset;
            addGenericEvent(beginOffset, curOffset - beginOffset, "Warp Final Point", "", Type::RepeatEnd);
          } else {
            addGenericEvent(beginOffset, curOffset - beginOffset, "Warp Point", "", Type::RepeatEnd);
          }
          curOffset = m_warpOrigin;
        }
        break;
      case 0xFF:
        addEndOfTrack(beginOffset, curOffset - beginOffset);
        return false;
      default: {
        auto eventName = fmt::format("Opcode {:02X}", opcode);
        addUnknown(beginOffset,
                   curOffset - beginOffset,
                   eventName,
                   "TODO: Implement Konami TMNT2 high opcode event");
        break;
      }
    }
  // } else {
  //   auto eventName = fmt::format("Opcode {:02X}", opcode);
  //   addUnknown(beginOffset,
  //              curOffset - beginOffset,
  //              eventName,
  //              "TODO: Implement Konami TMNT2 low opcode event");
  // }

  return true;
}
