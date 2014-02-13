#include "stdafx.h"
#include "SquSnesScanner.h"
#include "SquSnesSeq.h"
//#include "AkaoInstr.h"

void SquSnesScanner::Scan(RawFile* file, void* info)
{
	SearchForSquSnesSeq(file);
	return;
}



void SquSnesScanner::SearchForSquSnesSeq (RawFile* file)
{
	uint32_t nFileLength = file->size();
	if (nFileLength < 0x10000 || nFileLength > 0x10500)
		return;


	SquSnesSeq* newSeq = new SquSnesSeq(file, 0x2000, 0x5F10);
	if (!newSeq->LoadVGMFile())
		delete newSeq;
}