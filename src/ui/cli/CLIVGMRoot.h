/**
 * VGMTrans (c) - 2002-2021
 * Licensed under the zlib license
 * See the included LICENSE for more information
 */

#pragma once

#include "Root.h"

#define CLI_APP_NAME "vgmtrans"

class CLIVGMRoot : public VGMRoot {

public:

  size_t GetNumCollections() {
    return vVGMColl.size();
  }

  void DisplayUsage();

  bool MakeOutputDir();

  bool ConvertAllCollections();

  bool ConvertCollection(VGMColl* coll);

  bool SaveMidi(VGMColl *coll);

  bool SaveSF2(VGMColl *coll);

  bool SaveDLS(VGMColl *coll);

  virtual bool OpenRawFile(const std::wstring &filename);

  virtual bool Init();

  virtual void UI_SetRootPtr(VGMRoot** theRoot);

  virtual void UI_Exit() {}

  virtual void UI_AddLogItem(LogItem* theLog);

  virtual std::wstring UI_GetOpenFilePath(const std::wstring& suggestedFilename = L"",
                                          const std::wstring& extension = L"");
  virtual std::wstring UI_GetSaveFilePath(const std::wstring& suggestedFilename,
                                          const std::wstring& extension = L"");
  virtual std::wstring UI_GetSaveDirPath(const std::wstring& suggestedDir = L"");

  vector<const string> inputFiles {};
  std::string outputDir {"."};
};

extern CLIVGMRoot cliroot;