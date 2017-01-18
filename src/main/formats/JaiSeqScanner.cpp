#include "pch.h"
#include "common.h"
#include "RawFile.h"

#include "JaiSeqFormat.h"
#include "JaiSeqScanner.h"

#include "VGMSamp.h"
#include "VGMSampColl.h"
#include "VGMInstrSet.h"
#include "VGMRgn.h"
#include "VGMSeq.h"
#include "SeqTrack.h"

#include <locale>
#include <string>
#include <stack>

DECLARE_FORMAT(JaiSeq);

/* Names from JASystem::TSeqParser from framework.map in The Wind Waker. */
enum MML {
  /* wait with u8 arg */
  MML_WAIT_8 = 0x80,
  /* wait with u16 arg */
  MML_WAIT_16 = 0x88,
  /* wait with variable-length arg */
  MML_WAIT_VAR = 0xF0,

  /* Perf is hard to enum, but here goes: */
  MML_PERF_U8_NODUR = 0x94,
  MML_PERF_U8_DUR_U8 = 0x96,
  MML_PERF_U8_DUR_U16 = 0x97,
  MML_PERF_S8_NODUR = 0x98,
  MML_PERF_S8_DUR_U8 = 0x9A,
  MML_PERF_S8_DUR_U16 = 0x9B,
  MML_PERF_S16_NODUR = 0x9C,
  MML_PERF_S16_DUR_U8 = 0x9E,
  MML_PERF_S16_DUR_U16 = 0x9F,

  MML_PARAM_SET_8 = 0xA4,
  MML_PARAM_SET_16 = 0xAC,

  MML_OPEN_TRACK = 0xC1,
  /* open sibling track, seems unused */
  MML_OPEN_TRACK_BROS = 0xC2,
  MML_CALL = 0xC3,
  MML_CALL_COND = 0xC4,
  MML_RET = 0xC5,
  MML_RET_COND = 0xC6,
  MML_JUMP = 0xC7,
  MML_JUMP_COND = 0xC8,
  MML_TIME_BASE = 0xFD,
  MML_TEMPO = 0xFE,
  MML_FIN = 0xFF,
};

class JaiSeqTrack : public SeqTrack {
public:
  JaiSeqTrack(VGMSeq *parentFile, uint32_t offset, uint32_t length) :
    SeqTrack(parentFile, offset, length) {}

private:
  /* MIDI handles note durations by having "Note Off" events and keying it
   * to the note value. JaiSeq uses voice IDs and tells us when to turn off
   * voice IDs. Use a table to remember when each voice is playing each note. */
  uint8_t voiceToNote[8];

  struct StackFrame {
    uint32_t retOffset;
  };
  std::stack<StackFrame> callStack;

  uint32_t Read24BE(uint32_t &offset) {
    uint32_t val = (GetWordBE(offset - 1) & 0x00FFFFFF);
    offset += 3;
    return val;
  }

  enum MmlParam {
    MML_BANK = 0x20,
    MML_PROG = 0x21,
  };

  void SetParam(uint32_t beginOffset, uint8_t param, uint16_t value) {
    if (param == MML_PROG)
      AddProgramChange(beginOffset, curOffset - beginOffset, value);
    else if (param == MML_BANK)
      AddUnknown(beginOffset, curOffset - beginOffset, L"Bank Select");
    else
      AddUnknown(beginOffset, curOffset - beginOffset);
  }

  enum PerfType {
    MML_VOLUME = 0,
    MML_PITCH = 1,
    MML_PAN = 3,
  };

  void SetPerf(uint32_t beginOffset, uint8_t type, double value, uint16_t duration) {
    if (type == MML_VOLUME) {
      /* MIDI volume is between 0 and 7F */
      uint8_t midValue = (uint8_t)(value * 0x7F);
      if (duration == 0)
        AddVol(beginOffset, curOffset - beginOffset, midValue);
      else
        AddVolSlide(beginOffset, curOffset - beginOffset, duration, midValue);
    }
    else if (type == MML_PAN) {
      /* MIDI pan is between 0 and 7F, with 3F being the center. */
      uint8_t midValue = (uint8_t)((value * 0x3F) + 0x3F);
      if (duration == 0)
        AddPan(beginOffset, curOffset - beginOffset, midValue);
      else
        AddPanSlide(beginOffset, curOffset - beginOffset, duration, midValue);
    }
    else if (type == MML_PITCH) {
      /* MIDI pitch is between 0 and 7FFF */
      uint16_t midValue = (uint16_t)(value * 0x7FFF);
      if (duration == 0)
        AddPitchBend(beginOffset, curOffset - beginOffset, midValue);
      else
        AddPitchBend(beginOffset, curOffset - beginOffset, midValue); /* XXX what to do here? */
    }
  }

  void SetPerf(uint32_t beginOffset, uint8_t type, uint8_t value, uint16_t duration) {
    double valueFloat = (double)value / 0xFF;
    SetPerf(beginOffset, type, valueFloat, duration);
  }

