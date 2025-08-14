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

class PlayCommand : public Command {
public:
  PlayCommand()
      : m_contextFactory(std::make_shared<ItemListContextFactory<VGMColl>>()) {}

  void execute(CommandContext& context) override {
    auto& vgmContext = dynamic_cast<ItemListCommandContext<VGMColl>&>(context);
    const VGMColl* coll = vgmContext.items().front();
    SequencePlayer::the().playCollection(coll);
  }

  [[nodiscard]] std::shared_ptr<CommandContextFactory> contextFactory() const override {
    return m_contextFactory;
  }

  [[nodiscard]] QKeySequence shortcutKeySequence() const override {
    return Qt::Key_Return;
  };

  [[nodiscard]] std::string name() const override { return "Play / Pause"; }

private:
  std::shared_ptr<ItemListContextFactory<VGMColl>> m_contextFactory;
};