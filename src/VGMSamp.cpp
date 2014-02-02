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

	bool hasLoop = (this->loop.loopStatus != -1 && this->loop.loopStatus != 0);

	PushTypeOnVectBE<UINT>(waveBuf, 0x52494646);			//"RIFF"
	PushTypeOnVect<UINT>(waveBuf, 0x24 + ((bufSize + 1) & ~1) + (hasLoop ? 0x50 : 0));	//size

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

	if (hasLoop)
	{
		const int origFormatBytesPerSamp = bps/8;
		double compressionRatio = GetCompressionRatio();

		// If the sample loops, but the loop length is 0, then assume the length should
		// extend to the end of the sample.
		ULONG loopLength = loop.loopLength;
		if (loop.loopStatus && loop.loopLength == 0)
		{
			loopLength = dataLength - loop.loopStart;
		}

		ULONG loopStart =  (loop.loopStartMeasure==LM_BYTES) ?			//In sample chunk, the value is in number of samples
			(ULONG)((loop.loopStart * compressionRatio) / origFormatBytesPerSamp) ://(16/8) :		//if the val is a raw offset of the original format, multiply it by the compression ratio
			loop.loopStart /** origFormatBytesPerSamp * compressionRatio / origFormatBytesPerSamp*/;//(16/8);	 //if the value is in samples, multiply by bytes per sample to get raw offset of original format, then multiply by compression ratio to get raw offset in standard wav, then divide bytes bytesPerSamp of standard wave (2)
		ULONG loopLenInSamp = (loop.loopLengthMeasure==LM_BYTES) ?		//In sample chunk, the value is in number of samples
			(ULONG)((loopLength * compressionRatio) / origFormatBytesPerSamp) ://(16/8)  :	//if it's in raw bytes, multiply by compressionRatio to convert to Raw length in new format, and divide by bytesPerSamp of a 16 bit standard wave (16bits/8 bits per byte)
			loopLength /** origFormatBytesPerSamp * compressionRatio) / origFormatBytesPerSamp*/;//(16/8);
		ULONG loopEnd = loopStart + loopLenInSamp;

		PushTypeOnVectBE<UINT>(waveBuf, 0x736D706C);		//"smpl"
		PushTypeOnVect<UINT>(waveBuf, 0x50);				//size
		PushTypeOnVect<UINT>(waveBuf, 0);					//manufacturer
		PushTypeOnVect<UINT>(waveBuf, 0);					//product
		PushTypeOnVect<UINT>(waveBuf, 1000000000/rate);		//sample period
		PushTypeOnVect<UINT>(waveBuf, 60);					//MIDI uniti note (C5)
		PushTypeOnVect<UINT>(waveBuf, 0);					//MIDI pitch fraction
		PushTypeOnVect<UINT>(waveBuf, 0);					//SMPTE format
		PushTypeOnVect<UINT>(waveBuf, 0);					//SMPTE offset
		PushTypeOnVect<UINT>(waveBuf, 1);					//sample loops
		PushTypeOnVect<UINT>(waveBuf, 0);					//sampler data
		PushTypeOnVect<UINT>(waveBuf, 0);					//cue point ID
		PushTypeOnVect<UINT>(waveBuf, 0);					//type (loop forward)
		PushTypeOnVect<UINT>(waveBuf, loopStart);			//start sample #
		PushTypeOnVect<UINT>(waveBuf, loopEnd);				//end sample #
		PushTypeOnVect<UINT>(waveBuf, 0);					//fraction
		PushTypeOnVect<UINT>(waveBuf, 0);					//playcount
	}
	
	//DLSWave dlswave(1, channels, rate, rate*blockAlign, blockAlign, bps, bufSize, uncompSampBuf);
	//dlswave.Write(waveBuf);
	return pRoot->UI_WriteBufferToFile(filepath, &waveBuf[0], waveBuf.size());
}
