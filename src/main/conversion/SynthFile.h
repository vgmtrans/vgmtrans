#pragma once

#include "util/types.h"

#include "Modulation.h"
#include "RiffFile.h"
#include <vector>

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

  SynthInstr *addInstr(u32 bank, u32 instrNum, float reverb);
  SynthInstr *addInstr(u32 bank, u32 instrNum, std::string Name, float reverb);
  SynthWave *addWave(u16 formatTag, u16 channels, int samplesPerSec, int aveBytesPerSec,
                     u16 blockAlign, u16 bitsPerSample, u32 waveDataSize,
                     std::vector<u8> waveData,
                     std::string name = "Unnamed Wave");

  std::vector<SynthInstr *> vInstrs;
  std::vector<SynthWave *> vWaves;
  std::string m_name;
};

class SynthInstr {
 public:
  SynthInstr(u32 bank, u32 instrument, float reverb);
  SynthInstr(u32 bank, u32 instrument, std::string instrName, float reverb);
  SynthInstr(u32 bank, u32 instrument, std::string instrName,
             const std::vector<SynthRgn *>& listRgns, float reverb);
  ~SynthInstr();

  SynthRgn *addRgn();
  SynthRgn *addRgn(const SynthRgn& rgn);

  void addModulator(const SynthModulator& modulator);
  void addModulator(ModSource source, ModDest destination, ModAmount amount);
  void addModulator(ModDest destination, ModAmount amount);
  [[nodiscard]] const std::vector<SynthModulator>& modulators() const { return m_modulators; }
  void addGenerator(const SynthGenerator& generator);
  void addGenerator(ModDest destination, ModAmount amount);
  [[nodiscard]] const std::vector<SynthGenerator>& generators() const { return m_generators; }

  u32 ulBank;
  u32 ulInstrument;
  std::string name;
  float reverb;

  std::vector<SynthRgn *> vRgns;

private:
  std::vector<SynthModulator> m_modulators;
  std::vector<SynthGenerator> m_generators;
};

class SynthSampInfo {
public:
  SynthSampInfo() = default;
  SynthSampInfo(u16 unityNote,
                s16 fineTune,
                double atten,
                s8 sampleLoops,
                u32 loopType,
                u32 loopStart,
                u32 loopLength)
      : usUnityNote(unityNote), sFineTune(fineTune), attenuation(atten), cSampleLoops(sampleLoops),
        ulLoopType(loopType),
        ulLoopStart(loopStart), ulLoopLength(loopLength) { }
  ~SynthSampInfo() = default;

  void setLoopInfo(Loop &loop, VGMSamp *samp);
  void setPitchInfo(u16 unityNote, s16 fineTune, double attenuation);

  u16 usUnityNote{0x3C};
  s16 sFineTune{0};
  double attenuation{0};  // in decibels.
  s8 cSampleLoops{0};

  u32 ulLoopType{0};
  u32 ulLoopStart{0};
  u32 ulLoopLength{0};
};

class SynthRgn {
 public:
  SynthRgn() = default;
  SynthRgn(u16 keyLow, u16 keyHigh, u16 velLow, u16 velHigh)
      : usKeyLow(keyLow), usKeyHigh(keyHigh), usVelLow(velLow), usVelHigh(velHigh) {}
  ~SynthRgn();

  SynthArt *addArt();
  SynthSampInfo *addSampInfo();
  void setRanges(u16 keyLow = 0, u16 keyHigh = 0x7F, u16 velLow = 0, u16 velHigh = 0x7F);
  void setWaveLinkInfo(u16 options, u16 phaseGroup, u32 theChannel, u32 theTableIndex);
  void setFineTune(s16 semitones, s16 fineTune);
  void setAttenuationDb(double attenuation);

  void setLfoVibFreqHz(double freq) { m_lfoVibFreqHz = freq; }
  void setLfoVibDepthCents(double cents) { m_lfoVibDepthCents = cents; }
  void setLfoVibDelaySeconds(double seconds) { m_lfoVibDelaySeconds = seconds; }
  [[nodiscard]] double lfoVibFreqHz() const { return m_lfoVibFreqHz; }
  [[nodiscard]] double lfoVibDepthCents() const { return m_lfoVibDepthCents; }
  [[nodiscard]] double lfoVibDelaySeconds() const { return m_lfoVibDelaySeconds; }

  u16 usKeyLow {0};
  u16 usKeyHigh {0x7F};
  u16 usVelLow {0};
  u16 usVelHigh {0x7F};

  u16 fusOptions {0};    // would be used for DLS conversion
  u16 usPhaseGroup {0};  // ''
  u32 channel {1};       // ''
  u32 tableIndex {0};

  s16 coarseTuneSemitones {0};
  s16 fineTuneCents {0};

  double attenDb {0};   // attenuation in decibels

  SynthSampInfo *sampinfo {nullptr};
  SynthArt *art {nullptr};

private:
  double m_lfoVibFreqHz       {0};
  double m_lfoVibDepthCents   {0};
  double m_lfoVibDelaySeconds {0};
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
  SynthWave(u16 formatTag, u16 channels, int samplesPerSec, int aveBytesPerSec, u16 blockAlign,
            u16 bitsPerSample, u32 waveDataSize, std::vector<u8> waveData,
            std::string waveName = "Untitled Wave")
      : sampinfo(nullptr),
        wFormatTag(formatTag),
        wChannels(channels),
        dwSamplesPerSec(samplesPerSec),
        dwAveBytesPerSec(aveBytesPerSec),
        wBlockAlign(blockAlign),
        wBitsPerSample(bitsPerSample),
        dataSize(waveDataSize),
        data(std::move(waveData)),
        name(std::move(waveName)) {
    RiffFile::alignName(name);
    dataSize = static_cast<u32>(data.size());
  }
  ~SynthWave();

  SynthSampInfo *addSampInfo();

  void convertTo16bit();

 public:
  SynthSampInfo *sampinfo;

  u16 wFormatTag;
  u16 wChannels;
  u32 dwSamplesPerSec;
  u32 dwAveBytesPerSec;
  u16 wBlockAlign;
  u16 wBitsPerSample;

  u32 dataSize;
  std::vector<u8> data;

  std::string name;
};
