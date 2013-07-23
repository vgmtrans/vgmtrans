#include "stdafx.h"
#include "DLSFile.h"
#include "VGMInstrSet.h"
#include "VGMSamp.h"
#include "common.h"
#include "Root.h"
#include "RiffFile.h"

//void WriteLIST(vector<BYTE> & buf, UINT listName, UINT listSize)
//{
//	PushTypeOnVectBE<UINT>(buf, 0x4C495354);	//write "LIST"
//	PushTypeOnVect<UINT>(buf, listSize);
//	PushTypeOnVectBE<UINT>(buf, listName);
//}
//
//
////Adds a null byte and ensures 16 bit alignment of a text string
//void AlignName(string &name)
//{
//	name += (char)0x00;
//	if (name.size() % 2)						//if the size of the name string is odd
//		name += (char)0x00;						//add another null byte
//}


//  *******
//  DLSFile
//  *******

DLSFile::DLSFile(string dls_name)
: RiffFile(dls_name, "DLS ")
{
}

DLSFile::~DLSFile(void)
{
	DeleteVect(aInstrs);
	DeleteVect(aWaves);
}

DLSInstr* DLSFile::AddInstr(unsigned long bank, unsigned long instrNum)
{
	aInstrs.insert(aInstrs.end(), new DLSInstr(bank, instrNum, "Unnamed Instrument"));
	return aInstrs.back();
}

DLSInstr* DLSFile::AddInstr(unsigned long bank, unsigned long instrNum, string name)
{
	aInstrs.insert(aInstrs.end(), new DLSInstr(bank, instrNum, name));
	return aInstrs.back();
}

void DLSFile::DeleteInstr(unsigned long bank, unsigned long instrNum)
{

}

DLSWave* DLSFile::AddWave(USHORT formatTag, USHORT channels, int samplesPerSec, int aveBytesPerSec,
						  USHORT blockAlign, USHORT bitsPerSample, ULONG waveDataSize, unsigned char* waveData,
						  string name)
{
	aWaves.insert(aWaves.end(), new DLSWave(formatTag, channels, samplesPerSec, aveBytesPerSec, blockAlign, bitsPerSample, waveDataSize, waveData, name));
	return aWaves.back();
}

//GetSize returns total DLS size, including the "RIFF" header size
UINT DLSFile::GetSize(void)
{
	UINT size = 0;
	size += 12;										// "RIFF" + size + "DLS "
	size += COLH_SIZE;								//COLH chunk (collection chunk - tells how many instruments)
	size += LIST_HDR_SIZE;							//"lins" list (list of instruments - contains all the "ins " lists)

	for (UINT i = 0; i < aInstrs.size(); i++)
		size += aInstrs[i]->GetSize();				//each "ins " list
	size += 16;										// "ptbl" + size + cbSize + cCues
	size += (UINT)aWaves.size() * sizeof(ULONG);	//each wave gets a poolcue 
	size += LIST_HDR_SIZE;							//"wvpl" list (wave pool - contains all the "wave" lists)
	for (UINT i = 0; i < aWaves.size(); i++)
		size += aWaves[i]->GetSize();				//each "wave" list
	size += LIST_HDR_SIZE;							//"INFO" list
	size += 8;										//"INAM" + size
	size += (UINT)name.size();						//size of name string

	return size;
}



