#include "stdafx.h"
#include "VGMSampColl.h"
#include "VGMSamp.h"
#include "VGMInstrSet.h"
#include "Root.h"



// ***********
// VGMSampColl
// ***********

DECLARE_MENU(VGMSampColl)

VGMSampColl::VGMSampColl(const string& format, RawFile* rawfile, ULONG offset, ULONG length, wstring theName)
: VGMFile(FILETYPE_SAMPCOLL, format, rawfile, offset, length, theName),
  parInstrSet(NULL),
  bLoadOnInstrSetMatch(false),
  bLoaded(false),
  sampDataOffset(0)
{
	AddContainer<VGMSamp>(samples);
}

VGMSampColl::VGMSampColl(const string& format, RawFile* rawfile, VGMInstrSet* instrset, 
						 ULONG offset, ULONG length, wstring theName)
: VGMFile(FILETYPE_SAMPCOLL, format, rawfile, offset, length, theName),
  parInstrSet(instrset),
  bLoadOnInstrSetMatch(false),
  bLoaded(false),
  sampDataOffset(0)
{
	AddContainer<VGMSamp>(samples);
}

/*VGMSampColl::VGMSampColl(VGMInstrSet* instrSet, ULONG offset)
: parInstrSet(instrSet), VGMFile(instrSet->GetRawFile(), offset)
{
}*/

VGMSampColl::~VGMSampColl(void)
{
	DeleteVect<VGMSamp>(samples);
}


bool VGMSampColl::Load()
{
	if (bLoaded)
		return true;
	if (!GetHeaderInfo())
		return false;
	if (!GetSampleInfo())
		return false;

	if (samples.size() == 0)
		return false;

	if (unLength == 0)
	{
		for (std::vector<VGMSamp*>::iterator itr = samples.begin(); itr != samples.end(); ++itr)
		{
			VGMSamp* samp = (*itr);

			assert(dwOffset <= samp->dwOffset);
			//if (dwOffset > samp->dwOffset)
			//{
			//	unLength += samp->dwOffset - dwOffset;
			//	dwOffset = samp->dwOffset;
			//}

			if (dwOffset + unLength < samp->dwOffset + samp->unLength)
			{
				unLength = (samp->dwOffset + samp->unLength) - dwOffset;
			}
		}
	}

	UseRawFileData();
	if (!parInstrSet)
		pRoot->AddVGMFile(this);
	bLoaded = true;
	return true;
}


bool VGMSampColl::GetHeaderInfo()
{
	return true;
}

bool VGMSampColl::GetSampleInfo()
{
	return true;
}

VGMSamp* VGMSampColl::AddSamp(ULONG offset, ULONG length, ULONG dataOffset, ULONG dataLength,
					 BYTE nChannels, USHORT bps, ULONG theRate, wstring name)
{
	VGMSamp* newSamp = new VGMSamp(this, offset, length, dataOffset, dataLength, nChannels,
									bps, theRate, name);
	samples.push_back(newSamp);
	return newSamp;
}

bool VGMSampColl::OnSaveAllAsWav()
{
	wstring dirpath = pRoot->UI_GetSaveDirPath();
	if (dirpath.length() != 0)
	{
		for (UINT i=0; i<samples.size(); i++)
		{
			wstring filepath = dirpath + L"\\" + samples[i]->sampName + L".wav";
			samples[i]->SaveAsWav(filepath.c_str());
		}
		return true;
	}
	return false;
}
