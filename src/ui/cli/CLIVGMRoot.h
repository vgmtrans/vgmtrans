/**
 * VGMTrans (c) - 2002-2021
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#pragma once

#include "Root.h"

#define CLI_APP_NAME "vgmtrans"

namespace fs = ghc::filesystem;

class CLIVGMRoot : public VGMRoot {

public:

  size_t GetNumCollections() {
    return vVGMColl.size();
  }

  void DisplayUsage();

  void DisplayHelp();

  bool MakeOutputDir();

  bool ExportAllCollections();

  bool ExportCollection(VGMColl* coll);

  bool SaveMidi(VGMColl *coll);

  bool SaveSF2(VGMColl *coll);

  bool SaveDLS(VGMColl *coll);

  virtual bool OpenRawFile(const string &filename);

  virtual bool Init();

  virtual void UI_SetRootPtr(VGMRoot** theRoot);

  virtual void UI_Exit() {}

  virtual void UI_AddLogItem(LogItem* theLog);

  virtual void UpdateCollections();

  virtual string UI_GetOpenFilePath(const string& suggestedFilename = "",
                                          const string& extension = "");

  virtual string UI_GetSaveFilePath(const string& suggestedFilename,
                                          const string& extension = "");

  virtual string UI_GetSaveDirPath(const string& suggestedDir = "");

  set<fs::path> inputFiles = {};
  fs::path outputDir = fs::path(".");
};

extern CLIVGMRoot cliroot;