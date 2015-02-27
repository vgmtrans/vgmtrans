#include "stdafx.h"
#include "PrismSnesScanner.h"
#include "PrismSnesSeq.h"
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
BytePattern PrismSnesScanner::ptnLoadSeq(
	"\xf6\x00\x23\xc4\x06\xfc\xf6\x00"
	"\x23\xc4\x07\x8d\x00\xf7\x06\x30"
	"\xd4"
	,
	"x??x?xx?"
	"?x?xxx?x"
	"?"
	,
	17);

//; Dual Orb 2 SPC
//0b5d: 8d 00     mov   y,#$00
//0b5f: f7 26     mov   a,($26)+y         ; read vcmd
//0b61: 10 1f     bpl   $0b82
//0b63: 3a 26     incw  $26
//0b65: 68 a0     cmp   a,#$a0
//0b67: 90 08     bcc   $0b71
//0b69: 80        setc
//0b6a: a8 c0     sbc   a,#$c0            ; a0-b0 => crash
//0b6c: 1c        asl   a
//0b6d: 5d        mov   x,a
//0b6e: 1f ff 0e  jmp   ($0eff+x)
BytePattern PrismSnesScanner::ptnExecVCmd(
	"\x8d\x00\xf7\x26\x10\x1f\x3a\x26"
	"\x68\xa0\x90\x08\x80\xa8\xc0\x1c"
	"\x5d\x1f\xff\x0e"
	,
	"xxx?x?x?"
	"xxxxxxxx"
	"xx??"
	,
	20);

void PrismSnesScanner::Scan(RawFile* file, void* info)
{
	uint32_t nFileLength = file->size();
	if (nFileLength == 0x10000) {
		SearchForPrismSnesFromARAM(file);
	}
	else {
		SearchForPrismSnesFromROM(file);
	}
	return;
}

void PrismSnesScanner::SearchForPrismSnesFromARAM (RawFile* file)
{
	PrismSnesVersion version = PRISMSNES_NONE;
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

	UINT ofsExecVCmd;
	uint16_t addrVoiceCmdAddressTable;
	if (file->SearchBytePattern(ptnExecVCmd, ofsExecVCmd)) {
		addrVoiceCmdAddressTable = file->GetShort(ofsExecVCmd + 18);
		if (addrVoiceCmdAddressTable + (2 * 0x40) > 0x10000) {
			return;
		}
	}
	else {
		return;
	}

	// detect engine version
	if (file->GetShort(addrVoiceCmdAddressTable + 8) == file->GetShort(addrVoiceCmdAddressTable + 10)) {
		version = PRISMSNES_DO;
	}
	else {
		version = PRISMSNES_DO2;
	}

	// TODO: guess song index
	int8_t songIndex = 0;

	uint32_t addrSeqHeaderPtr = addrSeqList + (songIndex * 2);
	uint32_t addrSeqHeader = file->GetShort(addrSeqHeaderPtr);
	PrismSnesSeq* newSeq = new PrismSnesSeq(file, version, addrSeqHeader, name);
	if (!newSeq->LoadVGMFile()) {
		delete newSeq;
		return;
	}
}

void PrismSnesScanner::SearchForPrismSnesFromROM (RawFile* file)
{
}
