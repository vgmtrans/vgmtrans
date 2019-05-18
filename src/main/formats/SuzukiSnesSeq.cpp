/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
 

#include <cassert>
#include <cwchar>
#include <cmath>
#include <algorithm>
#include <climits>
#include <stdio.h>
#include <cstdint>

#include <fstream>
#include <vector>
#include <list>
#include <map>
#include <string>
#include <cstring>
#include <sstream>
#include <unordered_set>
#include <iterator>
#include <iostream>
#include <iomanip>
#include <ctype.h>
#include "portable.h"
#define countof(arr) sizeof(arr) / sizeof(arr[0])


#include "SuzukiSnesSeq.h"

DECLARE_FORMAT(SuzukiSnes);

//  *************
//  SuzukiSnesSeq
//  *************
#define MAX_TRACKS  8
#define SEQ_PPQN    48

const uint8_t SuzukiSnesSeq::NOTE_DUR_TABLE[13] = {
    0xc0, 0x90, 0x60, 0x48, 0x30, 0x24, 0x20, 0x18,
    0x10, 0x0c, 0x08, 0x06, 0x03
};

SuzukiSnesSeq::SuzukiSnesSeq(RawFile *file, SuzukiSnesVersion ver, uint32_t seqdataOffset, std::wstring newName)
    : VGMSeq(SuzukiSnesFormat::name, file, seqdataOffset, 0, newName),
      version(ver) {
  bLoadTickByTick = true;
  bAllowDiscontinuousTrackData = true;
  bUseLinearAmplitudeScale = true;

  UseReverb();
  AlwaysWriteInitialReverb(0);

  LoadEventMap();
}

SuzukiSnesSeq::~SuzukiSnesSeq(void) {
}

void SuzukiSnesSeq::ResetVars(void) {
  VGMSeq::ResetVars();

  spcTempo = 0x81; // just in case
}

bool SuzukiSnesSeq::GetHeaderInfo(void) {
  SetPPQN(SEQ_PPQN);

  VGMHeader *header = AddHeader(dwOffset, 0);
  uint32_t curOffset = dwOffset;

  // skip unknown stream
  if (version != SUZUKISNES_SD3) {
    while (true) {
      if (curOffset + 1 >= 0x10000) {
        return false;
      }

      uint8_t firstByte = GetByte(curOffset);
      if (firstByte >= 0x80) {
        header->AddSimpleItem(curOffset, 1, L"Unknown Items End");
        curOffset++;
        break;
      }
      else {
        header->AddUnknownItem(curOffset, 5);
        curOffset += 5;
      }
    }
  }

  // create tracks
  for (int trackIndex = 0; trackIndex < MAX_TRACKS; trackIndex++) {
    uint16_t addrTrackStart = GetShort(curOffset);

    if (addrTrackStart != 0) {
      std::wstringstream trackName;
      trackName << L"Track Pointer " << (trackIndex + 1);
      header->AddSimpleItem(curOffset, 2, trackName.str().c_str());

      aTracks.push_back(new SuzukiSnesTrack(this, addrTrackStart));
    }
    else {
      // example: Super Mario RPG - Where Am I Going?
      header->AddSimpleItem(curOffset, 2, L"NULL");
    }

    curOffset += 2;
  }

  header->SetGuessedLength();

  return true;        //successful
}

bool SuzukiSnesSeq::GetTrackPointers(void) {
  return true;
}

