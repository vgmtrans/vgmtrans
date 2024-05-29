/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include <vector>

#include "AkaoSeq.h"
#include "AkaoInstr.h"
#include "PSXSPU.h"

bool AkaoColl::loadMain() {
  AkaoInstrSet *instrset = reinterpret_cast<AkaoInstrSet *>(instrSets()[0]);
  AkaoSampColl *sampcoll = reinterpret_cast<AkaoSampColl *>(sampColls()[0]);

  //Set the sample numbers of each region using the articulation data references of each region
  for (uint32_t i = 0; i < instrset->aInstrs.size(); i++) {
    AkaoInstr *instr = reinterpret_cast<AkaoInstr *>(instrset->aInstrs[i]);
    std::vector<VGMRgn *> *rgns = &instr->aRgns;
    for (uint32_t j = 0; j < rgns->size(); j++) {
      AkaoRgn *rgn = reinterpret_cast<AkaoRgn *>((*rgns)[j]);


      AkaoArt *art;

      if (!(static_cast<int32_t>(rgn->artNum) - static_cast<int32_t>(sampcoll->starting_art_id) >= 0 &&
          rgn->artNum - sampcoll->starting_art_id < 200)) {

        L_ERROR("Articulation #{:d} does not exist in the samp collection", rgn->artNum);
        art = &sampcoll->akArts.front();
      } else {
        if (rgn->artNum - sampcoll->starting_art_id >= sampcoll->akArts.size()) {
          L_ERROR("Articulation #{:d} referenced but not loaded", rgn->artNum);
          art = &sampcoll->akArts.back();
        }
        else
          art = &sampcoll->akArts[rgn->artNum - sampcoll->starting_art_id];
      }
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

void AkaoColl::preSynthFileCreation() {
  if (!static_cast<AkaoSeq*>(seq())->bUsesIndividualArts)    //only do this if the 0xA1 event is actually used
    return;

  AkaoInstrSet *instrSet = reinterpret_cast<AkaoInstrSet *>(instrSets()[0]);

  AkaoSampColl *sampcoll = reinterpret_cast<AkaoSampColl *>(sampColls()[0]);
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

void AkaoColl::postSynthFileCreation() {
  //if the 0xA1 event isn't used in the sequence, then we didn't modify the instrset
  //so skip this
  if (!static_cast<AkaoSeq*>(seq())->bUsesIndividualArts)
    return;

  AkaoInstrSet *instrSet = reinterpret_cast<AkaoInstrSet *>(instrSets()[0]);
  for (size_t i = 0; i < numAddedInstrs; i++) {
    delete instrSet->aInstrs.back();
    instrSet->aInstrs.pop_back();
  }
}
