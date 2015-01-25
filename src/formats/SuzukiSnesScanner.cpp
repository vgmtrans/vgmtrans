#include "stdafx.h"
#include "SuzukiSnesScanner.h"
#include "SuzukiSnesSeq.h"
#include "SNESDSP.h"

//; Seiken Densetsu 3 SPC
//038b: fa f5 5c  mov   ($5c),($f5)
//038e: fa 5c f5  mov   ($f5),($5c)
//0391: 3f 0f 0a  call  $0a0f
//0394: cd 00     mov   x,#$00
//0396: e4 1a     mov   a,$1a
//0398: 1c        asl   a
//0399: fd        mov   y,a
//039a: f5 00 20  mov   a,$2000+x
//039d: d6 79 1b  mov   $1b79+y,a
//03a0: f5 01 20  mov   a,$2001+x
//03a3: d6 7a 1b  mov   $1b7a+y,a
//03a6: 3d        inc   x
//03a7: 3d        inc   x
BytePattern SuzukiSnesScanner::ptnLoadSongV1(
	"\xfa\xf5\x5c\xfa\x5c\xf5\x3f\x0f"
	"\x0a\xcd\x00\xe4\x1a\x1c\xfd\xf5"
	"\x00\x20\xd6\x79\x1b\xf5\x01\x20"
	"\xd6\x7a\x1b\x3d\x3d"
	,
	"x??x??x?"
	"?xxx?xxx"
	"??x??x??"
	"x??xx"
	,
	29);

//; Bahamut Lagoon SPC
//038b: fa f5 5f  mov   ($5f),($f5)
//038e: 3f fe 09  call  $09fe
//0391: 3f 8a 04  call  $048a
//0394: 8f 08 06  mov   $06,#$08
//0397: e4 1d     mov   a,$1d
//0399: 1c        asl   a
//039a: 5d        mov   x,a
//039b: f6 00 20  mov   a,$2000+y
//039e: d5 4c 1b  mov   $1b4c+x,a
//03a1: f6 01 20  mov   a,$2001+y
//03a4: d5 4d 1b  mov   $1b4d+x,a
//03a7: 3d        inc   x
//03a8: 3d        inc   x
BytePattern SuzukiSnesScanner::ptnLoadSongV2(
	"\xfa\xf5\x5f\x3f\xfe\x09\x3f\x8a"
	"\x04\x8f\x08\x06\xe4\x1d\x1c\x5d"
	"\xf6\x00\x20\xd5\x4c\x1b\xf6\x01"
	"\x20\xd5\x4d\x1b\x3d\x3d"
	,
	"x??x??x?"
	"?x??x?xx"
	"x??x??x?"
	"?x??xx"
	,
	30);

void SuzukiSnesScanner::Scan(RawFile* file, void* info)
{
	uint32_t nFileLength = file->size();
	if (nFileLength == 0x10000)
	{
		SearchForSuzukiSnesFromARAM(file);
	}
	else
	{
		SearchForSuzukiSnesFromROM(file);
	}
	return;
}

void SuzukiSnesScanner::SearchForSuzukiSnesFromARAM(RawFile* file)
{
	SuzukiSnesVersion version = SUZUKISNES_NONE;
	std::wstring name = file->tag.HasTitle() ? file->tag.title : RawFile::removeExtFromPath(file->GetFileName());

	// search for note length table
	UINT ofsSongLoad;
	uint16_t addrSeqHeader;
	if (file->SearchBytePattern(ptnLoadSongV1, ofsSongLoad)) {
		addrSeqHeader = file->GetShort(ofsSongLoad + 16);
		version = SUZUKISNES_V1;
	}
	else if (file->SearchBytePattern(ptnLoadSongV2, ofsSongLoad)) {
		addrSeqHeader = file->GetShort(ofsSongLoad + 17);
		version = SUZUKISNES_V2;
	}
	else {
		return;
	}

	// load sequence
	SuzukiSnesSeq* newSeq = new SuzukiSnesSeq(file, version, addrSeqHeader, name);
	if (!newSeq->LoadVGMFile()) {
		delete newSeq;
		return;
	}
}

void SuzukiSnesScanner::SearchForSuzukiSnesFromROM(RawFile* file)
{
}