int DLSFile::WriteDLSToBuffer(vector<BYTE> &buf)
{
	UINT theUINT;

	PushTypeOnVectBE<UINT>(buf, 0x52494646);			//"RIFF"
	PushTypeOnVect<UINT>(buf, GetSize() - 8);			//size
	PushTypeOnVectBE<UINT>(buf, 0x444C5320);			//"DLS "

	PushTypeOnVectBE<UINT>(buf, 0x636F6C68);			//"colh "
	PushTypeOnVect<UINT>(buf, 4);						//size
	PushTypeOnVect<UINT>(buf, aInstrs.size());			//cInstruments - number of instruments
	theUINT = 4;										//account for 4 "lins" bytes
	for (UINT i = 0; i < aInstrs.size(); i++)
		theUINT += aInstrs[i]->GetSize();				//each "ins " list
	WriteLIST(buf, 0x6C696E73, theUINT);				//Write the "lins" LIST
	for (UINT i = 0; i < aInstrs.size(); i++)
		aInstrs[i]->Write(buf);							//Write each "ins " list

	PushTypeOnVectBE<UINT>(buf, 0x7074626C);			//"ptbl"
	theUINT = 8;
	theUINT += (UINT)aWaves.size() * sizeof(ULONG);		//each wave gets a poolcue 
	PushTypeOnVect<UINT>(buf, theUINT);					//size
	PushTypeOnVect<UINT>(buf, 8);						//cbSize
	PushTypeOnVect<UINT>(buf, aWaves.size());			//cCues
	theUINT = 0;
	for (UINT i=0; i<(UINT)aWaves.size(); i++)
	{
		PushTypeOnVect<UINT>(buf, theUINT);				//write the poolcue for each sample
		//hFile->Write(&theUINT, sizeof(UINT));			//write the poolcue for each sample
		theUINT += aWaves[i]->GetSize();				//increment the offset to the next wave
	}

	theUINT = 4;
	for (UINT i = 0; i < aWaves.size(); i++)
		theUINT += aWaves[i]->GetSize();				//each "wave" list
	WriteLIST(buf, 0x7776706C, theUINT);				//Write the "wvpl" LIST
	for (UINT i = 0; i < aWaves.size(); i++)
		aWaves[i]->Write(buf);							//Write each "wave" list

	theUINT = 12 + (UINT)name.size();					//"INFO" + "INAM" + size + the string size
	WriteLIST(buf, 0x494E464F, theUINT);				//write the "INFO" list
	PushTypeOnVectBE<UINT>(buf, 0x494E414D);			//"INAM"
	PushTypeOnVect<UINT>(buf, name.size());	//size
	PushBackStringOnVector(buf, name);		//The Instrument Name string

	return true;
}


//I should probably make this function part of a parent class for both Midi and DLS file
bool DLSFile::SaveDLSFile(const wchar_t* filepath)
{
	vector<BYTE> dlsBuf;
	WriteDLSToBuffer(dlsBuf);
	return pRoot->UI_WriteBufferToFile(filepath, &dlsBuf[0], dlsBuf.size());
}


//  *******
//  DLSInstr
//  ********


DLSInstr::DLSInstr(ULONG bank, ULONG instrument)
: ulBank(bank), ulInstrument(instrument), name("Unnamed Instrument")
{
	RiffFile::AlignName(name);
}

DLSInstr::DLSInstr(ULONG bank, ULONG instrument, string instrName)
: ulBank(bank), ulInstrument(instrument), name(instrName)
{
	RiffFile::AlignName(name);
}

DLSInstr::DLSInstr(ULONG bank, ULONG instrument, string instrName, vector<DLSRgn*> listRgns)
: ulBank(bank), ulInstrument(instrument), name(instrName)
{
	RiffFile::AlignName(name);
	aRgns = listRgns;
}

DLSInstr::~DLSInstr()
{
	DeleteVect(aRgns);
}

UINT DLSInstr::GetSize(void)
{
	UINT size = 0;
	size += LIST_HDR_SIZE;								//"ins " list
	size += INSH_SIZE;									//insh chunk
	size += LIST_HDR_SIZE;								//"lrgn" list
	for (UINT i = 0; i < aRgns.size(); i++)
		size += aRgns[i]->GetSize();					//each "rgn2" list
	size += LIST_HDR_SIZE;								//"INFO" list
	size += 8;											//"INAM" + size
	size += (UINT)name.size();							//size of name string

	return size;
}

void DLSInstr::Write(vector<BYTE> &buf)
{
	UINT theUINT;
	
	theUINT = GetSize() - 8;
	RiffFile::WriteLIST(buf, 0x696E7320, theUINT);				//write "ins " list
	PushTypeOnVectBE<UINT>(buf, 0x696E7368);			//"insh"
	PushTypeOnVect<UINT>(buf, INSH_SIZE - 8);			//size
	PushTypeOnVect<UINT>(buf, (UINT)aRgns.size());		//cRegions
	PushTypeOnVect<UINT>(buf, ulBank);					//ulBank
	PushTypeOnVect<UINT>(buf, ulInstrument);			//ulInstrument

	theUINT = 4;
	for (UINT i = 0; i < aRgns.size(); i++)
		theUINT += aRgns[i]->GetSize();					//get the size of each "rgn2" list
	RiffFile::WriteLIST(buf, 0x6C72676E, theUINT);				//write the "lrgn" list
	for (UINT i = 0; i < aRgns.size(); i++)
		aRgns[i]->Write(buf);							//write each "rgn2" list


	theUINT = 12 + (UINT)name.size();					//"INFO" + "INAM" + size + the string size
	RiffFile::WriteLIST(buf, 0x494E464F, theUINT);				//write the "INFO" list
	PushTypeOnVectBE<UINT>(buf, 0x494E414D);			//"INAM"
	theUINT = (UINT)name.size();
	PushTypeOnVect<UINT>(buf, theUINT);					//size
	PushBackStringOnVector(buf, name);					//The Instrument Name string
}

