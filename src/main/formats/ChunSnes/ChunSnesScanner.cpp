/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "ChunSnesSeq.h"
#include "ChunSnesInstr.h"
#include "ScannerManager.h"

namespace vgmtrans::scanners {
ScannerRegistration<ChunSnesScanner> s_chun_snes("CHUNSNES", {"spc"});
}

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

//; Otogirisou SPC
//078e: db $6c,$f0 ; DSP FLG reset
//0790: db $7d,$01 ; echo delay 16ms
//0792: db $6d,$f7 ; echo start addr $f700
//0794: db $1c,$7f ; main volume R #$7f
//0796: db $0c,$7f ; main volume L #$7f
//0798: db $3c,$00 ; echo volume R zero
//079a: db $2c,$00 ; echo volume L zero
//079c: db $4c,$00 ; key on
//079e: db $5c,$00 ; key off
//07a0: db $6c,$23 ; DSP FLG echo on, noise clock 25 Hz
//07a2: db $0d,$00 ; echo feedback zero
//07a4: db $2d,$00 ; pitch modulation off
//07a6: db $3d,$00 ; noise off
//07a8: db $4d,$00 ; echo off
//07aa: db $5d,$07 ; sample dir $0700
//07ac: db $0d,$00 ; echo feedback zero
//07ae: db $00     ; end of table
BytePattern ChunSnesScanner::ptnDSPInitTable(
	"\x6c\xf0\x7d\x01\x6d\xf7\x1c\x7f"
	"\x0c\x7f\x3c\x00\x2c\x00\x4c\x00"
	"\x5c\x00\x6c\x23\x0d\x00\x2d\x00"
	"\x3d\x00\x4d\x00\x5d\x07\x0d\x00"
	"\x00"
	,
	"x?x?x?x?"
	"x?x?x?x?"
	"x?x?x?x?"
	"x?x?x?x?"
	"?"
	,
	33);

//; Otogirisou SPC
//1413: 3f aa 15  call  $15aa             ; arg1 (patch number)
//1416: 2d        push  a
//1417: f5 dd 03  mov   a,$03dd+x
//141a: fd        mov   y,a
//141b: f6 1d 05  mov   a,$051d+y
//141e: fd        mov   y,a
//141f: 4d        push  x
//1420: 8f 03 c4  mov   $c4,#$03
//1423: 8f 02 c5  mov   $c5,#$02          ; $c4 = instrument table
//1426: cd 00     mov   x,#$00
//1428: ad 00     cmp   y,#$00
//142a: f0 0e     beq   $143a             ; while y ~= 0 do
//142c: e7 c4     mov   a,($c4+x)         ;   read instrument count
//142e: bc        inc   a
//142f: 6d        push  y
//1430: 8d 00     mov   y,#$00
//1432: 7a c4     addw  ya,$c4            ;   skip (1 + instrument_count) bytes
//1434: da c4     movw  $c4,ya
//1436: ee        pop   y
//1437: dc        dec   y
//1438: 2f ee     bra   $1428             ; end
//143a: ce        pop   x
//143b: ae        pop   a                 ; patch number in A
//143c: bc        inc   a                 ; skip offset +0: number of instruments
//143d: fd        mov   y,a
//143e: f7 c4     mov   a,($c4)+y         ; read global instrument number
//1440: fd        mov   y,a
//1441: f6 67 05  mov   a,$0567+y
//1444: 68 ff     cmp   a,#$ff
//1446: d0 02     bne   $144a
//;
//1448: 00        nop
//1449: bc        inc   a
//; read sample info table (A=SRCN)
//144a: d5 0e 04  mov   $040e+x,a         ; save SRCN
//144d: 8d 04     mov   y,#$04
//144f: 3f 65 17  call  $1765             ; SRCN
//1452: 8d 08     mov   y,#$08
//1454: cf        mul   ya
//1455: 8f 2f c4  mov   $c4,#$2f
//1458: 8f 06 c5  mov   $c5,#$06          ; $062f = sample info table
//145b: 7a c4     addw  ya,$c4
//145d: da c4     movw  $c4,ya            ; $c4 = &SampInfoTable[patch * 8]
BytePattern ChunSnesScanner::ptnProgChangeVCmdSummer(
	"\x3f\xaa\x15\x2d\xf5\xdd\x03\xfd"
	"\xf6\x1d\x05\xfd\x4d\x8f\x03\xc4"
	"\x8f\x02\xc5\xcd\x00\xad\x00\xf0"
	"\x0e\xe7\xc4\xbc\x6d\x8d\x00\x7a"
	"\xc4\xda\xc4\xee\xdc\x2f\xee\xce"
	"\xae\xbc\xfd\xf7\xc4\xfd\xf6\x67"
	"\x05\x68\xff\xd0\x02\x00\xbc\xd5"
	"\x0e\x04\x8d\x04\x3f\x65\x17\x8d"
	"\x08\xcf\x8f\x2f\xc4\x8f\x06\xc5"
	"\x7a\xc4\xda\xc4"
	,
	"x??xx??x"
	"x??xxx??"
	"x??xxxxx"
	"xx?xxxxx"
	"?x?xxxxx"
	"xxxx?xx?"
	"?xxxxxxx"
	"??xxx??x"
	"xxx??x??"
	"x?x?"
	,
	76);

