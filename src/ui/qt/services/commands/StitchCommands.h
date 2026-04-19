/*
* VGMTrans (c) 2002-2026
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
 */

#pragma once

#include <vector>

#include "services/commands/GeneralCommands.h"

class QWidget;
class VGMColl;
class QAbstractButton;



class StitchSequencesCommand : public ItemListCommand<VGMFile> {
public:
  void executeItems(std::vector<VGMFile*> vgmfiles) const override;
  [[nodiscard]] std::string name() const override { return "Stitch as MIDI + SF2"; }
  [[nodiscard]] std::optional<MenuPath> menuPath() const override { return MenuPaths::Convert; }
};

class StitchCollectionsCommand : public ItemListCommand<VGMColl> {
public:
  void executeItems(std::vector<VGMColl*> vgmcolls) const override;
  [[nodiscard]] std::string name() const override { return "Stitch as MIDI + SF2"; }
  [[nodiscard]] std::optional<MenuPath> menuPath() const override { return MenuPaths::Convert; }
};
