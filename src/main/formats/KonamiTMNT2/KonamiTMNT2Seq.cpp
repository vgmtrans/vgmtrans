/*
 * VGMTrans (c) 2002-2025
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#include "KonamiTMNT2Seq.h"

#include "KonamiTMNT2Definitions.h"
#include "KonamiTMNT2Instr.h"
#include "ScaleConversion.h"
#include "VGMColl.h"

#include <utility>
#include <array>
#include <spdlog/fmt/fmt.h>

DECLARE_FORMAT(KonamiTMNT2);

namespace {

constexpr int PPQN = 96;
constexpr u8 K053260_BASE_VEL = 0x7F;
constexpr double K053260_VOL_MUL = 1.3;

// Pan multipliers from the K053260 MAME implementation. 0 represents silence and 65536 represents
// full scale. Values use a roughly equal-power panning law. ie. the sum of the squares of each side
// add up to roughly the same value (or, treating each value as a percent, the squares add up to 1).
// This means we don't need to adjust volume when converting pan events.
constexpr std::array<std::array<int, 2>, 8> K053260_PAN_MUL = {{
  {     0,     0 },
  { 65536,     0 },
  { 59870, 26656 },
  { 53684, 37950 },
  { 46341, 46341 },
  { 37950, 53684 },
  { 26656, 59870 },
  {     0, 65536 },
}};

constexpr double K053260_PAN_SCALE = 65536.0;

constexpr double calculateTempo(std::uint8_t clkb, double clockHz, int ppqn, int tickSkipInterval) {
  // The driver uses YM2151's Timer B to send IRQs to the Z80. It sets CLKB at startup.
  const double ticks = 1024.0 * (256.0 - static_cast<double>(clkb));
  double tempo = (60.0 * clockHz) / (static_cast<double>(ppqn) * ticks);
  if (tickSkipInterval > 0)
    tempo *= (tickSkipInterval - 1) / static_cast<double>(tickSkipInterval);
  return tempo;
}

}  // namespace

KonamiTMNT2Seq::KonamiTMNT2Seq(RawFile *file,
                               KonamiTMNT2FormatVer fmtVer,
                               u32 offset,
                               std::vector<u32> ym2151TrackOffsets,
                               std::vector<u32> k053260TrackOffsets,
                               u8 defaultTickSkipInterval,
                               u8 clkb,
                               const std::string &name)
    : VGMSeq(KonamiTMNT2Format::name, file, offset, 0, name),
      m_fmtVer(fmtVer),
      m_ym2151TrackOffsets(std::move(ym2151TrackOffsets)),
      m_k053260TrackOffsets(std::move(k053260TrackOffsets)),
      m_defaultTickSkipInterval(defaultTickSkipInterval),
      m_clkb(clkb)
{
  bLoadTickByTick = true;
  setPPQN(PPQN);
  setAlwaysWriteInitialVol(127);
  setAlwaysWriteInitialExpression(127);
  double tempo = calculateTempo(YM2151_CLKB, YM2151_CLOCK_RATE, PPQN, m_defaultTickSkipInterval);
  setAlwaysWriteInitialTempo(tempo);
  setAllowDiscontinuousTrackData(true);
}

void KonamiTMNT2Seq::resetVars() {
  VGMSeq::resetVars();
  m_globalTranspose = 0;
  m_masterAttenYM2151 = 0;
  m_masterAttenK053260 = 0;
}

bool KonamiTMNT2Seq::parseTrackPointers() {
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

void KonamiTMNT2Seq::useColl(const VGMColl* coll) {
  m_collContext.instrInfos.clear();
  m_collContext.drumKeyMap.clear();
  if (!coll)
    return;

  for (auto instrSet : coll->instrSets()) {
    if (auto* tmnt2InstrSet = dynamic_cast<KonamiTMNT2SampleInstrSet*>(instrSet)) {
      m_collContext.instrInfos = tmnt2InstrSet->instrInfos();
      for (int i = 0; i < tmnt2InstrSet->drumTables().size(); ++i) {
        auto drumBank = tmnt2InstrSet->drumTables()[i];
        for (int j = 0; j < drumBank.size(); ++j) {
          m_collContext.drumKeyMap[(i * 16) + j] = drumBank[j];
        }
      }
    }
    else if (auto vendettaInstrSet = dynamic_cast<KonamiVendettaSampleInstrSet*>(instrSet)) {
      for (auto instr : vendettaInstrSet->instrsK053260()) {
        // Adapt the combination konami_vendetta_instr_k053260 and konami_vendetta_sample_info into
        // a konami_tmnt2_instr_info, which is a superset of the data
        auto sampInfos = vendettaInstrSet->sampleInfos();
        if (instr.samp_info_idx >= sampInfos.size())
          continue;
        auto sampInfo = vendettaInstrSet->sampleInfos()[instr.samp_info_idx];
        konami_tmnt2_instr_info tmnt2Instr = {
          0,
          sampInfo.length_lo, sampInfo.length_hi,
          sampInfo.start_lo, sampInfo.start_mid, sampInfo.start_hi,
          static_cast<u8>(0x7F - instr.attenuation),
          0, 0, 0
        };
        m_collContext.instrInfos.emplace_back(tmnt2Instr);
      }

      // Adapt the konami_vendetta_drum_info and konami_vendetta_sample_info into a
      // a konami_tmnt2_drum_info, which is a superset of the data
      for (auto drumKeyPair : vendettaInstrSet->drumKeyMap()) {
        const konami_vendetta_drum_info& venDrum = drumKeyPair.second;
        auto sampInfos = vendettaInstrSet->sampleInfos();
        auto sampInfoIdx = venDrum.instr.samp_info_idx;
        if (sampInfoIdx >= sampInfos.size())
          continue;
        konami_vendetta_sample_info sampInfo = sampInfos[sampInfoIdx];
        u8 pitchLo = venDrum.pitch & 0xFF;
        u8 pitchHi = (venDrum.pitch >> 8) & 0xFF;
        u8 pan = venDrum.pan == -1 ? 0 : venDrum.pan;
        konami_tmnt2_drum_info tmnt2Drum = {
          pitchLo, pitchHi,
          0, 0,
          sampInfo.length_lo, sampInfo.length_hi,
          sampInfo.start_lo, sampInfo.start_mid, sampInfo.start_hi,
          static_cast<u8>(0x7F - venDrum.instr.attenuation),
          0, 0, 0, pan
        };
        m_collContext.drumKeyMap[drumKeyPair.first] = tmnt2Drum;
      }
    }
  }
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
  setUseLinearAmplitudeScale(!isFmTrack);
}

void KonamiTMNT2Track::resetVars() {
  SeqTrack::resetVars();

  m_program = 0;
  m_state = 0;
  m_rawBaseDur = 0;
  m_baseDur = 0;
  m_baseDurHalveBackup = 0;
  m_extendDur = 0;
  m_durSubtract = 0;
  m_noteDurPercent = 0;
  m_baseVol = 0;
  m_pan = 0;
  m_instrPan = 0;
  m_attenuation = 0;
  m_noteOffset = 0;
  m_transpose = 0;
  m_addedToNote = 0;
  m_drumBank = 0;
  m_dxAtten = 0;
  m_dxAttenMultiplier = 1;
  memset(m_loopCounter, 0, sizeof(m_loopCounter));
  memset(m_loopStartOffset, 0, sizeof(m_loopStartOffset));
  memset(m_callOrigin, 0, sizeof(m_callOrigin));
  m_warpCounter = 0;
  m_warpOrigin = 0;
  m_warpDest = 0;

  m_noteCountdown = 0;
  m_lfoRampValue = 0;
  m_lfoDelay = 0;
  m_lfoDelayCountdown = 0;
  m_lfoRampStepTicks = 0;
}

double KonamiTMNT2Track::calculateVol(u8 baseVol) {
  s16 volume = baseVol;
  volume -= m_dxAtten;
  volume -= masterAttenuation();
  if (!m_isFmTrack) {
    volume -= m_attenuation;
    volume *= K053260_VOL_MUL;
  }
  volume = std::clamp<s16>(volume, 0, 127);
  return volume / 127.0;
}

u8 KonamiTMNT2Track::calculatePan() {
  if (m_isFmTrack) {
    // Overrides RL registers (right/left channel enable) if a global state var is set to 0
    // value 0 -> cancel override and restore instruments original RL setting
    // 1 -> L only (0x40)
    // 2 -> R only (0x80)
    // 3 -> LR enabled
    // The pan L/R table values for K053260 tracks are weighted to maintain a uniform amplitude.
    // Adjusting volume is therefore unnecessary.
    u8 pan = 64;
    if (m_pan == 1)
      pan = 0;
    else if (m_pan == 2)
      pan = 127;
    return pan;
  }

  // If pan is 0, use instrument pan.
  u8 k053260Pan = m_pan & 0x7;
  if (k053260Pan == 0) {
    if (m_instrPan != 0)
      k053260Pan = m_instrPan & 0x7;
    else
      k053260Pan = 4;
  }
  double leftPan = static_cast<double>(K053260_PAN_MUL[k053260Pan][0]) / K053260_PAN_SCALE;
  double rightPan = static_cast<double>(K053260_PAN_MUL[k053260Pan][1]) / K053260_PAN_SCALE;
  if (fmtVersion() == VENDETTA)
    return convertVolumeBalanceToStdMidiPan(rightPan, leftPan);
  else
    return convertVolumeBalanceToStdMidiPan(leftPan, rightPan);
}

void KonamiTMNT2Track::updatePan() {
  addPanNoItem(calculatePan());
}

void KonamiTMNT2Track::handleProgramChangeK053260() {
  if (!percussionMode()) {
    std::optional<konami_tmnt2_instr_info> info = instrInfo(m_program);
    if (info) {
      m_noteDurPercent = info->note_dur;
      m_instrPan = info->default_pan;
      m_baseVol = info->volume & 0x7F;
      // There is special behavior when volume > 0x7F, but we'll ignore it for now
    }
    updatePan();
  }
}

void KonamiTMNT2Track::onTickBegin() {
  if (m_noteCountdown > 0)
    m_noteCountdown -= 1;

  if (m_noteCountdown <= 0) {
    // note not active
    return;
  }

  if (m_lfoRampStepTicks == 0)
    return;

  if (m_lfoDelayCountdown > 0) {
    m_lfoDelayCountdown -= 1;
    return;
  }

  if (m_lfoRampValue >= 7)
    return;

  if (--m_lfoRampStepCountdown == 0) {
    m_lfoRampStepCountdown = m_lfoRampStepTicks;
    m_lfoRampValue += 1;

    addControllerEventNoItem(77, m_lfoRampValue * 16);
    addControllerEventNoItem(92, (m_lfoRampValue >> 1) * 32);
  }
}

void KonamiTMNT2Track::onNoteBegin(int noteDur) {
  m_noteCountdown = noteDur;
  m_lfoDelayCountdown = m_lfoDelay;
  m_lfoRampStepCountdown = m_lfoRampStepTicks;
  if (m_lfoRampValue > 0) {
    addControllerEventNoItem(77, 0);
    addControllerEventNoItem(92, 0);
  }
  m_lfoRampValue = 0;
}

bool KonamiTMNT2Track::readEvent() {
  if (!isValidOffset(curOffset)) {
    return false;
  }

  uint32_t beginOffset = curOffset;
  uint8_t opcode = readByte(curOffset++);

  if (opcode < 0xD0) {
    // determine duration
    u16 dur = opcode & 0x0F;
    if (dur == 0)
      dur = 0x10;
    dur += m_extendDur;
    m_extendDur = 0;
    if ((m_state & 0x20) != 0) {
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
      m_state |= 0x80;
      addRest(beginOffset, curOffset - beginOffset, dur);
      return true;
    }
    if (m_isFmTrack) {
      u8 semitones = (opcode >> 4) - 1;
      u8 note = semitones + m_noteOffset + m_transpose + globalTranspose();
      note += 12;

      u32 noteDur = dur;
      if (m_noteDurPercent > 0) {
        noteDur = dur * (m_noteDurPercent / 256.0);
      }
      noteDur = std::max(1u, noteDur);

      u8 vel = calculateVol(0x7F) * 127.0;
      onNoteBegin(noteDur);
      addNoteByDur(beginOffset, curOffset - beginOffset, note, vel, noteDur);
    } else {
      if (percussionMode()) {
        u8 semitones = opcode >> 4;
        s8 note = semitones + (m_drumBank * 16);
        auto drum = drumInfo(m_drumBank, semitones);
        m_baseVol = drum ? drum->volume : 0x7F;
        if (m_baseVol > 0x7F) {
          // values greater than 7F have special unimplemented behavior
          // ex: tmnt2 seq 4
          m_baseVol &= 0x7F;
        }
        m_instrPan = drum ? drum->default_pan : 0;

        u8 finalAtten = 0x7F - (calculateVol(m_baseVol) * 127.0);
        u8 vel = K053260_BASE_VEL - finalAtten;

        if (fmtVersion() == VENDETTA) {
          if (drum->default_pan) {
            m_pan = drum->default_pan;
            updatePan();
          }
        }
        else if (m_pan == 0)
          updatePan();

        onNoteBegin(dur);
        addNoteByDur(beginOffset, curOffset - beginOffset, note, vel, dur);
      } else {
        // Melodic
        u8 semitones = (opcode >> 4) - 1;
        u8 note = semitones + m_noteOffset + m_transpose + globalTranspose();

        u32 noteDur = dur;
        if (m_noteDurPercent > 0) {
          noteDur = dur * (m_noteDurPercent / 256.0);
        }
        noteDur = std::max(1u, noteDur);

        u8 finalAtten = 0x7F - (calculateVol(m_baseVol) * 127.0);
        u8 vel = K053260_BASE_VEL - finalAtten;

        onNoteBegin(noteDur);
        addNoteByDur(beginOffset, curOffset - beginOffset, note, vel, noteDur);
      }
    }
    addTime(dur);
    return true;
  }

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
        m_dxAtten = (opcode & 0xF) * m_dxAttenMultiplier;
        m_dxAtten = std::min<u8>(m_dxAtten, 0x7F);
      }
      else {
        m_dxAtten = (opcode & 0xF) * std::max<u8>(1, m_dxAttenMultiplier);
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
      // Duration extender
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
      m_pan = readByte(curOffset++);
      addPan(beginOffset, 2, calculatePan());
      break;
    case 0xDB: {
      m_noteDurPercent = readByte(curOffset++);
      auto desc = fmt::format("Note Duration Percent - {} / 256 = {:.1f}%", m_noteDurPercent, (m_noteDurPercent / 256.0) * 100);
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
      addGenericEvent(beginOffset, 1, "Set Duration-related flag", "", Type::DurationChange);
      break;
    case 0xDF: {
      // Halve Base Duration Toggle
      if (m_baseDurHalveBackup == 0) {
        m_baseDurHalveBackup = m_baseDur;
        m_baseDur /= 2;
      } else {
        m_baseDur = m_baseDurHalveBackup;
        m_baseDurHalveBackup = 0;
      }
      auto name = fmt::format("Halve Duration {}", m_baseDurHalveBackup == 0 ? "(on)" : "(off)");
      addGenericEvent(beginOffset, 1, name, "", Type::DurationChange);
      break;
    }
    case 0xE0: {
      u8 val = readByte(curOffset++);

      if (m_isFmTrack) {
        m_state |= 4;
        if (val == 0) {
          u8 tickSkip = readByte(curOffset++);
          double tempo = calculateTempo(YM2151_CLKB, YM2151_CLOCK_RATE, PPQN, tickSkip);
          addTempoBPMNoItem(tempo);

          // byte effectively sets ppqn / tempo. Still need to hammer this down
          val = readByte(curOffset++);
        }
        m_rawBaseDur = val;
        m_baseDur = m_rawBaseDur * 3;
        m_program = readByte(curOffset++);
        // TMNT2 is weird. It has 113 FM instruments, but code to handle > 128, however, it just
        // performs a simple &= 0x7F. Values over 127 are used in sequences.
        if (fmtVersion() == TMNT2)
          m_program &= 0x7F;

        addProgramChangeNoItem(m_program, true);
        u8 attenuation = std::min<u8>(readByte(curOffset++), 0x7F);
        addVolNoItem(0x7F - attenuation);
        m_noteDurPercent = readByte(curOffset++);
        addGenericEvent(beginOffset, curOffset - beginOffset, "Program Change / Base Dur / Attenuation / State / Note Dur", "", Type::ProgramChange);
      } else {
        if ((val & 0xF0) == 0) {
          m_state = val & 0xF0;
          m_rawBaseDur = val;
          m_baseDur = m_rawBaseDur * 3;
          m_program = readByte(curOffset++);
          addProgramChangeNoItem(m_program, false);
          m_attenuation = readByte(curOffset++) & 0x7F;
          handleProgramChangeK053260();

          m_noteDurPercent = readByte(curOffset++);
          addGenericEvent(beginOffset, curOffset - beginOffset, "Program Change / Base Dur / Attenuation / State / Note Dur", "", Type::ProgramChange);
        }
        else {
          m_drumBank = (val >> 4) - 1;
          m_rawBaseDur = val & 0xF;
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
        if (val == 0) {
          setPercussionModeOff();
          addGenericEvent(beginOffset, 2, "Percussion Mode Off", "", Type::ChangeState);
          break;
        }
        m_drumBank = val;
        setPercussionModeOn();
        addGenericEvent(beginOffset, 2, "Percussion Mode On", "", Type::ChangeState);
      }
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
      if (m_isFmTrack) {
        // TMNT2 is weird. It has 113 FM instruments, but code to handle > 128, however, it just
        // performs a simple &= 0x7F. Oddly, values over 127 are what's actually used.
        m_program &= 0x7F;
      } else {
        handleProgramChangeK053260();
      }
      addProgramChange(beginOffset, 2, m_program, true);
      break;
    case 0xE4: {
      m_attenuation = readByte(curOffset++) & 0x7F;
      addGenericEvent(beginOffset, 2, "Set Attenuation", "", Type::Volume);
      if (m_isFmTrack)
        addVolNoItem(0x7F - m_attenuation);
      else {
        // updateVolume();
        // addVolNoItem(0x7F - m_attenuation);
      }
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
    case 0xE6:
      // used in ssriders 15
      curOffset += 1;
      addUnknown(beginOffset, curOffset - beginOffset, fmt::format("Opcode {:02X}", opcode));
      break;
    case 0xE7:
      curOffset += 1;
      addUnknown(beginOffset, curOffset - beginOffset, fmt::format("Opcode {:02X}", opcode));
      break;
    case 0xE8:    // NOP
      break;
    case 0xE9: {
      // Set up global and channel LFO params
      u8 lfrq = readByte(curOffset++);
      if (lfrq == 0) {
        m_lfoRampStepTicks = 0;
        addGenericEvent(beginOffset, 2, "LFO Effects Off", "", Type::Lfo);
        break;
      }
      // Set YM2151 LFO Frequency (global)
      addControllerEventNoItem(35, lfrq & 1);
      addControllerEventNoItem(3, lfrq >> 1);

      // Set YM2151 pulse modulation depth (global)
      u8 pmd = readByte(curOffset++);
      addBreathNoItem(pmd);

      // Set YM2151 amplitude modulation depth (global)
      u8 amd = readByte(curOffset++);
      addModulationNoItem(amd);

      // Set YM2151 LFO waveform (global)
      u8 waveformAndRampInterval = readByte(curOffset++);
      addControllerEventNoItem(79, waveformAndRampInterval & 3);

      // Set YM2151 LFO ramp rate and delay (channel). This determines how many ticks to process
      // before incrementing AMS and PMS (per channel amplitude/pitch modulation sensitivity)

      m_lfoRampStepTicks = std::max(1, (waveformAndRampInterval & 0xF0) >> 3);
      m_lfoDelay = std::max<int>(1, readByte(curOffset++));
      addGenericEvent(beginOffset, 6, "LFO Setup", "", Type::Lfo);
      break;
    }
    case 0xEA:
      // Set LFO delay
      m_lfoDelay = std::max<int>(1, readByte(curOffset++));
      addGenericEvent(beginOffset, 2, "LFO Delay", "", Type::Lfo);
      break;
    case 0xEB: {
      // Portamento (at least for YM2151)
      curOffset++;
      addGenericEvent(beginOffset, 2, "Portamento", "", Type::Unknown);
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
        setGlobalTranspose(transpose);
        auto desc = fmt::format("Global Transpose - {} semitones", transpose);
        addGenericEvent(beginOffset, 2, "Global Transpose", desc, Type::Transpose);
      }
      else {
        m_transpose = transpose;
        auto desc = fmt::format("Transpose - {} semitones", transpose);
        addGenericEvent(beginOffset, 2, "Transpose", desc, Type::Transpose);
      }
      break;
    }
    case 0xED: {
      // Pitch bend
      s8 val = static_cast<s8>(readByte(curOffset++));

      if (m_isFmTrack) {
        int bend = val & 0x7F;
        if (val & 0x80)
          bend = -bend;
        double cents = bend * (100/64.0);
        addPitchBendAsPercent(beginOffset, 2, cents / 200.0);
      } else {
        addGenericEvent(beginOffset, 2, "Pitch Bend", "", Type::Unknown);
      }
      break;
    }
    case 0xEE:
      curOffset += 3;
      addUnknown(beginOffset, curOffset - beginOffset, fmt::format("Opcode {:02X}", opcode));
      break;
    case 0xEF: {
      // Master Volume
      s8 ym2151MasterAtten = -static_cast<s8>(readByte(curOffset++));
      s8 k053260MasterAtten = -static_cast<s8>(readByte(curOffset++));
      setMasterAttenuationYM2151(ym2151MasterAtten);
      setMasterAttenuationK053260(k053260MasterAtten);
      // It may be more accurate to immediately update the volume for the YM2151 and K053260

      auto desc = fmt::format("YM2151: %d  K053260: %d", ym2151MasterAtten, k053260MasterAtten);
      addGenericEvent(beginOffset, 3, "Set Master Volume", desc, Type::MasterVolume);
      break;
    }
    case 0xF0:
    case 0xF1:
    case 0xF2:
    case 0xF3:
    case 0xF4:
    case 0xF5:
    case 0xF6:
    case 0xF7: {
      if (!m_isFmTrack && percussionMode()) {
        m_drumBank = (opcode & 0xF) - 1;
      } else {
        m_noteOffset = (opcode & 0xF) * 12;
      }
      addGenericEvent(beginOffset, 1, "Set Octave", fmt::format("Octave - {}", opcode & 0xF), Type::Octave);
      break;
    }
    case 0xF8:    // NOP
      break;
    // JUMP
    case 0xF9: {
      u16 dest = readShort(curOffset);
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
  return true;
}
