#include "stdafx.h"
#include "FFTScanner.h"
#include "FFT.h"
#include "FFTInstr.h"

#define SRCH_BUF_SIZE 0x20000

//==============================================================
//		Constructor
//--------------------------------------------------------------
FFTScanner::FFTScanner(void)
{
}

//==============================================================
//		Destructor
//--------------------------------------------------------------
FFTScanner::~FFTScanner(void)
{
}

//==============================================================
//		scan "smds" and "wds"
//--------------------------------------------------------------
void FFTScanner::Scan(RawFile* file, void* info)
{
	SearchForFFTSeq(file);
	SearchForFFTwds(file);
	return;
}

//==============================================================
//		scan "smds"		(Sequence)
//--------------------------------------------------------------
void FFTScanner::SearchForFFTSeq (RawFile* file)
{
	UINT nFileLength = file->size();
	for (UINT i=0; i+4<nFileLength; i++)
	{
		if (file->GetWordBE(i) != 0x736D6473)
			continue;
		if (file->GetShort(i+10) != 0 && file->GetShort(i+16) != 0)
			continue;

		FFTSeq* NewFFTSeq = new FFTSeq(file, i);
		NewFFTSeq->LoadVGMFile();
	}
}


//==============================================================
//		scan "wds"		(Instrumnt)
//--------------------------------------------------------------
//	memo:
//		object "SampColl" ‚ÍAclass "WdsInstrSet"“à‚Å¶¬‚·‚éB
//--------------------------------------------------------------
void FFTScanner::SearchForFFTwds (RawFile* file)
{
	UINT nFileLength = file->size();
	for (UINT i=0; i+0x30<nFileLength; i++)
	{
		U32 sig = file->GetWordBE(i);
		if (sig != 0x64776473 && sig != 0x77647320)
			continue;

		// The sample collection size must not be impossibly large
		if (file->GetWord(i+0x14) > 0x100000)
			continue;

		U32 hdrSize = file->GetWord(i+0x10);
		//First 0x10 bytes of sample section should be 0s
		if (file->GetWord(i+hdrSize) != 0 || file->GetWord(i+hdrSize+4) != 0 ||
			file->GetWord(i+hdrSize+8) != 0 || file->GetWord(i+hdrSize+12) != 0)
			continue;

		//if (size <= file->GetWord(i+0x10) || size <= file->GetWord(i+0x18))
		//	continue;

		WdsInstrSet* newWds = new WdsInstrSet(file, i);
		newWds->LoadVGMFile();
	}
}