DLSRgn* DLSInstr::AddRgn(void)
{
	aRgns.insert(aRgns.end(), new DLSRgn());
	return aRgns.back();
}

DLSRgn* DLSInstr::AddRgn(DLSRgn rgn)
{
	DLSRgn* newRgn = new DLSRgn();
	*newRgn = rgn;
	aRgns.insert(aRgns.end(), newRgn);
	return aRgns.back();
}

//  ******
//  DLSRgn
//  ******

DLSRgn::~DLSRgn(void)
{
	if (Wsmp)
		delete Wsmp;
	if (Art)
		delete Art;
}

UINT DLSRgn::GetSize(void)
{
	UINT size = 0;
	size += LIST_HDR_SIZE;		//"rgn2" list
	size += RGNH_SIZE;			//rgnh chunk
	if (Wsmp)
		size += Wsmp->GetSize();
	size += WLNK_SIZE;
	if (Art)
		size += Art->GetSize();
	return size;
}

void DLSRgn::Write(vector<BYTE> &buf)
{
	RiffFile::WriteLIST(buf, 0x72676E32, (UINT)(GetSize() - 8));	//write "rgn2" list
	PushTypeOnVectBE<UINT>(buf, 0x72676E68);			//"rgnh"
	PushTypeOnVect<UINT>(buf, (UINT)(RGNH_SIZE - 8));	//size 
	PushTypeOnVect<USHORT>(buf, usKeyLow);				//usLow  (key)
	PushTypeOnVect<USHORT>(buf, usKeyHigh);				//usHigh (key)
	PushTypeOnVect<USHORT>(buf, usVelLow);				//usLow  (vel)
	PushTypeOnVect<USHORT>(buf, usVelHigh);				//usHigh (vel)
	PushTypeOnVect<USHORT>(buf, 1);						//fusOptions
	PushTypeOnVect<USHORT>(buf, 0);						//usKeyGroup

	//new for dls2
	PushTypeOnVect<USHORT>(buf, 1);						//NO CLUE

	if (Wsmp)
		Wsmp->Write(buf);								//write the "wsmp" chunk

	PushTypeOnVectBE<UINT>(buf, 0x776C6E6B);			//"wlnk"
	PushTypeOnVect<UINT>(buf, WLNK_SIZE-8);				//size	
	PushTypeOnVect<USHORT>(buf, fusOptions);			//fusOptions
	PushTypeOnVect<USHORT>(buf, usPhaseGroup);			//usPhaseGroup
	PushTypeOnVect<UINT>(buf, channel);					//ulChannel
	PushTypeOnVect<UINT>(buf, tableIndex);				//ulTableIndex

	if (Art)
		Art->Write(buf);

}

DLSArt* DLSRgn::AddArt(void)
{
	Art = new DLSArt();
	return Art;
}

DLSWsmp* DLSRgn::AddWsmp(void)
{
	Wsmp = new DLSWsmp();
	return Wsmp;
}

void DLSRgn::SetRanges(USHORT keyLow, USHORT keyHigh, USHORT velLow, USHORT velHigh)
{
	usKeyLow = keyLow;
	usKeyHigh = keyHigh;
	usVelLow = velLow;
	usVelHigh = velHigh;
}

void DLSRgn::SetWaveLinkInfo(USHORT options, USHORT phaseGroup, ULONG theChannel, ULONG theTableIndex)
{
	fusOptions = options;
	usPhaseGroup = phaseGroup;
	channel = theChannel;
	tableIndex = theTableIndex;
}

//  ******
//  DLSArt
//  ******

DLSArt::~DLSArt()
{
	DeleteVect(aConnBlocks);
}


