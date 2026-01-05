/**
 * VGMTrans (c) - 2002-2021
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#pragma once

#include "Root.h"
#include <set>
#include <filesystem>

#define CLI_APP_NAME "vgmtrans"

namespace fs = std::filesystem;

class CLIVGMRoot : public VGMRoot {

public:

  size_t numCollections() {
    return vgmColls().size();
  }

  void displayUsage();

  void displayHelp();

  bool makeOutputDir();

  bool exportAllCollections();

  bool exportCollection(VGMColl* coll);

  bool saveMidi(const VGMColl *coll);

  bool saveSF2(VGMColl *coll);

  bool saveDLS(VGMColl *coll);

  bool openRawFile(const std::filesystem::path &filename) override;

  bool init() override;

  void UI_setRootPtr(VGMRoot** theRoot) override;

  void UI_log(LogItem* theLog) override;

  std::filesystem::path UI_getSaveFilePath(const std::string& suggestedFilename,
                                           const std::string& extension = "") override;

  std::filesystem::path UI_getSaveDirPath(const std::filesystem::path& suggestedDir = {}) override;

  std::set<fs::path> inputFiles = {};
  fs::path outputDir = fs::path(".");
};

extern CLIVGMRoot cliroot;
