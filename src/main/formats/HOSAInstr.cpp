/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include "HOSAInstr.h"
#include "VGMRgn.h"
#include "PSXSPU.h"

/****************************************************************/
/*																*/
/*			Instrument Set		(Bank全体)
 */
/*																*/
/****************************************************************/
//==============================================================
//		Constructor
//--------------------------------------------------------------
HOSAInstrSet::HOSAInstrSet(RawFile *file, uint32_t offset)
    : VGMInstrSet(HOSAFormat::name, file, offset, 0, "HOSAWAH ") {}

//==============================================================
//		Destructor
//--------------------------------------------------------------
HOSAInstrSet::~HOSAInstrSet(void) {}

//==============================================================
//		ヘッダー情報の取得
//--------------------------------------------------------------
//	Memo:
//		VGMInstrSet::Load()関数から呼ばれる
//==============================================================
bool HOSAInstrSet::GetHeaderInfo() {
    //"hdr"構造体へそのまま転送
    GetBytes(dwOffset, sizeof(InstrHeader), &instrheader);
    id = 0;  // Bank number.

    //バイナリエディタ表示用
    name = "HOSAWAH";

    //ヘッダーobjectの生成
    VGMHeader *wdsHeader = AddHeader(dwOffset, sizeof(InstrHeader));
    wdsHeader->AddSig(dwOffset, 8);
    wdsHeader->AddSimpleItem(dwOffset + 8, sizeof(uint32_t), "Number of Instruments");

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
bool HOSAInstrSet::GetInstrPointers() {
    uint32_t iOffset = dwOffset + sizeof(InstrHeader);  // pointer of attribute table

    //音色数だけ繰り返す。
    for (unsigned int i = 0; i < instrheader.numInstr; i++) {
        HOSAInstr *newInstr =
            new HOSAInstr(this, dwOffset + GetWord(iOffset), 0, i / 0x80, i % 0x80);
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
HOSAInstr::HOSAInstr(VGMInstrSet *instrSet, uint32_t offset, uint32_t length, uint32_t theBank,
                     uint32_t theInstrNum)
    : VGMInstr(instrSet, offset, length, theBank, theInstrNum), rgns(NULL) {}

//==============================================================
//		Make the Object "WdsRgn" (Attribute table)
//--------------------------------------------------------------
bool HOSAInstr::LoadInstr() {
    if (dwOffset + sizeof(InstrInfo) > vgmfile->GetEndOffset()) {
        return false;
    }

    // Get the instr data
    GetBytes(dwOffset, sizeof(InstrInfo), &instrinfo);
    unLength = sizeof(InstrInfo) + sizeof(RgnInfo) * instrinfo.numRgns;
    AddSimpleItem(dwOffset, sizeof(uint32_t), "Number of Rgns");

    // Get the rgn data
    rgns = new RgnInfo[instrinfo.numRgns];
    GetBytes((dwOffset + sizeof(InstrInfo)), (sizeof(RgnInfo) * instrinfo.numRgns), rgns);

    // ATLTRACE("LOADED INSTR   ProgNum: %X    BankNum: %X\n", instrinfo.progNum,
    // instrinfo.bankNum);

    uint8_t cKeyLow = 0x00;
    for (unsigned int i = 0; i < instrinfo.numRgns; i++) {
        RgnInfo *rgninfo = &rgns[i];
        VGMRgn *rgn =
            new VGMRgn(this, dwOffset + sizeof(InstrInfo) + sizeof(RgnInfo) * i, sizeof(RgnInfo));

        rgn->AddSimpleItem(rgn->dwOffset, 4, "Sample Offset");
        rgn->sampOffset =
            rgninfo->sampOffset;  //+ ((VGMInstrSet*)this->vgmfile)->sampColl->dwOffset;

        rgn->velLow = 0x00;
        rgn->velHigh = 0x7F;
        rgn->keyLow = cKeyLow;
        rgn->AddKeyHigh(rgninfo->note_range_high, rgn->dwOffset + 0x05);
        cKeyLow = (rgninfo->note_range_high) + 1;

        rgn->AddUnityKey((int8_t)0x3C + 0x3C - rgninfo->iSemiToneTune, rgn->dwOffset + 0x06);
        rgn->AddSimpleItem(rgn->dwOffset + 0x07, 1, "Semi Tone Tune");
        rgn->fineTune = (short)((double)rgninfo->iFineTune * (100.0 / 256.0));

        // Might want to simplify the code below.  I'm being nitpicky.
        if (rgninfo->iPan == 0x80)
            rgn->pan = 0;
        else if (rgninfo->iPan == 0xFF)
            rgn->pan = 1.0;
        else if ((rgninfo->iPan == 0xC0) || (rgninfo->iPan < 0x80))
            rgn->pan = 0.5;
        else
            rgn->pan = (double)(rgninfo->iPan - 0x80) / (double)0x7F;

        // The ADSR value ordering is all messed up for the hell of it.  This was a bitch to
        // reverse-engineer.
        rgn->AddSimpleItem(rgn->dwOffset + 0x0C, 4, "ADSR Values (non-standard ordering)");
        uint8_t Ar = (rgninfo->ADSR_vals >> 20) & 0x7F;
        uint8_t Dr = (rgninfo->ADSR_vals >> 16) & 0xF;
        uint8_t Sr = ((rgninfo->ADSR_vals >> 8) & 0xFF) >> 1;
        uint8_t Rr = (rgninfo->ADSR_vals >> 4) & 0x1F;
        uint8_t Sl = rgninfo->ADSR_vals & 0xF;
        uint8_t Am = (((rgninfo->ADSR_Am & 0xF) ^ 5) < 1)
                         ? 1
                         : 0;  // Not sure what other role this nibble plays, if any.
        PSXConvADSR(rgn, Am, Ar, Dr, Sl, 1, 1, Sr, 1, Rr, false);

        // Unsure if volume is using a linear scale, but it sounds like it.
        double vol = rgninfo->volume / (double)255;
        rgn->SetVolume(vol);
        aRgns.push_back(rgn);
    }
    return true;
}
