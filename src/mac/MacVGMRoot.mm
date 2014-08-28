//
//  MacVGMRoot.cpp
//  VGMTrans
//
//  Created by Mike on 8/27/14.
//
//

#include "MacVGMRoot.h"

void MacVGMRoot::SelectItem(VGMItem* item)
{

}

void MacVGMRoot::SelectColl(VGMColl* coll)
{

}

void MacVGMRoot::Play(void)
{

}

void MacVGMRoot::Pause(void)
{

}

void MacVGMRoot::Stop(void)
{

}

void MacVGMRoot::UI_SetRootPtr(VGMRoot** theRoot)
{

}

void MacVGMRoot::UI_PreExit()
{}

void MacVGMRoot::UI_Exit()
{}

void MacVGMRoot::UI_AddRawFile(RawFile* newFile)
{}

void MacVGMRoot::UI_CloseRawFile(RawFile* targFile)
{}

void MacVGMRoot::UI_OnBeginScan()
{}

void MacVGMRoot::UI_SetScanInfo()
{}

void MacVGMRoot::UI_OnEndScan()
{}

void MacVGMRoot::UI_AddVGMFile(VGMFile* theFile)
{}

void MacVGMRoot::UI_AddVGMSeq(VGMSeq* theSeq)
{}

void MacVGMRoot::UI_AddVGMInstrSet(VGMInstrSet* theInstrSet)
{}

void MacVGMRoot::UI_AddVGMSampColl(VGMSampColl* theSampColl)
{}

void MacVGMRoot::UI_AddVGMMisc(VGMMiscFile* theMiscFile)
{}

void MacVGMRoot::UI_AddVGMColl(VGMColl* theColl)
{}

void MacVGMRoot::UI_AddLogItem(LogItem* theLog)
{}

void MacVGMRoot::UI_RemoveVGMFile(VGMFile* targFile)
{}

void MacVGMRoot::UI_RemoveVGMColl(VGMColl* targColl)
{}

void MacVGMRoot::UI_BeginRemoveVGMFiles()
{}

void MacVGMRoot::UI_EndRemoveVGMFiles()
{}

void MacVGMRoot::UI_AddItem(VGMItem* item, VGMItem* parent, const std::wstring& itemName, void* UI_specific)
{}

void MacVGMRoot::UI_AddItemSet(VGMFile* file, std::vector<ItemSet>* itemset)
{}

std::wstring MacVGMRoot::UI_GetOpenFilePath(const std::wstring& suggestedFilename = L"", const std::wstring& extension = L"")
{
    std::wstring path = L"Placeholder";
    return path;
}

std::wstring MacVGMRoot::UI_GetSaveFilePath(const std::wstring& suggestedFilename, const std::wstring& extension = L"")
{
    std::wstring path = L"Placeholder";
    return path;
}

std::wstring MacVGMRoot::UI_GetSaveDirPath(const std::wstring& suggestedDir = L"")
{
    std::wstring path = L"Placeholder";
    return path;
}