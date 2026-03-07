#include "NDSSeq.h"

#include <array>
#include <algorithm>
#include <cmath>

#include "NDSFormat.h"

DECLARE_FORMAT(NDS);

using namespace std;

namespace {
uint8_t clampMidi7Bit(int value) {
  return static_cast<uint8_t>(std::clamp(value, 0, 127));
}

int8_t sineSampleAt(int index) {
  static constexpr std::array<int8_t, 33> SINE_LOOKUP_TABLE{
      0, 6, 12, 19, 25, 31, 37, 43, 49, 54, 60,
      65, 71, 76, 81, 85, 90, 94, 98, 102, 106, 109,
      112, 115, 117, 120, 122, 123, 125, 126, 126, 127, 127,
  };

  if (index < 0) {
    index = 0;
  } else if (index > 127) {
    index = 127;
  }

  if (index < 32) {
    return SINE_LOOKUP_TABLE[index];
  }
  if (index < 64) {
    return SINE_LOOKUP_TABLE[32 - (index - 32)];
  }
  if (index < 96) {
    return static_cast<int8_t>(-SINE_LOOKUP_TABLE[index - 64]);
  }
  return static_cast<int8_t>(-SINE_LOOKUP_TABLE[32 - (index - 96)]);
}

constexpr double kNdsPeriodicUpdateHz = 192.0;
constexpr double kNdsLfoSpeedToHz = kNdsPeriodicUpdateHz / 512.0;
constexpr double kSf2VibratoRateBaseHz = 0.375;
constexpr double kSf2VibratoRateCcSpanCents = 8400.0;
constexpr uint8_t kNdsDefaultPortamentoKey = 60;

uint8_t speedToVibratoRateCc(uint8_t speed) {
  if (speed == 0) {
    return 0;
  }

  const double targetHz = static_cast<double>(speed) * kNdsLfoSpeedToHz;
  if (targetHz <= kSf2VibratoRateBaseHz) {
    return 0;
  }

  const double cents = 1200.0 * std::log2(targetHz / kSf2VibratoRateBaseHz);
  const int cc = static_cast<int>(std::lround((cents * 127.0) / kSf2VibratoRateCcSpanCents));
  return clampMidi7Bit(cc);
}
} // namespace

NDSSeq::NDSSeq(RawFile *file, uint32_t offset, uint32_t length, string name)
    : VGMSeq(NDSFormat::name, file, offset, length, name) {
  setShouldTrackControlFlowState(true);
}

void NDSSeq::resetVars() {
  VGMSeq::resetVars();

  if (readMode == READMODE_ADD_TO_UI) {
    m_tempoMap.clear();
  }

  if (m_tempoMap.empty()) {
    m_tempoMap.emplace(0, TempoPoint{initialTempoBPM, -1});
  }
}

void NDSSeq::registerTempoChange(uint32_t tick, double bpm, int trackIndex) {
  if (bpm <= 0.0) {
    return;
  }

  const TempoPoint point{bpm, trackIndex};
  auto [it, inserted] = m_tempoMap.emplace(tick, point);
  if (!inserted && trackIndex >= it->second.trackIndex) {
    it->second = point;
  }
}

double NDSSeq::tempoAtTick(uint32_t tick) const {
  if (m_tempoMap.empty()) {
    return (initialTempoBPM > 0.0) ? initialTempoBPM : 120.0;
  }

  auto it = m_tempoMap.upper_bound(tick);
  if (it == m_tempoMap.begin()) {
    return it->second.bpm;
  }
  --it;
  return it->second.bpm;
}

bool NDSSeq::parseHeader(void) {
  VGMHeader *SSEQHdr = addHeader(offset(), 0x10, "SSEQ Chunk Header");
  SSEQHdr->addSig(offset(), 8);
  SSEQHdr->addChild(offset() + 8, 4, "Size");
  SSEQHdr->addChild(offset() + 12, 2, "Header Size");
  SSEQHdr->addUnknownChild(offset() + 14, 2);

  setLength(readWord(offset() + 8));
  setPPQN(0x30);

  return true;
}

