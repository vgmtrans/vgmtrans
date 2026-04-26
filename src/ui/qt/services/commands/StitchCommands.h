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
  [[nodiscard]] bool isEnabledForItems(const std::vector<VGMFile*>& vgmfiles) const override {
    return vgmfiles.size() >= 2;
  }
  [[nodiscard]] std::string name() const override { return "Stitch"; }
  [[nodiscard]] std::optional<MenuPath> menuPath() const override { return MenuPaths::Convert; }
};

class StitchCollectionsCommand : public ItemListCommand<VGMColl> {
public:
  void executeItems(std::vector<VGMColl*> vgmcolls) const override;
  [[nodiscard]] bool isEnabledForItems(const std::vector<VGMColl*>& vgmcolls) const override {
    return vgmcolls.size() >= 2;
  }
  [[nodiscard]] std::string name() const override { return "Stitch"; }
  [[nodiscard]] std::optional<MenuPath> menuPath() const override { return MenuPaths::Convert; }
};
