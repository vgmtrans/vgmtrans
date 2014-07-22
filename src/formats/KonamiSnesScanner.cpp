#include "stdafx.h"
#include "KonamiSnesScanner.h"
#include "KonamiSnesSeq.h"
#include "SNESDSP.h"

using namespace std;

//; Ganbare Goemon 4
//13d3: 8f 1e 06  mov   $06,#$1e
//13d6: 8f 00 0a  mov   $0a,#$00
//13d9: 8f 39 0b  mov   $0b,#$39          ; set header address $3900 to $0a/b
//13dc: cd 00     mov   x,#$00
BytePattern KonamiSnesScanner::ptnSetSongHeaderAddress(
	"\x8f\x1e\x06\x8f\x00\x0a\x8f\x39"
	"\x0b\xcd\x00"
	,
	"x?xx?xx?"
	"xxx"
	,
	11);

//; Ganbare Goemon 4
//; dispatch vcmd (e0-ff)
//1947: 1c        asl   a
//1948: fd        mov   y,a
//1949: f6 bc 1a  mov   a,$1abc+y
//194c: 2d        push  a
//194d: f6 bb 1a  mov   a,$1abb+y
//1950: 2d        push  a                 ; push vcmd func address, as a return address
//1951: f6 fb 1a  mov   a,$1afb+y
//1954: f0 08     beq   $195e
BytePattern KonamiSnesScanner::ptnJumpToVcmd(
	"\x1c\xfd\xf6\xbc\x1a\x2d\xf6\xbb"
	"\x1a\x2d\xf6\xfb\x1a\xf0\x08"
	,
	"xxx??xx?"
	"?xx??x?"
	,
	15);

void KonamiSnesScanner::Scan(RawFile* file, void* info)
{
	uint32_t nFileLength = file->size();
	if (nFileLength == 0x10000)
	{
		SearchForKonamiSnesFromARAM(file);
	}
	else
	{
		SearchForKonamiSnesFromROM(file);
	}
	return;
}

void KonamiSnesScanner::SearchForKonamiSnesFromARAM (RawFile* file)
{
	KonamiSnesVersion version = KONAMISNES_NONE;

	bool hasSongList;
	UINT ofsSetSongHeaderAddressASM;
	UINT ofsJumpToVcmdASM;
	uint16_t addrSongHeader;
	uint16_t addrVoiceCmdLengthTable;

	wstring basefilename = RawFile::removeExtFromPath(file->GetFileName());
	wstring name = file->tag.HasTitle() ? file->tag.title : basefilename;

	// find a song header
	if (file->SearchBytePattern(ptnSetSongHeaderAddress, ofsSetSongHeaderAddressASM))
	{
		addrSongHeader = file->GetByte(ofsSetSongHeaderAddressASM + 4) | (file->GetByte(ofsSetSongHeaderAddressASM + 7) << 8);
		hasSongList = false;
	}
	else
	{
		return;
	}

	// find the vcmd length table
	if (file->SearchBytePattern(ptnJumpToVcmd, ofsJumpToVcmdASM))
	{
		addrVoiceCmdLengthTable = file->GetShort(ofsJumpToVcmdASM + 11);

		// check table length
		if (addrVoiceCmdLengthTable + 0x20 >= 0x10000)
		{
			return;
		}
	}
	else
	{
		return;
	}

	// detect revision by vcmd length
	if (file->GetByte(addrVoiceCmdLengthTable + (0xed - 0xe0)) == 3)
	{
		version = KONAMISNES_NORMAL_REV1;
	}
	else if (file->GetByte(addrVoiceCmdLengthTable + (0xfc - 0xe0)) == 2)
	{
		version = KONAMISNES_NORMAL_REV2;
	}
	else
	{
		version = KONAMISNES_NORMAL_REV3;
	}

	// load song(s)
	if (hasSongList)
	{
		// TODO: NYI - Pop'n Twinbee
		return;
	}
	else
	{
		KonamiSnesSeq* newSeq = new KonamiSnesSeq(file, version, addrSongHeader, name);
		if (!newSeq->LoadVGMFile())
		{
			delete newSeq;
			return;
		}
	}
}

void KonamiSnesScanner::SearchForKonamiSnesFromROM (RawFile* file)
{
}
