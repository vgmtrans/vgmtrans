#include "stdafx.h"
#include "AkaoScanner.h"
#include "AkaoSeq.h"
#include "AkaoInstr.h"

AkaoScanner::AkaoScanner(void)
{
}

AkaoScanner::~AkaoScanner(void)
{
}

void AkaoScanner::Scan(RawFile* file, void* info)
{
	UINT nFileLength = file->size();
	for (UINT i=0; i+0x60<nFileLength; i++)
	{
		if (file->GetWordBE(i) != 0x414B414F)	//sig must match ascii characters "AKAO"
			continue;

		if ( file->GetWord(i+0x20) != 0 && file->GetWord(i+8) != 0)		//0x20 contains the num of tracks, via num positive bits
		{
			if (file->GetWord(i+0x2C) != 0 || file->GetWord(i+0x28) != 0)
				continue;
			if (file->GetWord(i+0x38) != 0 || file->GetWord(i+0x3C) != 0)
				continue;
			if (file->GetShort(i+6) == 0)				//sequence length value must != 0
				continue;

			AkaoSeq* NewAkaoSeq = new AkaoSeq(file, i);
			NewAkaoSeq->LoadVGMFile();
		}
		else
		{
			if (file->GetWord(i+8)    != 0 || file->GetWord(i+0x0C) != 0 ||
				file->GetWord(i+0x24) != 0 || file->GetWord(i+0x28) != 0 || file->GetWord(i+0x2C) != 0 &&
				file->GetWord(i+0x30) != 0 || file->GetWord(i+0x34) != 0 || file->GetWord(i+0x38) != 0 &&
				file->GetWord(i+0x3C) != 0 ||
				file->GetWord(i+0x40) != 0 ||
				file->GetWord(i+0x4C) == 0)		//ADSR1 and ADSR2 will never be 0 in any real-world case.
				//file->GetWord(i+0x50) == 0 || file->GetWord(i+0x54) == 00) //Chrono Cross 311 is exception to this :-/
				continue;

			AkaoSampColl* sampColl = new AkaoSampColl(file, i, 0);
			sampColl->LoadVGMFile();
		}
	}
	return;
}
