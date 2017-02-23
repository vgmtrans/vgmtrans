
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

DECLARE_FORMAT(JaiSeqBMS);
DECLARE_FORMAT(JaiSeqAAF);
DECLARE_FORMAT(JaiSeqBAA);

/* Names from JASystem::TSeqParser from framework.map in The Wind Waker. */
enum MML {
  /* wait with u8 arg */
  MML_WAIT_8          = 0x80,
  /* wait with u16 arg */
  MML_WAIT_16         = 0x88,
  /* wait with variable-length arg */
  MML_WAIT_VAR        = 0xF0,

  /* Perf is hard to enum, but here goes: */
  MML_PERF_U8_NODUR    = 0x94,
  MML_PERF_U8_DUR_U8   = 0x96,
  MML_PERF_U8_DUR_U16  = 0x97,
  MML_PERF_S8_NODUR    = 0x98,
  MML_PERF_S8_DUR_U8   = 0x9A,
  MML_PERF_S8_DUR_U16  = 0x9B,
  MML_PERF_S16_NODUR   = 0x9C,
  MML_PERF_S16_DUR_U8  = 0x9E,
  MML_PERF_S16_DUR_U16 = 0x9F,

  MML_PARAM_SET_8     = 0xA4,
  MML_PARAM_SET_16    = 0xAC,

  MML_OPEN_TRACK      = 0xC1,
  /* open sibling track, seems unused */
  MML_OPEN_TRACK_BROS = 0xC2,
  MML_CALL            = 0xC3,
  MML_CALL_COND       = 0xC4,
  MML_RET             = 0xC5,
  MML_RET_COND        = 0xC6,
  MML_JUMP            = 0xC7,
  MML_JUMP_COND       = 0xC8,
  MML_TIME_BASE       = 0xFD,
  MML_TEMPO           = 0xFE,
  MML_FIN             = 0xFF,