UINT DLSArt::GetSize(void)
{
	UINT size = 0;
	size += LIST_HDR_SIZE;							//"lar2" list
	size += 16;										//"art2" chunk + size + cbSize + cConnectionBlocks
	for (UINT i = 0; i < aConnBlocks.size(); i++)
		size += aConnBlocks[i]->GetSize();			//each connection block
	return size;
}

void DLSArt::Write(vector<BYTE> &buf)
{
	RiffFile::WriteLIST(buf, 0x6C617232, GetSize() - 8);		//write "lar2" list
	PushTypeOnVectBE<UINT>(buf, 0x61727432);					//"art2"
	PushTypeOnVect<UINT>(buf, GetSize() - LIST_HDR_SIZE - 8);	//size
	PushTypeOnVect<UINT>(buf, 8);								//cbSize
	PushTypeOnVect<UINT>(buf, (UINT)aConnBlocks.size());		//cConnectionBlocks
	for (UINT i = 0; i < aConnBlocks.size(); i++)
		aConnBlocks[i]->Write(buf);								//each connection block
}


void DLSArt::AddADSR(long attack_time, USHORT atk_transform, long decay_time, long sustain_lev, long release_time, USHORT rls_transform)
{
	aConnBlocks.insert(aConnBlocks.end(), new ConnectionBlock(CONN_SRC_NONE, CONN_SRC_NONE, CONN_DST_EG1_ATTACKTIME, atk_transform, attack_time));
	aConnBlocks.insert(aConnBlocks.end(), new ConnectionBlock(CONN_SRC_NONE, CONN_SRC_NONE, CONN_DST_EG1_DECAYTIME, CONN_TRN_NONE, decay_time));
	aConnBlocks.insert(aConnBlocks.end(), new ConnectionBlock(CONN_SRC_NONE, CONN_SRC_NONE, CONN_DST_EG1_SUSTAINLEVEL, CONN_TRN_NONE, sustain_lev));
	aConnBlocks.insert(aConnBlocks.end(), new ConnectionBlock(CONN_SRC_NONE, CONN_SRC_NONE, CONN_DST_EG1_RELEASETIME, rls_transform, release_time));	
}

void DLSArt::AddPan(long pan)
{
	aConnBlocks.insert(aConnBlocks.end(), new ConnectionBlock(CONN_SRC_NONE, CONN_SRC_NONE, CONN_DST_PAN, CONN_TRN_NONE, pan));
}


//  ***************
//  ConnectionBlock
//  ***************

void ConnectionBlock::Write(vector<BYTE> &buf)
{
	PushTypeOnVect<USHORT>(buf, usSource);			//usSource
	PushTypeOnVect<USHORT>(buf, usControl);			//usControl
	PushTypeOnVect<USHORT>(buf, usDestination);		//usDestination
	PushTypeOnVect<USHORT>(buf, usTransform);		//usTransform
	PushTypeOnVect<LONG>(buf, lScale);				//lScale
}

//  *******
//  DLSWsmp
//  *******


UINT DLSWsmp::GetSize(void)
{
	UINT size = 0;
	size += 28;		//all the variables minus the loop info
	if (cSampleLoops)
		size += 16;	//plus the loop info
	return size;
}

void DLSWsmp::Write(vector<BYTE> &buf)
{
	PushTypeOnVectBE<UINT>(buf, 0x77736D70);			//"wsmp"
	PushTypeOnVect<UINT>(buf, GetSize()-8);				//size
	PushTypeOnVect<UINT>(buf, 20);						//cbSize (size of structure without loop record)
	PushTypeOnVect<USHORT>(buf, usUnityNote);			//usUnityNote
	PushTypeOnVect<SHORT>(buf, sFineTune);				//sFineTune
	PushTypeOnVect<LONG>(buf, lAttenuation);			//lAttenuation
	PushTypeOnVect<UINT>(buf, 1);						//fulOptions
	PushTypeOnVect<UINT>(buf, cSampleLoops);			//cSampleLoops
	if (cSampleLoops)									//if it loops, write the loop structure
	{
		PushTypeOnVect<ULONG>(buf, 16);
		PushTypeOnVect<ULONG>(buf, ulLoopType);			//ulLoopType
		PushTypeOnVect<ULONG>(buf, ulLoopStart);
		PushTypeOnVect<ULONG>(buf, ulLoopLength);
	}
}

