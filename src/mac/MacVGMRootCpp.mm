//
//  MacVGMRootCpp.cpp
//  VGMTrans
//
//  Created by Mike on 8/27/14.
//
//

#import "MacVGMRoot.hh"
#include "MacVGMRootCpp.hh"
#include "VGMItem.h"
#include "VGMColl.h"

MacVGMRootCpp macroot;


MacVGMRootCpp::MacVGMRootCpp(void)
{
}

MacVGMRootCpp::~MacVGMRootCpp(void)
{
}

//void MacVGMRootCpp::SelectItem(VGMItem* item)
//{
////    [[MacVGMRoot sharedInstance] SelectItem:item];
//}
//
//void MacVGMRootCpp::SelectColl(VGMColl* coll)
//{
//
//}

void MacVGMRootCpp::Play(void)
{

}

void MacVGMRootCpp::Pause(void)
{

}

void MacVGMRootCpp::Stop(void)
{

}

void MacVGMRootCpp::UI_SetRootPtr(VGMRoot** theRoot)
{
    *theRoot = &macroot;
}

void MacVGMRootCpp::UI_PreExit()
{}

void MacVGMRootCpp::UI_Exit()
{}

void MacVGMRootCpp::UI_AddRawFile(RawFile* newFile)
{
    [[MacVGMRoot sharedInstance] UI_AddRawFile:newFile];
}

void MacVGMRootCpp::UI_CloseRawFile(RawFile* targFile)
{
    [[MacVGMRoot sharedInstance] UI_CloseRawFile:targFile];
}

void MacVGMRootCpp::UI_OnBeginScan()
{}

void MacVGMRootCpp::UI_SetScanInfo()
{}

void MacVGMRootCpp::UI_OnEndScan()
{}

void MacVGMRootCpp::UI_AddVGMFile(VGMFile* theFile)
{
    [[MacVGMRoot sharedInstance] UI_AddVGMFile:theFile];
}

void MacVGMRootCpp::UI_AddVGMSeq(VGMSeq* theSeq)
{}

void MacVGMRootCpp::UI_AddVGMInstrSet(VGMInstrSet* theInstrSet)
{}

void MacVGMRootCpp::UI_AddVGMSampColl(VGMSampColl* theSampColl)
{}

void MacVGMRootCpp::UI_AddVGMMisc(VGMMiscFile* theMiscFile)
{}

void MacVGMRootCpp::UI_AddVGMColl(VGMColl* theColl)
{
    [[MacVGMRoot sharedInstance] UI_AddVGMColl:theColl];
}

void MacVGMRootCpp::UI_AddLogItem(LogItem* theLog)
{
    [[MacVGMRoot sharedInstance] UI_AddLogItem:theLog];
}

void MacVGMRootCpp::UI_RemoveVGMFile(VGMFile* targFile)
{
    [[MacVGMRoot sharedInstance] UI_RemoveVGMFile:targFile];
}

void MacVGMRootCpp::UI_RemoveVGMColl(VGMColl* targColl)
{
    [[MacVGMRoot sharedInstance] UI_RemoveVGMColl:targColl];
}

void MacVGMRootCpp::UI_BeginRemoveVGMFiles()
{}

void MacVGMRootCpp::UI_EndRemoveVGMFiles()
{}

void MacVGMRootCpp::UI_AddItem(VGMItem* item, VGMItem* parent, const std::wstring& itemName, void* UI_specific)
{}

void MacVGMRootCpp::UI_AddItemSet(VGMFile* file, std::vector<ItemSet>* itemset)
{}

std::wstring MacVGMRootCpp::UI_GetOpenFilePath(const std::wstring& suggestedFilename, const std::wstring& extension)
{
    std::wstring path = L"Placeholder";
    return path;
}

std::wstring MacVGMRootCpp::UI_GetSaveFilePath(const std::wstring& suggestedFilename, const std::wstring& extension)
{
    std::wstring path = L"Placeholder";
    return path;
}

std::wstring MacVGMRootCpp::UI_GetSaveDirPath(const std::wstring& suggestedDir)
{
    std::wstring path = L"Placeholder";
    return path;
}

//@implementation MacVGMRootCpp
//
//- (void) SelectItem(VGMItem *item)
//{
//
//}
//
//@end