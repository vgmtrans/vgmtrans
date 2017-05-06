//
// Created by Mike on 8/30/14.
//

#include "QtUICallbacks.h"
#include "MenuBar.h"

QtUICallbacks qtUICallbacks;

QtUICallbacks::QtUICallbacks(void)
{
}

void QtUICallbacks::UI_PreExit()
{}

void QtUICallbacks::UI_Exit()
{}

void QtUICallbacks::UI_AddRawFile(RawFile* newFile)
{
    this->UI_AddedRawFile();
}

void QtUICallbacks::UI_CloseRawFile(RawFile* targFile)
{
    this->UI_RemovedRawFile();
}

void QtUICallbacks::UI_OnBeginScan()
{}

void QtUICallbacks::UI_OnEndScan()
{}

void QtUICallbacks::UI_AddVGMFile(VGMFile* theFile) {
    this->UI_AddedVGMFile();
}

void QtUICallbacks::UI_AddVGMColl(VGMColl* theColl)
{
    this->UI_AddedVGMColl();
}

void QtUICallbacks::UI_AddLogItem(LogItem* theLog)
{
//    [[MacVGMRoot sharedInstance] UI_AddLogItem:theLog];
}

void QtUICallbacks::UI_RemoveVGMFile(VGMFile* targFile)
{
    this->UI_RemovedVGMFile();
}

void QtUICallbacks::UI_RemoveVGMColl(VGMColl* targColl)
{
    this->UI_RemovedVGMColl();
}

void QtUICallbacks::UI_BeginRemoveVGMFiles()
{}

void QtUICallbacks::UI_EndRemoveVGMFiles()
{}

void QtUICallbacks::UI_AddItem(VGMItem* item, VGMItem* parent, const std::wstring& itemName, void* UI_specific)
{}

void QtUICallbacks::UI_AddItemSet(VGMFile* file, std::vector<ItemSet>* itemset)
{}

std::wstring QtUICallbacks::UI_GetOpenFilePath(const std::wstring& suggestedFilename, const std::wstring& extension)
{
    std::wstring path = L"Placeholder";
    return path;
}

std::wstring QtUICallbacks::UI_GetSaveFilePath(const std::wstring& suggestedFilename, const std::wstring& extension)
{
    std::wstring path = L"Placeholder";
    return path;
}

std::wstring QtUICallbacks::UI_GetSaveDirPath(const std::wstring& suggestedDir)
{
    std::wstring path = L"Placeholder";
    return path;
}