#include "stdafx.h"
#include "SonyPS2Scanner.h"
#include "SonyPS2Seq.h"
#include "SonyPS2InstrSet.h"

#define SRCH_BUF_SIZE 0x20000


void SonyPS2Scanner::Scan(RawFile* file, void* info)
{
	SearchForSeq(file);
	SearchForInstrSet(file);
	SearchForSampColl(file);
	return;
}

void SonyPS2Scanner::SearchForSeq (RawFile* file)
{
	UINT nFileLength = file->size();
	for (UINT i=0; i+0x40<nFileLength; i++)
	{
		U32 sig1 = file->GetWord(i);
		U32 sig2 = file->GetWord(i+4);
		if (sig1 != 0x53434549 || sig2 != 0x56657273)	// "SCEIVers" in ASCII
			continue;

		sig1 = file->GetWord(i + 0x10);
		sig2 = file->GetWord(i + 0x14);
		if (sig1 != 0x53434549 || sig2 != 0x53657175)	// "SCEISequ" in ASCII
			continue;

		sig1 = file->GetWord(i + 0x30);
		sig2 = file->GetWord(i + 0x34);
		if (sig1 != 0x53434549 || sig2 != 0x4D696469)	// "SCEIMidi" in ASCII
			continue;
		
		SonyPS2Seq* newSeq = new SonyPS2Seq(file, i);
		newSeq->LoadVGMFile();
	}
}

void SonyPS2Scanner::SearchForInstrSet (RawFile* file)
{
	UINT nFileLength = file->size();
	for (UINT i=0; i+0x40<nFileLength; i++)
	{
		U32 sig1 = file->GetWord(i);
		U32 sig2 = file->GetWord(i+4);
		if (sig1 != 0x53434549 || sig2 != 0x56657273)	// "SCEIVers" in ASCII
			continue;

		sig1 = file->GetWord(i + 0x10);
		sig2 = file->GetWord(i + 0x14);
		if (sig1 != 0x53434549 || sig2 != 0x48656164)	// "SCEIHead" in ASCII
			continue;

		sig1 = file->GetWord(i + 0x50);
		sig2 = file->GetWord(i + 0x54);
		if (sig1 != 0x53434549 || sig2 != 0x56616769)	// "SCEIVagi" in ASCII
			continue;
		
		SonyPS2InstrSet* newInstrSet= new SonyPS2InstrSet(file, i);
		newInstrSet->LoadVGMFile();
	}
}

void SonyPS2Scanner::SearchForSampColl (RawFile* file)
{
	UINT nFileLength = file->size();
	if (nFileLength < 32)
		return;

	if (StringToLower(file->GetExtension()) == L"bd")
	{
		// Hack for incorrectly ripped bd files.  Should ALWAYS start with 16 0x00 bytes (must... suppress... rage)
		// If it doesn't, we'll throw out this file and create a new one with the correct formating
		BYTE buf[16];
		file->GetBytes(0, 16, buf);
		int num = CountBytesOfVal(buf, 16, 0);		//The first 16 bytes must be all 0x00
		if (num != 16)
		{
			U32 newFileSize = file->size() + 16;
			BYTE* newdataBuf = new BYTE[newFileSize];
			file->GetBytes(0, file->size(), newdataBuf+16);
			memset(newdataBuf, 0, 16);
			pRoot->CreateVirtFile(newdataBuf, newFileSize, file->GetFileName(), file->GetParRawFileFullPath().c_str());
			return;
		}

		SonyPS2SampColl* sampColl = new SonyPS2SampColl(file, 0);
		//sampColl->LoadVGMFile();
		Format* fmt = sampColl->GetFormat();
		if (fmt)
			fmt->OnNewFile(sampColl);
	}

	//BYTE buf[32];
	//file->GetBytes(0, 32, buf);
	//int num = CountBytesOfVal(buf, 16, 0);		//The first 16 bytes must be all 0x00
	//if (num != 16)	
	//	return;
	//num = CountBytesOfVal(buf+16, 16, 0);	//It is either extremely unlikely or impossible for bytes 16-31 to be all 0
	//if (num == 16)
	//	return;

	//SonyPS2SampColl* sampColl = new SonyPS2SampColl(file, 0);
	//sampColl->LoadVGMFile();
}