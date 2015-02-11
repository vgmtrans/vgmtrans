#include "stdafx.h"
#include "HeartBeatSnesScanner.h"
#include "HeartBeatSnesSeq.h"
#include "SNESDSP.h"

//; Dragon Quest 6 SPC
//1b9c: ee        pop   y
//1b9d: f6 0d 0a  mov   a,$0a0d+y
//1ba0: c4 08     mov   $08,a
//1ba2: f6 19 0a  mov   a,$0a19+y
//1ba5: c4 09     mov   $09,a             ; set $0A0D/0A19+Y to $08/9
//1ba7: f8 36     mov   x,$36
//1ba9: dd        mov   a,y
//1baa: d5 e0 08  mov   $08e0+x,a
//1bad: 8d 00     mov   y,#$00            ; zero offset
BytePattern HeartBeatSnesScanner::ptnReadSongList(
	"\xee\xf6\x0d\x0a\xc4\x08\xf6\x19"
	"\x0a\xc4\x09\xf8\x36\xdd\xd5\xe0"
	"\x08\x8d\x00"
	,
	"xx??x?x?"
	"?x?x?xx?"
	"?xx"
	,
	19);

void HeartBeatSnesScanner::Scan(RawFile* file, void* info)
{
	uint32_t nFileLength = file->size();
	if (nFileLength == 0x10000)
	{
		SearchForHeartBeatSnesFromARAM(file);
	}
	else
	{
		SearchForHeartBeatSnesFromROM(file);
	}
	return;
}

void HeartBeatSnesScanner::SearchForHeartBeatSnesFromARAM(RawFile* file)
{
	HeartBeatSnesVersion version = HEARTBEATSNES_NONE;
	std::wstring name = file->tag.HasTitle() ? file->tag.title : RawFile::removeExtFromPath(file->GetFileName());

	// search song list
	UINT ofsReadSongList;
	uint16_t addrSongListLo;
	uint16_t addrSongListHi;
	uint8_t maxSongIndex;
	if (file->SearchBytePattern(ptnReadSongList, ofsReadSongList)) {
		addrSongListLo = file->GetShort(ofsReadSongList + 2);
		addrSongListHi = file->GetShort(ofsReadSongList + 7);

		if (addrSongListLo >= addrSongListHi || addrSongListHi - addrSongListLo >= 0x80) {
			return;
		}

		maxSongIndex = addrSongListHi - addrSongListLo;
		if (addrSongListHi + maxSongIndex > 0x10000) {
			return;
		}

		version = HEARTBEATSNES_STANDARD;
	}
	else {
		return;
	}

	// TODO: guess song index
	int8_t songIndex = 0;

	uint16_t addrSeqHeader = file->GetByte(addrSongListLo + songIndex) | (file->GetByte(addrSongListHi + songIndex) << 8);
	HeartBeatSnesSeq* newSeq = new HeartBeatSnesSeq(file, version, addrSeqHeader, name);
	if (!newSeq->LoadVGMFile()) {
		delete newSeq;
		return;
	}
}

void HeartBeatSnesScanner::SearchForHeartBeatSnesFromROM(RawFile* file)
{
}
