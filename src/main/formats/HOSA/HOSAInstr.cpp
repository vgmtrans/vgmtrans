/*
* VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#include "HOSAInstr.h"
#include "VGMRgn.h"
#include "PSXSPU.h"

/****************************************************************/
/*																*/
/*			Instrument Set		(Bank全体)						*/
/*																*/
/****************************************************************/
//==============================================================
//		Constructor
//--------------------------------------------------------------
HOSAInstrSet::HOSAInstrSet(RawFile *file, uint32_t offset)
    : VGMInstrSet(HOSAFormat::name, file, offset, 0, "HOSAWAH") {
}

//==============================================================
//		Destructor
//--------------------------------------------------------------
HOSAInstrSet::~HOSAInstrSet(void) {
}


//==============================================================
//		ヘッダー情報の取得
//--------------------------------------------------------------
//	Memo:
//		VGMInstrSet::Load()関数から呼ばれる
//==============================================================
bool HOSAInstrSet::parseHeader() {

  //"hdr"構造体へそのまま転送
  readBytes(offset(), sizeof(InstrHeader), &instrheader);
  setId(0);                        //Bank number.

  //ヘッダーobjectの生成
  VGMHeader *wdsHeader = addHeader(offset(), sizeof(InstrHeader));
  wdsHeader->addSig(offset(), 8);
  wdsHeader->addChild(offset() + 8, sizeof(uint32_t), "Number of Instruments");

  //波形objectの生成
//	sampColl = new PSXSampColl(HOSAFormat::name, this, 0x00160800); // moved to HOSAScanner
//	sampColl->Load();				//VGMInstrSet::Load()関数内でやっている。
//	sampColl->UseInstrSet(this);	//"WD.cpp"では、同様の事をやっている。

  return true;
}

//==============================================================
//		各音色の情報取得
//--------------------------------------------------------------
//	Memo:
//		VGMInstrSet::Load()関数から呼ばれる
//==============================================================
bool HOSAInstrSet::parseInstrPointers() {

  uint32_t iOffset = offset() + sizeof(InstrHeader);    //pointer of attribute table

  //音色数だけ繰り返す。
  for (unsigned int i = 0; i < instrheader.numInstr; i++) {
    HOSAInstr *newInstr = new HOSAInstr(this, offset() + readWord(iOffset), 0, i / 0x80, i % 0x80);
    aInstrs.push_back(newInstr);
    iOffset += 4;
  }

  return true;

}


/****************************************************************/
/*																*/
/*			Program information			( 1 Instrument)			*/
/*																*/
/****************************************************************/
//==============================================================
//		Constructor
//--------------------------------------------------------------
HOSAInstr::HOSAInstr(VGMInstrSet *instrSet, uint32_t offset, uint32_t length, uint32_t theBank, uint32_t theInstrNum)
    : VGMInstr(instrSet, offset, length, theBank, theInstrNum),
      rgns(nullptr) {
}

//==============================================================
//		Make the Object "WdsRgn" (Attribute table)
//--------------------------------------------------------------
bool HOSAInstr::loadInstr() {
  if (offset() + sizeof(InstrInfo) > vgmFile()->endOffset()) {
    return false;
  }

  // Get the instr data
  readBytes(offset(), sizeof(InstrInfo), &instrinfo);
  setLength(sizeof(InstrInfo) + sizeof(RgnInfo) * instrinfo.numRgns);
  addChild(offset(), sizeof(uint32_t), "Number of Rgns");

  // Get the rgn data
  rgns = new RgnInfo[instrinfo.numRgns];
  readBytes((offset() + sizeof(InstrInfo)), (sizeof(RgnInfo) * instrinfo.numRgns), rgns);

  //ATLTRACE("LOADED INSTR   ProgNum: %X    BankNum: %X\n", instrinfo.progNum, instrinfo.bankNum);

  uint8_t cKeyLow = 0x00;
  for (unsigned int i = 0; i < instrinfo.numRgns; i++) {
    RgnInfo *rgninfo = &rgns[i];
    VGMRgn *rgn = new VGMRgn(this, offset() + sizeof(InstrInfo) + sizeof(RgnInfo) * i, sizeof(RgnInfo));

    rgn->addChild(rgn->offset(), 4, "Sample Offset");
    rgn->sampOffset = rgninfo->sampOffset; //+ ((VGMInstrSet*)this->vgmfile)->sampColl->offset();

    rgn->velLow = 0x00;
    rgn->velHigh = 0x7F;
    rgn->keyLow = cKeyLow;
    rgn->addKeyHigh(rgninfo->note_range_high, rgn->offset() + 0x05);
    cKeyLow = (rgninfo->note_range_high) + 1;

    rgn->addUnityKey(static_cast<int8_t>(0x3C)+ 0x3C - rgninfo->iSemiToneTune, rgn->offset() + 0x06);
    rgn->addChild(rgn->offset() + 0x07, 1, "Semi Tone Tune");
    rgn->fineTune = static_cast<int16_t>(rgninfo->iFineTune * (100.0 / 256.0));

    // Might want to simplify the code below.  I'm being nitpicky.
    if (rgninfo->iPan == 0x80) rgn->pan = 0;
    else if (rgninfo->iPan == 0xFF) rgn->pan = 1.0;
    else if ((rgninfo->iPan == 0xC0) ||
        (rgninfo->iPan < 0x80))
      rgn->pan = 0.5;
    else rgn->pan = static_cast<double>(rgninfo->iPan - 0x80) / 0x7F;

    // The ADSR value ordering is all messed up for the hell of it.  This was a bitch to reverse-engineer.
    rgn->addChild(rgn->offset() + 0x0C, 4, "ADSR Values (non-standard ordering)");
    uint8_t Ar = (rgninfo->ADSR_vals >> 20) & 0x7F;
    uint8_t Dr = (rgninfo->ADSR_vals >> 16) & 0xF;
    uint8_t Sr = ((rgninfo->ADSR_vals >> 8) & 0xFF) >> 1;
    uint8_t Rr = (rgninfo->ADSR_vals >> 4) & 0x1F;
    uint8_t Sl = rgninfo->ADSR_vals & 0xF;
    uint8_t Am = (((rgninfo->ADSR_Am & 0xF) ^ 5) < 1) ? 1 : 0;    //Not sure what other role this nibble plays, if any.
    psxConvADSR(rgn, Am, Ar, Dr, Sl, 1, 1, Sr, 1, Rr, false);

    // Unsure if volume is using a linear scale, but it sounds like it.
    double vol = rgninfo->volume / 255.0;
    rgn->setVolume(vol);
    addRgn(rgn);
  }
  return true;
}
