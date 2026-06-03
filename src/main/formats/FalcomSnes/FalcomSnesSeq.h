#pragma once

#include "base/Types.h"
#include "FalcomSnesFormat.h"
#include "SeqEvent.h"
#include "SeqTrack.h"
#include "VGMSeq.h"

#include <map>
#include <string>
#include <vector>

enum FalcomSnesSeqEventType {
  //start enum at 1 because if map[] look up fails, it returns 0, and we don't want that to get confused with a legit event
  EVENT_UNKNOWN0 = 1,
  EVENT_UNKNOWN1,
  EVENT_UNKNOWN2,
  EVENT_UNKNOWN3,
  EVENT_UNKNOWN4,
  EVENT_NOP1,
  EVENT_NOP3,
  EVENT_NOTE,
  EVENT_OCTAVE,
  EVENT_TEMPO,
  EVENT_PROGCHANGE,
  EVENT_VIBRATO,
  EVENT_VIBRATO_ON_OFF,
  EVENT_QUANTIZE,
  EVENT_VOLUME,
  EVENT_VOLUME_DEC,
  EVENT_VOLUME_INC,
  EVENT_PAN,
  EVENT_PAN_DEC,
  EVENT_PAN_INC,
  EVENT_PAN_LFO,
  EVENT_PAN_LFO_ON_OFF,
  EVENT_TUNING,
  EVENT_LOOP_START,
  EVENT_LOOP_BREAK,
  EVENT_LOOP_END,
  EVENT_PITCH_ENVELOPE,
  EVENT_PITCH_ENVELOPE_ON_OFF,
  EVENT_ADSR,
  EVENT_GAIN,
  EVENT_NOISE_FREQ,
  EVENT_PITCHMOD,
  EVENT_ECHO,
  EVENT_ECHO_PARAM,
  EVENT_ECHO_VOLUME_ON_OFF,
  EVENT_ECHO_VOLUME,
  EVENT_ECHO_FIR_OVERWRITE,
  EVENT_GOTO,
};

class FalcomSnesSeq
    : public VGMSeq {
 public:
  FalcomSnesSeq(RawFile *file,
                    FalcomSnesVersion ver,
                    u32 seqdata_offset,
                    std::string name = "Falcom SNES Seq");
  ~FalcomSnesSeq() override;

  bool parseHeader() override;
  bool parseTrackPointers() override;
  void resetVars() override;

  static const u8 VOLUME_TABLE[129];

  FalcomSnesVersion version;
  std::map<u8, FalcomSnesSeqEventType> EventMap;

  std::vector<u8> NoteDurTable;

  std::map<u16, u8> repeatCountMap;
  std::map<u8, u16> instrADSRHints;

  static double getTempoInBPM(u8 tempo);

 private:
  void loadEventMap();
};


class FalcomSnesTrack
    : public SeqTrack {
 public:
  FalcomSnesTrack(FalcomSnesSeq *parentFile, u32 offset = 0, u32 length = 0);
  void resetVars() override;
  bool readEvent() override;

  static s8 calculatePanValue(u8 pan, double &volumeScale);

 private:
  s8 prevNoteKey;
  bool prevNoteSlurred;
  u8 spcNoteQuantize;
  u8 spcInstr;
  u16 spcADSR;
  u8 spcVolume;
  u8 spcPan;
};
