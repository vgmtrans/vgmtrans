/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file

 * GBA MusicPlayer2000 (Sappy)
 *
 * Special Thanks / Respects To:
 * GBAMusRiper (c) 2012 by Bregalad
 * http://www.romhacking.net/utilities/881/
 * http://www.romhacking.net/documents/%5B462%5Dsappy.txt
 */

#include "MP2kSeq.h"

#include <array>
#include <spdlog/fmt/fmt.h>
#include "automation/SeqTrackAutomation.h"
#include "MidiFile.h"
#include "MP2kFormat.h"

DECLARE_FORMAT(MP2k);

constexpr uint8_t kMp2kModTypeVibrato = 0;
constexpr uint8_t kMp2kModTypeTremolo = 1;
constexpr uint8_t kMp2kModTypePan = 2;

const char* mp2kModTypeName(uint8_t type) {
  switch (type) {
    case kMp2kModTypeVibrato:
      return "Vibrato";
    case kMp2kModTypeTremolo:
      return "Tremolo";
    case kMp2kModTypePan:
      return "Pan";
    default:
      return "Unknown";
  }
}

MP2kSeq::MP2kSeq(RawFile *file, uint32_t offset, std::string name)
    : VGMSeq(MP2kFormat::name, file, offset, 0, std::move(name)) {
  setAllowDiscontinuousTrackData(true);
}

bool MP2kSeq::parseHeader() {
  if (offset() + 2 > vgmFile()->endOffset()) {
    return false;
  }

  nNumTracks = readShort(offset());

  // if there are no tracks or there are more tracks than allowed
  // return an error; the sequence shall be deleted
  if (nNumTracks == 0 || nNumTracks > 24) {
    return false;
  }
  if (offset() + 8 + nNumTracks * 4 > vgmFile()->endOffset()) {
    return false;
  }

  VGMHeader *seqHdr = addHeader(offset(), 8 + nNumTracks * 4, "Sequence header");
  seqHdr->addChild(offset(), 1, "Number of tracks");
  seqHdr->addChild(offset() + 1, 1, "Unknown");
  seqHdr->addChild(offset() + 2, 1, "Priority");
  seqHdr->addChild(offset() + 3, 1, "Reverb");

  uint32_t dwInstPtr = readWord(offset() + 4);
  seqHdr->addPointer(offset() + 4, 4, dwInstPtr - 0x8000000, true, "Instrument pointer");
  for (u32 i = 0; i < nNumTracks; i++) {
    uint32_t dwTrackPtrOffset = offset() + 8 + i * 4;
    uint32_t dwTrackPtr = readWord(dwTrackPtrOffset);
    seqHdr->addPointer(dwTrackPtrOffset, 4, dwTrackPtr - 0x8000000, true, "Track pointer");
  }

  setPPQN(0x30);

  return true;
}

bool MP2kSeq::parseTrackPointers(void) {
  // Add each tracks
  for (unsigned int i = 0; i < nNumTracks; i++) {
    uint32_t dwTrackPtrOffset = offset() + 8 + i * 4;
    uint32_t dwTrackPtr = readWord(dwTrackPtrOffset);
    aTracks.push_back(new MP2kTrack(this, dwTrackPtr - 0x8000000));
  }

  // Make seq offset the first track offset
  for (auto itr = aTracks.begin(); itr != aTracks.end(); ++itr) {
    SeqTrack *track = (*itr);
    if (track->offset() < offset()) {
      if (length() != 0) {
        setLength(length() + ((offset() - track->offset())));
      }
      setOffset(track->offset());
    }
  }

  return true;
}

//  *********
//  MP2kTrack
//  *********

MP2kTrack::MP2kTrack(MP2kSeq *parentFile, uint32_t offset, uint32_t length)
    : SeqTrack(parentFile, offset, length) {
}

void MP2kTrack::resetVars() {
  SeqTrack::resetVars();
  state = State::Note;
  curDuration = 0;
  current_vel = 0;
  loopEndPositions.clear();
  modType = kMp2kModTypeVibrato;
  vibratoLfo.reset();
  tremoloLfo.reset();
  constexpr uint8_t kDefaultLfoSpeed = 22;
  vibratoLfo.configure(0, kDefaultLfoSpeed, 0);
  tremoloLfo.configure(0, kDefaultLfoSpeed, 0);
}

