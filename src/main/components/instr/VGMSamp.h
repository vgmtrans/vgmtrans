/**
 * VGMTrans (c) - 2002-2021
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#pragma once
#include "VGMItem.h"
#include "Loop.h"
#include <cstring>

class VGMSampColl;

enum WAVE_TYPE { WT_UNDEFINED, WT_PCM8, WT_PCM16 };

class VGMSamp : public VGMItem {
public:
  VGMSamp(VGMSampColl *sampColl, uint32_t offset = 0, uint32_t length = 0, uint32_t dataOffset = 0,
          uint32_t dataLength = 0, uint8_t channels = 1, uint16_t bps = 16, uint32_t rate = 0,
          std::string name = "Sample");
  ~VGMSamp() override = default;

  virtual double compressionRatio();  // ratio of space conserved.  should generally be > 1
  // used to calculate both uncompressed sample size and loopOff after conversion
  virtual void convertToStdWave(uint8_t *buf);

  inline void setWaveType(WAVE_TYPE type) { waveType = type; }
  inline void setBPS(uint16_t theBPS) { bps = theBPS; }
  inline void setRate(uint32_t theRate) { rate = theRate; }
  inline void setNumChannels(uint8_t nChannels) { channels = nChannels; }
  inline void setDataOffset(uint32_t theDataOff) { dataOff = theDataOff; }
  inline void setDataLength(uint32_t theDataLength) { dataLength = theDataLength; }
  inline int loopStatus() const { return loop.loopStatus; }
  inline void setLoopStatus(int loopStat) { loop.loopStatus = loopStat; }
  inline void setLoopOffset(uint32_t loopStart) { loop.loopStart = loopStart; }
  inline int loopLength() const { return loop.loopLength; }
  inline void setLoopLength(uint32_t theLoopLength) { loop.loopLength = theLoopLength; }
  inline void setLoopStartMeasure(LoopMeasure measure) { loop.loopStartMeasure = measure; }
  inline void setLoopLengthMeasure(LoopMeasure measure) { loop.loopLengthMeasure = measure; }
  inline double attenDb() const { return m_attenDb; }
  void setVolume(double volume);
  inline void setAttenuation(double decibels) { m_attenDb = decibels;}
  inline bool reverse() { return m_reverse; }
  inline void setReverse(bool reverse) { m_reverse = reverse; }
  inline Endianness endianness() const { return m_endianness; }
  inline void setEndianness(Endianness e) { m_endianness = e; }
  inline Signedness signedness() const { return m_signedness; }
  inline void setSignedness(Signedness s) { m_signedness = s; }

  bool onSaveAsWav();
  bool saveAsWav(const std::string &filepath);

public:
  WAVE_TYPE waveType = WT_UNDEFINED;
  uint32_t dataOff;  // offset of original sample data
  uint32_t dataLength;
  uint16_t bps;      // bits per sample
  uint32_t rate;     // sample rate in herz (samples per second)
  uint8_t channels;  // mono or stereo?
  uint32_t ulUncompressedSize{0};

  bool bPSXLoopInfoPrioritizing = false;
  Loop loop;

  /* FIXME: these placeholders value are not so clean... */
  int8_t unityKey {-1};
  short fineTune {0};

  long pan{0};

  VGMSampColl *parSampColl;

private:
  double m_attenDb {0};
  bool m_reverse = false;
  Endianness m_endianness = Endianness::Little;
  Signedness m_signedness = Signedness::Signed;

};


class EmptySamp : public VGMSamp {
public:
  EmptySamp(VGMSampColl* sampColl): VGMSamp(sampColl, 0, 0, 0, 16) {}
  void convertToStdWave(uint8_t *buf) override {
    memset(buf, 0, 16);
  }
};