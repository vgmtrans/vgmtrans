#include "stdafx.h"
#include "RareSnesScanner.h"
#include "RareSnesSeq.h"

//; Donkey Kong Country SPC
//1123: e8 01     mov   a,#$01
//1125: d4 3c     mov   $3c+x,a
//1127: d5 10 01  mov   $0110+x,a
//112a: f6 a0 12  mov   a,$12a0+y
//112d: d4 4c     mov   $4c+x,a
//112f: f6 a1 12  mov   a,$12a1+y
//1132: d4 5c     mov   $5c+x,a           ; set pointer for each track
BytePattern RareSnesScanner::ptnSongLoadDKC(
	"\xe8\x01\xd4\x3c\xd5\x10\x01\xf6"
	"\xa0\x12\xd4\x4c\xf6\xa1\x12\xd4"
	"\x5c"
	,
	"xxx?x??x"
	"??x?x??x"
	"?"
	,
	17);

//; Donkey Kong Country 2 SPC
//10a9: e8 01     mov   a,#$01
//10ab: d4 34     mov   $34+x,a
//10ad: d5 10 01  mov   $0110+x,a
//10b0: f7 e5     mov   a,($e5)+y
//10b2: d4 44     mov   $44+x,a
//10b4: fc        inc   y
//10b5: f7 e5     mov   a,($e5)+y
//10b7: d4 54     mov   $54+x,a           ; set pointer for each track
BytePattern RareSnesScanner::ptnSongLoadDKC2(
	"\xe8\x01\xd4\x34\xd5\x10\x01\xf7"
	"\xe5\xd4\x44\xfc\xf7\xe5\xd4\x54"
	,
	"xxx?x??x"
	"?x?xx?x?"
	,
	16);

//; Donkey Kong Country SPC
//078e: 8d 00     mov   y,#$00
//0790: f7 01     mov   a,($01)+y
//0792: 68 00     cmp   a,#$00
//0794: 30 06     bmi   $079c
//0796: 4d        push  x
//0797: 1c        asl   a
//0798: 5d        mov   x,a
//0799: 1f 0f 10  jmp   ($100f+x)
BytePattern RareSnesScanner::ptnVCmdExecDKC(
	"\x8d\x00\xf7\x01\x68\x00\x30\x06"
	"\x4d\x1c\x5d\x1f\x0f\x10"
	,
	"xxx?xxxx"
	"xxxx??"
	,
	14);

//;Donkey Kong Country 2 SPC
//0856: 8d 00     mov   y,#$00
//0858: f7 00     mov   a,($00)+y
//085a: 30 06     bmi   $0862
//085c: 4d        push  x
//085d: 1c        asl   a
//085e: 5d        mov   x,a
//085f: 1f a5 0f  jmp   ($0fa5+x)
BytePattern RareSnesScanner::ptnVCmdExecDKC2(
	"\x8d\x00\xf7\x00\x30\x06\x4d\x1c"
	"\x5d\x1f\xa5\x0f"
	,
	"xxx?xxxx"
	"xx??"
	,
	12);

void RareSnesScanner::Scan(RawFile* file, void* info)
{
	ULONG nFileLength = file->size();
	if (nFileLength == 0x10000)
	{
		SearchForRareSnesFromARAM(file);
	}
	else
	{
		SearchForRareSnesFromROM(file);
	}
	return;
}

void RareSnesScanner::SearchForRareSnesFromARAM (RawFile* file)
{
	RareSnesVersion version = NONE;
	UINT ofsSongLoadASM;
	UINT ofsVCmdExecASM;
	UINT addrSeqHeader;
	UINT addrVCmdTable;
	wstring name = RawFile::removeExtFromPath(file->GetFileName());

	if (file->SearchBytePattern(ptnSongLoadDKC2, ofsSongLoadASM))
	{
		addrSeqHeader = file->GetShort(file->GetByte(ofsSongLoadASM + 8));
	}
	else if (file->SearchBytePattern(ptnSongLoadDKC, ofsSongLoadASM) &&
		file->GetShort(ofsSongLoadASM + 13) == file->GetShort(ofsSongLoadASM + 8) + 1)
	{
		addrSeqHeader = file->GetShort(ofsSongLoadASM + 8);
	}
	else
	{
		return;
	}

	if (file->SearchBytePattern(ptnVCmdExecDKC2, ofsVCmdExecASM))
	{
		addrVCmdTable = file->GetShort(ofsVCmdExecASM + 10);
		if (file->GetShort(addrVCmdTable + (0x0c * 2)) != 0)
		{
			if (file->GetShort(addrVCmdTable + (0x11 * 2)) != 0)
			{
				version = WNRN;
			}
			else
			{
				version = DKC2;
			}
		}
		else
		{
			version = KI;
		}
	}
	else if (file->SearchBytePattern(ptnVCmdExecDKC, ofsVCmdExecASM))
	{
		addrVCmdTable = file->GetShort(ofsVCmdExecASM + 12);
		version = DKC;
	}
	else
	{
		return;
	}

	RareSnesSeq* newSeq = new RareSnesSeq(file, version, addrSeqHeader, name);
	if (!newSeq->LoadVGMFile())
	{
		delete newSeq;
		return;
	}

	for (int i = 0; i < newSeq->aInstrumentsUsed.size(); i++)
	{
		ATLTRACE("Instrument Used: %d\n", newSeq->aInstrumentsUsed[i]);
	}
}

void RareSnesScanner::SearchForRareSnesFromROM (RawFile* file)
{
}
