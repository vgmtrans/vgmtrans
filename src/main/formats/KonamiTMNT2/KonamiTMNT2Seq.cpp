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
  bLoadTickByTick = true;
  setPPQN(240);
  setAlwaysWriteInitialVol(127);
  setAlwaysWriteInitialExpression(127);
  setAlwaysWriteInitialTempo(110);
  setAllowDiscontinuousTrackData(true);
  // setShouldTrackControlFlowState(true);
}

void KonamiTMNT2Seq::resetVars() {
  VGMSeq::resetVars();
  m_globalTransposeYM2151 = 0;
  m_globalTransposeK053260 = 0;
}

// bool KonamiTMNT2Seq::parseHeader() {
  // addHeader(dwOffset, 0, "Konami TMNT2 Header");
  // return true;
// }

bool KonamiTMNT2Seq::parseTrackPointers() {
  // auto *track = new KonamiTMNT2Track(true, this, m_ym2151TrackOffsets[0], 0, "FM Track");
  // aTracks.push_back(track);
  for (int i = 0; i < m_ym2151TrackOffsets.size(); ++i) {
    auto offset = m_ym2151TrackOffsets[i];
    auto name = fmt::format("FM Track {}", i);
    auto *track = new KonamiTMNT2Track(true, this, offset, 0, name);
    aTracks.push_back(track);
  }
  for (int i = 0; i < m_k053260TrackOffsets.size(); ++i) {
    auto offset = m_k053260TrackOffsets[i];
    auto name = fmt::format("Sampled Track {}", i);
    auto *track = new KonamiTMNT2Track(false, this, offset, 0, name);
    aTracks.push_back(track);
  }

  nNumTracks = static_cast<uint32_t>(aTracks.size());
  return nNumTracks > 0;
}

KonamiTMNT2Track::KonamiTMNT2Track(
  bool isFmTrack,
  KonamiTMNT2Seq *parentSeq,
  uint32_t offset,
  uint32_t length,
  std::string name
)
    : SeqTrack(parentSeq, offset, length, std::move(name)),
      m_isFmTrack(isFmTrack) {
  synthType = isFmTrack ? SynthType::YM2151 : SynthType::SoundFont;
}

void KonamiTMNT2Track::resetVars() {
  SeqTrack::resetVars();

  m_state = 0;
  m_rawBaseDur = 0;
  m_baseDur = 0;
  m_baseDurHalveBackup = 0;
  m_extendDur = 0;
  m_durSubtract = 0;
  m_noteDurPercent = 0;
  m_attenuation = 0;
  m_octave = 0;
  m_transpose = 0;
  m_addedToNote = 0;
  m_dxAtten = 0;
  m_dxAttenMultiplier = 1;
  memset(m_loopCounter, 0, sizeof(m_loopCounter));
  memset(m_loopStartOffset, 0, sizeof(m_loopStartOffset));
  memset(m_callOrigin, 0, sizeof(m_callOrigin));
  m_warpCounter = 0;
  m_warpOrigin = 0;
  m_warpDest = 0;
}

