#include "stdafx.h"
#include "AkaoSnesScanner.h"
#include "AkaoSnesSeq.h"
//#include "AkaoInstr.h"

void AkaoSnesScanner::Scan(RawFile* file, void* info)
{
	SearchForAkaoSnesSeq(file);
	return;
}



void AkaoSnesScanner::SearchForAkaoSnesSeq (RawFile* file)
{
	uint32_t nFileLength = file->size();
	if (nFileLength < 0x10000 || nFileLength > 0x10500)
		return;


	AkaoSnesSeq* newSeq = new AkaoSnesSeq(file, 0x2000, 0x5F10);
	if (!newSeq->LoadVGMFile())
		delete newSeq;
}