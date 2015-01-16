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
	uint32_t ofsIncSectionPtr = 0;
	uint8_t addrSectionPtr;
	if (file->SearchBytePattern(ptnIncSectionPtr, ofsIncSectionPtr))
	{
		addrSectionPtr = file->GetByte(ofsIncSectionPtr + 3);
	}
	else
	{
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

	// END DYNAMIC PATTERN DEFINITIONS

	// ACQUIRE SEQUENCE LIST ADDRESS:
	// find the initialization code of the section pointer,
	// and acquire the sequence list address
	uint32_t ofsInitSectionPtr = 0;
	uint32_t addrSongList = 0;
	if (file->SearchBytePattern(ptnInitSectionPtr, ofsInitSectionPtr))
	{
		addrSongList = file->GetShort(ofsInitSectionPtr + 5);
	}
	else if (file->SearchBytePattern(ptnInitSectionPtrYI, ofsInitSectionPtr))
	{
		addrSongList = file->GetShort(ofsInitSectionPtr + 12);
	}
	else if (file->SearchBytePattern(ptnInitSectionPtrSMW, ofsInitSectionPtr))
	{
		addrSongList = file->GetShort(ofsInitSectionPtr + 3);
	}
	else
	{
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

	uint32_t ofsBranchForVcmd = 0;
	if (file->SearchBytePattern(ptnBranchForVcmd, ofsBranchForVcmd))
	{
		uint8_t firstVoiceCmd = 0;
		uint32_t ofsJumpToVcmd = 0;
		uint32_t ofsReadVcmdLength = 0;
		uint16_t addrVoiceCmdAddressTable = 0;
		uint16_t addrVoiceCmdLengthTable = 0;

		firstVoiceCmd = file->GetByte(ofsBranchForVcmd + 1);

		// find a jump to address_table[cmd * 2]
		if (file->SearchBytePattern(ptnJumpToVcmd, ofsJumpToVcmd))
		{
			addrVoiceCmdAddressTable = file->GetShort(ofsJumpToVcmd + 7) + ((firstVoiceCmd * 2) & 0xff);
			addrVoiceCmdLengthTable = file->GetShort(ofsJumpToVcmd + 14) + (firstVoiceCmd & 0x7f);
			version = NINSNES_STANDARD;
		}
		else if (file->SearchBytePattern(ptnJumpToVcmdSMW, ofsJumpToVcmd)) {
			// search vcmd length table as well
			if (file->SearchBytePattern(ptnReadVcmdLengthSMW, ofsReadVcmdLength)) {
				addrVoiceCmdAddressTable = file->GetShort(ofsJumpToVcmd + 5) + ((firstVoiceCmd * 2) & 0xff);
				addrVoiceCmdLengthTable = file->GetShort(ofsReadVcmdLength + 9) + firstVoiceCmd;
				version = NINSNES_EARLIER;
			}
		}
	}

	// GUESS CURRENT SONG NUMBER
	int8_t guessedSongIndex = -1;

	// scan for a song that contains the current section
	uint16_t addrCurrentSection = file->GetShort(addrSectionPtr);
	if (addrCurrentSection >= 0x0100 && addrCurrentSection < 0xfff0) {
		int8_t songIndexCandidate = -1;
		uint16_t bestSectionDistance = 0xffff;

		for (int songIndex = 0; songIndex <= 0x7f; songIndex++) {
			UINT addrSectionListPtr = addrSongList + songIndex * 2;
			if (addrSectionListPtr >= 0x10000) {
				break;
			}

			uint16_t firstSectionAddress = file->GetShort(addrSectionListPtr);
			if (firstSectionAddress > addrCurrentSection) {
				continue;
			}

			uint16_t curAddress = firstSectionAddress;
			if ((addrCurrentSection % 2) == (curAddress % 2)) {
				while (curAddress >= 0x0100 && curAddress < 0xfff0) {
					uint16_t addrSection = file->GetShort(curAddress);

					if ((addrSection & 0xff00) == 0) {
						// section list end / jump
						break;
					}

					if (curAddress == addrCurrentSection) {
						uint16_t sectionDistance = addrCurrentSection - firstSectionAddress;
						if (sectionDistance < bestSectionDistance) {
							songIndexCandidate = songIndex;
							bestSectionDistance = sectionDistance;
						}
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
	NinSnesSeq* newSeq = new NinSnesSeq(file, version, addrSongStart, name);
	if (!newSeq->LoadVGMFile())
	{
		delete newSeq;
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
