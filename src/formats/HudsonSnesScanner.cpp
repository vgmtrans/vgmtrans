#include "stdafx.h"
#include "HudsonSnesScanner.h"
#include "HudsonSnesInstr.h"
#include "HudsonSnesSeq.h"
#include "SNESDSP.h"

BytePattern HudsonSnesScanner::ptnNoteLenTable(
	"\xc0\x60\x30\x18\x0c\x06\x03\x01"
	,
	"xxxxxxxx"
	,
	8);

//; Super Bomberman 2 SPC
//0b30: f6 6b 0f  mov   a,$0f6b+y
//0b33: c4 0d     mov   $0d,a
//0b35: fc        inc   y
//0b36: f6 6b 0f  mov   a,$0f6b+y
//0b39: c4 0e     mov   $0e,a
//0b3b: 2f d8     bra   $0b15
//0b3d: f6 6b 0f  mov   a,$0f6b+y
//0b40: c4 0f     mov   $0f,a
//0b42: fc        inc   y
//0b43: f6 6b 0f  mov   a,$0f6b+y
//0b46: c4 10     mov   $10,a
//0b48: 2f cb     bra   $0b15
//0b4a: f6 6b 0f  mov   a,$0f6b+y
//0b4d: c5 4b 01  mov   $014b,a
BytePattern HudsonSnesScanner::ptnGetSeqTableAddrV0(
	"\xf6\x6b\x0f\xc4\x0d\xfc\xf6\x6b"
	"\x0f\xc4\x0e\x2f\xd8\xf6\x6b\x0f"
	"\xc4\x0f\xfc\xf6\x6b\x0f\xc4\x10"
	"\x2f\xcb\xf6\x6b\x0f\xc5\x4b\x01"
	,
	"x??x?xx?"
	"?x?x?x??"
	"x?xx??x?"
	"x?x??x??"
	,
	32);

//; Super Bomberman 3 SPC
//08d0: e5 c2 07  mov   a,$07c2
//08d3: ec c3 07  mov   y,$07c3
//08d6: da 13     movw  $13,ya            ; set music structure address from $07c2/3 to $13/4
//08d8: e5 c4 07  mov   a,$07c4
//08db: ec c5 07  mov   y,$07c5
//08de: da 15     movw  $15,ya
//08e0: e5 c6 07  mov   a,$07c6
//08e3: ec c7 07  mov   y,$07c7
//08e6: da 17     movw  $17,ya
//08e8: e5 c8 07  mov   a,$07c8
//08eb: c5 5b 01  mov   $015b,a
//08ee: e5 c9 07  mov   a,$07c9
//08f1: c4 19     mov   $19,a
BytePattern HudsonSnesScanner::ptnGetSeqTableAddrV1V2(
	"\xe5\xc2\x07\xec\xc3\x07\xda\x13"
	"\xe5\xc4\x07\xec\xc5\x07\xda\x15"
	"\xe5\xc6\x07\xec\xc7\x07\xda\x17"
	"\xe5\xc8\x07\xc5\x5b\x01\xe5\xc9"
	"\x07\xc4\x19"
	,
	"x??x??x?"
	"x??x??x?"
	"x??x??x?"
	"x??x??x?"
	"?x?"
	,
	35);

//; Super Bomberman 2 SPC
//0bf8: e4 4b     mov   a,$4b
//0bfa: 8d 5d     mov   y,#$5d
//0bfc: 4f 0c     pcall $0c
BytePattern HudsonSnesScanner::ptnLoadDIRV0(
	"\xe4\x4b\x8d\x5d\x4f\x0c"
	,
	"x?xxxx"
	,
	6);

void HudsonSnesScanner::Scan(RawFile* file, void* info)
{
	uint32_t nFileLength = file->size();
	if (nFileLength == 0x10000)
	{
		SearchForHudsonSnesFromARAM(file);
	}
	else
	{
		SearchForHudsonSnesFromROM(file);
	}
	return;
}

void HudsonSnesScanner::SearchForHudsonSnesFromARAM(RawFile* file)
{
	HudsonSnesVersion version = HUDSONSNES_NONE;
	std::wstring name = file->tag.HasTitle() ? file->tag.title : RawFile::removeExtFromPath(file->GetFileName());

	// search for note length table
	UINT ofsNoteLenTable;
	if (!file->SearchBytePattern(ptnNoteLenTable, ofsNoteLenTable)) {
		return;
	}

	// TODO: fix "An American Tail: Fievel Goes West"

	// search song list and detect engine version
	UINT ofsGetSeqTableAddr;
	uint16_t addrEngineHeader;
	uint16_t addrSongList;
	if (file->SearchBytePattern(ptnGetSeqTableAddrV1V2, ofsGetSeqTableAddr)) {
		addrEngineHeader = file->GetShort(ofsGetSeqTableAddr + 1);

		if (addrEngineHeader == 0x07c2) {
			version = HUDSONSNES_V1;
		}
		else if (addrEngineHeader == 0x0803) {
			version = HUDSONSNES_V2;
		}
		else {
			return;
		}

		addrSongList = file->GetShort(addrEngineHeader);
	}
	else if (file->SearchBytePattern(ptnGetSeqTableAddrV0, ofsGetSeqTableAddr)) {
		uint8_t addrSongListPtr = file->GetByte(ofsGetSeqTableAddr + 4);
		addrSongList = file->GetShort(addrSongListPtr);
		addrEngineHeader = 0; // N/A
		version = HUDSONSNES_V0;
	}
	else {
		return;
	}

	// load song(s)
	for (int songIndex = 0; songIndex < 1; songIndex++) {
		UINT ofsSongPtr = addrSongList + songIndex * 2;

		// address range check
		if (ofsSongPtr > 0x10000) {
			break;
		}

		// get song pointer
		uint16_t addrSongPtr = file->GetShort(ofsSongPtr);
		if (addrSongPtr + 2 > 0x10000) {
			break;
		}

		// get header address
		uint16_t addrSeqHeader = file->GetShort(addrSongPtr);
		HudsonSnesSeq* newSeq = new HudsonSnesSeq(file, version, addrSeqHeader, name);
		if (!newSeq->LoadVGMFile()) {
			delete newSeq;
			break;
		}

		// load instrument set if available
		if (newSeq->InstrumentTableSize != 0) {
			uint16_t spcDirAddr;
			uint16_t addrSampTuningTable;
			if (version == HUDSONSNES_V0) {
				UINT ofsLoadDIR;
				if (file->SearchBytePattern(ptnLoadDIRV0, ofsLoadDIR)) {
					uint8_t addrDIRPtr = file->GetByte(ofsLoadDIR + 1);
					spcDirAddr = file->GetByte(0x100 + addrDIRPtr) << 8;

					uint16_t addrSampRegionPtr = file->GetShort(ofsGetSeqTableAddr + 30);
					addrSampTuningTable = (file->GetByte(addrSampRegionPtr) + 1) << 8;
				}
				else {
					return;
				}
			}
			else { // HUDSONSNES_V1, HUDSONSNES_V2
				spcDirAddr = file->GetByte(addrEngineHeader + 6) << 8;
				addrSampTuningTable = file->GetShort(addrEngineHeader + 4);
			}

			HudsonSnesInstrSet * newInstrSet = new HudsonSnesInstrSet(file, version, newSeq->InstrumentTableAddress, newSeq->InstrumentTableSize, spcDirAddr, addrSampTuningTable, name);
			if (!newInstrSet->LoadVGMFile()) {
				delete newInstrSet;
				return;
			}
		}
	}
}

void HudsonSnesScanner::SearchForHudsonSnesFromROM(RawFile* file)
{
}
