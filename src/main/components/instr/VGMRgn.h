#pragma once
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

  //VGMArt* addArt(vector<connectionBlock*> connBlocks);
  //VGMSamp* addSamp(void);
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
  //void setAttenuation(long attenuation);
  //void addAttenuation(long atten, uint32_t offset, uint32_t length = 1);
  void setVolume(double volume);
  void addVolume(double volume, uint32_t offset, uint32_t length = 1);
  void addUnityKey(uint8_t unityKey, uint32_t offset, uint32_t length = 1);
  void addFineTune(int16_t relativePitchCents, uint32_t offset, uint32_t length = 1);
  void addKeyLow(uint8_t keyLow, uint32_t offset, uint32_t length = 1);
  void addKeyHigh(uint8_t keyHigh, uint32_t offset, uint32_t length = 1);
  void addVelLow(uint8_t velLow, uint32_t offset, uint32_t length = 1);
  void addVelHigh(uint8_t velHigh, uint32_t offset, uint32_t length = 1);
  void addSampNum(int sampNum, uint32_t offset, uint32_t length = 1);
  void addADSRValue(uint32_t offset, uint32_t length, const std::string& name);
  //void setAttack();
  //void setDelay();
  //void setSustain();
  //void setRelease();

  //void SetWaveLinkInfo(uint16_t options, uint16_t phaseGroup, uint32_t theChannel, uint32_t theTableIndex);

 public:
  VGMInstr *parInstr;
  uint8_t keyLow;
  uint8_t keyHigh;
  uint8_t velLow;
  uint8_t velHigh;

  int8_t unityKey;
  int16_t fineTune;

  Loop loop;

  //uint16_t fusOptions;
  //uint16_t usPhaseGroup;
  //uint32_t channel;
  //uint32_t tableIndex;

  uint32_t sampNum;
  uint32_t sampOffset;        //optional value. If a sample offset is provided, then find the sample number based on this offset.
  // This is an absolute offset into the SampColl.  It's not necessarily relative to the beginning of the
  // actual sample data, unless the sample data begins at offset 0.
  //int sampCollNum;	//optional value. for formats that use multiple sampColls and reference samples base 0 for each sampColl (NDS, for instance)
  VGMSampColl *sampCollPtr;

  //long attenuation;
  double volume;    // as percentage of full volume.  This will be converted to to an attenuation when we convert to a SynthFile
  double pan;        //percentage.  0 = full left. 0.5 = center.  1 = full right
  double attack_time;            //in seconds
  uint16_t attack_transform;
  double hold_time;            //in seconds
  double decay_time;            //in seconds
  double sustain_level;        //as a percentage
  double sustain_time;        //in seconds (we don't support positive rate here, as is possible on psx)
  uint16_t release_transform;
  double release_time;        //in seconds
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
  };        //HIT = Header Item Type

  VGMRgnItem(const VGMRgn *rgn, RgnItemType theType, uint32_t offset, uint32_t length, std::string name);
  Icon icon() override;

 public:
  RgnItemType type;
};



/*
class VGMArt :
	public VGMItem
{
public:
	VGMArt();
	~VGMArt();
};*/