bool NDSSeq::parseTrackPointers(void) {
  VGMHeader *DATAHdr = addHeader(offset() + 0x10, 0xC, "DATA Chunk Header");
  DATAHdr->addSig(offset() + 0x10, 4);
  DATAHdr->addChild(offset() + 0x10 + 4, 4, "Size");
  DATAHdr->addChild(offset() + 0x10 + 8, 4, "Data Pointer");
  uint32_t off = offset() + 0x1C;
  uint8_t b = readByte(off);
  aTracks.push_back(new NDSTrack(this));

  //FE XX XX signifies multiple tracks, each true bit in the XX values signifies there is a track for that channel
  if (b == 0xFE)
  {
    VGMHeader *TrkPtrs = addHeader(off, 0, "Track Pointers");
    TrkPtrs->addChild(off, 3, "Valid Tracks");
    off += 3;    //but all we need to do is check for subsequent 0x93 track pointer events
    b = readByte(off);
    uint32_t songDelay = 0;

    while (b == 0x80) {
      uint32_t value;
      uint8_t c;
      uint32_t beginOffset = off;
      off++;
      if ((value = readByte(off++)) & 0x80) {
        value &= 0x7F;
        do {
          value = (value << 7) + ((c = readByte(off++)) & 0x7F);
        } while (c & 0x80);
      }
      songDelay += value;
      TrkPtrs->addChild(beginOffset, off - beginOffset, "Delay");
      //songDelay += SeqTrack::ReadVarLen(++offset);
      b = readByte(off);
      break;
    }

    //Track/Channel assignment and pointer.  Channel # is irrelevant
    while (b == 0x93)
    {
      TrkPtrs->addChild(off, 5, "Track Pointer");
      uint32_t trkOffset = readByte(off + 2) + (readByte(off + 3) << 8) +
          (readByte(off + 4) << 16) + offset() + 0x1C;
      NDSTrack *newTrack = new NDSTrack(this, trkOffset);
      aTracks.push_back(newTrack);
      off += 5;
      b = readByte(off);
    }
    TrkPtrs->setLength(off - TrkPtrs->offset());
  }
  aTracks[0]->setOffset(off);
  aTracks[0]->dwStartOffset = off;
  
  return true;
}

//  ********
//  NDSTrack
//  ********

NDSTrack::NDSTrack(NDSSeq *parentFile, uint32_t offset, uint32_t length)
    : SeqTrack(parentFile, offset, length) {
  resetVars();
  bDetermineTrackLengthEventByEvent = true;
}

void NDSTrack::resetVars() {
  SeqTrack::resetVars();
  noteWithDelta = false;
  resetModState();
}

void NDSTrack::resetModState() {
  modDepth = 0;
  modSpeed = 16;
  modType = static_cast<uint8_t>(ModTarget::Pitch);
  modRange = 1;
  modDelay = 0;

  modLastRenderTick = getTime();
  modPhaseCounter = 0;
  modDelayCounter = 0;
  modPeriodicRemainder = 0.0;
  modRangeSent = false;
  pitchModEnableTick = getTime();
  currentTempoBpm = static_cast<const NDSSeq*>(parentSeq)->tempoAtTick(getTime());

  lastPitchModCc = -1;
  lastPitchVibratoRateCc = -1;
  lastExprCc = -1;
  lastPanCc = -1;
  sweepPitch = 0;
  basePitchBend = 0;
  portamentoEnabled = false;
  portamentoControlKey = clampMidi7Bit(static_cast<int>(kNdsDefaultPortamentoKey) +
                                       static_cast<int>(cKeyCorrection) + static_cast<int>(transpose));
  portamentoTime = 0;
  tieModeEnabled = false;
  tieNoteActive = false;
  tieNoteKey = 0;
  tieNoteEndTick = 0;
}

