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

  bool MakeOutputDir();

  bool ExportAllCollections();

  bool ExportCollection(VGMColl* coll);

  bool SaveMidi(VGMColl *coll);

  bool SaveSF2(VGMColl *coll);

  bool SaveDLS(VGMColl *coll);

  virtual bool OpenRawFile(const wstring &filename);

  virtual bool Init();

  virtual void UI_SetRootPtr(VGMRoot** theRoot);

  virtual void UI_Exit() {}

  virtual void UI_AddLogItem(LogItem* theLog);

  virtual wstring UI_GetOpenFilePath(const wstring& suggestedFilename = L"",
                                          const wstring& extension = L"");
  virtual wstring UI_GetSaveFilePath(const wstring& suggestedFilename,
                                          const wstring& extension = L"");
  virtual wstring UI_GetSaveDirPath(const wstring& suggestedDir = L"");

  set<string> inputFiles = {};
  fs::path outputDir = fs::path(".");
};

extern CLIVGMRoot cliroot;