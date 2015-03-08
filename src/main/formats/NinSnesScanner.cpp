#include "stdafx.h"
#include "NinSnesFormat.h"
#include "NinSnesScanner.h"
#include "NinSnesInstr.h"
#include "NinSnesSeq.h"

//; Yoshi's Island SPC
//; vcmd branches 80-ff
//0813: 68 e0     cmp   a,#$e0
//0815: 90 05     bcc   $081c
//0817: 3f 95 08  call  $0895             ; vcmds e0-ff
//081a: 2f b9     bra   $07d5
BytePattern NinSnesScanner::ptnBranchForVcmd(
	"\x68\xe0\x90\x05\x3f\x95\x08\x2f"
	"\xb9"
	,
	"x?xxx??x"
	"?"
	,
	9);

//; Yoshi's Island SPC
//0c91: 68 ef     cmp   a,#$ef
//0c93: f0 29     beq   $0cbe
//0c95: 68 e0     cmp   a,#$e0
//0c97: 90 30     bcc   $0cc9             ; next note (key-off the current note)
//0c99: 6d        push  y
//0c9a: fd        mov   y,a
//0c9b: ae        pop   a
//0c9c: 96 b2 0a  adc   a,$0ab2+y         ; vcmd lengths ($0af6) (skip vcmd by using oplens table)
BytePattern NinSnesScanner::ptnBranchForVcmdReadahead(
	"\x68\xef\xf0\x29\x68\xe0\x90\x30"
	"\x6d\xfd\xae\x96\xb2\x0a"
	,
	"x?x?x?x?"
	"xxxx??"
	,
	14);

//; Yoshi's Island SPC
//; dispatch vcmd in A (e0-ff)
//0895: 1c        asl   a                 ; e0-ff => c0-fe (8 bit)
//0896: fd        mov   y,a
//0897: f6 9d 0a  mov   a,$0a9d+y
//089a: 2d        push  a
//089b: f6 9c 0a  mov   a,$0a9c+y
//089e: 2d        push  a                 ; push jump address from table
//089f: dd        mov   a,y
//08a0: 5c        lsr   a
//08a1: fd        mov   y,a
//08a2: f6 32 0b  mov   a,$0b32+y         ; vcmd length
//08a5: f0 08     beq   $08af             ; if non zero
BytePattern NinSnesScanner::ptnJumpToVcmd(
	"\x1c\xfd\xf6\x9d\x0a\x2d\xf6\x9c"
	"\x0a\x2d\xdd\x5c\xfd\xf6\x32\x0b"
	"\xf0\x08"
	,
	"xxx??xx?"
	"?xxxxx??"
	"x?"
	,
	18);

//; Super Mario World SPC
//; dispatch vcmd in A (da-f2)
//0d40: 1c        asl   a
//0d41: 5d        mov   x,a
//0d42: e8 00     mov   a,#$00
//0d44: 1f dc 0e  jmp   ($0edc+x)
BytePattern NinSnesScanner::ptnJumpToVcmdSMW(
	"\x1c\x5d\xe8\x00\x1f\xdc\x0e"
	,
	"xxxxx??"
	,
	7);

//; Super Mario World SPC
//; read vcmd length table
//10c3: 68 da     cmp   a,#$da
//10c5: 90 0a     bcc   $10d1
//10c7: 6d        push  y
//10c8: fd        mov   y,a
//10c9: ae        pop   a
//10ca: 60        clrc
//10cb: 96 e8 0e  adc   a,$0ee8+y
//10ce: fd        mov   y,a
//10cf: 2f e3     bra   $10b4
BytePattern NinSnesScanner::ptnReadVcmdLengthSMW(
	"\x68\xda\x90\x0a\x6d\xfd\xae\x60"
	"\x96\xe8\x0e\xfd\x2f\xe3"
	,
	"x?xxxxxx"
	"x??xx?"
	,
	14);

//; Yoshi's Island SPC
//07fb: 2d        push  a
//07fc: 9f        xcn   a
//07fd: 28 07     and   a,#$07
//07ff: fd        mov   y,a
//0800: f6 e8 3f  mov   a,$3fe8+y
//0803: d5 01 02  mov   $0201+x,a         ;   set dur% from high nybble
//0806: ae        pop   a
//0807: 28 0f     and   a,#$0f
//0809: fd        mov   y,a
//080a: f6 f0 3f  mov   a,$3ff0+y
//080d: d5 10 02  mov   $0210+x,a         ;   set per-note vol from low nybble
BytePattern NinSnesScanner::ptnDispatchNoteYI(
	"\x2d\x9f\x28\x07\xfd\xf6\xe8\x3f"
	"\xd5\x01\x02\xae\x28\x0f\xfd\xf6"
	"\xf0\x3f\xd5\x10\x02"
	,
	"xxxxxx??"
	"x??xxxxx"
	"??x??"
	,
	21);

//; Yoshi's Island SPC
//; dereference and increment the section pointer $40/1
//06d7: 8d 00     mov   y,#$00
//06d9: f7 40     mov   a,($40)+y
//06db: 3a 40     incw  $40
//06dd: 2d        push  a
//06de: f7 40     mov   a,($40)+y
//06e0: 3a 40     incw  $40
//06e2: fd        mov   y,a
//06e3: ae        pop   a
BytePattern NinSnesScanner::ptnIncSectionPtr(
	"\x8d\x00\xf7\x40\x3a\x40\x2d\xf7"
	"\x40\x3a\x40\xfd\xae"
	,
	"xxx?x?xx"
	"?x?xx"
	,
	13);

//; Yoshi's Island SPC
//0b14: 8d 06     mov   y,#$06
//0b16: cf        mul   ya
//0b17: da 10     movw  $10,ya
//0b19: 60        clrc
//0b1a: 98 00 10  adc   $10,#$00
//0b1d: 98 05 11  adc   $11,#$05
BytePattern NinSnesScanner::ptnLoadInstrTableAddress(
	"\x8d\x06\xcf\xda\x10\x60\x98\x00"
	"\x10\x98\x05\x11"
	,
	"xxxx?xx?"
	"?x??"
	,
	12);

//; Super Mario World SPC
//0d4e: 8d 05     mov   y,#$05
//0d50: 8f 46 14  mov   $14,#$46
//0d53: 8f 5f 15  mov   $15,#$5f
//0d56: cf        mul   ya
//0d57: 7a 14     addw  ya,$14
//0d59: da 14     movw  $14,ya
BytePattern NinSnesScanner::ptnLoadInstrTableAddressSMW(
	"\x8d\x05\x8f\x46\x14\x8f\x5f\x15"
	"\xcf\x7a\x14\xda\x14"
	,
	"xxx??x??"
	"xx?x?"
	,
	13);

//; Kirby Super Star SPC
//071e: 8f 5d f2  mov   $f2,#$5d
//0721: 8f 03 f3  mov   $f3,#$03          ; source dir = $0300
BytePattern NinSnesScanner::ptnSetDIR(
	"\x8f\x5d\xf2\x8f\x03\xf3"
	,
	"xxxx?x"
	,
	6);