void NDSTrack::addTime(uint32_t delta) {
  renderModUntil(getTime() + delta);
  SeqTrack::addTime(delta);
}

void NDSTrack::renderModUntil(uint32_t endTick) {
  if (endTick <= modLastRenderTick) {
    return;
  }

  for (uint32_t tick = modLastRenderTick; tick < endTick; tick++) {
    renderModPoint(tick);
    advanceModStateByMidiTicks(1, tick);
  }
  modLastRenderTick = endTick;
}

NDSTrack::ModTarget NDSTrack::toModTarget(uint8_t value) {
  switch (value) {
    case 0:
      return ModTarget::Pitch;
    case 1:
      return ModTarget::Volume;
    case 2:
      return ModTarget::Pan;
    default:
      return ModTarget::Unknown;
  }
}

std::string NDSTrack::to_string(ModTarget target) {
  switch (target) {
    case ModTarget::Pitch:
      return "Pitch";
    case ModTarget::Volume:
      return "Volume";
    case ModTarget::Pan:
      return "Pan";
    case ModTarget::Unknown:
    default:
      return "Unknown";
  }
}

uint32_t NDSTrack::modDelayToMidiTicks(uint16_t delay, uint32_t atTick) const {
  if (delay == 0) {
    return 0;
  }

  const double tempoAtTick = static_cast<const NDSSeq*>(parentSeq)->tempoAtTick(atTick);
  const double effectiveTempo = (tempoAtTick > 0.0) ? tempoAtTick : currentTempoBpm;

  const double ticks = (static_cast<double>(delay) * effectiveTempo *
                        static_cast<double>(parentSeq->ppqn())) /
                       (60.0 * kNdsPeriodicUpdateHz);
  return std::max<uint32_t>(1, static_cast<uint32_t>(std::lround(ticks)));
}

void NDSTrack::onNoteStart(uint32_t noteStartTick, uint8_t noteKey) {
  if (readMode == READMODE_CONVERT_TO_MIDI && portamentoEnabled) {
    addPortamentoControlNoItem(portamentoControlKey);
  }
  portamentoControlKey = clampMidi7Bit(static_cast<int>(noteKey) + static_cast<int>(cKeyCorrection) +
                                       static_cast<int>(transpose));

  modPhaseCounter = 0;
  modDelayCounter = 0;

  if (toModTarget(modType) != ModTarget::Pitch) {
    return;
  }

  pitchModEnableTick = noteStartTick + modDelayToMidiTicks(modDelay, noteStartTick);
  if (lastPitchModCc != 0) {
    lastPitchModCc = -1;
  }
  emitPitchVibratoParamsAt(noteStartTick);
}

void NDSTrack::emitPitchModRangeAt(uint32_t tick) {
  if (readMode != READMODE_CONVERT_TO_MIDI) {
    return;
  }
  const double semitones = static_cast<double>(modRange);
  pMidiTrack->insertModulationDepthRange(channel, semitones, tick);
}

void NDSTrack::emitPitchVibratoParamsAt(uint32_t tick) {
  if (readMode != READMODE_CONVERT_TO_MIDI) {
    return;
  }

  const bool depthEnabled = (tick >= pitchModEnableTick) && (modDepth != 0) && (modSpeed != 0);
  const int depthCc = depthEnabled ? static_cast<int>(modDepth) : 0;
  const int rateCc = static_cast<int>(speedToVibratoRateCc(modSpeed));

  if (depthCc != lastPitchModCc) {
    pMidiTrack->insertModulation(channel, static_cast<uint8_t>(depthCc), tick); // CC1
    lastPitchModCc = depthCc;
  }

  if (rateCc != lastPitchVibratoRateCc) {
    pMidiTrack->insertControllerEvent(channel, 76, static_cast<uint8_t>(rateCc), tick); // Vibrato Rate
    lastPitchVibratoRateCc = rateCc;
  }
}