  void SetPerf(uint32_t beginOffset, uint8_t type, int8_t value, uint16_t duration) {
    double valueFloat = (double)value / 0x7F;
    SetPerf(beginOffset, type, valueFloat, duration);
  }

  void SetPerf(uint32_t beginOffset, uint8_t type, int16_t value, uint16_t duration) {
    double valueFloat = (double)value / 0x7FFF;
    SetPerf(beginOffset, type, valueFloat, duration);
  }

  virtual bool ReadEvent() override {
    uint32_t beginOffset = curOffset;
    uint8_t status_byte = GetByte(curOffset++);

    if (status_byte < 0x80) {
      /* Note on. */
      uint8_t note = status_byte;
      uint8_t voice = GetByte(curOffset++);
      uint8_t vel = GetByte(curOffset++);
      assert(voice >= 0x01 && voice <= 0x08);
      voiceToNote[voice - 1] = status_byte;
      AddNoteOn(beginOffset, curOffset - beginOffset, note, vel);
    }
    else if (status_byte == MML_WAIT_8) {
      uint8_t waitTime = GetByte(curOffset++);
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Rest", L"MML_WAIT_8", CLR_REST);
      AddTime(waitTime);
    }
    else if (status_byte < 0x88) {
      /* Note off. */
      uint8_t voice = (status_byte) & ~0x80;
      uint8_t note = voiceToNote[voice - 1];
      AddNoteOff(beginOffset, curOffset - beginOffset, note);
    }
    else {
      switch (status_byte) {
      case MML_WAIT_16: {
        uint16_t waitTime = GetShortBE(curOffset);
        curOffset += 2;
        AddGenericEvent(beginOffset, curOffset - beginOffset, L"Rest", L"MML_WAIT_16", CLR_REST);
        AddTime(waitTime);
        break;
      }
      case MML_WAIT_VAR: {
        uint32_t waitTime = ReadVarLen(curOffset);
        AddGenericEvent(beginOffset, curOffset - beginOffset, L"Rest", L"MML_WAIT_VAR", CLR_REST);
        AddTime(waitTime);
        break;
      }
      case MML_OPEN_TRACK:
        /* Hopefully was handled in the JaiSeqSeq parent impl. */
        curOffset += 4;
        break;

      case MML_CALL_COND:
        /* conditional call, eat the mode parameter */
        curOffset++;
        /* fallthrough */
      case MML_CALL: {
        uint32_t destOffset = parentSeq->dwOffset + Read24BE(curOffset);
        AddGenericEvent(beginOffset, 4, L"Call", L"", CLR_LOOP);
        callStack.push({ curOffset });
        curOffset = destOffset;
        break;
      }
      case MML_RET_COND:
        /* conditional ret, eat the mode parameter */
        curOffset++;
        /* fallthrough */
      case MML_RET:
        AddGenericEvent(beginOffset, curOffset - beginOffset, L"Return", L"", CLR_LOOP);
        curOffset = callStack.top().retOffset;
        callStack.pop();
        break;
      case MML_JUMP_COND:
        /* conditional jump, eat the mode parameter */
        curOffset++;
        /* fallthrough */
      case MML_JUMP: {
        uint32_t destOffset = parentSeq->dwOffset + Read24BE(curOffset);
        if (IsOffsetUsed(destOffset)) {
          /* This isn't going to go anywhere... just quit early. */
          return AddLoopForever(beginOffset, 4);
        }
        else {
          AddGenericEvent(beginOffset, 4, L"Jump", L"", CLR_LOOPFOREVER);
        }
        curOffset = destOffset;
        break;
      }

      case MML_PERF_U8_NODUR: {
        uint8_t type = GetByte(curOffset++);
        uint8_t value = GetByte(curOffset++);
        SetPerf(beginOffset, type, value, 0);
        break;
      }
      case MML_PERF_U8_DUR_U8: {
        uint8_t type = GetByte(curOffset++);
        uint8_t value = GetByte(curOffset++);
        uint8_t duration = GetByte(curOffset++);
        SetPerf(beginOffset, type, value, duration);
        break;
      }
      case MML_PERF_U8_DUR_U16: {
        uint8_t type = GetByte(curOffset++);
        uint8_t value = GetByte(curOffset++);
        uint16_t duration = GetShortBE(curOffset);
        curOffset += 2;
        SetPerf(beginOffset, type, value, duration);
        break;
      }
      case MML_PERF_S8_NODUR: {
        uint8_t type = GetByte(curOffset++);
        int8_t value = (int8_t)GetByte(curOffset++);
        SetPerf(beginOffset, type, value, 0);
        break;
      }
      case MML_PERF_S8_DUR_U8: {
        uint8_t type = GetByte(curOffset++);
        int8_t value = (int8_t)GetByte(curOffset++);
        uint8_t duration = GetByte(curOffset++);
        SetPerf(beginOffset, type, value, duration);
        break;
      }
      case MML_PERF_S8_DUR_U16: {
        uint8_t type = GetByte(curOffset++);
        int8_t value = (int8_t)GetByte(curOffset++);
        uint16_t duration = GetShortBE(curOffset);
        curOffset += 2;
        SetPerf(beginOffset, type, value, duration);
        break;
      }
      case MML_PERF_S16_NODUR: {
        uint8_t type = GetByte(curOffset++);
        int16_t value = (int16_t)GetShortBE(curOffset);
        curOffset += 2;
        SetPerf(beginOffset, type, value, 0);
        break;
      }
      case MML_PERF_S16_DUR_U8: {
        uint8_t type = GetByte(curOffset++);
        int16_t value = (int16_t)GetShortBE(curOffset);
        curOffset += 2;
        uint8_t duration = GetByte(curOffset++);
        SetPerf(beginOffset, type, value, duration);
        break;
      }
      case MML_PERF_S16_DUR_U16: {
        uint8_t type = GetByte(curOffset++);
        int16_t value = (int16_t)GetShortBE(curOffset);
        curOffset += 2;
        uint16_t duration = GetShortBE(curOffset);
        curOffset += 2;
        SetPerf(beginOffset, type, value, duration);
        break;
      }

      case MML_PARAM_SET_8: {
        uint8_t param = GetByte(curOffset++);
        uint8_t value = GetByte(curOffset++);
        SetParam(beginOffset, param, value);
        break;
      }
      case MML_PARAM_SET_16: {
        uint8_t param = GetByte(curOffset++);
        uint16_t value = GetShortBE(curOffset);
        curOffset += 2;
        SetParam(beginOffset, param, value);
        break;
      }

      case 0xE0:
        curOffset += 2;
        AddUnknown(beginOffset, curOffset - beginOffset, L"Unset Dynamic");
        break;
      case 0xE1:
        curOffset += 1;
        AddUnknown(beginOffset, curOffset - beginOffset, L"Clear Dynamic");
        break;
      case 0xE2:
      case 0xE3:
      case 0xF4: /* VibPitch */
        curOffset += 1;
        AddUnknown(beginOffset, curOffset - beginOffset);
        break;
      case 0xB8:
      case 0xCB:
      case 0xE6: /* Vibrato */
      case 0xE7: /* SyncGPU */
      case 0xF9:
        curOffset += 2;
        AddUnknown(beginOffset, curOffset - beginOffset);
        break;
      case 0xB9:
      case 0xD8:
        curOffset += 3;
        AddUnknown(beginOffset, curOffset - beginOffset);
        break;
      case MML_TEMPO: {
        uint16_t bpm = GetShortBE(curOffset);
        curOffset += 2;
        AddTempoBPM(beginOffset, curOffset - beginOffset, bpm);
        break;
      }
      case MML_TIME_BASE: {
        uint16_t ppqn = GetShortBE(curOffset);
        curOffset += 2;
        /* XXX: This can differ per-track. What happens then? */
        parentSeq->SetPPQN(ppqn);
        break;
      }
      case MML_FIN:
        AddEndOfTrack(beginOffset, curOffset - beginOffset);
        return false;
      default:
        AddUnknown(beginOffset, curOffset - beginOffset);
        return false;
      }
    }

    return true;
  }
};