//; Yoshi's Island SPC
//042c: e8 3c     mov   a,#$3c
//042e: 8d 5d     mov   y,#$5d
//0430: 3f fa 05  call  $05fa             ; source dir = $3c00
BytePattern NinSnesScanner::ptnSetDIRYI(
	"\xe8\x3c\x8d\x5d\x3f\xfa\x05"
	,
	"x?xxx??"
	,
	7);

//; Super Mario World SPC
//; default values for DSP regs
//1295: db $7f,$7f,$00,$00,$2f,$60,$00,$00,$00,$80,$60,$02 
//12a1: db $0c,$1c,$2c,$3c,$6c,$0d,$2d,$3d,$4d,$5d,$6d,$7d
BytePattern NinSnesScanner::ptnSetDIRSMW(
	"\x7f\x7f\x00\x00\x2f\x60\x00\x00\x00\x80\x60\x02"
	"\x0c\x1c\x2c\x3c\x6c\x0d\x2d\x3d\x4d\x5d\x6d\x7d"
	,
	"????????????"
	"xxxxxxxxxxxx"
	,
	24);

// PATTERNS FOR DERIVED VERSIONS

//; Gradius 3 SPC
//0724: 8d 00     mov   y,#$00
//0726: f7 40     mov   a,($40)+y
//0728: 3a 40     incw  $40
//072a: 2d        push  a
//072b: f7 40     mov   a,($40)+y
//072d: f0 08     beq   $0737
//072f: 3a 40     incw  $40
//0731: fd        mov   y,a
//0732: ae        pop   a
//0733: 7a 4b     addw  ya,$4b            ; add address base
BytePattern NinSnesScanner::ptnIncSectionPtrGD3(
	"\x8d\x00\xf7\x40\x3a\x40\x2d\xf7"
	"\x40\xf0\x08\x3a\x40\xfd\xae\x7a"
	"\x4b"
	,
	"xxx?x?xx"
	"?x?x?xxx"
	"?"
	,
	17);

//; Yoshi's Safari SPC
//14ae: 8d 00     mov   y,#$00
//14b0: f7 4c     mov   a,($4c)+y
//14b2: c4 00     mov   $00,a
//14b4: c4 4e     mov   $4e,a
//14b6: 3a 4c     incw  $4c
//14b8: f7 4c     mov   a,($4c)+y
//14ba: c4 01     mov   $01,a
//14bc: c4 4f     mov   $4f,a
//14be: 3a 4c     incw  $4c               ; read a word from section list ptr
//14c0: 68 00     cmp   a,#$00
//14c2: d0 28     bne   $14ec             ; >= $0100, load that section ($00/1)
BytePattern NinSnesScanner::ptnIncSectionPtrYSFR(
	"\x8d\x00\xf7\x4c\xc4\x00\xc4\x4e"
	"\x3a\x4c\xf7\x4c\xc4\x01\xc4\x4f"
	"\x3a\x4c\x68\x00\xd0\x28"
	,
	"xxx?x?x?"
	"x?x?x?x?"
	"x?xxx?"
	,
	22);

//; Clock Tower SPC
// 07cc: 80        setc
// 07cd: a8 e0     sbc   a,#$e0
// 07cf: 1c        asl   a
// 07d0: fd        mov   y,a
// 07d1: f6 6d 07  mov   a,$076d+y
// 07d4: 2d        push  a
// 07d5: f6 6c 07  mov   a,$076c+y
// 07d8: 2d        push  a
// 07d9: dd        mov   a,y
// 07da: 5c        lsr   a
// 07db: fd        mov   y,a
// 07dc: f6 ac 07  mov   a,$07ac+y
// 07df: fd        mov   y,a
// 07e0: f0 03     beq   $07e5
BytePattern NinSnesScanner::ptnJumpToVcmdCTOW(
	"\x80\xa8\xe0\x1c\xfd\xf6\x6d\x07"
	"\x2d\xf6\x6c\x07\x2d\xdd\x5c\xfd"
	"\xf6\xac\x07\xfd\xf0\x03"
	,
	"xxxxxx??"
	"xx??xxxx"
	"x??xx?"
	,
	22);

//; Yoshi's Safari SPC
//10ce: 28 1f     and   a,#$1f
//10d0: 1c        asl   a
//10d1: fd        mov   y,a
//10d2: f6 dc 10  mov   a,$10dc+y
//10d5: 2d        push  a
//10d6: f6 db 10  mov   a,$10db+y
//10d9: 2d        push  a
//10da: 6f        ret
BytePattern NinSnesScanner::ptnJumpToVcmdYSFR(
	"\x28\x1f\x1c\xfd\xf6\xdc\x10\x2d"
	"\xf6\xdb\x10\x2d\x6f"
	,
	"xxxxx??x"
	"x??xx"
	,
	13);

//; Yoshi's Safari SPC
//0b2a: 80        setc
//0b2b: a8 e0     sbc   a,#$e0
//0b2d: cb 00     mov   $00,y
//0b2f: fd        mov   y,a
//0b30: f6 eb 0b  mov   a,$0beb+y ; read vcmd length
BytePattern NinSnesScanner::ptnReadVcmdLengthYSFR(
	"\x80\xa8\xe0\xcb\x00\xfd\xf6\xeb"
	"\x0b"
	,
	"xx?x?xx?"
	"?"
	,
	9);

//; Gradius 3 SPC
//09fe: 2d        push  a
//09ff: 9f        xcn   a
//0a00: 28 07     and   a,#$07
//0a02: fd        mov   y,a
//0a03: f6 e5 10  mov   a,$10e5+y
//0a06: d5 01 02  mov   $0201+x,a         ;   set dur% from high nybble
//0a09: ae        pop   a
//0a0a: 28 0f     and   a,#$0f
//0a0c: fd        mov   y,a
//0a0d: f6 ed 10  mov   a,$10ed+y
//0a10: 60        clrc
//0a11: 95 01 01  adc   a,$0101+x         ;   add volume delta of repeat vcmd
//0a14: d5 10 02  mov   $0210+x,a         ;   set per-note vol from low nybble
BytePattern NinSnesScanner::ptnDispatchNoteGD3(
	"\x2d\x9f\x28\x07\xfd\xf6\xe5\x10"
	"\xd5\x01\x02\xae\x28\x0f\xfd\xf6"
	"\xed\x10\x60\x95\x01\x01\xd5\x10"
	"\x02"
	,
	"xxxxxx??"
	"x??xxxxx"
	"??xx??x?"
	"?"
	,
	25);

