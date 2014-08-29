//
// Created by Mike on 8/27/14.
//

#import "MacVGMRoot.hh"
#import "MacVGMRootCpp.hh"
#import "NSString+VGMTrans.h"
#import "VGRawFileTableView.h"

class VGMItem;
class VGMFile;
class VGMColl;
class VGMRoot;
class VGMSeq;
class VGMInstrSet;
class VGMSampColl;
class VGMMiscFile;
class LogItem;
class RawFile;

NSString * const kVGDidAddRawFileNotification = @"kVGDidAddRawFileNotification";
NSString * const kVGDidRemoveRawFileNotification = @"kVGDidRemoveRawFileNotification";
NSString * const kVGDidAddVGMFileNotification = @"kVGDidAddVGMFileNotification";
NSString * const kVGDidRemoveVGMFileNotification = @"kVGDidRemoveVGMFileNotification";
NSString * const kVGDidAddCollectionNotification = @"kVGDidAddCollectionNotification";
NSString * const kVGDidRemoveCollectionNotification = @"kVGDidRemoveCollectionNotification";
NSString * const kVGDidAddLogItemNotification = @"kVGDidAddLogItemNotification";

@implementation MacVGMRoot

+ (instancetype)sharedInstance
{
    static dispatch_once_t once;
    static MacVGMRoot *sharedInstance;
    dispatch_once(&once, ^ { sharedInstance = [[self alloc] init]; });
    return sharedInstance;
}

- (void) Init
{
    macroot.Init();
}

- (void) Reset
{
    macroot.Reset();
}

- (void) Exit
{
    macroot.Exit();
}

- (bool) OpenRawFile:(NSString *)filename
{
    NSLog(@"TESTING");
    return macroot.OpenRawFile([NSString wcharFromString:filename]);
}

- (bool) CreateVirtFile:(uint8_t *)databuf fileSize:(uint32_t)fileSize filename:(NSString *)filename parRawFileFullPath:(NSString *)parRawFileFullPath
{
    return macroot.CreateVirtFile(databuf, fileSize, [NSString wcharFromString:filename], [NSString wcharFromString:parRawFileFullPath]);
}

- (bool) SetupNewRawFile:(RawFile *)newRawFile
{
    return macroot.SetupNewRawFile(newRawFile);
}

- (bool) CloseRawFile:(RawFile *)targFile
{
    return macroot.CloseRawFile(targFile);
}

- (void) AddVGMFile:(VGMFile *)theFile
{
    macroot.AddVGMFile(theFile);
}

- (void) RemoveVGMFile:(VGMFile *)theFile bRemoveFromRaw:(bool)bRemoveFromRaw
{
    macroot.RemoveVGMFile(theFile, bRemoveFromRaw);
}

- (void) AddVGMColl:(VGMColl *)theColl
{
    macroot.AddVGMColl(theColl);
}

- (void) RemoveVGMColl:(VGMColl *)theFile;
{
    macroot.RemoveVGMColl(theFile);
}

- (void) AddLogItem:(LogItem *)theLog
{
    macroot.AddLogItem(theLog);
}

//- (void) UI_SetRootPtr:(VGMRoot**) theRoot
//{
//    *theRoot = &macroot;
//}

- (void) UI_AddRawFile:(RawFile*)newFile
{
    [[NSNotificationCenter defaultCenter] postNotificationName:kVGDidAddRawFileNotification object:nil];
}

- (void) UI_CloseRawFile:(RawFile*)newFile
{
    [[NSNotificationCenter defaultCenter] postNotificationName:kVGDidRemoveRawFileNotification object:nil];
}

- (void) UI_AddVGMFile:(VGMFile*)theFile
{
    [[NSNotificationCenter defaultCenter] postNotificationName:kVGDidAddVGMFileNotification object:nil];
}

- (void) UI_RemoveVGMFile:(VGMFile*)theFile
{
    [[NSNotificationCenter defaultCenter] postNotificationName:kVGDidRemoveVGMFileNotification object:nil];
}

- (void) UI_AddVGMColl:(void*) theColl
{
    [[NSNotificationCenter defaultCenter] postNotificationName:kVGDidAddCollectionNotification object:nil];
}

- (void) UI_RemoveVGMColl:(void*) theColl
{
    [[NSNotificationCenter defaultCenter] postNotificationName:kVGDidRemoveCollectionNotification object:nil];
}

- (void) UI_AddLogItem:(void*) theLog
{

}

@end

