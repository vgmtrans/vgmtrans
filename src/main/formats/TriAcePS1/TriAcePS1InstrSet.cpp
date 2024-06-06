#include "TriAcePS1InstrSet.h"
#include "VGMRgn.h"
#include "PSXSPU.h"

// *****************
// TriAcePS1InstrSet
// *****************

TriAcePS1InstrSet::TriAcePS1InstrSet(RawFile *file, uint32_t offset)
    : VGMInstrSet(TriAcePS1Format::name, file, offset, 0, "TriAce InstrSet") {
}

TriAcePS1InstrSet::~TriAcePS1InstrSet(void) {
}


//==============================================================
//		ヘッダー情報の取得
//--------------------------------------------------------------
//	Memo:
//		VGMInstrSet::Load()関数から呼ばれる
//==============================================================
bool TriAcePS1InstrSet::GetHeaderInfo() {
  VGMHeader *header = addHeader(dwOffset, sizeof(TriAcePS1InstrSet::_InstrHeader));    //1,Sep.2009 revise
  header->addChild(dwOffset, 4, "InstrSet Size");
  header->addChild(dwOffset + 4, 2, "Instr Section Size");
  //header->addSimpleChild(dwOffset+6, 1, "Number of Instruments");

  //-----------------------
  //1,Sep.2009 revise		to do こうしたい。
  //-----------------------
  //	GetBytes(dwOffset, sizeof(TriAcePS1InstrSet::_InstrHeader), &InstrHeader);
  //		↑　↑　↑
  unLength = GetWord(dwOffset);                     // to do delete
  instrSectionSize = GetShort(dwOffset + 4);        // to do delete
  //-----------------------


  //sampColl = new TriAcePS1SampColl(this, dwOffset+instrSectionSize, unLength-instrSectionSize);
  sampColl = new PSXSampColl(TriAcePS1Format::name, this, dwOffset + instrSectionSize, unLength - instrSectionSize);


  return true;
}

//==============================================================
//		各音色の情報取得
//--------------------------------------------------------------
//	Memo:
//		VGMInstrSet::Load()関数から呼ばれる
//==============================================================
bool TriAcePS1InstrSet::GetInstrPointers() {

  uint32_t firstWord = GetWord(dwOffset + sizeof(TriAcePS1InstrSet::_InstrHeader));        //1,Sep.2009 revise

  //0xFFFFFFFFになるまで繰り返す。
  for (uint32_t i = dwOffset + sizeof(TriAcePS1InstrSet::_InstrHeader);                    //1,Sep.2009 revise
       ((firstWord != 0xFFFFFFFF) && (i < dwOffset + unLength));
       i += sizeof(TriAcePS1Instr::InstrInfo), firstWord = GetWord(i)) {
    TriAcePS1Instr *newInstr = new TriAcePS1Instr(this, i, 0, 0, 0);
    aInstrs.push_back(newInstr);
    GetBytes(i, sizeof(TriAcePS1Instr::InstrInfo), &newInstr->instrinfo);
    newInstr->addChild(i + 0, sizeof(short), "Instrument Number");            //1,Sep.2009 revise
    newInstr->addChild(i + 2, sizeof(short), "ADSR1");                        //1,Sep.2009 revise
    newInstr->addChild(i + 4, sizeof(short), "ADSR2");                        //1,Sep.2009 revise
    newInstr->addUnknownChild(i + 6, sizeof(char));                                  //1,Sep.2009 revise
    newInstr->addChild(i + 7, sizeof(char), "Number of Rgns");                //1,Sep.2009 revise
    i += sizeof(TriAcePS1Instr::RgnInfo) * (newInstr->instrinfo.numRgns);
  }
  return true;
}


// **************
// TriAcePS1Instr
// **************

TriAcePS1Instr::TriAcePS1Instr(VGMInstrSet *instrSet,
                               uint32_t offset,
                               uint32_t length,
                               uint32_t theBank,
                               uint32_t theInstrNum)
    : VGMInstr(instrSet, offset, length, theBank, theInstrNum),
      rgns(NULL) {
}

