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
bool TriAcePS1InstrSet::parseHeader() {
  VGMHeader *header = addHeader(offset(), sizeof(TriAcePS1InstrSet::_InstrHeader));    //1,Sep.2009 revise
  header->addChild(offset(), 4, "InstrSet Size");
  header->addChild(offset() + 4, 2, "Instr Section Size");
  //header->addSimpleChild(offset()+6, 1, "Number of Instruments");

  //-----------------------
  //1,Sep.2009 revise		to do こうしたい。
  //-----------------------
  //	GetBytes(offset(), sizeof(TriAcePS1InstrSet::_InstrHeader), &InstrHeader);
  //		↑　↑　↑
  setLength(readWord(offset()));                     // to do delete
  instrSectionSize = readShort(offset() + 4);        // to do delete
  //-----------------------


  //sampColl = new TriAcePS1SampColl(this, offset()+instrSectionSize, length()-instrSectionSize);
  sampColl = new PSXSampColl(TriAcePS1Format::name, this, offset() + instrSectionSize, length() - instrSectionSize);


  return true;
}

//==============================================================
//		各音色の情報取得
//--------------------------------------------------------------
//	Memo:
//		VGMInstrSet::Load()関数から呼ばれる
//==============================================================
bool TriAcePS1InstrSet::parseInstrPointers() {

  uint32_t firstWord = readWord(offset() + sizeof(TriAcePS1InstrSet::_InstrHeader));        //1,Sep.2009 revise

  //0xFFFFFFFFになるまで繰り返す。
  for (uint32_t i = offset() + sizeof(TriAcePS1InstrSet::_InstrHeader);                    //1,Sep.2009 revise
       ((firstWord != 0xFFFFFFFF) && (i < offset() + length()));
       i += sizeof(TriAcePS1Instr::InstrInfo), firstWord = readWord(i)) {
    TriAcePS1Instr *newInstr = new TriAcePS1Instr(this, i, 0, 0, 0);
    aInstrs.push_back(newInstr);
    readBytes(i, sizeof(TriAcePS1Instr::InstrInfo), &newInstr->instrinfo);
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
bool TriAcePS1Instr::loadInstr() {
  setLength(sizeof(InstrInfo) + sizeof(RgnInfo) * instrinfo.numRgns);

//	TriAcePS1InstrSet*	_parInstrSet	=	(TriAcePS1InstrSet*)parInstrSet;

  // Get the rgn data
  rgns = new RgnInfo[instrinfo.numRgns];
  readBytes((offset() + sizeof(InstrInfo)), (sizeof(RgnInfo) * instrinfo.numRgns), rgns);

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
                             offset() + sizeof(InstrInfo) + sizeof(RgnInfo) * i,
                             sizeof(RgnInfo));        //1,Sep.2009 revise
    rgn->addKeyLow(rgninfo->note_range_low, rgn->offset());
    rgn->addKeyHigh(rgninfo->note_range_high, rgn->offset() + 1);
    rgn->addVelLow(rgninfo->vel_range_high == 0 ? 0 : rgninfo->vel_range_low, rgn->offset() + 2);
    rgn->addVelHigh(rgninfo->vel_range_high == 0 ? 0x7F : rgninfo->vel_range_high, rgn->offset() + 3);
    rgn->addChild(rgn->offset() + 4, 4, "Sample Offset");
    rgn->sampOffset = rgninfo->sampOffset; //+ ((VGMInstrSet*)this->vgmfile)->sampColl->offset();
    rgn->addChild(rgn->offset() + 8, 4, "Sample Loop Point");
    //rgn->loop.loopStatus = (rgninfo->loopOffset != rgninfo->sampOffset) && (rgninfo->loopOffset != 0);
    rgn->loop.loopStart = rgninfo->loopOffset;
    rgn->addChild(rgn->offset() + 12, 1, "Attenuation");
    rgn->addUnityKey((int8_t) 0x3B - rgninfo->pitchTuneSemitones,
                     rgn->offset() + 13);  //You would think it would be 0x3C (middle c)
    rgn->addChild(rgn->offset() + 14, 1, "Pitch Fine Tune");
    const int kTuningOffset = 22; // approx. 21.500638 from pitch table (0x10be vs 0x1000)
    rgn->fineTune = (short)((double)rgninfo->pitchTuneFine / 64.0 * 100) - kTuningOffset;
    rgn->sampCollPtr = ((VGMInstrSet *) this->vgmFile())->sampColl;
    rgn->addChild(rgn->offset() + 15, 5, "Unknown values");


    psxConvADSR(rgn, instrinfo.ADSR1, instrinfo.ADSR2, false);
    // Attenuation is not using a linear scale. Will need to look at the original code.  For now, it's ignored.
    //  also, be aware the same scale may be employed for vol and expression events (haven't investigated).
    //long dlsAtten = -(ConvertPercentVolToAttenDB(rgninfo->attenuation/((double)255)) * DLS_DECIBEL_UNIT);
    //rgn->SetAttenuation(dlsAtten);
    addRgn(rgn);
  }
  return true;
}
