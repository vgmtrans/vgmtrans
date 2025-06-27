#include "KonamiArcadeSeq.h"
#include "KonamiArcadeDefinitions.h"
#include "ScaleConversion.h"
#include <spdlog/fmt/fmt.h>

DECLARE_FORMAT(KonamiArcade);

// ***************
// KonamiArcadeSeq
// ***************

KonamiArcadeSeq::KonamiArcadeSeq(
  RawFile *file,
  KonamiArcadeFormatVer fmtVer,
  u32 offset,
  u32 ramOffset,
  const std::array<KonamiArcadeInstrSet::drum, 46>& drums,
  float nmiRate
)
    : VGMSeq(KonamiArcadeFormat::name, file, offset, 0, "Konami Arcade Seq"),
      fmtVer(fmtVer), m_memOffset(ramOffset), m_drums(drums), m_nmiRate(nmiRate) {
  setUseLinearAmplitudeScale(true);
  setAllowDiscontinuousTrackData(true);
  setAlwaysWriteInitialVol(127);
  setAlwaysWriteInitialTempo(120);
}

bool KonamiArcadeSeq::parseHeader() {
  setPPQN(48);
  return true;
}

bool KonamiArcadeSeq::parseTrackPointers() {
  u32 pos = dwOffset;
  int numTracks = 16;
  if (fmtVer == MysticWarrior) {
    // Check to see if this sequence uses 16 tracks or 8 tracks for games with only 1 K054539.
    // We'll check for any pattern of 0x0000 where the second set of track pointers would be, as this
    // is typically present.
    for (int pos = dwOffset + 16; pos < dwOffset + 32; pos += 2) {
      if (readWord(pos) == 0) {
        numTracks = 8;
        break;
      }
    }

    for (int i = 0; i < numTracks; i++) {
      u16 memTrackOffset = readShort(pos);
      if (memTrackOffset == 0) continue;

      u32 romTrackOffset = dwOffset + (memTrackOffset - m_memOffset);

      aTracks.push_back(new KonamiArcadeTrack(this, romTrackOffset, 0));
      pos += 2;
    }
  } else {
    for (int i = 0; i < numTracks; i++) {
      u32 trackOffset = readWordBE(pos);
      if (trackOffset == 0) continue;

      // Racin' Force has tracks that precede the start of the sequence
      if (trackOffset < dwOffset) {
        dwOffset = trackOffset;
      }
      aTracks.push_back(new KonamiArcadeTrack(this, trackOffset, 0));
      pos += 4;
    }
  }
  return true;
}

// *****************
// KonamiArcadeTrack
// *****************


KonamiArcadeTrack::KonamiArcadeTrack(KonamiArcadeSeq *parentSeq, uint32_t offset, uint32_t length)
    : SeqTrack(parentSeq, offset, length), m_inJump(false) {
}

void KonamiArcadeTrack::resetVars() {
  m_inJump = false;
  m_percussionFlag1 = false;
  m_percussionFlag2 = false;
  m_pan = 0;
  m_releaseRate = 0;
  m_curProg = 0;
  m_prevDelta = 0;
  m_duration = 0;
  m_jumpReturnOffset = 0;
  m_inSubRoutine = false;
  m_needsSubroutineEnd = false;
  m_subroutineOffset = -1;
  m_subroutineReturnOffset = -1;
  memset(m_loopCounter, 0, sizeof(m_loopCounter));
  memset(m_loopMarker, 0, sizeof(m_loopMarker));
  memset(m_loopAtten, 0, sizeof(m_loopAtten));
  memset(m_loopTranspose, 0, sizeof(m_loopTranspose));
  SeqTrack::resetVars();
}

u8 KonamiArcadeTrack::calculateMidiPanForK054539(u8 pan) {
  if (pan >= 0x81 && pan <= 0x8f)
    pan -= 0x81;
  else if (pan >= 0x11 && pan <= 0x1f)
    pan -= 0x11;
  else
    pan = 0x18 - 0x11;
  double rightPan = panTable[pan];
  double leftPan = panTable[0xE - pan];
  double volumeScale;
  u8 newPan = convertVolumeBalanceToStdMidiPan(leftPan, rightPan, &volumeScale);
  return newPan;
}

