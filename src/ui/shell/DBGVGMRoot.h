/**
 * VGMTrans (c) - 2002-2026
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#pragma once

#include "Root.h"
#include <vector>
#include <filesystem>

class DBGVGMRoot : public VGMRoot {
public:
  DBGVGMRoot();
  ~DBGVGMRoot() override = default;

  bool init() override;

  void UI_setRootPtr(VGMRoot** theRoot) override;
  void UI_log(LogItem* theLog) override;
  std::filesystem::path UI_getSaveFilePath(const std::string& suggestedFilename,
                                           const std::string& extension = "") override;
  std::filesystem::path UI_getSaveDirPath(const std::filesystem::path& suggestedDir = {}) override;

  // Debug specific methods
  std::vector<VGMFile*> getLoadedFiles();
};

extern DBGVGMRoot dbgRoot;
