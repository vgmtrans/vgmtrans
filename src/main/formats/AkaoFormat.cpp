#include "pch.h"
#include "AkaoFormat.h"
#include "AkaoSeq.h"
#include "AkaoInstr.h"
#include "PSXSPU.h"

using namespace std;

/*
int AkaoFormat::OnMatch(vector<VGMFile*>& files)
{
	uint32_t i;
	for (i=0; files[i]->GetFileType() != FILETYPE_SEQ  &&  i<2; i++)
		;

	AkaoSeq* seq = (AkaoSeq*)files[i];
	AkaoInstrSet* instrset = seq->instrset;
	AkaoSampColl* sampcoll = (AkaoSampColl*)files[!i];

	AkaoColl* coll = new AkaoColl(*seq->GetName());
	coll->UseSeq(seq);
	coll->AddInstrSet(instrset);
	coll->AddSampColl(sampcoll);
	if (!coll->Load())
	{
		delete coll;
		return false;
	}
	return true;
}*/


bool AkaoColl::LoadMain() {
  AkaoInstrSet *instrset = (AkaoInstrSet *) instrsets[0];
  AkaoSampColl *sampcoll = (AkaoSampColl *) sampcolls[0];

  //Set the sample numbers of each region using the articulation data references of each region
  for (uint32_t i = 0; i < instrset->aInstrs.size(); i++) {
    AkaoInstr *instr = (AkaoInstr *) instrset->aInstrs[i];
    vector<VGMRgn *> *rgns = &instr->aRgns;
    for (uint32_t j = 0; j < rgns->size(); j++) {
      AkaoRgn *rgn = (AkaoRgn *) (*rgns)[j];


      AkaoArt *art;

      if (!((rgn->artNum - sampcoll->starting_art_id) >= 0 &&
          rgn->artNum - sampcoll->starting_art_id < 200)) {
        pRoot->AddLogItem(new LogItem(std::wstring(L"Articualation reference does not exist in the samp collection"),
                                      LOG_LEVEL_ERR,
                                      L"AkaoColl"));
        art = &sampcoll->akArts.front();
      }

      if (rgn->artNum - sampcoll->starting_art_id >= sampcoll->akArts.size()) {
        pRoot->AddLogItem(new LogItem(std::wstring(L"referencing an articulation that was not loaded"),
                                      LOG_LEVEL_ERR,
                                      L"AkaoColl"));
        art = &sampcoll->akArts.back();
      }
      else
        art = &sampcoll->akArts[rgn->artNum - sampcoll->starting_art_id];
      rgn->SetSampNum(art->sample_num);
      if (art->loop_point != 0)
        rgn->SetLoopInfo(1, art->loop_point, sampcoll->samples[rgn->sampNum]->dataLength - art->loop_point);

      PSXConvADSR<AkaoRgn>(rgn, art->ADSR1, art->ADSR2, false);
      if (instr->bDrumKit)
        rgn->unityKey = art->unityKey + rgn->keyLow - rgn->drumRelUnityKey;
      else
        rgn->unityKey = art->unityKey;

      short ft = art->fineTune;
      if (ft < 0)
        ft += 0x8000;
      //this gives us the pitch multiplier value ex. 1.05946
      double freq_multiplier = (double) (((ft * 32) + 0x100000) / (double) 0x100000);
      double cents = log(freq_multiplier) / log((double) 2) * 1200;
      if (art->fineTune < 0)
        cents -= 1200;
      rgn->fineTune = (short) cents;
    }
  }


  return true;
}

