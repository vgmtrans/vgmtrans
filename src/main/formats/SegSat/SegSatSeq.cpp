/*
 * VGMTrans (c) 2002-2025
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "SegSatSeq.h"
#include "ScaleConversion.h"
#include "SegSatFormat.h"
#include "SegSatInstrSet.h"
#include "VGMColl.h"
#include "util/MidiConstants.h"

DECLARE_FORMAT(SegSat);

SegSatSeq::SegSatSeq(RawFile *file, uint32_t offset, std::string name)
    : VGMSeqNoTrks(SegSatFormat::name, file, offset, std::move(name)) {
  VGMSeq::setUseLinearAmplitudeScale(false);
  setInitialVolume(0x7F);
  setInitialExpression(0x7F);
}

void SegSatSeq::resetVars() {
  VGMSeqNoTrks::resetVars();

  m_remainingNotesInLoop = 0;
  m_loopEndPos = -1;
  m_foreverLoopStart = -1;
  m_durationAccumulator = 0;
  memset(m_vol, 0x7F, 16);
  memset(m_progNum, 0, 16);
}

void SegSatSeq::useColl(const VGMColl* coll) {
  m_collContext.m_vlTables.clear();
  m_collContext.instrs.clear();

  if (!coll) return;

  const auto& instrSets = coll->instrSets();
  if (instrSets.empty())
    return;

  auto* instrSet = dynamic_cast<SegSatInstrSet*>(instrSets.front());
  if (!instrSet || instrSet->aInstrs.empty())
    return;

  m_collContext.m_vlTables = instrSet->vlTables();
  if (m_collContext.m_vlTables.empty())
    return;

  m_collContext.instrs.reserve(instrSet->aInstrs.size());

  for (VGMInstr* instr : instrSet->aInstrs) {
    const SegSatInstr* pInstr = dynamic_cast<const SegSatInstr*>(instr);
    SegSatInstr instrCopy = *pInstr;
    instrCopy.removeChildren();
    m_collContext.instrs.push_back(instrCopy);
  }
}

bool SegSatSeq::parseHeader() {
  setPPQN(readShortBE(offset()));
  m_normalTrackOffset = offset() + readShortBE(offset() + 4);
  setEventsOffset(offset() + 8);
  nNumTracks = 16;

  VGMHeader *seqHdr = SeqTrack::addHeader(offset(), 8, "Sequence Header");
  seqHdr->addChild(offset(), 2, "Pulses Per Quarter Note");
  seqHdr->addChild(offset() + 2, 2, "Number of Tempo Events");
  seqHdr->addPointer(offset() + 4, 2, offset() + readShortBE(offset()+4),
  true, "Pointer to Normal Track");
  seqHdr->addPointer(offset() + 6, 2, offset() + readShortBE(offset()+6),
    true, "Pointer to First Tempo Event in Loop");
  return true;
}

/// For a given bank, program number, and note, determine the applied instrument region.
const SegSatRgn* SegSatSeq::resolveRegion(uint8_t bank, uint8_t progNum, uint8_t noteNum) {
  auto& instrs = m_collContext.instrs;
  if (progNum >= instrs.size())
    return nullptr;

  const auto& rgns = instrs[progNum].regions();
  for (const auto* rgn : rgns) {
    auto segSatRgn = dynamic_cast<const SegSatRgn*>(rgn);
    if (noteNum >= segSatRgn->keyLow && noteNum <= segSatRgn->keyHigh) {
      return segSatRgn;
    }
  }
  return nullptr;
}

/// For a velocity and instrument region, determine the final velocity value by applying the
/// Velocity Level table transformation and accounting for the TL register attenuation behavior.
/// The logic is based on the 1.33 version of the driver (circa "95/08/07")
uint8_t SegSatSeq::resolveVelocity(uint8_t vel, const SegSatRgn& rgn, int8_t volBias, uint8_t ch) {
  uint8_t vlTableIndex = rgn.vlTableIndex();
  const auto& vlTables = m_collContext.m_vlTables;
  if (vlTableIndex >= vlTables.size())
    return vel;

  const auto& vlTable = vlTables[vlTableIndex];
  uint8_t base = 0;
  uint8_t rate = vlTable.rate0;
  uint8_t point = 0;
  if (vel > vlTable.point0) {
    point = vlTable.point0;
    base = vlTable.level0;
    rate = vlTable.rate1;
    if (vel > vlTable.point1) {
      point = vlTable.point1;
      base = vlTable.level1;
      rate = vlTable.rate2;
      if (vel > vlTable.point2) {
        point = vlTable.point2;
        base = vlTable.level2;
        rate = vlTable.rate3;
      }
    }
  }

  // Now we handle the rate. This logic is specific to the 1.33 driver. There are logical
  // differences in future versions (and perhaps older versions too?)
  uint8_t shift = rate >> 4;
  bool boost = (rate >> 3) & 1;
  uint8_t mode = rate & 7;
  uint8_t newVel = base;
  uint16_t velMargin = vel - point;
  switch (mode) {
    case 0: break;
    case 1:
      newVel += boost ? (((velMargin & 0x7F) * 12) << shift) >> 3 : (velMargin << shift);
      break;
    case 2:
      newVel += velMargin;
      break;
    case 3:
      newVel += velMargin >> shift;
      break;
    case 4: break;
    case 5:
      newVel -= velMargin >> shift;
      break;
    case 6:
      newVel -= velMargin;
      break;
    case 7:
      newVel -= boost ? (((velMargin & 0x7F) * 12) << shift) >> 3 : (velMargin << shift);
      break;
  }
  newVel = std::clamp<uint8_t>(newVel, 0, 0x7F);
  uint32_t volScale = (newVel + 1) * (256 - rgn.totalLevel());
  uint8_t amp = (volScale * ( (0x7F & 0x7F)+1 )*4 - 1) >> 16;
  // The driver actually performs the following calculation, multiplying channel volume with
  // the velocity and region total level values. This is unfortunate. It means we can't calculate
  // volume attenuation independent of these values like in MIDI, and we can't easily adhere to the
  // driver behavior as volume changes on live notes will break (and each channel is polyphonic,
  // so any workaround would have to change volume per note). We choose to keep vol/expression
  // events and lose some volume accuracy.
  // Note that this version of the driver treats volume and expression events as one and the same.
  // Later versions treat them as distinct, though details remain unexamined.
  // uint8_t amp = (volScale * ( (m_vol[ch] & 0x7F)+1 )*4 - 1) >> 16;

  uint8_t tl = ~static_cast<uint8_t>(std::clamp<int>(amp + volBias, 0, 255));
  uint8_t result = convertDBAttenuationToStdMidiVal(tlToDB(tl));
  return result;
}

bool SegSatSeq::readEvent() {
  // If we're in the tempo track, expect a tempo event
  if (curOffset < m_normalTrackOffset) {
    uint32_t time = 0;
    while (curOffset < m_normalTrackOffset) {
      uint32_t mpqn = readWordBE(curOffset + 4);
      insertTempo(curOffset, 8, mpqn, time);
      time += readWordBE(curOffset);
      curOffset += 8;
    }
    return true;
  }

  uint32_t beginOffset = curOffset;
  uint8_t status_byte = readByte(curOffset++);

  if (status_byte <= 0x7F) {            // note on
    uint8_t ch = status_byte & 0x0F;
    setChannel(ch);
    uint16_t durBit8 = (status_byte & 0x40) << 2;
    uint16_t deltaBit8 = (status_byte & 0x20) << 3;
    if ((status_byte & 0x10) > 0) {
      L_DEBUG("found 0x10 bit on for note on status byte {:x}", beginOffset);
    }

    auto key = readByte(curOffset++);
    auto vel = readByte(curOffset++);
    uint16_t noteDuration = readByte(curOffset++) | durBit8;
    noteDuration += m_durationAccumulator;
    m_durationAccumulator = 0;
    uint16_t deltaTime = readByte(curOffset++) | deltaBit8;
    addTime(deltaTime);

    uint8_t progNum = m_progNum[ch];
    auto* rgn = resolveRegion(0, m_progNum[ch], key);
    if (rgn) {
      int8_t volBias = m_collContext.instrs[progNum].volBias();
      vel = resolveVelocity(vel, *rgn, volBias, ch);
    } else {
      L_WARN("Didn't find an instrument region with key range covering note event at offset: {:X}", beginOffset);
    }

    addNoteByDur(beginOffset, curOffset - beginOffset, key, vel, noteDuration);
  }
  else {
    if ((status_byte & 0xF0) == Midi::CONTROL_CHANGE) {
      // BX are midi controller events
      uint8_t ch = status_byte & 0x0F;
      setChannel(ch);
      uint8_t controllerType = readByte(curOffset++);
      uint8_t value = readByte(curOffset++);
      uint8_t controllerValue = value & 0x7F;
      uint16_t deltaBit8 = (value & 0x80) << 1;
      addTime(readByte(curOffset++) | deltaBit8);
      switch (controllerType) {
        case Midi::MODULATION_MSB:
          addModulation(beginOffset, curOffset - beginOffset, controllerValue);
          break;
        case Midi::BREATH_MSB:
          addBreath(beginOffset, curOffset - beginOffset, controllerValue);
          break;
        case Midi::EXPRESSION_MSB:
        case Midi::VOLUME_MSB: {
          m_vol[ch] = controllerValue;
          uint8_t scsp_tl = (uint8_t)std::max(0, (254 - 2*controllerValue));
          if (scsp_tl > 0)
            --scsp_tl;
          double attenDb = tlToDB(scsp_tl);

          uint8_t newVol = convertDBAttenuationToStdMidiVal(attenDb);
          if (controllerType == Midi::VOLUME_MSB) {
            addVol(beginOffset, curOffset - beginOffset, newVol);
          } else {
            addExpression(beginOffset, curOffset - beginOffset, newVol);
          }
          break;
        }
        case Midi::PAN_MSB:
          addPan(beginOffset, curOffset - beginOffset, controllerValue);
          break;
        case Midi::BANK_SELECT_LSB:
          m_bank[ch] = controllerValue;
          // Skip bank select during MIDI conversion if the seq uses only a single bank
          if (VGMSeq::readMode == READMODE_ADD_TO_UI || parentSeq->referencedBanks().size() > 1) {
            addBankSelect(beginOffset, curOffset - beginOffset, m_bank[ch]);
          }
          break;
        case Midi::SUSTAIN_SWITCH:
          addSustainEvent(beginOffset, curOffset - beginOffset, controllerValue);
          break;
        default:
          addUnknown(beginOffset, curOffset - beginOffset);
          if (controllerValue <= 0x7F)
            addControllerEventNoItem(controllerType, controllerValue);
          break;
      }
    }
    else if ((status_byte & 0xF0) == 0xC0) {
      uint8_t ch = status_byte & 0x0F;
      setChannel(ch);
      uint8_t dataByte = readByte(curOffset++);
      uint8_t progNum = dataByte & 0x7F;
      uint16_t deltaBit8 = 0; //(dataByte & 0x80) << 1;
      m_progNum[ch] = progNum;
      addTime(readByte(curOffset++) | deltaBit8);
      if (dataByte < 0x80)
        addProgramChange(beginOffset, curOffset - beginOffset, progNum);
    }
    else if ((status_byte & 0xF0) == 0xD0) {
      setChannel(status_byte & 0x0F);
      uint8_t dataByte = readByte(curOffset++);
      uint16_t deltaBit8 = 0; //(dataByte & 0x80) << 1;
      addTime(readByte(curOffset++) | deltaBit8);
      // TODO: add channel pressure events to SeqTrack and MidiFile
      addUnknown(beginOffset, curOffset - beginOffset, "Channel Pressure");
    }
    else if ((status_byte & 0xF0) == 0xE0) {
      setChannel(status_byte & 0x0F);
      uint8_t dataByte = readByte(curOffset++);
      int16_t bend = (static_cast<int32_t>(dataByte) << 7) - 8192;
      uint16_t deltaBit8 = 0; //(dataByte & 0x80) << 1;
      addTime(readByte(curOffset++) | deltaBit8);
      addPitchBend(beginOffset, curOffset - beginOffset, bend);
    }
    else {
      switch (status_byte) {
        case 0x81: {
          if (m_remainingNotesInLoop != 0) {
            L_WARN("Nested SegSat loop at {:x}. Ending sequence parsing", beginOffset);
            curOffset += 3;
            return false;
          }
          uint32_t loopOffset = m_normalTrackOffset + readShortBE(curOffset);
          curOffset += 2;
          m_remainingNotesInLoop = readByte(curOffset++);
          m_loopEndPos = curOffset;
          addGenericEvent(beginOffset, curOffset - beginOffset, "Loop", "", Type::Loop);
          curOffset = loopOffset;
          bInLoop = true;
          break;
        }
        case 0x82:
          addTime(readByte(curOffset++));
          if (m_foreverLoopStart == -1) {
            m_foreverLoopStart = curOffset;
            addGenericEvent(beginOffset, curOffset - beginOffset,
              "Forever Loop Start Point", "", Type::RepeatStart);
          } else {
            if (!addLoopForever(beginOffset, curOffset - beginOffset))
              return false;
            if (SeqTrack::isValidOffset(m_foreverLoopStart))
              curOffset = m_foreverLoopStart;
          }
          break;
        case 0x83:
          addEndOfTrack(beginOffset, curOffset - beginOffset);
          return false;
        case 0x87:
          addGenericEvent(beginOffset, curOffset - beginOffset, "Add 256 Duration Ticks", "", Type::Tie);
          m_durationAccumulator += 256;
          break;
        case 0x88:
          addGenericEvent(beginOffset, curOffset - beginOffset, "Add 512 Duration Ticks", "", Type::Tie);
          m_durationAccumulator += 512;
          break;
        case 0x89:
          addGenericEvent(beginOffset, curOffset - beginOffset, "Add 2048 Duration Ticks", "", Type::Tie);
          m_durationAccumulator += 2048;
          break;
        case 0x8A:
          addGenericEvent(beginOffset, curOffset - beginOffset, "Add 4096 Duration Ticks", "", Type::Tie);
          m_durationAccumulator += 4096;
          break;
        case 0x8B:
          addGenericEvent(beginOffset, curOffset - beginOffset, "Add 8192 Duration Ticks", "", Type::Tie);
          m_durationAccumulator += 8192;
          break;
        case 0x8C:
          addRest(beginOffset, curOffset - beginOffset, 256, "Rest 256 Ticks");
          break;
        case 0x8D:
          addRest(beginOffset, curOffset - beginOffset, 512, "Rest 512 Ticks");
          break;
        case 0x8E:
          addRest(beginOffset, curOffset - beginOffset, 2048, "Rest 2048 Ticks");
          break;
        case 0x8F:
          addRest(beginOffset, curOffset - beginOffset, 4096, "Rest 4096 Ticks");
          break;
      }
    }
  }
  // All events except 0x8X events decrement the loop counter
  if ((status_byte & 0xF0) != 0x80) {
    if (m_remainingNotesInLoop == 0)
      return true;

    m_remainingNotesInLoop--;
    if (m_remainingNotesInLoop == 0) {
      bInLoop = false;
      curOffset = m_loopEndPos;
      return true;
    }
  }

  return true;
}
