#include "stdafx.h"
#include "SuzukiSnesScanner.h"
#include "SuzukiSnesSeq.h"

void SuzukiSnesScanner::Scan(RawFile* file, void* info)
{
	SearchForSuzukiSnesSeq(file);
	return;
}

void SuzukiSnesScanner::SearchForSuzukiSnesSeq (RawFile* file)
{
	uint32_t nFileLength = file->size();
	if (nFileLength < 0x10000 || nFileLength > 0x10500)
		return;

	SuzukiSnesSeq* newSeq = new SuzukiSnesSeq(file, 0x2000, 0x5F10);
	if (!newSeq->LoadVGMFile())
		delete newSeq;
}