void SuzukiSnesSeq::LoadEventMap() {
  for (unsigned int statusByte = 0x00; statusByte <= 0xc3; statusByte++) {
    EventMap[statusByte] = EVENT_NOTE;
  }

  EventMap[0xc4] = EVENT_OCTAVE_UP;
  EventMap[0xc5] = EVENT_OCTAVE_DOWN;
  EventMap[0xc6] = EVENT_OCTAVE;
  EventMap[0xc7] = EVENT_NOP;
  EventMap[0xc8] = EVENT_NOISE_FREQ;
  EventMap[0xc9] = EVENT_NOISE_ON;
  EventMap[0xca] = EVENT_NOISE_OFF;
  EventMap[0xcb] = EVENT_PITCH_MOD_ON;
  EventMap[0xcc] = EVENT_PITCH_MOD_OFF;
  EventMap[0xcd] = EVENT_JUMP_TO_SFX_LO;
  EventMap[0xce] = EVENT_JUMP_TO_SFX_HI;
  EventMap[0xcf] = EVENT_TUNING;
  EventMap[0xd0] = EVENT_END;
  EventMap[0xd1] = EVENT_TEMPO;
  if (version == SUZUKISNES_SD3) {
    EventMap[0xd2] = EVENT_LOOP_START; // duplicated
    EventMap[0xd3] = EVENT_LOOP_START; // duplicated
  }
  else {
    EventMap[0xd2] = EVENT_TIMER1_FREQ;
    EventMap[0xd3] = EVENT_TIMER1_FREQ_REL;
  }
  EventMap[0xd4] = EVENT_LOOP_START;
  EventMap[0xd5] = EVENT_LOOP_END;
  EventMap[0xd6] = EVENT_LOOP_BREAK;
  EventMap[0xd7] = EVENT_LOOP_POINT;
  EventMap[0xd8] = EVENT_ADSR_DEFAULT;
  EventMap[0xd9] = EVENT_ADSR_AR;
  EventMap[0xda] = EVENT_ADSR_DR;
  EventMap[0xdb] = EVENT_ADSR_SL;
  EventMap[0xdc] = EVENT_ADSR_SR;
  EventMap[0xdd] = EVENT_DURATION_RATE;
  EventMap[0xde] = EVENT_PROGCHANGE;
  EventMap[0xdf] = EVENT_NOISE_FREQ_REL;
  if (version == SUZUKISNES_SD3) {
    EventMap[0xe0] = EVENT_VOLUME;
  }
  else { // SUZUKISNES_BL, SUZUKISNES_SMR
    EventMap[0xe0] = EVENT_UNKNOWN1;
  }
  //EventMap[0xe1] = (SuzukiSnesSeqEventType)0;
  EventMap[0xe2] = EVENT_VOLUME;
  EventMap[0xe3] = EVENT_VOLUME_REL;
  EventMap[0xe4] = EVENT_VOLUME_FADE;
  EventMap[0xe5] = EVENT_PORTAMENTO;
  EventMap[0xe6] = EVENT_PORTAMENTO_TOGGLE;
  EventMap[0xe7] = EVENT_PAN;
  EventMap[0xe8] = EVENT_PAN_FADE;
  EventMap[0xe9] = EVENT_PAN_LFO_ON;
  EventMap[0xea] = EVENT_PAN_LFO_RESTART;
  EventMap[0xeb] = EVENT_PAN_LFO_OFF;
  EventMap[0xec] = EVENT_TRANSPOSE_ABS;
  EventMap[0xed] = EVENT_TRANSPOSE_REL;
  EventMap[0xee] = EVENT_PERC_ON;
  EventMap[0xef] = EVENT_PERC_OFF;
  EventMap[0xf0] = EVENT_VIBRATO_ON;
  EventMap[0xf1] = EVENT_VIBRATO_ON_WITH_DELAY;
  EventMap[0xf2] = EVENT_TEMPO_REL;
  EventMap[0xf3] = EVENT_VIBRATO_OFF;
  EventMap[0xf4] = EVENT_TREMOLO_ON;
  EventMap[0xf5] = EVENT_TREMOLO_ON_WITH_DELAY;
  if (version == SUZUKISNES_SD3) {
    EventMap[0xf6] = EVENT_OCTAVE_UP; // duplicated
  }
  else {
    EventMap[0xf6] = EVENT_UNKNOWN1;
  }
  EventMap[0xf7] = EVENT_TREMOLO_OFF;
  EventMap[0xf8] = EVENT_SLUR_ON;
  EventMap[0xf9] = EVENT_SLUR_OFF;
  EventMap[0xfa] = EVENT_ECHO_ON;
  EventMap[0xfb] = EVENT_ECHO_OFF;
  if (version == SUZUKISNES_SD3) {
    EventMap[0xfc] = EVENT_CALL_SFX_LO;
    EventMap[0xfd] = EVENT_CALL_SFX_HI;
    EventMap[0xfe] = EVENT_OCTAVE_UP; // duplicated
    EventMap[0xff] = EVENT_OCTAVE_UP; // duplicated
  }
  else if (version == SUZUKISNES_BL) {
    EventMap[0xfc] = EVENT_OCTAVE_UP; // duplicated
    EventMap[0xfd] = EVENT_OCTAVE_UP; // duplicated
    EventMap[0xfe] = EVENT_UNKNOWN0;
    EventMap[0xff] = EVENT_UNKNOWN0;
  }
  else if (version == SUZUKISNES_SMR) {
    EventMap[0xfc] = EVENT_UNKNOWN3;
    EventMap[0xfd] = EVENT_OCTAVE_UP; // duplicated
    EventMap[0xfe] = EVENT_UNKNOWN0;
    EventMap[0xff] = EVENT_OCTAVE_UP; // duplicated
  }
}

