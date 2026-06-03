/*
* VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include "base/Types.h"
#include "VGMInstrSet.h"
#include "VGMRgn.h"

/****************************************************************/
/*																*/
/*					Define of structs							*/
/*																*/
/****************************************************************/
//==============================================================
//		Header of wds
//--------------------------------------------------------------
//	Memo:
//			Offset Address	= 0x0000
//			Size			= 0x0030
//--------------------------------------------------------------
struct WdsHdr {
  u32 sig;                //strings "wds "
  s32 unknown_0004;
  u32 iFFT_InstrSetSize;    //size of entire instrset (header + instrs + samps)
  //but FFT only, so we ignore it
  s32 unknown_000C;
  u32 szHeader1;            //size of header???
  u32 szSampColl;            //size of AD-PCM body (.VB) ???
  u32 szHeader2;            //size of header???
  u32 iNumInstrs;            //Quantity of instruments.
  u32 iBank;                //Bank No.
  s32 unknown_0024;
  s32 unknown_0028;
  s32 unknown_002C;
};


//==============================================================
//		Attribute of wds
//--------------------------------------------------------------
//	Memo:
//			Offset Address	= 0x0030
//			Size			= 0x0010 × iQuantity
//--------------------------------------------------------------
struct WdsRgnData {
  u32 ptBody;                    //Offset address to AD-PCM 波形実体
  u16 ptLoop;                    //size?  loop?  unknown
  u8 iFineTune;                // Pitch table is at 800290D8 in FFT.  See function at 80017424
  //  takes $a0: u16- MSB = semitone (note+semitone_tune),
  //                  LSB = fine tune index
  s8 iSemiToneTune;    // Pitch tune in semitones (determines unitykey)
  u8 Ar;                // & 0x7F attack rate
  u8 Dr;                // & 0x0F decay rate
  u8 Sr;                // & 0x7F sustain rate
  u8 Rr;                // & 0x1F release rate
  u8 Sl;                // & 0x0F sustain level
  u8 Am;                // & 0x01 attack rate linear (0)or exponential(1)  UNSURE
  u8 unk_E;
  u8 unk_F;
};





/****************************************************************/
/*																*/
/*					Define of structs							*/
/*																*/
/****************************************************************/
//==============================================================
//		Instrument Set		(Bank全体)
//--------------------------------------------------------------
class WdsInstrSet:
    public VGMInstrSet {
 public:
  WdsInstrSet(RawFile *file, u32 offset);
  ~WdsInstrSet() override;

  bool parseHeader() override;    //ヘッダーの処理
  //ここで、Object"VabSampColl"を生成するべき？
  //（スキャナーでVBを検索すると、複数有るから解らなくなる。）
  bool parseInstrPointers() override;    //音色Object"WdsInstr"を生成する。
  //"aInstrs"に、登録する。
  //各音色毎の処理

  enum Version { VERSION_DWDS, VERSION_WDS };

 public:
  WdsHdr hdr{};
  Version version{};

/*	member of "VGMInstrSet"
	VGMInstrSet::aInstrs		//音色情報のvector
	VGMInstrSet::dls			//class DLSを作る用
	VGMInstrSet::menu			//
	VGMInstrSet::sampColl		//波形実体のオブジェクト
*/
};


//==============================================================
//		Program information			( 1 Instrument)
//--------------------------------------------------------------
class WdsInstr
    : public VGMInstr {
 public:
  WdsInstr(VGMInstrSet *instrSet, u32 offset, u32 length, u32 bank, u32 instrNum);
  ~WdsInstr() override;

  bool loadInstr() override;    //Object "WdsRgn"の生成、
  //"WdsRgn->LoadRgn()"の呼び出し
  //member "aRgns" へオブジェクトのポインタを登録

 public:
  WdsRgnData rgndata{};

/*	member of "VGMInstr"
	VGMInstr::aRgns				// 
	VGMInstr::bank				// bank number
	VGMInstr::instrNum			// program number
	VGMInstr::parInstrSet		// 
*/

};
