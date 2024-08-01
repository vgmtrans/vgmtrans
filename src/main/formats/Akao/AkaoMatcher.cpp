#include "AkaoMatcher.h"
#include "AkaoSeq.h"
#include "AkaoInstr.h"

bool AkaoMatcher::onNewSeq(VGMSeq* seq) {
  if (auto akaoSeq = dynamic_cast<AkaoSeq *>(seq)) {
    seqs[akaoSeq->seq_id] = akaoSeq;
    return tryCreateCollection(akaoSeq->seq_id);
  }
  return true;
}

bool AkaoMatcher::onNewInstrSet(VGMInstrSet* instrSet) {
  if (auto akaoInstrSet = dynamic_cast<AkaoInstrSet*>(instrSet)) {
    instrSets[akaoInstrSet->id()] = akaoInstrSet;
    return tryCreateCollection(akaoInstrSet->id());
  }
  return true;
}

bool AkaoMatcher::onNewSampColl(VGMSampColl* sampColl) {
  if (auto akaoSampColl = dynamic_cast<AkaoSampColl*>(sampColl)) {
    sampColls.push_back(akaoSampColl);
    for (const auto& pair : seqs) {
      AkaoSeq* seq = pair.second;
      return tryCreateCollection(seq->seq_id);
    }
  }
  return true;
}

bool AkaoMatcher::onCloseSeq(VGMSeq* seq) {
  if (auto akaoSeq = dynamic_cast<AkaoSeq *>(seq)) {
    seqs.erase(akaoSeq->seq_id);
  }
  return true;
}

bool AkaoMatcher::onCloseInstrSet(VGMInstrSet* instrSet) {
  if (auto akaoInstrSet = dynamic_cast<AkaoInstrSet *>(instrSet)) {
    instrSets.erase(akaoInstrSet->id());
  }
  return true;
}

bool AkaoMatcher::onCloseSampColl(VGMSampColl* sampColl) {
  if (auto akaoSampColl = dynamic_cast<AkaoSampColl *>(sampColl)) {
    sampColls.erase(std::remove(sampColls.begin(), sampColls.end(), akaoSampColl), sampColls.end());
  }
  return true;
}

bool AkaoMatcher::tryCreateCollection(int id) {
  auto itSeq = seqs.find(id);
  auto itInstrSet = instrSets.find(id);

  if (itSeq != seqs.end() && itInstrSet != instrSets.end() && !sampColls.empty()) {
    AkaoSeq* seq = itSeq->second;
    AkaoInstrSet* instrSet = itInstrSet->second;

    std::vector<AkaoSampColl *> sampCollsToCheck;
    // id() represents the associated sample set id
    if (seq->id() != -1) {
      auto it = std::find_if(sampColls.begin(), sampColls.end(), [seq](AkaoSampColl *sc) {
        return sc->id() == seq->id();
      });
      if (it != sampColls.end()) {
        sampCollsToCheck.push_back(*it);
      } else if (seq->rawFile()->extension() != "psf") {
        return false;
      }
    }
    // Add the rest of the sample collections that are not already in sampCollsToCheck
    for (auto* sc : std::ranges::reverse_view(sampColls)) {
      if (std::find(sampCollsToCheck.begin(), sampCollsToCheck.end(), sc) == sampCollsToCheck.end()) {
        sampCollsToCheck.push_back(sc);
      }
    }

    std::vector<int> requiredArtIds;
    for (const auto &instr : instrSet->aInstrs) {
      for (const auto &region : instr->regions()) {
        AkaoRgn* akaoRegion = static_cast<AkaoRgn*>(region);
        // We will exclude articulation id 0, as it often indicates an unused artic
        // Also ignoring values > 0x60 is a hack that allows compatability with many psfs.
        if (akaoRegion->artNum != 0 && akaoRegion->artNum <= 0x60)
          requiredArtIds.emplace_back(akaoRegion->artNum);
      }
    }
    std::sort(requiredArtIds.begin(), requiredArtIds.end());
    requiredArtIds.erase(std::unique(requiredArtIds.begin(), requiredArtIds.end()), requiredArtIds.end());

    std::vector<AkaoSampColl *> matchingSampColls;

    for (auto *sc : sampCollsToCheck) {
      bool matches = std::any_of(requiredArtIds.begin(), requiredArtIds.end(), [sc](int artId) {
        return artId >= sc->starting_art_id && artId < (sc->starting_art_id + sc->nNumArts);
      });

      if (matches) {
        matchingSampColls.push_back(sc);

        // Remove the IDs covered by this sample collection
        auto end = std::remove_if(requiredArtIds.begin(), requiredArtIds.end(), [sc](int artId) {
          return artId >= sc->starting_art_id && artId < (sc->starting_art_id + sc->nNumArts);
        });

        // Erase the removed elements
        requiredArtIds.erase(end, requiredArtIds.end());
        if (requiredArtIds.empty())
          break;
      }
    }

    if (matchingSampColls.size() > 0) {
      if (std::all_of(requiredArtIds.begin(), requiredArtIds.end(), [&matchingSampColls](int artId) {
            return std::any_of(matchingSampColls.begin(), matchingSampColls.end(), [artId](AkaoSampColl *sc) {
              return artId >= sc->starting_art_id && artId < (sc->starting_art_id + sc->nNumArts);
            });
          })) {
        auto coll = fmt->newCollection();
        if (!coll) return false;

        coll->setName(seq->name());
        coll->useSeq(seq);
        coll->addInstrSet(instrSet);

        // Sort the vector by starting_art_id in ascending order
        std::sort(matchingSampColls.begin(), matchingSampColls.end(),
        [](AkaoSampColl* a, AkaoSampColl* b) {
            return a->starting_art_id < b->starting_art_id;
        });
        for (auto *sc : matchingSampColls) {
          coll->addSampColl(sc);
        }

        seqs.erase(seq->seq_id);

        if (!coll->load()) {
          delete coll;
          return false;
        }

        std::cout << "Collection created successfully!" << std::endl;
        return true;
      }
    }
  }
  return false;
}
