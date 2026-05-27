/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#include "Types.h"
#include "ScaleConversion.h"
#include "VGMRgn.h"
#include "VGMInstrSet.h"

// ******
// VGMRgn
// ******

VGMRgn::VGMRgn(VGMInstr *instr, u32 offset, u32 length, std::string name)
    : VGMItem(instr->parInstrSet, offset, length, std::move(name)),
      parInstr(instr)
{}

VGMRgn::VGMRgn(VGMInstr *instr, u32 offset, u32 length, u8 theKeyLow, u8 theKeyHigh,
               u8 theVelLow, u8 theVelHigh, int theSampNum, std::string name)
    : VGMItem(instr->parInstrSet, offset, length, std::move(name)),
      parInstr(instr),
      keyLow(theKeyLow),
      keyHigh(theKeyHigh),
      velLow(theVelLow),
      velHigh(theVelHigh),
      sampNum(theSampNum)
{}

void VGMRgn::setRanges(u8 theKeyLow, u8 theKeyHigh, u8 theVelLow, u8 theVelHigh) {
  keyLow = theKeyLow;
  keyHigh = theKeyHigh;
  velLow = theVelLow;
  velHigh = theVelHigh;
}

void VGMRgn::setUnityKey(u8 theUnityKey) {
  unityKey = theUnityKey;
}

void VGMRgn::setSampNum(u8 sampNumber) {
  sampNum = sampNumber;
}

void VGMRgn::setPan(u8 p) {
  //pan = thePan;
  pan = p;
  if (pan == 127)
    pan = 1.0;
  else if (pan == 0)
    pan = 0;
  else if (pan == 64)
    pan = 0.5;
  else
    pan = pan / static_cast<double>(127);
}

void VGMRgn::setLoopInfo(int theLoopStatus, u32 theLoopStart, u32 theLoopLength) {
  loop.loopStatus = theLoopStatus;
  loop.loopStart = theLoopStart;
  loop.loopLength = theLoopLength;
}


void VGMRgn::setADSR(long attackTime, u16 atkTransform, long decayTime, long sustainLev,
                     u16 rlsTransform, long releaseTime) {
  attack_time = attackTime;
  attack_transform = atkTransform;
  decay_time = decayTime;
  sustain_level = sustainLev;
  release_transform = rlsTransform;
  release_time = releaseTime;
}

void VGMRgn::addGeneralItem(u32 offset, u32 length, const std::string &name) {
  addChild(new VGMRgnItem(this, VGMRgnItem::RIT_GENERIC, offset, length, name));
}

void VGMRgn::addUnknown(u32 offset, u32 length) {
  addChild(new VGMRgnItem(this, VGMRgnItem::RIT_UNKNOWN, offset, length, "Unknown"));
}

//assumes pan is given as 0-127 value, converts it to our double -1.0 to 1.0 format
void VGMRgn::addPan(u8 p, u32 offset, u32 length, const std::string& name) {
  setPan(p);
  addChild(new VGMRgnItem(this, VGMRgnItem::RIT_PAN, offset, length, name));
}

void VGMRgn::setVolume(double vol) {
  m_attenDb = ampToDb(vol);
}

void VGMRgn::setAttenuation(double attenDb) {
  m_attenDb = attenDb;
}

void VGMRgn::addVolume(double vol, u32 offset, u32 length) {
  setVolume(vol);
  addChild(new VGMRgnItem(this, VGMRgnItem::RIT_VOL, offset, length, "Volume"));
}

void VGMRgn::addAttenuation(double attenDb, u32 offset, u32 length) {
  setAttenuation(attenDb);
  addChild(new VGMRgnItem(this, VGMRgnItem::RIT_VOL, offset, length, "Attenuation"));
}

void VGMRgn::addUnityKey(u8 uk, u32 offset, u32 length) {
  this->unityKey = uk;
  addChild(new VGMRgnItem(this, VGMRgnItem::RIT_UNITYKEY, offset, length, "Unity Key"));
}

void VGMRgn::addCoarseTune(s16 relativeSemitones, u32 offset, u32 length) {
  this->coarseTune = relativeSemitones;
  addChild(new VGMRgnItem(this, VGMRgnItem::RIT_FINETUNE, offset, length, "Coarse Tune"));
}

void VGMRgn::addFineTune(s16 relativePitchCents, u32 offset, u32 length) {
  this->fineTune = relativePitchCents;
  addChild(new VGMRgnItem(this, VGMRgnItem::RIT_FINETUNE, offset, length, "Fine Tune"));
}

void VGMRgn::addKeyLow(u8 kl, u32 offset, u32 length) {
  keyLow = kl;
  addChild(new VGMRgnItem(this, VGMRgnItem::RIT_KEYLOW, offset, length, "Note Range: Low Key"));
}

void VGMRgn::addKeyHigh(u8 kh, u32 offset, u32 length) {
  keyHigh = kh;
  addChild(new VGMRgnItem(this, VGMRgnItem::RIT_KEYHIGH, offset, length, "Note Range: High Key"));
}

void VGMRgn::addVelLow(u8 vl, u32 offset, u32 length) {
  velLow = vl;
  addChild(new VGMRgnItem(this, VGMRgnItem::RIT_VELLOW, offset, length, "Vel Range: Low"));
}

void VGMRgn::addVelHigh(u8 vh, u32 offset, u32 length) {
  velHigh = vh;
  addChild(new VGMRgnItem(this, VGMRgnItem::RIT_VELHIGH, offset, length, "Vel Range: High"));
}

void VGMRgn::addSampNum(int sn, u32 offset, u32 length) {
  sampNum = sn;
  addChild(new VGMRgnItem(this, VGMRgnItem::RIT_SAMPNUM, offset, length, "Sample Number"));
}

void VGMRgn::addADSRValue(u32 offset, u32 length, const std::string& name) {
  addChild(new VGMRgnItem(this, VGMRgnItem::RIT_ADSR, offset, length, name));
}


// **********
// VGMRgnItem
// **********

VGMRgnItem::VGMRgnItem(const VGMRgn *rgn, RgnItemType rgnItemType, u32 offset, u32 length, std::string name)
    : VGMItem(rgn->vgmFile(), offset, length, std::move(name), resolveType(rgnItemType)) {
}
