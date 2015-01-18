#include "stdafx.h"
#include "AkaoSnesScanner.h"
#include "AkaoSnesSeq.h"
#include "SNESDSP.h"

void AkaoSnesScanner::Scan(RawFile* file, void* info)
{
	uint32_t nFileLength = file->size();
	if (nFileLength == 0x10000)
	{
		SearchForAkaoSnesFromARAM(file);
	}
	else
	{
		SearchForAkaoSnesFromROM(file);
	}
	return;
}

void AkaoSnesScanner::SearchForAkaoSnesFromARAM(RawFile* file)
{
	AkaoSnesVersion version = AKAOSNES_NONE;
}

void AkaoSnesScanner::SearchForAkaoSnesFromROM(RawFile* file)
{
}
