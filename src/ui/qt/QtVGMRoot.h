/*
 * VGMTrans (c) 2002-2021
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <QObject>
#include "Root.h"

class QtVGMRoot final : public QObject, public VGMRoot {
  Q_OBJECT

public:
  ~QtVGMRoot() override = default;

  const std::wstring UI_GetResourceDirPath();
  void UI_SetRootPtr(VGMRoot** theRoot) override;
  void UI_PreExit() override;
  void UI_Exit() override;
  void UI_AddRawFile(RawFile* newFile) override;
  void UI_CloseRawFile(RawFile* targFile) override;

  void UI_OnBeginLoadRawFile() override;
  void UI_OnEndLoadRawFile() override;
  void UI_OnBeginScan() override;
  void UI_SetScanInfo() override;
  void UI_OnEndScan() override;
  void UI_AddVGMFile(VGMFile* theFile) override;
  void UI_AddVGMSeq(VGMSeq* theSeq) override;
  void UI_AddVGMInstrSet(VGMInstrSet* theInstrSet) override;
  void UI_AddVGMSampColl(VGMSampColl* theSampColl) override;
  void UI_AddVGMMisc(VGMMiscFile* theMiscFile) override;
  void UI_AddVGMColl(VGMColl* theColl) override;
  void UI_RemoveVGMColl(VGMColl* targColl) override;
  void UI_BeginRemoveVGMFiles() override;
  void UI_EndRemoveVGMFiles() override;
  void UI_AddItem(VGMItem* item, VGMItem* parent, const std::wstring& itemName,
                  void* UI_specific) override;
  std::wstring UI_GetOpenFilePath(const std::wstring& suggestedFilename = L"",
                                          const std::wstring& extension = L"") override;
  std::wstring UI_GetSaveFilePath(const std::wstring& suggestedFilename,
                                          const std::wstring& extension = L"") override;
  std::wstring UI_GetSaveDirPath(const std::wstring& suggestedDir = L"") override;

private:
  int rawFileLoadRecurseStack = 0;

signals:
  void UI_BeganLoadingRawFile();
  void UI_EndedLoadingRawFile();
  void UI_AddedRawFile();
  void UI_RemovedRawFile();
  void UI_AddedVGMFile();
  void UI_AddedVGMColl();
  void UI_RemovedVGMColl();
  void UI_RemoveVGMFile(VGMFile* targFile) override;
  void UI_AddLogItem(LogItem* theLog) override;
};

extern QtVGMRoot qtVGMRoot;