#include "stdafx.h"
#include "ChunSnesScanner.h"
#include "ChunSnesSeq.h"
#include "SNESDSP.h"

//; Otogirisou SPC
//0eca: d5 1d 05  mov   $051d+x,a         ; $051D+X = A
//0ecd: c9 66 05  mov   $0566,x
//0ed0: 2d        push  a
//0ed1: e5 8f 21  mov   a,$218f
//0ed4: c4 c0     mov   $c0,a
//0ed6: e5 90 21  mov   a,$2190
//0ed9: c4 c1     mov   $c1,a             ; $C0/1 = $218F/90
//0edb: ae        pop   a
//0edc: 1c        asl   a
//0edd: 90 02     bcc   $0ee1
//;
//0edf: ab c1     inc   $c1
//0ee1: 60        clrc
//;
//0ee2: 84 c0     adc   a,$c0
//0ee4: c4 c0     mov   $c0,a             ; $C0/1 += ($051D+X * 2)
//0ee6: 90 02     bcc   $0eea
//0ee8: ab c1     inc   $c1
//0eea: 8d 00     mov   y,#$00
//0eec: f7 c0     mov   a,($c0)+y
//0eee: 2d        push  a
//0eef: fc        inc   y
//0ef0: f7 c0     mov   a,($c0)+y
//0ef2: c4 c1     mov   $c1,a
//0ef4: ae        pop   a
//0ef5: c4 c0     mov   $c0,a             ; $C0/1 = (WORD) [$C0] (sequence header)
//0ef7: 04 c1     or    a,$c1
//0ef9: f0 9a     beq   $0e95             ; no song
//0efb: e8 ff     mov   a,#$ff
//0efd: d5 15 05  mov   $0515+x,a
//0f00: e8 00     mov   a,#$00
//0f02: d5 45 05  mov   $0545+x,a
//0f05: d5 4d 05  mov   $054d+x,a
//0f08: d5 35 05  mov   $0535+x,a
//0f0b: d5 55 05  mov   $0555+x,a
//; read the sequence header
//0f0e: 8d 00     mov   y,#$00
//0f10: f7 c0     mov   a,($c0)+y         ; header+0: initial tempo
//0f12: fc        inc   y
//0f13: d5 25 05  mov   $0525+x,a
BytePattern ChunSnesScanner::ptnLoadSeqSummerV2(
	"\xd5\x1d\x05\xc9\x66\x05\x2d\xe5"
	"\x8f\x21\xc4\xc0\xe5\x90\x21\xc4"
	"\xc1\xae\x1c\x90\x02\xab\xc1\x60"
	"\x84\xc0\xc4\xc0\x90\x02\xab\xc1"
	"\x8d\x00\xf7\xc0\x2d\xfc\xf7\xc0"
	"\xc4\xc1\xae\xc4\xc0\x04\xc1\xf0"
	"\x9a\xe8\xff\xd5\x15\x05\xe8\x00"
	"\xd5\x45\x05\xd5\x4d\x05\xd5\x35"
	"\x05\xd5\x55\x05\x8d\x00\xf7\xc0"
	"\xfc\xd5\x25\x05"
	,
	"x??x??xx"
	"??x?x??x"
	"?xxxxx?x"
	"x?x?xxx?"
	"xxx?xxx?"
	"x?xx?x?x"
	"?xxx??xx"
	"x??x??x?"
	"?x??xxx?"
	"xx??"
	,
	76);

//; Dragon Quest 5 SPC
//1201: c9 f8 03  mov   $03f8,x
//1204: fd        mov   y,a               ; Y = A = $03AF+X
//1205: f6 c7 04  mov   a,$04c7+y
//1208: 8f d5 a0  mov   $a0,#$d5
//120b: 8f 05 a1  mov   $a1,#$05
//120e: 8d 06     mov   y,#$06
//1210: cf        mul   ya
//1211: 7a a0     addw  ya,$a0
//1213: da a0     movw  $a0,ya            ; $A0/1 = #$5D5 + ($04C7+Y * 6)
//1215: 8d 01     mov   y,#$01
//1217: f7 a0     mov   a,($a0)+y
//1219: 2d        push  a
//121a: fc        inc   y
//121b: f7 a0     mov   a,($a0)+y
//121d: c4 a1     mov   $a1,a
//121f: ae        pop   a
//1220: c4 a0     mov   $a0,a             ; $A0/1 = (WORD) [$A0]+1 (sequence header)
//1222: e8 00     mov   a,#$00
//1224: d5 df 03  mov   $03df+x,a
//1227: d5 c7 03  mov   $03c7+x,a
//122a: d5 e7 03  mov   $03e7+x,a
//122d: e8 02     mov   a,#$02
//122f: d5 d7 03  mov   $03d7+x,a
//; read the sequence header
//1232: 8d 00     mov   y,#$00
//1234: f7 a0     mov   a,($a0)+y         ; header+0: initial tempo
//1236: fc        inc   y
//1237: d5 b7 03  mov   $03b7+x,a
BytePattern ChunSnesScanner::ptnLoadSeqWinterV1V2(
	"\xc9\xf8\x03\xfd\xf6\xc7\x04\x8f"
	"\xd5\xa0\x8f\x05\xa1\x8d\x06\xcf"
	"\x7a\xa0\xda\xa0\x8d\x01\xf7\xa0"
	"\x2d\xfc\xf7\xa0\xc4\xa1\xae\xc4"
	"\xa0\xe8\x00\xd5\xdf\x03\xd5\xc7"
	"\x03\xd5\xe7\x03\xe8\x02\xd5\xd7"
	"\x03\x8d\x00\xf7\xa0\xfc\xd5\xb7"
	"\x03"
	,
	"x??xx??x"
	"??x??xxx"
	"x?x?xxx?"
	"xxx?x?xx"
	"?xxx??x?"
	"?x??xxx?"
	"?xxx?xx?"
	"?"
	,
	57);