void NDSTrack::applySweepPitchForNote(uint32_t startTick, uint32_t duration) {
  if (readMode != READMODE_CONVERT_TO_MIDI || duration == 0 || sweepPitch == 0) {
    return;
  }

  uint32_t sweepLength = duration;
  if (portamentoTime != 0) {
    const uint64_t absSweep = static_cast<uint64_t>(std::abs(static_cast<int32_t>(sweepPitch)));
    const uint64_t t = static_cast<uint64_t>(portamentoTime);
    // Not 100% confident about this, but I think it's exact!
    sweepLength = static_cast<uint32_t>((t * t * absSweep) >> 11);
  }

  const int endBend = std::clamp(static_cast<int>(basePitchBend), -8192, 8191);
  bool haveLast = false;
  int lastBend = 0;

  if (sweepLength == 0) {
    pMidiTrack->insertPitchBend(channel, static_cast<int16_t>(endBend), startTick);
    return;
  }

  const uint32_t rampTicks = std::min(duration, sweepLength);
  for (uint32_t i = 0; i <= rampTicks; i++) {
    const int64_t scaled = static_cast<int64_t>(sweepPitch) * static_cast<int64_t>(sweepLength - i);
    const int sweepAtTick = static_cast<int>(scaled / static_cast<int64_t>(sweepLength));
    const int bend = std::clamp(static_cast<int>(basePitchBend) + sweepAtTick, -8192, 8191);
    if (!haveLast || bend != lastBend) {
      pMidiTrack->insertPitchBend(channel, static_cast<int16_t>(bend), startTick + i);
      lastBend = bend;
      haveLast = true;
    }
  }

  if (!haveLast || lastBend != endBend) {
    pMidiTrack->insertPitchBend(channel, static_cast<int16_t>(endBend), startTick + duration);
  }
}

void NDSTrack::renderModPoint(uint32_t tick) {
  const ModTarget target = toModTarget(modType);
  if (target == ModTarget::Unknown) {
    return;
  }

  int32_t raw = 0;
  if (modDepth != 0 && modDelayCounter >= modDelay) {
    const int index = (modPhaseCounter >> 8) & 0x7F;
    raw = static_cast<int32_t>(sineSampleAt(index)) * static_cast<int32_t>(modDepth) * static_cast<int32_t>(modRange);
  }

  if (readMode != READMODE_CONVERT_TO_MIDI) {
    return;
  }

  switch (target) {
    case ModTarget::Pitch: {
      if (!modRangeSent) {
        emitPitchModRangeAt(tick);
        modRangeSent = true;
      }
      emitPitchVibratoParamsAt(tick);
      break;
    }
    case ModTarget::Volume: {
      const int32_t lfo = (raw * 60) >> 14;
      const uint8_t cc = clampMidi7Bit(static_cast<int>(expression) + static_cast<int>(lfo));
      if (cc != lastExprCc) {
        pMidiTrack->insertExpression(channel, cc, tick);
        lastExprCc = cc;
      }
      break;
    }
    case ModTarget::Pan: {
      const int32_t lfo = (raw * 64) >> 14;
      const uint8_t cc = clampMidi7Bit(static_cast<int>(prevPan) + static_cast<int>(lfo));
      if (cc != lastPanCc) {
        pMidiTrack->insertPan(channel, cc, tick);
        lastPanCc = cc;
      }
      break;
    }
    case ModTarget::Unknown:
      break;
  }
}

void NDSTrack::advanceModStateByMidiTicks(uint32_t midiTicks, uint32_t startTick) {
  if (midiTicks == 0) {
    return;
  }

  const auto* ndsSeq = static_cast<const NDSSeq*>(parentSeq);
  for (uint32_t i = 0; i < midiTicks; i++) {
    const double tempoAtTick = ndsSeq->tempoAtTick(startTick + i);
    const double effectiveTempo = (tempoAtTick > 0.0) ? tempoAtTick : currentTempoBpm;
    const double periodicPerMidiTick = (kNdsPeriodicUpdateHz * 60.0) /
                                       (effectiveTempo * static_cast<double>(parentSeq->ppqn()));

    const double stepsWithRemainder = modPeriodicRemainder + periodicPerMidiTick;
    const uint32_t wholeSteps = static_cast<uint32_t>(stepsWithRemainder);
    modPeriodicRemainder = stepsWithRemainder - static_cast<double>(wholeSteps);

    for (uint32_t j = 0; j < wholeSteps; j++) {
      advanceModStateOnePeriodicTick();
    }
  }
}

