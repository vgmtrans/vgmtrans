#include "stdafx.h"
#include "NamcoSnesScanner.h"
#include "NamcoSnesSeq.h"
#include "SNESDSP.h"

// Wagan Paradise SPC
// 05cc: 68 60     cmp   a,#$60
// 05ce: b0 0a     bcs   $05da
// 05d0: cd 18     mov   x,#$18
// 05d2: d8 3c     mov   $3c,x
// 05d4: cd dc     mov   x,#$dc
// 05d6: d8 3d     mov   $3d,x             ; $dc18 - song list
// 05d8: 2f 0a     bra   $05e4
// 05da: cd 00     mov   x,#$00
// 05dc: d8 3c     mov   $3c,x
// 05de: cd f4     mov   x,#$f4
// 05e0: d8 3d     mov   $3d,x             ; $f400 - song list (sfx?)
// 05e2: 28 1f     and   a,#$1f
// 05e4: 8d 03     mov   y,#$03
// 05e6: cf        mul   ya
// 05e7: fd        mov   y,a
// 05e8: f7 3c     mov   a,($3c)+y         ; read slot index (priority?)
// 05ea: 1c        asl   a
BytePattern NamcoSnesScanner::ptnReadSongList(
	"\x68\x60\xb0\x0a\xcd\x18\xd8\x3c"
	"\xcd\xdc\xd8\x3d\x2f\x0a\xcd\x00"
	"\xd8\x3c\xcd\xf4\xd8\x3d\x28\x1f"
	"\x8d\x03\xcf\xfd\xf7\x3c\x1c"
	,
	"x?xxx?x?"
	"x?x?xxx?"
	"x?x?x?xx"
	"xxxxx?x"
	,
	31);

//; Wagan Paradise SPC
//0703: cd 00     mov   x,#$00
//0705: d8 df     mov   $df,x
//0707: d8 da     mov   $da,x
//0709: d8 db     mov   $db,x
//070b: d8 dc     mov   $dc,x
//070d: f4 49     mov   a,$49+x
//070f: c4 d6     mov   $d6,a             ; song number
//0711: d8 de     mov   $de,x             ; slot index (0~3?)
//0713: f0 05     beq   $071a
//0715: 10 4a     bpl   $0761
//0717: 5f 0a 09  jmp   $090a
BytePattern NamcoSnesScanner::ptnStartSong(
	"\xcd\x00\xd8\xdf\xd8\xda\xd8\xdb"
	"\xd8\xdc\xf4\x49\xc4\xd6\xd8\xde"
	"\xf0\x05\x10\x4a\x5f\x0a\x09"
	,
	"x?x?x?x?"
	"x?x?x?x?"
	"xxx?x??"
	,
	23);

void NamcoSnesScanner::Scan(RawFile* file, void* info)
{
	uint32_t nFileLength = file->size();
	if (nFileLength == 0x10000) {
		SearchForNamcoSnesFromARAM(file);
	}
	else {
		SearchForNamcoSnesFromROM(file);
	}
	return;
}

void NamcoSnesScanner::SearchForNamcoSnesFromARAM(RawFile* file)
{
	NamcoSnesVersion version = NAMCOSNES_NONE;
	std::wstring name = file->tag.HasTitle() ? file->tag.title : RawFile::removeExtFromPath(file->GetFileName());

	// search song list
	UINT ofsReadSongList;
	uint16_t addrSongList;
	if (file->SearchBytePattern(ptnReadSongList, ofsReadSongList)) {
		addrSongList = file->GetByte(ofsReadSongList + 5) | (file->GetByte(ofsReadSongList + 9) << 8);
		version = NAMCOSNES_STANDARD;
	}
	else {
		return;
	}

	// search song start sequence
	UINT ofsStartSong;
	uint8_t addrSongIndexArray;
	uint8_t addrSongSlotIndex;
	if (file->SearchBytePattern(ptnStartSong, ofsStartSong)) {
		addrSongIndexArray = file->GetByte(ofsStartSong + 11);
		addrSongSlotIndex = file->GetByte(ofsStartSong + 15);
	}
	else {
		return;
	}

	// determine song index
	uint8_t songSlot = file->GetByte(addrSongSlotIndex); // 0..3
	uint8_t songIndex = file->GetByte(addrSongIndexArray + songSlot);
	uint16_t addrSeqHeader = addrSongList + (songIndex * 3);
	if (addrSeqHeader + 3 < 0x10000) {
		if (file->GetByte(addrSeqHeader) > 3 || (file->GetShort(addrSeqHeader + 1) & 0xff00) == 0) {
			songIndex = 1;
		}
		addrSeqHeader = addrSongList + (songIndex * 3);
	}
	if (addrSeqHeader + 3 > 0x10000) {
		return;
	}

	uint16_t addrEventStart = file->GetShort(addrSeqHeader + 1);
	if (addrEventStart + 1 > 0x10000) {
		return;
	}

	NamcoSnesSeq* newSeq = new NamcoSnesSeq(file, version, addrEventStart, name);
	if (!newSeq->LoadVGMFile()) {
		delete newSeq;
		return;
	}
}

void NamcoSnesScanner::SearchForNamcoSnesFromROM(RawFile* file)
{
}
