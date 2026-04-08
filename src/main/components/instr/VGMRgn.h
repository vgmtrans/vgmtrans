/*
 * VGMTrans (c) 2002-2026
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once
#include "Modulator.h"
#include "SynthFile.h"
#include "VGMItem.h"
#include "Loop.h"

class VGMInstr;
class VGMRgnItem;
class VGMSampColl;


// ******
// VGMRgn
// ******

class VGMRgn : public VGMItem {
 public:
  VGMRgn(VGMInstr *instr, uint32_t offset, uint32_t length = 0, std::string name = "Region");
  VGMRgn(VGMInstr *instr, uint32_t offset, uint32_t length, uint8_t keyLow, uint8_t keyHigh, uint8_t velLow,
         uint8_t velHigh, int sampNum, std::string name = "Region");

  virtual bool loadRgn() { return true; }

  void setRanges(uint8_t keyLow, uint8_t keyHigh, uint8_t velLow = 0, uint8_t velHigh = 0x7F);
  void setUnityKey(uint8_t unityNote);
  void setSampNum(uint8_t sampNumber);
  void setLoopInfo(int theLoopStatus, uint32_t theLoopStart, uint32_t theLoopLength);
  void setADSR(long attack_time, uint16_t atk_transform, long decay_time, long sustain_lev,
               uint16_t rls_transform, long release_time);

  void addGeneralItem(uint32_t offset, uint32_t length, const std::string &name);
  void addUnknown(uint32_t offset, uint32_t length);
  void setFineTune(int16_t relativePitchCents) { fineTune = relativePitchCents; }
  void setPan(uint8_t pan);
  void addPan(uint8_t pan, uint32_t offset, uint32_t length = 1, const std::string& name = "Pan");
  void setVolume(double volume);
  void setAttenuation(double decibels);
  void addVolume(double volume, uint32_t offset, uint32_t length = 1);
  void addAttenuation(double decibels, uint32_t offset, uint32_t length = 1);
  void addUnityKey(uint8_t unityKey, uint32_t offset, uint32_t length = 1);
  void addCoarseTune(int16_t relativeSemitones, uint32_t offset, uint32_t length = 1);
  void addFineTune(int16_t relativePitchCents, uint32_t offset, uint32_t length = 1);
  void addKeyLow(uint8_t keyLow, uint32_t offset, uint32_t length = 1);
  void addKeyHigh(uint8_t keyHigh, uint32_t offset, uint32_t length = 1);
  void addVelLow(uint8_t velLow, uint32_t offset, uint32_t length = 1);
  void addVelHigh(uint8_t velHigh, uint32_t offset, uint32_t length = 1);
  void addSampNum(int sampNum, uint32_t offset, uint32_t length = 1);
  void addADSRValue(uint32_t offset, uint32_t length, const std::string& name);

  void setLfoVibFreqHz(double freq) { m_lfoVibFreqHz = freq; }
  void setLfoVibDepthCents(double cents) { m_lfoVibDepthCents = cents; }
  void setLfoVibDelaySeconds(double seconds) { m_lfoVibDelaySeconds = seconds; }
  [[nodiscard]] double attenDb() const { return m_attenDb; }
  [[nodiscard]] double lfoVibFreqHz() const { return m_lfoVibFreqHz; }
  [[nodiscard]] double lfoVibDepthCents() const { return m_lfoVibDepthCents; }
  [[nodiscard]] double lfoVibDelaySeconds() const { return m_lfoVibDelaySeconds; }

  VGMInstr *parInstr;
  u8 keyLow       {0};
  u8 keyHigh      {0x7F};
  u8 velLow       {0};
  u8 velHigh      {0x7F};

  s8 unityKey     {-1};
  s16 coarseTune  {0};     // in semitones
  s16 fineTune    {0};     // in cents

  Loop loop;

  u32 sampNum        {0};
  s32 sampOffset     {-1};  // optional value. If a sample offset is provided, then find the sample
  s32 sampDataLength {-1};  // optional value. Refines sample matching in case of collisions.
  // number based on this offset. It will match a sample with an identical absolute offset, or
  // sample's offset relative to the beginning of sample data (VGMSampColl::sampDataOffset)

  // For formats that use multiple sampColls and reference samples base 0 for each sampColl (NDS)
  VGMSampColl *sampCollPtr {nullptr};

  double pan            {0.5};   // percentage.  0 = full left. 0.5 = center.  1 = full right
  double attack_time    {0};     // in seconds
  u16 attack_transform  {no_transform};
  double hold_time      {0};     // in seconds
  double decay_time     {0};     // in seconds
  double sustain_level  {-1};    // as a percentage of amplitude
  double sustain_time   {0};     // in seconds (we don't support positive rate here, as is possible on psx)
  u16 release_transform {no_transform};
  double release_time   {0};     // in seconds

  // Real-time modulation
  void addModulator(Modulator mod) { m_modulators.push_back(mod); }
  const std::vector<Modulator> &modulators() const { return m_modulators; }

private:
  std::vector<Modulator> m_modulators;

  double m_attenDb      {0};     // attenuation in decibels;

  double m_lfoVibFreqHz       {0};
  double m_lfoVibDepthCents   {0};
  double m_lfoVibDelaySeconds {0};
};


// **********
// VGMRgnItem
// **********


class VGMRgnItem : public VGMItem {
 public:
  enum RgnItemType {
    RIT_GENERIC,
    RIT_UNKNOWN,
    RIT_UNITYKEY,
    RIT_FINETUNE,
    RIT_KEYLOW,
    RIT_KEYHIGH,
    RIT_VELLOW,
    RIT_VELHIGH,
    RIT_PAN,
    RIT_VOL,
    RIT_SAMPNUM,
    RIT_ADSR,
  };

  VGMRgnItem(const VGMRgn *rgn, RgnItemType theType, uint32_t offset, uint32_t length, std::string name);

private:
  static Type resolveType(RgnItemType rgnItemType) {
    switch (rgnItemType) {
      case RIT_ADSR:    return Type::Adsr;
      case RIT_VOL:     return Type::Volume;
      case RIT_PAN:     return Type::Pan;
      default:          return Type::Misc;
    }
  }
};