class JaiSeqSeq : public VGMSeq {
public:
  JaiSeqSeq(RawFile *file, uint32_t offset, uint32_t length) :
    VGMSeq(JaiSeqFormat::name, file, offset, length) {
    bAllowDiscontinuousTrackData = true;
  }

private:
  virtual bool GetHeaderInfo() override {
    SetPPQN(0x30);
    return true;
  }

  void ScanForOpenTrack(uint32_t offset) {
    while (GetByte(offset) == MML_OPEN_TRACK) {
      uint8_t trackNo = GetByte(offset + 1);
      uint32_t trackOffs = GetWordBE(offset + 1) & 0x00FFFFFF;
      uint32_t trackBase = dwOffset + trackOffs;
      ScanForOpenTrack(trackBase);
      aTracks.push_back(new JaiSeqTrack(this, trackBase, 0));
      offset += 0x05;
    }
  }

  virtual bool GetTrackPointers() override {
    /* So this is a bit weird... instead of having a defined set of
     * tracks to play, we actually have tracks that *asynchronously*
     * spawn other child tracks. So we could be halfway through a song
     * when the game asks for a new track to open. Games don't seem to
     * actually use this functionality, but basically we need to be on
     * the lookout for every track to spawn another one. */

    uint32_t offset = dwOffset;
    aTracks.push_back(new JaiSeqTrack(this, offset, 0));
    ScanForOpenTrack(offset);

    return true;
  }
};

void JaiSeqScanner::Scan(RawFile *file, void *info) {
  JaiSeqSeq *seq = new JaiSeqSeq(file, 0, 0);
  seq->Load();
}
