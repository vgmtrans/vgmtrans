/**
 * VGMTrans (c) - 2002-2021
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#pragma once
#include "VGMInstrSet.h"
#include "VGMSampColl.h"
#include "VGMSamp.h"
#include "NDSFormat.h"

class NDSInstr;

// ***********
// NDSInstrSet
// ***********

class NDSInstrSet : public VGMInstrSet {
public:
  NDSInstrSet(RawFile *file, uint32_t offset, uint32_t length, VGMSampColl *psg_samples,
              std::string name = "NDS Instrument Bank");
  bool parseInstrPointers() override;

  std::vector<VGMSampColl *> sampCollWAList;

private:
  VGMSampColl* m_psg_samples{};
  friend NDSInstr;
};

// ********
// NDSInstr
// ********

class NDSInstr : public VGMInstr {
public:
  NDSInstr(NDSInstrSet *instrSet, uint32_t offset, uint32_t length, uint32_t bank,
           uint32_t instrNum, uint8_t instrType);

  bool loadInstr() override;

  void getSampCollPtr(VGMRgn *rgn, int waNum) const;
  void getArticData(VGMRgn *rgn, uint32_t offset) const;
  uint16_t getFallingRate(uint8_t DecayTime) const;

private:
  uint8_t instrType;
};

// ***********
// NDSWaveArch
// ***********

// I'm using the data from http://nocash.emubase.de/gbatek.htm#dssound
// i should probably reproduce this table programmatically instead
// "The closest way to reproduce the AdpcmTable with 32bit integer maths appears:
//    X=000776d2h, FOR I=0 TO 88, Table[I]=X SHR 16, X=X+(X/10), NEXT I
//    Table[3]=000Ah, Table[4]=000Bh, Table[88]=7FFFh, Table[89..127]=0000h      "

static const unsigned AdpcmTable[89] = {
    0x0007, 0x0008, 0x0009, 0x000A, 0x000B, 0x000C, 0x000D, 0x000E, 0x0010, 0x0011, 0x0013, 0x0015,
    0x0017, 0x0019, 0x001C, 0x001F, 0x0022, 0x0025, 0x0029, 0x002D, 0x0032, 0x0037, 0x003C, 0x0042,
    0x0049, 0x0050, 0x0058, 0x0061, 0x006B, 0x0076, 0x0082, 0x008F, 0x009D, 0x00AD, 0x00BE, 0x00D1,
    0x00E6, 0x00FD, 0x0117, 0x0133, 0x0151, 0x0173, 0x0198, 0x01C1, 0x01EE, 0x0220, 0x0256, 0x0292,
    0x02D4, 0x031C, 0x036C, 0x03C3, 0x0424, 0x048E, 0x0502, 0x0583, 0x0610, 0x06AB, 0x0756, 0x0812,
    0x08E0, 0x09C3, 0x0ABD, 0x0BD0, 0x0CFF, 0x0E4C, 0x0FBA, 0x114C, 0x1307, 0x14EE, 0x1706, 0x1954,
    0x1BDC, 0x1EA5, 0x21B6, 0x2515, 0x28CA, 0x2CDF, 0x315B, 0x364B, 0x3BB9, 0x41B2, 0x4844, 0x4F7E,
    0x5771, 0x602F, 0x69CE, 0x7462, 0x7FFF};

static constexpr int IMA_IndexTable[9] = {-1, -1, -1, -1, 2, 4, 6, 8};

class NDSWaveArch : public VGMSampColl {
public:
  NDSWaveArch(RawFile *file, uint32_t offset, uint32_t length,
              std::string name = "NDS Wave Archive");
  ~NDSWaveArch() override = default;

  bool parseHeader() override;
  bool parseSampleInfo() override;
};

class NDSPSG : public VGMSampColl {
public:
  NDSPSG(RawFile *file);
  ~NDSPSG() override = default;
  
private:
  bool parseSampleInfo() override;
};

// *******
// NDSSamp
// *******

class NDSSamp : public VGMSamp {
public:
  NDSSamp(VGMSampColl *sampColl, uint32_t offset = 0, uint32_t length = 0, uint32_t dataOffset = 0,
          uint32_t dataLength = 0, uint8_t channels = 1, uint16_t bps = 16, uint32_t rate = 0,
          uint8_t waveType = 0, std::string name = "Sample");

  double compressionRatio() override;  // ratio of space conserved.  should generally be > 1
  // used to calculate both uncompressed sample size and loopOff after conversion
  void convertToStdWave(uint8_t *buf) override;

  void convertImaAdpcm(uint8_t *buf);

  static inline void clamp_step_index(int &stepIndex);
  static inline void clamp_sample(int &decompSample);
  static inline void process_nibble(unsigned char code, int &stepIndex, int &decompSample);

  enum { PCM8, PCM16, IMA_ADPCM };

  uint8_t waveType;
};

class NDSPSGSamp : public VGMSamp {
public:
  NDSPSGSamp(VGMSampColl *sampcoll, uint8_t duty_cycle);
  ~NDSPSGSamp() override = default;

private:
  void convertToStdWave(uint8_t *buf) override;

  /* We use -1 to indicate noise */
  double m_duty_cycle{-1};
};
