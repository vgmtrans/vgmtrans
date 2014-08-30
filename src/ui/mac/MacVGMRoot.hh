//
// Created by Mike on 8/27/14.
//

#import <Foundation/Foundation.h>

extern NSString * const kVGDidAddRawFileNotification;
extern NSString * const kVGDidRemoveRawFileNotification;
extern NSString * const kVGDidAddVGMFileNotification;
extern NSString * const kVGDidRemoveVGMFileNotification;
extern NSString * const kVGDidAddCollectionNotification;
extern NSString * const kVGDidRemoveCollectionNotification;
extern NSString * const kVGDidAddLogItemNotification;


@interface MacVGMRoot : NSObject
+ (id)sharedInstance;

- (void) Init;
- (void) Reset;
- (void) Exit;
- (bool) OpenRawFile:(NSString *)filename;
- (bool) CreateVirtFile:(uint8_t *)databuf fileSize:(uint32_t)fileSize filename:(NSString *)filename parRawFileFullPath:(NSString *)parRawFileFullPath;
- (bool) SetupNewRawFile:(void *)newRawFile;
- (bool) CloseRawFile:(void *)targFile;
- (void) AddVGMFile:(void *)theFile;
- (void) RemoveVGMFile:(void *)theFile bRemoveFromRaw:(bool)bRemoveFromRaw;
- (void) AddVGMColl:(void *)theColl;
- (void) RemoveVGMColl:(void *)theColl;
- (void) AddLogItem:(void *)theLog;


//- (void) SelectItem:(VGMItem*) item;
//- (void) SelectColl:(VGMColl*) coll;
//- (void) Play;
//- (void) Pause;
//- (void) Stop;
//inline bool AreWeExiting() { return bExiting; }
//
- (void) UI_SetRootPtr:(void**) theRoot;
//- (void) UI_PreExit;
//- (void) UI_Exit;
- (void) UI_AddRawFile:(void*) newFile;
- (void) UI_CloseRawFile:(void*) targFile;
//
//- (void) UI_OnBeginScan;
//- (void) UI_SetScanInfo;
//- (void) UI_OnEndScan;
- (void) UI_AddVGMFile:(void*) theFile;
//- (void) UI_AddVGMSeq:(VGMSeq*) theSeq;
//- (void) UI_AddVGMInstrSet:(VGMInstrSet*) theInstrSet;
//- (void) UI_AddVGMSampColl:(VGMSampColl*) theSampColl;
//- (void) UI_AddVGMMisc:(VGMMiscFile*) theMiscFile;
- (void) UI_AddVGMColl:(void*) theColl;
- (void) UI_AddLogItem:(void*) theLog;
- (void) UI_RemoveVGMFile:(void*) targFile;
- (void) UI_RemoveVGMColl:(void*) targColl;
//- (void) UI_BeginRemoveVGMFiles;
//- (void) UI_EndRemoveVGMFiles;

//- (void) UI_AddItem:(VGMItem*) item parent:(VGMItem*)parent, const std::wstring& itemName, void* UI_specific);
//- (void) UI_AddItemSet(VGMFile* file, std::vector<ItemSet>* itemset);
//- std::wstring UI_GetOpenFilePath(const std::wstring& suggestedFilename = L"", const std::wstring& extension = L"");
//virtual std::wstring UI_GetSaveFilePath(const std::wstring& suggestedFilename, const std::wstring& extension = L"");
//virtual std::wstring UI_GetSaveDirPath(const std::wstring& suggestedDir = L"");

@end