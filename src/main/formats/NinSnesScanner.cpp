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
BytePattern NinSnesScanner::ptnIncSectionPtrYSFR(
	"\x8d\x00\xf7\x4c\xc4\x00\xc4\x4e"
	"\x3a\x4c\xf7\x4c\xc4\x01\xc4\x4f"
	"\x3a\x4c"
	,
	"xxx?x?x?"
	"x?x?x?x?"
	"x?"
	,
	18);

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
//0932: 68 40     cmp   a,#$40
//0934: 28 3f     and   a,#$3f
//0936: fd        mov   y,a
//0937: f6 38 10  mov   a,$1038+y
//093a: b0 05     bcs   $0941
//093c: d5 11 02  mov   $0211+x,a         ;   00-3f - set dur%
//093f: 2f ee     bra   $092f             ;   check more bytes
//0941: d5 20 02  mov   $0220+x,a         ;   40-7f - set vel
BytePattern NinSnesScanner::ptnDispatchNoteFE4(
	"\x68\x40\x28\x3f\xfd\xf6\x38\x10"
	"\xb0\x05\xd5\x11\x02\x2f\xee\xd5"
	"\x20\x02"
	,
	"xxxxxx??"
	"xxx??x?x"
	"??"
	,
	18);

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
	// SPECIFIC DERIVED VERSIONS
	if (konamiBaseAddress != 0xffff && file->SearchBytePattern(ptnInitSectionPtrGD3, ofsInitSectionPtr)) {
		addrSongList = file->GetShort(ofsInitSectionPtr + 8);
	}
	// STANDARD VERSIONS
	else if (file->SearchBytePattern(ptnInitSectionPtr, ofsInitSectionPtr)) {
		addrSongList = file->GetShort(ofsInitSectionPtr + 5);
	}
	else if (file->SearchBytePattern(ptnInitSectionPtrYI, ofsInitSectionPtr)) {
		addrSongList = file->GetShort(ofsInitSectionPtr + 12);
	}
	else if (file->SearchBytePattern(ptnInitSectionPtrSMW, ofsInitSectionPtr)) {
		addrSongList = file->GetShort(ofsInitSectionPtr + 3);
	}
	// OTHER DERIVED VERSIONS
	else if (file->SearchBytePattern(ptnInitSectionPtrYSFR, ofsInitSectionPtr)) {
		byte addrSongListPtr = file->GetByte(ofsInitSectionPtr + 2);

		//; Yoshi's Safari SPC
		//0886: 8f 00 48  mov   $48,#$00
		//0889: 8f 1e 49  mov   $49,#$1e
		char ptnInitSongListPtrBytesYSFR[] =
			"\x8f\x00\x48\x8f\x1e\x49";
		ptnInitSectionPtrBytesYSFR[2] = addrSongListPtr;
		ptnInitSectionPtrBytesYSFR[5] = addrSongListPtr + 1;
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
	uint8_t firstVoiceCmd;
	uint16_t addrVoiceCmdAddressTable;
	uint16_t addrVoiceCmdLengthTable;
	if (file->SearchBytePattern(ptnBranchForVcmd, ofsBranchForVcmd))
	{
		uint32_t ofsJumpToVcmd;

		firstVoiceCmd = file->GetByte(ofsBranchForVcmd + 1);

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
			addrVoiceCmdAddressTable = file->GetShort(ofsJumpToVcmd + 10);
			addrVoiceCmdLengthTable = file->GetShort(ofsJumpToVcmd + 17);
			version = NINSNES_STANDARD; // TODO: set different version code (Human Games: Clock Tower, Firemen, Septentrion)
		}
		else {
			return;
		}
	}
	// DERIVED VERSIONS
	else if (file->SearchBytePattern(ptnJumpToVcmdYSFR, ofsBranchForVcmd)) {
		addrVoiceCmdAddressTable = file->GetShort(ofsBranchForVcmd + 9);

		uint32_t ofsReadVcmdLength;
		if (file->SearchBytePattern(ptnReadVcmdLengthYSFR, ofsReadVcmdLength)) {
			firstVoiceCmd = file->GetByte(ofsReadVcmdLength + 2);
			addrVoiceCmdLengthTable = file->GetShort(ofsReadVcmdLength + 7);

			if (firstVoiceCmd == 0xe0) {
				version = NINSNES_UNKNOWN; // TODO: set different version code (Yoshi's Safari)
			}
		}
		else {
			return;
		}
	}
	else {
		return;
	}

	// CLASSIFY DERIVED VERSIONS (fix false-positive)
	if (version == NINSNES_STANDARD)
	{
		UINT ofsDispatchNote;
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
					version = NINSNES_STANDARD;
				}
				else {
					// compatible version?
					version = NINSNES_STANDARD;
				}
			}
			else {
				version = NINSNES_UNKNOWN;

				if (file->SearchBytePattern(ptnDispatchNoteFE3, ofsDispatchNote)) {
					if (firstVoiceCmd == 0xd6) {
						version = NINSNES_INTELLI_FE3;
					}
				}
				else if (file->SearchBytePattern(ptnDispatchNoteFE4, ofsDispatchNote)) {
					if (firstVoiceCmd == 0xda) {
						version = NINSNES_INTELLI_FE4;
					}
				}
			}
		}
	}

	// GUESS CURRENT SONG NUMBER
	int8_t guessedSongIndex = -1;

	// scan for a song that contains the current section
	// (note that the section pointer points to the "next" section actually)
	// edge case: Super Mario World - Valley of Bowser
	uint16_t addrCurrentSection = file->GetShort(addrSectionPtr) - 2;
	if (addrCurrentSection >= 0x0100 && addrCurrentSection < 0xfff0) {
		int8_t songIndexCandidate = -1;
		uint16_t bestSectionDistance = 0xffff;

		for (int songIndex = 0; songIndex <= 0x7f; songIndex++) {
			UINT addrSectionListPtr = addrSongList + songIndex * 2;
			if (addrSectionListPtr + 2 > 0x10000) {
				break;
			}

			uint16_t firstSectionAddress = file->GetShort(addrSectionListPtr);
			if (version == NINSNES_KONAMI) {
				firstSectionAddress += konamiBaseAddress;
			}
			if (firstSectionAddress > addrCurrentSection) {
				continue;
			}

			uint16_t curAddress = firstSectionAddress;
			if ((addrCurrentSection % 2) == (curAddress % 2)) {
				while (curAddress >= 0x0100 && curAddress < 0xfff0) {
					uint16_t addrSection = file->GetShort(curAddress);
					if (version == NINSNES_KONAMI) {
						addrSection += konamiBaseAddress;
					}

					if (curAddress == addrCurrentSection) {
						uint16_t sectionDistance = addrCurrentSection - firstSectionAddress;
						if (sectionDistance < bestSectionDistance) {
							songIndexCandidate = songIndex;
							bestSectionDistance = sectionDistance;
						}
						break;
					}

					if ((addrSection & 0xff00) == 0) {
						// section list end / jump
						break;
					}

					curAddress += 2;
				}

				if (bestSectionDistance == 0) {
					break;
				}
			}
		}

		guessedSongIndex = songIndexCandidate;
	}

	// acquire song index from APU port
	if (guessedSongIndex == -1) {
		int8_t songIndexCandidate;

		switch (version)
		{
		case NINSNES_EARLIER:
			songIndexCandidate = file->GetByte(0xf6);
			break;

		case NINSNES_STANDARD:
			songIndexCandidate = file->GetByte(0xf4);
			break;
		}

		if (songIndexCandidate >= 0 && songIndexCandidate <= 0x7f) {
			guessedSongIndex = songIndexCandidate;
		}
	}

	// use first song if no hints available
	if (guessedSongIndex == -1) {
		guessedSongIndex = 1;
	}
	
	// load the song
	uint16_t addrSongStart = file->GetShort(addrSongList + guessedSongIndex * 2);
	if (version == NINSNES_KONAMI) {
		addrSongStart += konamiBaseAddress;
	}

	NinSnesSeq* newSeq = new NinSnesSeq(file, version, addrSongStart, name);
	newSeq->konamiBaseAddress = konamiBaseAddress;
	if (!newSeq->LoadVGMFile())
	{
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
	if (file->SearchBytePattern(ptnLoadInstrTableAddress, ofsLoadInstrTableAddressASM))
	{
		addrInstrTable = file->GetByte(ofsLoadInstrTableAddressASM + 7) | (file->GetByte(ofsLoadInstrTableAddressASM + 10) << 8);
	}
	else if (file->SearchBytePattern(ptnLoadInstrTableAddressSMW, ofsLoadInstrTableAddressASM))
	{
		addrInstrTable = file->GetByte(ofsLoadInstrTableAddressASM + 3) | (file->GetByte(ofsLoadInstrTableAddressASM + 6) << 8);
	}
	else
	{
		return;
	}

	// scan for DIR address
	uint8_t spcDIR;
	UINT ofsSetDIRASM;
	if (file->SearchBytePattern(ptnSetDIR, ofsSetDIRASM))
	{
		spcDIR = file->GetByte(ofsSetDIRASM + 4);
	}
	else if (file->SearchBytePattern(ptnSetDIRYI, ofsSetDIRASM))
	{
		spcDIR = file->GetByte(ofsSetDIRASM + 1);
	}
	else if (file->SearchBytePattern(ptnSetDIRSMW, ofsSetDIRASM))
	{
		spcDIR = file->GetByte(ofsSetDIRASM + 9);
	}
	else
	{
		return;
	}

	NinSnesInstrSet * newInstrSet = new NinSnesInstrSet(file, version, addrInstrTable, spcDIR << 8);
	if (!newInstrSet->LoadVGMFile())
	{
		delete newInstrSet;
		return;
	}
}

void NinSnesScanner::SearchForNinSnesFromROM (RawFile* file)
{
}
