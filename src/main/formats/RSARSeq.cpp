#include "pch.h"
#include "RSARSeq.h"

/* Seems to be very similar, if not the exact same, as the MML in SSEQ and JaiSeq.
 * Perhaps a cleanup is in order? */

/* From https://github.com/libertyernie/brawltools/blob/master/BrawlLib/SSBB/Types/Audio/RSEQ.cs#L124 */
enum {
  MML_WAIT = 0x80,
  MML_PRG = 0x81,

  MML_OPEN_TRACK = 0x88,
  MML_JUMP = 0x89,
  MML_CALL = 0x8a,

  MML_RANDOM = 0xa0,
  MML_VARIABLE = 0xa1,
  MML_IF = 0xa2,
  MML_TIME = 0xa3,
  MML_TIME_RANDOM = 0xa4,
  MML_TIME_VARIABLE = 0xa5,

  MML_TIMEBASE = 0xb0,
  MML_ENV_HOLD = 0xb1,
  MML_MONOPHONIC = 0xb2,
  MML_VELOCITY_RANGE = 0xb3,
  MML_PAN = 0xc0,
  MML_VOLUME = 0xc1,
  MML_MAIN_VOLUME = 0xc2,
  MML_TRANSPOSE = 0xc3,
  MML_PITCH_BEND = 0xc4,
  MML_BEND_RANGE = 0xc5,
  MML_PRIO = 0xc6,
  MML_NOTE_WAIT = 0xc7,
  MML_TIE = 0xc8,
  MML_PORTA = 0xc9,
  MML_MOD_DEPTH = 0xca,
  MML_MOD_SPEED = 0xcb,
  MML_MOD_TYPE = 0xcc,
  MML_MOD_RANGE = 0xcd,
  MML_PORTA_SW = 0xce,
  MML_PORTA_TIME = 0xcf,
  MML_ATTACK = 0xd0,
  MML_DECAY = 0xd1,
  MML_SUSTAIN = 0xd2,
  MML_RELEASE = 0xd3,
  MML_LOOP_START = 0xd4,
  MML_VOLUME2 = 0xd5,
  MML_PRINTVAR = 0xd6,
  MML_SURROUND_PAN = 0xd7,
  MML_LPF_CUTOFF = 0xd8,
  MML_FXSEND_A = 0xd9,
  MML_FXSEND_B = 0xda,
  MML_MAINSEND = 0xdb,
  MML_INIT_PAN = 0xdc,
  MML_MUTE = 0xdd,
  MML_FXSEND_C = 0xde,
  MML_DAMPER = 0xdf,

  MML_MOD_DELAY = 0xe0,
  MML_TEMPO = 0xe1,
  MML_SWEEP_PITCH = 0xe3,

  MML_EX_COMMAND = 0xf0,

  MML_LOOP_END = 0xfc,
  MML_RET = 0xfd,
  MML_ALLOC_TRACK = 0xfe,
  MML_FIN = 0xff,
};

uint32_t RSARTrack::ReadArg(ArgType defaultArgType, uint32_t &offset) {
  ArgType argType = nextArgType == ARG_DEFAULT ? defaultArgType : nextArgType;
  nextArgType = ARG_DEFAULT;

  switch (argType) {
  case ARG_VAR:
    return ReadVarLen(offset);
  case ARG_U8:
    return GetByte(offset++);
  case ARG_RANDOM: {
    int16_t min = GetShortBE(offset);
    offset += 2;
    int16_t max = GetShortBE(offset);
    offset += 2;
    /* To be consistent, return the average of min/max. */
    return (min + max) / 2;
  }
  case ARG_DEFAULT:
  default:
    assert(false);
  }

  return ReadVarLen(offset);
}

