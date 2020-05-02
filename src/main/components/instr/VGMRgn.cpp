#include "pch.h"
#include "VGMRgn.h"
#include "VGMInstrSet.h"

// ******
// VGMRgn
// ******

VGMRgn::VGMRgn(VGMInstr *instr, uint32_t offset, uint32_t length, const std::string &name)
    : VGMContainerItem(instr->parInstrSet, offset, length, name),
      parInstr(instr),
      keyLow(0),
      keyHigh(0x7F),
      velLow(0),
      velHigh(0x7F),
      sampNum(0),
      sampOffset(-1),
      sampCollPtr(NULL),
      unityKey(-1),
      fineTune(0),
      pan(0.5),
      volume(-1),
      attack_time(0),
      attack_transform(no_transform),
      hold_time(0),
      decay_time(0),
      sustain_level(-1),
      release_transform(no_transform),
      release_time(0) {
  AddContainer<VGMRgnItem>(items);
}

VGMRgn::VGMRgn(VGMInstr *instr, uint32_t offset, uint32_t length, uint8_t theKeyLow, uint8_t theKeyHigh,
               uint8_t theVelLow, uint8_t theVelHigh, int theSampNum, const std::string &name)
    : VGMContainerItem(instr->parInstrSet, offset, length, name),
      parInstr(instr),
      keyLow(theKeyLow),
      keyHigh(theKeyHigh),
      velLow(theVelLow),
      velHigh(theVelHigh),
      sampNum(theSampNum),
      sampOffset(-1),
      sampCollPtr(NULL),
      unityKey(-1),
      fineTune(0),
      pan(0.5),
      volume(-1),
      attack_time(0),
      attack_transform(no_transform),
      hold_time(0),
      decay_time(0),
      sustain_level(-1),
      sustain_time(0),
      release_transform(no_transform),
      release_time(0) {
  AddContainer<VGMRgnItem>(items);
}

VGMRgn::~VGMRgn() {
  DeleteVect<VGMRgnItem>(items);
}

void VGMRgn::SetRanges(uint8_t theKeyLow, uint8_t theKeyHigh, uint8_t theVelLow, uint8_t theVelHigh) {
  keyLow = theKeyLow;
  keyHigh = theKeyHigh;
  velLow = theVelLow;
  velHigh = theVelHigh;
}

void VGMRgn::SetUnityKey(uint8_t theUnityKey) {
  unityKey = theUnityKey;
}

void VGMRgn::SetSampNum(uint8_t sampNumber) {
  sampNum = sampNumber;
}

void VGMRgn::SetPan(uint8_t p) {
  //pan = thePan;
  pan = p;
  if (pan == 127)
    pan = 1.0;
  else if (pan == 0)
    pan = 0;
  else if (pan == 64)
    pan = 0.5;
  else
    pan = (double) pan / (double) 127;
}

void VGMRgn::SetLoopInfo(int theLoopStatus, uint32_t theLoopStart, uint32_t theLoopLength) {
  loop.loopStatus = theLoopStatus;
  loop.loopStart = theLoopStart;
  loop.loopLength = theLoopLength;
}


void VGMRgn::SetADSR(long attackTime, uint16_t atkTransform, long decayTime, long sustainLev,
                     uint16_t rlsTransform, long releaseTime) {
  attack_time = attackTime;
  attack_transform = atkTransform;
  decay_time = decayTime;
  sustain_level = sustainLev;
  release_transform = rlsTransform;
  release_time = releaseTime;
}

void VGMRgn::AddGeneralItem(uint32_t offset, uint32_t length, const std::string &name) {
  items.push_back(new VGMRgnItem(this, VGMRgnItem::RIT_GENERIC, offset, length, name));
}

void VGMRgn::AddUnknown(uint32_t offset, uint32_t length) {
  items.push_back(new VGMRgnItem(this, VGMRgnItem::RIT_UNKNOWN, offset, length, "Unknown"));
}

//assumes pan is given as 0-127 value, converts it to our double -1.0 to 1.0 format
void VGMRgn::AddPan(uint8_t p, uint32_t offset, uint32_t length) {
  SetPan(p);
  items.push_back(new VGMRgnItem(this, VGMRgnItem::RIT_PAN, offset, length, "Pan"));
}

void VGMRgn::SetVolume(double vol) {
  volume = vol;
}

void VGMRgn::AddVolume(double vol, uint32_t offset, uint32_t length) {
  volume = vol;
  items.push_back(new VGMRgnItem(this, VGMRgnItem::RIT_VOL, offset, length, "Volume"));
}

void VGMRgn::AddUnityKey(uint8_t uk, uint32_t offset, uint32_t length) {
  this->unityKey = uk;
  items.push_back(new VGMRgnItem(this, VGMRgnItem::RIT_UNITYKEY, offset, length, "Unity Key"));
}

void VGMRgn::AddFineTune(int16_t relativePitchCents, uint32_t offset, uint32_t length) {
  this->fineTune = relativePitchCents;
  items.push_back(new VGMRgnItem(this, VGMRgnItem::RIT_FINETUNE, offset, length, "Fine Tune"));
}

void VGMRgn::AddKeyLow(uint8_t kl, uint32_t offset, uint32_t length) {
  keyLow = kl;
  items.push_back(new VGMRgnItem(this, VGMRgnItem::RIT_KEYLOW, offset, length, "Note Range: Low Key"));
}

void VGMRgn::AddKeyHigh(uint8_t kh, uint32_t offset, uint32_t length) {
  keyHigh = kh;
  items.push_back(new VGMRgnItem(this, VGMRgnItem::RIT_KEYHIGH, offset, length, "Note Range: High Key"));
}

void VGMRgn::AddVelLow(uint8_t vl, uint32_t offset, uint32_t length) {
  velLow = vl;
  items.push_back(new VGMRgnItem(this, VGMRgnItem::RIT_VELLOW, offset, length, "Vel Range: Low"));
}

void VGMRgn::AddVelHigh(uint8_t vh, uint32_t offset, uint32_t length) {
  velHigh = vh;
  items.push_back(new VGMRgnItem(this, VGMRgnItem::RIT_VELHIGH, offset, length, "Vel Range: High"));
}

void VGMRgn::AddSampNum(int sn, uint32_t offset, uint32_t length) {
  sampNum = sn;
  items.push_back(new VGMRgnItem(this, VGMRgnItem::RIT_SAMPNUM, offset, length, "Sample Number"));
}


// **********
// VGMRgnItem
// **********

VGMRgnItem::VGMRgnItem(VGMRgn *rgn, RgnItemType theType, uint32_t offset, uint32_t length, const std::string &name)
    : VGMItem(rgn->vgmfile, offset, length, name), type(theType) {
}

VGMItem::Icon VGMRgnItem::GetIcon() {
  return ICON_BINARY;
}
