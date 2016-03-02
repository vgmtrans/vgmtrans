#include "pch.h"
#include "NeverlandSnesFormat.h"
#include "NeverlandSnesSeq.h"

//; Lufia SPC
//16c3: 8f 10 08  mov   $08,#$10
//16c6: 8f 28 09  mov   $09,#$28
//16c9: cd 04     mov   x,#$04
//16cb: e8 00     mov   a,#$00
//16cd: d5 18 03  mov   $0318+x,a
//16d0: 3d        inc   x
//16d1: c8 08     cmp   x,#$08
//16d3: d0 f8     bne   $16cd
//16d5: 8d 00     mov   y,#$00
//16d7: f7 08     mov   a,($08)+y
//16d9: d6 00 02  mov   $0200+y,a
//16dc: 10 0e     bpl   $16ec
BytePattern NeverlandSnesScanner::ptnLoadSongSFC(
	"\x8f\x10\x08\x8f\x28\x09\xcd\x04"
	"\xe8\x00\xd5\x18\x03\x3d\xc8\x08"
	"\xd0\xf8\x8d\x00\xf7\x08\xd6\x00"
	"\x02\x10\x0e"
	,
	"xx?x??xx"
	"xxx??xxx"
	"xxxxx?x?"
	"?x?"
	,
	27);

//; Lufia II SPC
//356e: 8f 10 18  mov   $18,#$10
//3571: 8f 44 19  mov   $19,#$44
//3574: cd 00     mov   x,#$00
//3576: 8d 00     mov   y,#$00
//3578: f4 3d     mov   a,$3d+x
//357a: 28 02     and   a,#$02
//357c: d4 3d     mov   $3d+x,a
//357e: f7 18     mov   a,($18)+y
//3580: 68 ff     cmp   a,#$ff
//3582: f0 08     beq   $358c
BytePattern NeverlandSnesScanner::ptnLoadSongS2C(
	"\x8f\x10\x18\x8f\x44\x19\xcd\x00"
	"\x8d\x00\xf4\x3d\x28\x02\xd4\x3d"
	"\xf7\x18\x68\xff\xf0\x08"
	,
	"xx?x??xx"
	"xxx?xxx?"
	"x?xxx?"
	,
	22);

void NeverlandSnesScanner::Scan(RawFile *file, void *info) {
  uint32_t nFileLength = file->size();
  if (nFileLength == 0x10000) {
    SearchForNeverlandSnesFromARAM(file);
  }
  else {
    SearchForNeverlandSnesFromROM(file);
  }
  return;
}

void NeverlandSnesScanner::SearchForNeverlandSnesFromARAM(RawFile *file) {
  NeverlandSnesVersion version = NEVERLANDSNES_NONE;

  std::wstring basefilename = RawFile::removeExtFromPath(file->GetFileName());
  std::wstring name = file->tag.HasTitle() ? file->tag.title : basefilename;

  uint32_t ofsLoadSong;
  uint16_t addrSeqHeader;
  if (file->SearchBytePattern(ptnLoadSongS2C, ofsLoadSong)) {
    addrSeqHeader = file->GetByte(ofsLoadSong + 4) << 8;
    version = NEVERLANDSNES_S2C;
  }
  else if (file->SearchBytePattern(ptnLoadSongSFC, ofsLoadSong)) {
    addrSeqHeader = file->GetByte(ofsLoadSong + 4) << 8;
    version = NEVERLANDSNES_SFC;
  }
  else {
    return;
  }

  NeverlandSnesSeq *newSeq = new NeverlandSnesSeq(file, version, addrSeqHeader);
  if (!newSeq->LoadVGMFile()) {
    delete newSeq;
    return;
  }
}

void NeverlandSnesScanner::SearchForNeverlandSnesFromROM(RawFile *file) {
}
