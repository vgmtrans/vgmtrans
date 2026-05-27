/**
 * VGMTrans (c) - 2002-2021
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#pragma once

#include "base/Types.h"
#include "Modulation.h"
#include "RiffFile.h"
#include <filesystem>
#include <string>

struct Loop;
class VGMSamp;
struct ConversionContext;

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
#define CONN_SRC_POLYPRESSURE 0x0007
#define CONN_SRC_CHANNELPRESSURE 0x0008
#define CONN_SRC_VIBRATO 0x0009

/* Midi Controllers 0-127 */
#define CONN_SRC_CC1 0x0081
#define CONN_SRC_CC7 0x0087
#define CONN_SRC_CC10 0x008a
#define CONN_SRC_CC11 0x008b
#define CONN_SRC_CC91 0x00db
#define CONN_SRC_CC93 0x00dd

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

/* LFO Destinations */
#define CONN_DST_LFO_FREQUENCY 0x0104
#define CONN_DST_LFO_STARTDELAY 0x0105

/* DLS2 Vibrato LFO Destinations */
#define CONN_DST_VIB_FREQUENCY 0x0114
#define CONN_DST_VIB_STARTDELAY 0x0115

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
  DLSWave *addWave(u16 formatTag, u16 channels, int samplesPerSec, int aveBytesPerSec,
                   u16 blockAlign, u16 bitsPerSample, u32 waveDataSize,
                   u8 *waveData, std::string name = "Unnamed Wave");

  std::vector<DLSInstr *> instruments();
  std::vector<DLSWave *> waves();

  u32 size() override;

  int writeDLSToBuffer(std::vector<u8> &buf);
  bool saveDLSFile(const std::filesystem::path &filepath);

private:
  std::vector<std::unique_ptr<DLSInstr>> m_instrs;
  std::vector<std::unique_ptr<DLSWave>> m_waves;
};

class DLSInstr {
public:
  DLSInstr() = default;
  DLSInstr(u32 bank, u32 instrument);
  DLSInstr(u32 bank, u32 instrument, std::string instrName);

  DLSRgn *addRgn();

  u32 size() const;
  void write(std::vector<u8> &buf);

private:
  u32 ulBank;
  u32 ulInstrument;

  std::vector<std::unique_ptr<DLSRgn>> m_regions;
  std::string m_name;
};

class DLSRgn {
public:
  DLSRgn() = default;
  DLSRgn(u16 keyLow, u16 keyHigh, u16 velLow, u16 velHigh)
      : usKeyLow(keyLow), usKeyHigh(keyHigh), usVelLow(velLow), usVelHigh(velHigh),
        fusOptions(0), usPhaseGroup(0), channel(0), tableIndex(0) {}

  DLSArt *addArt();
  DLSWsmp *addWsmp();
  void setRanges(u16 keyLow = 0, u16 keyHigh = 0x7F, u16 velLow = 0,
                 u16 velHigh = 0x7F);
  void setWaveLinkInfo(u16 options, u16 phaseGroup, u32 theChannel,
                       u32 theTableIndex);

  u32 size() const;
  void write(std::vector<u8> &buf) const;

private:
  u16 usKeyLow;
  u16 usKeyHigh;
  u16 usVelLow;
  u16 usVelHigh;

  u16 fusOptions;
  u16 usPhaseGroup;
  u32 channel;
  u32 tableIndex;

  std::unique_ptr<DLSWsmp> m_wsmp;
  std::unique_ptr<DLSArt> m_art;
};

class ConnectionBlock {
public:
  ConnectionBlock(u16 source, u16 control, u16 destination, u16 transform,
                  s32 scale)
      : usSource(source), usControl(control), usDestination(destination), usTransform(transform),
        lScale(scale) {}
  ~ConnectionBlock() {}

  static u32 size() { return 12; }
  void write(std::vector<u8> &buf) const;

private:
  u16 usSource;
  u16 usControl;
  u16 usDestination;
  u16 usTransform;
  s32 lScale;
};

class DLSArt {
public:
  DLSArt() = default;

  void addADSR(long attack_time, u16 atk_transform, long hold_time, long decay_time,
               long sustain_lev, long release_time, u16 rls_transform);
  void addPan(long pan);
  void addVibrato(s32 depth, s32 frequency, s32 delay);
  void addGenerator(const SynthGenerator& generator);
  void addModulator(const SynthModulator& modulator, const ConversionContext& context);

  u32 GetSize() const;
  void Write(std::vector<u8> &buf) const;

private:
  std::vector<std::unique_ptr<ConnectionBlock>> m_blocks;
};

class DLSWsmp {
public:
  DLSWsmp() = default;

  void setLoopInfo(Loop &loop, VGMSamp *samp);
  void setPitchInfo(u16 unityNote, s16 fineTune, s32 attenuation);

  [[nodiscard]] u32 size() const;
  void write(std::vector<u8> &buf) const;

private:
  u16 usUnityNote;
  s16 sFineTune;
  s32 lAttenuation;
  s8 cSampleLoops;

  u32 ulLoopType;
  u32 ulLoopStart;
  u32 ulLoopLength;
};

class DLSWave {
public:
  DLSWave(u16 formatTag, u16 channels, u32 samplesPerSec, u32 aveBytesPerSec,
          u16 blockAlign, u16 bitsPerSample, u32 waveDataSize,
          u8* waveData, std::string waveName = "Untitled wave")
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
  u32 size() const;
  void write(std::vector<u8> &buf);

private:
  u16 wFormatTag;
  u16 wChannels;
  u32 dwSamplesPerSec;
  u32 dwAveBytesPerSec;
  u16 wBlockAlign;
  u16 wBitsPerSample;

  std::string m_name{"Untitled wave"};
  std::vector<u8> m_wave_data;
};
