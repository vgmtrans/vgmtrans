/**
 * VGMTrans (c) - 2002-2021
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#pragma once

#include "common.h"
#include "RiffFile.h"
#include <filesystem>

struct Loop;
class VGMSamp;

/*
   Articulation connection graph definitions
*/

/* Generic Sources */
#define CONN_SRC_NONE 0x0000
#define CONN_SRC_LFO 0x0001
#define CONN_SRC_KEYONVELOCITY 0x0002
#define CONN_SRC_KEYNUMBER 0x0003
#define CONN_SRC_EG1 0x0004
#define CONN_SRC_EG2 0x0005
#define CONN_SRC_PITCHWHEEL 0x0006

/* Midi Controllers 0-127 */
#define CONN_SRC_CC1 0x0081
#define CONN_SRC_CC7 0x0087
#define CONN_SRC_CC10 0x008a
#define CONN_SRC_CC11 0x008b

/* Registered Parameter Numbers */
#define CONN_SRC_RPN0 0x0100
#define CONN_SRC_RPN1 0x0101
#define CONN_SRC_RPN2 0x0102

/* Generic Destinations */
#define CONN_DST_NONE 0x0000
#define CONN_DST_ATTENUATION 0x0001
#define CONN_DST_RESERVED 0x0002
#define CONN_DST_PITCH 0x0003
#define CONN_DST_PAN 0x0004
#define CONN_DST_REVERB_SEND 0x0005
#define CONN_DST_CHORUS_SEND 0x0006

/* Filter Destinations */
#define CONN_DST_FILTER_CUTOFF 0x0100
#define CONN_DST_FILTER_Q 0x0101

/* LFO Destinations */
#define CONN_DST_LFO_FREQUENCY 0x0104
#define CONN_DST_LFO_STARTDELAY 0x0105

/* EG1 Destinations */
#define CONN_DST_EG1_ATTACKTIME 0x0206
#define CONN_DST_EG1_DECAYTIME 0x0207
#define CONN_DST_EG1_RESERVED 0x0208
#define CONN_DST_EG1_RELEASETIME 0x0209
#define CONN_DST_EG1_SUSTAINLEVEL 0x020a
#define CONN_DST_EG1_HOLDTIME      0x020c


/* EG2 Destinations */
#define CONN_DST_EG2_ATTACKTIME 0x030a
#define CONN_DST_EG2_DECAYTIME 0x030b
#define CONN_DST_EG2_RESERVED 0x030c
#define CONN_DST_EG2_RELEASETIME 0x030d
#define CONN_DST_EG2_SUSTAINLEVEL 0x030e

/* DLS2 EG DESTINATIONS */
#define CONN_DST_EG1_DELAYTIME 0x020B
#define CONN_DST_EG1_SHUTDOWNTIME 0x020D

#define CONN_DST_EG2_DELAYTIME 0x030F
#define CONN_DST_EG2_HOLDTIME 0x0310



#define CONN_TRN_NONE 0x0000
#define CONN_TRN_CONCAVE 0x0001

#define F_INSTRUMENT_DRUMS 0x80000000

#define COLH_SIZE (4 + 8)
#define INSH_SIZE (12 + 8)
#define RGNH_SIZE (14 + 8)  //(12+8)
#define WLNK_SIZE (12 + 8)
#define LIST_HDR_SIZE 12

class DLSInstr;
class DLSRgn;
class DLSArt;
class DLSWsmp;
class DLSWave;

class DLSFile : public RiffFile {
public:
  DLSFile(std::string dls_name = "Instrument Set");

  DLSInstr *addInstr(unsigned long bank, unsigned long instrNum);
  DLSInstr *addInstr(unsigned long bank, unsigned long instrNum, std::string Name);
  DLSWave *addWave(uint16_t formatTag, uint16_t channels, int samplesPerSec, int aveBytesPerSec,
                   uint16_t blockAlign, uint16_t bitsPerSample, uint32_t waveDataSize,
                   uint8_t *waveData, std::string name = "Unnamed Wave");

  std::vector<DLSInstr *> instruments();
  std::vector<DLSWave *> waves();

  uint32_t size() override;

  int writeDLSToBuffer(std::vector<uint8_t> &buf);
  bool saveDLSFile(const std::filesystem::path &filepath);

private:
  std::vector<std::unique_ptr<DLSInstr>> m_instrs;
  std::vector<std::unique_ptr<DLSWave>> m_waves;
};

class DLSInstr {
public:
  DLSInstr() = default;
  DLSInstr(uint32_t bank, uint32_t instrument);
  DLSInstr(uint32_t bank, uint32_t instrument, std::string instrName);

  DLSRgn *addRgn();

