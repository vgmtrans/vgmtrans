#include "stdafx.h"
#include "NinSnesScanner.h"
#include "NinSnesSeq.h"

#define SRCH_BUF_SIZE 0x20000

//; Yoshi's Island SPC
//; vcmd branches 80-ff
//0813: 68 e0     cmp   a,#$e0
//0815: 90 05     bcc   $081c
//0817: 3f 95 08  call  $0895             ; vcmds e0-ff
//081a: 2f b9     bra   $07d5
BytePattern NinSnesScanner::ptnBranchForVcmd(
	"\x68\xe0\x90\x05\x3f\x95\x08\x2f"
	"\xb9"
	,
	"x?xxx??x"
	"?"
	,
	9);

void NinSnesScanner::Scan(RawFile* file, void* info)
{
	SearchForNinSnesSeq(file);
	return;
}
/*
void NinSnesScanner::SearchForNinSnesSeq (RawFile* file)
{
	UINT nFileLength = file->size();
	for (UINT i=0; i+6<nFileLength; i++)
	{
		if ((*file)[i] == 'O' && (*file)[i+1] == 'r' && (*file)[i+2] == 'g' && (*file)[i+3] == '-' &&
			  (*file)[i+4] == '0' && (*file)[i+5] == '2')
		{
			if (file->GetShort(i+6))
			{
				OrgSeq* NewOrgSeq = new OrgSeq(file, i);
				NewOrgSeq->Load();
			}
		}
	}
}*/



void NinSnesScanner::SearchForNinSnesSeq (RawFile* file)
{
	ULONG nFileLength = file->size();
	if (nFileLength < 0x10000 || nFileLength > 0x10500)
		return;

	// a small logic to prevent a false positive
	UINT ofsBranchForVcmd;
	if (!file->SearchBytePattern(ptnBranchForVcmd, ofsBranchForVcmd))
	{
		return;
	}

	for (ULONG i = 0x100; i+1 < nFileLength; i++)
	{
		USHORT theShort2;
		USHORT theShort = file->GetShort(i);
		if (theShort > i && theShort < i + /*0x150*/0x200)
		{
			bool bFailed = false;
			int m = 0;
			while (true)
			{
				m+=2;
				if (i+m <= nFileLength-2)
				{
					theShort2 = file->GetShort(i+m);
					if (theShort2 > i+m && theShort2 < i+m+0x200/*0x150*/)
						continue;
					else if (theShort2 == 0 || theShort2 < 0x100)//== 0xFF)
						break;
					else
					{
						bFailed = true;
						break;
					}
				}
				else
				{
					bFailed = true;
					break;
				}
			}
			if (!bFailed)
			{
				for (int n=0; n<m; n+=2)
				{
					for (int p=0; p<8; p++)
					{
						theShort = file->GetShort(i+n);
						USHORT tempShort;
						if ((ULONG)(theShort+p*2+1) < nFileLength)
							tempShort = file->GetShort(theShort+p*2);
						else
						{
							bFailed = true;
							break;
						}
						if ((ULONG)(tempShort+1) < nFileLength)
						{
							if ((tempShort <= i || tempShort >= i+0x3000) && tempShort != 0)
								bFailed = true;
						}
					}
				}
				if (!bFailed)
				{
					NinSnesSeq* newSeq = new NinSnesSeq(file, i);
					newSeq->LoadVGMFile();
					i += m;
				}
			}
		}
	}
}