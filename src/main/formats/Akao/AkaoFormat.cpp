/*
 * VGMTrans (c) 2002-2024
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#include <vector>
#include "AkaoSeq.h"
#include "AkaoInstr.h"
#include "PSXSPU.h"

// Generate maps to help route the relationships to and from articulations, especially for games
// that load more than one sample collection at a time.
std::tuple<std::map<int, AkaoArt*>, std::map<int, int>, std::map<int, AkaoSampColl*>> AkaoColl::mapSampleCollections() {
  std::vector<AkaoSampColl*> orderedSampColls;
  orderedSampColls.reserve(sampColls().size());

  std::map<int, AkaoArt*> artIdToArtMap;
  std::map<int, int> artIdToSampleNumMap;
  std::map<int, AkaoSampColl*> artIdToSampCollMap;

  // First order the sa
  std::transform(sampColls().begin(), sampColls().end(), std::back_inserter(orderedSampColls),
                 [](VGMSampColl* vgm) {
                     return static_cast<AkaoSampColl*>(vgm);
                 });

  std::sort(orderedSampColls.begin(), orderedSampColls.end(), [](AkaoSampColl *a, AkaoSampColl *b) {
    return a->starting_art_id < b->starting_art_id;
  });

  int cumulativeSamples = 0;
  for (auto *sampcoll : orderedSampColls) {
    for (int i = 0; i < sampcoll->nNumArts; ++i) {
      int artId = sampcoll->starting_art_id + i;
      artIdToArtMap[artId] = &sampcoll->akArts[i];
      artIdToSampleNumMap[artId] = sampcoll->akArts[i].sample_num + cumulativeSamples;
      artIdToSampCollMap[artId] = sampcoll;
    }
    cumulativeSamples += sampcoll->samples.size();
  }

  return std::make_tuple(artIdToArtMap, artIdToSampleNumMap, artIdToSampCollMap);
}

bool AkaoColl::loadMain() {
  AkaoInstrSet *instrset = reinterpret_cast<AkaoInstrSet *>(instrSets()[0]);

  auto [artIdToArtMap, artIdToSampleNumMap, artIdToSampCollMap] = mapSampleCollections();

  for (auto *vgminstr : instrset->aInstrs) {
    auto* instr = dynamic_cast<AkaoInstr*>(vgminstr);
    auto regions = instr->regions();
    for (auto *vgmregion : regions) {
      auto *rgn = dynamic_cast<AkaoRgn*>(vgmregion);
      auto itArt = artIdToArtMap.find(rgn->artNum);

      if (itArt == artIdToArtMap.end()) {
        L_ERROR("Articulation #{:d} does not exist in the sample collection", rgn->artNum);
        rgn->setSampNum(0);  // Default sample number if not found
        continue;
      }

      AkaoArt *art = itArt->second;

      if (art->loop_point != 0) {
        AkaoSampColl *sampColl = artIdToSampCollMap[rgn->artNum];
        if (rgn->sampNum < sampColl->samples.size()) {
          rgn->setLoopInfo(1, art->loop_point, sampColl->samples[art->sample_num]->dataLength - art->loop_point);
        }
        else
          L_ERROR("Akao region points to out-of-range sample (#{:d}) in {}.", rgn->sampNum, sampColl->name());
      }

      auto itSampleNum = artIdToSampleNumMap.find(rgn->artNum);
      if (itSampleNum != artIdToSampleNumMap.end()) {
        rgn->setSampNum(itSampleNum->second);
      }

      // TODO: confirm the actual logic. This is simply a guess.
      u16 adsr1 = art->ADSR1;
      u16 adsr2 = art->ADSR2;
      adsr1 &= ~0x7F00;
      adsr1 |= (rgn->attackRate & 0x7F) << 8;
      adsr2 &= ~0xFFDF;
      adsr2 |= (rgn->sustainRate & 0x7F) << 6;
      adsr2 |= (rgn->sustainMode & 0x07) << 13;
      adsr2 |= (rgn->releaseRate & 0x1F);

      psxConvADSR<AkaoRgn>(rgn, adsr1, adsr2, false);
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
  auto [artIdToArtMap, artIdToSampleNumMap, artIdToSampCollMap] = mapSampleCollections();

  for(auto vgmsampcoll : sampColls()) {
    const auto sampcoll = dynamic_cast<AkaoSampColl*>(vgmsampcoll);
    const uint32_t numArts = static_cast<uint32_t>(sampcoll->akArts.size());
    numInstrsToAdd = numArts;

    for (uint32_t i = 0; i < numInstrsToAdd; i++) {
      AkaoArt *art = &sampcoll->akArts[i];
      AkaoInstr *newInstr = new AkaoInstr(instrSet, 0, 0, 0, sampcoll->starting_art_id + i);

      AkaoRgn *rgn = new AkaoRgn(newInstr, 0, 0);

      if (art->loop_point != 0)
        rgn->setLoopInfo(1, art->loop_point, sampcoll->samples[art->sample_num]->dataLength - art->loop_point);

      auto itSampleNum = artIdToSampleNumMap.find(art->artID);
      if (itSampleNum != artIdToSampleNumMap.end()) {
        rgn->setSampNum(itSampleNum->second);
      }

      psxConvADSR<AkaoRgn>(rgn, art->ADSR1, art->ADSR2, false);

      rgn->unityKey = art->unityKey;
      rgn->fineTune = art->fineTune;

      newInstr->addRgn(rgn);

      instrSet->aInstrs.push_back(newInstr);
    }
  }
}

void AkaoColl::postSynthFileCreation() {
  //if the 0xA1 event isn't used in the sequence, then we didn't modify the instrset
  //so skip this
  if (!static_cast<AkaoSeq*>(seq())->bUsesIndividualArts)
    return;

  AkaoInstrSet *instrSet = reinterpret_cast<AkaoInstrSet *>(instrSets()[0]);
  for (size_t i = 0; i < numInstrsToAdd; i++) {
    delete instrSet->aInstrs.back();
    instrSet->aInstrs.pop_back();
  }
}
