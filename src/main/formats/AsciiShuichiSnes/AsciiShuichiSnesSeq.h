#pragma once

#include <array>

#include "VGMSeq.h"
#include "SeqTrack.h"
#include "AsciiShuichiSnesFormat.h"

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
      (RawFile *file, uint32_t seqHeaderOffset, std::string newName = "ASCII Shuichi Ukai SNES Seq");

  bool parseHeader() override;
  bool parseTrackPointers() override;
  void resetVars() override;

  std::map<uint8_t, AsciiShuichiSnesSeqEventType> EventMap;

  uint8_t tempo;

  [[nodiscard]] double getTempoInBPM() const;
  static double getTempoInBPM(uint8_t tempo);

 private:
  void loadEventMap();
};


class AsciiShuichiSnesTrack
    : public SeqTrack {
 public:
  AsciiShuichiSnesTrack(AsciiShuichiSnesSeq *parentFile, uint32_t offset = 0, uint32_t length = 0);
  void resetVars() override;
  State readEvent() override;

  static void getVolumeBalance(uint8_t pan, double &volumeLeft, double &volumeRight);
  static int8_t calcMidiPanValue(uint8_t pan);

 private:
  uint8_t spcVolume;
  uint16_t infiniteLoopPoint;
  int8_t lastNoteKey;
  uint8_t rawNoteLength;
  uint8_t noteDuration;
  uint8_t noteDurationRate;
  bool slurDeferred;
  bool useNoteDurationRate;
  bool reachedRepeatBreakBefore;
  uint8_t repeatStartNestLevel;
  uint8_t repeatEndNestLevel;
  uint8_t callNestLevel;
  std::array<uint16_t, 3> repeatStartAddressStack;
  std::array<uint16_t, 3> repeatEndAddressStack;
  std::array<uint8_t, 3> repeatCountStack;
  std::array<uint16_t, 3> callStack;
};