bool RSARTrack::ReadEvent() {
  uint32_t beginOffset = curOffset;
  uint8_t status_byte = GetByte(curOffset++);

  if (status_byte < 0x80) {
    /* Note on. */
    uint8_t vel = GetByte(curOffset++);
    dur = ReadArg(ARG_VAR, curOffset);
    AddNoteByDur(beginOffset, curOffset - beginOffset, status_byte, vel, dur);
    if (noteWait) {
      AddTime(dur);
    }
  } else {
    switch (status_byte) {
    case MML_RANDOM:
      AddUnknown(beginOffset, curOffset - beginOffset, L"Random");
      nextArgType = ARG_RANDOM;
      break;
    case MML_WAIT:
      dur = ReadArg(ARG_VAR, curOffset);
      AddRest(beginOffset, curOffset - beginOffset, dur);
      break;
    case MML_PRG: {
      uint8_t newProg = (uint8_t)ReadArg(ARG_VAR, curOffset);
      AddProgramChange(beginOffset, curOffset - beginOffset, newProg);
      break;
    }
    case MML_CALL: {
      uint32_t destOffset = parentSeq->dwOffset + Read24BE(curOffset);
      AddGenericEvent(beginOffset, 4, L"Call", L"", CLR_LOOP);
      callStack.push({ curOffset, 0 });
      curOffset = destOffset;
      break;
    }
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
    case MML_OPEN_TRACK:
      curOffset += 4;
      AddUnknown(beginOffset, curOffset - beginOffset, L"Open Track");
      break;
    case MML_PAN: {
      uint8_t pan = GetByte(curOffset++);
      AddPan(beginOffset, curOffset - beginOffset, pan);
      break;
    }
    case MML_VOLUME:
      vol = GetByte(curOffset++);
      AddVol(beginOffset, curOffset - beginOffset, vol);
      break;
    case MML_MAIN_VOLUME: {
      uint8_t mvol = GetByte(curOffset++);
      AddUnknown(beginOffset, curOffset - beginOffset, L"Master Volume");
      break;
    }
    case MML_TIMEBASE: {
      uint8_t ppqn = GetByte(curOffset++);
      parentSeq->SetPPQN(ppqn);
      break;
    }
    case MML_TRANSPOSE: {
      int8_t transpose = (int8_t) ReadArg(ARG_U8, curOffset);
      AddTranspose(beginOffset, curOffset - beginOffset, transpose);
      break;
    }
    case MML_PITCH_BEND: {
      int8_t bend = (int8_t) ReadArg(ARG_U8, curOffset);
      /* Pitch bends in RSEQ are 0-7F. MID is 16-bit: 0-1FFF. */
      int16_t midiBend = (bend / 127.0) * 0x1FFF;
      AddPitchBend(beginOffset, curOffset - beginOffset, midiBend);
      break;
    }
    case MML_BEND_RANGE: {
      uint8_t semitones = GetByte(curOffset++);
      AddPitchBendRange(beginOffset, curOffset - beginOffset, semitones);
      break;
    }
    case MML_PRIO:
      curOffset++;
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Priority", L"", CLR_CHANGESTATE);
      break;
    case MML_NOTE_WAIT: {
      noteWait = !!GetByte(curOffset++);
      AddUnknown(beginOffset, curOffset - beginOffset, L"Notewait Mode");
      break;
    }
    case MML_TIE:
      curOffset++;
      AddUnknown(beginOffset, curOffset - beginOffset, L"Tie");
      break;
    case MML_PORTA:
      curOffset++;
      AddUnknown(beginOffset, curOffset - beginOffset, L"Portamento Control");
      break;
    case MML_MOD_DEPTH: {
      uint8_t amount = GetByte(curOffset++);
      AddModulation(beginOffset, curOffset - beginOffset, amount, L"Modulation Depth");
      break;
    }
    case MML_MOD_SPEED:
      curOffset++;
      AddUnknown(beginOffset, curOffset - beginOffset, L"Modulation Speed");
      break;
    case MML_MOD_TYPE:
      curOffset++;
      AddUnknown(beginOffset, curOffset - beginOffset, L"Modulation Type");
      break;
    case MML_MOD_RANGE:
      curOffset++;
      AddUnknown(beginOffset, curOffset - beginOffset, L"Modulation Range");
      break;
    case MML_PORTA_SW: {
      bool bPortOn = (GetByte(curOffset++) != 0);
      AddPortamento(beginOffset, curOffset - beginOffset, bPortOn);
      break;
    }
    case MML_PORTA_TIME: {
      uint8_t portTime = GetByte(curOffset++);
      AddPortamentoTime(beginOffset, curOffset - beginOffset, portTime);
      break;
    }
    case MML_ATTACK:
      curOffset++;
      AddUnknown(beginOffset, curOffset - beginOffset, L"Attack Rate");
      break;
    case MML_DECAY:
      curOffset++;
      AddUnknown(beginOffset, curOffset - beginOffset, L"Decay Rate");
      break;
    case MML_SUSTAIN:
      curOffset++;
      AddUnknown(beginOffset, curOffset - beginOffset, L"Sustain Level");
      break;
    case MML_RELEASE:
      curOffset++;
      AddUnknown(beginOffset, curOffset - beginOffset, L"Release Rate");
      break;
    case MML_LOOP_START: {
      uint8_t loopCount = GetByte(curOffset++);
      callStack.push({ curOffset, loopCount });
      AddUnknown(beginOffset, curOffset - beginOffset, L"Loop Start");
      break;
    }
    case MML_VOLUME2: {
      uint8_t expression = GetByte(curOffset++);
      AddExpression(beginOffset, curOffset - beginOffset, expression);
      break;
    }
    case MML_PRINTVAR:
      curOffset++;
      AddUnknown(beginOffset, curOffset - beginOffset, L"Print Variable");
      break;
    case MML_MOD_DELAY:
      curOffset += 2;
      AddUnknown(beginOffset, curOffset - beginOffset, L"Modulation Delay");
      break;
    case MML_TEMPO: {
      uint16_t bpm = GetShortBE(curOffset);
      curOffset += 2;
      AddTempoBPM(beginOffset, curOffset - beginOffset, bpm);
      break;
    }
    case MML_SWEEP_PITCH:
      curOffset += 2;
      AddUnknown(beginOffset, curOffset - beginOffset, L"Sweep Pitch");
      break;
    case MML_FXSEND_A:
    case MML_FXSEND_B:
    case MML_FXSEND_C:
      curOffset++;
      AddUnknown(beginOffset, curOffset - beginOffset, L"FXSEND");
      break;
    case MML_DAMPER:
      curOffset++;
      AddUnknown(beginOffset, curOffset - beginOffset, L"Damper");
      break;
    case MML_LOOP_END:
      /* XXX: Not sure why this happens sometimes... */
      if (callStack.size() == 0) break;
      /* Jump only if we should loop more... */
      if (callStack.top().loopCount-- == 0)
        break;
      /* Fall through. */
    case MML_RET:
      AddGenericEvent(beginOffset, curOffset - beginOffset, L"Return", L"", CLR_LOOP);
      /* XXX: Not sure why this happens sometimes... */
      if (callStack.size() == 0) break;
      curOffset = callStack.top().retOffset;
      callStack.pop();
      break;

      /* Handled for the master track in RSARSeq. */
    case MML_ALLOC_TRACK:
      curOffset += 2;
      AddUnknown(beginOffset, curOffset - beginOffset, L"Allocate Track");
      break;
    case MML_FIN:
      AddEndOfTrack(beginOffset, curOffset - beginOffset);
      return false;

    case MML_EX_COMMAND: {
      uint8_t ex = GetByte(curOffset++);
      if ((ex & 0xF0) == 0xE0)
        curOffset += 2;
      else if ((ex & 0xF0) == 0x80 || (ex & 0xF0) == 0x90)
        curOffset += 3;
      else
        return false;
      AddUnknown(beginOffset, curOffset - beginOffset, L"Extended Command");
      break;
    }

    default:
      AddUnknown(beginOffset, curOffset - beginOffset);
      return false;
    }
  }
  return true;
}

bool RSARSeq::GetHeaderInfo() {
  SetPPQN(0x30);
  return true;
}

bool RSARSeq::GetTrackPointers() {
  uint32_t offset = dwOffset + masterTrackOffset;
  aTracks.push_back(new RSARTrack(this, offset, 0));

  /* Scan through the master track for the OPEN_TRACK commands. */
  uint32_t cmd;
  while ((cmd = GetByte(offset)) != MML_OPEN_TRACK) {
    if (cmd >= 0xB0 && cmd <= 0xDF)
      offset += 2; /* uint8_t */
    else
      break;
  }

  while (GetByte(offset) == MML_OPEN_TRACK) {
    AddHeader(offset, 5, L"Opening Track");
    uint8_t trackNo = GetByte(offset + 1);
    uint32_t trackOffs = GetWordBE(offset + 1) & 0x00FFFFFF;
    aTracks.push_back(new RSARTrack(this, dwOffset + trackOffs, 0));
    offset += 0x05;
  }

  return true;
}