//==============================================================
//		Make the Object "WdsRgn" (Attribute table)
//--------------------------------------------------------------
bool TriAcePS1Instr::LoadInstr() {
  unLength = sizeof(InstrInfo) + sizeof(RgnInfo) * instrinfo.numRgns;

//	TriAcePS1InstrSet*	_parInstrSet	=	(TriAcePS1InstrSet*)parInstrSet;

  // Get the rgn data
  rgns = new RgnInfo[instrinfo.numRgns];
  GetBytes((dwOffset + sizeof(InstrInfo)), (sizeof(RgnInfo) * instrinfo.numRgns), rgns);

  // Set the program num and bank num, adjusting for values > 0x7F
  this->instrNum =  instrinfo.progNum & 0x7F;                                        //1,Sep.2009 revise
  this->bank =    ((instrinfo.progNum + (instrinfo.bankNum << 8)) & 0xFF80) >> 7;    //1,Sep.2009 revise
//	this->instrNum = instrinfo.progNum;
//	if (this->instrNum > 0x7F)
//		instrNum -= 0x80;
//	this->bank = (instrinfo.bankNum*2);
//	if (instrinfo.progNum > 0x7F)
//		this->bank++;




  //ATLTRACE("LOADED INSTR   ProgNum: %X    BankNum: %X\n", instrinfo.progNum, instrinfo.bankNum);

  for (int i = 0; i < instrinfo.numRgns; i++) {
    RgnInfo *rgninfo = &rgns[i];
    VGMRgn *rgn = new VGMRgn(this,
                             dwOffset + sizeof(InstrInfo) + sizeof(RgnInfo) * i,
                             sizeof(RgnInfo));        //1,Sep.2009 revise
    rgn->AddKeyLow(rgninfo->note_range_low, rgn->dwOffset);
    rgn->AddKeyHigh(rgninfo->note_range_high, rgn->dwOffset + 1);
    rgn->AddVelLow(rgninfo->vel_range_low, rgn->dwOffset + 2);
    rgn->AddVelHigh(rgninfo->vel_range_high, rgn->dwOffset + 3);
    rgn->addChild(rgn->dwOffset + 4, 4, "Sample Offset");
    rgn->sampOffset = rgninfo->sampOffset; //+ ((VGMInstrSet*)this->vgmfile)->sampColl->dwOffset;
    rgn->addChild(rgn->dwOffset + 8, 4, "Sample Loop Point");
    //rgn->loop.loopStatus = (rgninfo->loopOffset != rgninfo->sampOffset) && (rgninfo->loopOffset != 0);
    rgn->loop.loopStart = rgninfo->loopOffset;
    rgn->addChild(rgn->dwOffset + 12, 1, "Attenuation");
    rgn->AddUnityKey((int8_t) 0x3B - rgninfo->pitchTuneSemitones,
                     rgn->dwOffset + 13);  //You would think it would be 0x3C (middle c)
    rgn->addChild(rgn->dwOffset + 14, 1, "Pitch Fine Tune");
    const int kTuningOffset = 22; // approx. 21.500638 from pitch table (0x10be vs 0x1000)
    rgn->fineTune = (short)((double)rgninfo->pitchTuneFine / 64.0 * 100) - kTuningOffset;
    rgn->sampCollPtr = ((VGMInstrSet *) this->vgmFile())->sampColl;
    rgn->addChild(rgn->dwOffset + 15, 5, "Unknown values");


    PSXConvADSR(rgn, instrinfo.ADSR1, instrinfo.ADSR2, false);
    // Attenuation is not using a linear scale. Will need to look at the original code.  For now, it's ignored.
    //  also, be aware the same scale may be employed for vol and expression events (haven't investigated).
    //long dlsAtten = -(ConvertPercentVolToAttenDB(rgninfo->attenuation/((double)255)) * DLS_DECIBEL_UNIT);
    //rgn->SetAttenuation(dlsAtten);
    AddRgn(rgn);
  }
  return true;
}
