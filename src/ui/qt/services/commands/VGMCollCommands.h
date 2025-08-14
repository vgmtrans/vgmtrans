/*
 * VGMTrans (c) 2002-2025
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "GeneralCommands.h"
#include "SequencePlayer.h"
#include "services/commands/Command.h"
#include "VGMColl.h"

class PlayCommand : public SingleItemCommand<VGMColl> {
public:
  void executeItem(VGMColl* coll) const override {
    SequencePlayer::the().playCollection(coll);
  }
  [[nodiscard]] QKeySequence shortcutKeySequence() const override { return Qt::Key_Return; };
  [[nodiscard]] std::string name() const override { return "Play / Pause"; }
};