//; Kamaitachi no Yoru SPC
//1473: c9 09 04  mov   $0409,x
//1476: fd        mov   y,a               ; Y = A = $03B7+X
//1477: f6 d8 04  mov   a,$04d8+y
//147a: 8f e6 a0  mov   $a0,#$e6
//147d: 8f 06 a1  mov   $a1,#$06
//1480: 8d 06     mov   y,#$06
//1482: cf        mul   ya
//1483: 7a a0     addw  ya,$a0
//1485: da a0     movw  $a0,ya            ; $A0/1 = #$06E6 + ($04D8+Y * 6)
//1487: 8d 05     mov   y,#$05
//1489: f7 a0     mov   a,($a0)+y
//148b: 08 08     or    a,#$08
//148d: d7 a0     mov   ($a0)+y,a
//148f: 8d 01     mov   y,#$01
//1491: f7 a0     mov   a,($a0)+y
//1493: 2d        push  a
//1494: fc        inc   y
//1495: f7 a0     mov   a,($a0)+y
//1497: c4 a1     mov   $a1,a
//1499: ae        pop   a
//149a: c4 a0     mov   $a0,a             ; $A0/1 = (WORD) [$A0]+1 (sequence header)
//149c: e8 00     mov   a,#$00
//149e: d5 ef 03  mov   $03ef+x,a
//14a1: d5 d7 03  mov   $03d7+x,a
//14a4: d5 f7 03  mov   $03f7+x,a
//14a7: e8 02     mov   a,#$02
//14a9: d5 e7 03  mov   $03e7+x,a
//; read the sequence header
//14ac: 8d 00     mov   y,#$00
//14ae: f7 a0     mov   a,($a0)+y         ; header+0: initial tempo
//14b0: fc        inc   y
//14b1: d5 bf 03  mov   $03bf+x,a
//14b4: d5 c7 03  mov   $03c7+x,a
BytePattern ChunSnesScanner::ptnLoadSeqWinterV3(
	"\xc9\x09\x04\xfd\xf6\xd8\x04\x8f"
	"\xe6\xa0\x8f\x06\xa1\x8d\x06\xcf"
	"\x7a\xa0\xda\xa0\x8d\x05\xf7\xa0"
	"\x08\x08\xd7\xa0\x8d\x01\xf7\xa0"
	"\x2d\xfc\xf7\xa0\xc4\xa1\xae\xc4"
	"\xa0\xe8\x00\xd5\xef\x03\xd5\xd7"
	"\x03\xd5\xf7\x03\xe8\x02\xd5\xe7"
	"\x03\x8d\x00\xf7\xa0\xfc\xd5\xbf"
	"\x03\xd5\xc7\x03"
	,
	"x??xx??x"
	"??x??xxx"
	"x?x?xxx?"
	"xxx?xxx?"
	"xxx?x?xx"
	"?xxx??x?"
	"?x??xxx?"
	"?xxx?xx?"
	"?x??"
	,
	68);

//; Otogirisou SPC
//0ec5: 3f c3 0f  call  $0fc3             ; set X >= 0, $0525+X != 0
//0ec8: b0 ce     bcs   $0e98
//0eca: d5 1d 05  mov   $051d+x,a         ; $051D+X = A
//0ecd: c9 66 05  mov   $0566,x
//0ed0: 2d        push  a
BytePattern ChunSnesScanner::ptnSaveSongIndexSummerV2(
	"\x3f\xc3\x0f\xb0\xce\xd5\x1d\x05"
	"\xc9\x66\x05\x2d"
	,
	"x??x?x??"
	"x??x"
	,
	12);

void ChunSnesScanner::Scan(RawFile* file, void* info)
{
	uint32_t nFileLength = file->size();
	if (nFileLength == 0x10000)
	{
		SearchForChunSnesFromARAM(file);
	}
	else
	{
		SearchForChunSnesFromROM(file);
	}
	return;
}