void MP2kTrack::onTickBegin() {
  updateLfoFade();
}

bool MP2kTrack::readEvent() {
  static constexpr std::array<u8, 0x31> kLengthTable{
      0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C,
      0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x1C,
      0x1E, 0x20, 0x24, 0x28, 0x2A, 0x2C, 0x30, 0x34, 0x36, 0x38, 0x3C, 0x40, 0x42,
      0x44, 0x48, 0x4C, 0x4E, 0x50, 0x54, 0x58, 0x5A, 0x5C, 0x60};
  uint32_t beginOffset = curOffset;
  uint8_t status_byte = readByte(curOffset++);
  bool bContinue = true;

  /* Status change event (note, vel (fade), vol, ...) */
  if (status_byte <= 0x7F) {
    handleStatusCommand(beginOffset, status_byte);
  } else if (status_byte >= 0x80 && status_byte <= 0xB0) { /* Rest event */
    addRest(beginOffset, curOffset - beginOffset, kLengthTable[status_byte - 0x80] * 2);
  } else if (status_byte >= 0xD0) { /* Note duration event */
    state = State::Note;
    curDuration = kLengthTable[status_byte - 0xCF] * 2;

    /* Assume key and velocity values if the next value is greater than 0x7F */
    if (readByte(curOffset) > 0x7F) {
      beginNoteLfo();
      addNoteByDur(beginOffset, curOffset - beginOffset, prevKey, prevVel, curDuration,
                   "Duration Note State + Note On (prev key and vel)");
    } else {
      addGenericEvent(beginOffset, curOffset - beginOffset, "Duration Note State", "",
                      Type::ChangeState);
    }
  } else if (status_byte == 0xB1) { /* End of track */
    addEndOfTrack(beginOffset, curOffset - beginOffset);
    return false;
  } else if (status_byte == 0xB2) { /* Goto */
    uint32_t destOffset = getWord(curOffset) - 0x8000000;
    curOffset += 4;
    uint32_t length = curOffset - beginOffset;
    uint32_t dwEndTrackOffset = curOffset;

    curOffset = destOffset;
    if (!isOffsetUsed(destOffset) || loopEndPositions.size() != 0) {
      addGenericEvent(beginOffset, length, "Goto", "", Type::LoopForever);
    } else {
      bContinue = addLoopForever(beginOffset, length, "Goto");
    }

    // Add next end of track event
    if (readMode == READMODE_ADD_TO_UI) {
      if (dwEndTrackOffset < this->parentSeq->vgmFile()->endOffset()) {
        uint8_t nextCmdByte = readByte(dwEndTrackOffset);
        if (nextCmdByte == 0xB1) {
          addEndOfTrack(dwEndTrackOffset, 1);
        }
      }
    }
  } else if (status_byte > 0xB2 && status_byte <= 0xCF) { /* Special event */
    handleSpecialCommand(beginOffset, status_byte);
  }

  return bContinue;
}