  /* "Improved" JaiSeq from TP / SMG / SMG2 seems to use this instead */
  MML2_SET_PERF_8     = 0xB8,
  MML2_SET_PERF_16    = 0xB9,
  /* Set "articulation"? Used for setting timebase. */
  MML2_SET_ARTIC = 0xD8,
  MML2_TEMPO = 0xE0,
  MML2_SET_BANK       = 0xE2,
  MML2_SET_PROG       = 0xE3,
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
    else if (param == MML_BANK) {
      AddBankSelectNoItem(value);
      AddUnknown(beginOffset, curOffset - beginOffset, L"Bank Select");
    } else
      AddUnknown(beginOffset, curOffset - beginOffset);
  }

  enum PerfType {
    MML_VOLUME = 0,
    MML_PITCH = 1,
    MML_REVERB = 2,
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
      uint8_t midValue = (uint8_t)(value * 0x7F);
      if (duration == 0)
        AddPan(beginOffset, curOffset - beginOffset, midValue);
      else
        AddPanSlide(beginOffset, curOffset - beginOffset, duration, midValue);
    }
    else if (type == MML_PITCH) {
      /* MIDI pitch is between 0 and 7FFF */
      uint16_t midValue = (uint16_t)(value * 0x7FFF) * 4;
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

      case 0xF4: /* VibPitch */
        curOffset += 1;
        AddUnknown(beginOffset, curOffset - beginOffset);
        break;
      case 0xCB:
      case 0xE6: /* Vibrato */
      case 0xE7: /* SyncGPU */
      case 0xF9:
        curOffset += 2;
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

      case MML2_SET_BANK: {
        uint8_t bank = GetByte(curOffset++);
        AddBankSelectNoItem(bank);
        AddGenericEvent(beginOffset, curOffset - beginOffset, L"Bank Select", L"MML2_SET_BANK", CLR_PROGCHANGE);
        break;
      }
      case MML2_SET_PROG: {
        uint8_t prog = GetByte(curOffset++);
        AddProgramChange(beginOffset, curOffset - beginOffset, prog);
        break;
      }
      case MML2_SET_PERF_8: {
        uint8_t type = GetByte(curOffset++);
        int8_t value = (int8_t)GetByte(curOffset++);
        if (type == MML_VOLUME)
          SetPerf(beginOffset, type, value, 0);
        else
          SetPerf(beginOffset, type, value, 0);
        break;
      }
      case MML2_SET_PERF_16: {
        uint8_t type = GetByte(curOffset++);
        int16_t value = (int16_t)GetShortBE(curOffset);
        curOffset += 2;
        SetPerf(beginOffset, type, value, 0);
        break;
      }
      case MML2_SET_ARTIC: {
        uint8_t type = GetByte(curOffset++);
        if (type == 0x62) {
          uint16_t ppqn = GetShortBE(curOffset);
          curOffset += 2;
          parentSeq->SetPPQN(ppqn);
        }
        else {
          curOffset += 2;
        }
        break;
      }
      case MML2_TEMPO: {
        uint16_t bpm = GetShortBE(curOffset);
        curOffset += 2;
        AddTempoBPM(beginOffset, curOffset - beginOffset, bpm);
        break;
      }

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
    VGMSeq(JaiSeqBMSFormat::name, file, offset, length) {
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

static bool MatchMagic(RawFile *file, uint32_t offs, const char *magic) {
  char buf[16];
  assert(sizeof(buf) >= strlen(magic));
  file->GetBytes(offs, (uint32_t) strlen(magic), buf);
  return memcmp(buf, magic, strlen(magic)) == 0;
}

class JaiSeqSamp : public VGMSamp {
public:
  JaiSeqSamp(VGMSampColl *sampColl, uint32_t offset, RawFile *waFile_) :
    VGMSamp(sampColl, offset, 0x2C, 0, 0), waFile(waFile_) {
    Load();
  }

private:
  RawFile *waFile;
  uint32_t waOffset, waSize;

  uint32_t DspAddressToSamples(uint32_t dspAddr) {
    return (dspAddr / 9) * 16;
  }

  const uint16_t coef[16][2] = {
    { 0x0000, 0x0000 },
    { 0x0800, 0x0000 },
    { 0x0000, 0x0800 },
    { 0x0400, 0x0400 },
    { 0x1000, 0xF800 },
    { 0x0E00, 0xFA00 },
    { 0x0C00, 0xFC00 },
    { 0x1200, 0xF600 },
    { 0x1068, 0xF738 },
    { 0x12C0, 0xF704 },
    { 0x1400, 0xF400 },
    { 0x0800, 0xF800 },
    { 0x0400, 0xFC00 },
    { 0xFC00, 0x0400 },
    { 0xFC00, 0x0000 },
    { 0xF800, 0x0000 },
  };

  static int16_t Clamp16(int32_t sample) {
    if (sample > 0x7FFF) return  0x7FFF;
    if (sample < -0x8000) return -0x8000;
    return sample;
  }

  virtual void ConvertToStdWave(uint8_t *buf) override {
    int16_t hist1 = 0, hist2 = 0;

    uint32_t samplesDone = 0;
    uint32_t numSamples = DspAddressToSamples(waSize);

    uint32_t srcOffs = waOffset;
    uint32_t dstOffs = 0;

    /* Go over and convert each frame of 14 samples. */
    while (samplesDone < numSamples) {
      /* Each frame is 9 bytes -- a one-byte header, and then 8 bytes
       * of samples. Each sample is 4 bits, so we have 16 samples per frame. */
      uint8_t frameHeader = waFile->GetByte(srcOffs++);

      int32_t scale = 1 << (frameHeader >> 4);
      uint8_t coefIdx = (frameHeader & 0x0F);
      int16_t coef1 = (int16_t)coef[coefIdx][0], coef2 = (int16_t)coef[coefIdx][1];

      for (int i = 0; i < 16; i++) {
        /* Pull out the correct byte and nibble. */
        uint8_t byte = waFile->GetByte(srcOffs + (i >> 1));
        uint8_t unsignedNibble = (i & 1) ? (byte & 0x0F) : (byte >> 4);

        /* Sign extend. */
        int8_t signedNibble = ((int8_t)(unsignedNibble << 4)) >> 4;

        /* Decode. */
        int16_t sample = Clamp16((((signedNibble * scale) << 11) + (coef1*hist1 + coef2*hist2)) >> 11);
        ((int16_t *)buf)[dstOffs] = sample;
        dstOffs++;

        hist2 = hist1;
        hist1 = sample;

        samplesDone++;
        if (samplesDone >= numSamples)
          break;
      }

      srcOffs += 8;
    }
  }

  void Load() {
    /* Samples are always ADPCM data, as far as I can tell... */
    SetWaveType(WT_PCM16);
    SetBPS(16);

    /* Always mono too... */
    SetNumChannels(1);

    /* unknown */
    
    unityKey = GetByte(dwOffset + 0x02);
    SetRate(GetShortBE(dwOffset + 0x05) / 2);
    /* unknown */

    waOffset = GetWordBE(dwOffset + 0x08);
    waSize = GetWordBE(dwOffset + 0x0C);

    uint32_t nSamples = DspAddressToSamples(waSize);
    ulUncompressedSize = nSamples * 2;

    uint32_t loopFlag = GetWordBE(dwOffset + 0x10);
    uint32_t loopStart = GetWordBE(dwOffset + 0x14);
    uint32_t loopEnd = GetWordBE(dwOffset + 0x18);

    bool loop = loopFlag == 0xFFFFFFFF;
    SetLoopStatus(loop);
    if (loop) {
      SetLoopStartMeasure(LM_SAMPLES);
      SetLoopLengthMeasure(LM_SAMPLES);
      SetLoopOffset(loopStart);
      SetLoopLength(loopEnd - loopStart);
    }
  }
};

class JaiSeqSampCollWSYS : public VGMSampColl {
public:
  JaiSeqSampCollWSYS(RawFile *file, uint32_t offset, uint32_t length,
    std::wstring name = L"WSYS Sample Collection")
    : VGMSampColl(JaiSeqAAFFormat::name, file, offset, length, name) {}
private:

  virtual bool GetSampleInfo() override {
    if (!MatchMagic(rawfile, dwOffset, "WSYS"))
      return false;

    unLength = GetWordBE(dwOffset + 0x04);

    uint32_t winfBase = dwOffset + GetWordBE(dwOffset + 0x10);
    if (!MatchMagic(rawfile, winfBase, "WINF"))
      return false;

    /* First up, load up our AW files. */
    struct AwFile { RawFile *file; uint32_t offset; };
    std::vector<AwFile> awFiles;
    uint32_t awCount = GetWordBE(winfBase + 0x04);
    uint32_t awTableIdx = winfBase + 0x08;
    for (uint32_t i = 0; i < awCount; i++) {
      uint32_t awTableBase = dwOffset + GetWordBE(winfBase + 0x08);
      char awFilename[0x71] = {};
      GetBytes(awTableBase, 0x70, awFilename);

      wchar_t tempfn[PATH_MAX] = { 0 };
      mbstowcs(tempfn, awFilename, sizeof(awFilename));

      wchar_t *fullPath;
      fullPath = GetFileWithBase(rawfile->GetFullPath(), tempfn);
      RawFile *rawFile = new RawFile(fullPath);
      if (!rawFile->open(fullPath))
        return false;
      /* XXX: Leaks if above fails. */
      delete fullPath;

      AwFile file = { rawFile, awTableBase };
      awFiles.push_back(file);
    }

    uint32_t wbctBase = dwOffset + GetWordBE(dwOffset + 0x14);
    if (!MatchMagic(rawfile, wbctBase, "WBCT"))
      return false;

    uint32_t scneBase = dwOffset + GetWordBE(wbctBase + 0x0C);
    if (!MatchMagic(rawfile, scneBase, "SCNE"))
      return false;

    uint32_t c_dfBase = dwOffset + GetWordBE(scneBase + 0x0C);
    if (!MatchMagic(rawfile, c_dfBase, "C-DF"))
      return false;

    /* XXX: In the future, we should handle WSYS archives that reference
     * more than one AW file. That would require reading the C-DF section
     * to know which sample corresponds to which WA file. */

    uint32_t c_dfCount = GetWordBE(c_dfBase + 0x04);
    uint32_t c_dfTableIndex = c_dfBase + 0x08;
    for (uint32_t i = 0; i < c_dfCount; i++) {
      uint32_t awFileIdx = GetShortBE(c_dfTableIndex + 0x00);
      uint32_t sampIdx = GetShortBE(c_dfTableIndex + 0x02);

      AwFile awFile = awFiles[awFileIdx];
      uint32_t waveTableIdx = awFile.offset + 0x74 + (i * 0x04);
      uint32_t waveBase = dwOffset + GetWordBE(waveTableIdx);

      VGMSamp *samp = new JaiSeqSamp(this, waveBase, awFile.file);
      wchar_t name[32] = {};
      swprintf(name, sizeof(name) / sizeof(*name), L"Sample_%04d", i);
      samp->name = name;
      samples.push_back(samp);
      c_dfTableIndex += 0x04;
    }

    return true;
  }
};

class JaiSeqInstrBNK : public VGMInstr {
public:
  uint32_t osctBase;
  uint32_t envtBase;

  JaiSeqInstrBNK(VGMInstrSet *instrSet, uint32_t offset, uint32_t length, uint32_t theInstrNum, uint32_t osctBase_, uint32_t envtBase_, uint32_t theBank = 0)
    : VGMInstr(instrSet, offset, length, theBank, theInstrNum), osctBase(osctBase_), envtBase(envtBase_) {}

  virtual bool LoadInstr() override {
    char buf[4];
    GetBytes(dwOffset, sizeof(buf), buf);

    if (memcmp(buf, "Inst", sizeof(buf)) == 0) {
      /* "Normal" instrument */

      uint32_t osctCount = GetWordBE(osctBase + 0x08);

      uint32_t osciIndex = GetWordBE(dwOffset + 0x08);
      assert(osciIndex < osctCount);
      uint32_t envpOffs = GetWordBE(osctBase + 0x0C + (osciIndex * 0x1C) + 0x10);
      uint32_t envpBase = envtBase + 0x08 + envpOffs;
      uint16_t attack  = GetShortBE(envpBase + 0x00);
      uint16_t decay   = GetShortBE(envpBase + 0x02);
      uint16_t sustain = GetShortBE(envpBase + 0x04);
      uint16_t release = GetShortBE(envpBase + 0x06);

      uint32_t instIdx = dwOffset + 0x0C;
      if (GetWordBE(instIdx) == 0x02)
        instIdx += 0x08;
      else if (GetWordBE(instIdx) != 0)
        instIdx += 0x04;
      instIdx += 0x04;

      uint32_t keyRgnCount = GetWordBE(instIdx + 0x00);
      uint32_t regionTableIdx = instIdx + 0x04;
      uint8_t keyLow = 0;
      for (uint32_t i = 0; i < keyRgnCount; i++) {
        uint8_t keyHigh = GetByte(regionTableIdx);
        regionTableIdx += 0x04;

        uint32_t velRgnCount = GetWordBE(regionTableIdx);
        regionTableIdx += 0x04;

        uint8_t velLow = 0;
        for (uint32_t j = 0; j < velRgnCount; j++) {
          uint8_t velHigh = GetByte(regionTableIdx);
          uint32_t regStart = regionTableIdx;
          regionTableIdx += 0x04;

          uint32_t sampleNum;
          VGMRgn *rgn;

          regionTableIdx += 0x02; /* unknown */
          sampleNum = GetShortBE(regionTableIdx);
          regionTableIdx += 0x02;
          regionTableIdx += 0x04; /* unknown */
          regionTableIdx += 0x04; /* frequency multiplier */

          rgn = AddRgn(regStart, regionTableIdx - regStart, sampleNum, keyLow, keyHigh, velLow, velHigh);

          /* Fake ADSR for now. */
          rgn->attack_time = 0.0;
          rgn->release_time = 0.2;

          velLow = velHigh + 1;
        }

        keyLow = keyHigh + 1;
      }
    }
    else if (memcmp(buf, "Perc", sizeof(buf)) == 0) {
      uint32_t numSamples = GetWordBE(dwOffset + 0x04);
      uint32_t sampTableIdx = dwOffset + 0x08;
      for (uint32_t i = 0; i < numSamples; i++) {
        uint32_t pmapOffs = GetWordBE(sampTableIdx);
        sampTableIdx += 0x04;
        if (pmapOffs == 0)
          continue;
        uint32_t pmapBase = parInstrSet->dwOffset + pmapOffs;
        if (!MatchMagic(vgmfile->rawfile, pmapBase, "Pmap"))
          return false;
        pmapBase += 0x04;
        pmapBase += 0x04; /* unknown */
        pmapBase += 0x04; /* frequency multiplier */
        uint8_t pan = GetByte(pmapBase);
        pmapBase += 0x10; /* unknown */
        uint32_t sampleNum = GetWordBE(pmapBase) & 0xFFFF;
        pmapBase += 0x04;

        VGMRgn *rgn = AddRgn(sampTableIdx, 0x04, sampleNum, i, i);
        JaiSeqSamp *samp = (JaiSeqSamp *)parInstrSet->sampColl->samples[sampleNum];
        rgn->SetUnityKey(i);
        rgn->SetPan(pan);
      }
    }

    return true;
  }
};

class JaiSeqInstrSetBNK : public VGMInstrSet {
public:
  uint32_t index;
  uint32_t osctBase;
  uint32_t envtBase;

  JaiSeqInstrSetBNK(RawFile *file,
    uint32_t offset,
    uint32_t length,
    VGMSampColl *sampColl,
    std::wstring name = L"JaiSeq Instrument Bank") : VGMInstrSet(JaiSeqBAAFormat::name, file, offset, length, name, sampColl) {}

private:

  uint32_t FindChunk(char *magic) {
    uint32_t searchBase = dwOffset + 0x20;
    while (true) {
      if (MatchMagic(rawfile, searchBase, magic))
        return searchBase;
      searchBase += GetWordBE(searchBase + 0x04) + 8;
      /* Align to next 4 */
      searchBase = (searchBase + 0x03) & ~0x03;
    }
    return 0;
  }

  virtual bool GetInstrPointers() override {
    if (!MatchMagic(rawfile, dwOffset, "IBNK"))
      return false;

    unLength = GetWordBE(dwOffset + 0x04);
    index = GetWordBE(dwOffset + 0x08);

    wchar_t buf[64] = {};
    swprintf(buf, sizeof(buf) / sizeof(*buf), L"Bank_%02d", index);
    name = buf;

    osctBase = FindChunk("OSCT");
    envtBase = FindChunk("ENVT");

    /* We should have found LIST */
    uint32_t listBase = FindChunk("LIST") + 0x08;
    uint32_t nInstr = GetWordBE(listBase);
    uint32_t listIdx = listBase + 0x04;
    for (uint32_t i = 0; i < nInstr; i++) {
      uint32_t instrOffs = dwOffset + GetWordBE(listIdx);
      aInstrs.push_back(new JaiSeqInstrBNK(this, instrOffs, 0, i, osctBase, envtBase));
      listIdx += 0x04;
    }

    return true;
  }
};

/* Handle the BAA (Binary Audio Archive?) format from Twilight Princess,
 * Super Mario Galaxy, and Super Mario Galaxy 2. */
static bool LoadBAA(RawFile *file) {
  if (!MatchMagic(file, 0, "AA_<"))
    return false;

  uint32_t offset = 0x04;

  std::map<uint32_t, VGMSampColl *> wsys;
  std::map<uint32_t, VGMInstrSet *> bnk;

  while (true) {
    char buf[4];
    file->GetBytes(offset, sizeof(buf), buf);
    offset += sizeof(buf);

    /* End of archive. */
    if (memcmp(buf, ">_AA", sizeof(buf)) == 0) {
      break;
    }
    else if (memcmp(buf, "bst ", sizeof(buf)) == 0) {
      offset += 0x04; /* start */
      offset += 0x04; /* length */
    }
    else if (memcmp(buf, "bstn", sizeof(buf)) == 0) {
      offset += 0x04; /* start */
      offset += 0x04; /* length */
    }
    else if (memcmp(buf, "bsc ", sizeof(buf)) == 0) {
      offset += 0x04; /* start */
      offset += 0x04; /* length */
    }
    else if (memcmp(buf, "ws  ", sizeof(buf)) == 0) {
      uint32_t wsysIdx = file->GetWordBE(offset);
      offset += 0x04;
      uint32_t start = file->GetWordBE(offset);
      offset += 0x04;
      offset += 0x04; /* flag */
      VGMSampColl *sampColl = new JaiSeqSampCollWSYS(file, start, 0);
      if (!sampColl->LoadVGMFile())
        continue;
      wsys[wsysIdx] = sampColl;
    }
    else if (memcmp(buf, "bnk ", sizeof(buf)) == 0) {
      uint32_t wsysIdx = file->GetWordBE(offset);
      offset += 0x04;
      uint32_t start = file->GetWordBE(offset);
      offset += 0x04;
      VGMSampColl *sampColl = wsys[wsysIdx];
      if (sampColl == nullptr)
        continue;
      JaiSeqInstrSetBNK *instrSet = new JaiSeqInstrSetBNK(file, start, 0, sampColl);
      if (!instrSet->LoadVGMFile())
        continue;
      bnk[instrSet->index] = instrSet;

      VGMColl *coll = new VGMColl(*instrSet->GetName());
      coll->AddInstrSet(instrSet);
      coll->AddSampColl(sampColl);
      coll->Load();
    }
    else {
      return false;
    }
  }

  return true;
}

static double FrequencyRatioToCents(double freqRatio) {
  return round(1000000 * 1200 * log2(freqRatio)) / 1000000.0;
}

class JaiSeqInstrAAF : public VGMInstr {
public:
  JaiSeqInstrAAF(VGMInstrSet *instrSet, uint32_t offset, uint32_t length, uint32_t theInstrNum, uint32_t theBank = 0)
    : VGMInstr(instrSet, offset, length, theBank, theInstrNum) {}

private:
  virtual bool LoadInstr() override {
    if (MatchMagic(vgmfile->rawfile, dwOffset, "INST")) {
      uint32_t keyRgnCount = GetWordBE(dwOffset + 0x28);
      uint32_t keyRgnIdx = dwOffset + 0x2C;
      uint8_t keyLow = 0;
      for (uint32_t i = 0; i < keyRgnCount; i++) {
        uint32_t keyRgnBase = parInstrSet->dwOffset + GetWordBE(keyRgnIdx);
        keyRgnIdx += 0x04;

        uint8_t keyHigh = GetByte(keyRgnBase);

        uint32_t velRgnCount = GetWordBE(keyRgnBase + 0x04);
        uint32_t velRgnIdx = keyRgnBase + 0x08;
        uint8_t velLow = 0;
        for (uint32_t j = 0; j < velRgnCount; j++) {
          uint32_t velRgnBase = parInstrSet->dwOffset + GetWordBE(velRgnIdx);
          uint8_t velHigh = GetByte(velRgnBase);

          /* 0x04 is some sort of bank identifier? Doesn't seem to match the WSYS ID... */
          uint32_t sampleNum = GetShortBE(velRgnBase + 0x06);
          /* 0x08 = unknown float */
          uint32_t freqMultBits = GetWordBE(velRgnBase + 0x0C);
          float freqMult = *((float *)&freqMultBits);

          VGMRgn *rgn = AddRgn(keyRgnBase, 0, sampleNum, keyLow, keyHigh, velLow, velHigh);
          rgn->SetFineTune(FrequencyRatioToCents(freqMult));
          velLow = velHigh + 1;
          velRgnIdx += 0x04;
        }

        keyLow = keyHigh + 1;
      }
      return true;
    } else if (MatchMagic(vgmfile->rawfile, dwOffset, "PER2")) {
      uint32_t keyRgnIdx = dwOffset + 0x88;
      for (uint32_t i = 0; i < 100; i++) {
        uint32_t keyRgnOffs = GetWordBE(keyRgnIdx);
        keyRgnIdx += 0x04;
        if (keyRgnOffs == 0)
          continue;

        uint32_t keyRgnBase = parInstrSet->dwOffset + keyRgnOffs;
        uint32_t keyRgnFreqMultBits = GetWordBE(keyRgnBase + 0x04);
        float keyRgnFreqMult = *((float *)&keyRgnFreqMultBits);

        uint32_t velRgnCount = GetWordBE(keyRgnBase + 0x10);
        uint32_t velRgnIdx = keyRgnBase + 0x14;
        uint8_t velLow = 0;
        for (uint32_t j = 0; j < velRgnCount; j++) {
          uint32_t velRgnBase = parInstrSet->dwOffset + GetWordBE(velRgnIdx);
          uint8_t velHigh = GetByte(velRgnBase);

          /* 0x04 is some sort of bank identifier? Doesn't seem to match the WSYS ID... */
          uint32_t sampleNum = GetShortBE(velRgnBase + 0x06);
          /* 0x08 = unknown float */
          uint32_t velRgnFreqMultBits = GetWordBE(velRgnBase + 0x0C);
          float velRgnFreqMult = *((float *)&velRgnFreqMultBits);

          VGMRgn *rgn = AddRgn(keyRgnBase, 0, sampleNum, i, i, velLow, velHigh);
          rgn->SetFineTune(FrequencyRatioToCents(velRgnFreqMult * keyRgnFreqMult));
          rgn->SetUnityKey(i);
          velLow = velHigh + 1;
          velRgnIdx += 0x04;
        }
      }
      return true;
    }
    return false;
  }
};

class JaiSeqInstrSetAAF : public VGMInstrSet {
public:
  uint32_t wsysId;
  uint32_t bnkId;

  JaiSeqInstrSetAAF(RawFile *file,
    uint32_t offset,
    uint32_t length,
    uint32_t wsysId_,
    VGMSampColl *sampColl = nullptr,
    std::wstring name = L"JaiSeq AAF Instrument Bank") :
      VGMInstrSet(JaiSeqAAFFormat::name, file, offset, length, name, sampColl), wsysId(wsysId_) {}

private:
  virtual bool GetInstrPointers() override {
    if (!MatchMagic(rawfile, dwOffset, "IBNK"))
      return false;

    unLength = GetWordBE(dwOffset + 0x04);
    bnkId = GetWordBE(dwOffset + 0x08);

    wchar_t buf[64] = {};
    swprintf(buf, sizeof(buf) / sizeof(*buf), L"Bank_%04x", bnkId);
    name = buf;

    uint32_t bankOffs = dwOffset + 0x20;
    if (!MatchMagic(rawfile, bankOffs, "BANK"))
      return false;

    uint32_t listIdx = bankOffs + 0x04;
    for (uint32_t instrNum = 0; instrNum < 245; instrNum++) {
      uint32_t instrOffs = GetWordBE(listIdx);
      listIdx += 0x04;
      if (instrOffs == 0x00)
        continue;
      uint32_t instrBase = dwOffset + instrOffs;
      aInstrs.push_back(new JaiSeqInstrAAF(this, instrBase, 0, instrNum & 0x7F));
    }

    return true;
  }

};

/* Handle the AAF (Audio Archive File?) format from Wind Waker. */
static bool LoadAAF(RawFile *file) {
  uint32_t offset = 0x00;

  std::vector<VGMSampColl *> wsys;
  std::map<uint32_t, VGMInstrSet *> bnk;

  while (true) {
    uint32_t chunkType = file->GetWordBE(offset);
    offset += 0x04;

    switch (chunkType) {
    case 0x00:
      /* End of archive. */
      goto end;
    case 0x01:
    case 0x05:
    case 0x06:
    case 0x07:
      while (true) {
        uint32_t start = file->GetWordBE(offset);
        offset += 0x04;
        if (start == 0x00)
          break;
        offset += 0x04; /* size */
      }
      break;
    case 0x02: {
      /* bnk */
      while (true) {
        uint32_t start = file->GetWordBE(offset);
        offset += 0x04;
        if (start == 0x00)
          break;
        offset += 0x04; /* size */
        uint32_t wsysId = file->GetWordBE(offset);
        offset += 0x04;
        JaiSeqInstrSetAAF *instrSet = new JaiSeqInstrSetAAF(file, start, 0, wsysId);
        instrSet->LoadVGMFile();
        bnk[instrSet->bnkId] = instrSet;
      }
      break;
    }
    case 0x03: {
      /* wsys */
      while (true) {
        uint32_t start = file->GetWordBE(offset);
        offset += 0x04;
        if (start == 0x00)
          break;
        offset += 0x04; /* size */
        offset += 0x04; /* flag */
        JaiSeqSampCollWSYS *sampColl = new JaiSeqSampCollWSYS(file, start, 0);
        sampColl->LoadVGMFile();
        wsys.push_back(sampColl);
      }
      break;
    }
    }
  }

end:

  for (auto const &pair : bnk) {
    VGMInstrSet *instrSet = pair.second;

    VGMSampColl *sampColl = wsys[((JaiSeqInstrSetAAF *)instrSet)->wsysId];
    VGMColl *coll = new VGMColl(*instrSet->GetName());
    coll->AddInstrSet(instrSet);
    coll->AddSampColl(sampColl);
    coll->Load();
  }

  return true;
}

void JaiSeqBMSScanner::Scan(RawFile *file, void *info) {
  JaiSeqSeq *seq = new JaiSeqSeq(file, 0, 0); seq->Load();
}

void JaiSeqAAFScanner::Scan(RawFile *file, void *info) {
  LoadAAF(file);
}

void JaiSeqBAAScanner::Scan(RawFile *file, void *info) {
  LoadBAA(file);
}