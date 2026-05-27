#pragma once

#include "base/Types.h"
#include "Loop.h"
#include "SynthFile.h"
#include "VGMItem.h"

#include <string>

class VGMInstr;
class VGMRgnItem;
class VGMSampColl;

// ******
// VGMRgn
// ******

class VGMRgn : public VGMItem {
 public:
  VGMRgn(VGMInstr *instr, u32 offset, u32 length = 0, std::string name = "Region");
  VGMRgn(VGMInstr *instr, u32 offset, u32 length, u8 keyLow, u8 keyHigh, u8 velLow,
         u8 velHigh, int sampNum, std::string name = "Region");

  virtual bool loadRgn() { return true; }

  void setRanges(u8 keyLow, u8 keyHigh, u8 velLow = 0, u8 velHigh = 0x7F);
  void setUnityKey(u8 unityNote);
  void setSampNum(u8 sampNumber);
  void setLoopInfo(int theLoopStatus, u32 theLoopStart, u32 theLoopLength);
  void setADSR(long attack_time, u16 atk_transform, long decay_time, long sustain_lev,
               u16 rls_transform, long release_time);

  void addGeneralItem(u32 offset, u32 length, const std::string &name);
  void addUnknown(u32 offset, u32 length);
  void setFineTune(s16 relativePitchCents) { fineTune = relativePitchCents; }
  void setPan(u8 pan);
  void addPan(u8 pan, u32 offset, u32 length = 1, const std::string& name = "Pan");
  void setVolume(double volume);
  void setAttenuation(double decibels);
  double attenDb() { return m_attenDb; }
  void addVolume(double volume, u32 offset, u32 length = 1);
  void addAttenuation(double decibels, u32 offset, u32 length = 1);
  void addUnityKey(u8 unityKey, u32 offset, u32 length = 1);
  void addCoarseTune(s16 relativeSemitones, u32 offset, u32 length = 1);
  void addFineTune(s16 relativePitchCents, u32 offset, u32 length = 1);
  void addKeyLow(u8 keyLow, u32 offset, u32 length = 1);
  void addKeyHigh(u8 keyHigh, u32 offset, u32 length = 1);
  void addVelLow(u8 velLow, u32 offset, u32 length = 1);
  void addVelHigh(u8 velHigh, u32 offset, u32 length = 1);
  void addSampNum(int sampNum, u32 offset, u32 length = 1);
  void addADSRValue(u32 offset, u32 length, const std::string& name);

  void setLfoVibFreqHz(double freq) { m_lfoVibFreqHz = freq; }
  void setLfoVibDepthCents(double cents) { m_lfoVibDepthCents = cents; }
  void setLfoVibDelaySeconds(double seconds) { m_lfoVibDelaySeconds = seconds; }
  [[nodiscard]] double lfoVibFreqHz() const { return m_lfoVibFreqHz; }
  [[nodiscard]] double lfoVibDepthCents() const { return m_lfoVibDepthCents; }
  [[nodiscard]] double lfoVibDelaySeconds() const { return m_lfoVibDelaySeconds; }

public:
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

private:
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

  VGMRgnItem(const VGMRgn *rgn, RgnItemType theType, u32 offset, u32 length, std::string name);

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