  uint32_t size() const;
  void write(std::vector<uint8_t> &buf);

private:
  uint32_t ulBank;
  uint32_t ulInstrument;

  std::vector<std::unique_ptr<DLSRgn>> m_regions;
  std::string m_name;
};

class DLSRgn {
public:
  DLSRgn() = default;
  DLSRgn(uint16_t keyLow, uint16_t keyHigh, uint16_t velLow, uint16_t velHigh)
      : usKeyLow(keyLow), usKeyHigh(keyHigh), usVelLow(velLow), usVelHigh(velHigh),
        fusOptions(0), usPhaseGroup(0), channel(0), tableIndex(0) {}

  DLSArt *addArt();
  DLSWsmp *addWsmp();
  void setRanges(uint16_t keyLow = 0, uint16_t keyHigh = 0x7F, uint16_t velLow = 0,
                 uint16_t velHigh = 0x7F);
  void setWaveLinkInfo(uint16_t options, uint16_t phaseGroup, uint32_t theChannel,
                       uint32_t theTableIndex);

  uint32_t size() const;
  void write(std::vector<uint8_t> &buf) const;

private:
  uint16_t usKeyLow;
  uint16_t usKeyHigh;
  uint16_t usVelLow;
  uint16_t usVelHigh;

  uint16_t fusOptions;
  uint16_t usPhaseGroup;
  uint32_t channel;
  uint32_t tableIndex;

  std::unique_ptr<DLSWsmp> m_wsmp;
  std::unique_ptr<DLSArt> m_art;
};

class ConnectionBlock {
public:
  ConnectionBlock(uint16_t source, uint16_t control, uint16_t destination, uint16_t transform,
                  int32_t scale)
      : usSource(source), usControl(control), usDestination(destination), usTransform(transform),
        lScale(scale) {}
  ~ConnectionBlock() {}

  static uint32_t size() { return 12; }
  void write(std::vector<uint8_t> &buf) const;

private:
  uint16_t usSource;
  uint16_t usControl;
  uint16_t usDestination;
  uint16_t usTransform;
  int32_t lScale;
};

class DLSArt {
public:
  DLSArt() = default;

  void addADSR(long attack_time, uint16_t atk_transform, long hold_time, long decay_time,
               long sustain_lev, long release_time, uint16_t rls_transform);
  void addPan(long pan);
  void addVibrato(int32_t depth, int32_t frequency, int32_t delay);
  void addConnectionBlock(uint16_t source, uint16_t control, uint16_t destination,
                          uint16_t transform, int32_t scale);

  uint32_t GetSize() const;
  void Write(std::vector<uint8_t> &buf) const;

private:
  std::vector<std::unique_ptr<ConnectionBlock>> m_blocks;
};

class DLSWsmp {
public:
  DLSWsmp() = default;

  void setLoopInfo(Loop &loop, VGMSamp *samp);
  void setPitchInfo(uint16_t unityNote, int16_t fineTune, int32_t attenuation);

  [[nodiscard]] uint32_t size() const;
  void write(std::vector<uint8_t> &buf) const;

private:
  uint16_t usUnityNote;
  int16_t sFineTune;
  int32_t lAttenuation;
  int8_t cSampleLoops;

  uint32_t ulLoopType;
  uint32_t ulLoopStart;
  uint32_t ulLoopLength;
};

class DLSWave {
public:
  DLSWave(uint16_t formatTag, uint16_t channels, uint32_t samplesPerSec, uint32_t aveBytesPerSec,
          uint16_t blockAlign, uint16_t bitsPerSample, uint32_t waveDataSize,
          uint8_t* waveData, std::string waveName = "Untitled wave")
      : wFormatTag(formatTag), wChannels(channels), dwSamplesPerSec(samplesPerSec),
        dwAveBytesPerSec(aveBytesPerSec), wBlockAlign(blockAlign), wBitsPerSample(bitsPerSample),
        m_name(std::move(waveName)), m_wave_data(waveData, waveData + waveDataSize) {
    RiffFile::alignName(m_name);
  }

  //	This function will always return an even value, to maintain the alignment
  // necessary for the RIFF format.
  unsigned long sampleSize() const {
    if (m_wave_data.size() % 2)
      return m_wave_data.size() + 1;
    else
      return m_wave_data.size();
  }
  uint32_t size() const;
  void write(std::vector<uint8_t> &buf);

private:
  uint16_t wFormatTag;
  uint16_t wChannels;
  uint32_t dwSamplesPerSec;
  uint32_t dwAveBytesPerSec;
  uint16_t wBlockAlign;
  uint16_t wBitsPerSample;

  std::string m_name{"Untitled wave"};
  std::vector<uint8_t> m_wave_data;
};
