//
// Created by Mike on 8/30/14.
//

#include "QtVGMRoot.h"

QtVGMRoot qtVGMRoot;

QtVGMRoot::QtVGMRoot(void)
{

}

QtVGMRoot::~QtVGMRoot(void)
{

}

//void QtVGMRoot::Play(void)
//{
//
//}
//
//void QtVGMRoot::Pause(void)
//{
//
//}
//
//void QtVGMRoot::Stop(void)
//{
//
//}

void QtVGMRoot::UI_SetRootPtr(VGMRoot** theRoot)
{
    *theRoot = &qtVGMRoot;
}

void QtVGMRoot::UI_PreExit()
{}

void QtVGMRoot::UI_Exit()
{}

void QtVGMRoot::UI_AddRawFile(RawFile* newFile)
{
    this->UI_AddedRawFile();
//    [[MacVGMRoot sharedInstance] UI_AddRawFile:newFile];
}

void QtVGMRoot::UI_CloseRawFile(RawFile* targFile)
{
//    [[MacVGMRoot sharedInstance] UI_CloseRawFile:targFile];
}

void QtVGMRoot::UI_OnBeginScan()
{}

void QtVGMRoot::UI_SetScanInfo()
{}

void QtVGMRoot::UI_OnEndScan()
{}

void QtVGMRoot::UI_AddVGMFile(VGMFile* theFile)
{
    this->UI_AddedVGMFile();
//    [[MacVGMRoot sharedInstance] UI_AddVGMFile:theFile];
}

void QtVGMRoot::UI_AddVGMSeq(VGMSeq* theSeq)
{}

void QtVGMRoot::UI_AddVGMInstrSet(VGMInstrSet* theInstrSet)
{}

void QtVGMRoot::UI_AddVGMSampColl(VGMSampColl* theSampColl)
{}

void QtVGMRoot::UI_AddVGMMisc(VGMMiscFile* theMiscFile)
{}

void QtVGMRoot::UI_AddVGMColl(VGMColl* theColl)
{
    this->UI_AddedVGMColl();
//    [[MacVGMRoot sharedInstance] UI_AddVGMColl:theColl];
}

void QtVGMRoot::UI_AddLogItem(LogItem* theLog)
{
//    [[MacVGMRoot sharedInstance] UI_AddLogItem:theLog];
}

void QtVGMRoot::UI_RemoveVGMFile(VGMFile* targFile)
{
//    [[MacVGMRoot sharedInstance] UI_RemoveVGMFile:targFile];
}

void QtVGMRoot::UI_RemoveVGMColl(VGMColl* targColl)
{
//    [[MacVGMRoot sharedInstance] UI_RemoveVGMColl:targColl];
}

void QtVGMRoot::UI_BeginRemoveVGMFiles()
{}

void QtVGMRoot::UI_EndRemoveVGMFiles()
{}

void QtVGMRoot::UI_AddItem(VGMItem* item, VGMItem* parent, const std::wstring& itemName, void* UI_specific)
{}

void QtVGMRoot::UI_AddItemSet(VGMFile* file, std::vector<ItemSet>* itemset)
{}

std::wstring QtVGMRoot::UI_GetOpenFilePath(const std::wstring& suggestedFilename, const std::wstring& extension)
{
    std::wstring path = L"Placeholder";
    return path;
}

std::wstring QtVGMRoot::UI_GetSaveFilePath(const std::wstring& suggestedFilename, const std::wstring& extension)
{
    std::wstring path = L"Placeholder";
    return path;
}

std::wstring QtVGMRoot::UI_GetSaveDirPath(const std::wstring& suggestedDir)
{
    std::wstring path = L"Placeholder";
    return path;
}