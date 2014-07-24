#include "stdafx.h"
#include "KonamiSnesScanner.h"
#include "KonamiSnesSeq.h"
#include "KonamiSnesInstr.h"
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

//; Ganbare Goemon 4
//0266: 8f 5d f2  mov   $f2,#$5d
//0269: 8f 04 f3  mov   $f3,#$04          ; source dir = $0400
BytePattern KonamiSnesScanner::ptnSetDIR(
	"\x8f\x5d\xf2\x8f\x04\xf3"
	,
	"xxxx?x"
	,
	6);

//; Ganbare Goemon 4
//; vcmd e2 - set instrument
//1b95: 09 11 10  or    ($10),($11)
//1b98: fd        mov   y,a
//1b99: f5 a1 01  mov   a,$01a1+x
//1b9c: d0 27     bne   $1bc5
//1b9e: dd        mov   a,y
//1b9f: 68 28     cmp   a,#$28
//1ba1: b0 0c     bcs   $1baf             ; use another map if patch number is large
//1ba3: 8f 3c 04  mov   $04,#$3c
//1ba6: 8f 0a 05  mov   $05,#$0a          ; common sample map = #$0a3c
//1ba9: 3f ee 1b  call  $1bee
//1bac: 5f e2 18  jmp   $18e2
//; use another map
//1baf: a8 28     sbc   a,#$28            ; patch -= 0x28
//1bb1: 2d        push  a
//1bb2: eb 25     mov   y,$25             ; bank number
//1bb4: f6 20 0a  mov   a,$0a20+y
//1bb7: c4 04     mov   $04,a
//1bb9: f6 21 0a  mov   a,$0a21+y
//1bbc: c4 05     mov   $05,a             ; sample map = *(u16)($0a20 + bank * 2)
//1bbe: ae        pop   a
//1bbf: 3f ee 1b  call  $1bee
//1bc2: 5f e2 18  jmp   $18e2
BytePattern KonamiSnesScanner::ptnLoadInstr(
	"\x09\x11\x10\xfd\xf5\xa1\x01\xd0"
	"\x27\xdd\x68\x28\xb0\x0c\x8f\x3c"
	"\x04\x8f\x0a\x05\x3f\xee\x1b\x5f"
	"\xe2\x18\xa8\x28\x2d\xeb\x25\xf6"
	"\x20\x0a\xc4\x04\xf6\x21\x0a\xc4"
	"\x05\xae\x3f\xee\x1b\x5f\xe2\x18"
	,
	"x??xx??x"
	"?xx?xxx?"
	"?x??x??x"
	"??x?xx?x"
	"??x?x??x"
	"?xx??x??"
	,
	48);

//; Ganbare Goemon 4
//; load instrument for percussive note
//1be8: 8f e6 04  mov   $04,#$e6
//1beb: 8f 0d 05  mov   $05,#$0d          ; $04 = #$0de6
//; load instrument attributes from instrument table
//; a = patch number, $04 = instrument table address
//1bee: 8d 07     mov   y,#$07
//1bf0: cf        mul   ya
//1bf1: 7a 04     addw  ya,$04
//1bf3: da 04     movw  $04,ya            ; load address by index `$04 += (patch * 7)`
BytePattern KonamiSnesScanner::ptnLoadPercInstr(
	"\x8f\xe6\x04\x8f\x0d\x05\x8d\x07"
	"\xcf\x7a\x04\xda\x04"
	,
	"x??x??xx"
	"xx?x?"
	,
	13);

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
	UINT ofsSetDIRASM;
	UINT ofsLoadInstrASM;
	UINT ofsLoadPercInstrASM;
	uint16_t addrSongHeader;
	uint16_t addrCommonInstrTable;
	uint16_t addrBankedInstrTable;
	uint16_t addrPercInstrTable;
	uint16_t addrVoiceCmdLengthTable;
	uint16_t addrInstrTableBanks;
	uint16_t addrCurrentBank;
	uint8_t firstBankedInstr;

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

	// scan for DIR address
	// geez, it sometimes fails (gg4-11.spc, for example)
	uint8_t spcDIR;
	if (file->SearchBytePattern(ptnSetDIR, ofsSetDIRASM))
	{
		spcDIR = file->GetByte(ofsSetDIRASM + 4);
	}
	else
	{
		return;
	}

	// scan for instrument table
	if (file->SearchBytePattern(ptnLoadInstr, ofsLoadInstrASM))
	{
		addrCommonInstrTable = file->GetByte(ofsLoadInstrASM + 15) | (file->GetByte(ofsLoadInstrASM + 18) << 8);
		firstBankedInstr = file->GetByte(ofsLoadInstrASM + 11);
		addrCurrentBank = file->GetByte(ofsLoadInstrASM + 30);
		addrInstrTableBanks = file->GetShort(ofsLoadInstrASM + 32);
		addrBankedInstrTable = addrInstrTableBanks + (file->GetByte(addrCurrentBank) * 2);
	}
	else
	{
		return;
	}

	// scan for percussive instrument table
	if (file->SearchBytePattern(ptnLoadPercInstr, ofsLoadPercInstrASM))
	{
		addrPercInstrTable = file->GetByte(ofsLoadInstrASM + 1) | (file->GetByte(ofsLoadInstrASM + 4) << 8);
	}
	else
	{
		return;
	}

	KonamiSnesInstrSet * newInstrSet = new KonamiSnesInstrSet(file, addrCommonInstrTable, addrBankedInstrTable, firstBankedInstr, addrPercInstrTable, spcDIR << 8);
	if (!newInstrSet->LoadVGMFile())
	{
		delete newInstrSet;
		return;
	}
}

void KonamiSnesScanner::SearchForKonamiSnesFromROM (RawFile* file)
{
}
