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

	UINT ofsLoadInstrTableAddressASM;
	UINT ofsSetDIRASM;
	UINT addrInstrTable;

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
		"?"
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
		"x?xx??x?"
		,
		16);

	// END DYNAMIC PATTERN DEFINITIONS

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
	else
	{
		return;
	}

	// TODO: version detection
	version = NINSNES_STANDARD;

	// guess current song number
	// TODO: add heuristic search
	int8_t guessedSongIndex = -1;
	switch (version)
	{
	case NINSNES_STANDARD:
		guessedSongIndex = file->GetByte(0xf4);
		break;

	default:
		guessedSongIndex = 1;
		break;
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
	if (file->SearchBytePattern(ptnLoadInstrTableAddress, ofsLoadInstrTableAddressASM))
	{
		addrInstrTable = file->GetByte(ofsLoadInstrTableAddressASM + 7) | (file->GetByte(ofsLoadInstrTableAddressASM + 10) << 8);
	}
	else
	{
		return;
	}

	// scan for DIR address
	uint8_t spcDIR;
	if (file->SearchBytePattern(ptnSetDIR, ofsSetDIRASM))
	{
		spcDIR = file->GetByte(ofsSetDIRASM + 4);
	}
	else if (file->SearchBytePattern(ptnSetDIRYI, ofsSetDIRASM))
	{
		spcDIR = file->GetByte(ofsSetDIRASM + 1);
	}
	else
	{
		return;
	}

	NinSnesInstrSet * newInstrSet = new NinSnesInstrSet(file, addrInstrTable, spcDIR << 8);
	if (!newInstrSet->LoadVGMFile())
	{
		delete newInstrSet;
		return;
	}

#if 0
	// BEGIN STANDARD VERSION DETECTION

	uint8_t firstVoiceCmd = 0;
	uint16_t addrVoiceCmdAddressTable = 0;
	uint16_t addrVoiceCmdLengthTable = 0;

	// find a branch for voice command,
	// and acquire the following info for standard engines.
	// 
	// - First voice command (usually $e0)
	// - Voice command address table
	// - Voice command length table

	uint32_t ofsBranchForVcmd = 0;
	uint32_t ofsJumpToVcmd = 0;
	if (file->SearchBytePattern(ptnBranchForVcmd, ofsBranchForVcmd))
	{
		firstVoiceCmd = file->GetByte(ofsBranchForVcmd + 1);

		// find a jump to address_table[cmd * 2]
		if (file->SearchBytePattern(ptnJumpToVcmd, ofsJumpToVcmd))
		{
			addrVoiceCmdAddressTable = file->GetShort(ofsJumpToVcmd + 7) + ((firstVoiceCmd * 2) & 0xff);
			addrVoiceCmdLengthTable = file->GetShort(ofsJumpToVcmd + 14) + (firstVoiceCmd & 0x7f);
			version = NINSNES_STANDARD;
		}
	}

	// END STANDARD VERSION DETECTION
#endif
}

void NinSnesScanner::SearchForNinSnesFromROM (RawFile* file)
{
}

/*
void NinSnesScanner::SearchForNinSnesSeq (RawFile* file)
{
	uint32_t nFileLength = file->size();
	if (nFileLength < 0x10000 || nFileLength > 0x10500)
		return;

	// a small logic to prevent a false positive
	uint32_t ofsBranchForVcmd;
	if (!file->SearchBytePattern(ptnBranchForVcmd, ofsBranchForVcmd))
	{
		return;
	}

	for (uint32_t i = 0x100; i+1 < nFileLength; i++)
	{
		uint16_t theShort2;
		uint16_t theShort = file->GetShort(i);
		if (theShort > i && theShort < i + 0x200)
		{
			bool bFailed = false;
			int m = 0;
			while (true)
			{
				m+=2;
				if (i+m <= nFileLength-2)
				{
					theShort2 = file->GetShort(i+m);
					if (theShort2 > i+m && theShort2 < i+m+0x200)
						continue;
					else if (theShort2 == 0 || theShort2 < 0x100)//== 0xFF)
						break;
					else
					{
						bFailed = true;
						break;
					}
				}
				else
				{
					bFailed = true;
					break;
				}
			}
			if (!bFailed)
			{
				for (int n=0; n<m; n+=2)
				{
					for (int p=0; p<8; p++)
					{
						theShort = file->GetShort(i+n);
						uint16_t tempShort;
						if ((uint32_t)(theShort+p*2+1) < nFileLength)
							tempShort = file->GetShort(theShort+p*2);
						else
						{
							bFailed = true;
							break;
						}
						if ((uint32_t)(tempShort+1) < nFileLength)
						{
							if ((tempShort <= i || tempShort >= i+0x3000) && tempShort != 0)
								bFailed = true;
						}
					}
				}
				if (!bFailed)
				{
					NinSnesSeq* newSeq = new NinSnesSeq(file, NINSNES_STANDARD, i);
					if (!newSeq->LoadVGMFile())
						delete newSeq;
					i += m;
				}
			}
		}
	}
}
*/
