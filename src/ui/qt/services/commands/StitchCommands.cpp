/*
* VGMTrans (c) 2002-2026
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
 */

#include "services/commands/StitchCommands.h"

#include "Root.h"
#include "VGMColl.h"
#include "VGMSeq.h"
#include "widgets/StitchUI.h"

#include <vector>

#include <QApplication>

void StitchSequencesCommand::executeItems(std::vector<VGMFile*> vgmfiles) const {
  std::vector<VGMColl*> collections;
  collections.reserve(vgmfiles.size());

  for (VGMFile* file : vgmfiles) {
    auto* seq = dynamic_cast<VGMSeq*>(file);
    if (!seq) {
      continue;
    }

    const auto& assocColls = seq->assocColls();
    const VGMColl* coll = assocColls.empty() ? nullptr : assocColls.front();
    if (!coll) {
      pRoot->UI_toast("Each selected sequence must be associated with a collection to stitch.",
                      ToastType::Error, 15000);
      return;
    }
    collections.push_back(const_cast<VGMColl*>(coll));
  }

  stitchui::openCollectionStitchBalloon(collections, QApplication::activeWindow());
}

void StitchCollectionsCommand::executeItems(std::vector<VGMColl*> vgmcolls) const {
  stitchui::openCollectionStitchBalloon(vgmcolls, QApplication::activeWindow());
}
