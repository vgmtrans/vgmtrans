/*
 * VGMCis (c) 2002-2019
 * Licensed under the zlib license,
 * refer to the included LICENSE.txt file
 */

#pragma once

#include <QObject>
#include "Root.h"

class QtVGMRoot : public QObject, public VGMRoot {
    Q_OBJECT

   public:
    void UI_SetRootPtr(VGMRoot **theRoot) override;
    void UI_PreExit() override;
    void UI_Exit() override;
    void UI_AddRawFile(RawFile *newFile) override;
    virtual void UI_CloseRawFile(RawFile *targFile);

    virtual void UI_OnBeginScan();
    virtual void UI_SetScanInfo();
    virtual void UI_OnEndScan();
    void UI_AddVGMFile(std::variant<VGMSeq *, VGMInstrSet *, VGMSampColl *, VGMMiscFile *> file) override;
    void UI_AddVGMSeq(VGMSeq *theSeq) override;
    void UI_AddVGMInstrSet(VGMInstrSet *theInstrSet) override;
    void UI_AddVGMSampColl(VGMSampColl *theSampColl) override;
    void UI_AddVGMMisc(VGMMiscFile *theMiscFile) override;
    void UI_AddVGMColl(VGMColl *theColl) override;
    void UI_RemoveVGMColl(VGMColl *targColl) override;
    virtual void UI_BeginRemoveVGMFiles();
    virtual void UI_EndRemoveVGMFiles();
    std::string UI_GetSaveFilePath(const std::string &suggestedFilename,
                                           const std::string &extension = "") override;
    std::string UI_GetSaveDirPath(const std::string &suggestedDir = "") override;

   signals:
    void UI_AddedRawFile();
    void UI_RemovedRawFile();
    void UI_AddedVGMFile();
    void UI_RemovedVGMFile();
    void UI_AddedVGMColl();
    void UI_RemovedVGMColl();
    void UI_RemoveVGMFile(VGMFile *targFile);
    void UI_AddItem(VGMItem *item, VGMItem *parent, const std::string &itemName, void *UI_specific) override;
};

extern QtVGMRoot qtVGMRoot;
