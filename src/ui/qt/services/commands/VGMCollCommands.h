/*
 * VGMTrans (c) 2002-2025
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include "GeneralCommands.h"
#include "SequencePlayer.h"
#include "services/NotificationCenter.h"
#include "services/commands/Command.h"
#include "VGMColl.h"

using MenuPath = Command::MenuPath;

/**
 * A command for playing or pausing a collection with the SequencePlayer
 */
class PlayCommand : public SingleItemCommand<VGMColl> {
public:
  void executeItem(VGMColl* coll) const override {
    SequencePlayer::the().playCollection(coll);
  }
  [[nodiscard]] QList<QKeySequence> shortcutKeySequences() const override { return {Qt::Key_Return}; };
  [[nodiscard]] std::string name() const override { return "Play / Pause"; }
};

class RenameCommand : public SingleItemCommand<VGMColl> {
public:
  void executeItem(VGMColl* coll) const override {
    NotificationCenter::the()->requestVGMCollRename(coll);
  }

  [[nodiscard]] QList<QKeySequence> shortcutKeySequences() const override {
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    return {};
#else
    return {Qt::Key_F2};
#endif
  }

  [[nodiscard]] bool shouldDisplayForItemCount(size_t count) const override { return count == 1; }
  [[nodiscard]] std::string name() const override { return "Rename"; }
};