void AkaoColl::PreSynthFileCreation() {
  //Before DLS Conversion, we want to add instruments for every single articulation definition
  //in the Akao Sample Collection, so that they can be used with the 0xA1 program change sequence event
  //to do this, we create a copy of the AkaoInstrSet before conversion and add the instruments, then
  //when we finish the dls conversion, we put back the original unchanged AkaoInstrSet.  We don't want to make
  //any permanent changes to the InstrSet, in case the collection gets recreated or something.
  /*origInstrSet = (AkaoInstrSet*)instrsets[0];
  AkaoInstrSet* newInstrSet = new AkaoInstrSet(NULL, 0, 0, 0, L"");
  *newInstrSet = *origInstrSet;
  //copy all of the instruments so that we have copied instruments deleted on the destructor of the Instrument Set
  for (uint32_t i=0; i<origInstrSet->aInstrs.size(); i++)
  {
      AkaoInstr* cpyInstr = new AkaoInstr(newInstrSet, 0, 0, 0, 0);
      *cpyInstr = *((AkaoInstr*)origInstrSet->aInstrs[i]);
      newInstrSet->aInstrs[i] =  cpyInstr;
      //ditto for all regions in the instruments
      for (uint32_t j=0; j<cpyInstr->aRgns.size(); j++)
      {
          AkaoRgn* cpyRgn = new AkaoRgn(cpyInstr, 0, 0, L"");
          *cpyRgn = *((AkaoRgn*)cpyInstr->aRgns[j]);
          cpyInstr->aRgns[j] =  cpyRgn;
          cpyRgn->items.clear();
      }
  }
  instrsets[0] = newInstrSet;*/

  if (!((AkaoSeq *) seq)->bUsesIndividualArts)    //only do this if the 0xA1 event is actually used
    return;

  AkaoInstrSet *instrSet = (AkaoInstrSet *) instrsets[0];

  AkaoSampColl *sampcoll = (AkaoSampColl *) sampcolls[0];
  const uint32_t numArts = (const uint32_t) sampcoll->akArts.size();
  const uint32_t origNumInstrs = (const uint32_t) instrSet->aInstrs.size();
  if (origNumInstrs + numArts > 0x7F)
    numAddedInstrs = 0x7F - origNumInstrs;
  else
    numAddedInstrs = numArts;

  for (uint32_t i = 0; i < numAddedInstrs; i++) {
    AkaoArt *art = &sampcoll->akArts[i];
    AkaoInstr *newInstr = new AkaoInstr(instrSet, 0, 0, 0, origNumInstrs + sampcoll->starting_art_id + i);

    AkaoRgn *rgn = new AkaoRgn(newInstr, 0, 0);

    rgn->SetSampNum(art->sample_num);
    if (art->loop_point != 0)
      rgn->SetLoopInfo(1, art->loop_point, sampcoll->samples[rgn->sampNum]->dataLength - art->loop_point);

    PSXConvADSR<AkaoRgn>(rgn, art->ADSR1, art->ADSR2, false);
    rgn->unityKey = art->unityKey;

    short ft = art->fineTune;
    if (ft < 0)
      ft += 0x8000;
    double freq_multiplier =
        (double) (((ft * 32) + 0x100000) / (double) 0x100000);  //this gives us the pitch multiplier value ex. 1.05946
    double cents = log(freq_multiplier) / log((double) 2) * 1200;
    if (art->fineTune < 0)
      cents -= 1200;
    rgn->fineTune = (short) cents;
    newInstr->aRgns.push_back(rgn);

    instrSet->aInstrs.push_back(newInstr);
  }
}

void AkaoColl::PostSynthFileCreation() {

  //if the 0xA1 event isn't used in the sequence, then we didn't modify the instrset
  //so skip this
  if (!((AkaoSeq *) seq)->bUsesIndividualArts)
    return;

  AkaoInstrSet *instrSet = (AkaoInstrSet *) instrsets[0];
  AkaoSampColl *sampcoll = (AkaoSampColl *) sampcolls[0];
  const size_t numArts = sampcoll->akArts.size();
  size_t beginOffset = instrSet->aInstrs.size() - numArts;
  for (size_t i = 0; i < numAddedInstrs; i++) {
    delete instrSet->aInstrs.back();
    instrSet->aInstrs.pop_back();
  }
}
