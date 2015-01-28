#include "stdafx.h"
#include "AkaoSnesScanner.h"
#include "AkaoSnesSeq.h"
#include "SNESDSP.h"

//; Final Fantasy 4 SPC
//0a0d: cd 0f     mov   x,#$0f
//0a0f: 8d 00     mov   y,#$00
//0a11: 9e        div   ya,x
//0a12: f8 27     mov   x,$27
//0a14: f6 b1 18  mov   a,$18b1+y
BytePattern AkaoSnesScanner::ptnReadNoteLengthV1(
	"\xcd\x0f\x8d\x00\x9e\xf8\x27\xf6"
	"\xb1\x18"
	,
	"xxxxxx?x"
	"??"
	,
	10);

//; Romancing SaGa SPC
//0a56: 8d 00     mov   y,#$00
//0a58: cd 0f     mov   x,#$0f
//0a5a: 9e        div   ya,x
//0a5b: f8 06     mov   x,$06
//0a5d: f6 f4 19  mov   a,$19f4+y
BytePattern AkaoSnesScanner::ptnReadNoteLengthV2(
	"\x8d\x00\xcd\x0f\x9e\xf8\x06\xf6"
	"\xf4\x19"
	,
	"xxxxxx?x"
	"??"
	,
	10);

//; Romancing SaGa 2 SPC
//03d2: cd 0e     mov   x,#$0e
//03d4: 9e        div   ya,x
//03d5: f8 a2     mov   x,$a2
//03d7: f6 aa 16  mov   a,$16aa+y
BytePattern AkaoSnesScanner::ptnReadNoteLengthV4(
	"\xcd\x0e\x9e\xf8\xa2\xf6\xaa\x16"
	,
	"xxxx?x??"
	,
	8);

//; Final Fantasy 4 SPC
//; This pattern is applicable to earlier V4 engines (e.g. Romancing SaGa 2)
//0c71: a8 d2     sbc   a,#$d2
//0c73: 1c        asl   a
//0c74: fd        mov   y,a
//0c75: f6 ee 17  mov   a,$17ee+y
//0c78: 2d        push  a
//0c79: f6 ed 17  mov   a,$17ed+y
//0c7c: 2d        push  a
//0c7d: dd        mov   a,y
//0c7e: 5c        lsr   a
//0c7f: fd        mov   y,a
//0c80: f6 49 18  mov   a,$1849+y
//0c83: f0 0a     beq   $0c8f
BytePattern AkaoSnesScanner::ptnVCmdExecFF4(
	"\xa8\xd2\x1c\xfd\xf6\xee\x17\x2d"
	"\xf6\xed\x17\x2d\xdd\x5c\xfd\xf6"
	"\x49\x18\xf0\x0a"
	,
	"x?xxx??x"
	"x??xxxxx"
	"????"
	,
	20);

//; Romancing SaGa 3 SPC
//0731: a8 c4     sbc   a,#$c4
//0733: c4 a6     mov   $a6,a
//0735: 1c        asl   a
//0736: fd        mov   y,a
//0737: f6 56 16  mov   a,$1656+y
//073a: 2d        push  a
//073b: f6 55 16  mov   a,$1655+y
//073e: 2d        push  a
//073f: eb a6     mov   y,$a6
//0741: f6 cd 16  mov   a,$16cd+y
//0744: d0 01     bne   $0747
BytePattern AkaoSnesScanner::ptnVCmdExecRS3(
	"\xa8\xc4\xc4\xa6\x1c\xfd\xf6\x56"
	"\x16\x2d\xf6\x55\x16\x2d\xeb\xa6"
	"\xf6\xcd\x16\xd0\x01"
	,
	"x?x?xxx?"
	"?xx??xx?"
	"x????"
	,
	21);