void KonamiArcadeTrack::enablePercussion(bool& flag) {
  flag = true;
  // Drums define their own pan, which is only used if the pan state value is 0
  addBankSelectNoItem(1);
  addProgramChangeNoItem(0, false);
  applyTranspose();
}

void KonamiArcadeTrack::disablePercussion(bool& flag) {
  flag = false;

  // If either percussion flag is still set, exit and don't disable percussion
  if (m_percussionFlag1 || m_percussionFlag2)
    return;

  if (m_pan == 0) {
    u8 midiPan = calculateMidiPanForK054539(0);
    if (midiPan != prevPan)
      addPanNoItem(midiPan);
  }
  addProgramChangeNoItem(m_curProg, true);
  applyTranspose();
}

void KonamiArcadeTrack::applyTranspose() {
  // If percussion is active we cannot transpose by activating a different note since drums use
  // a drumkit instrument (where each note is a different percussive sound). Instead we use fine tune.
  if (m_percussionFlag1 || m_percussionFlag2) {
    transpose = 0;
    if (coarseTuningSemitones != m_driverTranspose) {
      // TODO: uncomment this when we stop using BASS. Bass doesn't properly implement coarse
      // fine-tuning. It shifts the actual note played instead of only affecting pitch.
      // This screws up drum kits
      // addCoarseTuningNoItem(m_driverTranspose);
    }
  }
  else {
    transpose = m_driverTranspose;
    if (coarseTuningSemitones != 0) {
      // TODO: same as above
      // addCoarseTuningNoItem(0);
    }
  }
}