//; Dragon Quest 5 SPC
//182e: 3f bd 19  call  $19bd             ; arg1 (patch number)
//1831: 2d        push  a
//1832: f5 a3 02  mov   a,$02a3+x
//1835: fd        mov   y,a
//1836: f6 af 03  mov   a,$03af+y         ; instrument table selector
//1839: fd        mov   y,a
//183a: 4d        push  x
//183b: e5 e9 28  mov   a,$28e9
//183e: c4 a4     mov   $a4,a
//1840: e5 ea 28  mov   a,$28ea
//1843: c4 a5     mov   $a5,a             ; $a4 = instrument table
//1845: cd 00     mov   x,#$00
//1847: ad 00     cmp   y,#$00
//1849: f0 0f     beq   $185a             ; while y ~= 0 do
//184b: e7 a4     mov   a,($a4+x)         ;   read instrument count
//184d: bc        inc   a
//184e: bc        inc   a
//184f: 6d        push  y
//1850: 8d 00     mov   y,#$00
//1852: 7a a4     addw  ya,$a4            ;   skip (2 + instrument_count) bytes
//1854: da a4     movw  $a4,ya
//1856: ee        pop   y
//1857: dc        dec   y
//1858: 2f ed     bra   $1847             ; end
//185a: ce        pop   x
//185b: ae        pop   a                 ; patch number in A
//185c: bc        inc   a                 ; skip offset +0: number of instruments
//185d: bc        inc   a                 ; skip offset +1: ?
//185e: fd        mov   y,a
//185f: f7 a4     mov   a,($a4)+y         ; read global instrument number
//1861: 65 84 01  cmp   a,$0184
//1864: f0 08     beq   $186e
//1866: fd        mov   y,a
//1867: f6 ff 03  mov   a,$03ff+y         ; read SRCN by global instrument number
//186a: 68 ff     cmp   a,#$ff
//186c: d0 15     bne   $1883
//;
//186e: f5 cb 02  mov   a,$02cb+x
//1871: 08 80     or    a,#$80
//1873: d5 cb 02  mov   $02cb+x,a
//1876: 3f 9e 1a  call  $1a9e
//1879: 3f 3e 1c  call  $1c3e
//187c: b0 04     bcs   $1882
//187e: fd        mov   y,a
//187f: 3f 93 1a  call  $1a93
//1882: 6f        ret
//; read sample info table (A=SRCN)
//1883: d5 cc 02  mov   $02cc+x,a         ; save SRCN
//1886: 8d 04     mov   y,#$04
//1888: 3f 23 1b  call  $1b23             ; SRCN
//188b: 8d 08     mov   y,#$08
//188d: cf        mul   ya
//188e: 8f 35 a4  mov   $a4,#$35
//1891: 8f 05 a5  mov   $a5,#$05          ; $0535 = sample info table
//1894: 7a a4     addw  ya,$a4
//1896: da a4     movw  $a4,ya            ; $a4 = &SampInfoTable[patch * 8]
BytePattern ChunSnesScanner::ptnProgChangeVCmdWinter(
	"\x3f\xbd\x19\x2d\xf5\xa3\x02\xfd"
	"\xf6\xaf\x03\xfd\x4d\xe5\xe9\x28"
	"\xc4\xa4\xe5\xea\x28\xc4\xa5\xcd"
	"\x00\xad\x00\xf0\x0f\xe7\xa4\xbc"
	"\xbc\x6d\x8d\x00\x7a\xa4\xda\xa4"
	"\xee\xdc\x2f\xed\xce\xae\xbc\xbc"
	"\xfd\xf7\xa4\x65\x84\x01\xf0\x08"
	"\xfd\xf6\xff\x03\x68\xff\xd0\x15"
	"\xf5\xcb\x02\x08\x80\xd5\xcb\x02"
	"\x3f\x9e\x1a\x3f\x3e\x1c\xb0\x04"
	"\xfd\x3f\x93\x1a\x6f\xd5\xcc\x02"
	"\x8d\x04\x3f\x23\x1b\x8d\x08\xcf"
	"\x8f\x35\xa4\x8f\x05\xa5\x7a\xa4"
	"\xda\xa4"
	,
	"x??xx??x"
	"x??xxx??"
	"x?x??x?x"
	"xxxxxx?x"
	"xxxxx?x?"
	"xxxxxxxx"
	"xx?x??xx"
	"xx??xxxx"
	"x??xxx??"
	"x??x??xx"
	"xx??xx??"
	"xxx??xxx"
	"x??x??x?"
	"x?"
	,
	106);

