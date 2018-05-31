#pragma once

#include <QObject>
#include "main/UICallbacks.h"

class QtUICallbacks
        : public QObject, public UICallbacks
{
   Q_OBJECT

public:
  QtUICallbacks(void);

 signals:
  void UI_AddedRawFile();
  void UI_RemovedRawFile();
  void UI_AddedVGMFile();
  void UI_RemovedVGMFile();
  void UI_AddedVGMColl();
  void UI_RemovedVGMColl();

  void UI_SelectedVGMColl();

 private:
  virtual void UI_PreExit();
  virtual void UI_Exit();
  virtual void UI_AddRawFile(RawFile* newFile);
  virtual void UI_CloseRawFile(RawFile* targFile);

  virtual void UI_OnBeginScan();
  virtual void UI_OnEndScan();
  virtual void UI_AddVGMFile(VGMFile* theFile);
  virtual void UI_AddVGMColl(VGMColl* theColl);
  virtual void UI_AddLogItem(LogItem* theLog);
  virtual void UI_RemoveVGMFile(VGMFile* targFile);
  virtual void UI_RemoveVGMColl(VGMColl* targColl);
  virtual void UI_BeginRemoveVGMFiles();
  virtual void UI_EndRemoveVGMFiles();
  virtual void UI_AddItem(VGMItem* item, VGMItem* parent, const std::wstring& itemName, void* UI_specific);
  virtual void UI_AddItemSet(VGMFile* file, std::vector<ItemSet>* itemset);
  virtual std::wstring UI_GetOpenFilePath(const std::wstring& suggestedFilename = L"", const std::wstring& extension = L"");
  virtual std::wstring UI_GetSaveFilePath(const std::wstring& suggestedFilename, const std::wstring& extension = L"");
  virtual std::wstring UI_GetSaveDirPath(const std::wstring& suggestedDir = L"");
};

extern QtUICallbacks qtUICallbacks;