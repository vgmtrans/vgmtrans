#include "stdafx.h"
#include "VGMRgn.h"
#include "VGMInstrSet.h"

// ******
// VGMRgn
// ******

VGMRgn::VGMRgn(VGMInstr* instr, ULONG offset, ULONG length, const wchar_t* name)
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
  attack_time(-1),
  attack_transform(-1),
  decay_time(-1),
  sustain_level(-1),
  release_transform(-1),
  release_time(-1)
{
	AddContainer<VGMRgnItem>(items);
}

VGMRgn::VGMRgn(VGMInstr* instr, ULONG offset, ULONG length, BYTE theKeyLow, BYTE theKeyHigh,
			   BYTE theVelLow, BYTE theVelHigh, int theSampNum, const wchar_t* name)
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
  attack_time(-1),
  attack_transform(-1),
  decay_time(-1),
  sustain_level(-1),
  sustain_time(-1),
  release_transform(-1),
  release_time(-1)
{
	AddContainer<VGMRgnItem>(items);
}

VGMRgn::~VGMRgn()
{
	DeleteVect<VGMRgnItem>(items);
}

void VGMRgn::SetRanges(BYTE theKeyLow, BYTE theKeyHigh, BYTE theVelLow, BYTE theVelHigh)
{
	keyLow = theKeyLow;
	keyHigh = theKeyHigh;
	velLow = theVelLow;
	velHigh = theVelHigh;
}

void VGMRgn::SetUnityKey(BYTE theUnityKey)
{
	unityKey = theUnityKey;
}

void VGMRgn::SetSampNum(BYTE sampNumber)
{
	sampNum = sampNumber;
}

void VGMRgn::SetPan(BYTE p)
{
	//pan = thePan;
	pan = p;
	if (pan == 127)
		pan = 1.0;
	else if (pan == 0)
		pan = 0;
	else if (pan == 64)
		pan = 0.5;
	else
		pan = (double)pan/(double)127;
}

void VGMRgn::SetLoopInfo(int theLoopStatus, ULONG theLoopStart, ULONG theLoopLength)
{
	loop.loopStatus = theLoopStatus;
	loop.loopStart = theLoopStart;
	loop.loopLength = theLoopLength;
}


void VGMRgn::SetADSR(long attackTime, USHORT atkTransform, long decayTime, long sustainLev,
			 USHORT rlsTransform, long releaseTime)
{
	attack_time = attackTime;
	attack_transform = atkTransform;
	decay_time = decayTime;
	sustain_level = sustainLev;
	release_transform = rlsTransform;
	release_time = releaseTime;
}

void VGMRgn::AddGeneralItem(ULONG offset, ULONG length, const wchar_t* name)
{
	items.push_back(new VGMRgnItem(this, VGMRgnItem::RIT_GENERIC, offset, length, name));
}

void VGMRgn::AddUnknown(ULONG offset, ULONG length)
{
	items.push_back(new VGMRgnItem(this, VGMRgnItem::RIT_UNKNOWN, offset, length, L"Unknown"));
}

//assumes pan is given as 0-127 value, converts it to our double -1.0 to 1.0 format
void VGMRgn::AddPan(BYTE p, ULONG offset, ULONG length)
{
	SetPan(p);
	items.push_back(new VGMRgnItem(this, VGMRgnItem::RIT_PAN, offset, length, L"Pan"));
}

void VGMRgn::SetVolume(double vol)
{
	volume = vol;
}

void VGMRgn::AddVolume(double vol, ULONG offset, ULONG length)
{
	volume = vol;
	items.push_back(new VGMRgnItem(this, VGMRgnItem::RIT_VOL, offset, length, L"Volume"));
}

void VGMRgn::AddUnityKey(BYTE uk, ULONG offset, ULONG length)
{
	this->unityKey = uk;
	items.push_back(new VGMRgnItem(this, VGMRgnItem::RIT_UNITYKEY, offset, length, L"Unity Key"));
}

void VGMRgn::AddKeyLow(BYTE kl, ULONG offset, ULONG length)
{
	keyLow = kl;
	items.push_back(new VGMRgnItem(this, VGMRgnItem::RIT_KEYLOW, offset, length, L"Note Range: Low Key"));
}

void VGMRgn::AddKeyHigh(BYTE kh, ULONG offset, ULONG length)
{
	keyHigh = kh;
	items.push_back(new VGMRgnItem(this, VGMRgnItem::RIT_KEYHIGH, offset, length, L"Note Range: High Key"));
}

void VGMRgn::AddVelLow(BYTE vl, ULONG offset, ULONG length)
{
	velLow = vl;
	items.push_back(new VGMRgnItem(this, VGMRgnItem::RIT_VELLOW, offset, length, L"Vel Range: Low"));
}

void VGMRgn::AddVelHigh(BYTE vh, ULONG offset, ULONG length)
{
	velHigh = vh;
	items.push_back(new VGMRgnItem(this, VGMRgnItem::RIT_VELHIGH, offset, length, L"Vel Range: High"));
}

void VGMRgn::AddSampNum(int sn, ULONG offset, ULONG length)
{
	sampNum = sn;
	items.push_back(new VGMRgnItem(this, VGMRgnItem::RIT_SAMPNUM, offset, length, L"Sample Number"));
}


// **********
// VGMRgnItem
// **********

VGMRgnItem::VGMRgnItem(VGMRgn* rgn, RgnItemType theType, ULONG offset, ULONG length, const wchar_t* name)
: VGMItem(rgn->vgmfile, offset, length, name), type(theType)
{
}

VGMItem::Icon VGMRgnItem::GetIcon()
{
	//switch (type)
	//{
	//default:
	//	return ICON_BINARY;
	//}
	return ICON_BINARY;
}