void MP2kTrack::handleStatusCommand(u32 beginOffset, u8 status_byte) {
  switch (state) {
    case State::Note: {
      /* Velocity update might be needed */
      if (readByte(curOffset) <= 0x7F) {
        current_vel = readByte(curOffset++);
        /* If the next value is 0-127, it's unknown (velocity-related?) */
        if (readByte(curOffset) <= 0x7F) {
          curOffset++;
        }
      }
      beginNoteLfo();
      addNoteByDur(beginOffset, curOffset - beginOffset, status_byte, current_vel, curDuration);
      break;
    }

    case State::Tie: {
      /* Velocity update might be needed */
      if (readByte(curOffset) <= 0x7F) {
        current_vel = readByte(curOffset++);
        /* If the next value is 0-127, it's unknown (velocity-related?) */
        if (readByte(curOffset) <= 0x7F) {
          curOffset++;
        }
      }
      beginNoteLfo();
      addNoteOn(beginOffset, curOffset - beginOffset, status_byte, current_vel, "Tie");
      break;
    }

    case State::TieEnd: {
      addNoteOff(beginOffset, curOffset - beginOffset, status_byte, "End Tie");
      break;
    }

    case State::Vol: {
      addVol(beginOffset, curOffset - beginOffset, status_byte);
      break;
    }

    case State::Pan: {
      addPan(beginOffset, curOffset - beginOffset, status_byte);
      break;
    }

    case State::PitchBend: {
      addPitchBend(beginOffset, curOffset - beginOffset, static_cast<int16_t>(status_byte - 0x40) * 128);
      break;
    }

    case State::Modulation: {
      const uint8_t depth = status_byte;
      const auto desc = fmt::format("Depth: {}", depth);
      setModulationDepth(depth);
      switch (modType) {
        case kMp2kModTypeVibrato:
          addGenericEvent(beginOffset, curOffset - beginOffset, "Modulation Depth", desc,
                          Type::Modulation);
          break;
        case kMp2kModTypeTremolo:
          addGenericEvent(beginOffset, curOffset - beginOffset, "Tremolo Depth", desc,
                          Type::Tremelo);
          break;
        case kMp2kModTypePan:
          addGenericEvent(beginOffset, curOffset - beginOffset, "Pan LFO Depth", desc,
                          Type::PanLfo);
          break;
        default:
          addGenericEvent(beginOffset, curOffset - beginOffset, "Modulation Depth", desc,
                          Type::Modulation);
          break;
      }
      break;
    }

    default: {
      L_WARN("Illegal status {} {}", static_cast<uint8_t>(state), status_byte);
      break;
    }
  }
}

