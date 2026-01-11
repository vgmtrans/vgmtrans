/**
 * VGMTrans (c) - 2002-2021
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#pragma once
#include "VGMItem.h"
#include "Loop.h"
#include <cstddef>
#include <filesystem>
#include <vector>

class VGMSampColl;

enum class BPS : int {
  PCM8 = 8,
  PCM16 = 16
};

class VGMSamp : public VGMItem {
public:
  VGMSamp(VGMSampColl *sampColl, uint32_t offset = 0, uint32_t length = 0, uint32_t dataOffset = 0,
          uint32_t dataLength = 0, uint8_t channels = 1, BPS bps = BPS::PCM16, uint32_t rate = 0,
          std::string name = "Sample");
  ~VGMSamp() override = default;

  virtual double compressionRatio() const;  // ratio of space conserved.  should generally be > 1
  std::vector<uint8_t> toPcm(Signedness targetSignedness,
                             Endianness targetEndianness,
                             BPS targetBps);

  inline void setBPS(BPS theBps) { m_bps = theBps; }
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
  inline BPS bps() const { return m_bps; }
  inline int bpsInt() const { return static_cast<int>(m_bps); }
  inline int bytesPerSample() const { return bpsInt() / 8; }
  uint32_t uncompressedSize() const;

  bool onSaveAsWav();
  bool saveAsWav(const std::filesystem::path &filepath);

public:
  uint32_t dataOff;  // offset of original sample data
  uint32_t dataLength;
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
  BPS m_bps;      // bits per sample
  double m_attenDb {0};
  bool m_reverse = false;
  Endianness m_endianness = Endianness::Little;
  Signedness m_signedness = Signedness::Signed;

protected:
  virtual std::vector<uint8_t> decodeToNativePcm();
};


class EmptySamp : public VGMSamp {
public:
  EmptySamp(VGMSampColl* sampColl): VGMSamp(sampColl, 0, 0, 0, 16, 1, BPS::PCM16) {}
protected:
  std::vector<uint8_t> decodeToNativePcm() override {
    return std::vector<uint8_t>(dataLength, 0);
  }
};
