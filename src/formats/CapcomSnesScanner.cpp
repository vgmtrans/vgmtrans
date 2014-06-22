#include "stdafx.h"
#include "CapcomSnesScanner.h"
#include "CapcomSnesSeq.h"
#include "SNESDSP.h"

using namespace std;

// ; Super Ghouls 'N Ghosts SPC
// 03f5: 1c        asl   a
// 03f6: 5d        mov   x,a
// 03f7: f5 03 0e  mov   a,$0e03+x         ; NOTE: some games read LSB first!
// 03fa: c4 c0     mov   $c0,a
// 03fc: f5 02 0e  mov   a,$0e02+x         ; read song header address from song list
// 03ff: c4 c1     mov   $c1,a
// 0401: 04 c0     or    a,$c0
// 0403: f0 dd     beq   $03e2             ; return if song header address == $0000
BytePattern CapcomSnesScanner::ptnReadSongList(
	"\x1c\x5d\xf5\x03\x0e\xc4\xc0\xf5"
	"\x02\x0e\xc4\xc1\x04\xc0\xf0\xdd"
	,
	"xxx??x?x"
	"??x?x?x?"
	,
	16);

//; Mega Man X SPC
// 059f: 6f        ret
// 
// 05a0: 3f ef 06  call  $06ef
// 05a3: 8f 0d a1  mov   $a1,#$0d
// 05a6: 8f af a0  mov   $a0,#$af          ; song address = $0daf (+1 for actual start address)
// ; fall through...
// ; or when song header first byte == 0:
// 05a9: 3f 82 05  call  $0582
// 05ac: 8d 00     mov   y,#$00
// 05ae: dd        mov   a,y
BytePattern CapcomSnesScanner::ptnReadBGMAddress(
	"\x6f\x3f\xef\x06\x8f\x0d\xa1\x8f"
	"\xaf\xa0\x3f\x82\x05\x8d\x00\xdd"
	,
	"xx??x??x"
	"??x??xxx"
	,
	16);

void CapcomSnesScanner::Scan(RawFile* file, void* info)
{
	uint32_t nFileLength = file->size();
	if (nFileLength == 0x10000)
	{
		SearchForCapcomSnesFromARAM(file);
	}
	else
	{
		SearchForCapcomSnesFromROM(file);
	}
	return;
}

void CapcomSnesScanner::SearchForCapcomSnesFromARAM (RawFile* file)
{
	CapcomSnesVersion version = NONE;

	UINT ofsReadSongListASM;
	UINT ofsReadBGMAddressASM;
	bool hasSongList;
	bool bgmAtFixedAddress;
	UINT addrSongList;
	UINT addrBGMHeader;

	wstring name = file->tag.HasTitle() ? file->tag.title : RawFile::removeExtFromPath(file->GetFileName());

	// find a song list
	hasSongList = file->SearchBytePattern(ptnReadSongList, ofsReadSongListASM);
	if (hasSongList)
	{
		UINT addrSongListPtr = min(file->GetShort(ofsReadSongListASM + 3), file->GetShort(ofsReadSongListASM + 8));
		addrSongList = file->GetShort(addrSongListPtr);
	}

	// find BGM address
	bgmAtFixedAddress = file->SearchBytePattern(ptnReadBGMAddress, ofsReadBGMAddressASM);
	if (bgmAtFixedAddress)
	{
		addrBGMHeader = (file->GetByte(ofsReadBGMAddressASM + 5) << 8) | file->GetByte(ofsReadBGMAddressASM + 8);
	}

	// guess engine version
	if (hasSongList)
	{
		if (bgmAtFixedAddress)
		{
			version = V2_BGM_USUALLY_AT_FIXED_LOCATION;

			// Some games still use BGM/SFX list, apparently.
			// - The Magical Quest Starring Mickey Mouse
			// - Captain Commando
			bool bgmHeaderCoversSongList = (addrBGMHeader <= addrSongList && addrBGMHeader + 17 > addrSongList);
			if (bgmHeaderCoversSongList || !IsValidBGMHeader(file, addrBGMHeader))
			{
				bgmAtFixedAddress = false;
			}
		}
		else
		{
			version = V1_BGM_IN_LIST;
		}
	}
	else
	{
		version = V3_BGM_FIXED_LOCATION;
	}

	// load a sequence from BGM region
	if (bgmAtFixedAddress)
	{
		CapcomSnesSeq* newSeq = new CapcomSnesSeq(file, version, addrBGMHeader + 1, false, name);
		if (!newSeq->LoadVGMFile())
		{
			delete newSeq;
			return;
		}
	}

	// TODO: load songs from list

	// TODO: scan samples
}

void CapcomSnesScanner::SearchForCapcomSnesFromROM (RawFile* file)
{
}

bool CapcomSnesScanner::IsValidBGMHeader (RawFile* file, UINT addrSongHeader)
{
	if (addrSongHeader + 17 > 0x10000)
	{
		return false;
	}

	for (int track = 0; track < 8; track++)
	{
		UINT addrScoreData = file->GetShortBE(addrSongHeader + 1 + track * 2);
		if ((addrScoreData & 0xff00) == 0) {
			return false;
		}
	}

	return true;
}