void MP2kTrack::handleSpecialCommand(u32 beginOffset, u8 status_byte) {
  switch (status_byte) {
    // Branch
    case 0xB3: {
      uint32_t destOffset = getWord(curOffset);
      curOffset += 4;
      loopEndPositions.push_back(curOffset);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Pattern Play", "", Type::Loop);
      curOffset = destOffset - 0x8000000;
      break;
    }

    // Branch Break
    case 0xB4: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "Pattern End", "", Type::Loop);
      if (loopEndPositions.size() != 0) {
        curOffset = loopEndPositions.back();
        loopEndPositions.pop_back();
      }
      break;
    }

    case 0xBA: {
      const uint8_t priority = readByte(curOffset++);
      const auto desc = fmt::format("Priority: {}", priority);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Priority", desc, Type::Priority);
      break;
    }

    case 0xBB: {
      constexpr double kGbaFrameRateHz = 16777216.0 / 280896.0; // 59.7275005696 Hz
      constexpr double kMp2kTempoScale = 2.0 * (kGbaFrameRateHz / 60.0);
      double tempoBpm = static_cast<double>(readByte(curOffset++)) * kMp2kTempoScale;
      addTempoBPM(beginOffset, curOffset - beginOffset, tempoBpm);
      break;
    }

    case 0xBC: {
      const int8_t keyShift = static_cast<int8_t>(readByte(curOffset++));
      addTranspose(beginOffset, curOffset - beginOffset, keyShift, "Key Shift");
      break;
    }

    case 0xBD: {
      uint8_t progNum = readByte(curOffset++);
      addProgramChange(beginOffset, curOffset - beginOffset, progNum);
      break;
    }

    case 0xBE: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "Volume State", "", Type::ChangeState);
      state = State::Vol;
      break;
    }

    // pan
    case 0xBF: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "Pan State", "", Type::ChangeState);
      state = State::Pan;
      break;
    }

    // pitch bend
    case 0xC0: {
      addGenericEvent(beginOffset, curOffset - beginOffset, "Pitch Bend State", "",
                      Type::ChangeState);
      state = State::PitchBend;
      break;
    }

    // pitch bend range
    case 0xC1: {
      const uint8_t semitones = readByte(curOffset++);
      addPitchBendRange(beginOffset, curOffset - beginOffset, static_cast<uint16_t>(semitones) * 100);
      break;
    }

    // lfo speed
    case 0xC2: {
      const uint8_t speed = readByte(curOffset++);
      setLfoSpeed(speed);
      const auto desc = fmt::format("Speed: {}", speed);
      addGenericEvent(beginOffset, curOffset - beginOffset, "LFO Speed", desc, Type::Lfo);
      break;
    }

    // lfo delay
    case 0xC3: {
      const uint8_t delay = readByte(curOffset++);
      setLfoDelay(delay);
      const auto desc = fmt::format("Delay: {} clocks", delay);
      addGenericEvent(beginOffset, curOffset - beginOffset, "LFO Delay", desc, Type::Lfo);
      break;
    }

    // modulation depth
    case 0xC4: {
      const auto desc = fmt::format("Type: {}", mp2kModTypeName(modType));
      addGenericEvent(beginOffset, curOffset - beginOffset, "Modulation Depth State", desc,
                      Type::Modulation);
      state = State::Modulation;
      break;
    }

    // modulation type
    case 0xC5: {
      const uint8_t type = readByte(curOffset++);
      setModulationType(type);
      const auto desc = fmt::format("Type: {} ({})", mp2kModTypeName(type), type);
      addGenericEvent(beginOffset, curOffset - beginOffset, "Modulation Type", desc,
                      Type::Modulation);
      break;
    }

    case 0xC8: {
      const int16_t tune = static_cast<int16_t>(readByte(curOffset++)) - 0x40;
      const double cents = static_cast<double>(tune) * (100.0 / 64.0);
      addFineTuning(beginOffset, curOffset - beginOffset, cents, "Microtune");
      break;
    }

    // extend command
    case 0xCD: {
      // This command has a subcommand-byte and its arguments.
      // xIECV = 0x08;		//  imi.echo vol   ***lib
      // xIECL = 0x09;		//  imi.echo len   ***lib
      //
      // Probably, some games extend this command by their own code.

      uint8_t subCommand = readByte(curOffset++);
      uint8_t subParam = readByte(curOffset);
      auto addEchoTextNoItem = [this](const std::string &name, const std::string &desc) {
        if (readMode == READMODE_CONVERT_TO_MIDI && !bWriteGenericEventAsTextEvent) {
          pMidiTrack->addText(fmt::format("{} - {}", name, desc));
        }
      };

      if (subCommand == 0x08 && subParam <= 127) {
        curOffset++;
        constexpr const char* kEventName = "Pseudo Echo Volume";
        const auto desc = fmt::format("Raw value: {} ({:#04x})", static_cast<unsigned>(subParam),
                                      static_cast<unsigned>(subParam));
        addGenericEvent(beginOffset, curOffset - beginOffset, kEventName, desc, Type::Reverb);
        addEchoTextNoItem(kEventName, desc);
        addReverbNoItem(subParam);
      } else if (subCommand == 0x09 && subParam <= 127) {
        curOffset++;
        constexpr const char* kEventName = "Pseudo Echo Length";
        const double seconds = static_cast<double>(subParam) / 60.0;
        const auto desc = fmt::format("Raw value: {} ({:#04x}), length: {:.2f}s",
                                      static_cast<unsigned>(subParam),
                                      static_cast<unsigned>(subParam), seconds);
        addGenericEvent(beginOffset, curOffset - beginOffset, kEventName, desc, Type::Reverb);
        addEchoTextNoItem(kEventName, desc);
      } else {
        // Heuristic method
        while (subParam <= 127) {
          curOffset++;
          subParam = readByte(curOffset);
        }
        addGenericEvent(beginOffset, curOffset - beginOffset, "Extend Command", "", Type::Unknown);
      }
      break;
    }

    case 0xCE: {
      state = State::TieEnd;

      // yes, this seems to be how the actual driver code handles it.  Ex. Aria of Sorrow
      // (U): 0x80D91C0 - handle 0xCE event
      if (readByte(curOffset) > 0x7F) {
        addNoteOffNoItem(prevKey);
        addGenericEvent(beginOffset, curOffset - beginOffset, "End Tie State + End Tie", "",
                        Type::Tie);
      } else
        addGenericEvent(beginOffset, curOffset - beginOffset, "End Tie State", "", Type::Tie);
      break;
    }

    case 0xCF: {
      state = State::Tie;
      if (readByte(curOffset) > 0x7F) {
        beginNoteLfo();
        addNoteOnNoItem(prevKey, prevVel);
        addGenericEvent(beginOffset, curOffset - beginOffset,
                        "Tie State + Tie (with prev key and vel)", "", Type::Tie);
      } else
        addGenericEvent(beginOffset, curOffset - beginOffset, "Tie State", "", Type::Tie);
      break;
    }

    default: {
      addUnknown(beginOffset, curOffset - beginOffset);
      break;
    }
  }
}