bool KonamiTMNT2Track::readEvent() {
  if (!isValidOffset(curOffset)) {
    return false;
  }

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
      m_state &= (0xFF ^ 0x20);    // reset bit 5
      m_durSubtract = dur;
      // TODO? stores duration value in chan_state[0x67]
    } else {
      dur *= m_baseDur;
      if ((m_state & 0x40) > 0) {
        dur -= m_durSubtract;
        m_state &= (0xFF ^ 0x40);
      }
    }

    if ((opcode & 0xF0) == 0) {
      // Don't play note?
      m_state |= 0x80;
      addRest(beginOffset, curOffset - beginOffset, dur);
      return true;
    }
    if (!m_isFmTrack && percussionMode()) {
      u8 semitones = opcode >> 4;
      s8 note = semitones + (m_addedToNote * 16);
      // note -= transpose;
      addNoteByDur(beginOffset, curOffset - beginOffset, note, /*0x7F*/ 0x50, dur);
    } else {
      // Melodic
      u8 semitones = (opcode >> 4) - 1;
      // u8 durationIndex = opcode & 0x0F;
      u8 note = semitones + m_addedToNote + m_transpose + globalTranspose();
      if (m_isFmTrack)
        note += 12;
      // note -= 12;
      // printf("DUR: %d\n", dur);
      if (dur == 0) {
        printf("DUR 0. offset: %X\n", beginOffset);
      }
      u32 noteDur = dur;
      if (m_noteDurPercent > 0) {
        noteDur = dur * (m_noteDurPercent / 256.0);
      }
      noteDur = std::max(1u, noteDur);
      // YM2151 requires time between notes, otherwise it strings them together portamento style.
      // if (m_isFmTrack && (noteDur == dur) && (noteDur > 0))
        // noteDur -= 1;
      // u32 finalNoteDur = (m_isFmTrack && noteDur > 1) ? noteDur - 1 : noteDur;
      // if (finalNoteDur == 0) {
        // printf("FINAL NOTE DUR 0\n");
      // }
      u8 vel = m_isFmTrack ? (0x7F - m_dxAtten) : 0x50;
      addNoteByDur(beginOffset, curOffset - beginOffset, note, vel, noteDur);
    }
    addTime(dur);
    return true;
  }

  // if (opcode >= 0xD0) {
    m_extendDur = 0;
    u8 loopIdx;
    u8 callIdx;
    switch (opcode) {
      case 0xD0:
      case 0xD1:
      case 0xD2:
      case 0xD3:
      case 0xD4:
      case 0xD5:
      case 0xD6:
      case 0xD7:
        if (m_isFmTrack) {
          m_state |= 8;
        }
        if (m_isFmTrack) {
          m_dxAtten = (opcode & 0xF) * m_dxAttenMultiplier;
          m_dxAtten = std::min<u8>(m_dxAtten, 0x7F);
        }
        else {
          m_dxAtten = (opcode & 0xF) * ((m_dxAttenMultiplier == 0) ? 1 : m_dxAttenMultiplier);
          m_dxAtten &= 0x7F;
        }
        addGenericEvent(beginOffset, curOffset - beginOffset, "Attenuation", "", Type::Volume);
        break;
      case 0xD8:
        m_dxAttenMultiplier = readByte(curOffset++);
        if (m_isFmTrack && m_dxAttenMultiplier > 0x12)
          m_dxAttenMultiplier = 0x12;
        addGenericEvent(beginOffset, curOffset - beginOffset, "Attenuation Multiplier", "", Type::Volume);
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

      case 0xDA: {
        u8 val = readByte(curOffset++);
        if (m_isFmTrack) {
          // Overrides RL registers (right/left channel enable) if a global state var is set to 0
          // value 0 -> cancel override and restore instruments original RL setting
          // 1 -> L only (0x40)
          // 2 -> R only (0x80)
          // 3 -> LR enabled
          u8 pan = 64;
          if (val == 1)
            pan = 0;
          else if (val == 2)
            pan = 127;
          addPan(beginOffset, 2, pan);
        } else {
          addUnknown(beginOffset, curOffset - beginOffset, "Pan");
        }
        break;
      }
      case 0xDB: {
        m_noteDurPercent = readByte(curOffset++);
        auto desc = fmt::format("Note Duration Percent - {} / 256 = {:.1f}%", m_noteDurPercent, m_noteDurPercent / 256.0);
        addGenericEvent(beginOffset, 2, "Note Duration Percent", desc, Type::DurationChange);
        break;
      }
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
        // Halve Base Duration Toggle
        if (m_baseDurHalveBackup == 0) {
          m_baseDurHalveBackup = m_baseDur;
          m_baseDur /= 2;
        } else {
          m_baseDur = m_baseDurHalveBackup;
          m_baseDurHalveBackup = 0;
        }
        addGenericEvent(beginOffset, 1, "Halve Duration", "", Type::DurationChange);
        break;
      case 0xE0: {
        u8 val = readByte(curOffset++);

        if (m_isFmTrack) {
          m_state |= 4;
          if (val == 0) {
            u8 tempo = readByte(curOffset++);
            // byte acts as tempo, but unclear exact calculation
            // lower value means slower
            val = readByte(curOffset++);
          }
          m_rawBaseDur = val;
          // We multiply the raw base duration by two so we can cleanly halve durations (for event DF)
          m_rawBaseDur *= 2;
          m_baseDur = m_rawBaseDur * 3;
          m_program = readByte(curOffset++);
          // TMNT2 is weird. It has 113 FM instruments, but code to handle > 128, however, it just
          // performs a simple &= 0x7F. Values over 127 are used in sequences.
          m_program &= 0x7F;
          addProgramChangeNoItem(m_program, false);
          // Next byte is attenuation.
          addVolNoItem(0x7F - (readByte(curOffset++) & 0x7F));
          m_noteDurPercent = readByte(curOffset++);
          addGenericEvent(beginOffset, curOffset - beginOffset, "Program Change / Base Dur / Attenuation / State / Note Dur", "", Type::ProgramChange);
        } else {
          if ((val & 0xF0) == 0) {
            m_state = val & 0xF0;
            m_rawBaseDur = val;
            // We multiply the raw base duration by two so we can cleanly halve durations (for event DF)
            m_rawBaseDur *= 2;
            m_baseDur = m_rawBaseDur * 3;
            m_program = readByte(curOffset++);
            addProgramChangeNoItem(m_program, false);
            m_attenuation = readByte(curOffset++) & 0x7F;
            m_noteDurPercent = readByte(curOffset++);
            addGenericEvent(beginOffset, curOffset - beginOffset, "Program Change / Base Dur / Attenuation / State / Note Dur", "", Type::ProgramChange);
          }
          else {
            // m_transpose_1 = (val >> 4) - 1;
            m_addedToNote = (val >> 4) - 1;
            m_rawBaseDur = val & 0xF;
            // We multiply the raw base duration by two so we can cleanly halve durations (for event DF)
            m_rawBaseDur *= 2;
            m_baseDur = m_rawBaseDur * 3;
            m_attenuation = readByte(curOffset++);
            setPercussionModeOn();
            addGenericEvent(beginOffset, curOffset - beginOffset, "Percussion On / Pitch / Base Dur / Attenuation", "", Type::ProgramChange);
          }
        }
        break;
      }
      case 0xE1: {
        if (m_isFmTrack) {
          m_state = readByte(curOffset++);
          addGenericEvent(beginOffset, 2, "Set State", "", Type::ChangeState);
        } else {
          s8 val = readByte(curOffset++);
          setPercussionModeOff();
          if (val == 0) {
            addGenericEvent(beginOffset, 2, "Percussion Mode Off", "", Type::ChangeState);
            break;
          }
          m_addedToNote = val;
          setPercussionModeOn();
          addGenericEvent(beginOffset, 2, "Percussion Mode On", "", Type::ChangeState);
        }
        break;
      }
      case 0xE2: {
        // Set base duration
        m_rawBaseDur = readByte(curOffset++);
        // We multiply the raw base duration by two so we can cleanly halve durations (for event DF)
        m_rawBaseDur *= 2;
        m_baseDur = m_rawBaseDur * 3;
        auto desc = fmt::format("Base Duration - {}", m_baseDur);
        addGenericEvent(beginOffset, 2, "Set Base Duration", desc, Type::DurationChange);
        break;
      }
      case 0xE3:
        // PROGRAM CHANGE
        m_program = readByte(curOffset++);
        if (m_isFmTrack) {
          // TMNT2 is weird. It has 113 FM instruments, but code to handle > 128, however, it just
          // performs a simple &= 0x7F. Values over 127 are used in sequences.
          m_program &= 0x7F;
        }
        addProgramChange(beginOffset, 2, m_program);
        break;
      case 0xE4: {
        m_attenuation = readByte(curOffset++) & 0x7F;
        addGenericEvent(beginOffset, 2, "Set Attenuation", "", Type::Volume);
        if (m_isFmTrack)
          addVolNoItem(0x7F - m_attenuation);
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
        // TODO: used in ssriders seq 11
      case 0xEA:    // NOP
        // TODO: used in ssriders seq 11
        break;
      case 0xEB: {
        // u8 atten = -readByte(curOffset++);
        // if (m_isFmTrack)
        //   addVol(beginOffset, 2, atten);
        // else
        //   addGenericEvent(beginOffset, 2, "Volume", "", Type::Unknown);
        curOffset++;
        addUnknown(beginOffset, 2);
        break;
      }
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
        if ((val & 0x40) > 0) {
          if (!m_isFmTrack)
            setGlobalTranspose(transpose);
          auto desc = fmt::format("Global Transpose - {} semitones", transpose);
          addGenericEvent(beginOffset, 2, "Global Transpose", desc, Type::Transpose);
        }
        else {
          m_transpose = transpose;
          auto desc = fmt::format("Transpose - {} semitones", transpose);
          addGenericEvent(beginOffset, 2, "Transpose", desc, Type::Transpose);
          // addTranspose(beginOffset, 2, transpose);
        }
        break;
      }
      case 0xED: {
        // Pitch bend
        s8 val = static_cast<s8>(readByte(curOffset++));
        if (m_isFmTrack) {
          double cents = val * (100/64.0);
          addPitchBend(beginOffset, 2, cents);
        } else {
          addGenericEvent(beginOffset, 2, "Pitch Bend", "", Type::Unknown);
        }
        break;
      }
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
          auto desc = fmt::format("Destination: {:X}", dest);
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
        callIdx = 0;
        goto callMarker;
      case 0xFD:
        callIdx = 1;
      callMarker: {
        if (m_callOrigin[callIdx] == 0) {
          m_callOrigin[callIdx] = curOffset + 2;
          u16 dest = readShort(curOffset);
          if (dest < parentSeq->dwOffset) {
            return false;
          }
          auto desc = fmt::format("Call - destination: {:X}", dest);
          addGenericEvent(beginOffset, 3, "Call", desc, Type::Jump);
          curOffset = dest;
        } else {
          addGenericEvent(beginOffset, 1, "Return", "", Type::Jump);
          curOffset = m_callOrigin[callIdx];
          m_callOrigin[callIdx] = 0;
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