//; Yoshi's Safari SPC
//0a47: 28 0f     and   a,#$0f
//0a49: fd        mov   y,a
//0a4a: f6 0b 0c  mov   a,$0c0b+y
//0a4d: d5 90 03  mov   $0390+x,a         ; set per-note vol from low nybble
//0a50: ae        pop   a
//0a51: 5c        lsr   a
//0a52: 5c        lsr   a
//0a53: 5c        lsr   a
//0a54: 5c        lsr   a
//0a55: fd        mov   y,a
//0a56: f6 80 1d  mov   a,$1d80+y
//0a59: d5 40 06  mov   $0640+x,a         ; set dur% from high nybble
BytePattern NinSnesScanner::ptnDispatchNoteYSFR(
	"\x28\x0f\xfd\xf6\x0b\x0c\xd5\x90"
	"\x03\xae\x5c\x5c\x5c\x5c\xfd\xf6"
	"\x80\x1d\xd5\x40\x06"
	,
	"xxxx??x?"
	"?xxxxxxx"
	"??x??"
	,
	21);

//; Lemmings SPC
//0ad0: 30 1e     bmi   $0af0             ; vcmds 01-7f - note info:
//0ad2: d5 00 02  mov   $0200+x,a         ; set duration by opcode
//0ad5: 3f 85 0b  call  $0b85             ; read next byte
//0ad8: 30 16     bmi   $0af0             ; process it, if < $80
//0ada: c4 11     mov   $11,a
//0adc: 4b 11     lsr   $11
//0ade: 1c        asl   a                 ; a  = (a << 1) | (a & 1)
//0adf: 84 11     adc   a,$11             ; a += (a >> 1)
//0ae1: d5 01 02  mov   $0201+x,a         ; set duration rate
//0ae4: 3f 85 0b  call  $0b85             ; read next byte
//0ae7: 30 07     bmi   $0af0             ; process it, if < $80
//0ae9: 1c        asl   a                 ; a *= 2
//0aea: d5 10 02  mov   $0210+x,a         ; set per-note volume (velocity)
BytePattern NinSnesScanner::ptnDispatchNoteLEM(
	"\x30\x1e\xd5\x00\x02\x3f\x85\x0b"
	"\x30\x16\xc4\x11\x4b\x11\x1c\x84"
	"\x11\xd5\x01\x02\x3f\x85\x0b\x30"
	"\x07\x1c\xd5\x10\x02"
	,
	"xxx??x??"
	"x?x?x?xx"
	"?x??x??x"
	"xxx??"
	,
	29);

// Fire Emblem 3 SPC
//; intelligent style - set from larger table
//062f: 68 40     cmp   a,#$40
//0631: b0 0c     bcs   $063f
//; 00-3f - set dur% from least 6 bits
//0633: 28 3f     and   a,#$3f
//0635: fd        mov   y,a
//0636: f6 00 ff  mov   a,$ff00+y
//0639: d5 01 02  mov   $0201+x,a
//063c: 5f 43 07  jmp   $0743
//; 40-7f - set per-note vol from least 6 bits
//063f: 28 3f     and   a,#$3f
//0641: fd        mov   y,a
//0642: f6 00 ff  mov   a,$ff00+y
//0645: d5 10 02  mov   $0210+x,a
//0648: 5f 22 07  jmp   $0722
BytePattern NinSnesScanner::ptnDispatchNoteFE3(
	"\x68\x40\xb0\x0c\x28\x3f\xfd\xf6"
	"\x00\xff\xd5\x01\x02\x5f\x43\x07"
	"\x28\x3f\xfd\xf6\x00\xff\xd5\x10"
	"\x02\x5f\x22\x07"
	,
	"xxxxxxxx"
	"??x??x??"
	"xxxx??x?"
	"?x??"
	,
	28);

//; Fire Emblem 4 SPC
//092f: 01        tcall 0                 ;   read next byte
//0930: 30 13     bmi   $0945             ;   if note note then
//0932: 68 40     cmp   a,#$40
//0934: 28 3f     and   a,#$3f
//0936: fd        mov   y,a
//0937: f6 38 10  mov   a,$1038+y
//093a: b0 05     bcs   $0941
//093c: d5 11 02  mov   $0211+x,a         ;   00-3f - set dur%
//093f: 2f ee     bra   $092f             ;   check more bytes
//0941: d5 20 02  mov   $0220+x,a         ;   40-7f - set vel
BytePattern NinSnesScanner::ptnDispatchNoteFE4(
	"\x01\x30\x13\x68\x40\x28\x3f\xfd"
	"\xf6\x38\x10\xb0\x05\xd5\x11\x02"
	"\x2f\xee\xd5\x20\x02"
	,
	"xx?xxxxx"
	"x??xxx??"
	"x?x??"
	,
	21);

//; Kirby Super Star SPC
//0e03: 3f 16 0e  call  $0e16
//0e06: d4 d0     mov   $d0+x,a           ; VOL(L)
//0e08: 8d 14     mov   y,#$14
//0e0a: e8 00     mov   a,#$00
//0e0c: 9a 1c     subw  ya,$1c
//0e0e: da 1c     movw  $1c,ya
//0e10: 3f 16 0e  call  $0e16
//0e13: d4 d1     mov   $d1+x,a           ; VOL(R)
//0e15: 6f        ret
BytePattern NinSnesScanner::ptnWriteVolumeKSS(
	"\x3f\x16\x0e\xd4\xd0\x8d\x14\xe8"
	"\x00\x9a\x1c\xda\x1c\x3f\x16\x0e"
	"\xd4\xd1\x6f"
	,
	"x??x?xxx"
	"xx?x?x??"
	"x?x"
	,
	19);

//; Super Metroid (developed by Nintendo RD1)
//; vcmd fa - set perc patch base
//1af1: c4 5f     mov   $5f,a
//1af3: 6f        ret
//; vcmd fb - skip 2 bytes
//1af4: 3f f1 18  call  $18f1
//1af7: 6f        ret
//; vcmd fc
//1af8: bc        inc   a
//1af9: d5 00 04  mov   $0400+x,a
//1afc: 6f        ret
//; vcmd fd
//1afd: bc        inc   a
//; vcmd fe
//1afe: c4 1b     mov   $1b,a
//1b00: 5f 50 17  jmp   $1750
BytePattern NinSnesScanner::ptnRD1VCmd_FA_FE(
	"\xc4\x5f\x6f\x3f\xf1\x18\x6f\xbc"
	"\xd5\x00\x04\x6f\xbc\xc4\x1b\x5f"
	"\x50\x17"
	,
	"x?xx??xx"
	"x??xxx?x"
	"??"
	,
	18);