//; Final Fantasy 4 SPC
//1530: 8d 01     mov   y,#$01
//1532: cb 8d     mov   $8d,y
//1534: cd 00     mov   x,#$00
//1536: f5 00 20  mov   a,$2000+x
//1539: d4 02     mov   $02+x,a
//153b: f5 01 20  mov   a,$2001+x
//153e: d4 03     mov   $03+x,a
//1540: f0 0a     beq   $154c
//1542: db 48     mov   $48+x,y
BytePattern AkaoSnesScanner::ptnReadSeqHeaderV1(
	"\x8d\x01\xcb\x8d\xcd\x00\xf5\x00"
	"\x20\xd4\x02\xf5\x01\x20\xd4\x03"
	"\xf0\x0a\xdb\x48"
	,
	"xxx?xxx?"
	"?x?x??x?"
	"x?x?"
	,
	20);

//; Romancing SaGa SPC
//14a5: cd 00     mov   x,#$00
//14a7: 8d 00     mov   y,#$00
//14a9: 8f 01 93  mov   $93,#$01
//14ac: f5 01 20  mov   a,$2001+x
//14af: f0 27     beq   $14d8
//14b1: 09 93 8e  or    ($8e),($93)
//14b4: d4 08     mov   $08+x,a
//14b6: f5 00 20  mov   a,$2000+x
//14b9: d4 07     mov   $07+x,a
BytePattern AkaoSnesScanner::ptnReadSeqHeaderV2(
	"\xcd\x00\x8d\x00\x8f\x01\x93\xf5"
	"\x01\x20\xf0\x27\x09\x93\x8e\xd4"
	"\x08\xf5\x00\x20\xd4\x07"
	,
	"xxxxxx?x"
	"??x?x??x"
	"?x??x?"
	,
	22);

//; Final Fantasy: Mystic Quest SPC
//0df9: cd 10     mov   x,#$10
//0dfb: f5 ff 1b  mov   a,$1bff+x
//0dfe: d4 0d     mov   $0d+x,a
//0e00: 1d        dec   x
//0e01: d0 f8     bne   $0dfb
//0e03: e8 12     mov   a,#$12
//0e05: 8d 1c     mov   y,#$1c
//0e07: 9a 0e     subw  ya,$0e
//0e09: da 08     movw  $08,ya
//0e0b: cd 0e     mov   x,#$0e
//0e0d: 8f 80 c1  mov   $c1,#$80
//0e10: e5 10 1c  mov   a,$1c10
//0e13: ec 11 1c  mov   y,$1c11
//0e16: da 36     movw  $36,ya
BytePattern AkaoSnesScanner::ptnReadSeqHeaderFFMQ(
	"\xcd\x10\xf5\xff\x1b\xd4\x0d\x1d"
	"\xd0\xf8\xe8\x12\x8d\x1c\x9a\x0e"
	"\xda\x08\xcd\x0e\x8f\x80\xc1\xe5"
	"\x10\x1c\xec\x11\x1c\xda\x36"
	,
	"xxx??x?x"
	"xxx?x?x?"
	"x?xxxx?x"
	"??x??x?"
	,
	31);

//; Romancing SaGa 2 SPC
//0a58: e5 00 1c  mov   a,$1c00           ; $1c00 - header start address
//0a5b: c4 00     mov   $00,a             
//0a5d: e5 01 1c  mov   a,$1c01           
//0a60: c4 01     mov   $01,a             ; header 00/1 - ROM address base
//0a62: e8 24     mov   a,#$24            
//0a64: 8d 1c     mov   y,#$1c            ; $1c24 - ARAM address base
//0a66: 9a 00     subw  ya,$00            
//0a68: da 00     movw  $00,ya            ; $00/1 - offset from ROM to ARAM address
BytePattern AkaoSnesScanner::ptnReadSeqHeaderV4(
	"\xe5\x00\x1c\xc4\x00\xe5\x01\x1c"
	"\xc4\x01\xe8\x24\x8d\x1c\x9a\x00"
	"\xda\x00"
	,
	"x??x?x??"
	"x?x?x?x?"
	"x?"
	,
	18);

void AkaoSnesScanner::Scan(RawFile* file, void* info)
{
	uint32_t nFileLength = file->size();
	if (nFileLength == 0x10000)
	{
		SearchForAkaoSnesFromARAM(file);
	}
	else
	{
		SearchForAkaoSnesFromROM(file);
	}
	return;
}

