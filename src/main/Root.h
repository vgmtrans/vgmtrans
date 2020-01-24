/*
 * VGMTrans (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */
#pragma once

#include <memory>

#include "common.h"
#include "VGMTag.h"
#include "LogManager.h"

class VGMScanner;
class VGMColl;
class VGMFile;
class VGMItem;

class RawFile;
class VGMSeq;
class VGMInstrSet;
class VGMSampColl;
class VGMMiscFile;

struct ItemSet;

class VGMRoot {
   public:
    VGMRoot() = default;
    virtual ~VGMRoot() = default;

    bool Init(void);
    void Reset(void);
    void Exit(void);
    bool OpenRawFile(const std::string &filename);
    bool CreateVirtFile(uint8_t *databuf, uint32_t fileSize, const std::string &filename,
                        const std::string &parRawFileFullPath = "", VGMTag tag = VGMTag());
    bool SetupNewRawFile(std::shared_ptr<RawFile> newRawFile);
    bool CloseRawFile(RawFile *targFile);
    void AddVGMFile(VGMFile *theFile);
    void RemoveVGMFile(VGMFile *theFile, bool bRemoveFromRaw = true);
    void AddVGMColl(VGMColl *theColl);
    void RemoveVGMColl(VGMColl *theFile);
    void AddScanner(const std::string &formatname);

    virtual void UI_SetRootPtr(VGMRoot **theRoot) = 0;
    virtual void UI_PreExit() {}
    virtual void UI_Exit() = 0;
    virtual void UI_AddRawFile(RawFile *newFile) {}
    virtual void UI_CloseRawFile(RawFile *targFile) {}

    virtual void UI_OnBeginScan() {}
    virtual void UI_SetScanInfo() {}
    virtual void UI_OnEndScan() {}
    virtual void UI_AddVGMFile(VGMFile *theFile);
    virtual void UI_AddVGMSeq(VGMSeq *theSeq) {}
    virtual void UI_AddVGMInstrSet(VGMInstrSet *theInstrSet) {}
    virtual void UI_AddVGMSampColl(VGMSampColl *theSampColl) {}
    virtual void UI_AddVGMMisc(VGMMiscFile *theMiscFile) {}
    virtual void UI_AddVGMColl(VGMColl *theColl) {}
    virtual void UI_RemoveVGMFile(VGMFile *theFile) {}
    virtual void UI_BeginRemoveVGMFiles() {}
    virtual void UI_EndRemoveVGMFiles() {}
    virtual void UI_RemoveVGMColl(VGMColl *theColl) {}
    // virtual void UI_RemoveVGMFileRange(VGMFile* first, VGMFile* last) {}
    virtual void UI_AddItem(VGMItem *item, VGMItem *parent, const std::string &itemName,
                            void *UI_specific) {}
    virtual void UI_AddItemSet(void *UI_specific, std::vector<ItemSet> *itemset) {}
    virtual std::string UI_GetOpenFilePath(const std::string &suggestedFilename = "",
                                           const std::string &extension = "") = 0;
    virtual std::string UI_GetSaveFilePath(const std::string &suggestedFilename,
                                           const std::string &extension = "") = 0;
    virtual std::string UI_GetSaveDirPath(const std::string &suggestedDir = "") = 0;
    virtual bool UI_WriteBufferToFile(const std::string &filepath, uint8_t *buf, uint32_t size);

    bool SaveAllAsRaw();

    std::vector<RawFile *> vRawFile;
    std::vector<VGMFile *> vVGMFile;
    std::vector<VGMColl *> vVGMColl;

    std::vector<VGMScanner *> vScanner;

   private:
    std::vector<std::shared_ptr<RawFile>> m_activefiles;
    std::vector<std::shared_ptr<VGMColl>> m_colls;
};

extern VGMRoot *pRoot;