//; Marvelous (developed by Nintendo RD2)
//; vcmd fb - set instrument with ADSR
//108f: 2d        push  a                 ; arg1 - sample number
//1090: 8d 06     mov   y,#$06
//1092: cf        mul   ya
//1093: da 14     movw  $14,ya
//1095: 60        clrc
//1096: 98 00 14  adc   $14,#$00
//1099: 98 5e 15  adc   $15,#$5e          ; $14/5 = $5e00 + (arg1 * 6)
//109c: 3f f4 09  call  $09f4
//109f: 8d 01     mov   y,#$01
//10a1: d7 14     mov   ($14)+y,a         ; ADSR(1)
//10a3: 3f f4 09  call  $09f4
//10a6: 8d 02     mov   y,#$02
//10a8: d7 14     mov   ($14)+y,a         ; ADSR(2)
//10aa: ae        pop   a
//10ab: 5f fe 09  jmp   $09fe             ; set instrument
BytePattern NinSnesScanner::ptnRD2VCmdInstrADSR(
	"\x2d\x8d\x06\xcf\xda\x14\x60\x98"
	"\x00\x14\x98\x5e\x15\x3f\xf4\x09"
	"\x8d\x01\xd7\x14\x3f\xf4\x09\x8d"
	"\x02\xd7\x14\xae\x5f\xfe\x09"
	,
	"xxxxx?xx"
	"??x??x??"
	"xxx?x??x"
	"xx?xx??"
	,
	31);

//; Fire Emblem 3 (developed by Intelligent Systems)
//; vcmd fa
//0884: 30 dd     bmi   $0863
//0886: f4 22     mov   a,$22+x
//0888: c4 b6     mov   $b6,a
//088a: f4 23     mov   a,$23+x
//088c: c4 b7     mov   $b7,a             ; set reading ptr to $b6/7
//088e: e8 04     mov   a,#$04
//0890: cf        mul   ya                ; skip arg1*4 bytes
//; add A to reading ptr
//0891: 60        clrc
//0892: 94 22     adc   a,$22+x
//0894: d4 22     mov   $22+x,a
//0896: 90 02     bcc   $089a
//0898: bb 23     inc   $23+x
//089a: 6f        ret
BytePattern NinSnesScanner::ptnIntelliVCmdFA(
	"\x30\xdd\xf4\x22\xc4\xb6\xf4\x23"
	"\xc4\xb7\xe8\x04\xcf\x60\x94\x22"
	"\xd4\x22\x90\x02\xbb\x23\x6f"
	,
	"x?x?x?x?"
	"x?x?xxx?"
	"x?xxx?x"
	,
	23);

//; Gradius 3 SPC
//; vcmd e0 - instrument
//0a9b: d5 11 02  mov   $0211+x,a
//0a9e: fd        mov   y,a
//0a9f: 30 15     bmi   $0ab6
//; set sample
//0aa1: 2d        push  a
//0aa2: 4d        push  x
//0aa3: 5d        mov   x,a
//0aa4: f5 73 0a  mov   a,$0a73+x
//0aa7: fd        mov   y,a
//0aa8: f5 87 0a  mov   a,$0a87+x
//0aab: ce        pop   x
//0aac: d5 91 03  mov   $0391+x,a
//0aaf: dd        mov   a,y
//0ab0: d5 90 03  mov   $0390+x,a         ; save per-instrument tuning
//0ab3: ae        pop   a
BytePattern NinSnesScanner::ptnInstrVCmdGD3(
	"\xd5\x11\x02\xfd\x30\x15\x2d\x4d"
	"\x5d\xf5\x73\x0a\xfd\xf5\x87\x0a"
	"\xce\xd5\x91\x03\xdd\xd5\x90\x03"
	"\xae"
	,
	"x??xx?xx"
	"xx??xx??"
	"xx??xx??"
	"x"
	,
	25);

//; S.O.S. (Septentrion) SPC
//0441: 8f 00 00  mov   $00,#$00
//0444: 8f 3e 01  mov   $01,#$3e
//0447: 68 00     cmp   a,#$00
//0449: 10 06     bpl   $0451
//044b: 80        setc
//044c: a8 ca     sbc   a,#$ca
//044e: 60        clrc
//044f: 84 40     adc   a,$40
//0451: 8d 06     mov   y,#$06
//0453: cf        mul   ya
//0454: 7a 00     addw  ya,$00
//0456: da 00     movw  $00,ya
BytePattern NinSnesScanner::ptnLoadInstrTableAddressSOS(
	"\x8f\x00\x00\x8f\x3e\x01\x68\x00"
	"\x10\x06\x80\xa8\xca\x60\x84\x40"
	"\x8d\x06\xcf\x7a\x00\xda\x00"
	,
	"x??x??x?"
	"xxxx?xx?"
	"xxxx?x?"
	,
	23);

//; Clock Tower SPC
//0810: 2d        push  a
//0811: 78 00 04  cmp   $04,#$00
//0814: d0 08     bne   $081e
//0816: 8f 00 00  mov   $00,#$00
//0819: 8f 3c 01  mov   $01,#$3c
//081c: 2f 06     bra   $0824
//081e: 8f 00 00  mov   $00,#$00
//0821: 8f 3e 01  mov   $01,#$3e
//0824: ae        pop   a
//0825: 8d 06     mov   y,#$06
//0827: cf        mul   ya
//0828: 7a 00     addw  ya,$00
//082a: da 00     movw  $00,ya
BytePattern NinSnesScanner::ptnLoadInstrTableAddressCTOW(
	"\x2d\x78\x00\x04\xd0\x08\x8f\x00"
	"\x00\x8f\x3c\x01\x2f\x06\x8f\x00"
	"\x00\x8f\x3e\x01\xae\x8d\x06\xcf"
	"\x7a\x00\xda\x00"
	,
	"xx??xxx?"
	"?x??xxx?"
	"?x??xxxx"
	"x?x?"
	,
	28);

//; Yoshi's Safari SPC
//0877: 8d 5d     mov   y,#$5d
//0879: e8 1b     mov   a,#$1b
//087b: c4 45     mov   $45,a
//087d: 3f 99 10  call  $1099             ; set DIR
//0880: 8f 00 46  mov   $46,#$00
//0883: 8f 1c 47  mov   $47,#$1c          ; instrument list = $1c00
//0886: 8f 00 48  mov   $48,#$00
//0889: 8f 1e 49  mov   $49,#$1e          ; song list = $1e00
//088c: 8f 00 4a  mov   $4a,#$00
//088f: 8f 1f 4b  mov   $4b,#$1f
BytePattern NinSnesScanner::ptnLoadInstrTableAddressYSFR(
	"\x8d\x5d\xe8\x1b\xc4\x45\x3f\x99"
	"\x10\x8f\x00\x46\x8f\x1c\x47\x8f"
	"\x00\x48\x8f\x1e\x49\x8f\x00\x4a"
	"\x8f\x1f\x4b"
	,
	"xxx?x?x?"
	"?x??x??x"
	"??x??x??"
	"x??"
	,
	27);

//; Clock Tower SPC
//11a6: 8d 5d     mov   y,#$5d
//11a8: e8 3a     mov   a,#$3a
//11aa: 3f 0b 05  call  $050b             ; set DIR
BytePattern NinSnesScanner::ptnSetDIRCTOW(
	"\x8d\x5d\xe8\x3a\x3f\x0b\x05"
	,
	"xxx?x??"
	,
	7);

