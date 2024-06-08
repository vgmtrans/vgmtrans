/*
* VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once
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
  uint32_t sig;                //strings "wds "
  int32_t unknown_0004;
  uint32_t iFFT_InstrSetSize;    //size of entire instrset (header + instrs + samps)
  //but FFT only, so we ignore it
  int32_t unknown_000C;
  uint32_t szHeader1;            //size of header???
  uint32_t szSampColl;            //size of AD-PCM body (.VB) ???
  uint32_t szHeader2;            //size of header???
  uint32_t iNumInstrs;            //Quantity of instruments.
  uint32_t iBank;                //Bank No.
  int32_t unknown_0024;
  int32_t unknown_0028;
  int32_t unknown_002C;
};


//==============================================================
//		Attribute of wds
//--------------------------------------------------------------
//	Memo:
//			Offset Address	= 0x0030
//			Size			= 0x0010 × iQuantity
//--------------------------------------------------------------
struct WdsRgnData {
  uint32_t ptBody;                    //Offset address to AD-PCM 波形実体
  uint16_t ptLoop;                    //size?  loop?  unknown
  uint8_t iFineTune;                // Pitch table is at 800290D8 in FFT.  See function at 80017424
  //  takes $a0: uint16_t- MSB = semitone (note+semitone_tune),
  //                  LSB = fine tune index
  int8_t iSemiToneTune;    // Pitch tune in semitones (determines unitykey)
  uint8_t Ar;                // & 0x7F attack rate
  uint8_t Dr;                // & 0x0F decay rate
  uint8_t Sr;                // & 0x7F sustain rate
  uint8_t Rr;                // & 0x1F release rate
  uint8_t Sl;                // & 0x0F sustain level
  uint8_t Am;                // & 0x01 attack rate linear (0)or exponential(1)  UNSURE
  uint8_t unk_E;
  uint8_t unk_F;
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
  WdsInstrSet(RawFile *file, uint32_t offset);
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
  WdsInstr(VGMInstrSet *instrSet, uint32_t offset, uint32_t length, uint32_t theBank, uint32_t theInstrNum);
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
