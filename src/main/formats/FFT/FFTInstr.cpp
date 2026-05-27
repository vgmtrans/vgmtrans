#include "base/types.h"
#include "FFTFormat.h"
#include "FFTInstr.h"
#include "VGMSamp.h"
#include "PSXSPU.h"

const float defaultFFTReverbPercent = 0.5;

/****************************************************************/
/*																*/
/*			Instrument Set		(Bank全体)						*/
/*																*/
/****************************************************************/
//==============================================================
//		Constructor
//--------------------------------------------------------------
WdsInstrSet::WdsInstrSet(RawFile *file, u32 offset) :
    VGMInstrSet(FFTFormat::name, file, offset) {
}

//==============================================================
//		Destructor
//--------------------------------------------------------------
WdsInstrSet::~WdsInstrSet(void) {
}

//==============================================================
//		ヘッダー情報の取得
//--------------------------------------------------------------
//	Memo:
//		VGMInstrSet::Load()関数から呼ばれる
//==============================================================
bool WdsInstrSet::parseHeader() {

  //"hdr"構造体へそのまま転送
  readBytes(offset(), sizeof(WdsHdr), &hdr);
  setLength(hdr.szHeader1 + hdr.szSampColl);    //header size + samp coll size
  setId(hdr.iBank);                        //Bank number.

  if (hdr.sig == 0x73647764)
    version = VERSION_DWDS;
  else if (hdr.sig == 0x20736477)
    version = VERSION_WDS;

  //バイナリエディタ表示用
  setName(fmt::format("wds {:d}", id()));

  //ヘッダーobjectの生成
  VGMHeader *wdsHeader = addHeader(offset(), sizeof(WdsHdr));
  wdsHeader->addSig(offset(), sizeof(long));
  wdsHeader->addUnknownChild(offset() + 0x04, sizeof(long));
  wdsHeader->addChild(offset() + 0x08, sizeof(long), "Header size? (0)");
  wdsHeader->addUnknownChild(offset() + 0x0C, sizeof(long));
  wdsHeader->addChild(offset() + 0x10, sizeof(long), "Header size? (1)");
  wdsHeader->addChild(offset() + 0x14, sizeof(long), "AD-PCM body(.VB) size");
  wdsHeader->addChild(offset() + 0x18, sizeof(long), "Header size? (2)");
  wdsHeader->addChild(offset() + 0x1C, sizeof(long), "Number of Instruments");
  wdsHeader->addChild(offset() + 0x20, sizeof(long), "Bank number");
  wdsHeader->addUnknownChild(offset() + 0x24, sizeof(long));
  wdsHeader->addUnknownChild(offset() + 0x28, sizeof(long));
  wdsHeader->addUnknownChild(offset() + 0x2C, sizeof(long));

  //波形objectの生成
  sampColl = new PSXSampColl(FFTFormat::name, this, offset() + hdr.szHeader1, hdr.szSampColl);
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
bool    WdsInstrSet::parseInstrPointers() {

  u32 iOffset = offset() + sizeof(WdsHdr);    //pointer of attribute table

  //音色数だけ繰り返す。
  for (unsigned int i = 0; i <= hdr.iNumInstrs; i++) {
    //WdsInstr* newInstr = new WdsInstr(this,iOffset,sizeof(WdsRgnData),hdr.iBank,i);		//0 … hdr.iBank
    WdsInstr *newInstr = new WdsInstr(this, iOffset, sizeof(WdsRgnData), i / 128, i % 128);        //0 … hdr.iBank
    aInstrs.push_back(newInstr);
    //	newInstr->LoadInstr();		//VGMInstrSet::Load()関数内（LoadInstrs()）でやっている。
    iOffset += sizeof(WdsRgnData);    // size = 0x0010
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
WdsInstr::WdsInstr(VGMInstrSet *instrSet, u32 offset, u32 length, u32 theBank, u32 theInstrNum) :
    VGMInstr(instrSet, offset, length, theBank, theInstrNum) {}

//==============================================================
//		Destructor
//--------------------------------------------------------------
WdsInstr::~WdsInstr() {}

//==============================================================
//		Make the Object "WdsRgn" (Attribute table)
//--------------------------------------------------------------
bool WdsInstr::loadInstr() {
  WdsInstrSet *parInstrSet = static_cast<WdsInstrSet*>(this->parInstrSet);

  readBytes(offset(), sizeof(WdsRgnData), &rgndata);
  VGMRgn *rgn = new VGMRgn(this, offset(), length());
  rgn->sampOffset = rgndata.ptBody;
  if (parInstrSet->version == WdsInstrSet::VERSION_WDS) {
    rgn->sampOffset *= 8;
  }
  //rgn->loop.loopStart =	rgndata.ptLoop;
  rgn->unityKey = 0x3C - rgndata.iSemiToneTune;
  // an iFineTune value of 256 should equal 100 cents, and linear scaling seems to do the trick.
  // see the declaration of iFineTune for info on where to find the actual code and table for this in FFT
  rgn->fineTune = static_cast<s16>(rgndata.iFineTune * (100.0 / 256.0));

  rgn->addGeneralItem(offset() + 0x00, sizeof(u32), "Sample Offset");
  rgn->addGeneralItem(offset() + 0x04, sizeof(u16), "Loop Offset");
  rgn->addGeneralItem(offset() + 0x06, sizeof(u16), "Pitch Fine Tune");

  if (parInstrSet->version == WdsInstrSet::VERSION_WDS) {
    u32 adsr_rate = getWord(offset() + 0x08);
    u16 adsr_mode = readShort(offset() + 0x0c);

    // Xenogears: function 0x8003e5bc
    // These values will be set to SPU by function 0x8003e900
    u8 Ar = adsr_rate & 0x7f;
    u8 Dr = (adsr_rate >> 8) & 0x0f;
    u8 Sl = (adsr_rate >> 12) & 0x0f;
    u8 Sr = (adsr_rate >> 16) & 0x7f;
    u8 Rr = (adsr_rate >> 24) & 0x1f;
    u8 Am = adsr_mode & 0x07;
    u8 Sm = (adsr_mode >> 4) & 0x07;
    u8 Rm = (adsr_mode >> 8) & 0x07;

    rgn->addADSRValue(offset() + 0x08, sizeof(u8), "ADSR Attack Rate");
    rgn->addADSRValue(offset() + 0x09, sizeof(u8), "ADSR Decay Rate & Sustain Level");
    rgn->addADSRValue(offset() + 0x0a, sizeof(u8), "ADSR Sustain Rate");
    rgn->addADSRValue(offset() + 0x0b, sizeof(u8), "ADSR Release Rate");
    rgn->addADSRValue(offset() + 0x0c, sizeof(u8), "ADSR Attack Mode & Sustain Mode / Direction");
    rgn->addADSRValue(offset() + 0x0d, sizeof(u8), "ADSR Release Mode");
    rgn->addUnknown(offset() + 0x0e, sizeof(u8));
    rgn->addUnknown(offset() + 0x0f, sizeof(u8));

    psxConvADSR(rgn, Am >> 2, Ar, Dr, Sl, Sm >> 2, (Sm >> 1) & 1, Sr, Rm >> 2, Rr, false);
    addRgn(rgn);
  }
  else if (parInstrSet->version == WdsInstrSet::VERSION_DWDS) {
    psxConvADSR(rgn, rgndata.Am > 1, rgndata.Ar, rgndata.Dr, rgndata.Sl, 1, 1, rgndata.Sr, 1, rgndata.Rr, false);
    addRgn(rgn);

    rgn->addADSRValue(offset() + 0x08, sizeof(u8), "Attack Rate");
    rgn->addADSRValue(offset() + 0x09, sizeof(u8), "Decay Rate");
    rgn->addADSRValue(offset() + 0x0A, sizeof(u8), "Sustain Rate");
    rgn->addADSRValue(offset() + 0x0B, sizeof(u8), "Release Rate");
    rgn->addADSRValue(offset() + 0x0C, sizeof(u8), "Sustain Level");
    rgn->addADSRValue(offset() + 0x0D, sizeof(u8), "Attack Rate Mode?");
    rgn->addUnknown(offset() + 0x0E, sizeof(u8));
    rgn->addUnknown(offset() + 0x0F, sizeof(u8));
  }

  return true;
}
