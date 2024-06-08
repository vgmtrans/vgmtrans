#pragma once

#include "RiffFile.h"

struct Loop;
class VGMSamp;

class SynthInstr;
class SynthRgn;
class SynthArt;
class SynthSampInfo;
class SynthWave;

typedef enum {
  no_transform,
  concave_transform
} Transform;

class SynthFile {
 public:
  SynthFile(std::string synth_name = "Instrument Set");
  ~SynthFile();

  SynthInstr *addInstr(uint32_t bank, uint32_t instrNum, float reverb);
  SynthInstr *addInstr(uint32_t bank, uint32_t instrNum, std::string Name, float reverb);
  SynthWave *addWave(uint16_t formatTag, uint16_t channels, int samplesPerSec, int aveBytesPerSec,
                     uint16_t blockAlign, uint16_t bitsPerSample, uint32_t waveDataSize, uint8_t *waveData,
                     std::string name = "Unnamed Wave");

  std::vector<SynthInstr *> vInstrs;
  std::vector<SynthWave *> vWaves;
  std::string m_name;
};

class SynthInstr {
 public:
  SynthInstr(uint32_t bank, uint32_t instrument, float reverb);
  SynthInstr(uint32_t bank, uint32_t instrument, std::string instrName, float reverb);
  SynthInstr(uint32_t bank, uint32_t instrument, std::string instrName,
             const std::vector<SynthRgn *>& listRgns, float reverb);
  ~SynthInstr();

  SynthRgn *addRgn();
  SynthRgn *addRgn(const SynthRgn& rgn);

  uint32_t ulBank;
  uint32_t ulInstrument;
  std::string name;
  float reverb;

  std::vector<SynthRgn *> vRgns;
};

class SynthSampInfo {
public:
  SynthSampInfo() = default;
  SynthSampInfo(uint16_t unityNote,
                int16_t fineTune,
                double atten,
                int8_t sampleLoops,
                uint32_t loopType,
                uint32_t loopStart,
                uint32_t loopLength)
      : usUnityNote(unityNote), sFineTune(fineTune), attenuation(atten), cSampleLoops(sampleLoops),
        ulLoopType(loopType),
        ulLoopStart(loopStart), ulLoopLength(loopLength) { }
  ~SynthSampInfo() = default;

  void setLoopInfo(Loop &loop, VGMSamp *samp);
  void setPitchInfo(uint16_t unityNote, int16_t fineTune, double attenuation);

  uint16_t usUnityNote;
  int16_t sFineTune;
  double attenuation;  // in decibels.
  int8_t cSampleLoops;

  uint32_t ulLoopType;
  uint32_t ulLoopStart;
  uint32_t ulLoopLength;
};

class SynthRgn {
 public:
  SynthRgn() = default;
  SynthRgn(uint16_t keyLow, uint16_t keyHigh, uint16_t velLow, uint16_t velHigh)
      : usKeyLow(keyLow), usKeyHigh(keyHigh), usVelLow(velLow), usVelHigh(velHigh) {}
  ~SynthRgn();

  SynthArt *addArt();
  SynthSampInfo *addSampInfo();
  void setRanges(uint16_t keyLow = 0, uint16_t keyHigh = 0x7F, uint16_t velLow = 0, uint16_t velHigh = 0x7F);
  void setWaveLinkInfo(uint16_t options, uint16_t phaseGroup, uint32_t theChannel, uint32_t theTableIndex);

  uint16_t usKeyLow {0};
  uint16_t usKeyHigh {0x7F};
  uint16_t usVelLow {0};
  uint16_t usVelHigh {0x7F};

  uint16_t fusOptions {0};    // would be used for DLS conversion
  uint16_t usPhaseGroup {0};  // ''
  uint32_t channel {1};       // ''
  uint32_t tableIndex {0};

  SynthSampInfo *sampinfo {nullptr};
  SynthArt *art {nullptr};
};

class SynthArt {
 public:
  SynthArt() = default;
  ~SynthArt();

  void addADSR(double attack, Transform atk_transform, double hold, double decay, double sustain_lev,
               double sustain_time, double release_time, Transform rls_transform);
  void addPan(double pan);

  double pan;                // -100% = left channel 100% = right channel 0 = 50/50

  double attack_time;        // rate expressed as seconds from 0 to 100% level
  double hold_time;
  double decay_time;        // rate expressed as seconds from 100% to 0% level, even though the sustain level isn't necessarily 0%
  double sustain_lev;        // db of attenuation at sustain level
  double sustain_time;    // this is part of the PSX envelope (and can actually be positive), but is not in DLS or SF2.  from 100 to 0, like release
  double release_time;    // rate expressed as seconds from 100% to 0% level, even though the sustain level may not be 100%
  Transform attack_transform;
  Transform release_transform;
};

class SynthWave {
 public:
  SynthWave(uint16_t formatTag, uint16_t channels, int samplesPerSec, int aveBytesPerSec, uint16_t blockAlign,
            uint16_t bitsPerSample, uint32_t waveDataSize, uint8_t *waveData, std::string waveName = "Untitled Wave")
      : sampinfo(nullptr),
        wFormatTag(formatTag),
        wChannels(channels),
        dwSamplesPerSec(samplesPerSec),
        dwAveBytesPerSec(aveBytesPerSec),
        wBlockAlign(blockAlign),
        wBitsPerSample(bitsPerSample),
        dataSize(waveDataSize),
        data(waveData),
        name(std::move(waveName)) {
    RiffFile::alignName(name);
  }
  ~SynthWave();

  SynthSampInfo *addSampInfo();

  void convertTo16bitSigned();

 public:
  SynthSampInfo *sampinfo;

  uint16_t wFormatTag;
  uint16_t wChannels;
  uint32_t dwSamplesPerSec;
  uint32_t dwAveBytesPerSec;
  uint16_t wBlockAlign;
  uint16_t wBitsPerSample;

  uint32_t dataSize;
  uint8_t *data;

  std::string name;
};