void NDSTrack::advanceModStateOnePeriodicTick() {
  if (modDelayCounter < modDelay) {
    modDelayCounter++;
    return;
  }

  uint32_t offset = modPhaseCounter;
  offset += static_cast<uint32_t>(modSpeed) << 6;
  offset >>= 8;
  while (offset >= 128) {
    offset -= 128;
  }
  offset <<= 8;

  modPhaseCounter += static_cast<uint16_t>(static_cast<uint32_t>(modSpeed) << 6);
  modPhaseCounter &= 0xFF;
  modPhaseCounter |= static_cast<uint16_t>(offset);
}

bool NDSTrack::readEvent(void) {
  uint32_t beginOffset = curOffset;
  uint8_t status_byte = readByte(curOffset++);

  if (status_byte < 0x80) //then it's a note on event
  {
    const uint32_t noteStartTick = getTime();
    uint8_t vel = readByte(curOffset++);
    dur = readVarLen(curOffset);//GetByte(curOffset++);
    onNoteStart(noteStartTick, status_byte);

    if (tieNoteActive && tieNoteEndTick <= noteStartTick) {
      tieNoteActive = false;
    }

    if (tieModeEnabled && tieNoteActive && tieNoteKey == status_byte) {
      // Tie keeps the voice alive; same-key notes just extend the existing note length.
      makePrevDurNoteEnd(noteStartTick + dur);
      tieNoteEndTick = noteStartTick + dur;
      addTie(beginOffset, curOffset - beginOffset, dur, "Tie");
    }
    else {
      if (tieModeEnabled && tieNoteActive) {
        // Approximation: key changes in tie mode restart at this tick in MIDI.
        makePrevDurNoteEnd(noteStartTick);
        tieNoteActive = false;
      }

      addNoteByDur(beginOffset, curOffset - beginOffset, status_byte, vel, dur);
      if (tieModeEnabled) {
        tieNoteActive = true;
        tieNoteKey = status_byte;
        tieNoteEndTick = noteStartTick + dur;
      }
    }

    applySweepPitchForNote(noteStartTick, dur);
    if (noteWithDelta) {
      addTime(dur);
    }
  }
  else
    switch (status_byte) {
      case 0x80:
        dur = readVarLen(curOffset);
        addRest(beginOffset, curOffset - beginOffset, dur);
        break;

      case 0x81: {
        uint8_t newProg = (uint8_t) readVarLen(curOffset);
        addProgramChange(beginOffset, curOffset - beginOffset, newProg);
        break;
      }

      // [loveemu] open track, however should not handle in this function
      case 0x93:
        curOffset += 4;
        addUnknown(beginOffset, curOffset - beginOffset, "Open Track");
        break;

      case 0x94: {
        u32 jumpAddr = readByte(curOffset) + (readByte(curOffset + 1) << 8)
            + (readByte(curOffset + 2) << 16) + parentSeq->offset() + 0x1C;
        curOffset += 3;

        return addJump(beginOffset, curOffset - beginOffset, jumpAddr);
      }

      case 0x95: {
        u32 destination = readByte(curOffset) + (readByte(curOffset + 1) << 8)
            + (readByte(curOffset + 2) << 16) + parentSeq->offset() + 0x1C;
        curOffset += 3;
        u32 returnOffset = curOffset;
        return addCall(beginOffset, curOffset - beginOffset, destination, returnOffset, "Call");
      }

      // [loveemu] (ex: Hanjuku Hero DS: NSE_45, New Mario Bros: BGM_AMB_CHIKA, Slime Morimori Dragon Quest 2: SE_187, SE_210, Advance Wars)
      case 0xA0: {
        uint8_t subStatusByte;
        int16_t randMin;
        int16_t randMax;

        subStatusByte = readByte(curOffset++);
        randMin = (signed) readShort(curOffset);
        curOffset += 2;
        randMax = (signed) readShort(curOffset);
        curOffset += 2;

        addUnknown(beginOffset, curOffset - beginOffset, "Cmd with Random Value");
        break;
      }

      // [loveemu] (ex: New Mario Bros: BGM_AMB_SABAKU)
      case 0xA1: {
        uint8_t subStatusByte = readByte(curOffset++);
        uint8_t varNumber = readByte(curOffset++);

        addUnknown(beginOffset, curOffset - beginOffset, "Cmd with Variable");
        break;
      }

      case 0xA2: {
        addUnknown(beginOffset, curOffset - beginOffset, "If");
        break;
      }

      case 0xB0: // [loveemu] (ex: Children of Mana: SEQ_BGM001)
      case 0xB1: // [loveemu] (ex: Advance Wars - Dual Strike: SE_TAGPT_COUNT01)
      case 0xB2: // [loveemu]
      case 0xB3: // [loveemu]
      case 0xB4: // [loveemu]
      case 0xB5: // [loveemu]
      case 0xB6: // [loveemu] (ex: Mario Kart DS: 76th sequence)
      case 0xB8: // [loveemu] (ex: Tottoko Hamutaro: MUS_ENDROOL, Nintendogs)
      case 0xB9: // [loveemu]
      case 0xBA: // [loveemu]
      case 0xBB: // [loveemu]
      case 0xBC: // [loveemu]
      case 0xBD: {
        uint8_t varNumber;
        int16_t val;
        const char* eventName[] = {
            "Set Variable", "Add Variable", "Sub Variable", "Mul Variable", "Div Variable",
            "Shift Vabiable", "Rand Variable", "", "If Variable ==", "If Variable >=",
            "If Variable >", "If Variable <=", "If Variable <", "If Variable !="
        };

        varNumber = readByte(curOffset++);
        val = readShort(curOffset);
        curOffset += 2;

        addUnknown(beginOffset, curOffset - beginOffset, eventName[status_byte - 0xB0]);
        break;
      }

      case 0xC0: {
        uint8_t pan = readByte(curOffset++);
        addPan(beginOffset, curOffset - beginOffset, pan);
        break;
      }

      case 0xC1:
        vol = readByte(curOffset++);
        addVol(beginOffset, curOffset - beginOffset, vol);
        break;

      // [loveemu] (ex: Castlevania Dawn of Sorrow: SDL_BGM_BOSS1_)
      case 0xC2: {
        uint8_t mvol = readByte(curOffset++);
        addMasterVol(beginOffset, curOffset - beginOffset, mvol);
        break;
      }

      // [loveemu] (ex: Puyo Pop Fever 2: BGM00)
      case 0xC3: {
        int8_t transpose = static_cast<int8_t>(readByte(curOffset++));
        addTranspose(beginOffset, curOffset - beginOffset, transpose);
        break;
      }

      // [loveemu] pitch bend (ex: Castlevania Dawn of Sorrow: BGM_BOSS2)
      case 0xC4: {
        int16_t bend = (signed) readByte(curOffset++) * 64;
        basePitchBend = bend;
        addPitchBend(beginOffset, curOffset - beginOffset, bend);
        break;
      }

      // [loveemu] pitch bend range (ex: Castlevania Dawn of Sorrow: BGM_BOSS2)
      case 0xC5: {
        uint8_t semitones = readByte(curOffset++);
        addPitchBendRange(beginOffset, curOffset - beginOffset, semitones * 100);
        break;
      }

      // [loveemu] (ex: Children of Mana: SEQ_BGM000)
      case 0xC6:
        curOffset++;
        addGenericEvent(beginOffset, curOffset - beginOffset, "Priority", "", Type::ChangeState);
        break;

      // [loveemu] (ex: Castlevania Dawn of Sorrow: SDL_BGM_ARR1_)
      case 0xC7: {
        uint8_t notewait = readByte(curOffset++);
        noteWithDelta = (notewait != 0);
        addUnknown(beginOffset, curOffset - beginOffset, "Notewait Mode");
        break;
      }

      // [loveemu] (ex: Hanjuku Hero DS: NSE_42)
      case 0xC8: {
        tieModeEnabled = (readByte(curOffset++) != 0);
        if (readMode == READMODE_CONVERT_TO_MIDI) {
          makePrevDurNoteEnd(getTime());
        }
        tieNoteActive = false;
        tieNoteEndTick = getTime();
        addGenericEvent(beginOffset, curOffset - beginOffset, "Tie",
                        tieModeEnabled ? "On" : "Off", Type::Tie);
        break;
      }

      // [loveemu] (ex: Hanjuku Hero DS: NSE_50)
      case 0xC9: {
        uint8_t portKey = readByte(curOffset++);
        portamentoEnabled = true;
        portamentoControlKey = clampMidi7Bit(static_cast<int>(portKey) + static_cast<int>(cKeyCorrection) +
                                             static_cast<int>(transpose));
        if (readMode == READMODE_CONVERT_TO_MIDI) {
          addPortamentoNoItem(true);
          addPortamentoControlNoItem(portamentoControlKey);
        }
        addGenericEvent(beginOffset, curOffset - beginOffset, "Portamento Control",
                        "Key=" + std::to_string(portKey), Type::Portamento);
        break;
      }

      // [loveemu] (ex: Castlevania Dawn of Sorrow: SDL_BGM_ARR1_)
      case 0xCA: {
        renderModUntil(getTime());
        modDepth = readByte(curOffset++);
        addGenericEvent(beginOffset, curOffset - beginOffset, "Modulation Depth",
                        "Depth=" + std::to_string(modDepth), Type::Lfo);
        if (modDepth == 0) {
          lastPitchModCc = -1;
          lastExprCc = -1;
          lastPanCc = -1;
        }
        break;
      }

      // [loveemu] (ex: Castlevania Dawn of Sorrow: SDL_BGM_ARR1_)
      case 0xCB: {
        renderModUntil(getTime());
        modSpeed = readByte(curOffset++);
        addGenericEvent(beginOffset, curOffset - beginOffset, "Modulation Speed",
                        "Speed=" + std::to_string(modSpeed), Type::Lfo);
        break;
      }

      // [loveemu] (ex: Children of Mana: SEQ_BGM001)
      case 0xCC: {
        renderModUntil(getTime());
        modType = readByte(curOffset++);
        modRangeSent = false;
        lastPitchModCc = -1;
        lastPitchVibratoRateCc = -1;
        lastExprCc = -1;
        lastPanCc = -1;
        const ModTarget target = toModTarget(modType);
        addGenericEvent(beginOffset, curOffset - beginOffset, "Modulation Type",
                        "Target=" + to_string(target), Type::Lfo);
        break;
      }

      // [loveemu] (ex: Phoenix Wright - Ace Attorney: BGM021)
      case 0xCD: {
        renderModUntil(getTime());
        modRange = readByte(curOffset++);
        modRangeSent = false;
        addGenericEvent(beginOffset, curOffset - beginOffset, "Modulation Range",
                        "Range=" + std::to_string(modRange), Type::Lfo);
        break;
      }

      // [loveemu] (ex: Castlevania Dawn of Sorrow: SDL_BGM_ARR1_)
      case 0xCE: {
        bool bPortOn = (readByte(curOffset++) != 0);
        portamentoEnabled = bPortOn;
        addPortamento(beginOffset, curOffset - beginOffset, bPortOn);
        break;
      }

      // [loveemu] (ex: Bomberman: SEQ_AREA04)
      case 0xCF: {
        portamentoTime = readByte(curOffset++);
        addPortamentoTime(beginOffset, curOffset - beginOffset, portamentoTime);
        break;
      }

      // [loveemu] (ex: Castlevania Dawn of Sorrow: SDL_BGM_WIND_)
      case 0xD0:
        curOffset++;
        addUnknown(beginOffset, curOffset - beginOffset, "Attack Rate");
        break;

      // [loveemu] (ex: Castlevania Dawn of Sorrow: SDL_BGM_WIND_)
      case 0xD1:
        curOffset++;
        addUnknown(beginOffset, curOffset - beginOffset, "Decay Rate");
        break;

      // [loveemu] (ex: Castlevania Dawn of Sorrow: SDL_BGM_WIND_)
      case 0xD2:
        curOffset++;
        addUnknown(beginOffset, curOffset - beginOffset, "Sustain Level");
        break;

      // [loveemu] (ex: Castlevania Dawn of Sorrow: SDL_BGM_WIND_)
      case 0xD3:
        curOffset++;
        addUnknown(beginOffset, curOffset - beginOffset, "Release Rate");
        break;

      // [loveemu] (ex: Castlevania Dawn of Sorrow: SDL_BGM_WIND_)
      case 0xD4:
        curOffset++;
        addUnknown(beginOffset, curOffset - beginOffset, "Loop Start");
        break;

      case 0xD5: {
        uint8_t expression = readByte(curOffset++);
        addExpression(beginOffset, curOffset - beginOffset, expression);
        break;
      }

      // [loveemu]
      case 0xD6:
        curOffset++;
        addUnknown(beginOffset, curOffset - beginOffset, "Print Variable");
        break;

      // [loveemu] (ex: Children of Mana: SEQ_BGM001)
      case 0xE0: {
        renderModUntil(getTime());
        modDelay = readShort(curOffset);
        curOffset += 2;
        modDelayCounter = 0;
        if (toModTarget(modType) == ModTarget::Pitch) {
          pitchModEnableTick = getTime() + modDelayToMidiTicks(modDelay, getTime());
          lastPitchModCc = -1;
          emitPitchVibratoParamsAt(getTime());
        }
        addGenericEvent(beginOffset, curOffset - beginOffset, "Modulation Delay",
                        "Delay=" + std::to_string(modDelay), Type::Lfo);
        break;
      }

      case 0xE1: {
        uint16_t bpm = readShort(curOffset);
        curOffset += 2;
        currentTempoBpm = static_cast<double>(bpm);
        addTempoBPM(beginOffset, curOffset - beginOffset, bpm);
        static_cast<NDSSeq*>(parentSeq)->registerTempoChange(
            getTime(), currentTempoBpm, channelGroup * 16 + channel);
        break;
      }

      // [loveemu] (ex: Hippatte! Puzzle Bobble: SEQ_1pbgm03)
      case 0xE3: {
        sweepPitch = readShort(curOffset);
        curOffset += 2;
        addGenericEvent(beginOffset, curOffset - beginOffset, "Sweep Pitch",
                        "Sweep=" + std::to_string(sweepPitch), Type::Lfo);
        break;
      }

      // [loveemu] (ex: Castlevania Dawn of Sorrow: SDL_BGM_WIND_)
      case 0xFC:
        addUnknown(beginOffset, curOffset - beginOffset, "Loop End");
        break;

      case 0xFD: {
        return addReturn(beginOffset, curOffset - beginOffset, "Return");
      }

      // [loveemu] allocate track, however should not handle in this function
      case 0xFE:
        curOffset += 2;
        addUnknown(beginOffset, curOffset - beginOffset, "Allocate Track");
        break;

      case 0xFF:
        renderModUntil(getTime());
        addEndOfTrack(beginOffset, curOffset - beginOffset);
        return false;

      default:
        addUnknown(beginOffset, curOffset - beginOffset);
        return false;
    }
  return true;
}
