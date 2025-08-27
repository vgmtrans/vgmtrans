#include "AkaoMatcher.h"
#include "AkaoSeq.h"
#include "AkaoInstr.h"

bool isPsxPSFFile(RawFile* file) {
  return file->extension() == "psf" ||
         file->extension() == "minipsf" ||
         file->extension() == "psflib";
}

void AkaoMatcher::onFinishedScan(RawFile* rawfile) {
  std::vector<int> keys;
  keys.reserve(seqs.size());
  for (const auto& pair : seqs) {
    keys.push_back(pair.first);
  }

  for (int key : keys) {
    if (seqs.find(key) != seqs.end()) {
      AkaoSeq* seq = seqs[key];
      tryCreateCollection(seq->seq_id);
    }
  }

  // We assume psf files contain all of the files necessary to form a collection. Therefore, we
  // treat each one as a one-off and remove all of its detected files from future match consideration.
  if (isPsxPSFFile(rawfile)) {
    auto eraseByRawFile = [rawfile](auto& map) {
      std::erase_if(map, [rawfile](const auto& pair) {
          return pair.second->rawFile() == rawfile;
      });
    };
    auto eraseByRawFileVec = [rawfile](auto& vec) {
      vec.erase(std::remove_if(vec.begin(), vec.end(), [rawfile](auto* elem) {
          return elem->rawFile() == rawfile;
      }), vec.end());
    };
    eraseByRawFile(seqs);
    eraseByRawFile(instrSets);
    eraseByRawFileVec(sampColls);
  }
}

bool AkaoMatcher::onNewSeq(VGMSeq* seq) {
  if (auto akaoSeq = dynamic_cast<AkaoSeq *>(seq)) {
    seqs[akaoSeq->seq_id] = akaoSeq;
  }
  return true;
}

bool AkaoMatcher::onNewInstrSet(VGMInstrSet* instrSet) {
  if (auto akaoInstrSet = dynamic_cast<AkaoInstrSet*>(instrSet)) {
    instrSets[akaoInstrSet->id()] = akaoInstrSet;
  }
  return true;
}

bool AkaoMatcher::onNewSampColl(VGMSampColl* sampColl) {
  if (auto akaoSampColl = dynamic_cast<AkaoSampColl*>(sampColl)) {
    sampColls.push_back(akaoSampColl);
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
    // seq->id() represents the associated sample set id
    if (seq->id() > 0) {
      auto it = std::find_if(sampColls.rbegin(), sampColls.rend(), [seq](AkaoSampColl *sc) {
        return sc->id() == seq->id();
      });
      if (it != sampColls.rend()) {
        sampCollsToCheck.push_back(*it);
      } else {
        // PSF files may optimize out the IDs, so be lenient
        if (isPsxPSFFile(seq->rawFile()))
          return false;
      }
    }
    // Add the rest of the sample collections that are not already in sampCollsToCheck.
    // Iterate in reverse to prioritize sample collections most recently scanned.
    for (auto* sc : std::ranges::reverse_view(sampColls)) {
      if (std::find(sampCollsToCheck.begin(), sampCollsToCheck.end(), sc) == sampCollsToCheck.end()) {
        sampCollsToCheck.push_back(sc);
      }
    }

    std::vector<int> requiredArtIds;
    for (const auto &instr : instrSet->aInstrs) {
      for (const auto &region : instr->regions()) {
        AkaoRgn* akaoRegion = static_cast<AkaoRgn*>(region);
        // We will exclude articulation id 0, as it often just indicates an unused region
        if (akaoRegion->artNum != 0)
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

      // It is possible that no instrument regions reference any articulations in the associated
      // sample collection (FF8 106, for ex). In this case, the sequence still references the
      // sample collection via "Key-split" program changes where an articulation is specified rather
      // than an instrument. Therefore, always include the associated sample collection.
      if (matches || sc->id() == seq->id()) {
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

      return true;
    }
  }
  return false;
}