void NinSnesScanner::Scan(RawFile* file, void* info)
{
	uint32_t nFileLength = file->size();
	if (nFileLength == 0x10000)
	{
		SearchForNinSnesFromARAM(file);
	}
	else
	{
		SearchForNinSnesFromROM(file);
	}
	return;
}

void NinSnesScanner::SearchForNinSnesFromARAM (RawFile* file)
{
	NinSnesVersion version = NINSNES_NONE;

	std::wstring basefilename = RawFile::removeExtFromPath(file->GetFileName());
	std::wstring name = file->tag.HasTitle() ? file->tag.title : basefilename;

	// get section pointer address
	uint32_t ofsIncSectionPtr;
	uint8_t addrSectionPtr;
	uint16_t konamiBaseAddress = 0xffff;
	if (file->SearchBytePattern(ptnIncSectionPtr, ofsIncSectionPtr)) {
		addrSectionPtr = file->GetByte(ofsIncSectionPtr + 3);
	}
	// DERIVED VERSIONS
	else if (file->SearchBytePattern(ptnIncSectionPtrGD3, ofsIncSectionPtr)) {
		uint8_t konamiBaseAddressPtr = file->GetByte(ofsIncSectionPtr + 16);
		addrSectionPtr = file->GetByte(ofsIncSectionPtr + 3);
		konamiBaseAddress = file->GetShort(konamiBaseAddressPtr);
	}
	else if (file->SearchBytePattern(ptnIncSectionPtrYSFR, ofsIncSectionPtr)) {
		addrSectionPtr = file->GetByte(ofsIncSectionPtr + 3);
	}
	else {
		return;
	}

	// BEGIN DYNAMIC PATTERN DEFINITIONS

	//; Kirby Super Star SPC
	//; set initial value to section pointer
	//08e4: f5 ff 38  mov   a,$38ff+x
	//08e7: fd        mov   y,a
	//08e8: f5 fe 38  mov   a,$38fe+x
	//08eb: da 30     movw  $30,ya
	char ptnInitSectionPtrBytes[] =
		"\xf5\xff\x38\xfd\xf5\xfe\x38\xda"
		"\x30";
	ptnInitSectionPtrBytes[8] = addrSectionPtr;
	BytePattern ptnInitSectionPtr(
		ptnInitSectionPtrBytes
		,
		"x??xx??x"
		"x"
		,
		9);

	//; Yoshi's Island SPC
	//; set initial value to section pointer
	//06f0: 1c        asl   a
	//06f1: 5d        mov   x,a
	//06f2: f5 8f ff  mov   a,$ff8f+x
	//06f5: fd        mov   y,a
	//06f6: d0 03     bne   $06fb
	//06f8: c4 04     mov   $04,a
	//06fa: 6f        ret
	//06fb: f5 8e ff  mov   a,$ff8e+x
	//06fe: da 40     movw  $40,ya
	char ptnInitSectionPtrBytesYI[] =
		"\x1c\x5d\xf5\x8f\xff\xfd\xd0\x03"
		"\xc4\x04\x6f\xf5\x8e\xff\xda\x40";
	ptnInitSectionPtrBytesYI[15] = addrSectionPtr;
	BytePattern ptnInitSectionPtrYI(
		ptnInitSectionPtrBytesYI
		,
		"xxx??xxx"
		"x?xx??xx"
		,
		16);

	//; Super Mario World SPC
	//; set initial value to section pointer
	//0b5f: 1c        asl   a
	//0b60: fd        mov   y,a
	//0b61: f6 5e 13  mov   a,$135e+y
	//0b64: c4 40     mov   $40,a
	//0b66: f6 5f 13  mov   a,$135f+y
	//0b69: c4 41     mov   $41,a
	char ptnInitSectionPtrBytesSMW[] =
		"\x1c\xfd\xf6\x5e\x13\xc4\x40\xf6"
		"\x5f\x13\xc4\x41";
	ptnInitSectionPtrBytesSMW[6] = addrSectionPtr;
	ptnInitSectionPtrBytesSMW[11] = addrSectionPtr + 1;
	BytePattern ptnInitSectionPtrSMW(
		ptnInitSectionPtrBytesSMW
		,
		"xxx??xxx"
		"??xx"
		,
		12);

	// DERIVED VERSIONS

	//; Gradius 3 SPC
	//08b7: 5d        mov   x,a
	//08b8: f5 fc 11  mov   a,$11fc+x
	//08bb: f0 ee     beq   $08ab
	//08bd: fd        mov   y,a
	//08be: f5 fb 11  mov   a,$11fb+x
	//08c1: da 40     movw  $40,ya            ; song metaindex ptr
	char ptnInitSectionPtrBytesGD3[] =
		"\x5d\xf5\xfc\x11\xf0\xee\xfd\xf5"
		"\xfb\x11\xda\x40";
	ptnInitSectionPtrBytesGD3[11] = addrSectionPtr;
	BytePattern ptnInitSectionPtrGD3(
		ptnInitSectionPtrBytesGD3
		,
		"xx??x?xx"
		"??xx"
		,
		12);

	//; Yoshi's Safari SPC
	//1488: fd        mov   y,a
	//1489: f7 48     mov   a,($48)+y
	//148b: c4 4c     mov   $4c,a
	//148d: fc        inc   y
	//148e: f7 48     mov   a,($48)+y
	//1490: c4 4d     mov   $4d,a
	char ptnInitSectionPtrBytesYSFR[] =
		"\xfd\xf7\x48\xc4\x4c\xfc\xf7\x48"
		"\xc4\x4d";
	ptnInitSectionPtrBytesYSFR[4] = addrSectionPtr;
	ptnInitSectionPtrBytesYSFR[9] = addrSectionPtr + 1;
	BytePattern ptnInitSectionPtrYSFR(
		ptnInitSectionPtrBytesYSFR
		,
		"xx?xxxx?"
		"xx"
		,
		10);

	// END DYNAMIC PATTERN DEFINITIONS

	// ACQUIRE SEQUENCE LIST ADDRESS:
	// find the initialization code of the section pointer,
	// and acquire the sequence list address
	uint32_t ofsInitSectionPtr;
	uint32_t addrSongList;
	// STANDARD VERSIONS
	if (file->SearchBytePattern(ptnInitSectionPtr, ofsInitSectionPtr)) {
		addrSongList = file->GetShort(ofsInitSectionPtr + 5);
	}
	else if (file->SearchBytePattern(ptnInitSectionPtrYI, ofsInitSectionPtr)) {
		addrSongList = file->GetShort(ofsInitSectionPtr + 12);
	}
	else if (file->SearchBytePattern(ptnInitSectionPtrSMW, ofsInitSectionPtr)) {
		addrSongList = file->GetShort(ofsInitSectionPtr + 3);
	}
	// DERIVED VERSIONS
	else if (file->SearchBytePattern(ptnInitSectionPtrGD3, ofsInitSectionPtr)) {
		addrSongList = file->GetShort(ofsInitSectionPtr + 8);
		if (konamiBaseAddress == 0xffff) {
			// Parodius Da! does not have base address
			konamiBaseAddress = 0;
		}
	}
	else if (file->SearchBytePattern(ptnInitSectionPtrYSFR, ofsInitSectionPtr)) {
		byte addrSongListPtr = file->GetByte(ofsInitSectionPtr + 2);

		//; Yoshi's Safari SPC
		//0886: 8f 00 48  mov   $48,#$00
		//0889: 8f 1e 49  mov   $49,#$1e
		char ptnInitSongListPtrBytesYSFR[] =
			"\x8f\x00\x48\x8f\x1e\x49";
		ptnInitSongListPtrBytesYSFR[2] = addrSongListPtr;
		ptnInitSongListPtrBytesYSFR[5] = addrSongListPtr + 1;
		BytePattern ptnInitSongListPtrYSFR(
			ptnInitSongListPtrBytesYSFR
			,
			"x?xx?x"
			,
			6);

		UINT ofsInitSongListPtr;
		if (file->SearchBytePattern(ptnInitSongListPtrYSFR, ofsInitSongListPtr)) {
			addrSongList = file->GetByte(ofsInitSongListPtr + 1) | (file->GetByte(ofsInitSongListPtr + 4) << 8);
		}
	}
	else {
		return;
	}

	// ACQUIRE VOICE COMMAND LIST & DETECT ENGINE VERSION
	// (Minor classification for derived versions should come later as far as possible)

	// find a branch for voice command,
	// and acquire the following info for standard engines.
	// 
	// - First voice command (usually $e0)
	// - Voice command address table
	// - Voice command length table

	uint32_t ofsBranchForVcmd;
	uint8_t firstVoiceCmd = 0;
	uint16_t addrVoiceCmdAddressTable;
	uint16_t addrVoiceCmdLengthTable;
	// DERIVED VERSIONS
	if (file->SearchBytePattern(ptnJumpToVcmdYSFR, ofsBranchForVcmd)) {
		addrVoiceCmdAddressTable = file->GetShort(ofsBranchForVcmd + 9);

		uint32_t ofsReadVcmdLength;
		if (file->SearchBytePattern(ptnReadVcmdLengthYSFR, ofsReadVcmdLength)) {
			firstVoiceCmd = file->GetByte(ofsReadVcmdLength + 2);
			addrVoiceCmdLengthTable = file->GetShort(ofsReadVcmdLength + 7);

			if (firstVoiceCmd == 0xe0) {
				version = NINSNES_TOSE;
			}
		}
		else {
			return;
		}
	}
	else {
		// STANDARD VERSION
		if (file->SearchBytePattern(ptnBranchForVcmdReadahead, ofsBranchForVcmd)) {
			firstVoiceCmd = file->GetByte(ofsBranchForVcmd + 5);
		}
		else if (file->SearchBytePattern(ptnBranchForVcmd, ofsBranchForVcmd)) {
			// this search often finds a wrong code, but some games still need it (for example, Human games)
			firstVoiceCmd = file->GetByte(ofsBranchForVcmd + 1);
		}
		else {
			return;
		}

		uint32_t ofsJumpToVcmd;

		// find a jump to address_table[cmd * 2]
		if (file->SearchBytePattern(ptnJumpToVcmd, ofsJumpToVcmd)) {
			addrVoiceCmdAddressTable = file->GetShort(ofsJumpToVcmd + 7) + ((firstVoiceCmd * 2) & 0xff);
			addrVoiceCmdLengthTable = file->GetShort(ofsJumpToVcmd + 14) + (firstVoiceCmd & 0x7f);

			// false-positive needs to be fixed in later classification
			version = NINSNES_STANDARD;
		}
		else if (file->SearchBytePattern(ptnJumpToVcmdSMW, ofsJumpToVcmd)) {
			// search vcmd length table as well
			uint32_t ofsReadVcmdLength;
			if (file->SearchBytePattern(ptnReadVcmdLengthSMW, ofsReadVcmdLength)) {
				addrVoiceCmdAddressTable = file->GetShort(ofsJumpToVcmd + 5) + ((firstVoiceCmd * 2) & 0xff);
				addrVoiceCmdLengthTable = file->GetShort(ofsReadVcmdLength + 9) + firstVoiceCmd;
				version = NINSNES_EARLIER;
			}
			else {
				return;
			}
		}
		// DERIVED VERSIONS
		else if (file->SearchBytePattern(ptnJumpToVcmdCTOW, ofsJumpToVcmd)) {
			// Human Games: Clock Tower, Firemen, S.O.S. (Septentrion)
			addrVoiceCmdAddressTable = file->GetShort(ofsJumpToVcmd + 10);
			addrVoiceCmdLengthTable = file->GetShort(ofsJumpToVcmd + 17);
			version = NINSNES_HUMAN;
		}
		else {
			return;
		}
	}

	// TRY TO GRAB NOTE VOLUME/DURATION TABLE
	UINT ofsDispatchNote = 0;
	uint16_t addrDurRateTable = 0;
	uint16_t addrVolumeTable = 0;
	std::vector<uint8_t> durRateTable;
	std::vector<uint8_t> volumeTable;
	if (file->SearchBytePattern(ptnDispatchNoteYI, ofsDispatchNote)) {
		addrDurRateTable = file->GetShort(ofsDispatchNote + 6);
		for (uint8_t offset = 0; offset < 8; offset++) {
			durRateTable.push_back(file->GetByte(addrDurRateTable + offset));
		}

		addrVolumeTable = file->GetShort(ofsDispatchNote + 16);
		for (uint8_t offset = 0; offset < 16; offset++) {
			volumeTable.push_back(file->GetByte(addrVolumeTable + offset));
		}
	}
	else if (file->SearchBytePattern(ptnDispatchNoteGD3, ofsDispatchNote)) {
		addrDurRateTable = file->GetShort(ofsDispatchNote + 6);
		for (uint8_t offset = 0; offset < 8; offset++) {
			durRateTable.push_back(file->GetByte(addrDurRateTable + offset));
		}

		addrVolumeTable = file->GetShort(ofsDispatchNote + 16);
		for (uint8_t offset = 0; offset < 16; offset++) {
			volumeTable.push_back(file->GetByte(addrVolumeTable + offset));
		}
	}
	else if (file->SearchBytePattern(ptnDispatchNoteYSFR, ofsDispatchNote)) {
		addrDurRateTable = file->GetShort(ofsDispatchNote + 16);
		for (uint8_t offset = 0; offset < 8; offset++) {
			durRateTable.push_back(file->GetByte(addrDurRateTable + offset));
		}

		addrVolumeTable = file->GetShort(ofsDispatchNote + 4);
		for (uint8_t offset = 0; offset < 16; offset++) {
			volumeTable.push_back(file->GetByte(addrVolumeTable + offset));
		}
	}

	// CLASSIFY DERIVED VERSIONS (fix false-positive)
	if (version == NINSNES_STANDARD)
	{
		if (konamiBaseAddress != 0xffff) {
			version = NINSNES_KONAMI;
		}
		else if (file->SearchBytePattern(ptnDispatchNoteLEM, ofsDispatchNote)) {
			if (firstVoiceCmd == 0xe0) {
				version = NINSNES_LEMMINGS;
			}
			else {
				version = NINSNES_UNKNOWN;
			}
		}
		else {
			const uint8_t STD_VCMD_LEN_TABLE[27] = { 0x01, 0x01, 0x02, 0x03, 0x00, 0x01, 0x02, 0x01, 0x02, 0x01, 0x01, 0x03, 0x00, 0x01, 0x02, 0x03, 0x01, 0x03, 0x03, 0x00, 0x01, 0x03, 0x00, 0x03, 0x03, 0x03, 0x01 };
			if (firstVoiceCmd == 0xe0 && file->MatchBytes(STD_VCMD_LEN_TABLE, addrVoiceCmdLengthTable, sizeof(STD_VCMD_LEN_TABLE))) {
				if (addrVoiceCmdAddressTable + sizeof(STD_VCMD_LEN_TABLE) * 2 == addrVoiceCmdLengthTable) {
					UINT ofsWriteVolume;
					if (file->SearchBytePattern(ptnWriteVolumeKSS, ofsWriteVolume)) {
						version = NINSNES_HAL;
					}
					else {
						version = NINSNES_STANDARD;
					}
				}
				else {
					// compatible design, but customized anyway
					version = NINSNES_STANDARD;

					UINT ofsRD1VCmd_FA_FE;
					UINT ofsRD2VCmdInstrADSR;
					if (file->SearchBytePattern(ptnRD1VCmd_FA_FE, ofsRD1VCmd_FA_FE)) {
						version = NINSNES_RD1;
					}
					else if (file->SearchBytePattern(ptnRD2VCmdInstrADSR, ofsRD2VCmdInstrADSR)) {
						// Marvelous
						version = NINSNES_RD2;
					}
				}
			}
			else {
				version = NINSNES_UNKNOWN;

				UINT ofsIntelliVCmdFA;
				if (file->SearchBytePattern(ptnIntelliVCmdFA, ofsIntelliVCmdFA)) {
					// Intelligent Systems
					if (file->SearchBytePattern(ptnDispatchNoteFE3, ofsDispatchNote)) {
						const uint8_t INTELLI_FE3_VCMD_LEN_TABLE[40] = { 0x01, 0x01, 0x02, 0x03, 0x00, 0x01, 0x02, 0x01, 0x02, 0x01, 0x01, 0x03, 0x00, 0x01, 0x02, 0x03, 0x01, 0x03, 0x03, 0x00, 0x01, 0x03, 0x00, 0x03, 0x03, 0x03, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x00, 0x01, 0x01, 0x02, 0x02 };
						if (firstVoiceCmd == 0xd6 && file->MatchBytes(INTELLI_FE3_VCMD_LEN_TABLE, addrVoiceCmdLengthTable, sizeof(INTELLI_FE3_VCMD_LEN_TABLE))) {
							version = NINSNES_INTELLI_FE3;
						}
					}
					else if (file->SearchBytePattern(ptnDispatchNoteFE4, ofsDispatchNote)) {
						const uint8_t INTELLI_FE4_VCMD_LEN_TABLE[36] = { 0x01, 0x01, 0x02, 0x03, 0x00, 0x01, 0x02, 0x01, 0x02, 0x01, 0x01, 0x03, 0x00, 0x01, 0x02, 0x03, 0x01, 0x03, 0x03, 0x00, 0x01, 0x03, 0x00, 0x03, 0x03, 0x03, 0x01, 0x00, 0x00, 0x02, 0x01, 0x00, 0x01, 0x01, 0x01, 0x01 };
						if (firstVoiceCmd == 0xda && file->MatchBytes(INTELLI_FE4_VCMD_LEN_TABLE, addrVoiceCmdLengthTable, sizeof(INTELLI_FE4_VCMD_LEN_TABLE))) {
							version = NINSNES_INTELLI_FE4;
						}
					}
					else {
						const uint8_t INTELLI_TA_VCMD_LEN_TABLE[36] = { 0x01, 0x01, 0x02, 0x03, 0x00, 0x01, 0x02, 0x01, 0x02, 0x01, 0x01, 0x03, 0x00, 0x01, 0x02, 0x03, 0x01, 0x03, 0x03, 0x00, 0x01, 0x03, 0x00, 0x03, 0x03, 0x03, 0x01, 0x00, 0x00, 0x02, 0x02, 0x00, 0x01, 0x01, 0x01, 0x01 };
						if (firstVoiceCmd == 0xda && file->MatchBytes(INTELLI_TA_VCMD_LEN_TABLE, addrVoiceCmdLengthTable, sizeof(INTELLI_TA_VCMD_LEN_TABLE))) {
							version = NINSNES_INTELLI_TA;
						}
					}
				}
			}
		}
	}

	// GUESS SONG COUNT
	// Test Case: Star Fox, Kirby Super Star, Earthbound
	// Note that the result sometimes exceeds the real length
	uint8_t songListLength = 1;
	uint16_t addrSectionListCutoff = 0xffff;
	// skip index 0, it's not a part of table in most games
	for (uint8_t songIndex = 1; songIndex <= 0x7f; songIndex++) {
		UINT addrSectionListPtr = addrSongList + songIndex * 2;
		if (addrSectionListPtr >= addrSectionListCutoff) {
			break;
		}

		uint16_t firstSectionPtr = file->GetShort(addrSectionListPtr);
		if (version == NINSNES_KONAMI) {
			firstSectionPtr += konamiBaseAddress;
		}
		if (firstSectionPtr == 0) {
			continue;
		}
		if ((firstSectionPtr & 0xff00) == 0 || firstSectionPtr == 0xffff) {
			break;
		}
		if (firstSectionPtr >= addrSectionListPtr) {
			addrSectionListCutoff = min(addrSectionListCutoff, firstSectionPtr);
		}

		uint16_t addrFirstSection = file->GetShort(firstSectionPtr);
		if (addrFirstSection < 0x0100) {
			// usually it does not appear
			// probably it's broken
			continue;
		}

		if (version == NINSNES_KONAMI) {
			addrFirstSection += konamiBaseAddress;
		}
		if (addrFirstSection + 16 > 0x10000) {
			break;
		}

		bool hasIllegalTrack = false;
		for (uint8_t trackIndex = 0; trackIndex < 8; trackIndex++) {
			uint16_t addrTrackStart = file->GetShort(addrFirstSection + trackIndex * 2);
			if (addrTrackStart != 0) {
				if (addrTrackStart == 0xffff) {
					hasIllegalTrack = true;
					break;
				}

				if (version == NINSNES_KONAMI) {
					addrTrackStart += konamiBaseAddress;
				}

				if ((addrTrackStart & 0xff00) == 0 || addrTrackStart == 0xffff) {
					hasIllegalTrack = true;
					break;
				}
			}
		}
		if (hasIllegalTrack) {
			break;
		}

		songListLength = songIndex + 1;
	}

	// GUESS CURRENT SONG NUMBER
	uint8_t guessedSongIndex = 0xff;

	// scan for a song that contains the current section
	// (note that the section pointer points to the "next" section actually, in most cases)
	uint16_t addrCurrentSection = file->GetShort(addrSectionPtr);
	if (addrCurrentSection >= 0x0100 && addrCurrentSection < 0xfff0) {
		uint8_t songIndexCandidate = 0xff;

		for (uint8_t songIndex = 0; songIndex <= songListLength; songIndex++) {
			UINT addrSectionListPtr = addrSongList + songIndex * 2;
			if (addrSectionListPtr == 0 || addrSectionListPtr == 0xffff) {
				continue;
			}

			uint16_t firstSectionPtr = file->GetShort(addrSectionListPtr);
			if (version == NINSNES_KONAMI) {
				firstSectionPtr += konamiBaseAddress;
			}
			if (firstSectionPtr > addrCurrentSection) {
				continue;
			}

			uint16_t curAddress = firstSectionPtr;
			if ((addrCurrentSection % 2) == (curAddress % 2)) {
				uint8_t sectionCount = 0; // prevent overrun of illegal data
				while (curAddress >= 0x0100 && curAddress < 0xfff0 && sectionCount < 32) {
					uint16_t addrSection = file->GetShort(curAddress);
					if (version == NINSNES_KONAMI) {
						addrSection += konamiBaseAddress;
					}

					if (curAddress == addrCurrentSection) {
						songIndexCandidate = songIndex;
						break;
					}

					if ((addrSection & 0xff00) == 0) {
						// section list end / jump
						break;
					}

					curAddress += 2;
					sectionCount++;
				}

				if (songIndexCandidate != 0xff) {
					break;
				}
			}
		}

		if (songIndexCandidate != 0xff) {
			guessedSongIndex = songIndexCandidate;
		}
	}

	if (guessedSongIndex == 0xff) {
		return;
	}
	
	// load the song
	uint16_t addrSongStart = file->GetShort(addrSongList + guessedSongIndex * 2);
	if (version == NINSNES_KONAMI) {
		addrSongStart += konamiBaseAddress;
	}

	NinSnesSeq* newSeq = new NinSnesSeq(file, version, addrSongStart, 0, volumeTable, durRateTable, name);
	newSeq->konamiBaseAddress = konamiBaseAddress;
	if (!newSeq->LoadVGMFile()) {
		delete newSeq;
		return;
	}

	// skip unknown instruments
	if (version == NINSNES_UNKNOWN) {
		return;
	}

	// scan for instrument table
	UINT ofsLoadInstrTableAddressASM;
	UINT addrInstrTable;
	uint16_t spcDirAddr = 0;
	if (file->SearchBytePattern(ptnLoadInstrTableAddress, ofsLoadInstrTableAddressASM)) {
		addrInstrTable = file->GetByte(ofsLoadInstrTableAddressASM + 7) | (file->GetByte(ofsLoadInstrTableAddressASM + 10) << 8);
	}
	else if (file->SearchBytePattern(ptnLoadInstrTableAddressSMW, ofsLoadInstrTableAddressASM)) {
		addrInstrTable = file->GetByte(ofsLoadInstrTableAddressASM + 3) | (file->GetByte(ofsLoadInstrTableAddressASM + 6) << 8);
	}
	// DERIVED VERSIONS
	else if (version == NINSNES_HUMAN) {
		if (file->SearchBytePattern(ptnLoadInstrTableAddressCTOW, ofsLoadInstrTableAddressASM)) {
			addrInstrTable = file->GetByte(ofsLoadInstrTableAddressASM + 7) | (file->GetByte(ofsLoadInstrTableAddressASM + 10) << 8);
		}
		else if (file->SearchBytePattern(ptnLoadInstrTableAddressSOS, ofsLoadInstrTableAddressASM)) {
			addrInstrTable = file->GetByte(ofsLoadInstrTableAddressASM + 1) | (file->GetByte(ofsLoadInstrTableAddressASM + 4) << 8);
		}
		else {
			return;
		}
	}
	else if (version == NINSNES_TOSE) {
		if (file->SearchBytePattern(ptnLoadInstrTableAddressYSFR, ofsLoadInstrTableAddressASM)) {
			spcDirAddr = file->GetByte(ofsLoadInstrTableAddressASM + 3) << 8;
			addrInstrTable = file->GetByte(ofsLoadInstrTableAddressASM + 10) | (file->GetByte(ofsLoadInstrTableAddressASM + 13) << 8);
		}
		else {
			return;
		}
	}
	else {
		return;
	}

	// scan for DIR address
	if (spcDirAddr == 0) {
		UINT ofsSetDIR;
		if (file->SearchBytePattern(ptnSetDIR, ofsSetDIR)) {
			spcDirAddr = file->GetByte(ofsSetDIR + 4) << 8;
		}
		else if (file->SearchBytePattern(ptnSetDIRYI, ofsSetDIR)) {
			spcDirAddr = file->GetByte(ofsSetDIR + 1) << 8;
		}
		else if (file->SearchBytePattern(ptnSetDIRSMW, ofsSetDIR)) {
			spcDirAddr = file->GetByte(ofsSetDIR + 9) << 8;
		}
		// DERIVED VERSIONS
		else if (file->SearchBytePattern(ptnSetDIRCTOW, ofsSetDIR)) {
			spcDirAddr = file->GetByte(ofsSetDIR + 3) << 8;
		}
		else {
			return;
		}
	}

	uint16_t konamiTuningTableAddress = 0;
	uint8_t konamiTuningTableSize = 0;
	if (version == NINSNES_KONAMI) {
		UINT ofsInstrVCmd;
		if (file->SearchBytePattern(ptnInstrVCmdGD3, ofsInstrVCmd)) {
			uint16_t konamiAddrTuningTableLow = file->GetShort(ofsInstrVCmd + 10);
			uint16_t konamiAddrTuningTableHigh = file->GetShort(ofsInstrVCmd + 14);

			if (konamiAddrTuningTableHigh > konamiAddrTuningTableLow &&
				konamiAddrTuningTableHigh - konamiAddrTuningTableLow <= 0x7f)
			{
				konamiTuningTableAddress = konamiAddrTuningTableLow;
				konamiTuningTableSize = konamiAddrTuningTableHigh - konamiAddrTuningTableLow;
			}
		}
	}

	NinSnesInstrSet * newInstrSet = new NinSnesInstrSet(file, version, addrInstrTable, spcDirAddr);
	newInstrSet->konamiTuningTableAddress = konamiTuningTableAddress;
	newInstrSet->konamiTuningTableSize = konamiTuningTableSize;
	if (!newInstrSet->LoadVGMFile()) {
		delete newInstrSet;
		return;
	}
}

void NinSnesScanner::SearchForNinSnesFromROM (RawFile* file)
{
}