void ChunSnesScanner::scan(RawFile* file, void* /*info*/) {
  size_t nFileLength = file->size();
  if (nFileLength == 0x10000) {
    searchForChunSnesFromARAM(file);
  }
  else {
    // Search from ROM is unimplemented
  }
  return;
}

void ChunSnesScanner::searchForChunSnesFromARAM(RawFile *file) {
  ChunSnesVersion version;
  ChunSnesMinorVersion minorVersion;
  std::string name = file->tag.hasTitle() ? file->tag.title : file->stem();

  // search song list and detect engine version
  uint32_t ofsLoadSeq;
  uint16_t addrSongList;
  if (file->searchBytePattern(ptnLoadSeqWinterV3, ofsLoadSeq)) {
    addrSongList = file->readByte(ofsLoadSeq + 8) | (file->readByte(ofsLoadSeq + 11) << 8);
    version = CHUNSNES_WINTER;
    minorVersion = CHUNSNES_WINTER_V3;
  }
  else if (file->searchBytePattern(ptnLoadSeqWinterV1V2, ofsLoadSeq)) {
    addrSongList = file->readByte(ofsLoadSeq + 8) | (file->readByte(ofsLoadSeq + 11) << 8);
    version = CHUNSNES_WINTER;
    minorVersion = CHUNSNES_WINTER_V1; // TODO: classify V1/V2
  }
  else if (file->searchBytePattern(ptnLoadSeqSummerV2, ofsLoadSeq)) {
    uint16_t addrSongListPtr = file->readShort(ofsLoadSeq + 8);
    if (addrSongListPtr + 2 > 0x10000) {
      return;
    }

    addrSongList = file->readShort(addrSongListPtr);
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
  uint32_t ofsSaveSongIndex;
  if (file->searchBytePattern(ptnSaveSongIndexSummerV2, ofsSaveSongIndex)) {
    uint16_t addrSongIndexArray = file->readShort(ofsSaveSongIndex + 6);
    uint16_t addrSongSlotIndex = file->readShort(ofsSaveSongIndex + 9);
    uint8_t songSlotIndex = file->readByte(addrSongSlotIndex);
    songIndex = file->readByte(addrSongIndexArray + songSlotIndex);
  }
  else {
    // read voice stream pointer value of the first track
    uint16_t addrCurrentPos = file->readShort(0x0000);
    uint16_t bestDistance = 0xffff;

    // search the nearest address
    if (addrCurrentPos != 0) {
      int8_t guessedSongIndex = -1;

      for (int8_t songIndexCandidate = 0; songIndexCandidate < 0x7f; songIndexCandidate++) {
        uint16_t addrSeqEntry = addrSongList + (songIndexCandidate * CHUNSNES_SEQENT_SIZE);
        if ((addrSeqEntry & 0xff00) == 0 || (addrSeqEntry & 0xff00) == 0xff00) {
          continue;
        }

        uint16_t addrSeqHeader = file->readShort(addrSeqEntry + CHUNSNES_SEQENT_OFFSET_OF_HEADER);
        if ((addrSeqHeader & 0xff00) == 0 || (addrSeqHeader & 0xff00) == 0xff00) {
          continue;
        }

        uint8_t nNumTracks = file->readByte(addrSeqHeader + 1);
        if (nNumTracks == 0 || nNumTracks > 8) {
          continue;
        }

        uint16_t ofsTrackStart = file->readShort(addrSeqHeader + 2);
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

  uint16_t addrSeqHeader = file->readShort(addrSeqEntry + CHUNSNES_SEQENT_OFFSET_OF_HEADER);
  ChunSnesSeq *newSeq = new ChunSnesSeq(file, version, minorVersion, addrSeqHeader, name);
  if (!newSeq->loadVGMFile()) {
    delete newSeq;
    return;
  }

  uint32_t ofsDSPInitTable;
  uint16_t spcDirAddr;
  if (file->searchBytePattern(ptnDSPInitTable, ofsDSPInitTable)) {
    spcDirAddr = file->readByte(ofsDSPInitTable + 29) << 8;
  }
  else {
    return;
  }

  uint32_t ofsProgChangeVCmd;
  uint16_t addrInstrSetTable;
  uint16_t addrSampNumTable;
  uint16_t addrSampleTable;
  uint8_t songSlotIndex;
  uint8_t instrSetIndex;
  if (file->searchBytePattern(ptnProgChangeVCmdSummer, ofsProgChangeVCmd)) {
    addrInstrSetTable = file->readByte(ofsProgChangeVCmd + 14) | (file->readByte(ofsProgChangeVCmd + 17) << 8);
    addrSampNumTable = file->readShort(ofsProgChangeVCmd + 47);
    addrSampleTable = file->readByte(ofsProgChangeVCmd + 67) | (file->readByte(ofsProgChangeVCmd + 70) << 8);

    uint16_t addrTrackSlotLookupPtr = file->readShort(ofsProgChangeVCmd + 5);
    songSlotIndex = file->readByte(addrTrackSlotLookupPtr);
    if (songSlotIndex == 0xff) {
      songSlotIndex = 0;
    }

    uint16_t addrInstrSetLookupPtr = file->readShort(ofsProgChangeVCmd + 9);
    instrSetIndex = file->readByte(addrInstrSetLookupPtr + songSlotIndex);
  }
  else if (file->searchBytePattern(ptnProgChangeVCmdWinter, ofsProgChangeVCmd)) {
    uint16_t addrInstrumentTablePtr = file->readShort(ofsProgChangeVCmd + 14);
    addrInstrSetTable = file->readShort(addrInstrumentTablePtr);
    addrSampNumTable = file->readShort(ofsProgChangeVCmd + 58);
    addrSampleTable = file->readByte(ofsProgChangeVCmd + 97) | (file->readByte(ofsProgChangeVCmd + 100) << 8);

    uint16_t addrTrackSlotLookupPtr = file->readShort(ofsProgChangeVCmd + 5);
    songSlotIndex = file->readByte(addrTrackSlotLookupPtr);
    if (songSlotIndex == 0xff) {
      songSlotIndex = 0;
    }

    uint16_t addrInstrSetLookupPtr = file->readShort(ofsProgChangeVCmd + 9);
    instrSetIndex = file->readByte(addrInstrSetLookupPtr + songSlotIndex);
  }
  else {
    return;
  }

  uint32_t addrInstrSet = addrInstrSetTable;
  for (uint8_t i = 0; i < instrSetIndex; i++) {
    if (addrInstrSet + 2 > 0x10000) {
      return;
    }

    uint8_t nNumInstrs = file->readByte(addrInstrSet);
    if (version == CHUNSNES_SUMMER) {
      addrInstrSet += 1;
    }
    else { // CHUNSNES_WINTER
      addrInstrSet += 2;
    }
    addrInstrSet += nNumInstrs;
  }

  ChunSnesInstrSet *newInstrSet = new ChunSnesInstrSet(file, version, addrInstrSet, addrSampNumTable, addrSampleTable, spcDirAddr);
  if (!newInstrSet->loadVGMFile()) {
    delete newInstrSet;
    return;
  }
}
