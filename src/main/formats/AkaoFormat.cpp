#include "pch.h"
#include "AkaoFormat.h"
#include "AkaoSeq.h"
#include "AkaoInstr.h"
#include "PSXSPU.h"

using namespace std;

bool AkaoColl::LoadMain() {
  AkaoInstrSet *instrset = reinterpret_cast<AkaoInstrSet *>(instrsets[0]);
  AkaoSampColl *sampcoll = reinterpret_cast<AkaoSampColl *>(sampcolls[0]);

  //Set the sample numbers of each region using the articulation data references of each region
  for (uint32_t i = 0; i < instrset->aInstrs.size(); i++) {
    AkaoInstr *instr = reinterpret_cast<AkaoInstr *>(instrset->aInstrs[i]);
    std::vector<VGMRgn *> *rgns = &instr->aRgns;
    for (uint32_t j = 0; j < rgns->size(); j++) {
      AkaoRgn *rgn = reinterpret_cast<AkaoRgn *>((*rgns)[j]);


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

      rgn->fineTune = art->fineTune;
    }
  }


  return true;
}

void AkaoColl::PreSynthFileCreation() {
  if (!static_cast<AkaoSeq*>(seq)->bUsesIndividualArts)    //only do this if the 0xA1 event is actually used
    return;

  AkaoInstrSet *instrSet = reinterpret_cast<AkaoInstrSet *>(instrsets[0]);

  AkaoSampColl *sampcoll = reinterpret_cast<AkaoSampColl *>(sampcolls[0]);
  const uint32_t numArts = static_cast<uint32_t>(sampcoll->akArts.size());
  numAddedInstrs = numArts;

  for (uint32_t i = 0; i < numAddedInstrs; i++) {
    AkaoArt *art = &sampcoll->akArts[i];
    AkaoInstr *newInstr = new AkaoInstr(instrSet, 0, 0, 0, sampcoll->starting_art_id + i);

    AkaoRgn *rgn = new AkaoRgn(newInstr, 0, 0);

    rgn->SetSampNum(art->sample_num);
    if (art->loop_point != 0)
      rgn->SetLoopInfo(1, art->loop_point, sampcoll->samples[rgn->sampNum]->dataLength - art->loop_point);

    PSXConvADSR<AkaoRgn>(rgn, art->ADSR1, art->ADSR2, false);

    rgn->unityKey = art->unityKey;
    rgn->fineTune = art->fineTune;

    newInstr->aRgns.push_back(rgn);

    instrSet->aInstrs.push_back(newInstr);
  }
}

void AkaoColl::PostSynthFileCreation() {
  //if the 0xA1 event isn't used in the sequence, then we didn't modify the instrset
  //so skip this
  if (!static_cast<AkaoSeq*>(seq)->bUsesIndividualArts)
    return;

  AkaoInstrSet *instrSet = reinterpret_cast<AkaoInstrSet *>(instrsets[0]);
  for (size_t i = 0; i < numAddedInstrs; i++) {
    delete instrSet->aInstrs.back();
    instrSet->aInstrs.pop_back();
  }
}
