#include "stdafx.h"
#include "PandoraBoxSnesScanner.h"
#include "PandoraBoxSnesSeq.h"
#include "SNESDSP.h"

// ; Kishin Kourinden Oni SPC
// f91d: 8d 10     mov   y,#$10
// f91f: fc        inc   y
// f920: f7 d3     mov   a,($d3)+y
// f922: dc        dec   y
// f923: 37 d3     and   a,($d3)+y
// f925: 68 ff     cmp   a,#$ff
// f927: f0 30     beq   $f959
BytePattern PandoraBoxSnesScanner::ptnLoadSeqKKO(
	"\x8d\x10\xfc\xf7\xd3\xdc\x37\xd3"
	"\x68\xff\xf0\x30"
	,
	"xxxx?xx?"
	"xxx?"
	,
	12);

// ; Traverse: Starlight and Prairie SPC
// f96f: 8d 10     mov   y,#$10
// f971: 7d        mov   a,x
// f972: f0 45     beq   $f9b9
// f974: 6d        push  y
// f975: f7 08     mov   a,($08)+y
// f977: fc        inc   y
// f978: c4 00     mov   $00,a
// f97a: f7 08     mov   a,($08)+y
// f97c: fc        inc   y
// f97d: c4 01     mov   $01,a
// f97f: bc        inc   a
// f980: f0 31     beq   $f9b3
BytePattern PandoraBoxSnesScanner::ptnLoadSeqTSP(
	"\x8d\x10\x7d\xf0\x45\x6d\xf7\x08"
	"\xfc\xc4\x00\xf7\x08\xfc\xc4\x01"
	"\xbc\xf0\x31"
	,
	"xxxx?xx?"
	"xx?x?xx?"
	"xx?"
	,
	19);

void PandoraBoxSnesScanner::Scan(RawFile* file, void* info)
{
	uint32_t nFileLength = file->size();
	if (nFileLength == 0x10000) {
		SearchForPandoraBoxSnesFromARAM(file);
	}
	else {
		SearchForPandoraBoxSnesFromROM(file);
	}
	return;
}

void PandoraBoxSnesScanner::SearchForPandoraBoxSnesFromARAM (RawFile* file)
{
	PandoraBoxSnesVersion version = PANDORABOXSNES_NONE;
	std::wstring name = file->tag.HasTitle() ? file->tag.title : RawFile::removeExtFromPath(file->GetFileName());

	UINT ofsLoadSeq;
	uint16_t addrSeqHeader;
	if (file->SearchBytePattern(ptnLoadSeqTSP, ofsLoadSeq)) {
		uint8_t addrSeqHeaderPtr = file->GetByte(ofsLoadSeq + 7);
		addrSeqHeader = file->GetShort(addrSeqHeaderPtr);
		version = PANDORABOXSNES_V2;
	}
	else if (file->SearchBytePattern(ptnLoadSeqKKO, ofsLoadSeq)) {
		uint8_t addrSeqHeaderPtr = file->GetByte(ofsLoadSeq + 4);
		addrSeqHeader = file->GetShort(addrSeqHeaderPtr);
		version = PANDORABOXSNES_V1;
	}
	else {
		return;
	}

	PandoraBoxSnesSeq* newSeq = new PandoraBoxSnesSeq(file, version, addrSeqHeader, name);
	if (!newSeq->LoadVGMFile()) {
		delete newSeq;
		return;
	}
}

void PandoraBoxSnesScanner::SearchForPandoraBoxSnesFromROM (RawFile* file)
{
}
