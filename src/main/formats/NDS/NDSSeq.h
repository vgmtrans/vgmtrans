#pragma once
#include <map>
#include <string>

#include "VGMSeq.h"
#include "SeqTrack.h"

class NDSSeq:
    public VGMSeq {
 public:
  NDSSeq(RawFile *file, uint32_t offset, uint32_t length = 0, std::string theName = "NDSSeq");

  bool parseHeader() override;
  bool parseTrackPointers() override;
  void resetVars() override;

  void registerTempoChange(uint32_t tick, double bpm, int trackIndex);
  [[nodiscard]] double tempoAtTick(uint32_t tick) const;

 private:
  struct TempoPoint {
    double bpm;
    int trackIndex;
  };

  std::map<uint32_t, TempoPoint> m_tempoMap;

};


class NDSTrack
    : public SeqTrack {
 public:
  NDSTrack(NDSSeq *parentFile, uint32_t offset = 0, uint32_t length = 0);
  void resetVars() override;
  bool readEvent() override;
  void addTime(uint32_t delta) override;

  uint32_t dur;
  bool noteWithDelta;

 private:
  enum class ModTarget : uint8_t {
    Pitch = 0,
    Volume = 1,
    Pan = 2,
    Unknown = 0xFF,
  };

  void resetModState();
  void renderModUntil(uint32_t endTick);
  void renderModPoint(uint32_t tick);
  void advanceModStateByMidiTicks(uint32_t midiTicks, uint32_t startTick);
  void advanceModStateOnePeriodicTick();
  void onNoteStart(uint32_t noteStartTick);
  static ModTarget toModTarget(uint8_t value);
  static std::string to_string(ModTarget target);
  void applySweepPitchForNote(uint32_t startTick, uint32_t duration);
  void emitPitchModRangeAt(uint32_t tick);
  void emitPitchVibratoParamsAt(uint32_t tick);
  uint32_t modDelayToMidiTicks(uint16_t delay, uint32_t atTick) const;

  uint8_t modDepth;
  uint8_t modSpeed;
  uint8_t modType;
  uint8_t modRange;
  uint16_t modDelay;

  uint32_t modLastRenderTick;
  uint16_t modPhaseCounter;
  uint16_t modDelayCounter;
  double modPeriodicRemainder;
  bool modRangeSent;

  int lastPitchModCc;
  int lastPitchVibratoRateCc;
  int lastExprCc;
  int lastPanCc;
  int16_t sweepPitch;
  int16_t basePitchBend;
  uint32_t pitchModEnableTick;
  double currentTempoBpm;
};