double SuzukiSnesSeq::GetTempoInBPM(uint8_t tempo) {
  if (tempo != 0) {
    return (double) 60000000 / (125 * tempo * SEQ_PPQN);
  }
  else {
    return 1.0; // since tempo 0 cannot be expressed, this function returns a very small value.
  }
}

//  ***************
//  SuzukiSnesTrack
//  ***************

SuzukiSnesTrack::SuzukiSnesTrack(SuzukiSnesSeq *parentFile, long offset, long length)
    : SeqTrack(parentFile, offset, length) {
}

void SuzukiSnesTrack::ResetVars(void) {
  SeqTrack::ResetVars();

  vel = 100;
  octave = 6;
  spcVolume = 100;
  loopLevel = 0;
  infiniteLoopPoint = 0;
}

bool SuzukiSnesTrack::ReadEvent(void) {
  SuzukiSnesSeq *parentSeq = (SuzukiSnesSeq *) this->parentSeq;

  uint32_t beginOffset = curOffset;
  if (curOffset >= 0x10000) {
    return false;
  }

  uint8_t statusByte = GetByte(curOffset++);
  bool bContinue = true;

  std::wstringstream desc;

  SuzukiSnesSeqEventType eventType = (SuzukiSnesSeqEventType) 0;
  std::map<uint8_t, SuzukiSnesSeqEventType>::iterator pEventType = parentSeq->EventMap.find(statusByte);
  if (pEventType != parentSeq->EventMap.end()) {
    eventType = pEventType->second;
  }

  switch (eventType) {
    case EVENT_UNKNOWN0:
      desc << L"Event: 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << (int) statusByte;
      AddUnknown(beginOffset, curOffset - beginOffset, L"Unknown Event", desc.str().c_str());
      break;

    case EVENT_UNKNOWN1: {
      uint8_t arg1 = GetByte(curOffset++);
      desc << L"Event: 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << (int) statusByte
          << std::dec << std::setfill(L' ') << std::setw(0)
          << L"  Arg1: " << (int) arg1;
      AddUnknown(beginOffset, curOffset - beginOffset, L"Unknown Event", desc.str().c_str());
      break;
    }

    case EVENT_UNKNOWN2: {
      uint8_t arg1 = GetByte(curOffset++);
      uint8_t arg2 = GetByte(curOffset++);
      desc << L"Event: 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << (int) statusByte
          << std::dec << std::setfill(L' ') << std::setw(0)
          << L"  Arg1: " << (int) arg1
          << L"  Arg2: " << (int) arg2;
      AddUnknown(beginOffset, curOffset - beginOffset, L"Unknown Event", desc.str().c_str());
      break;
    }

    case EVENT_UNKNOWN3: {
      uint8_t arg1 = GetByte(curOffset++);
      uint8_t arg2 = GetByte(curOffset++);
      uint8_t arg3 = GetByte(curOffset++);
      desc << L"Event: 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << (int) statusByte
          << std::dec << std::setfill(L' ') << std::setw(0)
          << L"  Arg1: " << (int) arg1
          << L"  Arg2: " << (int) arg2
          << L"  Arg3: " << (int) arg3;
      AddUnknown(beginOffset, curOffset - beginOffset, L"Unknown Event", desc.str().c_str());
      break;
    }

    case EVENT_UNKNOWN4: {
      uint8_t arg1 = GetByte(curOffset++);
      uint8_t arg2 = GetByte(curOffset++);
      uint8_t arg3 = GetByte(curOffset++);
      uint8_t arg4 = GetByte(curOffset++);
      desc << L"Event: 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << (int) statusByte
          << std::dec << std::setfill(L' ') << std::setw(0)
          << L"  Arg1: " << (int) arg1
          << L"  Arg2: " << (int) arg2
          << L"  Arg3: " << (int) arg3
          << L"  Arg4: " << (int) arg4;
      AddUnknown(beginOffset, curOffset - beginOffset, L"Unknown Event", desc.str().c_str());
      break;
    }

    case EVENT_NOTE: // 0x00..0xc3
    {
      uint8_t durIndex = statusByte / 14;
      uint8_t noteIndex = statusByte % 14;

      uint32_t dur;
      if (durIndex == 13) {
        dur = GetByte(curOffset++);
        if (parentSeq->version == SUZUKISNES_SD3)
          dur++;
      }
      else {
        dur = SuzukiSnesSeq::NOTE_DUR_TABLE[durIndex];
      }

      if (noteIndex < 12) {
        uint8_t note = octave * 12 + noteIndex;

        // TODO: percussion note

        AddNoteByDur(beginOffset, curOffset - beginOffset, note, vel, dur);
        AddTime(dur);
      }
      else if (noteIndex == 13) {
        MakePrevDurNoteEnd(GetTime() + dur);
        AddGenericEvent(beginOffset, curOffset - beginOffset, L"Tie", desc.str().c_str(), CLR_TIE, ICON_NOTE);
        AddTime(dur);
      }
      else {
        AddRest(beginOffset, curOffset - beginOffset, dur);
      }

      break;
    }

    case EVENT_OCTAVE_UP: {
      AddIncrementOctave(beginOffset, curOffset - beginOffset);
      break;
    }

    case EVENT_OCTAVE_DOWN: {
      AddDecrementOctave(beginOffset, curOffset - beginOffset);
      break;
    }

    case EVENT_OCTAVE: {
      uint8_t newOctave = GetByte(curOffset++);
      AddSetOctave(beginOffset, curOffset - beginOffset, newOctave);
      break;
    }

    case EVENT_NOP: {
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"NOP", desc.str().c_str(), CLR_MISC, ICON_BINARY);
      break;
    }

    case EVENT_NOISE_FREQ: {
      uint8_t newNCK = GetByte(curOffset++) & 0x1f;
      desc << L"Noise Frequency (NCK): " << (int) newNCK;
      AddGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      L"Noise Frequency",
                      desc.str().c_str(),
                      CLR_CHANGESTATE,
                      ICON_CONTROL);
      break;
    }

    case EVENT_NOISE_ON: {
      AddGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      L"Noise On",
                      desc.str().c_str(),
                      CLR_CHANGESTATE,
                      ICON_CONTROL);
      break;
    }

    case EVENT_NOISE_OFF: {
      AddGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      L"Noise Off",
                      desc.str().c_str(),
                      CLR_CHANGESTATE,
                      ICON_CONTROL);
      break;
    }

    case EVENT_PITCH_MOD_ON: {
      AddGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      L"Pitch Modulation On",
                      desc.str().c_str(),
                      CLR_CHANGESTATE,
                      ICON_CONTROL);
      break;
    }

    case EVENT_PITCH_MOD_OFF: {
      AddGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      L"Pitch Modulation Off",
                      desc.str().c_str(),
                      CLR_CHANGESTATE,
                      ICON_CONTROL);
      break;
    }

    case EVENT_JUMP_TO_SFX_LO: {
      // TODO: EVENT_JUMP_TO_SFX_LO
      uint8_t sfxIndex = GetByte(curOffset++);
      desc << L"SFX: " << (int) sfxIndex;
      AddUnknown(beginOffset, curOffset - beginOffset, L"Jump to SFX (LOWORD)", desc.str().c_str());
      bContinue = false;
      break;
    }

    case EVENT_JUMP_TO_SFX_HI: {
      // TODO: EVENT_JUMP_TO_SFX_HI
      uint8_t sfxIndex = GetByte(curOffset++);
      desc << L"SFX: " << (int) sfxIndex;
      AddUnknown(beginOffset, curOffset - beginOffset, L"Jump to SFX (HIWORD)", desc.str().c_str());
      bContinue = false;
      break;
    }

    case EVENT_CALL_SFX_LO: {
      // TODO: EVENT_CALL_SFX_LO
      uint8_t sfxIndex = GetByte(curOffset++);
      desc << L"SFX: " << (int) sfxIndex;
      AddUnknown(beginOffset, curOffset - beginOffset, L"Call SFX (LOWORD)", desc.str().c_str());
      bContinue = false;
      break;
    }

    case EVENT_CALL_SFX_HI: {
      // TODO: EVENT_CALL_SFX_HI
      uint8_t sfxIndex = GetByte(curOffset++);
      desc << L"SFX: " << (int) sfxIndex;
      AddUnknown(beginOffset, curOffset - beginOffset, L"Call SFX (HIWORD)", desc.str().c_str());
      bContinue = false;
      break;
    }

    case EVENT_TUNING: {
      int8_t newTuning = GetByte(curOffset++);
      AddFineTuning(beginOffset, curOffset - beginOffset, (newTuning / 16.0) * 100.0);
      break;
    }

    case EVENT_END: {
      // TODO: add "return from SFX" handler
      if ((infiniteLoopPoint & 0xff00) != 0) {
        bContinue = AddLoopForever(beginOffset, curOffset - beginOffset);
        curOffset = infiniteLoopPoint;
      }
      else {
        AddEndOfTrack(beginOffset, curOffset - beginOffset);
        bContinue = false;
      }
      break;
    }

    case EVENT_TEMPO: {
      uint8_t newTempo = GetByte(curOffset++);
      parentSeq->spcTempo = newTempo;
      AddTempoBPM(beginOffset, curOffset - beginOffset, parentSeq->GetTempoInBPM(parentSeq->spcTempo));
      break;
    }

    case EVENT_TEMPO_REL: {
      int8_t delta = GetByte(curOffset++);
      parentSeq->spcTempo += delta;
      AddTempoBPM(beginOffset,
                  curOffset - beginOffset,
                  parentSeq->GetTempoInBPM(parentSeq->spcTempo),
                  L"Tempo (Relative)");
      break;
    }

    case EVENT_TIMER1_FREQ: {
      uint8_t newFreq = GetByte(curOffset++);
      desc << L"Frequency: " << (0.125 * newFreq) << L"ms";
      AddGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      L"Timer 1 Frequency",
                      desc.str().c_str(),
                      CLR_CHANGESTATE,
                      ICON_TEMPO);
      break;
    }

    case EVENT_TIMER1_FREQ_REL: {
      int8_t delta = GetByte(curOffset++);
      desc << L"Frequency Delta: " << (0.125 * delta) << L"ms";
      AddGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      L"Timer 1 Frequency (Relative)",
                      desc.str().c_str(),
                      CLR_CHANGESTATE,
                      ICON_TEMPO);
      break;
    }

    case EVENT_LOOP_START: {
      uint8_t count = GetByte(curOffset++);
      int realLoopCount = (count == 0) ? 256 : count;

      desc << L"Loop Count: " << realLoopCount;
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Loop Start", desc.str().c_str(), CLR_LOOP, ICON_STARTREP);

      if (loopLevel >= SUZUKISNES_LOOP_LEVEL_MAX) {
        // stack overflow
        break;
      }

      loopStart[loopLevel] = curOffset;
      loopCount[loopLevel] = count - 1;
      loopOctave[loopLevel] = octave;
      loopLevel++;
      break;
    }

    case EVENT_LOOP_END: {
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Loop End", desc.str().c_str(), CLR_LOOP, ICON_ENDREP);

      if (loopLevel == 0) {
        // stack overflow
        break;
      }

      if (loopCount[loopLevel - 1] == 0) {
        // repeat end
        loopLevel--;
      }
      else {
        // repeat again
        octave = loopOctave[loopLevel - 1];
        loopEnd[loopLevel - 1] = curOffset;
        curOffset = loopStart[loopLevel - 1];
        loopCount[loopLevel - 1]--;
      }

      break;
    }

    case EVENT_LOOP_BREAK: {
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Loop Break", desc.str().c_str(), CLR_LOOP, ICON_ENDREP);

      if (loopLevel == 0) {
        // stack overflow
        break;
      }

      if (loopCount[loopLevel - 1] == 0) {
        // repeat end
        curOffset = loopEnd[loopLevel - 1];
        loopLevel--;
      }

      break;
    }

    case EVENT_LOOP_POINT: {
      infiniteLoopPoint = curOffset;
      AddGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      L"Infinite Loop Point",
                      desc.str().c_str(),
                      CLR_LOOP,
                      ICON_STARTREP);
      break;
    }

    case EVENT_ADSR_DEFAULT: {
      AddGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      L"Default ADSR",
                      desc.str().c_str(),
                      CLR_ADSR,
                      ICON_CONTROL);
      break;
    }

    case EVENT_ADSR_AR: {
      uint8_t newAR = GetByte(curOffset++) & 15;
      desc << L"AR: " << (int) newAR;
      AddGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      L"ADSR Attack Rate",
                      desc.str().c_str(),
                      CLR_ADSR,
                      ICON_CONTROL);
      break;
    }

    case EVENT_ADSR_DR: {
      uint8_t newDR = GetByte(curOffset++) & 7;
      desc << L"DR: " << (int) newDR;
      AddGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      L"ADSR Decay Rate",
                      desc.str().c_str(),
                      CLR_ADSR,
                      ICON_CONTROL);
      break;
    }

    case EVENT_ADSR_SL: {
      uint8_t newSL = GetByte(curOffset++) & 7;
      desc << L"SL: " << (int) newSL;
      AddGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      L"ADSR Sustain Level",
                      desc.str().c_str(),
                      CLR_ADSR,
                      ICON_CONTROL);
      break;
    }

    case EVENT_ADSR_SR: {
      uint8_t newSR = GetByte(curOffset++) & 15;
      desc << L"SR: " << (int) newSR;
      AddGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      L"ADSR Sustain Rate",
                      desc.str().c_str(),
                      CLR_ADSR,
                      ICON_CONTROL);
      break;
    }

    case EVENT_DURATION_RATE: {
      // TODO: save duration rate and apply to note length
      uint8_t newDurRate = GetByte(curOffset++);
      desc << L"Duration Rate: " << (int) newDurRate;
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Duration Rate", desc.str().c_str(), CLR_DURNOTE);
      break;
    }

    case EVENT_PROGCHANGE: {
      uint8_t newProg = GetByte(curOffset++);
      AddProgramChange(beginOffset, curOffset - beginOffset, newProg);
      break;
    }

    case EVENT_NOISE_FREQ_REL: {
      int8_t delta = GetByte(curOffset++);
      desc << L"Noise Frequency (NCK) Delta: " << (int) delta;
      AddGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      L"Noise Frequency (Relative)",
                      desc.str().c_str(),
                      CLR_CHANGESTATE,
                      ICON_CONTROL);
      break;
    }

    case EVENT_VOLUME: {
      uint8_t vol = GetByte(curOffset++);
      spcVolume = vol & 0x7f;
      AddVol(beginOffset, curOffset - beginOffset, spcVolume);
      break;
    }

    case EVENT_VOLUME_REL: {
      int8_t delta = GetByte(curOffset++);
      spcVolume = (spcVolume + delta) & 0x7f;
      AddVol(beginOffset, curOffset - beginOffset, spcVolume, L"Volume (Relative)");
      break;
    }

    case EVENT_VOLUME_FADE: {
      uint8_t fadeLength = GetByte(curOffset++);
      uint8_t vol = GetByte(curOffset++);
      desc << L"Fade Length: " << (int) fadeLength << L"  Volume: " << (int) vol;
      AddGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      L"Volume Fade",
                      desc.str().c_str(),
                      CLR_VOLUME,
                      ICON_CONTROL);
      break;
    }

    case EVENT_PORTAMENTO: {
      uint8_t arg1 = GetByte(curOffset++);
      uint8_t arg2 = GetByte(curOffset++);
      desc << L"Arg1: " << (int) arg1 << L"  Arg2: " << (int) arg2;
      AddGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      L"Portamento",
                      desc.str().c_str(),
                      CLR_PORTAMENTO,
                      ICON_CONTROL);
      break;
    }

    case EVENT_PORTAMENTO_TOGGLE: {
      AddGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      L"Portamento On/Off",
                      desc.str().c_str(),
                      CLR_PORTAMENTO,
                      ICON_CONTROL);
      break;
    }

    case EVENT_PAN: {
      // For left pan, the engine will decrease right volume (linear), but will do nothing to left volume.
      // For right pan, the engine will do the opposite.
      // For center pan, it will not decrease any volumes.
      uint8_t pan = GetByte(curOffset++);

      // TODO: correct midi pan value, apply volume scale
      AddPan(beginOffset, curOffset - beginOffset, pan >> 1);
      break;
    }

    case EVENT_PAN_FADE: {
      uint8_t fadeLength = GetByte(curOffset++);
      uint8_t pan = GetByte(curOffset++);
      desc << L"Fade Length: " << (int) fadeLength << L"  Pan: " << (int) (pan >> 1);

      // TODO: correct midi pan value, apply volume scale, do pan slide
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Pan Fade", desc.str().c_str(), CLR_PAN, ICON_CONTROL);
      break;
    }

    case EVENT_PAN_LFO_ON: {
      uint8_t lfoDepth = GetByte(curOffset++);
      uint8_t lfoRate = GetByte(curOffset++);
      desc << L"Depth: " << (int) lfoDepth << L"  Rate: " << (int) lfoRate;
      AddGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      L"Pan LFO",
                      desc.str().c_str(),
                      CLR_MODULATION,
                      ICON_CONTROL);
      break;
    }

    case EVENT_PAN_LFO_RESTART: {
      AddGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      L"Pan LFO Restart",
                      desc.str().c_str(),
                      CLR_MODULATION,
                      ICON_CONTROL);
      break;
    }

    case EVENT_PAN_LFO_OFF: {
      AddGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      L"Pan LFO Off",
                      desc.str().c_str(),
                      CLR_MODULATION,
                      ICON_CONTROL);
      break;
    }

    case EVENT_TRANSPOSE_ABS: {
      // TODO: fraction part of transpose?
      int8_t newTranspose = GetByte(curOffset++);
      int8_t semitones = newTranspose / 4;
      AddTranspose(beginOffset, curOffset - beginOffset, semitones);
      break;
    }

    case EVENT_TRANSPOSE_REL: {
      // TODO: fraction part of transpose?
      int8_t newTranspose = GetByte(curOffset++);
      int8_t semitones = newTranspose / 4;
      AddTranspose(beginOffset, curOffset - beginOffset, transpose + semitones, L"Transpose (Relative)");
      break;
    }

    case EVENT_PERC_ON: {
      AddGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      L"Percussion On",
                      desc.str().c_str(),
                      CLR_CHANGESTATE,
                      ICON_CONTROL);
      break;
    }

    case EVENT_PERC_OFF: {
      AddGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      L"Percussion Off",
                      desc.str().c_str(),
                      CLR_CHANGESTATE,
                      ICON_CONTROL);
      break;
    }

    case EVENT_VIBRATO_ON: {
      uint8_t lfoDepth = GetByte(curOffset++);
      uint8_t lfoRate = GetByte(curOffset++);
      desc << L"Depth: " << (int) lfoDepth << L"  Rate: " << (int) lfoRate;
      AddGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      L"Vibrato",
                      desc.str().c_str(),
                      CLR_MODULATION,
                      ICON_CONTROL);
      break;
    }

    case EVENT_VIBRATO_ON_WITH_DELAY: {
      uint8_t lfoDepth = GetByte(curOffset++);
      uint8_t lfoRate = GetByte(curOffset++);
      uint8_t lfoDelay = GetByte(curOffset++);
      desc << L"Depth: " << (int) lfoDepth << L"  Rate: " << (int) lfoRate << L"  Delay: " << (int) lfoDelay;
      AddGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      L"Vibrato",
                      desc.str().c_str(),
                      CLR_MODULATION,
                      ICON_CONTROL);
      break;
    }

    case EVENT_VIBRATO_OFF: {
      AddGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      L"Vibrato Off",
                      desc.str().c_str(),
                      CLR_MODULATION,
                      ICON_CONTROL);
      break;
    }

    case EVENT_TREMOLO_ON: {
      uint8_t lfoDepth = GetByte(curOffset++);
      uint8_t lfoRate = GetByte(curOffset++);
      desc << L"Depth: " << (int) lfoDepth << L"  Rate: " << (int) lfoRate;
      AddGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      L"Tremolo",
                      desc.str().c_str(),
                      CLR_MODULATION,
                      ICON_CONTROL);
      break;
    }

    case EVENT_TREMOLO_ON_WITH_DELAY: {
      uint8_t lfoDepth = GetByte(curOffset++);
      uint8_t lfoRate = GetByte(curOffset++);
      uint8_t lfoDelay = GetByte(curOffset++);
      desc << L"Depth: " << (int) lfoDepth << L"  Rate: " << (int) lfoRate << L"  Delay: " << (int) lfoDelay;
      AddGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      L"Tremolo",
                      desc.str().c_str(),
                      CLR_MODULATION,
                      ICON_CONTROL);
      break;
    }

    case EVENT_TREMOLO_OFF: {
      AddGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      L"Tremolo Off",
                      desc.str().c_str(),
                      CLR_MODULATION,
                      ICON_CONTROL);
      break;
    }

    case EVENT_SLUR_ON: {
      AddGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      L"Slur On",
                      desc.str().c_str(),
                      CLR_CHANGESTATE,
                      ICON_CONTROL);
      break;
    }

    case EVENT_SLUR_OFF: {
      AddGenericEvent(beginOffset,
                      curOffset - beginOffset,
                      L"Slur Off",
                      desc.str().c_str(),
                      CLR_CHANGESTATE,
                      ICON_CONTROL);
      break;
    }

    case EVENT_ECHO_ON: {
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Echo On", desc.str().c_str(), CLR_REVERB, ICON_CONTROL);
      break;
    }

    case EVENT_ECHO_OFF: {
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Echo Off", desc.str().c_str(), CLR_REVERB, ICON_CONTROL);
      break;
    }

    default:
      desc << L"Event: 0x" << std::hex << std::setfill(L'0') << std::setw(2) << std::uppercase << (int) statusByte;
      AddUnknown(beginOffset, curOffset - beginOffset, L"Unknown Event", desc.str().c_str());
      pRoot->AddLogItem(new LogItem((std::wstring(L"Unknown Event - ") + desc.str()).c_str(),
                                    LOG_LEVEL_ERR,
                                    L"CompileSnesSeq"));
      bContinue = false;
      break;
  }

  //
  //ssTrace << L"" << std::hex << std::setfill(L'0') << std::setw(8) << std::uppercase << beginOffset << L": " << std::setw(2) << (int)statusByte  << L" -> " << std::setw(8) << curOffset << std::endl;
  //OutputDebugString(ssTrace.str().c_str());

  return bContinue;
}
