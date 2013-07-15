#include "stdafx.h"
#include "PS1SeqScanner.h"
#include "PS1Format.h"
#include "PS1Seq.h"
#include "Vab.h"
#include "PSXSPU.h"

#define SRCH_BUF_SIZE 0x20000

PS1SeqScanner::PS1SeqScanner(void)
{
}

PS1SeqScanner::~PS1SeqScanner(void)
{
}


void PS1SeqScanner::Scan(RawFile* file, void* info)
{
	SearchForPS1Seq(file);
	SearchForVab(file);
	PSXSampColl::SearchForPSXADPCM(file, PS1Format::name);
	return;
}

void PS1SeqScanner::SearchForPS1Seq (RawFile* file)
{
	UINT nFileLength = file->size();
	for (UINT i=0; i+4<nFileLength; i++)
	{
		if ((*file)[i] == 'p' && (*file)[i+1] == 'Q' && (*file)[i+2] == 'E' && (*file)[i+3] == 'S')
		{
			PS1Seq* newPS1Seq = new PS1Seq(file, i);
			newPS1Seq->LoadVGMFile();
		}
	}
}


void PS1SeqScanner::SearchForVab (RawFile* file)
{
	UINT nFileLength = file->size();
	for (UINT i=0; i+4<nFileLength; i++)
	{
		if ((*file)[i] == 'p' && (*file)[i+1] == 'B' && (*file)[i+2] == 'A' && (*file)[i+3] == 'V')
		{
			Vab* newVab = new Vab(file, i);
			newVab->LoadVGMFile();
		}
	}
}