bool KonamiArcadeTrack::readEvent() {
  uint32_t beginOffset = curOffset;
  uint8_t status_byte = readByte(curOffset++);

  if (status_byte == 0x60) {
    addGenericEvent(beginOffset, curOffset - beginOffset, "Percussion On", "", Type::ChangeState);
    enablePercussion(m_percussionFlag1);
    return true;
  }

  if (status_byte == 0x61) {
    addGenericEvent(beginOffset, curOffset - beginOffset, "Percussion Off", "", Type::ChangeState);
    disablePercussion(m_percussionFlag1);
    return true;
  }

  // Note
  if (status_byte < 0xC0) {
    uint8_t note, delta;
    if (status_byte < 0x60) {
      delta = readByte(curOffset++);
      note = status_byte;
      m_prevDelta = delta;
    }
    else {
      delta = m_prevDelta;
      note = status_byte - 0x62;
    }

    uint8_t durOrVel = readByte(curOffset++);
    uint8_t vel;
    if (durOrVel < 0x80) {
      m_duration = durOrVel;
      vel = readByte(curOffset++);
    }
    else {
      vel = durOrVel - 0x80;
    }
    u8 atten = 0x7f - vel;

    u8 linearVel = std::max(0.0, volTable[atten]) * 0x7F;

    // TODO: fix duration calculation. It uses dur as index to table at 0x1F0 in viostorm.
    // Something like this - need to verify the + 0.5 rounding thing
    //
    // if (m_duration > 0 && m_duration < 100) {
    //   durNumerator = durationTable[m_duration];
    //   durFraction = (durNumerator / 255.0)
    //   u8 actualDuration = (u8)(durFraction * delta + 0.5)
    //   // Adjust the result if numerator is even and greater than 128
    //   if (durNumerator > 128 && (durNumerator % 2) == 0) {
    //     actualDuration -= 1;
    //   }
    // }

    note += 24;

    uint32_t actualDuration;
    if (m_duration == 0) {
      if ((m_percussionFlag1 || m_percussionFlag2) && (note - 24) < 46) {
        auto seq = static_cast<KonamiArcadeSeq*>(parentSeq);
        auto& drum = seq->drums()[note-24];
        u8 defaultDrumDur = drum.default_duration;
        actualDuration = (delta * defaultDrumDur) / 100;

        // Drums provide their own pan, but this is only applied when the channel pan value is 0
        if (m_pan == 0) {
          u8 midiPan = calculateMidiPanForK054539(drum.pan | 0x10);
          if (midiPan != prevPan)
            addPanNoItem(midiPan);
        }
      } else {
        actualDuration = delta;
      }
    } else {
      if (m_releaseRate != 0)
        actualDuration = (delta * m_duration) / 100;
      else
        actualDuration = delta;
    }
    addNoteByDur(beginOffset, curOffset - beginOffset, note, linearVel, actualDuration);
    addTime(delta);
    return true;
  }

  int loopNum;
  switch (status_byte) {
    case 0xC0:
      curOffset++;
      addUnknown(beginOffset, curOffset - beginOffset);
      break;

    case 0xC2:    // writes to undocumented reg 0x1BF. Looks like pan value.
    case 0xC3:    // writes to undocumented reg 0x1FF. Looks like pan value.
      curOffset++;
      addUnknown(beginOffset, curOffset - beginOffset);
      break;

    case 0xCD:
      curOffset++;
      addUnknown(beginOffset, curOffset - beginOffset);
      break;

    case 0xCE:
      curOffset += 2;
      addUnknown(beginOffset, curOffset - beginOffset);
      break;

    case 0xCF:
      curOffset += 3;
      addUnknown(beginOffset, curOffset - beginOffset);
      break;

    case 0xD0:
      curOffset += 3;
      addUnknown(beginOffset, curOffset - beginOffset);
      break;

    case 0xD2:  // reverb vol
      curOffset += 2;
      addUnknown(beginOffset, curOffset - beginOffset, "Reverb Volume");
      break;

    case 0xD1:
    case 0xD3:        // writes to undocumented reg 0x13E
    case 0xD4:        // writes to undocumented reg 0x17E - same logic as D3
    case 0xD5:        // writes to undocumented reg 0x1BE - same logic as D3
    case 0xD6:        // writes to undocumented reg 0x1FE - same logic as D3
      curOffset += 2;
      addUnknown(beginOffset, curOffset - beginOffset);
      break;

    case 0xD7:    // writes to undocumented registers 0x216 - 0x21A
      curOffset += 3;
      addUnknown(beginOffset, curOffset - beginOffset);
      break;

    case 0xD8:    // writes to undocumented registers 0x21B - 0x21C
      curOffset += 2;
      addUnknown(beginOffset, curOffset - beginOffset);
      break;

    case 0xD9:    // writes to undocumented registers 0x21D - 0x221, same logic as D7
      curOffset += 3;
      addUnknown(beginOffset, curOffset - beginOffset);
      break;

    case 0xDA:    // writes to undocumented registers 0x222 - 0x223, same logic as D8
      curOffset += 2;
      addUnknown(beginOffset, curOffset - beginOffset);
      break;

    // 0xDE sets a flag distinct from the 0x60/0x61 percussion flag, but still checked to enable percussion
    case 0xDE: {
      uint8_t bEnablePercussion = readByte(curOffset++);
      if (bEnablePercussion != 0) {
        addGenericEvent(beginOffset, curOffset - beginOffset, "Enable Percussion", "", Type::UseDrumKit);
        enablePercussion(m_percussionFlag2);
      } else {
        addGenericEvent(beginOffset, curOffset - beginOffset, "Disable Percussion", "", Type::UseDrumKit);
        disablePercussion(m_percussionFlag2);
      }
      break;
    }

    case 0xDF:
      curOffset += 3;
      addUnknown(beginOffset, curOffset - beginOffset);
      break;

    // Rest
    case 0xE0: {
      uint8_t delta = readByte(curOffset++);
      addRest(beginOffset, curOffset-beginOffset, delta);
      m_prevDelta = delta;
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
      addGenericEvent(beginOffset, curOffset - beginOffset, "Hold", desc, Type::Tie);
      m_prevDelta = delta;
      break;
    }

    // program change
    case 0xE2: {
      m_curProg = readByte(curOffset++);
      if (m_curProg > 0x7F) {
        printf("program # is greater than 0x7f\n");
      }
      addProgramChange(beginOffset, curOffset - beginOffset, m_curProg);
      break;
    }

    // Pan
    case 0xE3: {
      m_pan = (readByte(curOffset++));
      u8 midiPan = calculateMidiPanForK054539(m_pan | 0x10);
      addPan(beginOffset, curOffset - beginOffset, midiPan);
      break;
    }

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
      m_loopMarker[loopNum] = curOffset;
      auto name = fmt::format("Loop {:d} Marker", loopNum);
      addGenericEvent(beginOffset, curOffset - beginOffset, name, "", Type::RepeatStart);
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
      if (m_loopCounter[loopNum] == 0) {
        if (loopCount == 1) {
          m_loopAtten[loopNum] = 0;
          m_loopTranspose[loopNum] = 0;
          break;
        }
        m_loopCounter[loopNum] = loopCount - 1;
        m_loopAtten[loopNum] = initialLoopAtten;
        m_loopTranspose[loopNum] = initialLoopTranspose;
      }
      else {
        if (loopCount > 0 && --m_loopCounter[loopNum] == 0) {
          m_loopAtten[loopNum] = 0;
          m_loopTranspose[loopNum] = 0;
          break;
        }
        m_loopAtten[loopNum] += initialLoopAtten;
        m_loopTranspose[loopNum] += initialLoopTranspose;
      }

      // TODO: confusing logic sets a couple channel state vars here based on atten and transpose

      bool shouldContinue = true;
      if (loopCount == 0) {
        shouldContinue = addLoopForever(beginOffset, curOffset - beginOffset);
      }
      else {
        auto desc = fmt::format("count: {:d}  attenuation: {:d}  transpose {:d}", loopCount,
          initialLoopAtten, initialLoopTranspose);
        addGenericEvent(beginOffset, curOffset - beginOffset, "Loop", desc, Type::RepeatEnd);
      }
      if (m_loopMarker[loopNum] < parentSeq->dwOffset) {
        L_ERROR("KonamiArcadeSeq wants to loopMarker outside bounds of sequence. Loop at %X", beginOffset);
        return false;
      }
      curOffset = m_loopMarker[loopNum];
      return shouldContinue;
    }

    //tempo
    case 0xEA: {
      u8 tempoValue = readByte(curOffset++);
      auto seq = static_cast<KonamiArcadeSeq*>(parentSeq);
      // if (seq->fmtVer == MysticWarrior) {
      if (true) {
        // The tempo value is added to a counter every other NMI. When it carries, a tick is processed
        double nmisPerTick = 256.0 / static_cast<double>(tempoValue);
        float nmiRate = seq->nmiRate();

        double timePerTickSeconds = nmisPerTick / nmiRate;
        double timePerBeatSeconds = timePerTickSeconds * parentSeq->ppqn();
        double timePerBeatMicroseconds = timePerBeatSeconds * 1000000;
        addTempo(beginOffset, curOffset - beginOffset, timePerBeatMicroseconds);
      } else {
        addTempoBPM(beginOffset, curOffset - beginOffset, tempoValue * 1.2);
      }
      break;
    }

    case 0xEB:
      curOffset += 2;
      addUnknown(beginOffset, curOffset - beginOffset);
      break;


    case 0xEC: {
      m_driverTranspose = readByte(curOffset++);
      addTranspose(beginOffset, curOffset - beginOffset, m_driverTranspose);
      applyTranspose();
      break;
    }

    case 0xED:
      curOffset += 3;
      addUnknown(beginOffset, curOffset - beginOffset);
      break;

    // Volume
    case 0xEE: {
      u8 vol = readByte(curOffset++);
      u8 volIndex = ~vol & 0x7F;
      m_vol = volIndex;
      u8 linearVol = volTable[volIndex] * 0x7F;
      addVol(beginOffset, curOffset - beginOffset, linearVol);
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
      m_subroutineOffset = curOffset;
      m_needsSubroutineEnd = true;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Subroutine Start", "", Type::Loop);
      break;

    case 0xF7:
      if (m_needsSubroutineEnd) {
        m_needsSubroutineEnd = false;
        m_inSubRoutine = false;
        addGenericEvent(beginOffset, curOffset - beginOffset, "Subroutine Return", "", Type::Loop);
      } else {
        if (m_inSubRoutine) {
          curOffset = m_subroutineReturnOffset;
          m_inSubRoutine = false;
        } else {
          addGenericEvent(beginOffset, curOffset - beginOffset, "Call Subroutine", "", Type::Loop);
          m_inSubRoutine = true;
          m_subroutineReturnOffset = curOffset;
          curOffset = m_subroutineOffset;
        }
      }
      break;

    case 0xF8:
      curOffset += 2;
      addUnknown(beginOffset, curOffset - beginOffset);
      break;

    case 0xF9: {
      u8 data = readByte(curOffset++);
      addUnknown(beginOffset, curOffset - beginOffset);
      break;
    }

    //release rate
    case 0xFA: {
      // lower value results in longer sustain
      m_releaseRate = readByte(curOffset++);
      addUnknown(beginOffset, curOffset - beginOffset, "Release Rate");
      break;
    }

    case 0xFD: {
      auto seq = static_cast<KonamiArcadeSeq*>(parentSeq);
      u32 dest;
      int eventLength = seq->fmtVer == MysticWarrior ? 3 : 5;
      if (seq->fmtVer == MysticWarrior) {
        u16 memJumpOffset = readShort(curOffset);
        dest = seq->dwOffset + (memJumpOffset - seq->memOffset());
      } else {
        dest = getWordBE(curOffset);
      }
      bool shouldContinue = true;
      if (dest < beginOffset) {
        shouldContinue = addLoopForever(beginOffset, eventLength);
      } else {
        auto desc = fmt::format("Destination: ${:X}", dest);
        addGenericEvent(beginOffset, eventLength, "Jump", desc, Type::Loop);
      }
      if (dest < seq->dwOffset) {
        L_ERROR("KonamiArcadeEvent FD attempted jump to offset outside the sequence at {:X}.  Jump offset: {:X}", beginOffset, dest);
        return false;
      }
      curOffset = dest;
      return shouldContinue;
    }

    case 0xFE: {
      m_inJump = true;
      u32 dest;
      auto seq = static_cast<KonamiArcadeSeq*>(parentSeq);
      if (seq->fmtVer == MysticWarrior) {
        m_jumpReturnOffset = curOffset + 2;
        u16 memJumpOffset = readShort(curOffset);
        dest = seq->dwOffset + (memJumpOffset - seq->memOffset());
      } else {
        m_jumpReturnOffset = curOffset + 4;
        dest = getWordBE(curOffset);
      }
      if (dest < seq->dwOffset) {
        L_ERROR("KonamiArcade Event FE attempted jump to offset outside the sequence at {:X}.  Jump offset: {:X}", beginOffset, dest);
        return false;
      }
      auto desc = fmt::format("Destination: ${:X}", dest);
      addGenericEvent(beginOffset, m_jumpReturnOffset - beginOffset, "Jump", desc, Type::Loop);
      curOffset = dest;
      break;
    }
    case 0xFF: {
      if (m_inJump) {
        addGenericEvent(beginOffset, 1, "Return", "", Type::Loop);
        m_inJump = false;
        curOffset = m_jumpReturnOffset;
      }
      else {
        addEndOfTrack(beginOffset, curOffset - beginOffset);
        return false;
      }
      break;
    }

    default:
      addEndOfTrack(beginOffset, curOffset - beginOffset);
      return false;
  }
  return true;
}