void AkaoSnesScanner::SearchForAkaoSnesFromARAM(RawFile* file)
{
	AkaoSnesVersion version = AKAOSNES_NONE;
	AkaoSnesMinorVersion minorVersion = AKAOSNES_NOMINORVERSION;
	std::wstring name = file->tag.HasTitle() ? file->tag.title : RawFile::removeExtFromPath(file->GetFileName());

	// search for note length table
	UINT ofsReadNoteLength;
	uint16_t addrNoteLengthTable;
	AkaoSnesVersion verReadNoteLength = AKAOSNES_NONE;
	if (file->SearchBytePattern(ptnReadNoteLengthV4, ofsReadNoteLength)) {
		addrNoteLengthTable = file->GetShort(ofsReadNoteLength + 6);
		verReadNoteLength = AKAOSNES_V4;
	}
	else if (file->SearchBytePattern(ptnReadNoteLengthV2, ofsReadNoteLength)) {
		addrNoteLengthTable = file->GetShort(ofsReadNoteLength + 8);
		verReadNoteLength = AKAOSNES_V2;
	}
	else if (file->SearchBytePattern(ptnReadNoteLengthV1, ofsReadNoteLength)) {
		addrNoteLengthTable = file->GetShort(ofsReadNoteLength + 8);
		verReadNoteLength = AKAOSNES_V1;
	}
	else {
		return;
	}

	// search for vcmd address/length table
	UINT ofsVCmdExec;
	uint8_t firstVCmd;
	uint16_t addrVCmdAddressTable;
	uint16_t addrVCmdLengthTable;
	if (file->SearchBytePattern(ptnVCmdExecRS3, ofsVCmdExec)) {
		firstVCmd = file->GetByte(ofsVCmdExec + 1);
		addrVCmdAddressTable = file->GetShort(ofsVCmdExec + 11);
		addrVCmdLengthTable = file->GetShort(ofsVCmdExec + 17);
	}
	else if (file->SearchBytePattern(ptnVCmdExecFF4, ofsVCmdExec)) {
		firstVCmd = file->GetByte(ofsVCmdExec + 1);
		addrVCmdAddressTable = file->GetShort(ofsVCmdExec + 9);
		addrVCmdLengthTable = file->GetShort(ofsVCmdExec + 16);
	}
	else {
		return;
	}

	// search for sequence header and some major format type differences
	UINT ofsReadSeqHeader;
	uint16_t addrSeqHeader;
	uint16_t addrAPURelocBase;
	bool relocatable;
	if (file->SearchBytePattern(ptnReadSeqHeaderV4, ofsReadSeqHeader)) {
		addrSeqHeader = file->GetShort(ofsReadSeqHeader + 1);
		addrAPURelocBase = (file->GetByte(ofsReadSeqHeader + 13) << 8) | file->GetByte(ofsReadSeqHeader + 11);
		relocatable = true;
	}
	else if (file->SearchBytePattern(ptnReadSeqHeaderFFMQ, ofsReadSeqHeader)) {
		addrSeqHeader = file->GetShort(ofsReadSeqHeader + 3) + 1; // don't miss +1
		addrAPURelocBase = (file->GetByte(ofsReadSeqHeader + 13) << 8) | file->GetByte(ofsReadSeqHeader + 11);
		relocatable = true;
		minorVersion = AKAOSNES_V3_FFMQ;
	}
	else if (file->SearchBytePattern(ptnReadSeqHeaderV2, ofsReadSeqHeader)) {
		addrSeqHeader = file->GetShort(ofsReadSeqHeader + 18);
		addrAPURelocBase = addrSeqHeader;
		relocatable = false;
	}
	else if (file->SearchBytePattern(ptnReadSeqHeaderV1, ofsReadSeqHeader)) {
		addrSeqHeader = file->GetShort(ofsReadSeqHeader + 7);
		addrAPURelocBase = addrSeqHeader;
		relocatable = false;
	}
	else {
		return;
	}

	// classify major version
	if (verReadNoteLength == AKAOSNES_V1 && firstVCmd == 0xd2 && !relocatable) {
		version = AKAOSNES_V1;
	}
	else if (verReadNoteLength == AKAOSNES_V2 && firstVCmd == 0xd2 && !relocatable) {
		version = AKAOSNES_V2;
	}
	else if (verReadNoteLength == AKAOSNES_V2 && firstVCmd == 0xd2 && relocatable) {
		version = AKAOSNES_V3;
	}
	else if (verReadNoteLength == AKAOSNES_V4 && firstVCmd == 0xc4 && relocatable) {
		version = AKAOSNES_V4;
	}
	else {
		return;
	}

	// optional: classify minor version
	const uint8_t FF4_VCMD_LEN_TABLE[] = { 0x03, 0x03, 0x01, 0x02, 0x03, 0x03, 0x03, 0x03, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x03, 0x02, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	const uint8_t RS1_VCMD_LEN_TABLE[] = { 0x01, 0x02, 0x01, 0x02, 0x01, 0x02, 0x01, 0x02, 0x01, 0x03, 0x00, 0x03, 0x00, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x02, 0x00, 0x01, 0x00, 0x00, 0x01, 0x00, 0x03, 0x02, 0x00, 0x01, 0x01, 0x00, 0x02, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	const uint8_t FF5_VCMD_LEN_TABLE[] = { 0x01, 0x02, 0x01, 0x02, 0x02, 0x03, 0x00, 0x03, 0x00, 0x02, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x01, 0x02, 0x01, 0x02, 0x02, 0x01, 0x03, 0x02, 0x02, 0x00, 0x00, 0x00, 0x00 };
	const uint8_t SD2_VCMD_LEN_TABLE[] = { 0x01, 0x02, 0x01, 0x02, 0x02, 0x03, 0x00, 0x03, 0x00, 0x02, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x01, 0x02, 0x01, 0x02, 0x02, 0x01, 0x03, 0x02, 0x02, 0x00, 0x01, 0x00, 0x00 };
	const uint8_t RS2_VCMD_LEN_TABLE[] = { 0x01, 0x02, 0x01, 0x02, 0x02, 0x03, 0x00, 0x03, 0x00, 0x02, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x01, 0x02, 0x02, 0x01, 0x03, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	const uint8_t LAL_VCMD_LEN_TABLE[] = { 0x01, 0x02, 0x01, 0x02, 0x02, 0x03, 0x00, 0x03, 0x00, 0x02, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x01, 0x02, 0x02, 0x01, 0x03, 0x02, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00 };
	const uint8_t FF6_VCMD_LEN_TABLE[] = { 0x01, 0x02, 0x01, 0x02, 0x02, 0x03, 0x00, 0x03, 0x00, 0x02, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x01, 0x02, 0x01, 0x03, 0x02, 0x02, 0x02, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00 };
	const uint8_t FM_VCMD_LEN_TABLE[]  = { 0x01, 0x02, 0x01, 0x02, 0x02, 0x03, 0x00, 0x03, 0x00, 0x02, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x01, 0x02, 0x01, 0x03, 0x02, 0x02, 0x02, 0x01, 0x03, 0x00, 0x00, 0x01, 0x00, 0x00 };
	const uint8_t RS3_VCMD_LEN_TABLE[] = { 0x01, 0x02, 0x01, 0x02, 0x02, 0x03, 0x00, 0x03, 0x00, 0x02, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x01, 0x02, 0x01, 0x03, 0x02, 0x01, 0x01, 0x01, 0x03, 0x00, 0x00, 0x01, 0x00, 0x00 };
	const uint8_t GH_VCMD_LEN_TABLE[]  = { 0x01, 0x02, 0x01, 0x02, 0x02, 0x03, 0x00, 0x03, 0x00, 0x02, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x01, 0x02, 0x01, 0x03, 0x02, 0x01, 0x01, 0x01, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00 };
	const uint8_t BSGAME_VCMD_LEN_TABLE[] = { 0x01, 0x02, 0x01, 0x02, 0x02, 0x03, 0x00, 0x03, 0x00, 0x02, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x01, 0x02, 0x01, 0x03, 0x02, 0x01, 0x01, 0x01, 0x03, 0x00, 0x00, 0x01, 0x00, 0x00 };
	if (file->MatchBytes(FF4_VCMD_LEN_TABLE, addrVCmdLengthTable, sizeof(FF4_VCMD_LEN_TABLE))) {
		minorVersion = AKAOSNES_V1_FF4;
	}
	else if (file->MatchBytes(RS1_VCMD_LEN_TABLE, addrVCmdLengthTable, sizeof(RS1_VCMD_LEN_TABLE))) {
		minorVersion = AKAOSNES_V2_RS1;
	}
	else if (file->MatchBytes(FF5_VCMD_LEN_TABLE, addrVCmdLengthTable, sizeof(FF5_VCMD_LEN_TABLE))) {
		if (minorVersion != AKAOSNES_V3_FFMQ) {
			minorVersion = AKAOSNES_V3_FF5;
		}
	}
	else if (file->MatchBytes(SD2_VCMD_LEN_TABLE, addrVCmdLengthTable, sizeof(SD2_VCMD_LEN_TABLE))) {
		minorVersion = AKAOSNES_V3_SD2;
	}
	else if (file->MatchBytes(RS2_VCMD_LEN_TABLE, addrVCmdLengthTable, sizeof(RS2_VCMD_LEN_TABLE))) {
		minorVersion = AKAOSNES_V4_RS2;
	}
	else if (file->MatchBytes(LAL_VCMD_LEN_TABLE, addrVCmdLengthTable, sizeof(LAL_VCMD_LEN_TABLE))) {
		minorVersion = AKAOSNES_V4_LAL;
	}
	else if (file->MatchBytes(FF6_VCMD_LEN_TABLE, addrVCmdLengthTable, sizeof(FF6_VCMD_LEN_TABLE))) {
		minorVersion = AKAOSNES_V4_FF6;
	}
	else if (file->MatchBytes(FM_VCMD_LEN_TABLE, addrVCmdLengthTable, sizeof(FM_VCMD_LEN_TABLE))) {
		//; Chrono Trigger SPC
		//1cf6: 28 0f     and   a,#$0f
		//1cf8: c4 7b     mov   $7b,a
		//1cfa: 6f        ret
		BytePattern ptnVCmdF9CT(
			"\x28\x0f\xc4\x7b\x6f"
			,
			"xxx?x"
			,
			5);

		uint16_t addrVCmdF9 = file->GetShort(addrVCmdAddressTable + 53 * 2);
		if (file->MatchBytePattern(ptnVCmdF9CT, addrVCmdF9)) {
			minorVersion = AKAOSNES_V4_CT;
		}
		else {
			minorVersion = AKAOSNES_V4_FM;
		}
	}
	else if (file->MatchBytes(RS3_VCMD_LEN_TABLE, addrVCmdLengthTable, sizeof(RS3_VCMD_LEN_TABLE))) {
		minorVersion = AKAOSNES_V4_RS3;
	}
	else if (file->MatchBytes(GH_VCMD_LEN_TABLE, addrVCmdLengthTable, sizeof(GH_VCMD_LEN_TABLE))) {
		minorVersion = AKAOSNES_V4_GH;
	}
	else if (file->MatchBytes(BSGAME_VCMD_LEN_TABLE, addrVCmdLengthTable, sizeof(BSGAME_VCMD_LEN_TABLE))) {
		minorVersion = AKAOSNES_V4_BSGAME;
	}

	// TODO: gather more information if necessary (pan bitdepth, etc.)

	// load sequence
	AkaoSnesSeq* newSeq = new AkaoSnesSeq(file, version, minorVersion, addrSeqHeader, addrAPURelocBase, name);
	if (!newSeq->LoadVGMFile()) {
		delete newSeq;
		return;
	}
}

void AkaoSnesScanner::SearchForAkaoSnesFromROM(RawFile* file)
{
}
