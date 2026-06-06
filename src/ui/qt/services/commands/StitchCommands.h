/*
* VGMTrans (c) 2002-2026
* Licensed under the zlib license,
* refer to the included LICENSE.txt file
 */

#pragma once

#include "services/commands/GeneralCommands.h"

#include <optional>
#include <span>
#include <string>
#include <vector>

class QWidget;
class VGMColl;
class QAbstractButton;



class StitchSequencesCommand : public ItemListCommand<VGMFile> {
public:
  void executeItems(std::span<VGMFile* const> vgmfiles) const override;
  [[nodiscard]] bool isEnabledForItems(std::span<VGMFile* const> vgmfiles) const override {
    return vgmfiles.size() >= 2;
  }
  [[nodiscard]] std::string name() const override { return "Stitch"; }
  [[nodiscard]] std::optional<MenuPath> menuPath() const override { return MenuPaths::Convert; }
};

class StitchCollectionsCommand : public ItemListCommand<VGMColl> {
public:
  void executeItems(std::span<VGMColl* const> vgmcolls) const override;
  [[nodiscard]] bool isEnabledForItems(std::span<VGMColl* const> vgmcolls) const override {
    return vgmcolls.size() >= 2;
  }
  [[nodiscard]] std::string name() const override { return "Stitch"; }
  [[nodiscard]] std::optional<MenuPath> menuPath() const override { return MenuPaths::Convert; }
};