void DLSWsmp::SetLoopInfo(Loop& loop, VGMSamp* samp)
{
	const int origFormatBytesPerSamp = samp->bps/8;
	double compressionRatio = samp->GetCompressionRatio();

	// If the sample loops, but the loop length is 0, then assume the length should
	// extend to the end of the sample.
	if (loop.loopStatus && loop.loopLength == 0)
		loop.loopLength = samp->dataLength - loop.loopStart;

	cSampleLoops = loop.loopStatus;
	ulLoopType = loop.loopType; 
	ulLoopStart =  (loop.loopStartMeasure==LM_BYTES) ?			//In DLS, the value is in number of samples
		(ULONG)((loop.loopStart * compressionRatio) / origFormatBytesPerSamp) ://(16/8) :		//if the val is a raw offset of the original format, multiply it by the compression ratio
		loop.loopStart /** origFormatBytesPerSamp * compressionRatio / origFormatBytesPerSamp*/;//(16/8);	 //if the value is in samples, multiply by bytes per sample to get raw offset of original format, then multiply by compression ratio to get raw offset in standard wav, then divide bytes bytesPerSamp of standard wave (2)
	ulLoopLength = (loop.loopLengthMeasure==LM_BYTES) ?			//In DLS, the value is in number of samples
		(ULONG)((loop.loopLength * compressionRatio) / origFormatBytesPerSamp) ://(16/8)  :	//if it's in raw bytes, multiply by compressionRatio to convert to Raw length in new format, and divide by bytesPerSamp of a 16 bit standard wave (16bits/8 bits per byte)
		loop.loopLength /** origFormatBytesPerSamp * compressionRatio) / origFormatBytesPerSamp*/;//(16/8);
}

void DLSWsmp::SetPitchInfo(USHORT unityNote, short fineTune, long attenuation)
{
	usUnityNote = unityNote;
	sFineTune = fineTune;
	lAttenuation = attenuation;
}


//  *******
//  DLSWave
//  *******

DLSWave::~DLSWave()
{
	if (Wsmp)
		delete Wsmp;
	if (data)
		delete data;
}

UINT DLSWave::GetSize()
{
	UINT size = 0;
	size += LIST_HDR_SIZE;								//"wave" list
	size += 8;											//"fmt " chunk + size
	size += 18;											//fmt chunk data
	if (Wsmp)
		size += Wsmp->GetSize();
	size += 8;											//"data" chunk + size
	size += this->GetSampleSize();//dataSize;			//size of sample data

	size += LIST_HDR_SIZE;								//"INFO" list
	size += 8;											//"INAM" + size
	size += (UINT)name.size();							//size of name string
	return size;
}

void DLSWave::Write(vector<BYTE> &buf)
{
	UINT theUINT;

	RiffFile::WriteLIST(buf, 0x77617665, GetSize()-8);	//write "wave" list
	PushTypeOnVectBE<UINT>(buf, 0x666D7420);			//"fmt "
	PushTypeOnVect<UINT>(buf, 18);						//size
	PushTypeOnVect<USHORT>(buf, wFormatTag);			//wFormatTag
	PushTypeOnVect<USHORT>(buf, wChannels);				//wChannels
	PushTypeOnVect<ULONG>(buf, dwSamplesPerSec);		//dwSamplesPerSec
	PushTypeOnVect<ULONG>(buf, dwAveBytesPerSec);		//dwAveBytesPerSec
	PushTypeOnVect<USHORT>(buf, wBlockAlign);			//wBlockAlign
	PushTypeOnVect<USHORT>(buf, wBitsPerSample);		//wBitsPerSample

	PushTypeOnVect<USHORT>(buf, 0);			//cbSize	DLS2 specific.  I don't know anything else

	if (Wsmp)
		Wsmp->Write(buf);								//write the "wsmp" chunk

	PushTypeOnVectBE<UINT>(buf, 0x64617461);			//"data"
	PushTypeOnVect<UINT>(buf, dataSize);			//size		this is the ACTUAL size, not the even-aligned size
	buf.insert(buf.end(), data, data+dataSize);	//Write the sample
	if (dataSize%2)
		buf.push_back(0);
	theUINT = 12 + (UINT)name.size();					//"INFO" + "INAM" + size + the string size
	RiffFile::WriteLIST(buf, 0x494E464F, theUINT);				//write the "INFO" list
	PushTypeOnVectBE<UINT>(buf, 0x494E414D);			//"INAM"
	PushTypeOnVect<UINT>(buf, name.size());				//size
	PushBackStringOnVector(buf, name);
}