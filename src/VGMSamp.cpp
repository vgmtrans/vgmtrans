#include "stdafx.h"
#include "VGMSamp.h"
#include "VGMSampColl.h"
#include "Root.h"


// *******
// VGMSamp
// *******

DECLARE_MENU(VGMSamp)

VGMSamp::VGMSamp(VGMSampColl* sampColl, ULONG offset, ULONG length, ULONG dataOffset,
				 ULONG dataLen, BYTE nChannels, USHORT theBPS,
				 ULONG theRate, wstring theName)
 : parSampColl(sampColl),
   sampName(theName),
   VGMItem(sampColl->vgmfile, offset, length), 
   dataOff(dataOffset),
   dataLength(dataLen),
   bps(theBPS),
   rate(theRate),
   ulUncompressedSize(0),
   channels(nChannels),
   pan(0),
   unityKey(-1),
   fineTune(0),
   volume(-1),
   waveType(WT_UNDEFINED),
   bPSXLoopInfoPrioritizing(false)
{
	name = sampName.data();		//I would do this in the initialization list, but VGMItem() constructor is called before sampName is initialized,
								//so data() ends up returning a bad pointer
}

VGMSamp::~VGMSamp()
{
}

double VGMSamp::GetCompressionRatio()
{
	return 1.0;
}

void VGMSamp::ConvertToStdWave(BYTE* buf)
{
	switch (waveType)
	{
	//case WT_IMA_ADPCM:
	//	ConvertImaAdpcm(buf);
	case WT_PCM8:
		GetBytes(dataOff, dataLength, buf);
		for(unsigned int i = 0; i < dataLength; i++)		//convert every byte from signed to unsigned value
			buf[i] ^= 0x80;			//For no good reason, the WAV standard has PCM8 unsigned and PCM16 signed
		break;
	case WT_PCM16:
	default:
		GetBytes(dataOff, dataLength, buf);
		break;
	}
}

bool VGMSamp::OnSaveAsWav()
{
	wstring filepath = pRoot->UI_GetSaveFilePath(name, L"wav");
	if (filepath.length() != 0)
		return SaveAsWav(filepath.c_str());
	return false;
}

bool VGMSamp::SaveAsWav(const wchar_t* filepath)
{
//	vector<BYTE> midiBuf;
//	WriteMidiToBuffer(midiBuf);
//	return pRoot->UI_WriteBufferToFile(filepath, &midiBuf[0], midiBuf.size());

	vector<BYTE> waveBuf;
	ULONG bufSize;
	if (this->ulUncompressedSize)
		bufSize = this->ulUncompressedSize;
	else
		bufSize = (ULONG)ceil((double)dataLength * GetCompressionRatio());

	BYTE* uncompSampBuf = new BYTE[bufSize];	//create a new memory space for the uncompressed wave
	//waveBuf.resize(bufSize );
	ConvertToStdWave(uncompSampBuf);			//and uncompress into that space

	USHORT blockAlign = bps / 8*channels;

	PushTypeOnVectBE<UINT>(waveBuf, 0x52494646);			//"RIFF"
	PushTypeOnVect<UINT>(waveBuf, bufSize+0x24);			//size

	//WriteLIST(waveBuf, 0x43564157, bufSize+24);			//write "WAVE" list
	PushTypeOnVectBE<UINT>(waveBuf, 0x57415645);			//"WAVE"
	PushTypeOnVectBE<UINT>(waveBuf, 0x666D7420);			//"fmt "
	PushTypeOnVect<UINT>(waveBuf, 16);						//size
	PushTypeOnVect<USHORT>(waveBuf, 1);						//wFormatTag
	PushTypeOnVect<USHORT>(waveBuf, channels);				//wChannels
	PushTypeOnVect<ULONG>(waveBuf, rate);					//dwSamplesPerSec
	PushTypeOnVect<ULONG>(waveBuf, rate*blockAlign);		//dwAveBytesPerSec
	PushTypeOnVect<USHORT>(waveBuf, blockAlign);			//wBlockAlign
	PushTypeOnVect<USHORT>(waveBuf, bps);					//wBitsPerSample

	PushTypeOnVectBE<UINT>(waveBuf, 0x64617461);			//"data"
	PushTypeOnVect<UINT>(waveBuf, bufSize);			//size
	waveBuf.insert(waveBuf.end(), uncompSampBuf, uncompSampBuf+bufSize);	//Write the sample
	if (bufSize % 2)
		waveBuf.push_back(0);

	
	//DLSWave dlswave(1, channels, rate, rate*blockAlign, blockAlign, bps, bufSize, uncompSampBuf);
	//dlswave.Write(waveBuf);
	return pRoot->UI_WriteBufferToFile(filepath, &waveBuf[0], waveBuf.size());
}
