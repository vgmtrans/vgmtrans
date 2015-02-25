#include "stdafx.h"
#include "IMaxSnesScanner.h"
#include "IMaxSnesSeq.h"
#include "SNESDSP.h"

//; Dual Orb 2 SPC
//0a28: f6 00 23  mov   a,$2300+y
//0a2b: c4 06     mov   $06,a
//0a2d: fc        inc   y
//0a2e: f6 00 23  mov   a,$2300+y
//0a31: c4 07     mov   $07,a
//; zero track header item index
//0a33: 8d 00     mov   y,#$00
//; read header for each tracks
//0a35: f7 06     mov   a,($06)+y
//0a37: 30 d4     bmi   $0a0d             ; break if >= $80 (usually $ff)
BytePattern IMaxSnesScanner::ptnLoadSeq(
	"\xf6\x00\x23\xc4\x06\xfc\xf6\x00"
	"\x23\xc4\x07\x8d\x00\xf7\x06\x30"
	"\xd4"
	,
	"x??x?xx?"
	"?x?xxx?x"
	"?"
	,
	17);

void IMaxSnesScanner::Scan(RawFile* file, void* info)
{
	uint32_t nFileLength = file->size();
	if (nFileLength == 0x10000) {
		SearchForIMaxSnesFromARAM(file);
	}
	else {
		SearchForIMaxSnesFromROM(file);
	}
	return;
}

void IMaxSnesScanner::SearchForIMaxSnesFromARAM (RawFile* file)
{
	IMaxSnesVersion version = IMAXSNES_NONE;
	std::wstring name = file->tag.HasTitle() ? file->tag.title : RawFile::removeExtFromPath(file->GetFileName());

	// search song list
	UINT ofsLoadSeq;
	uint16_t addrSeqList;
	if (file->SearchBytePattern(ptnLoadSeq, ofsLoadSeq)) {
		addrSeqList = file->GetShort(ofsLoadSeq + 1);
	}
	else {
		return;
	}

	// detect engine version
	version = IMAXSNES_STANDARD;

	// TODO: guess song index
	int8_t songIndex = 0;

	uint32_t addrSeqHeaderPtr = addrSeqList + (songIndex * 2);
	uint32_t addrSeqHeader = file->GetShort(addrSeqHeaderPtr);
	IMaxSnesSeq* newSeq = new IMaxSnesSeq(file, version, addrSeqHeader, name);
	if (!newSeq->LoadVGMFile()) {
		delete newSeq;
		return;
	}
}

void IMaxSnesScanner::SearchForIMaxSnesFromROM (RawFile* file)
{
}
