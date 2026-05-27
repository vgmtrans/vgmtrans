/**
 * VGMTrans (c) - 2002-2021
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#pragma once

#include "base/binary.h"
#include "base/types.h"
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
  VGMSamp(VGMSampColl *sampColl, u32 offset = 0, u32 length = 0, u32 dataOffset = 0,
          u32 dataLength = 0, u8 channels = 1, BPS bps = BPS::PCM16, u32 rate = 0,
          std::string name = "Sample");
  ~VGMSamp() override = default;

  virtual double compressionRatio() const;  // ratio of space conserved.  should generally be > 1
  std::vector<u8> toPcm(Signedness targetSignedness,
                             Endianness targetEndianness,
                             BPS targetBps);

  inline void setBPS(BPS theBps) { m_bps = theBps; }
  inline void setRate(u32 theRate) { rate = theRate; }
  inline void setNumChannels(u8 nChannels) { channels = nChannels; }
  inline void setDataOffset(u32 theDataOff) { dataOff = theDataOff; }
  inline void setDataLength(u32 theDataLength) { dataLength = theDataLength; }
  inline int loopStatus() const { return loop.loopStatus; }
  inline void setLoopStatus(int loopStat) { loop.loopStatus = loopStat; }
  inline void setLoopOffset(u32 loopStart) { loop.loopStart = loopStart; }
  inline int loopLength() const { return loop.loopLength; }
  inline void setLoopLength(u32 theLoopLength) { loop.loopLength = theLoopLength; }
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
  inline int bitsPerSample() const { return static_cast<int>(m_bps); }
  inline int bytesPerSample() const { return bitsPerSample() / 8; }
  u32 uncompressedSize() const;

  bool onSaveAsWav();
  bool saveAsWav(const std::filesystem::path &filepath);

public:
  u32 dataOff;  // offset of original sample data
  u32 dataLength;
  u32 rate;     // sample rate in herz (samples per second)
  u8 channels;  // mono or stereo?
  u32 ulUncompressedSize{0};

  bool bPSXLoopInfoPrioritizing = false;
  Loop loop;

  /* FIXME: these placeholders value are not so clean... */
  s8 unityKey {-1};
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
  virtual std::vector<u8> decodeToNativePcm();
};


class EmptySamp : public VGMSamp {
public:
  EmptySamp(VGMSampColl* sampColl): VGMSamp(sampColl, 0, 0, 0, 16, 1, BPS::PCM16) {}
protected:
  std::vector<u8> decodeToNativePcm() override {
    return std::vector<u8>(dataLength, 0);
  }
};