void ChunSnesScanner::SearchForChunSnesFromARAM(RawFile* file)
{
	ChunSnesVersion version = CHUNSNES_NONE;
	ChunSnesMinorVersion minorVersion = CHUNSNES_NOMINORVERSION;
	std::wstring name = file->tag.HasTitle() ? file->tag.title : RawFile::removeExtFromPath(file->GetFileName());

	// search song list and detect engine version
	UINT ofsLoadSeq;
	uint16_t addrSongList;
	if (file->SearchBytePattern(ptnLoadSeqWinterV3, ofsLoadSeq)) {
		addrSongList = file->GetByte(ofsLoadSeq + 8) | (file->GetByte(ofsLoadSeq + 11) << 8);
		version = CHUNSNES_WINTER;
		minorVersion = CHUNSNES_WINTER_V3;
	}
	else if (file->SearchBytePattern(ptnLoadSeqWinterV1V2, ofsLoadSeq)) {
		addrSongList = file->GetByte(ofsLoadSeq + 8) | (file->GetByte(ofsLoadSeq + 11) << 8);
		version = CHUNSNES_WINTER;
		minorVersion = CHUNSNES_WINTER_V1; // TODO: classify V1/V2
	}
	else if (file->SearchBytePattern(ptnLoadSeqSummerV2, ofsLoadSeq)) {
		uint16_t addrSongListPtr = file->GetShort(ofsLoadSeq + 8);
		if (addrSongListPtr + 2 > 0x10000) {
			return;
		}

		addrSongList = file->GetShort(addrSongListPtr);
		version = CHUNSNES_SUMMER;
		minorVersion = CHUNSNES_SUMMER_V2;
	}
	else {
		return;
	}

	// summer/winter const definitions
	uint8_t CHUNSNES_SEQENT_SIZE;
	uint8_t CHUNSNES_SEQENT_OFFSET_OF_HEADER;
	if (version == CHUNSNES_SUMMER) {
		CHUNSNES_SEQENT_SIZE = 2;
		CHUNSNES_SEQENT_OFFSET_OF_HEADER = 0;
	}
	else {
		CHUNSNES_SEQENT_SIZE = 6;
		CHUNSNES_SEQENT_OFFSET_OF_HEADER = 1;
	}

	// guess song index
	int8_t songIndex = -1;
	UINT ofsSaveSongIndex;
	if (file->SearchBytePattern(ptnSaveSongIndexSummerV2, ofsSaveSongIndex)) {
		uint16_t addrSongIndexArray = file->GetShort(ofsSaveSongIndex + 6);
		uint16_t addrSongSlotIndex = file->GetShort(ofsSaveSongIndex + 9);
		uint8_t songSlotIndex = file->GetByte(addrSongSlotIndex);
		songIndex = file->GetByte(addrSongIndexArray + songSlotIndex);
	}
	else {
		// read voice stream pointer value of the first track
		uint16_t addrCurrentPos = file->GetShort(0x0000);
		uint16_t bestDistance = 0xffff;

		// search the nearest address
		if (addrCurrentPos != 0) {
			int8_t guessedSongIndex = -1;

			for (int8_t songIndexCandidate = 0; songIndexCandidate < 0x7f; songIndexCandidate++) {
				uint16_t addrSeqEntry = addrSongList + (songIndexCandidate * CHUNSNES_SEQENT_SIZE);
				if ((addrSeqEntry & 0xff00) == 0 || (addrSeqEntry & 0xff00) == 0xff00) {
					continue;
				}

				uint16_t addrSeqHeader = file->GetShort(addrSeqEntry + CHUNSNES_SEQENT_OFFSET_OF_HEADER);
				if ((addrSeqHeader & 0xff00) == 0 || (addrSeqHeader & 0xff00) == 0xff00) {
					continue;
				}

				uint8_t nNumTracks = file->GetByte(addrSeqHeader + 1);
				if (nNumTracks == 0 || nNumTracks > 8) {
					continue;
				}

				uint16_t ofsTrackStart = file->GetShort(addrSeqHeader + 2);
				uint16_t addrTrackStart;
				if (version == CHUNSNES_SUMMER) {
					addrTrackStart = ofsTrackStart;
				}
				else {
					addrTrackStart = addrSeqHeader + ofsTrackStart;
				}

				if (addrTrackStart > addrCurrentPos) {
					continue;
				}

				uint16_t distance = addrCurrentPos - addrTrackStart;
				if (distance < bestDistance) {
					bestDistance = distance;
					guessedSongIndex = songIndexCandidate;
				}
			}

			if (guessedSongIndex != -1) {
				songIndex = guessedSongIndex;
			}
		}
	}

	if (songIndex == -1) {
		songIndex = 1;
	}

	uint16_t addrSeqEntry = addrSongList + (songIndex * CHUNSNES_SEQENT_SIZE);
	if (addrSeqEntry + CHUNSNES_SEQENT_SIZE > 0x10000) {
		return;
	}

	uint16_t addrSeqHeader = file->GetShort(addrSeqEntry + CHUNSNES_SEQENT_OFFSET_OF_HEADER);
	ChunSnesSeq* newSeq = new ChunSnesSeq(file, version, minorVersion, addrSeqHeader, name);
	if (!newSeq->LoadVGMFile()) {
		delete newSeq;
		return;
	}
}

void ChunSnesScanner::SearchForChunSnesFromROM(RawFile* file)
{
}