bool MP2kTrack::lfoOutputsEnabled() const {
  switch (modType) {
    case kMp2kModTypeVibrato:
      return vibratoLfo.rate() != 0 && vibratoLfo.depth() != 0;
    case kMp2kModTypeTremolo:
      return tremoloLfo.rate() != 0 && tremoloLfo.depth() != 0;
    default:
      return false;
  }
}

void MP2kTrack::clearLfoOutputs() {
  emitVibratoDepth(vibratoLfo, 0, true);
  emitTremoloDepth(tremoloLfo, 0, true);
}

void MP2kTrack::applyLfoDepth(bool force) {
  if (!lfoOutputsEnabled()) {
    clearLfoOutputs();
    return;
  }

  switch (modType) {
    case kMp2kModTypeVibrato:
      emitVibratoDepth(vibratoLfo, vibratoLfo.depth(), force);
      emitTremoloDepth(tremoloLfo, 0, true);
      break;
    case kMp2kModTypeTremolo:
      emitTremoloDepth(tremoloLfo, tremoloLfo.depth(), force);
      emitVibratoDepth(vibratoLfo, 0, true);
      break;
    default:
      clearLfoOutputs();
      break;
  }
}

void MP2kTrack::setLfoSpeed(uint8_t speed) {
  const uint8_t delay = vibratoLfo.delay();
  const uint8_t depth = vibratoLfo.depth();
  vibratoLfo.configure(delay, speed, depth);
  tremoloLfo.configure(delay, speed, depth);
  applyLfoDepth(true);
}

void MP2kTrack::setLfoDelay(uint8_t delay) {
  const uint8_t rate = vibratoLfo.rate();
  const uint8_t depth = vibratoLfo.depth();
  vibratoLfo.configure(delay, rate, depth);
  tremoloLfo.configure(delay, rate, depth);
}

void MP2kTrack::setModulationDepth(uint8_t depth) {
  const uint8_t delay = vibratoLfo.delay();
  const uint8_t rate = vibratoLfo.rate();
  vibratoLfo.configure(delay, rate, depth);
  tremoloLfo.configure(delay, rate, depth);
  applyLfoDepth(true);
}

void MP2kTrack::setModulationType(uint8_t type) {
  if (modType == type) {
    return;
  }
  clearLfoOutputs();
  vibratoLfo.clearReusableFade();
  tremoloLfo.clearReusableFade();
  modType = type;
  applyLfoDepth(true);
}

void MP2kTrack::beginNoteLfo() {
  if (!lfoOutputsEnabled()) {
    return;
  }
  if (modType == kMp2kModTypePan) {
    return;
  }

  const uint8_t delay = vibratoLfo.delay();
  if (delay == 0) {
    applyLfoDepth(false);
    return;
  }

  if (modType == kMp2kModTypeVibrato) {
    emitVibratoDepth(vibratoLfo, 0, false);
    vibratoLfo.setReusableFadeToConfiguredDepth(1);
    vibratoLfo.beginReusableFadeToConfiguredDepth();
  } else if (modType == kMp2kModTypeTremolo) {
    emitTremoloDepth(tremoloLfo, 0, false);
    tremoloLfo.setReusableFadeToConfiguredDepth(1);
    tremoloLfo.beginReusableFadeToConfiguredDepth();
  }
}

void MP2kTrack::updateLfoFade() {
  if (!lfoOutputsEnabled()) {
    return;
  }

  if (modType == kMp2kModTypeVibrato) {
    advanceVibratoDepthFade(vibratoLfo, 0, [](int32_t depth) {
      return depth;
    });
  } else if (modType == kMp2kModTypeTremolo) {
    advanceTremoloDepthFade(tremoloLfo, 0, [](int32_t depth) {
      return depth;
    });
  }
}
