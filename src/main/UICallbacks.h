#pragma once

#include "common.h"

class VGMColl;
class VGMFile;
class VGMItem;
class RawFile;
class LogItem;
FORWARD_DECLARE_TYPEDEF_STRUCT(ItemSet);


class UICallbacks {
 public:
  UICallbacks(void) { }

  virtual void UI_PreExit() { }
  virtual void UI_Exit() = 0;
  virtual void UI_AddRawFile(RawFile *newFile) { }
  virtual void UI_CloseRawFile(RawFile *targFile) { }

  virtual void UI_OnBeginScan() { }
  virtual void UI_OnEndScan() { }
  virtual void UI_AddVGMFile(VGMFile *theFile) { }
  virtual void UI_AddVGMColl(VGMColl *theColl) { }
  virtual void UI_AddLogItem(LogItem *theLog) { }
  virtual void UI_RemoveVGMFile(VGMFile *theFile) { }
  virtual void UI_BeginRemoveVGMFiles() { }
  virtual void UI_EndRemoveVGMFiles() { }
  virtual void UI_RemoveVGMColl(VGMColl *theColl) { }
  virtual void UI_AddItem(VGMItem *item, VGMItem *parent, const std::wstring &itemName, void *UI_specific) { }
  virtual void UI_AddItemSet(void *UI_specific, std::vector<ItemSet> *itemset) { }
  virtual std::wstring UI_GetOpenFilePath(const std::wstring &suggestedFilename = L"", const std::wstring &extension = L"") = 0;
  virtual std::wstring UI_GetSaveFilePath(const std::wstring &suggestedFilename, const std::wstring &extension = L"") = 0;
  virtual std::wstring UI_GetSaveDirPath(const std::wstring &suggestedDir = L"") = 0;
};
