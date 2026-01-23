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
  float nmiRate,
  const std::string& name
)
    : VGMSeq(KonamiArcadeFormat::name, file, offset, 0, name),
      fmtVer(fmtVer), m_memOffset(ramOffset), m_drums(drums), m_nmiRate(nmiRate) {
  bLoadTickByTick = true;
  setUseLinearAmplitudeScale(true);
  setAllowDiscontinuousTrackData(true);
  setAlwaysWriteInitialVol(127);
  setAlwaysWriteInitialExpression(127);
  setAlwaysWriteInitialTempo(120);
  useReverb();
}

void KonamiArcadeSeq::resetVars() {
  VGMSeq::resetVars();
  u8 m_tempoSlideDuration = 0;
  double m_tempoSlideIncrement = 0;
}

bool KonamiArcadeSeq::parseHeader() {
  setPPQN(48);
  return true;
}

bool KonamiArcadeSeq::parseTrackPointers() {
  u32 pos = dwOffset;
  int numTracks = 16;

  auto trackPtrTableItem = addChild(dwOffset, numTracks * 2, "Track Pointer Table");

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

      trackPtrTableItem->addChild(pos, 2, fmt::format("Track {} Pointer", i));

      aTracks.push_back(new KonamiArcadeTrack(this, romTrackOffset, 0));
      pos += 2;
    }
  } else {
    for (int i = 0; i < numTracks; i++) {
      u32 trackOffset = readWordBE(pos);
      if (trackOffset == 0) continue;

      // Racin' Force has jumps to sequence data that precede the start of the sequence
      if (trackOffset < dwOffset) {
        dwOffset = trackOffset;
      }

      trackPtrTableItem->addChild(pos, 4, fmt::format("Track {} Pointer", i));

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
  m_driverTranspose = 0;
  m_prevNoteAbsTime = 0;
  m_prevNoteDur = 0;
  m_prevNoteDelta = 0;
  m_prevFinalKey = 0;
  m_tiePrevNote = false;
  m_didCancelDurTie = false;
  m_microsecsPerTick = 0;
  m_vol = 0;
  m_pan = 8;
  m_actualPan = 0x800;
  m_volSlideDuration = 0;
  m_volSlideTarget = 0;
  m_volSlideIncrement = 0;
  m_panSlideDuration = 0;
  m_panSlideTarget = 0;
  m_panSlideIncrement = 0;
  m_portamentoTime = 0;
  m_slideModeDelay = 0;
  m_slideModeDuration = 0;
  m_slideModeDepth = 0;
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
  addBankSelectNoItem(2);
  addProgramChangeNoItem(0, false);
  m_didCancelDurTie = true;
  applyTranspose();
}

void KonamiArcadeTrack::disablePercussion(bool& flag) {
  flag = false;

  // If either percussion flag is still set, exit and don't disable percussion
  if (percussionEnabled())
    return;

  if (m_pan == 0) {
    u8 midiPan = calculateMidiPanForK054539(0);
    if (midiPan != prevPan)
      addPanNoItem(midiPan);
  }
  addProgramChangeNoItem(m_curProg, true);
  m_didCancelDurTie = true;
  applyTranspose();
}

bool KonamiArcadeTrack::percussionEnabled() {
  return m_percussionFlag1 || m_percussionFlag2;
}

void KonamiArcadeTrack::applyTranspose() {
  // If percussion is active we cannot transpose by activating a different note since drums use
  // a drumkit instrument (where each note is a different percussive sound). Instead we use fine tune.
  if (percussionEnabled()) {
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

void KonamiArcadeTrack::makeTrulyPrevDurNoteEnd(uint32_t absTime) const {
  if (readMode == READMODE_CONVERT_TO_MIDI) {
    if (pMidiTrack->prevDurNoteOffs.size() > 0) {
      auto prevDurNoteOff = pMidiTrack->prevDurNoteOffs.back();
      prevDurNoteOff->absTime = absTime;
    }
  }
}


std::pair<double, double>  KonamiArcadeTrack::calculateTempo(double tempoValue) {
  // The tempo value is added to a counter every other NMI. When it carries, a tick is processed
  double nmisPerTick = 256.0 / tempoValue;
  float nmiRate = static_cast<KonamiArcadeSeq*>(parentSeq)->nmiRate();

  double timePerTickSeconds = nmisPerTick / nmiRate;
  double timePerBeatSeconds = timePerTickSeconds * parentSeq->ppqn();
  double timePerBeatMicrosecs = timePerBeatSeconds * 1'000'000.0;
  return { timePerTickSeconds, timePerBeatMicrosecs };
}

void KonamiArcadeTrack::onTickBegin() {
  // Handle Volume Slide
  if (m_volSlideDuration > 0) {
    m_volSlideDuration -= 1;
    if (m_volSlideDuration == 0) {
      m_vol = m_volSlideTarget;
    } else {
      m_vol += m_volSlideIncrement;
      m_vol = std::max<s16>(m_vol, 0);
    }
    u8 volByte = m_vol >> 8;
    u8 volIndex = ~volByte & 0x7F;
    u8 linearVol = volTable[volIndex] * 0x7F;
    if (linearVol != vol) {
      addVolNoItem(linearVol);
    }
  }

  // Handle Pan Slide
  if (m_panSlideDuration > 0) {
    m_panSlideDuration -= 1;
    if (m_panSlideDuration == 0) {
      m_actualPan = m_panSlideTarget;
    } else {
      m_actualPan += m_panSlideIncrement;
      m_actualPan = std::clamp<s16>(m_actualPan, 0x100, 0x1F00);
    }
    u8 panByte = m_actualPan >> 8;
    u8 midiPan = calculateMidiPanForK054539(panByte | 0x10);
    if (midiPan != prevPan) {
      addPanNoItem(midiPan);
    }
  }

  // Handle Tempo Slide
  if (channel == 0) {
    auto seq = static_cast<KonamiArcadeSeq*>(parentSeq);
    if (seq->getTempoSlideDuration() > 0) {
      m_tempo += seq->getTempoSlideIncrement();
      auto [secondsPerTick, microsecsPerBeat] = calculateTempo(m_tempo);
      addTempoNoItem(microsecsPerBeat);
      seq->setTempoSlideDuration(seq->getTempoSlideDuration() - 1);
      m_microsecsPerTick = secondsPerTick * 1000.0;
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
      note = status_byte;
      delta = readByte(curOffset++);
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
    s16 atten = 0x7f - vel + m_loopAtten[0] + m_loopAtten[1];
    atten = std::clamp<s16>(atten, 0, 255);

    u8 linearVel = std::round(std::max(0.0, volTable[atten]) * 0x7F);

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
      if (percussionEnabled() && (note - 24) < 46) {
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
        actualDuration = std::max(1, (delta * m_duration) / 100);
      else
        actualDuration = delta;
    }
    note += m_loopTranspose[0] / 32 + m_loopTranspose[1] / 32;
    note = note > 0x7F ? 0x7F : note;

    // If an event caused the duration tie to be canceled, remove the duration tie flag and set
    // expression back to max (unless this is a new tied duration note, which will add its own expression)
    if (m_didCancelDurTie && m_tiePrevNote) {
      m_tiePrevNote = false;
      if (m_duration != 100 || percussionEnabled()) {
        addExpressionNoItem(127);
      }
    }

    // When the duration is 100%, the note acts like a tie. However, in this state, the note
    // 'velocity' (as we treat it) still affects volume. We handle this by setting the MIDI velocity
    // of these notes to max (0x7F) and manipulating volume with expression events.
    if ((m_tiePrevNote || m_duration == 100) && !percussionEnabled()) {
      addExpressionNoItem(linearVel);
      linearVel = 0x7F;
    }

    if (m_portamentoTime > 0 && prevKey != note) {
      if (m_prevNoteAbsTime + m_prevNoteDur == getTime()) {
        addPortamentoControlNoItem(m_prevFinalKey);
        makeTrulyPrevDurNoteEnd(getTime() + 1);
        addNoteByDur(beginOffset, curOffset - beginOffset, note, linearVel, actualDuration);
      } else if (actualDuration > 2) {
        addNoteByDurNoItem(prevKey, linearVel, 2);
        insertPortamentoControlNoItem(m_prevFinalKey, getTime() + 1);
        insertNoteByDur(beginOffset, curOffset - beginOffset, note, linearVel, actualDuration - 1, getTime() + 1);
      } else {
        L_DEBUG("{} at {:X}: didn't apply portamento because note duration <= 2.", parentSeq->name(), beginOffset);
        addNoteByDur(beginOffset, curOffset - beginOffset, note, linearVel, actualDuration);
      }
    } else if (m_slideModeDepth != 0 && m_slideModeDuration > 0 && actualDuration > (m_slideModeDelay + 1)) {
      // Slide note
      u8 slideStartNote = note - m_slideModeDepth;
      addNoteByDurNoItem(slideStartNote, linearVel, m_slideModeDelay + 1);
      insertPortamentoTime14BitNoItem(m_slideModeDuration * m_microsecsPerTick, m_slideModeDelay);
      insertPortamentoControlNoItem(slideStartNote, getTime() + m_slideModeDelay);
      insertNoteByDur(beginOffset, curOffset - beginOffset, note, linearVel, actualDuration - m_slideModeDelay, getTime() + m_slideModeDelay);
    } else {

      // When the previous note was tied, and this note is the same, just extend the previous note
      // instead of creating a new note.
      if (m_tiePrevNote && note == prevKey) {
        makePrevDurNoteEnd(getTime() + actualDuration);
        auto desc = fmt::format("Note with Duration (tied) - abs key: {} ({}), velocity: {}, duration: {}",
          static_cast<int>(prevKey), MidiEvent::getNoteName(prevKey), static_cast<int>(linearVel), actualDuration);
        addTie(beginOffset, curOffset - beginOffset, actualDuration, "Note with Duration (tied)", desc);
      } else {
        addNoteByDur(beginOffset, curOffset - beginOffset, note, linearVel, actualDuration);
      }
    }
    if (m_tiePrevNote && (m_duration != 100 || percussionEnabled())) {
      insertExpressionNoItem(127, getTime() + actualDuration);
    }
    m_tiePrevNote = m_duration == 100 && !percussionEnabled();
    m_didCancelDurTie = false;

    m_prevNoteAbsTime = getTime();
    m_prevNoteDur = actualDuration;
    m_prevNoteDelta = delta;
    m_prevFinalKey = note + transpose;    // needed for sliding prev notes with portamento control
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

    // some kind of alternative pan slide
    case 0xCD:
      curOffset++;
      addUnknown(beginOffset, curOffset - beginOffset);
      break;

    // yet another kind of pan slide
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
      m_didCancelDurTie = true;
      break;
    }

    // Hold
    case 0xE1: {
      uint8_t delta = readByte(curOffset++);
      uint8_t dur = readByte(curOffset++);
      uint32_t newdur = (delta * dur) / 0x64;
      addTime(newdur);
      makePrevDurNoteEnd();
      addTime(delta - newdur);
      auto desc = fmt::format("total delta: {:d} ticks  additional duration: {:d} ticks", delta, newdur);
      addTie(beginOffset, curOffset - beginOffset, newdur, "Hold", desc);
      m_prevNoteDur += newdur;
      m_prevNoteDelta += delta;
      m_prevDelta = delta;
      m_didCancelDurTie = true;
      break;
    }

    // Program Change
    case 0xE2: {
      m_curProg = readByte(curOffset++);
      addProgramChange(beginOffset, curOffset - beginOffset, m_curProg, true);
      m_didCancelDurTie = true;
      break;
    }

    // Pan
    case 0xE3: {
      m_pan = (readByte(curOffset++));
      m_actualPan = m_pan << 8;
      u8 midiPan = calculateMidiPanForK054539(m_pan | 0x10);
      addPan(beginOffset, curOffset - beginOffset, midiPan);
      m_panSlideDuration = 0;
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

    // Loop Start Marker
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

    // Loop
    case 0xE7:
      loopNum = 0;
      goto doLoop;
    case 0xE9: {
      loopNum = 1;
    doLoop:
      u8 loopCount = readByte(curOffset++);
      s8 initialLoopAtten = -readByte(curOffset++);
      s8 initialLoopTranspose = readByte(curOffset++);
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

    // Tempo
    case 0xEA: {
      m_tempo = readByte(curOffset++);
      auto [secondsPerTick, microsecsPerBeat] = calculateTempo(m_tempo);
      addTempo(beginOffset, curOffset - beginOffset, microsecsPerBeat);
      m_microsecsPerTick = secondsPerTick * 1000.0;
      auto seq = static_cast<KonamiArcadeSeq*>(parentSeq);
      seq->setTempoSlideDuration(0);
      break;
    }

    // Tempo Slide
    case 0xEB: {
      auto seq = static_cast<KonamiArcadeSeq*>(parentSeq);
      auto tempoSlideDuration = readByte(curOffset++);
      seq->setTempoSlideDuration(tempoSlideDuration);
      double targetTempo = readByte(curOffset++);
      double tempoDiff = targetTempo - m_tempo;
      seq->setTempoSlideIncrement(tempoDiff / tempoSlideDuration);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Tempo Slide", "", Type::Tempo);
      break;
    }


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
      u8 volByte = readByte(curOffset++);
      u8 volIndex = ~volByte & 0x7F;
      u8 linearVol = volTable[volIndex] * 0x7F;
      addVol(beginOffset, curOffset - beginOffset, linearVol);
      m_vol = volByte << 8;
      m_volSlideDuration = 0;
      break;
    }

    // Volume Slide
    case 0xEF: {
      m_volSlideDuration = readByte(curOffset++);
      m_volSlideTarget = readByte(curOffset++) << 8;
      m_volSlideIncrement = (m_volSlideTarget - m_vol) /  m_volSlideDuration;
      auto desc = fmt::format("Duration: {:d}  Target Volume: {}", m_volSlideDuration, m_volSlideTarget);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Volume Slide", desc, Type::VolumeSlide);
      break;
    }

    case 0xF0: {
      m_portamentoTime = readByte(curOffset++);
      m_slideModeDelay = 0;
      m_slideModeDuration = 0;

      if (m_portamentoTime > 0) {
        u16 actualPortamentoTime = m_portamentoTime * m_microsecsPerTick;
        addPortamentoTime14BitNoItem(actualPortamentoTime);
        auto desc = fmt::format("Portamento Time: {:d} milliseconds", actualPortamentoTime);
        addGenericEvent(beginOffset, curOffset - beginOffset, "Portamento On", desc, Type::Portamento);
      } else {
        addGenericEvent(beginOffset, curOffset - beginOffset, "Portamento Off", "", Type::Portamento);
      }
      break;
    }

    // Pitch Slide Mode
    // When enabled, triggers a pitch slide similar to event F3 on every note on.
    // Unlike F3, pitch is given as a relative value, and the slide goes FROM
    // the new pitch to the pitch of the triggered note (F3 slides from note to target note)
    // Pitch slide is disabled by setting the duration value to 0.
    case 0xF1: {
      m_slideModeDelay = readByte(curOffset++);
      m_slideModeDelay = m_slideModeDelay == 0 ? 1 : m_slideModeDelay;
      m_slideModeDuration = readByte(curOffset++);
      m_slideModeDepth = readByte(curOffset++);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Slide Mode", "", Type::PitchBendSlide);
      break;
    }

    // Pitch Bend
    case 0xF2: {
      s8 pitchBend = readByte(curOffset++);
      // data byte of 0x40 == 1 semitone. Equivalent to 4096 in MIDI when range is default 2 semitones
      addPitchBend(beginOffset, curOffset - beginOffset, pitchBend * 64);
      break;
    }

    // Pitch Slide
    case 0xF3: {
      u8 delay = readByte(curOffset++);
      u8 duration = readByte(curOffset++);
      u8 targetNote = readByte(curOffset++);
      delay = delay == 0 ? 1 : delay;
      targetNote += 24;
      auto desc = fmt::format("Delay: {:d}  Duration: {:d}  Target Note: {:d}",
        delay, duration, targetNote);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Pitch Slide", desc, Type::PitchBendSlide);

      // Pitch slide events are given immediately after the note to be slid. Since the slide is
      // expressed using a target note rather than cents, we use portamento control rather than
      // pitch bend, since the range could be huge, and portamento is closer in representation.

      // Since portamento creates a slide from overlapping note events, the strategy is to insert
      // a new note which overlaps with the previous duration note at the correct point (the delay value).
      // We shorten the duration of the interrupted note for cleanliness sake.

      // First check that the delay isn't longer than the previous note duration. This can still
      // have an effect because a note can be audible after note off via the software release
      // envelope. We can't account for this unless (maybe) we use pitch bend events instead of portamento.
      if (delay >= m_prevNoteDur) {
        L_DEBUG("Ignoring pitch slide event with a delay > note dur. Offset: {:X}  Time: {}\n", beginOffset, getTime());
        break;
      }

      makeTrulyPrevDurNoteEnd(m_prevNoteAbsTime + delay + 1);  // add one to ensure the notes overlap to trigger portamento effect
      insertPortamentoTime14BitNoItem(duration * m_microsecsPerTick, m_prevNoteAbsTime + delay);
      insertPortamentoControlNoItem(m_prevFinalKey, m_prevNoteAbsTime + delay);
      u32 newNoteDur = m_prevNoteDur - delay;
      insertNoteByDurNoItem(targetNote, prevVel, newNoteDur, m_prevNoteAbsTime + delay);
      if (m_portamentoTime > 0) {
        insertPortamentoTime14BitNoItem(m_portamentoTime * m_microsecsPerTick, m_prevNoteAbsTime + m_prevNoteDur);
      }
      break;
    }

    // Subroutine Start
    case 0xF6:
      m_subroutineOffset = curOffset;
      m_needsSubroutineEnd = true;
      addGenericEvent(beginOffset, curOffset - beginOffset, "Subroutine Start", "", Type::Loop);
      break;

    // Subroutine Return / Call Subroutine
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

    // Pan Slide
    case 0xF8: {
      m_panSlideDuration = readByte(curOffset++);
      m_panSlideTarget = readByte(curOffset++) << 8;
      m_panSlideIncrement = (m_panSlideTarget - m_actualPan) /  m_panSlideDuration;
      auto desc = fmt::format("Duration: {:d}  Target Pan: {}", m_panSlideDuration, m_panSlideTarget);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Pan Slide", desc, Type::PanSlide);
      break;
    }

    case 0xF9: {
      u8 data = readByte(curOffset++);
      addUnknown(beginOffset, curOffset - beginOffset);
      break;
    }

    // Release Rate
    case 0xFA: {
      // lower value results in longer sustain
      m_releaseRate = readByte(curOffset++);
      addUnknown(beginOffset, curOffset - beginOffset, "Release Rate");
      break;
    }

    // Jump
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

    // Call
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

    // Return  / End of Track
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
