#pragma once

#include "AsciiShuichiSnesFormat.h"
#include "base/Types.h"
#include "SeqTrack.h"
#include "VGMSeq.h"

#include <array>
#include <map>
#include <string>

enum AsciiShuichiSnesSeqEventType {
  //start enum at 1 because if map[] look up fails, it returns 0, and we don't want that to get confused with a legit event
  EVENT_UNKNOWN0 = 1,
  EVENT_UNKNOWN1,
  EVENT_UNKNOWN2,
  EVENT_UNKNOWN3,
  EVENT_UNKNOWN4,
  EVENT_NOTE,
  EVENT_END,
  EVENT_INFINITE_LOOP_START,
  EVENT_INFINITE_LOOP_END,
  EVENT_LOOP_START,
  EVENT_LOOP_END,
  EVENT_LOOP_BREAK,
  EVENT_CALL,
  EVENT_RET,
  EVENT_PROGCHANGE,
  EVENT_RELEASE_ADSR,
  EVENT_TEMPO,
  EVENT_TRANSPOSE_ABS,
  EVENT_TRANSPOSE_REL,
  EVENT_TUNING,
  EVENT_PITCH_BEND_SLIDE,
  EVENT_DURATION_RATE,
  EVENT_VOLUME_AND_PAN,
  EVENT_VOLUME,
  EVENT_VOLUME_REL,
  EVENT_VOLUME_REL_TEMP,
  EVENT_PAN,
  EVENT_PAN_FADE,
  EVENT_VOLUME_FADE,
  EVENT_MASTER_VOLUME,
  EVENT_ECHO_PARAM,
  EVENT_ECHO_CHANNELS,
  EVENT_VIBRATO,
  EVENT_VIBRATO_OFF,
  EVENT_REST,
  EVENT_NOISE_ON,
  EVENT_NOISE_OFF,
  EVENT_MUTE_CHANNELS,
  EVENT_INSTRUMENT_VOLUME_PAN_TRANSPOSE,
  EVENT_DURATION_TICKS,
  EVENT_WRITE_TO_PORT,
};

class AsciiShuichiSnesSeq
    : public VGMSeq {
 public:
  AsciiShuichiSnesSeq
      (RawFile *file, u32 seqHeaderOffset, std::string newName = "ASCII Shuichi Ukai SNES Seq");

  bool parseHeader() override;
  bool parseTrackPointers() override;
  void resetVars() override;

  std::map<u8, AsciiShuichiSnesSeqEventType> EventMap;

  u8 tempo;

  [[nodiscard]] double getTempoInBPM() const;
  static double getTempoInBPM(u8 tempo);

 private:
  void loadEventMap();
};


class AsciiShuichiSnesTrack
    : public SeqTrack {
 public:
  AsciiShuichiSnesTrack(AsciiShuichiSnesSeq *parentFile, u32 offset = 0, u32 length = 0);
  void resetVars() override;
  bool readEvent() override;

  static void getVolumeBalance(u8 pan, double &volumeLeft, double &volumeRight);
  static s8 calcMidiPanValue(u8 pan);

 private:
  u8 spcVolume;
  u16 infiniteLoopPoint;
  s8 lastNoteKey;
  u8 rawNoteLength;
  u8 noteDuration;
  u8 noteDurationRate;
  bool slurDeferred;
  bool useNoteDurationRate;
  bool reachedRepeatBreakBefore;
  u8 repeatStartNestLevel;
  u8 repeatEndNestLevel;
  u8 callNestLevel;
  std::array<u16, 3> repeatStartAddressStack;
  std::array<u16, 3> repeatEndAddressStack;
  std::array<u8, 3> repeatCountStack;
  std::array<u16, 3> callStack;
};
